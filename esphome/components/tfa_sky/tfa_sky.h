#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "cc1101_receiver.h"
#include "tfa_sky_decoder.h"

namespace esphome {
namespace tfa_sky {

// The "esphome" layer: owns a Cc1101Receiver (sensor) and calls
// decode_frame() (decoding) every loop, then publishes plausible readings
// to Home Assistant via the usual ESPHome sensor::Sensor state machine.
// Contains no protocol or radio knowledge of its own.
//
// Also supervises the radio: the CC1101 is periodically checked and
// re-initialised if it stopped answering or lost its configuration, and
// readings are marked unknown (NaN) if nothing valid arrived for `timeout`.
class TfaSkyComponent : public Component {
 public:
  static constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 60000UL;

  void set_pins(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t csn, uint8_t gdo0);
  void set_frequency(float mhz) { frequency_mhz_ = mhz; }
  void set_rssi_threshold(int16_t dbm) { rssi_threshold_dbm_ = dbm; }
  void set_sensor_id(uint8_t id) {
    expected_id_ = id;
    has_expected_id_ = true;
  }
  void set_timeout(uint32_t ms) { timeout_ms_ = ms; }

  void set_temperature_sensor(sensor::Sensor *s) { temperature_sensor_ = s; }
  void set_humidity_sensor(sensor::Sensor *s) { humidity_sensor_ = s; }
  void set_signal_strength_sensor(sensor::Sensor *s) { signal_strength_sensor_ = s; }
  void set_reinit_count_sensor(sensor::Sensor *s) { reinit_count_sensor_ = s; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 private:
  void supervise_(uint32_t now);
  void reinit_radio_(const char *reason);
  void handle_frame_(uint16_t count, uint32_t now);

  Cc1101Receiver receiver_;
  Cc1101Receiver::Config config_{};
  float frequency_mhz_ = 433.92f;
  int16_t rssi_threshold_dbm_ = -70;
  bool has_expected_id_ = false;
  uint8_t expected_id_ = 0;
  uint32_t timeout_ms_ = 15 * 60 * 1000UL;

  uint32_t last_health_check_ms_ = 0;
  uint32_t last_valid_frame_ms_ = 0;
  bool timed_out_ = false;
  uint32_t reinit_count_ = 0;

  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *humidity_sensor_{nullptr};
  sensor::Sensor *signal_strength_sensor_{nullptr};
  sensor::Sensor *reinit_count_sensor_{nullptr};
};

} // namespace tfa_sky
} // namespace esphome
