#include "cc1101_receiver.h"
#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

namespace esphome {
namespace tfa_sky {

namespace {
constexpr uint8_t MARCSTATE_RX = 0x0D;
} // namespace

void Cc1101Receiver::setup(const Config &config) {
  if (capturing_) stop_capture_();
  config_ = config;
  spi_stalled_ = false;

  // On a re-init, probe with a single status read first: if the chip doesn't
  // answer, Init() would wait the driver's 200ms MISO timeout for every one
  // of its ~25 register writes and trip the ESP8266 watchdog. A chip that
  // doesn't answer this can't be recovered over SPI anyway (power cycle).
  if (initialized_ && !ELECHOUSE_cc1101.getCC1101()) {
    chip_present_ = false;
    return;
  }
  initialized_ = true;

  ELECHOUSE_cc1101.setSpiPin(config_.sck_pin, config_.miso_pin, config_.mosi_pin, config_.csn_pin);
  ELECHOUSE_cc1101.setGDO0(config_.gdo0_pin);
  ELECHOUSE_cc1101.Init();

  chip_present_ = ELECHOUSE_cc1101.getCC1101();
  if (!chip_present_) return;

  ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
  ELECHOUSE_cc1101.setRxBW(200);     // kHz
  ELECHOUSE_cc1101.setCCMode(0);     // GDO0 = raw async demodulated bitstream
  ELECHOUSE_cc1101.SetRx(config_.frequency_mhz);

  pinMode(config_.gdo0_pin, INPUT);

  expected_iocfg0_ = ELECHOUSE_cc1101.SpiReadReg(CC1101_IOCFG0);
  expected_pktctrl0_ = ELECHOUSE_cc1101.SpiReadReg(CC1101_PKTCTRL0);
  expected_mdmcfg2_ = ELECHOUSE_cc1101.SpiReadReg(CC1101_MDMCFG2);
  expected_freq_[0] = ELECHOUSE_cc1101.SpiReadReg(CC1101_FREQ2);
  expected_freq_[1] = ELECHOUSE_cc1101.SpiReadReg(CC1101_FREQ1);
  expected_freq_[2] = ELECHOUSE_cc1101.SpiReadReg(CC1101_FREQ0);
}

void IRAM_ATTR Cc1101Receiver::gdo0_isr(void *arg) {
  auto *self = static_cast<Cc1101Receiver *>(arg);
  uint32_t now = micros();
  if (now - self->capture_start_us_ >= CAPTURE_WINDOW_US) return; // window over, loop will collect

  uint32_t delta = now - self->last_edge_us_;
  self->last_edge_us_ = now;
  uint16_t n = self->pulse_count_;
  if (delta > MIN_PULSE_US && n < MAX_PULSES) {
    self->buffer_[n] = (delta > 65535) ? 65535 : (uint16_t) delta;
    self->pulse_count_ = n + 1;
  }
}

void Cc1101Receiver::stop_capture_() {
  detachInterrupt(digitalPinToInterrupt(config_.gdo0_pin));
  capturing_ = false;
}

bool Cc1101Receiver::poll(uint16_t *count) {
  if (capturing_) {
    if ((micros() - capture_start_us_) < CAPTURE_WINDOW_US && pulse_count_ < MAX_PULSES) {
      return false; // still recording in the background
    }
    stop_capture_();
    *count = pulse_count_;
    return true;
  }

  if (!chip_present_ || spi_stalled_) return false;

  uint32_t t0 = micros();
  int rssi = ELECHOUSE_cc1101.getRssi();
  if (micros() - t0 > SPI_STALL_US) {
    // Chip didn't answer in time - reading is garbage, and every further
    // access would block the loop the same way. Let the owner re-init.
    spi_stalled_ = true;
    return false;
  }

  last_rssi_dbm_ = (int16_t) rssi;
  if (rssi <= config_.rssi_threshold_dbm) {
    return false;
  }

  pulse_count_ = 0;
  uint32_t now = micros();
  capture_start_us_ = now;
  last_edge_us_ = now;
  capturing_ = true;
  attachInterruptArg(digitalPinToInterrupt(config_.gdo0_pin), &Cc1101Receiver::gdo0_isr, this, CHANGE);
  return false;
}

bool Cc1101Receiver::check_health() {
  if (capturing_) return true;

  chip_present_ = ELECHOUSE_cc1101.getCC1101();
  if (!chip_present_) return false;

  if (ELECHOUSE_cc1101.SpiReadReg(CC1101_IOCFG0) != expected_iocfg0_) return false;
  if (ELECHOUSE_cc1101.SpiReadReg(CC1101_PKTCTRL0) != expected_pktctrl0_) return false;
  if (ELECHOUSE_cc1101.SpiReadReg(CC1101_MDMCFG2) != expected_mdmcfg2_) return false;
  if (ELECHOUSE_cc1101.SpiReadReg(CC1101_FREQ2) != expected_freq_[0]) return false;
  if (ELECHOUSE_cc1101.SpiReadReg(CC1101_FREQ1) != expected_freq_[1]) return false;
  if (ELECHOUSE_cc1101.SpiReadReg(CC1101_FREQ0) != expected_freq_[2]) return false;

  uint8_t marcstate = ELECHOUSE_cc1101.SpiReadStatus(CC1101_MARCSTATE) & 0x1F;
  return marcstate == MARCSTATE_RX;
}

} // namespace tfa_sky
} // namespace esphome
