#include "tfa_sky.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cmath>

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

  // Not fatal: the CC1101 may just have come up later than the ESP (or lost
  // power briefly). supervise_() keeps retrying every health-check interval.
  if (!receiver_.chip_present()) {
    ESP_LOGE(TAG, "CC1101 nicht gefunden - Verkabelung pruefen, neuer Versuch in %us",
             (unsigned) (HEALTH_CHECK_INTERVAL_MS / 1000));
    this->status_set_error();
  }

  uint32_t now = millis();
  last_health_check_ms_ = now;
  last_valid_frame_ms_ = now;
  if (reinit_count_sensor_ != nullptr) reinit_count_sensor_->publish_state(0);
}

void TfaSkyComponent::loop() {
  uint32_t now = millis();

  uint16_t count = 0;
  if (receiver_.poll(&count)) {
    handle_frame_(count, now);
  }

  supervise_(now);
}

void TfaSkyComponent::handle_frame_(uint16_t count, uint32_t now) {
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

  last_valid_frame_ms_ = now;
  timed_out_ = false;

  if (temperature_sensor_ != nullptr) temperature_sensor_->publish_state(frame.temperature_c);
  if (humidity_sensor_ != nullptr) humidity_sensor_->publish_state(frame.humidity_pct);
  if (signal_strength_sensor_ != nullptr) signal_strength_sensor_->publish_state(receiver_.last_rssi_dbm());
}

void TfaSkyComponent::supervise_(uint32_t now) {
  if (receiver_.capturing()) return; // never touch SPI while a burst is being recorded

  if (receiver_.spi_stalled()) {
    reinit_radio_("SPI-Zugriff haengt");
    last_health_check_ms_ = now;
    return;
  }

  // No valid reading for too long: mark values unknown in HA instead of
  // showing a stale value forever, and give the radio a fresh start in
  // case it's deaf without the health check noticing.
  if (!timed_out_ && timeout_ms_ > 0 && now - last_valid_frame_ms_ > timeout_ms_) {
    timed_out_ = true;
    ESP_LOGW(TAG, "Seit %us kein gueltiger Frame empfangen - Werte werden als unbekannt gemeldet",
             (unsigned) (timeout_ms_ / 1000));
    if (temperature_sensor_ != nullptr) temperature_sensor_->publish_state(NAN);
    if (humidity_sensor_ != nullptr) humidity_sensor_->publish_state(NAN);
    if (signal_strength_sensor_ != nullptr) signal_strength_sensor_->publish_state(NAN);
    reinit_radio_("Empfangs-Timeout");
    last_health_check_ms_ = now;
    return;
  }

  if (now - last_health_check_ms_ < HEALTH_CHECK_INTERVAL_MS) return;
  last_health_check_ms_ = now;

  if (!receiver_.check_health()) {
    reinit_radio_(receiver_.chip_present() ? "Konfiguration verloren / nicht im RX-Modus" : "Chip antwortet nicht");
  }
}

void TfaSkyComponent::reinit_radio_(const char *reason) {
  ESP_LOGW(TAG, "CC1101 wird neu initialisiert (%s)", reason);
  receiver_.setup(config_);
  reinit_count_++;
  if (reinit_count_sensor_ != nullptr) reinit_count_sensor_->publish_state(reinit_count_);

  if (receiver_.chip_present()) {
    this->status_clear_error();
  } else {
    ESP_LOGE(TAG, "CC1101 nach Neu-Initialisierung weiterhin nicht erreichbar");
    this->status_set_error();
  }
}

void TfaSkyComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "TFA Dostmann SKY (30.3195):");
  ESP_LOGCONFIG(TAG, "  Frequenz: %.2f MHz", frequency_mhz_);
  ESP_LOGCONFIG(TAG, "  RSSI-Schwelle: %d dBm", rssi_threshold_dbm_);
  ESP_LOGCONFIG(TAG, "  Timeout: %us", (unsigned) (timeout_ms_ / 1000));
  if (has_expected_id_) {
    ESP_LOGCONFIG(TAG, "  Erwartete Sensor-ID: 0x%02X", expected_id_);
  }
  LOG_SENSOR("  ", "Temperature", temperature_sensor_);
  LOG_SENSOR("  ", "Humidity", humidity_sensor_);
  LOG_SENSOR("  ", "Signal Strength", signal_strength_sensor_);
  LOG_SENSOR("  ", "Reinit Count", reinit_count_sensor_);
  if (!receiver_.chip_present()) {
    ESP_LOGE(TAG, "  CC1101 nicht erreichbar!");
  }
}

} // namespace tfa_sky
} // namespace esphome
