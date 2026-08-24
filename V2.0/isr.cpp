#include <Arduino.h>
#include "globals.h"

// ====== ISR – přerušovací rutiny (IRAM) ======

void IRAM_ATTR fuelPulse() {
  uint32_t now = micros();
  int level = digitalRead(FUEL_PIN);

  // podle směru logiky – když zjistíme, že je to obráceně, stačí změnit vstrikAktivniNizko = false;
  bool isOpen = vstrikAktivniNizko ? (level == LOW) : (level == HIGH);

  if (isOpen && !vstrikOtevreno) {
    // začátek pulzů (vstřik otevřený)
    vstrikOtevreno = true;
    vstrikPosledniHranaUs = now;
  } else if (!isOpen && vstrikOtevreno) {
    // konec pulzů (vstřik se zavřel)
    uint32_t w = now - vstrikPosledniHranaUs;
    uint32_t minPulseUs = vstrikFiltrUs;
    if (w >= minPulseUs && w <= VSTRIK_PULZ_MAX_US) {
      vstrikOtevreniUsAkum += w;
      vstrikPocetPulzu++;
    } else if (w < minPulseUs) {
      vstrikOdmitnutoKratke++;
    } else {
      vstrikOdmitnutoDlouhe++;
    }
    vstrikOtevreno = false;
  }
}

void IRAM_ATTR speedPulse() {
  uint32_t now = micros();
  uint32_t rawDelta = now - rychlostPosledniUs;
  rychlostPosledniUs = now;
  if (rawDelta < RYCHLOST_FILTR_US) {
    rychlostOdmitnutoGlitch++;
    return;
  }

  // Při pomalé jízdě odmítni izolovaný pulz, který přišel nepřirozeně brzo.
  if (rychlostPosledniPulzUs != 0) {
    uint32_t p = now - rychlostPosledniPulzUs;
    uint32_t reference = rychlostPeriodaUs;
    bool referencePouzitelna = reference >= RYCHLOST_FILTR_US &&
                               reference <= RYCHLOST_GLITCH_REF_MAX_US;
    bool podezreleKratka = referencePouzitelna &&
      ((uint64_t)p * 100ULL < (uint64_t)reference * RYCHLOST_GLITCH_POMER_PROC);
    if (podezreleKratka) {
      rychlostOdmitnutoGlitch++;
      return;
    }

    rychlostPeriodaUs = p;
    rychlostPeriodaBuf[rychlostPeriodaZapisIdx] = p;
    rychlostPeriodaZapisIdx = (rychlostPeriodaZapisIdx + 1) % RYCHLOST_PERIODA_N;
    if (rychlostPeriodaPlatnych < RYCHLOST_PERIODA_N) rychlostPeriodaPlatnych++;
  }

  rychlostPosledniPulzUs = now;
  rychlostPulzuPocet++;
}

void IRAM_ATTR rpmIsr() {
  uint32_t now = micros();
  if (now - otackyPosledniUs >= OTACKY_FILTR_US) {
    otackyHrany++;
    otackyPosledniUs = now;
  }
}
