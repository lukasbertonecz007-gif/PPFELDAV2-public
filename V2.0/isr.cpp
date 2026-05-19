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
    if (w >= VSTRIK_FILTR_US && w < 500000) {  // ignoruj šum + šílené dlouhé pulzy
      vstrikOtevreniUsAkum += w;
      vstrikPocetPulzu++;
    }
    vstrikOtevreno = false;
  }
}

void IRAM_ATTR speedPulse() {
  uint32_t now = micros();
  if (now - rychlostPosledniUs >= RYCHLOST_FILTR_US) {
    // perioda mezi dvěma po sobě jdoucími validními pulzy
    if (rychlostPosledniPulzUs != 0) {
      uint32_t p = now - rychlostPosledniPulzUs;
      rychlostPeriodaUs = p;
      rychlostPeriodaBuf[rychlostPeriodaZapisIdx] = p;
      rychlostPeriodaZapisIdx = (rychlostPeriodaZapisIdx + 1) % RYCHLOST_PERIODA_N;
      if (rychlostPeriodaPlatnych < RYCHLOST_PERIODA_N) rychlostPeriodaPlatnych++;
    }
    rychlostPosledniPulzUs = now;

    rychlostPulzuPocet++;
    rychlostPosledniUs = now;
  }
}

void IRAM_ATTR rpmIsr() {
  uint32_t now = micros();
  if (now - otackyPosledniUs >= OTACKY_FILTR_US) {
    otackyHrany++;
    otackyPosledniUs = now;
  }
}
