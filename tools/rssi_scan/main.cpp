#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// RSSI scan across 433.70-434.20 MHz in 20 kHz steps. Used to find the
// sensor's real carrier frequency (see README) and as a regression check
// that the RF path still behaves normally (real peak only at the true
// carrier, flat noise floor elsewhere - NOT flat/saturated everywhere).

constexpr float FREQ_START = 433.70f;
constexpr float FREQ_END   = 434.20f;
constexpr float FREQ_STEP  = 0.02f; // 20 kHz steps
constexpr uint8_t SAMPLES_PER_FREQ = 8;
constexpr uint16_t SAMPLE_DELAY_MS = 4;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("RSSI scan 433.70-434.20 MHz");

  ELECHOUSE_cc1101.setSpiPin(14, 12, 13, 15);
  ELECHOUSE_cc1101.setGDO0(5);
  ELECHOUSE_cc1101.Init();

  if (!ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("CC1101 NICHT gefunden!");
  }

  ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
  ELECHOUSE_cc1101.setRxBW(100);     // kHz
}

void loop() {
  for (float f = FREQ_START; f <= FREQ_END; f += FREQ_STEP) {
    ELECHOUSE_cc1101.SetRx(f);
    delay(2);

    int maxRssi = -128;
    for (uint8_t i = 0; i < SAMPLES_PER_FREQ; i++) {
      int r = ELECHOUSE_cc1101.getRssi();
      if (r > maxRssi) maxRssi = r;
      delay(SAMPLE_DELAY_MS);
    }
    Serial.printf("SCAN %.2f %d\n", f, maxRssi);
  }
  Serial.println("--- sweep done ---");
}
