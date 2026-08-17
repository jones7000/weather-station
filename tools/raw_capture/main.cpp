#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// RSSI-gated raw OOK pulse capture. Idles on RSSI polling; once RSSI clears
// a threshold (real transmission), switches into a tight busy-poll on GDO0
// to record pulse widths. Avoids interrupt-storm watchdog resets that a
// plain edge-interrupt on the noisy raw/async GDO0 line causes (see
// project history / README for why).

constexpr uint8_t GDO0_PIN = 5; // D1
constexpr float TARGET_FREQ_MHZ = 433.92f; // found via RSSI scan, see README

// Noise floor sits around -74dBm, real transmissions peaked at -40..-48dBm
// during the RSSI scan - pick a threshold safely in between.
constexpr int RSSI_THRESHOLD = -70;
constexpr uint32_t CAPTURE_WINDOW_US = 400000UL; // max 400ms per burst
constexpr uint16_t MAX_PULSES = 1000;
constexpr uint32_t MIN_PULSE_US = 80; // filter out sub-80us glitches

uint16_t pulseBuf[MAX_PULSES];

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("CC1101 RSSI-gated raw OOK capture");

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

  Serial.println("Warte auf RSSI-Trigger...");
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

    Serial.printf("[t=%lus] FRAME len=%u rssi=%d level=%d\n", millis() / 1000, count, rssi, lastLevel);
    for (uint16_t i = 0; i < count; i++) {
      Serial.print(pulseBuf[i]);
      Serial.print(' ');
    }
    Serial.println();

    delay(50); // brief settle before resuming RSSI polling
  }

  delay(1); // idle RSSI poll rate, keep detection latency low
}
