#pragma once
#include <cstdint>

namespace esphome {
namespace tfa_sky {

// Raw RF signal acquisition (the "sensor" layer). Configures the CC1101 for
// OOK/ASK reception and, once RSSI indicates an active transmission,
// captures the pulse-length burst on GDO0 into a buffer. Knows nothing
// about the TFA frame layout - decoding those pulses is tfa_sky_decoder's
// job.
//
// Capture is interrupt driven and non-blocking: poll() only arms/collects a
// capture window, the edges themselves are timestamped in an ISR. That keeps
// the main loop (WiFi stack, API, OTA) running while a burst is recorded.
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
  // A healthy SPI status read takes a few microseconds. The driver waits up
  // to 200ms for the chip to become ready - anything near that means the
  // CC1101 is hung or gone.
  static constexpr uint32_t SPI_STALL_US = 50000UL;

  // Configures the CC1101 and puts it into RX. Can be called again at any
  // time (outside of a capture) to recover a chip that lost its settings,
  // e.g. after a brown-out. Updates chip_present().
  void setup(const Config &config);

  // Non-blocking. When idle, reads RSSI and, if above threshold, arms an
  // interrupt-driven capture window. Returns true once a capture window has
  // finished, with *count set to the number of pulses in buffer(). Returns
  // false while idle/capturing. Does nothing while the chip is missing.
  bool poll(uint16_t *count);

  // Reads back the configuration registers and the radio state machine and
  // compares them to what setup() left behind. Returns false if the chip
  // doesn't answer, lost its configuration or isn't in RX anymore. Must not
  // be called while capturing() (returns true without checking then).
  bool check_health();

  bool capturing() const { return capturing_; }
  // Set when an SPI access took suspiciously long (chip hung) - the caller
  // should re-run setup(). Cleared by setup().
  bool spi_stalled() const { return spi_stalled_; }

  const uint16_t *buffer() const { return buffer_; }
  bool chip_present() const { return chip_present_; }
  int16_t last_rssi_dbm() const { return last_rssi_dbm_; }

 private:
  static void gdo0_isr(void *arg);
  void stop_capture_();

  Config config_{};
  bool initialized_ = false;
  bool chip_present_ = false;
  bool spi_stalled_ = false;
  int16_t last_rssi_dbm_ = -128;

  // Register snapshot taken right after setup(), compared by check_health().
  uint8_t expected_iocfg0_ = 0;
  uint8_t expected_pktctrl0_ = 0;
  uint8_t expected_mdmcfg2_ = 0;
  uint8_t expected_freq_[3] = {0, 0, 0};

  // Shared with the ISR. buffer_ is only written by the ISR while capturing_
  // and only read after the interrupt has been detached again.
  volatile bool capturing_ = false;
  volatile uint16_t pulse_count_ = 0;
  volatile uint32_t last_edge_us_ = 0;
  volatile uint32_t capture_start_us_ = 0;
  uint16_t buffer_[MAX_PULSES];
};

} // namespace tfa_sky
} // namespace esphome
