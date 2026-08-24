#include <Arduino.h>
#include "globals.h"

// ====== Servis menu ======
// Navigace:
//   BTN  (krátký stisk) = další položka (dolů)
//   RESET (krátký stisk) = edituj / toggle / akce
//   RESET (dlouhý stisk) = ulož a zavři

bool servisMenuAktivni  = false;
int  servisMenuPolozka  = 0;
bool smZpravaAktivni    = false;
bool smEditRezim        = false;  // false=navigace, true=editace hodnoty

static int smScroll = 0;   // první viditelná položka (scrolling okno)

// Stavová zpráva po uložení (zobrazí se 2s)
static bool       smZpravaOk = false;
static unsigned long smZpravaCas  = 0;
static bool smResetPotvrzeno = false;
static bool smDetailAktivni = false;
static char smZpravaText[20] = "";
constexpr unsigned long SM_ZPRAVA_MS = 1800;

// ===== Položky =====
// Typ: 0=bool, 1=hodnota, 2=akce, 3=info
struct SmPolozka {
  const char* nazev;
  uint8_t     typ;    // 0=bool, 1=hodnota, 2=akce, 3=info
};

enum SmIndex {
  SM_ALARM_VODY = 0,
  SM_ALARM_PALIVO,
  SM_OFFSET_VENKU,
  SM_OFFSET_VODY,
  SM_OFFSET_PALIVO,
  SM_OFFSET_BATERIE,
  SM_RPM_NAHORU,
  SM_RPM_DOLU,
  SM_PULZY_KM,
  SM_SPOTREBA_KOREKCE,
  SM_JAS_OLED,
  SM_ANIMACE,
  SM_NAJETO,
  SM_OLEJ_INFO,
  SM_OLEJ_RESET,
  SM_ROZVODY_INFO,
  SM_ROZVODY_RESET,
  SM_VAROVANI,
  SM_VERZE,
  SM_RESET_STAT
};

static const SmPolozka smTab[] = {
  { "Alarm vody",  0 },   // 0 – alarmyPovoleny
  { "Al. palivo",  0 },   // 1 – upozorneniNizkePalivoPovoleno
  { "Ofst venku",  1 },   // 2 – teplotaVenkuOffsetC  ±0.5°C  -10..10
  { "Ofst vody",   1 },   // 3 – teplotaOffsetC        ±0.5°C  -10..10
  { "Ofst paliv",  1 },   // 4 – palivoOffsetL          ±0.5L
  { "Ofst batt",   1 },   // 5 – napetiOffsetV          ±0.1V
  { "RPM nahoru",  1 },   // 6 – gearRpmNahoru          ±100 RPM
  { "RPM dolu",    1 },   // 7 – gearRpmDolu            ±100 RPM
  { "Pulzy/km",    1 },   // 8 – pulzyNaKm              toggle 3920/7840
  { "Spotr kor",   1 },
  { "Jas OLED",    1 },
  { "Animace",     1 },
  { "Najeto km",   3 },
  { "Olej",        3 },
  { "Reset olej",  2 },
  { "Rozvody",     3 },
  { "Reset rozv.", 2 },
  { "Varovani",    3 },
  { "Verze",       3 },
  { "Reset stat.", 2 },
};
static constexpr int SM_N = (int)(sizeof(smTab) / sizeof(smTab[0]));

static void smServisHodnota(int64_t zbyvaM, uint64_t intervalM, bool resetNastaven,
                            char* buf, size_t sz) {
  if (intervalM == 0) snprintf(buf, sz, "VYP");
  else if (!vozidloNajetoNastaveno) snprintf(buf, sz, "BEZ KM");
  else if (!resetNastaven || zbyvaM == INT64_MAX) snprintf(buf, sz, "RESET");
  else if (zbyvaM <= 0) snprintf(buf, sz, "PO TERM");
  else if (zbyvaM >= 999999000LL) snprintf(buf, sz, ">999999");
  else snprintf(buf, sz, "%ldkm", (long)((zbyvaM + 999) / 1000));
}

static void smZobrazZpravu(bool ok, const char* text) {
  smZpravaOk = ok;
  smZpravaAktivni = true;
  smZpravaCas = millis();
  snprintf(smZpravaText, sizeof(smZpravaText), "%s", text ? text : "");
}

// ===== Hodnota jako string =====
static void smHodnota(int idx, char* buf, size_t sz) {
  switch (idx) {
    case SM_ALARM_VODY: snprintf(buf, sz, "%s", alarmyPovoleny ? "ZAP" : "VYP"); break;
    case SM_ALARM_PALIVO: snprintf(buf, sz, "%s", upozorneniNizkePalivoPovoleno ? "ZAP" : "VYP"); break;
    case SM_OFFSET_VENKU: snprintf(buf, sz, "%.1fC", teplotaVenkuOffsetC); break;
    case SM_OFFSET_VODY: snprintf(buf, sz, "%.1fC", teplotaOffsetC); break;
    case SM_OFFSET_PALIVO: snprintf(buf, sz, "%.1fL", palivoOffsetL); break;
    case SM_OFFSET_BATERIE: snprintf(buf, sz, "%.1fV", napetiOffsetV); break;
    case SM_RPM_NAHORU: snprintf(buf, sz, "%.0f", gearRpmNahoru); break;
    case SM_RPM_DOLU: snprintf(buf, sz, "%.0f", gearRpmDolu); break;
    case SM_PULZY_KM: snprintf(buf, sz, "%.0f", pulzyNaKm); break;
    case SM_SPOTREBA_KOREKCE: {
      int pct = (int)lroundf((spotrebaKorekce - 1.0f) * 100.0f);
      snprintf(buf, sz, "%+d%%", pct);
      break;
    }
    case SM_JAS_OLED: {
      static const char* hodnoty[] = { "MAX", "STRED", "NOC", "AUTO" };
      snprintf(buf, sz, "%s", hodnoty[oledJasRezim <= OLED_JAS_AUTO_REZIM ? oledJasRezim : 0]);
      break;
    }
    case SM_ANIMACE: {
      static const char* hodnoty[] = { "VYP", "RYCH", "NORMAL", "POMAL" };
      snprintf(buf, sz, "%s", hodnoty[animaceRezim <= ANIMACE_POMALA_REZIM ? animaceRezim : 0]);
      break;
    }
    case SM_NAJETO:
      if (vozidloNajetoNastaveno) snprintf(buf, sz, "%.0f", (double)vozidloNajetoM_u64 / 1000.0);
      else snprintf(buf, sz, "NENAST");
      break;
    case SM_OLEJ_INFO:
      smServisHodnota(servisOlejZbyvaM(), servisOlejIntervalM_u64, servisOlejNastaven, buf, sz);
      break;
    case SM_OLEJ_RESET:
      snprintf(buf, sz, "%s", smResetPotvrzeno ? "POTVRD" : ">>> OK");
      break;
    case SM_ROZVODY_INFO:
      smServisHodnota(servisRozvodyZbyvaM(), servisRozvodyIntervalM_u64, servisRozvodyNastaveny, buf, sz);
      break;
    case SM_ROZVODY_RESET:
      snprintf(buf, sz, "%s", smResetPotvrzeno ? "POTVRD" : ">>> OK");
      break;
    case SM_VAROVANI: snprintf(buf, sz, "%s", aktivniVarovaniMask() ? "AKTIV" : "OK"); break;
    case SM_VERZE: snprintf(buf, sz, "%s", FW_VERSION); break;
    case SM_RESET_STAT: snprintf(buf, sz, "%s", smResetPotvrzeno ? "POTVRD" : ">>> OK"); break;
    default: buf[0] = '\0'; break;
  }
}

// ===== Otevřít menu =====
void servisMenuOtevri() {
  servisMenuAktivni = true;
  servisMenuPolozka = 0;
  smScroll          = 0;
  smEditRezim       = false;
  smResetPotvrzeno  = false;
  smDetailAktivni   = false;
  smZpravaText[0]   = '\0';
  uiPrekreslit      = true;
}

// ===== Navigace dopředu (BTN krátký – navigační režim) =====
void servisMenuNext() {
  if (smEditRezim) return;
  smDetailAktivni = false;
  servisMenuPolozka = (servisMenuPolozka + 1) % SM_N;
  smResetPotvrzeno = false;
  if (servisMenuPolozka >= smScroll + 3) smScroll = servisMenuPolozka - 2;
  if (servisMenuPolozka < smScroll)      smScroll = servisMenuPolozka;
  uiPrekreslit = true;
}

// ===== Navigace zpět (RESET krátký – navigační režim) =====
void servisMenuPrev() {
  if (smEditRezim) return;
  smDetailAktivni = false;
  servisMenuPolozka = (servisMenuPolozka - 1 + SM_N) % SM_N;
  smResetPotvrzeno = false;
  if (servisMenuPolozka < smScroll)           smScroll = servisMenuPolozka;
  if (servisMenuPolozka >= smScroll + 3)      smScroll = servisMenuPolozka - 2;
  uiPrekreslit = true;
}

// ===== Vstup do editace (BTN dlouhý – navigační režim) =====
void servisMenuVstupEditu() {
  if (!servisMenuAktivni || smEditRezim) return;
  if (smDetailAktivni) {
    smDetailAktivni = false;
    uiPrekreslit = true;
    return;
  }
  if (smTab[servisMenuPolozka].typ == 3) {
    smDetailAktivni = true;
    uiPrekreslit = true;
    return;
  }
  // Každá resetovací akce vyžaduje druhé potvrzení.
  if (smTab[servisMenuPolozka].typ == 2) {
    if (!smResetPotvrzeno) {
      smResetPotvrzeno = true;
      uiPrekreslit = true;
      return;
    }
    servisMenuPlus();
    smResetPotvrzeno = false;
    return;
  }
  smEditRezim  = true;
  uiPrekreslit = true;
}

// ===== Increment / + (BTN krátký – editační režim) =====
void servisMenuPlus() {
  switch (servisMenuPolozka) {
    case SM_ALARM_VODY: alarmyPovoleny = !alarmyPovoleny; break;
    case SM_ALARM_PALIVO: upozorneniNizkePalivoPovoleno = !upozorneniNizkePalivoPovoleno; break;
    case SM_OFFSET_VENKU:
      teplotaVenkuOffsetC += 0.5f;
      if (teplotaVenkuOffsetC > 10.0f) teplotaVenkuOffsetC = 10.0f;
      break;
    case SM_OFFSET_VODY:
      teplotaOffsetC += 0.5f;
      if (teplotaOffsetC > 10.0f) teplotaOffsetC = 10.0f;
      break;
    case SM_OFFSET_PALIVO:
      palivoOffsetL += 0.5f;
      if (palivoOffsetL > PALIVO_OFFSET_MAX_L) palivoOffsetL = PALIVO_OFFSET_MAX_L;
      break;
    case SM_OFFSET_BATERIE:
      napetiOffsetV += 0.1f;
      if (napetiOffsetV > NAPETI_OFFSET_MAX_V) napetiOffsetV = NAPETI_OFFSET_MAX_V;
      break;
    case SM_RPM_NAHORU:
      gearRpmNahoru += 100.0f;
      if (gearRpmNahoru > GEAR_RPM_NAHORU_MAX) gearRpmNahoru = GEAR_RPM_NAHORU_MAX;
      if (gearRpmDolu > gearRpmNahoru - 100.0f) gearRpmDolu = gearRpmNahoru - 100.0f;
      break;
    case SM_RPM_DOLU:
      gearRpmDolu += 100.0f;
      if (gearRpmDolu > GEAR_RPM_DOLU_MAX) gearRpmDolu = GEAR_RPM_DOLU_MAX;
      if (gearRpmDolu > gearRpmNahoru - 100.0f) gearRpmDolu = gearRpmNahoru - 100.0f;
      break;
    case SM_PULZY_KM:
      pulzyNaKm = (pulzyNaKm == 7840.0f) ? 3920.0f : 7840.0f;
      break;
    case SM_SPOTREBA_KOREKCE:
      spotrebaKorekce += 0.01f;
      if (spotrebaKorekce > SPOTREBA_KOREKCE_MAX) spotrebaKorekce = SPOTREBA_KOREKCE_MAX;
      break;
    case SM_JAS_OLED:
      oledJasRezim = (uint8_t)((oledJasRezim + 1U) % (OLED_JAS_AUTO_REZIM + 1U));
      aktualizujJasDispleje(true);
      break;
    case SM_ANIMACE:
      animaceRezim = (uint8_t)((animaceRezim + 1U) % (ANIMACE_POMALA_REZIM + 1U));
      break;
    case SM_NAJETO:
    case SM_OLEJ_INFO:
    case SM_ROZVODY_INFO:
    case SM_VAROVANI:
      break;
    case SM_OLEJ_RESET:
      if (!vozidloNajetoNastaveno) {
        smZobrazZpravu(false, "NASTAV KM");
      } else if (servisOlejIntervalM_u64 == 0) {
        smZobrazZpravu(false, "NASTAV INTERVAL");
      } else {
        bool ok = resetujServisOlej();
        smZobrazZpravu(ok, ok ? "OLEJ RESET" : "CHYBA SD!");
        if (ok) blikniDisplej(3, 100, 120);
      }
      break;
    case SM_ROZVODY_RESET:
      if (!vozidloNajetoNastaveno) {
        smZobrazZpravu(false, "NASTAV KM");
      } else if (servisRozvodyIntervalM_u64 == 0) {
        smZobrazZpravu(false, "NASTAV INTERVAL");
      } else {
        bool ok = resetujServisRozvody();
        smZobrazZpravu(ok, ok ? "ROZVODY RESET" : "CHYBA SD!");
        if (ok) blikniDisplej(3, 100, 120);
      }
      break;
    case SM_VERZE:
      break;
    case SM_RESET_STAT:
      {
        bool ok = vynulujStatistiky(true);
        smZobrazZpravu(ok, ok ? "STAT RESET" : "CHYBA SD!");
        if (ok) blikniDisplej(3, 100, 120);
      }
      break;
  }
  if (smTab[servisMenuPolozka].typ != 2 && smTab[servisMenuPolozka].typ != 3) oznacKonfiguraciJakoZmenenou();
  uiPrekreslit = true;
}

// ===== Decrement / − (RESET krátký – editační režim) =====
void servisMenuMinus() {
  switch (servisMenuPolozka) {
    case SM_ALARM_VODY: alarmyPovoleny = !alarmyPovoleny; break;
    case SM_ALARM_PALIVO: upozorneniNizkePalivoPovoleno = !upozorneniNizkePalivoPovoleno; break;
    case SM_OFFSET_VENKU:
      teplotaVenkuOffsetC -= 0.5f;
      if (teplotaVenkuOffsetC < -10.0f) teplotaVenkuOffsetC = -10.0f;
      break;
    case SM_OFFSET_VODY:
      teplotaOffsetC -= 0.5f;
      if (teplotaOffsetC < -10.0f) teplotaOffsetC = -10.0f;
      break;
    case SM_OFFSET_PALIVO:
      palivoOffsetL -= 0.5f;
      if (palivoOffsetL < PALIVO_OFFSET_MIN_L) palivoOffsetL = PALIVO_OFFSET_MIN_L;
      break;
    case SM_OFFSET_BATERIE:
      napetiOffsetV -= 0.1f;
      if (napetiOffsetV < NAPETI_OFFSET_MIN_V) napetiOffsetV = NAPETI_OFFSET_MIN_V;
      break;
    case SM_RPM_NAHORU:
      gearRpmNahoru -= 100.0f;
      if (gearRpmNahoru < GEAR_RPM_NAHORU_MIN) gearRpmNahoru = GEAR_RPM_NAHORU_MIN;
      if (gearRpmDolu > gearRpmNahoru - 100.0f) gearRpmDolu = gearRpmNahoru - 100.0f;
      break;
    case SM_RPM_DOLU:
      gearRpmDolu -= 100.0f;
      if (gearRpmDolu < GEAR_RPM_DOLU_MIN) gearRpmDolu = GEAR_RPM_DOLU_MIN;
      break;
    case SM_PULZY_KM:
      pulzyNaKm = (pulzyNaKm == 7840.0f) ? 3920.0f : 7840.0f;
      break;
    case SM_SPOTREBA_KOREKCE:
      spotrebaKorekce -= 0.01f;
      if (spotrebaKorekce < SPOTREBA_KOREKCE_MIN) spotrebaKorekce = SPOTREBA_KOREKCE_MIN;
      break;
    case SM_JAS_OLED:
      oledJasRezim = (oledJasRezim == OLED_JAS_MAX_REZIM)
                       ? OLED_JAS_AUTO_REZIM
                       : (uint8_t)(oledJasRezim - 1U);
      aktualizujJasDispleje(true);
      break;
    case SM_ANIMACE:
      animaceRezim = (animaceRezim == ANIMACE_VYP_REZIM)
                       ? ANIMACE_POMALA_REZIM
                       : (uint8_t)(animaceRezim - 1U);
      break;
    case SM_NAJETO:
    case SM_OLEJ_INFO:
    case SM_OLEJ_RESET:
    case SM_ROZVODY_INFO:
    case SM_ROZVODY_RESET:
    case SM_VAROVANI:
    case SM_VERZE:
      break;
    case SM_RESET_STAT:
      // Reset – minus nedělá nic, trigger jen přes +
      break;
  }
  if (gearRpmDolu < GEAR_RPM_DOLU_MIN) gearRpmDolu = GEAR_RPM_DOLU_MIN;
  if (smTab[servisMenuPolozka].typ != 2 && smTab[servisMenuPolozka].typ != 3) oznacKonfiguraciJakoZmenenou();
  uiPrekreslit = true;
}

// ===== Zachováno pro zpětnou kompatibilitu =====
void servisMenuEdit() {
  servisMenuPlus();
}

// ===== Ulož a zavři (RESET dlouhý stisk) =====
void servisMenuUlozAZavri() {
  bool ok = ulozKonfiguraciSdNyni();
  servisMenuAktivni = false;
  smEditRezim     = false;
  smResetPotvrzeno = false;
  smDetailAktivni = false;
  smZobrazZpravu(ok, ok ? "ULOZENO" : "CHYBA SD!");
  uiPrekreslit    = true;
}

static void smVykresliNadpis(const char* nadpis) {
  u8g2.setFont(u8g2_font_profont12_tr);
  int x = (128 - u8g2.getStrWidth(nadpis)) / 2;
  u8g2.drawStr(x, 10, nadpis);
  u8g2.drawLine(0, 12, 127, 12);
}

static void smVykresliServisDetail(const char* nadpis, uint64_t intervalM,
                                   uint64_t posledniM, bool resetNastaven, int64_t zbyvaM) {
  smVykresliNadpis(nadpis);
  u8g2.setFont(u8g2_font_profont12_tr);
  char buf[24];
  if (intervalM == 0) snprintf(buf, sizeof(buf), "Interval: VYPNUT");
  else snprintf(buf, sizeof(buf), "Interval: %lu km", (unsigned long)(intervalM / 1000ULL));
  u8g2.drawStr(3, 27, buf);

  if (!vozidloNajetoNastaveno) snprintf(buf, sizeof(buf), "Najezd: NENASTAVEN");
  else if (!resetNastaven) snprintf(buf, sizeof(buf), "Po vymene resetuj");
  else snprintf(buf, sizeof(buf), "Posledni: %lu km", (unsigned long)(posledniM / 1000ULL));
  u8g2.drawStr(3, 43, buf);

  if (zbyvaM == INT64_MAX) snprintf(buf, sizeof(buf), "Zbyva: ---");
  else if (zbyvaM <= 0) snprintf(buf, sizeof(buf), "Po terminu: %.1f km", (double)(-zbyvaM) / 1000.0);
  else snprintf(buf, sizeof(buf), "Zbyva: %.1f km", (double)zbyvaM / 1000.0);
  u8g2.drawStr(3, 59, buf);
}

static void smVykresliVarovaniDetail() {
  smVykresliNadpis("-- VAROVANI --");
  u8g2.setFont(u8g2_font_profont12_tr);
  uint16_t mask = aktivniVarovaniMask();
  if (mask == 0) {
    const char* ok = "ZADNE VAROVANI";
    u8g2.drawStr((128 - u8g2.getStrWidth(ok)) / 2, 39, ok);
    return;
  }

  struct VarovaniNazev { uint16_t bit; const char* text; };
  static const VarovaniNazev nazvy[] = {
    { VAROVANI_SERVIS_OLEJ, "SERVIS OLEJ" },
    { VAROVANI_SERVIS_ROZVODY, "SERVIS ROZVODY" },
    { VAROVANI_PREHRATI, "PREHRATI MOTORU" },
    { VAROVANI_BATERIE, "NIZKE NAPETI" },
    { VAROVANI_SD, "CHYBA SD" },
    { VAROVANI_RTC, "CHYBA RTC" },
    { VAROVANI_ADS, "CHYBA ADS" },
    { VAROVANI_CIDLO, "CHYBA CIDLA" },
    { VAROVANI_PALIVO, "NIZKE PALIVO" }
  };

  const char* aktivni[9];
  int pocet = 0;
  for (const auto& polozka : nazvy) {
    if (mask & polozka.bit) aktivni[pocet++] = polozka.text;
  }
  int zobrazit = pocet > 3 ? 2 : pocet;
  for (int i = 0; i < zobrazit; ++i) u8g2.drawStr(3, 27 + i * 16, aktivni[i]);
  if (pocet > 3) {
    char dalsi[20];
    snprintf(dalsi, sizeof(dalsi), "+%d DALSI", pocet - 2);
    u8g2.drawStr(3, 59, dalsi);
  }
}

// ===== Vykreslení =====
void vykresliServisMenu() {
  // --- Stavová zpráva ULOŽENO / CHYBA SD ---
  if (smZpravaAktivni) {
    if (millis() - smZpravaCas < SM_ZPRAVA_MS) {
      u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_helvB12_te);
        u8g2.setDrawColor(1);
        const char* msg = smZpravaText[0] ? smZpravaText : (smZpravaOk ? "ULOŽENO" : "CHYBA SD!");
        u8g2.drawUTF8((128 - u8g2.getUTF8Width(msg)) / 2, 38, msg);
      u8g2.sendBuffer();
      return;
    } else {
      smZpravaAktivni = false;
    }
  }

  if (!servisMenuAktivni) return;

  if (smDetailAktivni) {
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    if (servisMenuPolozka == SM_VERZE) {
      smVykresliNadpis("-- VERZE --");
      u8g2.setFont(u8g2_font_helvB10_tf);
      u8g2.drawStr((128 - u8g2.getStrWidth(FW_NAME)) / 2, 30, FW_NAME);
      u8g2.setFont(u8g2_font_helvB12_tf);
      u8g2.drawStr((128 - u8g2.getStrWidth(FW_VERSION)) / 2, 48, FW_VERSION);
      u8g2.setFont(u8g2_font_profont12_tr);
      u8g2.drawStr((128 - u8g2.getStrWidth(FW_DATE)) / 2, 62, FW_DATE);
    } else if (servisMenuPolozka == SM_NAJETO) {
      smVykresliNadpis("-- NAJEZD --");
      if (vozidloNajetoNastaveno) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.1f km", (double)vozidloNajetoM_u64 / 1000.0);
        u8g2.setFont(u8g2_font_helvB12_tf);
        u8g2.drawStr((128 - u8g2.getStrWidth(buf)) / 2, 40, buf);
      } else {
        u8g2.setFont(u8g2_font_profont12_tr);
        u8g2.drawStr(15, 34, "NAJEZD NENASTAVEN");
        u8g2.drawStr(18, 52, "BT: km <hodnota>");
      }
    } else if (servisMenuPolozka == SM_OLEJ_INFO) {
      smVykresliServisDetail("-- OLEJ --", servisOlejIntervalM_u64,
                             servisOlejPosledniM_u64, servisOlejNastaven, servisOlejZbyvaM());
    } else if (servisMenuPolozka == SM_ROZVODY_INFO) {
      smVykresliServisDetail("-- ROZVODY --", servisRozvodyIntervalM_u64,
                             servisRozvodyPosledniM_u64, servisRozvodyNastaveny, servisRozvodyZbyvaM());
    } else if (servisMenuPolozka == SM_VAROVANI) {
      smVykresliVarovaniDetail();
    }
    u8g2.sendBuffer();
    return;
  }

  u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont12_tr);
    u8g2.setDrawColor(1);
    u8g2.setBitmapMode(1);

    // --- Nadpis ---
    const char* hlavicka = smEditRezim ? "-- EDIT --" : "-- SERVIS --";
    u8g2.drawStr((128 - u8g2.getStrWidth(hlavicka)) / 2, 10, hlavicka);
    u8g2.drawLine(0, 12, 127, 12);

    // --- Položky (3 viditelné) ---
    for (int i = 0; i < 3; i++) {
      int idx = smScroll + i;
      if (idx >= SM_N) break;

      int y = 24 + i * 16;   // baseline: 24, 40, 56
      bool selected = (idx == servisMenuPolozka);

      char valBuf[10];
      smHodnota(idx, valBuf, sizeof(valBuf));

      char line[24];
      if (selected && smEditRezim) {
        // Editační režim: zobraz šipky < hodnota >
        char editVal[14];
        snprintf(editVal, sizeof(editVal), "<%s>", valBuf);
        snprintf(line, sizeof(line), "%-11s%6s", smTab[idx].nazev, editVal);
      } else {
        snprintf(line, sizeof(line), "%-11s%6s", smTab[idx].nazev, valBuf);
      }

      if (selected) {
        u8g2.drawBox(0, y - 11, 128, 14);
        u8g2.setDrawColor(0);
        u8g2.drawStr(2, y, line);
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(2, y, line);
      }
    }

    // --- Scroll indikátor (puntík napravo) ---
    if (SM_N > 3) {
      int dotY = 13 + (int)(((float)servisMenuPolozka / (SM_N - 1)) * 38.0f);
      u8g2.drawBox(126, dotY, 2, 2);
    }

  u8g2.sendBuffer();
}
