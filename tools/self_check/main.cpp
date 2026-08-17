#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// Self-check: verifies SPI communication is genuinely live (not a frozen/
// cached read) by reading PARTNUM/VERSION directly and watching RSSI while
// alternating between two very different frequencies.

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== CC1101 Self-Check ===");

  ELECHOUSE_cc1101.setSpiPin(14, 12, 13, 15); // SCK, MISO, MOSI, CSN
  ELECHOUSE_cc1101.setGDO0(5);                // GDO0 on D1
  ELECHOUSE_cc1101.Init();

  // Raw register reads - PARTNUM (0x30) and VERSION (0x31) status registers.
  // Known-good CC1101 clones typically report PARTNUM=0x00, VERSION=0x14 or 0x04.
  byte partnum = ELECHOUSE_cc1101.SpiReadStatus(0x30);
  byte version = ELECHOUSE_cc1101.SpiReadStatus(0x31);
  Serial.printf("PARTNUM=0x%02X VERSION=0x%02X\n", partnum, version);
  Serial.printf("getCC1101() self-test: %s\n", ELECHOUSE_cc1101.getCC1101() ? "OK" : "FAIL");

  ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
  ELECHOUSE_cc1101.setRxBW(100);     // kHz

  Serial.println("Setup done. Watching RSSI while switching frequencies...");
  Serial.println("Expect: PARTNUM/VERSION values change per read only if SPI is broken;");
  Serial.println("RSSI should differ noticeably between 433.92 MHz and 434.79 MHz idle channel.");
}

void loop() {
  static int i = 0;
  float freq = (i % 2 == 0) ? 433.92f : 434.79f; // toggle between target and a quiet channel
  ELECHOUSE_cc1101.SetRx(freq);
  delay(10);

  // Take 5 fresh readings at this frequency, print each - real noise should jitter a bit.
  Serial.printf("--- freq=%.2f ---\n", freq);
  for (uint8_t s = 0; s < 5; s++) {
    int rssi = ELECHOUSE_cc1101.getRssi();
    Serial.printf("  sample %u: rssi=%d\n", s, rssi);
    delay(50);
  }

  // Re-check chip presence every cycle too, to catch any SPI dropout live.
  bool ok = ELECHOUSE_cc1101.getCC1101();
  Serial.printf("chip check: %s\n", ok ? "OK" : "FAIL");

  i++;
  delay(300);
}
