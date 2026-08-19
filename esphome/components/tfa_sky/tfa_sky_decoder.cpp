#include "tfa_sky_decoder.h"
#include <cstring>

namespace esphome {
namespace tfa_sky {

namespace {

constexpr uint16_t REPEAT_GAP_US = 6000; // separates repeats within a burst
constexpr uint16_t SEGMENT_LEN = 74;     // 1 gap-marker + 36*(mark+data) + 1 trailing mark
constexpr uint16_t BIT_COUNT = 36;

// Decodes one 74-pulse repeat segment into a 36-char '0'/'1' string.
void decode_segment(const uint16_t *seg, char *out_bits) {
  for (uint16_t k = 0; k < BIT_COUNT; k++) {
    uint16_t gap = seg[2 + 2 * k];
    out_bits[k] = (gap < 3000) ? '0' : '1';
  }
  out_bits[BIT_COUNT] = '\0';
}

uint32_t bits_to_uint(const char *bits, uint8_t start, uint8_t len, bool reversed) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < len; i++) {
    char c = reversed ? bits[start + len - 1 - i] : bits[start + i];
    v = (v << 1) | (c == '1' ? 1 : 0);
  }
  return v;
}

} // namespace

DecodedFrame decode_frame(const uint16_t *pulses, uint16_t count) {
  DecodedFrame frame{};

  // Find repeat-gap markers.
  uint16_t markers[8];
  uint8_t marker_count = 0;
  for (uint16_t i = 0; i < count && marker_count < 8; i++) {
    if (pulses[i] > REPEAT_GAP_US) {
      markers[marker_count++] = i;
    }
  }
  if (marker_count < 2) return frame; // need at least 2 repeats to cross-check

  // Decode every complete (non-truncated) repeat segment we can find.
  char seg_bits[8][BIT_COUNT + 1];
  uint8_t seg_count = 0;
  for (uint8_t m = 0; m < marker_count && seg_count < 8; m++) {
    uint16_t start = markers[m];
    if (start + SEGMENT_LEN > count) continue; // truncated, skip
    if (m + 1 < marker_count && markers[m + 1] - start != SEGMENT_LEN) continue;
    decode_segment(&pulses[start], seg_bits[seg_count]);
    seg_count++;
  }
  if (seg_count < 2) return frame; // not enough clean repeats to cross-check

  // Per-bit majority vote across all decoded repeats. A single shared
  // fading dip can corrupt all repeats within one burst identically, so
  // requiring an exact match between just two of them isn't a strong
  // enough check on weaker signals - voting across every repeat we got
  // is more robust, and still works with the common case of exactly 2.
  char voted_bits[BIT_COUNT + 1];
  for (uint8_t b = 0; b < BIT_COUNT; b++) {
    uint8_t ones = 0;
    for (uint8_t s = 0; s < seg_count; s++) {
      if (seg_bits[s][b] == '1') ones++;
    }
    voted_bits[b] = (ones * 2 > seg_count) ? '1' : '0';
  }
  voted_bits[BIT_COUNT] = '\0';

  // Count how many repeats fully agree with the voted result, as a rough
  // confidence indicator.
  uint8_t agree = 0;
  for (uint8_t s = 0; s < seg_count; s++) {
    if (strcmp(seg_bits[s], voted_bits) == 0) agree++;
  }

  uint32_t id = bits_to_uint(voted_bits, 0, 8, false);
  bool battery = voted_bits[8] == '1';
  uint32_t channel = bits_to_uint(voted_bits, 10, 2, false);
  int32_t temp_raw = (int32_t) bits_to_uint(voted_bits, 12, 12, true);
  if (temp_raw >= 2048) temp_raw -= 4096; // 12-bit two's complement
  float temp_c = temp_raw * 0.1f;
  uint32_t humidity = bits_to_uint(voted_bits, 24, 6, true) + 36;

  frame.id = (uint8_t) id;
  frame.battery_low = battery;
  frame.channel = (uint8_t) (channel + 1);
  frame.temperature_c = temp_c;
  frame.humidity_pct = (uint8_t) humidity;
  frame.repeats_agreeing = agree;
  frame.repeats_total = seg_count;

  // Sanity check - sensor spec says -20..50C / 20..99% humidity. There's
  // no verified checksum, so implausible values get rejected outright even
  // if the repeats agreed with each other.
  if (temp_c < -40.0f || temp_c > 60.0f || humidity > 99) {
    frame.implausible = true;
    return frame;
  }

  frame.valid = true;
  return frame;
}

} // namespace tfa_sky
} // namespace esphome
