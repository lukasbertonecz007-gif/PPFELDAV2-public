# PPFELDA - zapojeni (V2.0)

Tento dokument odpovida souborum ve slozce `V2.0/`.
ADS1115 je zde provozovan na **3.3 V**.

## 1) Napajeni a zem

- ESP32 DevKit napajet stabilnim 5V (USB nebo step-down z auta).
- ADS1115, OLED SH1106, RTC DS3231 a SD modul musi mit spolecnou zem s ESP32.
- Vsechny signaly do ESP32/ADS drzet v rozsahu 0..3.3 V (pokud neni pouzit prevodnik).

## 2) I2C sbernice (spolecna)

- `GPIO21` -> SDA
- `GPIO22` -> SCL
- Na stejne I2C jsou:
  - OLED SH1106
  - RTC DS3231
  - ADS1115
- I2C clock: **400 kHz** (DS3231 max spec)

Poznamka:
- ADS1115 `ADDR` dej na GND (adresa 0x48), pokud nepouzivas jinou adresu.

## 3) ADS1115 (voda + palivo + venkovni teplota)

- ADS `VDD` -> 3.3V
- ADS `GND` -> GND
- ADS `SDA` -> GPIO21
- ADS `SCL` -> GPIO22
- ADS `A0` -> signal teploty vody (pres delic R1=15.9k / R2=10k, vetev 8V budiku)
- ADS `A1` -> signal paliva (pres delic R1=15.9k / R2=10k, vetev 8V budiku)
- ADS `A2` -> venkovni NTC 10K (MF-52, beta=3950) – vetev **3.3V**, serie 10kΩ primo na ADS (BEZ delice R1/R2!)
- ADS `A3` -> volny

Nastaveni v kodu:
- `ads.setGain(GAIN_TWOTHIRDS)` (rozsah +-6.144 V)
- `ads.setDataRate(RATE_ADS1115_860SPS)`

Dulezite:
- A0 a A1 jsou za napetovym delecem (R1=15.9k, R2=10k) kvuli ochrane pred 8V vetvemi budiku.
- A2 je primo na 3.3V vetvi (NTC delic: 3.3V -> 10kΩ -> uzel -> NTC -> GND), bez delice.

## 4) SD karta (HSPI)

- `GPIO5`  -> SD CS
- `GPIO14` -> SD SCK
- `GPIO12` -> SD MISO
- `GPIO13` -> SD MOSI

## 5) Digitalni vstupy a preruseni

- `GPIO27` -> tlacitko dalsi stranka / menu next (aktivni LOW, INPUT_PULLUP)
- `GPIO15` -> tlacitko predchozi stranka / menu edit / dlouhy stisk = servis menu (aktivni LOW, INPUT_PULLUP)
- `GPIO32` -> dvere (jeden pin, aktivni LOW, INPUT_PULLUP)
- `GPIO4`  -> vstup vstriku (ISR, CHANGE)
- `GPIO17` -> hall rychlosti (ISR, RISING)
- `GPIO16` -> RPM signal (ISR, CHANGE)

## 6) Interne ADC ESP32

- `GPIO36` (ADC1_CH0) -> napeti baterie pres externi delic (pomer `NAPETOVY_DELIC_POMER = 4.67`)

## 7) Uzitecne prikazy

```
# Kompilace
pio run

# Kompilace + upload
pio run --target upload

# Upload na konkretni port
pio run --target upload --upload-port COM4

# Upload s vymazanim Flash (nutne po zmene partition table, napr. pri prechodu na huge_app)
pio run --target erase --upload-port COM4
pio run --target upload --upload-port COM4

# nebo zkratka: esptool.py --port COM4 erase_flash && pio run --target upload --upload-port COM4

# Seriovy monitor
pio device monitor

# Seznam portu
pio device list
```