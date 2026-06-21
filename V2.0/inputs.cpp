#include <Arduino.h>
#include "globals.h"

// ====== Vstupy, tlačítka, dveře, alarmy ======
static bool debounceAktivniLow(bool rawPressed, bool& lastRaw, bool& stable, unsigned long& lastChangeMs, unsigned long nowMs) {
  if (rawPressed != lastRaw) {
    lastRaw = rawPressed;
    lastChangeMs = nowMs;
  }
  if ((nowMs - lastChangeMs) >= TLACITKO_DRZI_ANTIZKMIT) {
    stable = lastRaw;
  }
  return stable;
}

void obsluzVstupy() {
  unsigned long currentTime = millis();

  // === BTN (GPIO27) – krátký stisk = nav dopředu / +; dlouhý stisk = vstup do editace ===
  static bool btnHeld      = false;
  static bool btnLongFired = false;
  static unsigned long btnDownT = 0;
  static bool btnRawLast = false;
  static bool btnStable = false;
  static unsigned long btnLastChangeMs = 0;
  bool buttonReading = debounceAktivniLow((digitalRead(BTN_PIN) == LOW), btnRawLast, btnStable, btnLastChangeMs, currentTime);

  if (buttonReading && !btnHeld) {
    btnHeld      = true;
    btnLongFired = false;
    btnDownT     = currentTime;
  }

  // BTN dlouhý stisk – vstup/výstup editace
  if (btnHeld && !btnLongFired && (currentTime - btnDownT) >= CAS_DLOUHEHO_STISKU) {
    btnLongFired = true;
    if (servisMenuAktivni) {
      if (smEditRezim) {
        // Výstup z editace zpět do navigace
        smEditRezim  = false;
        uiPrekreslit = true;
      } else {
        servisMenuVstupEditu();
      }
    }
  }

  // BTN krátký stisk – aktivuje se při uvolnění (pokud dlouhý nestačil)
  if (!buttonReading && btnHeld) {
    if (!btnLongFired) {
      if (servisMenuAktivni) {
        if (smEditRezim) {
          servisMenuPlus();
        } else {
          servisMenuNext();
        }
      } else {
        prepniStrankuSAnimaci(1);       // doprava: 1→2→3→4→5→1
        oznacKonfiguraciJakoZmenenou();
      }
    }
    btnHeld = false;
  }

  if (!buttonReading && !btnHeld) {
    // idle
  }

  // === RESET (GPIO15) – krátký stisk = doleva / edit menu; dlouhý stisk = servis menu ===
  static bool resetHeld      = false;
  static bool resetLongFired = false;
  static unsigned long resetDownT = 0;
  static bool resetRawLast = false;
  static bool resetStable = false;
  static unsigned long resetLastChangeMs = 0;
  bool resetReading = debounceAktivniLow((digitalRead(RESET_PIN) == LOW), resetRawLast, resetStable, resetLastChangeMs, currentTime);

  if (resetReading && !resetHeld) {
    resetHeld      = true;
    resetLongFired = false;
    resetDownT     = currentTime;
  }

  // Dlouhý stisk – aktivuje se jednou po překročení prahu
  if (resetHeld && !resetLongFired && (currentTime - resetDownT) >= CAS_DLOUHEHO_STISKU) {
    resetLongFired = true;
    if (servisMenuAktivni) {
      servisMenuUlozAZavri();
    } else {
      servisMenuOtevri();
    }
  }

  // Krátký stisk – aktivuje se při uvolnění (pokud dlouhý nestačil)
  if (!resetReading && resetHeld) {
    if (!resetLongFired) {
      if (servisMenuAktivni) {
        if (smEditRezim) {
          servisMenuMinus();
        } else {
          servisMenuPrev();
        }
      } else {
        prepniStrankuSAnimaci(-1);      // doleva: 5→4→3→2→1→5
        oznacKonfiguraciJakoZmenenou();
      }
    }
    resetHeld = false;
  }

  // Dveře – jeden společný pin, debounce
  bool doorReading = digitalRead(DOOR_PIN);
  if (doorReading != dverePosledniStav) { dverePosledniCas = currentTime; }
  if ((currentTime - dverePosledniCas) > DVERE_ANTIZKMIT) {
    dvereOtevrene = (doorReading == LOW);
  }

  dverePosledniStav = doorReading;

  // Low fuel warning s hysterezí
  if (isnan(hladinaPaliva)) {
    upozorneniNizkePalivo = false;
  } else if (upozorneniNizkePalivoPovoleno && !upozorneniNizkePalivo && hladinaPaliva <= NIZKE_PALIVO_ZAP_L) {
    upozorneniNizkePalivo = true;
    predchoziStranka = stranka;
    zrusAnimaciStranky();
    stranka = 3;
    nizkePalivoZobrazeni = currentTime;
  } else if (upozorneniNizkePalivo && hladinaPaliva >= NIZKE_PALIVO_VYP_L) {
    upozorneniNizkePalivo = false;
  }

  if (upozorneniNizkePalivo && (currentTime - nizkePalivoZobrazeni) >= NIZKE_PALIVO_UPOZORNENI_MS) {
    if (stranka == 3) {
      zrusAnimaciStranky();
      stranka = (predchoziStranka >= 1 && predchoziStranka <= 5) ? predchoziStranka : 1;
    }
    nizkePalivoZobrazeni = currentTime;  // předejít opakovanému přepínání stránky
  }

  // blikání
  if (currentTime - blikaniPosledniCas >= BLIKANI_INTERVAL) {
    blikaniPosledniCas = currentTime;
    blikaniZapnuto = !blikaniZapnuto;
  }
}

void obsluzDenniReset() {
  if (denniMetry_u64 < DENNI_RESET_LIMIT_M) return;

  uint64_t predResetem = denniMetry_u64;
  denniMetry_u64 %= DENNI_RESET_LIMIT_M;
  denniVzdalenost = (float)(denniMetry_u64 / 1000.0);
  uiPrekreslit = true;

  char detail[80];
  snprintf(detail, sizeof(detail), "daily_m=%llu -> %llu",
           (unsigned long long)predResetem,
           (unsigned long long)denniMetry_u64);
  zapisErrorLog("INFO", "DAILY_RESET", detail);

  if (sdPripravena && !ulozStatistikySD()) {
    zapisErrorLog("ERROR", "SD_SAVE_FAIL", "daily reset save failed");
  }
}
