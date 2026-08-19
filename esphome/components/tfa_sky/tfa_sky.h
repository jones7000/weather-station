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
class TfaSkyComponent : public Component {
 public:
  void set_pins(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t csn, uint8_t gdo0);
  void set_frequency(float mhz) { frequency_mhz_ = mhz; }
  void set_rssi_threshold(int16_t dbm) { rssi_threshold_dbm_ = dbm; }
  void set_sensor_id(uint8_t id) {
    expected_id_ = id;
    has_expected_id_ = true;
  }

  void set_temperature_sensor(sensor::Sensor *s) { temperature_sensor_ = s; }
  void set_humidity_sensor(sensor::Sensor *s) { humidity_sensor_ = s; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 private:
  Cc1101Receiver receiver_;
  Cc1101Receiver::Config config_{};
  float frequency_mhz_ = 433.92f;
  int16_t rssi_threshold_dbm_ = -70;
  bool has_expected_id_ = false;
  uint8_t expected_id_ = 0;

  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *humidity_sensor_{nullptr};
};

} // namespace tfa_sky
} // namespace esphome
