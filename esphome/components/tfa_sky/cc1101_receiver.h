#pragma once
#include <cstdint>

namespace esphome {
namespace tfa_sky {

// Raw RF signal acquisition (the "sensor" layer). Configures the CC1101 for
// OOK/ASK reception and, once RSSI indicates an active transmission,
// captures the pulse-length burst on GDO0 into a buffer. Knows nothing
// about the TFA frame layout - decoding those pulses is tfa_sky_decoder's
// job.
class Cc1101Receiver {
 public:
  struct Config {
    uint8_t sck_pin;
    uint8_t miso_pin;
    uint8_t mosi_pin;
    uint8_t csn_pin;
    uint8_t gdo0_pin;
    float frequency_mhz;
    int16_t rssi_threshold_dbm;
  };

  static constexpr uint32_t CAPTURE_WINDOW_US = 400000UL; // max 400ms per burst
  static constexpr uint16_t MAX_PULSES = 1000;
  static constexpr uint32_t MIN_PULSE_US = 80; // filter out sub-80us glitches

  void setup(const Config &config);

  // Polls RSSI; if a transmission is detected, busy-captures pulses into
  // the internal buffer (blocking up to CAPTURE_WINDOW_US) and returns true
  // with *count set to the number of pulses recorded. Returns false
  // immediately if RSSI is below threshold.
  bool poll(uint16_t *count);

  const uint16_t *buffer() const { return buffer_; }
  bool chip_present() const { return chip_present_; }

 private:
  Config config_{};
  uint16_t buffer_[MAX_PULSES];
  bool chip_present_ = false;
};

} // namespace tfa_sky
} // namespace esphome
