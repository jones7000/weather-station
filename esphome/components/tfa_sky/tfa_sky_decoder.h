#pragma once
#include <cstdint>

namespace esphome {
namespace tfa_sky {

// TFA Dostmann SKY (30.3195) outdoor sensor frame decoder (the "decoding"
// layer). Pure logic, no hardware/Arduino/ESPHome dependency - takes the
// pulse-length burst captured by Cc1101Receiver and turns it into a reading.
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

struct DecodedFrame {
  bool valid = false;         // true if a plausible frame was decoded
  bool implausible = false;   // decoded but rejected by the range sanity check
  uint8_t id = 0;
  bool battery_low = false;
  uint8_t channel = 0;
  float temperature_c = 0;
  uint8_t humidity_pct = 0;
  uint8_t repeats_agreeing = 0;
  uint8_t repeats_total = 0;
};

// Attempts to find and decode a TFA SKY frame in a burst of pulse-length
// timings (as captured by Cc1101Receiver::buffer()). Returns a frame with
// valid=false if fewer than 2 clean repeats were found to cross-check.
DecodedFrame decode_frame(const uint16_t *pulses, uint16_t count);

} // namespace tfa_sky
} // namespace esphome
