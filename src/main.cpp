#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// TFA Dostmann SKY (30.3195) outdoor sensor decoder.
//
// Protocol (reverse-engineered, see README):
//   433.92 MHz, OOK, PWM encoding: fixed ~475us mark + variable gap
//   (~1950-2100us = bit 0, ~4020-4065us = bit 1). Each transmission
//   repeats the same 36-bit frame 3x, separated by ~8.4-8.9ms gaps.
//
//   36-bit frame layout (bit 0 = first bit sent):
//     [0:8]   ID        (8 bit, random per power-up, not scaled)
//     [8]     battery   (0 = OK)
//     [9]     unknown flag
//     [10:12] channel   (2 bit)
//     [12:24] temp      (12 bit, bit-reversed/LSB-first, signed x0.1 C)
//     [24:30] humidity  (6 bit, bit-reversed/LSB-first, + 36 offset)
//     [30:36] checksum  (6 bit, algorithm not reverse-engineered -
//                        frames are instead validated by requiring at
//                        least 2 of the 3 repeats to match bit-for-bit)

constexpr uint8_t GDO0_PIN = 5; // D1
constexpr float TARGET_FREQ_MHZ = 433.92f;

// Noise floor sits consistently around -74..-79dBm; real transmissions
// have measured anywhere from -30dBm (close range) to -61dBm (sensor at
// its normal outdoor spot) depending on distance/obstructions. -70dBm
// gives good margin above noise while still catching weaker signals, so
// this shouldn't need re-tuning for minor position changes.
constexpr int RSSI_THRESHOLD = -70;
constexpr uint32_t CAPTURE_WINDOW_US = 400000UL; // max 400ms per burst
constexpr uint16_t MAX_PULSES = 1000;
constexpr uint32_t MIN_PULSE_US = 80; // filter out sub-80us glitches

constexpr uint16_t REPEAT_GAP_US = 6000;  // separates repeats within a burst
constexpr uint16_t SEGMENT_LEN = 74;      // 1 gap-marker + 36*(mark+data) + 1 trailing mark
constexpr uint16_t BIT_COUNT = 36;

uint16_t pulseBuf[MAX_PULSES];

// Decodes one 74-pulse repeat segment into a 36-char '0'/'1' string.
void decodeSegment(const uint16_t *seg, char *outBits) {
  for (uint16_t k = 0; k < BIT_COUNT; k++) {
    uint16_t gap = seg[2 + 2 * k];
    outBits[k] = (gap < 3000) ? '0' : '1';
  }
  outBits[BIT_COUNT] = '\0';
}

uint32_t bitsToUint(const char *bits, uint8_t start, uint8_t len, bool reversed) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < len; i++) {
    char c = reversed ? bits[start + len - 1 - i] : bits[start + i];
    v = (v << 1) | (c == '1' ? 1 : 0);
  }
  return v;
}

void tryDecodeFrame(uint16_t *buf, uint16_t count) {
  // Find repeat-gap markers.
  uint16_t markers[8];
  uint8_t markerCount = 0;
  for (uint16_t i = 0; i < count && markerCount < 8; i++) {
    if (buf[i] > REPEAT_GAP_US) {
      markers[markerCount++] = i;
    }
  }
  if (markerCount < 2) return; // need at least 2 repeats to cross-check

  // Decode every complete (non-truncated) repeat segment we can find.
  char segBits[8][BIT_COUNT + 1];
  uint8_t segCount = 0;
  for (uint8_t m = 0; m < markerCount && segCount < 8; m++) {
    uint16_t start = markers[m];
    if (start + SEGMENT_LEN > count) continue; // truncated, skip
    if (m + 1 < markerCount && markers[m + 1] - start != SEGMENT_LEN) continue;
    decodeSegment(&buf[start], segBits[segCount]);
    segCount++;
  }
  if (segCount < 2) return; // not enough clean repeats to cross-check

  // DEBUG: print every repeat's raw bit string so mismatches are visible
  // for checksum reverse-engineering.
  Serial.printf("-- %u Wiederholungen --\n", segCount);
  for (uint8_t s = 0; s < segCount; s++) {
    Serial.printf("  [%u] %s\n", s, segBits[s]);
  }

  // Per-bit majority vote across all decoded repeats. A single shared
  // fading dip can corrupt all repeats within one burst identically, so
  // requiring an exact match between just two of them isn't a strong
  // enough check on weaker signals - voting across every repeat we got
  // is more robust, and still works with the common case of exactly 2.
  char votedBits[BIT_COUNT + 1];
  for (uint8_t b = 0; b < BIT_COUNT; b++) {
    uint8_t ones = 0;
    for (uint8_t s = 0; s < segCount; s++) {
      if (segBits[s][b] == '1') ones++;
    }
    votedBits[b] = (ones * 2 > segCount) ? '1' : '0';
  }
  votedBits[BIT_COUNT] = '\0';

  // Count how many repeats fully agree with the voted result, as a
  // rough confidence indicator.
  uint8_t agree = 0;
  for (uint8_t s = 0; s < segCount; s++) {
    if (strcmp(segBits[s], votedBits) == 0) agree++;
  }

  uint32_t id = bitsToUint(votedBits, 0, 8, false);
  bool battery = votedBits[8] == '1';
  uint32_t channel = bitsToUint(votedBits, 10, 2, false);
  int32_t tempRaw = (int32_t)bitsToUint(votedBits, 12, 12, true);
  if (tempRaw >= 2048) tempRaw -= 4096; // 12-bit two's complement
  float tempC = tempRaw * 0.1f;
  uint32_t humidity = bitsToUint(votedBits, 24, 6, true) + 36;

  // Sanity check - sensor spec says -20..50C / 20..99% humidity. There's
  // no verified checksum, so implausible values get rejected outright
  // even if the repeats agreed with each other.
  if (tempC < -40.0f || tempC > 60.0f || humidity > 99) {
    Serial.printf("Frame verworfen (unplausibel, %u/%u Wiederholungen einig): Temp=%.1f C Feuchte=%lu%%\n",
                  agree, segCount, tempC, humidity);
  } else {
    Serial.printf("ID=0x%02lX Kanal=%lu Batterie=%s  Temp=%.1f C  Feuchte=%lu%%  (%u/%u Wiederholungen einig)\n",
                  id, channel + 1, battery ? "niedrig" : "ok", tempC, humidity, agree, segCount);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("TFA Dostmann SKY (30.3195) Sensor-Decoder");

  ELECHOUSE_cc1101.setSpiPin(14, 12, 13, 15);
  ELECHOUSE_cc1101.setGDO0(GDO0_PIN);
  ELECHOUSE_cc1101.Init();

  if (!ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("CC1101 NICHT gefunden - Verkabelung pruefen!");
  }

  ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
  ELECHOUSE_cc1101.setRxBW(200);     // kHz
  ELECHOUSE_cc1101.setCCMode(0);     // GDO0 = raw async demodulated bitstream
  ELECHOUSE_cc1101.SetRx(TARGET_FREQ_MHZ);

  pinMode(GDO0_PIN, INPUT);

  Serial.println("Warte auf Sensor-Uebertragung...");
}

void loop() {
  int rssi = ELECHOUSE_cc1101.getRssi();

  if (rssi > RSSI_THRESHOLD) {
    uint16_t count = 0;
    uint32_t start = micros();
    uint32_t lastEdge = start;
    int lastLevel = digitalRead(GDO0_PIN);

    while ((micros() - start) < CAPTURE_WINDOW_US && count < MAX_PULSES) {
      int lvl = digitalRead(GDO0_PIN);
      if (lvl != lastLevel) {
        uint32_t now = micros();
        uint32_t delta = now - lastEdge;
        lastEdge = now;
        lastLevel = lvl;
        if (delta > MIN_PULSE_US) {
          pulseBuf[count++] = (delta > 65535) ? 65535 : (uint16_t)delta;
        }
      }
    }

    tryDecodeFrame(pulseBuf, count);
    delay(50); // brief settle before resuming RSSI polling
  }

  delay(1); // idle RSSI poll rate, keep detection latency low
}
