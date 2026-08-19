#include "cc1101_receiver.h"
#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

namespace esphome {
namespace tfa_sky {

void Cc1101Receiver::setup(const Config &config) {
  config_ = config;

  ELECHOUSE_cc1101.setSpiPin(config_.sck_pin, config_.miso_pin, config_.mosi_pin, config_.csn_pin);
  ELECHOUSE_cc1101.setGDO0(config_.gdo0_pin);
  ELECHOUSE_cc1101.Init();

  chip_present_ = ELECHOUSE_cc1101.getCC1101();

  ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
  ELECHOUSE_cc1101.setRxBW(200);     // kHz
  ELECHOUSE_cc1101.setCCMode(0);     // GDO0 = raw async demodulated bitstream
  ELECHOUSE_cc1101.SetRx(config_.frequency_mhz);

  pinMode(config_.gdo0_pin, INPUT);
}

bool Cc1101Receiver::poll(uint16_t *count) {
  int rssi = ELECHOUSE_cc1101.getRssi();
  last_rssi_dbm_ = (int16_t) rssi;
  if (rssi <= config_.rssi_threshold_dbm) {
    return false;
  }

  uint16_t n = 0;
  uint32_t start = micros();
  uint32_t lastEdge = start;
  int lastLevel = digitalRead(config_.gdo0_pin);

  while ((micros() - start) < CAPTURE_WINDOW_US && n < MAX_PULSES) {
    int lvl = digitalRead(config_.gdo0_pin);
    if (lvl != lastLevel) {
      uint32_t now = micros();
      uint32_t delta = now - lastEdge;
      lastEdge = now;
      lastLevel = lvl;
      if (delta > MIN_PULSE_US) {
        buffer_[n++] = (delta > 65535) ? 65535 : (uint16_t) delta;
      }
    }
  }

  *count = n;
  return true;
}

} // namespace tfa_sky
} // namespace esphome
