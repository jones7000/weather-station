#include "tfa_sky.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tfa_sky {

static const char *const TAG = "tfa_sky";

void TfaSkyComponent::set_pins(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t csn, uint8_t gdo0) {
  config_.sck_pin = sck;
  config_.miso_pin = miso;
  config_.mosi_pin = mosi;
  config_.csn_pin = csn;
  config_.gdo0_pin = gdo0;
}

void TfaSkyComponent::setup() {
  config_.frequency_mhz = frequency_mhz_;
  config_.rssi_threshold_dbm = rssi_threshold_dbm_;
  receiver_.setup(config_);

  if (!receiver_.chip_present()) {
    ESP_LOGE(TAG, "CC1101 nicht gefunden - Verkabelung pruefen");
    this->mark_failed();
  }
}

void TfaSkyComponent::loop() {
  uint16_t count = 0;
  if (!receiver_.poll(&count)) return;

  DecodedFrame frame = decode_frame(receiver_.buffer(), count);

  if (!frame.valid) {
    if (frame.implausible) {
      ESP_LOGW(TAG, "Frame verworfen (unplausibel, %u/%u Wiederholungen einig): Temp=%.1f Feuchte=%u%%",
                frame.repeats_agreeing, frame.repeats_total, frame.temperature_c, frame.humidity_pct);
    }
    return;
  }

  if (has_expected_id_ && frame.id != expected_id_) {
    ESP_LOGD(TAG, "Frame von anderem Sensor ignoriert (ID=0x%02X, erwartet 0x%02X)", frame.id, expected_id_);
    return;
  }

  ESP_LOGD(TAG, "ID=0x%02X Kanal=%u Batterie=%s Temp=%.1f Feuchte=%u%% (%u/%u Wiederholungen einig)", frame.id,
           frame.channel, frame.battery_low ? "niedrig" : "ok", frame.temperature_c, frame.humidity_pct,
           frame.repeats_agreeing, frame.repeats_total);

  if (temperature_sensor_ != nullptr) temperature_sensor_->publish_state(frame.temperature_c);
  if (humidity_sensor_ != nullptr) humidity_sensor_->publish_state(frame.humidity_pct);
  if (signal_strength_sensor_ != nullptr) signal_strength_sensor_->publish_state(receiver_.last_rssi_dbm());
}

void TfaSkyComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "TFA Dostmann SKY (30.3195):");
  ESP_LOGCONFIG(TAG, "  Frequenz: %.2f MHz", frequency_mhz_);
  ESP_LOGCONFIG(TAG, "  RSSI-Schwelle: %d dBm", rssi_threshold_dbm_);
  if (has_expected_id_) {
    ESP_LOGCONFIG(TAG, "  Erwartete Sensor-ID: 0x%02X", expected_id_);
  }
  LOG_SENSOR("  ", "Temperature", temperature_sensor_);
  LOG_SENSOR("  ", "Humidity", humidity_sensor_);
  LOG_SENSOR("  ", "Signal Strength", signal_strength_sensor_);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  CC1101 Init fehlgeschlagen!");
  }
}

} // namespace tfa_sky
} // namespace esphome
