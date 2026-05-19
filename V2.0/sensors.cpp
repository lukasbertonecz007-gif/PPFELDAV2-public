#include <Arduino.h>
#include "globals.h"

// ====== Čtení odporu snímače přes ADS1115 ======
// Topologie: 8V → R_série (cívka budíku) → uzel → čidlo → GND
// ADS měří napětí na uzlu přes dělič R1(15K)/R2(10K): Vadc = Vuzel * R2/(R1+R2)
float ctejOdporCidlaAds(int channel, float* rOut, float* vOut, int16_t* rawOut) {
  int16_t raw = medianAdsSyrovy(channel);
  float v = ads.computeVolts(raw);
  if (rawOut) *rawOut = raw;
  if (vOut) *vOut = v;

  // Zpětné výpočet skutečného napětí na uzlu (před děličem)
  float vNode = v * (DELIC_R1 + DELIC_R2) / DELIC_R2;

  float vcc    = (cidloNapajeni > 1.0f) ? cidloNapajeni : CIDLO_NAPAJENI_DEFAULT;
  float serR;
  if (channel == ADS_KAN_PALIVO) {
    serR = (cidloOhmPalivo > 0.1f) ? cidloOhmPalivo : CIDLO_OHM_PALIVO_DEFAULT;
  } else {
    // ADS_KAN_VODA (0) i ADS_KAN_TEPLOTA_VENKU (2)
    serR = (cidloOhmVoda > 0.1f) ? cidloOhmVoda : CIDLO_OHM_VODA_DEFAULT;
  }

  if (v <= 0.001f)            return NAN;  // zkrat / odpojeno
  if (vNode >= (vcc - 0.05f)) return NAN;  // saturace → čidlo odpojeno

  float denom = vcc - vNode;
  if (denom <= 0.001f) return NAN;

  float r = serR * (vNode / denom);
  if (rOut) *rOut = r;
  if (r <= 0.5f) return NAN;

  return r;
}

float vypocetTeplotyVodyZOdporu(float rOhm) {
  if (!senderOdporValidni(rOhm, VODA_MIN_OHM, VODA_MAX_OHM)) return NAN;

  // Kalibrovana tabulka pro mereni pres vetev budiku, sestupne: vyssi R = nizsi T.
  // Extrapolace nad tabulku (> 1500 ohm = pod -20 C) nebo pod tabulku (< 160 ohm = nad 100 C)
  const float rHi  = vodaOhmTab[0];
  const float rHi2 = vodaOhmTab[1];
  const float tLo  = vodaTeplotaTab[0];
  const float tLo2 = vodaTeplotaTab[1];
  const float rLo  = vodaOhmTab[VODA_TAB_N - 1];
  const float rLo2 = vodaOhmTab[VODA_TAB_N - 2];
  const float tHi  = vodaTeplotaTab[VODA_TAB_N - 1];
  const float tHi2 = vodaTeplotaTab[VODA_TAB_N - 2];

  float tC;
  if (rOhm > rHi) {
    // pod 0°C – extrapolace nahoru
    float slope = (tLo - tLo2) / (rHi - rHi2);
    tC = tLo + slope * (rOhm - rHi);
  } else if (rOhm < rLo) {
    // nad 115°C – extrapolace dolů
    float slope = (tHi - tHi2) / (rLo - rLo2);
    tC = tHi + slope * (rOhm - rLo);
  } else {
    tC = interpolaceN(rOhm, vodaOhmTab, vodaTeplotaTab, VODA_TAB_N);
  }

  if (!isfinite(tC)) return NAN;
  return tC;
}

// ====== I2C bus recovery ======
static void i2cRecovery() {
  unsigned long now = millis();
  if (i2cPosledniObnova != 0 && now - i2cPosledniObnova < I2C_OBNOVA_MIN_INTERVAL_MS) return;
  i2cPosledniObnova = now;
  adsChybPocet = 0;
  Serial.println("I2C: bus recovery zahajena...");
  zapisErrorLog("WARN", "I2C_RECOVERY_START", "ADS/I2C recovery started");
  Wire.end();
  delay(5);
  // Manuální bitbang – uvolnění zamrzlého slave (9 SCL pulsů)
  pinMode(PIN_SDA, OUTPUT);
  pinMode(PIN_SCL, OUTPUT);
  digitalWrite(PIN_SDA, HIGH);
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(10);
  for (int i = 0; i < 9; i++) {
    digitalWrite(PIN_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(5);
    // Přepnout SDA na INPUT abychom četli skutečný stav linky (ne vlastní latch)
    pinMode(PIN_SDA, INPUT);
    bool sdaFree = (digitalRead(PIN_SDA) == HIGH);
    pinMode(PIN_SDA, OUTPUT);
    digitalWrite(PIN_SDA, HIGH);
    if (sdaFree) break;  // SDA uvolněna slave zařízením
  }
  // STOP podmínka
  digitalWrite(PIN_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_SDA, HIGH);
  delayMicroseconds(5);
  // Restart I2C sběrnice
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.setTimeOut(50);
  delay(20);
  // Re-inicializace ADS1115
  adsPripraven = ads.begin();
  if (adsPripraven) {
    ads.setGain(GAIN_TWOTHIRDS);
    ads.setDataRate(RATE_ADS1115_860SPS);
    Serial.println("I2C: recovery OK, ADS ready.");
    zapisErrorLog("INFO", "I2C_RECOVERY_OK", "ADS ready");
  } else {
    Serial.println("I2C: recovery dokoncena, ADS stale nereaguje.");
    zapisErrorLog("ERROR", "I2C_RECOVERY_FAIL", "ADS still not responding");
  }
}

// ====== VODA & PALIVO ======
void spocitejVoduAPalivo() {
  if (!adsPripraven) i2cRecovery();

  if (!adsPripraven) {
    vodaDiag.raw = 0;
    vodaDiag.volts = NAN;
    vodaDiag.ohms = NAN;
    vodaDiag.valid = false;
    palivoDiag.raw = 0;
    palivoDiag.volts = NAN;
    palivoDiag.ohms = NAN;
    palivoDiag.valid = false;
    teplotaVody = NAN;
    hladinaPaliva = NAN;
  } else {
    float vW = NAN;
    float vF = NAN;
    int16_t rawW = 0;
    int16_t rawF = 0;
    float rW = ctejOdporCidlaAds(ADS_KAN_VODA, nullptr, &vW, &rawW);
    float rF = ctejOdporCidlaAds(ADS_KAN_PALIVO, nullptr, &vF, &rawF);

    vodaDiag.raw = rawW;
    vodaDiag.volts = vW;
    vodaDiag.ohms = rW;
    vodaDiag.valid = senderOdporValidni(rW, VODA_MIN_OHM, VODA_MAX_OHM);

    palivoDiag.raw = rawF;
    palivoDiag.volts = vF;
    palivoDiag.ohms = rF;
    palivoDiag.valid = senderOdporValidni(rF, PALIVO_MIN_OHM, PALIVO_MAX_OHM);

    // Detekce zamrzlého I2C: oba kanály vrací raw=0 opakovaně → recovery
    if (rawW == 0 && rawF == 0) {
      if (++adsChybPocet >= ADS_CHYBA_LIMIT) i2cRecovery();
    } else {
      adsChybPocet = 0;
    }

    float tC = rezimTestovani ? 95.0f : vypocetTeplotyVodyZOdporu(rW);
    if (!isfinite(tC)) {
      teplotaVody = NAN;
    } else {
      tC += teplotaOffsetC;
      tC = omezNaRozsah(tC, -40.0f, 140.0f);
      if (!teplotaInicializovana || !isfinite(teplotaVody)) {
        teplotaVody = tC;
        teplotaInicializovana = true;
      } else {
        teplotaVody = VODA_FILTR_ALFA * tC + (1.0f - VODA_FILTR_ALFA) * teplotaVody;
      }
    }

    float fl = vypocetPalivaZOdporu(rF);
    if (!isfinite(fl)) {
      hladinaPaliva = NAN;
    } else {
      fl = omezNaRozsah(fl + palivoOffsetL, 0.0f, 45.0f);
      if (!isfinite(hladinaPaliva)) {
        hladinaPaliva = fl;
      } else {
        float alphaFuel = (fl >= hladinaPaliva) ? PALIVO_FILTR_NAHORU_ALFA : PALIVO_FILTR_DOLU_ALFA;
        hladinaPaliva = alphaFuel * fl + (1.0f - alphaFuel) * hladinaPaliva;
      }
    }
  }

  // --- BATERIE (s EMA filtrem a ADC kalibrací) ---
  int rawBatt = analogRead(BATT_PIN);
  if (rawBatt > 0) {
    float vBatt;
    if (batAdcKalibrovano) {
      uint32_t mv = esp_adc_cal_raw_to_voltage((uint32_t)rawBatt, &batAdcChars);
      vBatt = (mv / 1000.0f) * NAPETOVY_DELIC_POMER;
    } else {
      vBatt = (rawBatt / ADC_MAX) * VREF * NAPETOVY_DELIC_POMER;
    }
    vBatt += napetiOffsetV;
    if (vBatt < 0.0f) vBatt = 0.0f;
    if (!isfinite(batFiltrovana)) {
      batFiltrovana = vBatt;
    } else {
      batFiltrovana = EMA_ALFA * vBatt + (1.0f - EMA_ALFA) * batFiltrovana;
    }
    napetiBaterie = batFiltrovana;
  }
}

// Venkovní NTC 10K přes ADS1115 A2
// Topologie: 3.3V → VENKU_SERIOVY_OHM (10kΩ) → uzel → NTC → GND
void vypocetVenkovniTeploty() {
  if (!adsPripraven) {
    teplotaVenku = NAN;
    return;
  }

  int16_t raw = medianAdsSyrovy(ADS_KAN_TEPLOTA_VENKU);
  if (raw <= 0) {
    teplotaVenku = NAN;
    return;
  }

  float v = ads.computeVolts(raw);
  constexpr float vcc = 3.3f;

  float denom = vcc - v;
  if (v <= 0.001f || denom <= 0.001f) {
    teplotaVenku = NAN;
    return;
  }

  // NTC odpor: serR * v / (vcc - v)
  float rNtc = VENKU_SERIOVY_OHM * v / denom;

  if (rNtc < 100.0f || rNtc > 500000.0f) {
    teplotaVenku = NAN;
    return;
  }

  float invT = (logf(rNtc / VENKU_R0) / betaVenku) + (1.0f / T0_KELVIN);
  if (!isfinite(invT) || invT <= 0.0f) {
    teplotaVenku = NAN;
    return;
  }

  float tC = (1.0f / invT) - 273.15f;
  tC += teplotaVenkuOffsetC;
  tC = omezNaRozsah(tC, -40.0f, 80.0f);

  if (!teplotaVenkuInicializovana || !isfinite(teplotaVenku)) {
    teplotaVenku = tC;
    teplotaVenkuInicializovana = true;
  } else {
    teplotaVenku = EMA_ALFA * tC + (1.0f - EMA_ALFA) * teplotaVenku;
  }
}
