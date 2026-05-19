#include <Arduino.h>
#include "globals.h"

// ====== Rychlost/spotřeba/RPM ======
void vypocetPalivoRychlost() {
  unsigned long nowMs = millis();

  // --- kopie hodnot z ISR ---
  uint32_t sc;
  uint32_t lastPulseUsCopy;
  uint32_t injAccUs;
  uint32_t injPulseCountCopy;
  uint32_t periodBufCopy[RYCHLOST_PERIODA_N];
  uint8_t periodCountCopy;

  noInterrupts();
  sc = rychlostPulzuPocet;
  lastPulseUsCopy = rychlostPosledniPulzUs;
  injAccUs = vstrikOtevreniUsAkum;
  injPulseCountCopy = vstrikPocetPulzu;
  periodCountCopy = rychlostPeriodaPlatnych;
  for (uint8_t i = 0; i < RYCHLOST_PERIODA_N; ++i) periodBufCopy[i] = rychlostPeriodaBuf[i];
  vstrikOtevreniUsAkum = 0;  // vynulovat okno pro další periodu
  interrupts();

  uint32_t dSpeedCount = sc - rychlostPosledniPulzuPocet;
  rychlostPosledniPulzuPocet = sc;
  uint32_t nowUs = micros();

  uint32_t dInjPulseCount = injPulseCountCopy - vstrikPosledniPocetPulzu;
  vstrikPosledniPocetPulzu = injPulseCountCopy;

  if (rychlostOknoZacatek == 0) {
    rychlostOknoZacatek = nowMs;
    rychlostOknoZakladPocet = sc;
  }

  unsigned long speedWindowMs = nowMs - rychlostOknoZacatek;
  uint32_t dWindowPulses = sc - rychlostOknoZakladPocet;
  if ((dWindowPulses > 0 && speedWindowMs >= RYCHLOST_OKNO_MS) || speedWindowMs >= RYCHLOST_OKNO_MAX_MS) {
    diagRychlostPulzy = dWindowPulses;
    diagRychlostOknoMs = speedWindowMs;
    if (speedWindowMs > 0) {
      rychlostOknoKmh = (float)(((double)dWindowPulses * 3600000.0) / ((double)pulzyNaKm * (double)speedWindowMs));
    } else {
      rychlostOknoKmh = 0.0f;
    }
    if (rychlostOknoKmh > MAX_KMH_FYZICKY * RYCHLOST_REZERVA) rychlostOknoKmh = MAX_KMH_FYZICKY * RYCHLOST_REZERVA;
    rychlostOknoZacatek = nowMs;
    rychlostOknoZakladPocet = sc;
  }

  bool speedFresh = (lastPulseUsCopy != 0) && ((nowUs - lastPulseUsCopy) <= (RYCHLOST_VYPADEK_MS * 1000UL));
  diagRychlostAktualni = speedFresh;

  uint32_t medPeriodUs = speedFresh ? medianPeriodaUs(periodBufCopy, periodCountCopy) : 0;
  diagRychlostMedianUs = medPeriodUs;

  float periodSpeed = 0.0f;
  if (medPeriodUs > 0) {
    periodSpeed = (float)(3600000000.0 / ((double)pulzyNaKm * (double)medPeriodUs));
    if (periodSpeed > MAX_KMH_FYZICKY * RYCHLOST_REZERVA) periodSpeed = MAX_KMH_FYZICKY * RYCHLOST_REZERVA;
  }

  diagRychlostPocetKmh = rychlostOknoKmh;
  diagRychlostPeriodaKmh = periodSpeed;

  float targetSpeed = 0.0f;
  if (!speedFresh) {
    rychlostOknoKmh = 0.0f;
    diagRychlostPocetKmh = 0.0f;
    diagRychlostPeriodaKmh = 0.0f;
    diagRychlostMedianUs = 0;
    rychlostOknoZacatek = nowMs;
    rychlostOknoZakladPocet = sc;
  } else if (rychlostOknoKmh <= 0.1f) {
    targetSpeed = periodSpeed;
  } else if (rychlostOknoKmh >= RYCHLOST_BLEND_KMH) {
    targetSpeed = rychlostOknoKmh;
  } else {
    float wCount = rychlostOknoKmh / RYCHLOST_BLEND_KMH;
    targetSpeed = (wCount * rychlostOknoKmh) + ((1.0f - wCount) * periodSpeed);
  }

  float dtS = (rychlostPosledniVypocet == 0) ? 0.10f : ((float)(nowMs - rychlostPosledniVypocet) / 1000.0f);
  rychlostPosledniVypocet = nowMs;
  float maxRise = 90.0f * dtS + 2.0f;
  float maxFall = 130.0f * dtS + 3.0f;
  if (targetSpeed > rychlostVozu + maxRise) targetSpeed = rychlostVozu + maxRise;
  if (targetSpeed < rychlostVozu - maxFall) targetSpeed = rychlostVozu - maxFall;
  if (targetSpeed < 0.0f) targetSpeed = 0.0f;

  float alphaSpdFiltered = (!speedFresh || targetSpeed < 1.0f) ? 0.30f : ((targetSpeed < RYCHLOST_BLEND_KMH) ? 0.14f : 0.18f);
  rychlostVozu = alphaSpdFiltered * targetSpeed + (1.0f - alphaSpdFiltered) * rychlostVozu;
  if (rychlostVozu < 0.1f) rychlostVozu = 0.0f;

  // přičíst vzdálenost jen podle pulzů
  double meters_per_pulse = 1000.0 / (double)pulzyNaKm;
  double add_m_f = (double)dSpeedCount * meters_per_pulse;

  // Akumulujeme zlomky metrů – llround(0.128) = 0, takže u64 by nikdy nerostlo
  static float metruNevyreseno = 0.0f;
  metruNevyreseno += (float)add_m_f;
  uint64_t add_m = (uint64_t)metruNevyreseno;
  metruNevyreseno -= (float)add_m;

  celkoveMetry_u64 += add_m;
  denniMetry_u64 += add_m;

  celkovaVzdalenost = (float)(celkoveMetry_u64 / 1000.0);
  denniVzdalenost = (float)(denniMetry_u64 / 1000.0);

  // ====== PALIVO – výpočet spotřeby z času otevření vstřiku ======
  palivoOknoUs += injAccUs;
  palivoOknoPulzu += dInjPulseCount;
  if (palivoPosledniAktualizace == 0) palivoPosledniAktualizace = nowMs;
  unsigned long deltaFuelMs = nowMs - palivoPosledniAktualizace;

  if (deltaFuelMs >= PALIVO_VZORKOVANI_MS) {
    double fuelPerUs = 0.0;
    if (PALIVO_NA_US > 0.0f) {
      fuelPerUs = PALIVO_NA_US;  // [L / µs]
    } else {
      // Jinak spočítáme z VSTRIK_TOK_CC_MIN
      double inj_Lps_total = (VSTRIK_TOK_CC_MIN / 1000.0) / 60.0;  // L/s při 100% otevření
      fuelPerUs = inj_Lps_total / 1000000.0;                      // L / µs
    }

    double injScale = (double)motorPocetVstriku / (double)merenyPocetVstriku;
    if (injScale < 1.0) injScale = 1.0;
    double L_window = (double)palivoOknoUs * fuelPerUs * injScale;
    double window_s = (double)deltaFuelMs / 1000.0;

    float newFuelRate = 0.0f;  // L/h
    diagPalivoSyreLh = 0.0f;
    diagVstrikOknoUs = palivoOknoUs;
    diagVstrikPulzy = palivoOknoPulzu;

    if (!vstrikPolaritaAutoOtocena && palivoOknoPulzu >= 8) {
      float avgPulseUs = (float)palivoOknoUs / (float)palivoOknoPulzu;
      if (avgPulseUs > 20000.0f && avgPulseUs < 250000.0f) {  // >20 ms – spolehlivě mimo rozsah studeného startu
        noInterrupts();
        vstrikAktivniNizko = !vstrikAktivniNizko;
        vstrikOtevreno = false;
        vstrikPosledniHranaUs = 0;
        interrupts();
        vstrikPolaritaAutoOtocena = true;
        palivoOknoUs = 0;
        palivoOknoPulzu = 0;
        palivoTokInst = 0.0f;
        spotrebaLh = 0.0f;
        diagPalivoSyreLh = 0.0f;
        diagVstrikOknoUs = 0;
        diagVstrikPulzy = 0;
        Serial.printf("AUTO: inj polarity flipped, avg pulse %.0f us looked inverted.\n", avgPulseUs);
        palivoPosledniAktualizace = nowMs;
        return;
      }
    }

    if (window_s > 0.0) {
      // L/h = (L / s) * 3600
      newFuelRate = (float)((L_window / window_s) * 3600.0);

      if (newFuelRate < 0.0f) newFuelRate = 0.0f;
      if (newFuelRate > 40.0f) newFuelRate = 40.0f;  // pojistka proti úletům
    }

    // --- rychlý filtr pro okamžitou spotřebu (L/h) ---
    const float ALPHA_INST = 0.45f;  // bylo 0.8 – příliš trhavé, vyhladit
    diagPalivoSyreLh = newFuelRate;
    palivoTokInst = ALPHA_INST * newFuelRate + (1.0f - ALPHA_INST) * palivoTokInst;
    if (palivoTokInst < 0.0f) palivoTokInst = 0.0f;

    // --- adaptivní filtr pro "pomalou" L/h (volnoběh / integrace) ---
    float alphaFuel;

    if (palivoOknoUs == 0) {
      // žádné pulzy -> vypnuté vstřiky / žádné vstřikování
      alphaFuel = 0.40f;  // rychle stáhnout k nule
      newFuelRate = 0.0f;
    } else if (rychlostVozu < 3.0f) {
      // volnoběh – lambda sonda způsobuje přirozenou variaci ±15-20%, silný filtr aby displej neskákal
      alphaFuel = 0.02f;  // bylo 0.06
    } else {
      // za jízdy – rychlejší reakce
      alphaFuel = 0.15f;  // bylo 0.20
    }

    spotrebaLh = alphaFuel * newFuelRate + (1.0f - alphaFuel) * spotrebaLh;
    if (spotrebaLh < 0.0f) spotrebaLh = 0.0f;

    // Integrovat celkovou spotřebu. u64 je hlavní zdroj pravdy, float jen kopie pro UI.
    static double palivoUlNevyreseno = 0.0;
    palivoUlNevyreseno += L_window * 1000000.0;
    uint64_t addFuelUl = (uint64_t)palivoUlNevyreseno;
    palivoUlNevyreseno -= (double)addFuelUl;
    celkovePalivoUl_u64 += addFuelUl;
    celkovePalivo = (float)(celkovePalivoUl_u64 / 1000000.0);

    // uzavřít okno a začít nové
    palivoOknoUs = 0;
    palivoOknoPulzu = 0;
    palivoPosledniAktualizace = nowMs;
  }

  // průměrná spotřeba L/100km
  celkovaVzdalenost = (float)(celkoveMetry_u64 / 1000.0);
  denniVzdalenost = (float)(denniMetry_u64 / 1000.0);
  celkovePalivo = (float)(celkovePalivoUl_u64 / 1000000.0);

  if (celkoveMetry_u64 > 0) {
    prumernaSpotreba = (float)((double)celkovePalivoUl_u64 / (10.0 * (double)celkoveMetry_u64));
  } else {
    prumernaSpotreba = 0.0f;
  }

  // dojezd přepočítávej pomaleji, aby nelítal s každým vzorkem hladiny paliva
  static unsigned long dojezdPosledniMs = 0;
  static bool dojezdInicializovan = false;
  if (!dojezdInicializovan || (nowMs - dojezdPosledniMs) >= DOJEZD_AKTUALIZACE_MS) {
    float refCons = (prumernaSpotreba > 0.1f) ? prumernaSpotreba : VYCHOZI_SPOTREBA_LH;
    float novyDojezd = NAN;
    if (!isnan(hladinaPaliva) && refCons > 0.1f) {
      novyDojezd = hladinaPaliva / (refCons / 100.0f);
    }

    if (!isfinite(novyDojezd)) {
      odhadovanyDojezd = NAN;
    } else if (!dojezdInicializovan || !isfinite(odhadovanyDojezd)) {
      odhadovanyDojezd = novyDojezd;
    } else {
      constexpr float DOJEZD_ALFA = 0.35f;
      odhadovanyDojezd = DOJEZD_ALFA * novyDojezd + (1.0f - DOJEZD_ALFA) * odhadovanyDojezd;
    }
    dojezdPosledniMs = nowMs;
    dojezdInicializovan = true;
  }

  // ====== OTÁČKY ======
  if (otackyT0 == 0) otackyT0 = nowMs;
  if (nowMs - otackyT0 >= OTACKY_PRUMER_MS) {
    uint32_t e;
    noInterrupts();
    e = otackyHrany;
    interrupts();

    uint32_t dE = e - otackyHranyPred;
    uint32_t dMs = nowMs - otackyT0;

    float edgesPerSec = (dE * 1000.0f) / (float)dMs;
    float rpmRaw = edgesPerSec * (60.0f / HRANY_NA_OTACKU);

    if (rpmRaw < 50.0f) rpmRaw = 0.0f;
    otackyMotoru = OTACKY_ALFA * rpmRaw + (1.0f - OTACKY_ALFA) * otackyMotoru;

    otackyHranyPred = e;
    otackyT0 = nowMs;
  }

  // ====== ODHAD ŘADÍHO STUPNĚ ======
  static constexpr float prahyStupnu[4] = { 99.0f, 60.0f, 41.0f, 34.0f };

  // Hystereze – zabrání blikání na hranici dvou stupňů (5% překmit/podkmit)
  constexpr float hysterezeProc = 0.05f;
  static uint8_t potvrzenyStupen = 0;
  static uint8_t kandidatStupen = 0;
  static unsigned long kandidatOdMs = 0;

  if (rychlostVozu >= 5.0f && otackyMotoru >= 500.0f) {
    float r = otackyMotoru / rychlostVozu;

    if (r < GEAR_POMER_MIN_PLATNY) {
      potvrzenyStupen = 0;
      kandidatStupen = 0;
      kandidatOdMs = 0;
    } else {
      // Urči surový stupeň z tabulky prahů
      uint8_t surovyStupen = 5;
      for (uint8_t i = 0; i < 4; i++) {
        if (r > prahyStupnu[i]) { surovyStupen = i + 1; break; }
      }

      uint8_t navrzenyStupen = potvrzenyStupen;
      if (potvrzenyStupen == 0) {
        navrzenyStupen = surovyStupen;
      } else if (surovyStupen > potvrzenyStupen) {
        // Řazení nahoru vyžaduje pokles r pod práh s rezervou.
        float prah = prahyStupnu[surovyStupen - 2];
        if (r < prah * (1.0f - hysterezeProc)) navrzenyStupen = surovyStupen;
      } else if (surovyStupen < potvrzenyStupen) {
        // Řazení dolů vyžaduje nárůst r nad práh s rezervou.
        float prah = prahyStupnu[surovyStupen - 1];
        if (r > prah * (1.0f + hysterezeProc)) navrzenyStupen = surovyStupen;
      }

      if (navrzenyStupen == potvrzenyStupen) {
        kandidatStupen = 0;
        kandidatOdMs = 0;
      } else {
        if (kandidatStupen != navrzenyStupen) {
          kandidatStupen = navrzenyStupen;
          kandidatOdMs = nowMs;
        } else if ((nowMs - kandidatOdMs) >= GEAR_STABILNI_MS) {
          potvrzenyStupen = navrzenyStupen;
          kandidatStupen = 0;
          kandidatOdMs = 0;
        }
      }
    }

    odhadnutyStupen = potvrzenyStupen;
  } else {
    odhadnutyStupen = 0;
    potvrzenyStupen = 0;
    kandidatStupen = 0;
    kandidatOdMs = 0;
  }
}
