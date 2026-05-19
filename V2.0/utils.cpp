#include <Arduino.h>
#include "globals.h"

// ====== Pomocné utility ======

float omezNaRozsah(float x, float a, float b) {
  return x < a ? a : (x > b ? b : x);
}

bool senderOdporValidni(float rOhm, float minOhm, float maxOhm) {
  return isfinite(rOhm) && rOhm >= minOhm && rOhm <= maxOhm;
}

int datumKlic(const RtcDateTime& dt) {
  return (int)dt.Year() * 10000 + (int)dt.Month() * 100 + (int)dt.Day();
}

int16_t medianAdsSyrovy(int channel) {
  int16_t vzorky[ADS_MEDIAN_VZORKY];
  for (int i = 0; i < ADS_MEDIAN_VZORKY; ++i) {
    vzorky[i] = ads.readADC_SingleEnded(channel);
  }

  for (int i = 0; i < ADS_MEDIAN_VZORKY - 1; ++i) {
    for (int j = i + 1; j < ADS_MEDIAN_VZORKY; ++j) {
      if (vzorky[j] < vzorky[i]) {
        int16_t t = vzorky[i];
        vzorky[i] = vzorky[j];
        vzorky[j] = t;
      }
    }
  }

  return vzorky[ADS_MEDIAN_VZORKY / 2];
}

uint32_t medianPeriodaUs(const uint32_t* data, uint8_t n) {
  if (n == 0) return 0;
  uint32_t tmp[RYCHLOST_PERIODA_N];
  if (n > RYCHLOST_PERIODA_N) n = RYCHLOST_PERIODA_N;
  for (uint8_t i = 0; i < n; ++i) tmp[i] = data[i];

  for (uint8_t i = 0; i < n - 1; ++i) {
    for (uint8_t j = i + 1; j < n; ++j) {
      if (tmp[j] < tmp[i]) {
        uint32_t t = tmp[i];
        tmp[i] = tmp[j];
        tmp[j] = t;
      }
    }
  }

  if (n & 1) return tmp[n / 2];
  return (tmp[(n / 2) - 1] + tmp[n / 2]) / 2;
}

// Interpolace N bodů – funguje pro vzestupné i sestupné x[].
float interpolaceN(float x, const float* xv, const float* yv, int n) {
  if (n <= 1) return yv[0];

  bool asc = (xv[0] <= xv[n - 1]);
  if (asc) {
    if (x <= xv[0]) return yv[0];
    if (x >= xv[n - 1]) return yv[n - 1];
    for (int i = 0; i < n - 1; i++) {
      if (x >= xv[i] && x <= xv[i + 1]) {
        float d = xv[i + 1] - xv[i];
        if (fabsf(d) < 1e-9f) return yv[i];
        float t = (x - xv[i]) / d;
        return yv[i] + t * (yv[i + 1] - yv[i]);
      }
    }
  } else {
    if (x >= xv[0]) return yv[0];
    if (x <= xv[n - 1]) return yv[n - 1];
    for (int i = 0; i < n - 1; i++) {
      if (x <= xv[i] && x >= xv[i + 1]) {
        float d = xv[i + 1] - xv[i];
        if (fabsf(d) < 1e-9f) return yv[i];
        float t = (x - xv[i]) / d;
        return yv[i] + t * (yv[i + 1] - yv[i]);
      }
    }
  }
  return yv[n - 1];
}

float vypocetPalivaZOdporu(float rOhm) {
  if (!isfinite(rOhm) || rOhm < PALIVO_MIN_OHM) return NAN;

  // Tabulkový výpočet s lineární extrapolací za plnou nádrž
  // Tabulka: 300Ω→3.5L … 10Ω→44L (sestupně), extrapolujeme pod 10Ω na max 45L
  const float rLo  = palivoOhmTab[PALIVO_TAB_N - 1];  // 10Ω
  const float rLo2 = palivoOhmTab[PALIVO_TAB_N - 2];  // 75Ω
  const float lHi  = palivoLitruTab[PALIVO_TAB_N - 1];  // 44L
  const float lHi2 = palivoLitruTab[PALIVO_TAB_N - 2];  // 33L
  if (rOhm < rLo) {
    float slope  = (lHi - lHi2) / (rLo - rLo2);  // negativní: R klesá, L roste
    float extraL = lHi + slope * (rOhm - rLo);
    return omezNaRozsah(extraL, 0.0f, 45.0f);
  }
  return omezNaRozsah(interpolaceN(rOhm, palivoOhmTab, palivoLitruTab, PALIVO_TAB_N), 0.0f, 45.0f);
}

// Porovnání floatů – správně ošetří přechod na/z NAN.
bool zmenenaHodnota(float current, float& previous, float tolerance) {
  bool currNan = isnan(current);
  bool prevNan = isnan(previous);
  if (currNan != prevNan) {
    previous = current;
    return true;
  }
  if (currNan) return false;
  if (fabsf(current - previous) >= tolerance) {
    previous = current;
    return true;
  }
  return false;
}
