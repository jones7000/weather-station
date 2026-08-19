# ESP Weather Station
* **Reverse engineered TFA Dostmann SKY Outdoor Sensor**
* Wemos D1 mini (ESP8266) + CC1101 fangen und decodieren das 433 MHz Funksignal
eines TFA Dostmann SKY Außensensors (Temperatur + Luftfeuchtigkeit), Ausgabe
per `Serial.print`.

## Hardware

**Empfänger**
- ESP8266 Wemos D1 mini
- CC1101 433 MHz Transceiver-Modul (SMA-Antenne, 8-Pin-Breakout)

**Sender**
- TFA Dostmann SKY Funk-Wetterstation, Außensensor 30.3195
- Trägerfrequenz laut Hersteller: 433 MHz, per RSSI-Scan verifiziert: **433.92 MHz**

## Pinbelegung

Hardware-SPI, alles auf 3,3 V (CC1101 verträgt kein 5V!). Siehe auch
`pinout_c1101.png` / `pinout_d1mini.png`.

| CC1101-Pin | D1-mini-Pin | GPIO   | Hinweis |
|---|---|---|---|
| VCC  | 3V3 | -      | nur 3,3 V |
| GND  | GND | -      | |
| SCK  | D5  | GPIO14 | Hardware-SPI-Clock |
| MISO | D6  | GPIO12 | Hardware-SPI-MISO |
| MOSI | D7  | GPIO13 | Hardware-SPI-MOSI |
| CSN  | D8  | GPIO15 | Chip-Select |
| GDO0 | D1  | GPIO5  | demodulierter Rohsignal-Bitstream |
| GDO2 | D2  | GPIO4  | unbeschaltet nutzbar, aktuell ungenutzt |

## Dev Environment

- PlatformIO (VSCode-Extension)
- Nix Flake für die Toolchain: `nix develop` startet eine Shell mit `pio`, `esptool`
- USB-Serial-Zugriff braucht die Gruppe `dialout` (System-Config, nicht Teil
  dieses Repos) oder als Notlösung `sudo chmod 666 /dev/ttyUSB0`

## Protokoll (reverse-engineered)

OOK, PWM-kodiert: fester ~475µs Mark-Puls + variable Lücke
(~1950-2100µs = Bit 0, ~4020-4065µs = Bit 1). Jede Übertragung wiederholt
denselben 36-Bit-Frame 3x, getrennt durch ~8.4-8.9ms Pausen.

```
[0:8]   ID         8 Bit, zufällig pro Batteriewechsel
[8]     Batterie   0 = ok
[9]     ?          unbekannt
[10:12] Kanal      2 Bit
[12:24] Temperatur 12 Bit, bitweise umgekehrt (LSB zuerst), signed, x0.1 °C
[24:30] Feuchte    6 Bit, bitweise umgekehrt (LSB zuerst), + 36
[30:36] Checksum   6 Bit, Algorithmus nicht rekonstruiert
```

Da die Checksum-Formel unbekannt ist, validiert der Decoder Frames stattdessen
per Mehrheitsentscheid über die 2-3 Wiederholungen pro Übertragung, plus einer
Plausibilitätsprüfung (Temperatur/Feuchte in sinnvollem Bereich).

## Vorgehen

1. PlatformIO-Projekt + Nix-Toolchain aufsetzen, ESP8266 flashen/testen
2. CC1101 per SPI anschließen, Verbindung prüfen (`self_check`)
3. Trägerfrequenz per RSSI-Scan über das 433 MHz Band finden (`rssi_scan`)
4. Rohsignal (Pulslängen) bei erkannter Übertragung aufzeichnen (`raw_capture`)
5. Protokoll aus den Pulsmustern von Hand rekonstruiert (Bitlayout siehe oben)
6. Finaler Decoder in `src/main.cpp`
7. ESPHome-Komponente (`esphome/`) für die Anbindung an Home Assistant

## Tools (`tools/`)

Eigene PlatformIO-Environments, überschreiben nicht `src/main.cpp`:

| Environment | Zweck |
|---|---|
| `self_check` | CC1101-SPI-Grundcheck (PARTNUM/VERSION, RSSI-Jitter) |
| `rssi_scan`  | RSSI-Scan 433.70-434.20 MHz, Trägerfrequenz finden/verifizieren |
| `raw_capture`| RSSI-getriggerte Rohsignal-Aufzeichnung (Pulslängen als Debug-Ausgabe) |

```
pio run -e <name> -t upload
```

## Nutzung

```
pio run -e d1_mini -t upload
pio device monitor -b 115200
```

Gibt bei jeder erkannten Übertragung eine Zeile aus, z.B.:

```
ID=0xC7 Kanal=1 Batterie=ok  Temp=25.0 C  Feuchte=61%  (3/3 Wiederholungen einig)
```

## ESPHome / Home Assistant (`esphome/`)

Für den Dauerbetrieb mit Home-Assistant-Anbindung gibt es neben dem PlatformIO-
Sketch (`src/main.cpp`, weiterhin nützlich für Serial-Debugging ohne HA) eine
eigenständige ESPHome-Firmware. Sie nutzt dieselbe Hardware/dasselbe Protokoll,
ist aber sauber in drei Schichten getrennt, als eigene ESPHome external
component unter `esphome/components/tfa_sky/`:

| Datei | Schicht | Zuständigkeit |
|---|---|---|
| `cc1101_receiver.h/.cpp` | **sensor** | CC1101-Setup, RSSI-Polling, rohes Pulslängen-Capture auf GDO0. Kennt das TFA-Protokoll nicht. |
| `tfa_sky_decoder.h/.cpp` | **decoding** | Reine Protokoll-Logik: Pulslängen → Frame (Temp/Feuchte/ID/...). Kein Hardware- oder ESPHome-Bezug, unabhängig testbar. |
| `tfa_sky.h/.cpp` + `sensor.py`/`__init__.py` | **esphome** | ESPHome-`Component`, verdrahtet Receiver + Decoder und published Temperatur/Feuchte/Empfangssignal als `sensor::Sensor` (native API → Home Assistant, Auto-Discovery). |

Die Protokoll-/Capture-Logik ist bewusst eigenständig implementiert und nicht
mit `src/main.cpp` geteilt, da ESPHome ein eigenes Build-System (eigener
PlatformIO-Tree pro Kompiliervorgang) hat und ein Cross-Directory-Include
brüchig wäre. Änderungen am Decoder müssen daher an beiden Stellen
nachgezogen werden.

### Ersteinrichtung (per USB)

```
cd esphome
cp secrets.yaml.example secrets.yaml   # ausfüllen, siehe Kommentare in der Datei
esphome run weather-station.yaml --device /dev/ttyUSB0
```

`wifi:` enthält bewusst keine feste `ssid`/`password` - das Gerät startet beim
allerersten Boot direkt als eigener Access Point **"Weather Station
Fallback"** mit Captive Portal. Verbinde dich damit von Handy/Laptop, trage
im aufpoppenden Portal (sonst manuell `http://192.168.4.1`) das echte WLAN
ein. Das Gerät merkt es sich (Flash) und ist danach im normalen Netz.

**Falls der Upload mit `Failed to connect ... No serial data received` abbricht:**
der Auto-Reset über die DTR/RTS-Leitungen des USB-Chips klappt nicht bei
jedem D1-mini-Board zuverlässig. Bootloader dann manuell erzwingen:
GPIO0 (Pin **D3**) mit **GND** verbinden, Board stromlos machen/neu
verbinden (USB ab-/anstecken oder RST-Taste), *während* D3 auf GND bleibt,
dann den Upload starten. Nach erfolgreichem Flash D3 wieder von GND trennen,
sonst bootet es nur in den Bootloader statt normal zu starten.

### OTA-Updates (nach der Ersteinrichtung)

Sobald das Gerät im WLAN ist, läuft jedes weitere `esphome run`/`upload` ganz
ohne Kabel über die native API:

```
esphome upload esphome/weather-station.yaml --device weather-station.local
# Live-Logs laufen genauso über die API:
esphome logs esphome/weather-station.yaml --device weather-station.local
```

Ohne `--device` fragt `esphome` interaktiv, ob USB oder OTA verwendet werden
soll. Falls `weather-station.local` (mDNS) im eigenen Netz nicht auflöst,
die IP stattdessen direkt angeben, z. B. per
`nmap -sn 10.0.0.0/24` (MAC-Präfix aus dem `wifi:`-Log/esptool-Ausgabe
abgleichen) oder ein Blick ins Router-Interface, dann:

```
esphome upload esphome/weather-station.yaml --device 10.0.0.93
```

Home Assistant findet das Gerät nach der Ersteinrichtung automatisch über die
[ESPHome-Integration](https://www.home-assistant.io/integrations/esphome/)
(mDNS) - keine manuelle MQTT-Konfiguration nötig.

Pins/Frequenz/RSSI-Schwelle in `weather-station.yaml` entsprechen den Defaults
aus der Pinbelegung oben und können pro Instanz überschrieben werden. Sind
mehrere TFA-SKY-Sensoren in Reichweite, lässt sich mit `sensor_id:` (Wert aus
dem Log ablesen) auf eine feste Sensor-ID filtern.
