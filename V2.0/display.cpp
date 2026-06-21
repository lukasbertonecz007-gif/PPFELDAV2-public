#include <Arduino.h>
#include "globals.h"

// ====== Displej ======

// Blikání – neblokující stavový automat (volá se v loopu)
static uint8_t  blinkTimesRemaining = 0;
static uint8_t  blinkPhase = 0;          // 0=OFF, 1=ON
static uint16_t blinkOffMs = 120;
static uint16_t blinkOnMs  = 120;
static unsigned long blinkNextMs = 0;

// Animace pouze informační části pod hodinami. Pravých 26 px patří ukazateli kvaltu.
constexpr int INFO_ANIM_X = 0;
constexpr int INFO_ANIM_Y = 30;
constexpr int INFO_ANIM_W = 102;
constexpr int INFO_ANIM_H = 34;
constexpr int INFO_ANIM_TILE_ROWS = (INFO_ANIM_H + 7) / 8;
constexpr int INFO_ANIM_BYTES = INFO_ANIM_W * INFO_ANIM_TILE_ROWS;

static uint8_t infoAnimStary[INFO_ANIM_BYTES];
static uint8_t infoAnimNovy[INFO_ANIM_BYTES];
static bool infoAnimAktivni = false;
static bool infoAnimNovyPripraven = false;
static bool infoObrazovkaPripravena = false;
static int8_t infoAnimSmer = 1;
static unsigned long infoAnimStartMs = 0;

unsigned long dobaAnimaceStrankyMs() {
  switch (animaceRezim) {
    case ANIMACE_VYP_REZIM: return 0;
    case ANIMACE_RYCHLA_REZIM: return ANIMACE_RYCHLA_MS;
    case ANIMACE_POMALA_REZIM: return ANIMACE_POMALA_MS;
    case ANIMACE_NORMAL_REZIM:
    default:
      return ANIMACE_NORMAL_MS;
  }
}

void aktualizujJasDispleje(bool vynutit) {
  static uint8_t posledniKontrast = 0;
  static bool kontrastNastaven = false;
  static unsigned long posledniKontrolaMs = 0;
  unsigned long now = millis();
  if (!vynutit && (now - posledniKontrolaMs) < 30000UL) return;
  posledniKontrolaMs = now;

  uint8_t kontrast = OLED_KONTRAST_MAX;
  if (oledJasRezim == OLED_JAS_STRED_REZIM) {
    kontrast = OLED_KONTRAST_STRED;
  } else if (oledJasRezim == OLED_JAS_NOC_REZIM) {
    kontrast = OLED_KONTRAST_NOC;
  } else if (oledJasRezim == OLED_JAS_AUTO_REZIM && Rtc.IsDateTimeValid()) {
    RtcDateTime dt = Rtc.GetDateTime();
    if (dt.Year() >= 2020 && dt.Year() <= 2099) {
      uint8_t hodina = dt.Hour();
      bool noc = hodina >= OLED_AUTO_NOC_OD_H || hodina < OLED_AUTO_NOC_DO_H;
      kontrast = noc ? OLED_KONTRAST_NOC : OLED_KONTRAST_MAX;
    }
  }

  if (vynutit || !kontrastNastaven || kontrast != posledniKontrast) {
    u8g2.setContrast(kontrast);
    posledniKontrast = kontrast;
    kontrastNastaven = true;
  }
}

static bool zachytInfoOblast(uint8_t* cil) {
  uint8_t* framebuffer = u8g2.getBufferPtr();
  if (!framebuffer || !cil) return false;

  memset(cil, 0, INFO_ANIM_BYTES);
  int displayW = u8g2.getDisplayWidth();
  for (int y = 0; y < INFO_ANIM_H; ++y) {
    int globalY = INFO_ANIM_Y + y;
    uint8_t maska = (uint8_t)(1U << (globalY & 7));
    int radek = (globalY >> 3) * displayW;
    int cilRadek = (y >> 3) * INFO_ANIM_W;
    uint8_t cilMaska = (uint8_t)(1U << (y & 7));
    for (int x = 0; x < INFO_ANIM_W; ++x) {
      if (framebuffer[radek + INFO_ANIM_X + x] & maska) {
        cil[cilRadek + x] |= cilMaska;
      }
    }
  }
  return true;
}

static void vykresliInfoSnimek(const uint8_t* snimek, int xOffset) {
  if (!snimek) return;

  for (int y = 0; y < INFO_ANIM_H; ++y) {
    int radek = (y >> 3) * INFO_ANIM_W;
    uint8_t maska = (uint8_t)(1U << (y & 7));
    for (int x = 0; x < INFO_ANIM_W; ++x) {
      int cilX = INFO_ANIM_X + x + xOffset;
      if (cilX < INFO_ANIM_X || cilX >= INFO_ANIM_X + INFO_ANIM_W) continue;
      if (snimek[radek + x] & maska) {
        u8g2.drawPixel(cilX, INFO_ANIM_Y + y);
      }
    }
  }
}

static void aplikujAnimaciInfo() {
  if (!infoAnimAktivni) return;

  if (!infoAnimNovyPripraven) {
    if (!zachytInfoOblast(infoAnimNovy)) {
      infoAnimAktivni = false;
      return;
    }
    infoAnimNovyPripraven = true;
  }

  unsigned long delkaAnimace = dobaAnimaceStrankyMs();
  if (delkaAnimace == 0) {
    infoAnimAktivni = false;
    infoAnimNovyPripraven = false;
    return;
  }

  unsigned long uplynulo = millis() - infoAnimStartMs;
  float prubeh = (uplynulo >= delkaAnimace) ? 1.0f : ((float)uplynulo / (float)delkaAnimace);
  float zbytek = 1.0f - prubeh;
  float zpomalenyPrubeh = 1.0f - zbytek * zbytek * zbytek;
  int posun = (int)lroundf(zpomalenyPrubeh * (float)INFO_ANIM_W);

  u8g2.setDrawColor(0);
  u8g2.drawBox(INFO_ANIM_X, INFO_ANIM_Y, INFO_ANIM_W, INFO_ANIM_H);
  u8g2.setDrawColor(1);

  if (infoAnimSmer > 0) {
    vykresliInfoSnimek(infoAnimStary, -posun);
    vykresliInfoSnimek(infoAnimNovy, INFO_ANIM_W - posun);
  } else {
    vykresliInfoSnimek(infoAnimStary, posun);
    vykresliInfoSnimek(infoAnimNovy, -INFO_ANIM_W + posun);
  }

  if (prubeh >= 1.0f) {
    infoAnimAktivni = false;
    infoAnimNovyPripraven = false;
  }
}

void zrusAnimaciStranky() {
  infoAnimAktivni = false;
  infoAnimNovyPripraven = false;
  infoObrazovkaPripravena = false;
}

bool animaceStrankyAktivni() {
  return infoAnimAktivni;
}

void prepniStrankuSAnimaci(int smer) {
  if (smer == 0) return;

  int novaStranka = stranka + ((smer > 0) ? 1 : -1);
  if (novaStranka > 5) novaStranka = 1;
  if (novaStranka < 1) novaStranka = 5;

  bool alarmTriggered = alarmyPovoleny && !isnan(teplotaVody) && teplotaVody > 90.0f;
  bool animovat = infoObrazovkaPripravena &&
                  dobaAnimaceStrankyMs() > 0 &&
                  !servisMenuAktivni && !smZpravaAktivni &&
                  !dvereOtevrene && !alarmTriggered;

  if (!animovat || !zachytInfoOblast(infoAnimStary)) {
    zrusAnimaciStranky();
    stranka = novaStranka;
    uiPrekreslit = true;
    return;
  }

  infoAnimSmer = (smer > 0) ? 1 : -1;
  infoAnimStartMs = millis();
  infoAnimNovyPripraven = false;
  infoAnimAktivni = true;
  stranka = novaStranka;
  uiPrekreslit = true;
}

void blikniDisplej(uint8_t times, uint16_t off_ms, uint16_t on_ms) {
  blinkTimesRemaining = times;
  blinkPhase = 0;
  blinkOffMs = off_ms;
  blinkOnMs  = on_ms;
  blinkNextMs = millis();
  u8g2.setPowerSave(1);  // hned zhasnout
}

// Voláno z hlavní smyčky – zpracuje jeden krok blikání
void obsluzBlikani() {
  if (blinkTimesRemaining == 0) return;
  if (millis() < blinkNextMs) return;

  if (blinkPhase == 0) {
    // bylo OFF → zapnout
    u8g2.setPowerSave(0);
    blinkNextMs = millis() + blinkOnMs;
    blinkPhase = 1;
  } else {
    // bylo ON → zhasnout, odpočítat cyklus
    blinkTimesRemaining--;
    if (blinkTimesRemaining > 0) {
      u8g2.setPowerSave(1);
      blinkNextMs = millis() + blinkOffMs;
      blinkPhase = 0;
    }
    // else: poslední cyklus, nech displej svítit, blinkTimesRemaining=0 → konec
  }
}

// Načítací obrazovka
// progress: 0.0 .. 1.0 — plnění baru
// stavText: popisek fáze zobrazený mezi logem a barem (NULL nebo "" = bez textu)
void vykresliStartovniObrazovku(float progress, const char* stavText) {
  if (progress < 0.0f) progress = 0.0f;
  if (progress > 1.0f) progress = 1.0f;

  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setBitmapMode(1);

  const int barMarginX = 4;
  const int barH = 8;
  const int barW = 128 - 2 * barMarginX;
  const int barX = barMarginX;
  const int barY = 64 - barH - 2;

  // Logo vzdy drzi pozici s rezervou pro text (prostor je vzdy rezervovan, text jen bliká)
  const bool maText = stavText && stavText[0];
  const int logoX = (128 - NACTENI_W) / 2;
  const int logoY = barY - NACTENI_H - 12;

  u8g2.drawXBMP(logoX, logoY, NACTENI_W, NACTENI_H, loading);

  // Stavovy text mezi logem a progress barem
  if (maText) {
    u8g2.setFont(u8g2_font_6x12_te);
    int tw = u8g2.getUTF8Width(stavText);
    u8g2.drawUTF8((128 - tw) / 2, barY - 1, stavText);
  }

  u8g2.drawFrame(barX, barY, barW, barH);
  int fill = (int)(progress * (barW - 2));
  if (fill > 0)
    u8g2.drawBox(barX + 1, barY + 1, fill, barH - 2);

  u8g2.sendBuffer();
}

void vykresliDisplej() {
  // --- SERVIS MENU / stavová zpráva – překryje vše ostatní ---
  if (servisMenuAktivni || smZpravaAktivni) {
    zrusAnimaciStranky();
    vykresliServisMenu();
    displayPosledniAktualizace = millis();
    return;
  }

  bool rtcValid = Rtc.IsDateTimeValid();
  bool alarmTriggered = (alarmyPovoleny && !isnan(teplotaVody) && (teplotaVody > 90.0f));
  if (dvereOtevrene || alarmTriggered) {
    zrusAnimaciStranky();
  }

  u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB24_tr);

    char timeStr[9];
    if (rtcValid) {
      RtcDateTime now = Rtc.GetDateTime();
      snprintf(timeStr, sizeof(timeStr), "%02u:%02u", (unsigned)now.Hour(), (unsigned)now.Minute());
    } else {
      strcpy(timeStr, "--:--");
    }

    u8g2.drawStr(20, 27, timeStr);
    u8g2.drawLine(0, 28, 128, 28);
    u8g2.drawLine(0, 29, 128, 29);

    if (dvereOtevrene) {
      u8g2.setFont(u8g2_font_helvB18_te);
      const char* txt = "DVEŘE!";
      int tw = u8g2.getUTF8Width(txt);
      u8g2.drawUTF8((128 - tw) / 2, 55, txt);

    } else {
      u8g2.setFont(u8g2_font_helvB12_tr);
      char buf[32];
      int y = 40;

      // Alarm vody: přepnout na stránku 1, blikat řádek s teplotou
      int renderStranka = (alarmTriggered && stranka != 1) ? 1 : stranka;
      {
        // --- STRÁNKA 1: Teploty ---
        if (renderStranka == 1) {
          const int ix   = 0;
          const int iy1  = 32;                          // bitmap řádek 1 (voda)
          const int iy2  = y + 18 - VODA_H + 1;         // bitmap řádek 2 (venku)
          const int ax   = VODA_W + 2;                  // x start šipky = 17
          const int textX = ax + 7;                     // x start textu = 24

          u8g2.setDrawColor(1);
          u8g2.setBitmapMode(1);

          // --- teplota vody + šipka doleva ---
          u8g2.drawXBMP(ix, iy1, VODA_W, VODA_H, water_icon_bits);
          {
            const int cy = iy1 + VODA_H / 2;
            u8g2.drawTriangle(ax, cy,  ax + 5, cy - 3,  ax + 5, cy + 3);
          }
          if (isnan(teplotaVody)) {
            u8g2.drawStr(textX, y + 5, "ERR");
          } else if (alarmTriggered && !blikaniZapnuto) {
            // blikání alarmu: prázdný řádek při vypnuté fázi
          } else {
            snprintf(buf, sizeof(buf), alarmTriggered ? "%.0f C !" : "%.1f C", teplotaVody);
            u8g2.drawStr(textX, y + 5, buf);
          }

          // --- teplota venku + šipka doprava ---
          u8g2.drawXBMP(ix, iy2, VODA_W, VODA_H, water_icon_bits);
          {
            const int cy = iy2 + VODA_H / 2;
            u8g2.drawTriangle(ax + 5, cy,  ax, cy - 3,  ax, cy + 3);
          }
          if (isnan(teplotaVenku)) {
            u8g2.drawStr(textX, y + 18, "ERR");
          } else {
            snprintf(buf, sizeof(buf), "%.1f C", teplotaVenku);
            u8g2.drawStr(textX, y + 18, buf);
          }

        // --- STRÁNKA 2: Spotřeba ---
        } else if (renderStranka == 2) {
          const int base1 = y + 3;
          const int ix = 0;
          const int iy = base1 - SPOTREBA_H + 1;

          u8g2.setDrawColor(1);
          u8g2.setBitmapMode(1);
          u8g2.drawXBMP(ix, iy, SPOTREBA_W, SPOTREBA_H, SPOTREBA_bits);
          snprintf(buf, sizeof(buf), "%.1fL/100", prumernaSpotreba);
          u8g2.drawStr(ix + SPOTREBA_W + 2, base1, buf);

          if (rychlostVozu >= 3.0f) {
            float inst = 0.0f;
            if (rychlostVozu > 0.1f) {
              inst = (palivoTokInst / rychlostVozu) * 100.0f;
            }
            if (palivoTokInst < 0.01f) inst = 0.0f;
            snprintf(buf, sizeof(buf), "%.1fL/100", inst);
          } else {
            snprintf(buf, sizeof(buf), "%.1fL/h", palivoTokInst);
          }
          u8g2.drawStr(17, y + 18, buf);

        // --- STRÁNKA 3: Rychlost a Palivo ---
        } else if (renderStranka == 3) {
          const int base1 = y + 3;
          const int ix_spd = 0;
          const int iy_spd = base1 - RYCHLOST_H + 1;

          u8g2.setDrawColor(1);
          u8g2.setBitmapMode(1);
          u8g2.drawXBMP(ix_spd, iy_spd, RYCHLOST_W, RYCHLOST_H, speed_icon_bits);
          snprintf(buf, sizeof(buf), " %.0f km/h", rychlostVozu < 5.0f ? 0.0f : rychlostVozu);
          u8g2.drawStr(ix_spd + RYCHLOST_W + 4, base1, buf);

          const int base2 = y + 18;
          const int ix_fuel = 0;
          const int iy_fuel = base2 - PALIVO_H + 1;

          bool lf_window = (millis() - nizkePalivoZobrazeni) < NIZKE_PALIVO_UPOZORNENI_MS;
          bool lf_blinking = (upozorneniNizkePalivoPovoleno && upozorneniNizkePalivo && lf_window);
          bool showIcon = !(lf_blinking && !blikaniZapnuto);

          if (showIcon) {
            u8g2.drawXBMP(ix_fuel, iy_fuel, PALIVO_W, PALIVO_H, fuel_icon_bits);
          }

          if (isnan(hladinaPaliva)) {
            u8g2.drawStr(ix_fuel + PALIVO_W + 4, base2, " ERR");
          } else {
            if (lf_blinking && blikaniZapnuto)
              snprintf(buf, sizeof(buf), " %.1f L!", hladinaPaliva);
            else
              snprintf(buf, sizeof(buf), " %.1f L", hladinaPaliva);
            u8g2.drawStr(ix_fuel + PALIVO_W + 4, base2, buf);
          }

        // --- STRÁNKA 4: Dojezd ---
        } else if (renderStranka == 4) {
          const int arrowOffset = 4;
          const int base1 = y + 3;
          const int ix0 = 0;
          const int arrowW = 5;
          u8g2.drawHLine(ix0, (base1 - 2) - arrowOffset, arrowW);
          u8g2.drawTriangle((ix0 + arrowW), (base1 - 5) - arrowOffset,
                            (ix0 + arrowW), (base1 + 1) - arrowOffset,
                            (ix0 + arrowW + 5), (base1 - 2) - arrowOffset);
          const int ix_fuel = ix0 + arrowW + 7;
          const int iy_fuel = base1 - PALIVO_H + 1;
          u8g2.setDrawColor(1);
          u8g2.setBitmapMode(1);
          u8g2.drawXBMP(ix_fuel, iy_fuel, PALIVO_W, PALIVO_H, fuel_icon_bits);

          if (isnan(hladinaPaliva) || isnan(odhadovanyDojezd) || odhadovanyDojezd < 0) {
            u8g2.drawStr(ix_fuel + PALIVO_W + 4, base1, " ERR");
          } else {
            snprintf(buf, sizeof(buf), " %.0f km", odhadovanyDojezd);
            u8g2.drawStr(ix_fuel + PALIVO_W + 4, base1, buf);
          }

          if (denniVzdalenost < 999.95f)
            snprintf(buf, sizeof(buf), " %.1f km", denniVzdalenost);
          else
            snprintf(buf, sizeof(buf), " %.0f km", denniVzdalenost);
          u8g2.drawStr(31, y + 18, buf);

        // --- STRÁNKA 5: Otáčky a Baterka ---
        } else if (renderStranka == 5) {
          const int base1 = y + 3;
          const int ix_rpm = 0;
          const int iy_rpm = base1 - OTACKY_H + 1;

          u8g2.setDrawColor(1);
          u8g2.setBitmapMode(1);
          u8g2.drawXBMP(ix_rpm, iy_rpm, OTACKY_W, OTACKY_H, rpm_icon_bits);

          snprintf(buf, sizeof(buf), " %.0f RPM", otackyMotoru);
          u8g2.drawStr(ix_rpm + OTACKY_W + 4, base1, buf);

          const int base2 = y + 18;
          const int ix_batt = 0;
          const int iy_batt = base2 - BATERIE_H + 1;

          bool nizkaB = (napetiBaterie < NAPETI_NIZKE_V);
          if (!(nizkaB && !blikaniZapnuto)) {
            u8g2.drawXBMP(ix_batt, iy_batt, BATERIE_W, BATERIE_H, batt_icon_bits);
          }
          if (!nizkaB || blikaniZapnuto) {
            snprintf(buf, sizeof(buf), nizkaB ? " %.1fV!" : " %.1f V", napetiBaterie);
            u8g2.drawStr(ix_batt + BATERIE_W + 4, base2, buf);
          }
        }
      }
    }
  // Gear indicator – pravá strana (jen při jízdě, bez alarmu a dveří)
  if (!dvereOtevrene && !alarmTriggered && odhadnutyStupen > 0) {
    constexpr int gearSepX = 102;
    constexpr int gearCenterX = 116;
    constexpr int gearArrowLeftX = 106;
    constexpr int gearArrowRightX = 126;
    constexpr int gearArrowTopY = 31;
    constexpr int gearArrowUpperBaseY = 40;
    constexpr int gearArrowLowerBaseY = 54;
    constexpr int gearArrowBottomY = 63;
    constexpr unsigned long gearBlinkMaxMs = 550;
    constexpr unsigned long gearBlinkMinMs = 45;
    constexpr float gearBlinkMaxRpm = 3800.0f;

    u8g2.setDrawColor(1);
    // Vertikální čára – 2px tlustá
    u8g2.drawVLine(gearSepX, 30, 34);
    u8g2.drawVLine(gearSepX + 1, 30, 34);
    u8g2.setFont(u8g2_font_profont15_tr);
    char gearStr[3];
    snprintf(gearStr, sizeof(gearStr), "%d", odhadnutyStupen);
    int gearTextX = gearCenterX - (u8g2.getStrWidth(gearStr) / 2);
    u8g2.drawStr(gearTextX, 52, gearStr);
    if (odhadnutyStupen < 5 && otackyMotoru > gearRpmNahoru) {
      float denom = gearBlinkMaxRpm - gearRpmNahoru;
      if (denom < 100.0f) denom = 100.0f;
      float rpmPomer = (otackyMotoru - gearRpmNahoru) / denom;
      rpmPomer = omezNaRozsah(rpmPomer, 0.0f, 1.0f);
      float agresivniPomer = sqrtf(rpmPomer);
      unsigned long blinkMs = gearBlinkMaxMs - (unsigned long)(agresivniPomer * (float)(gearBlinkMaxMs - gearBlinkMinMs));
      bool showShiftUp = ((millis() / blinkMs) & 1UL) == 0;
      if (showShiftUp) {
        u8g2.drawTriangle(gearCenterX, gearArrowTopY, gearArrowLeftX, gearArrowUpperBaseY, gearArrowRightX, gearArrowUpperBaseY);  // šipka nahoru
      }
    }
    if (odhadnutyStupen > 1 && otackyMotoru < gearRpmDolu)
      u8g2.drawTriangle(gearCenterX, gearArrowBottomY, gearArrowLeftX, gearArrowLowerBaseY, gearArrowRightX, gearArrowLowerBaseY);  // šipka dolů
  }

  if (!dvereOtevrene && !alarmTriggered) {
    aplikujAnimaciInfo();
  }

  u8g2.sendBuffer();
  infoObrazovkaPripravena = !dvereOtevrene && !alarmTriggered;

  displayPosledniAktualizace = millis();
}

// Omezené vykreslování
bool potrebujePrekreslitUI() {
  static bool rtcPrevValid = true;
  static uint8_t lastMinute = 255;

  bool rtcValid = Rtc.IsDateTimeValid();
  bool dirty = false;
  if (infoAnimAktivni) dirty = true;

  // Overlay stavy – pokud jsou aktivní nebo právě nastaly, vždy kresli
  static bool lastDvere = false;
  if (dvereOtevrene != lastDvere) { lastDvere = dvereOtevrene; dirty = true; }

  bool alarmNow = (alarmyPovoleny && !isnan(teplotaVody) && teplotaVody > 90.0f);
  static bool lastAlarm = false;
  if (alarmNow != lastAlarm) { lastAlarm = alarmNow; dirty = true; }
  if (alarmNow) dirty = true;  // při aktivním alarmu vždy refresh (blikání)

  static bool lastServisMenu = false;
  if (servisMenuAktivni != lastServisMenu) { lastServisMenu = servisMenuAktivni; dirty = true; }
  if (servisMenuAktivni) dirty = true;

  static bool lastSmZprava = false;
  if (smZpravaAktivni != lastSmZprava) { lastSmZprava = smZpravaAktivni; dirty = true; }
  if (smZpravaAktivni) dirty = true;

  if (rtcValid != rtcPrevValid) {
    rtcPrevValid = rtcValid;
    dirty = true;
  }

  if (rtcValid) {
    uint8_t m = Rtc.GetDateTime().Minute();
    if (m != lastMinute) {
      lastMinute = m;
      dirty = true;
    }
  } else {
    if (lastMinute != 255) {
      lastMinute = 255;
      dirty = true;
    }
  }

  static int lastPage = -1;
  if (stranka != lastPage) {
    lastPage = stranka;
    dirty = true;
  }

  static int lastGear = -1;
  if (odhadnutyStupen != lastGear) { lastGear = odhadnutyStupen; dirty = true; }

  if (!dvereOtevrene && !alarmNow && odhadnutyStupen > 0 &&
      odhadnutyStupen < 5 && otackyMotoru > gearRpmNahoru) {
    dirty = true;
  }

  static float p1_lastW = NAN, p1_lastOut = NAN;
  static float p2_lastAvg = NAN, p2_lastInst = NAN;
  static float p3_lastSpd = NAN, p3_lastFuel = NAN;
  static float p4_lastRange = NAN, p4_lastDaily = NAN;
  static float p5_lastRpm = NAN, p5_lastBatt = NAN;

  switch (stranka) {
    case 1:
      if (zmenenaHodnota(teplotaVody, p1_lastW, 0.1f)) dirty = true;
      if (zmenenaHodnota(teplotaVenku, p1_lastOut, 0.1f)) dirty = true;
      break;

    case 2:
      {
        if (zmenenaHodnota(prumernaSpotreba, p2_lastAvg, 0.01f)) dirty = true;
        float inst;
        float tol;
        if (rychlostVozu >= 3.0f) {
          if (rychlostVozu > 0.1f) {
            inst = (palivoTokInst / rychlostVozu) * 100.0f;
          } else {
            inst = 0.0f;
          }
          if (palivoTokInst < 0.01f) inst = 0.0f;
          tol = 0.1f;
        } else {
          inst = palivoTokInst;   // při stání zobrazujeme palivoTokInst, ne spotrebaLh
          tol = 0.05f;
        }
        if (zmenenaHodnota(inst, p2_lastInst, tol)) dirty = true;
        break;
      }

    case 3:
      {
        if (zmenenaHodnota(rychlostVozu, p3_lastSpd, 1.0f)) dirty = true;
        if (zmenenaHodnota(hladinaPaliva, p3_lastFuel, 0.1f)) dirty = true;
        bool lf_window = (upozorneniNizkePalivoPovoleno && upozorneniNizkePalivo) && ((millis() - nizkePalivoZobrazeni) < NIZKE_PALIVO_UPOZORNENI_MS);
        static bool lastBlink = false;
        if (lf_window && (blikaniZapnuto != lastBlink)) {
          lastBlink = blikaniZapnuto;
          dirty = true;
        }
        break;
      }

    case 4:
      if (zmenenaHodnota(odhadovanyDojezd, p4_lastRange, 1.0f)) dirty = true;
      if (zmenenaHodnota(denniVzdalenost, p4_lastDaily, 0.05f)) dirty = true;
      break;

    case 5:
      if (zmenenaHodnota(otackyMotoru, p5_lastRpm, 50.0f)) dirty = true;
      if (zmenenaHodnota(napetiBaterie, p5_lastBatt, 0.1f)) dirty = true;
      {
        bool nizkaB = (napetiBaterie < NAPETI_NIZKE_V);
        static bool lastBlinkBatt = false;
        if (nizkaB && (blikaniZapnuto != lastBlinkBatt)) { lastBlinkBatt = blikaniZapnuto; dirty = true; }
      }
      break;
  }

  return dirty || uiPrekreslit;
}
