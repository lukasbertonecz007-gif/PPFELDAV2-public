#include <Arduino.h>
#include "globals.h"

// ====== Příkazy přes Serial/BT ======
static bool parsujDoublePresne(const char* text, double& hodnota) {
  if (!text) return false;
  char* konec = nullptr;
  hodnota = strtod(text, &konec);
  if (konec == text || !isfinite(hodnota)) return false;
  while (*konec == ' ' || *konec == '\t') konec++;
  return *konec == '\0';
}

static bool parsujUint32Presne(const char* text, uint32_t& hodnota) {
  if (!text || *text < '0' || *text > '9') return false;
  char* konec = nullptr;
  unsigned long long n = strtoull(text, &konec, 10);
  if (konec == text || n > UINT32_MAX) return false;
  while (*konec == ' ' || *konec == '\t') konec++;
  if (*konec != '\0') return false;
  hodnota = (uint32_t)n;
  return true;
}

static void vypisZbyvajiciServis(Stream& stream, const char* nazev, int64_t zbyvaM,
                                 uint64_t intervalM, bool resetNastaven) {
  if (intervalM == 0) {
    stream.printf("%-8s: interval vypnut\n", nazev);
  } else if (!vozidloNajetoNastaveno) {
    stream.printf("%-8s: nejdriv nastav celkovy najezd\n", nazev);
  } else if (!resetNastaven || zbyvaM == INT64_MAX) {
    stream.printf("%-8s: interval %.0f km, nutny reset po vymene\n", nazev, (double)intervalM / 1000.0);
  } else if (zbyvaM <= 0) {
    stream.printf("%-8s: PO INTERVALU o %.1f km\n", nazev, (double)(-zbyvaM) / 1000.0);
  } else {
    stream.printf("%-8s: zbyva %.1f km z %.0f km\n", nazev,
                  (double)zbyvaM / 1000.0, (double)intervalM / 1000.0);
  }
}

static void vypisServis(Stream& stream) {
  stream.println("--- Servis ---");
  if (vozidloNajetoNastaveno) {
    stream.printf("Najeto  : %.1f km\n", (double)vozidloNajetoM_u64 / 1000.0);
  } else {
    stream.println("Najeto  : NENASTAVENO");
  }
  vypisZbyvajiciServis(stream, "Olej", servisOlejZbyvaM(), servisOlejIntervalM_u64, servisOlejNastaven);
  vypisZbyvajiciServis(stream, "Rozvody", servisRozvodyZbyvaM(), servisRozvodyIntervalM_u64, servisRozvodyNastaveny);
  stream.printf("Varovani: 0x%03X\n", (unsigned int)aktivniVarovaniMask());
  stream.println("--------------");
}

void zpracujPrikaz(char* buffer, Stream& stream) {

  // --- nuluj: reset statistik ---
  if (jePresnyPrikaz(buffer, "nuluj")) {
    bool ok = vynulujStatistiky(true);
    stream.println(ok ? "Trip/denni statistiky vynulovany a ulozeny." : "Trip/denni statistiky vynulovany jen v RAM, SD ulozeni selhalo.");
  }

  else if (jePresnyPrikaz(buffer, "km")) {
    if (vozidloNajetoNastaveno) stream.printf("Najeto: %.1f km\n", (double)vozidloNajetoM_u64 / 1000.0);
    else stream.println("Najeto: NENASTAVENO. Pouziti: km <stav tachometru>");
  }

  else if (strncmp(buffer, "km ", 3) == 0) {
    double km = 0.0;
    if (!parsujDoublePresne(buffer + 3, km) || km < 0.0 || km > 10000000.0) {
      stream.println("Pouziti: km <0..10000000>");
    } else if (rychlostVozu >= 1.0f) {
      stream.println("Najezd nelze menit za jizdy.");
    } else if (!sdPripravena) {
      stream.println("Najezd nezmenen: SD karta neni pripravena.");
    } else if (nastavNajetoVozidlaKm(km)) {
      stream.printf("Najeto nastaveno: %.1f km\n", (double)vozidloNajetoM_u64 / 1000.0);
    } else {
      stream.println("Najezd nezmenen: ulozeni na SD selhalo.");
    }
  }

  else if (jePresnyPrikaz(buffer, "servis")) {
    vypisServis(stream);
  }

  else if (jePresnyPrikaz(buffer, "servis.olej.reset")) {
    if (!vozidloNajetoNastaveno) stream.println("Nejdriv nastav najezd prikazem km <hodnota>.");
    else if (servisOlejIntervalM_u64 == 0) stream.println("Nejdriv nastav interval: servis.olej <km>.");
    else stream.println(resetujServisOlej() ? "Interval oleje resetovan." : "Reset oleje selhal, zkontroluj SD.");
  }

  else if (jePresnyPrikaz(buffer, "servis.rozvody.reset")) {
    if (!vozidloNajetoNastaveno) stream.println("Nejdriv nastav najezd prikazem km <hodnota>.");
    else if (servisRozvodyIntervalM_u64 == 0) stream.println("Nejdriv nastav interval: servis.rozvody <km>.");
    else stream.println(resetujServisRozvody() ? "Interval rozvodu resetovan." : "Reset rozvodu selhal, zkontroluj SD.");
  }

  else if (strncmp(buffer, "servis.olej ", 12) == 0) {
    uint32_t km = 0;
    if (!parsujUint32Presne(buffer + 12, km) || km > 1000000UL) {
      stream.println("Pouziti: servis.olej <0..1000000 km>, 0 = vypnout");
    } else {
      stream.println(nastavServisOlejIntervalKm(km) ? "Interval oleje ulozen." : "Interval oleje nezmenen, zkontroluj SD.");
    }
  }

  else if (strncmp(buffer, "servis.rozvody ", 15) == 0) {
    uint32_t km = 0;
    if (!parsujUint32Presne(buffer + 15, km) || km > 1000000UL) {
      stream.println("Pouziti: servis.rozvody <0..1000000 km>, 0 = vypnout");
    } else {
      stream.println(nastavServisRozvodyIntervalKm(km) ? "Interval rozvodu ulozen." : "Interval rozvodu nezmenen, zkontroluj SD.");
    }
  }

  // --- tvoda / tvoda.off: test alarmu přehřátí ---
  else if (jePresnyPrikaz(buffer, "tvoda")) {
    rezimTestovani = true;
    teplotaVody = 95.0f;
    teplotaInicializovana = true;
    stream.println("Test: teplota vody = 95 C");
  }
  else if (jePresnyPrikaz(buffer, "tvoda.off")) {
    rezimTestovani = false;
    stream.println("Test vody vypnut.");
  }

  // --- alarm.on / alarm.off ---
  else if (jePresnyPrikaz(buffer, "alarm.off")) {
    alarmyPovoleny = false;
    oznacKonfiguraciJakoZmenenou();
    stream.println("Alarmy: VYP");
  }
  else if (jePresnyPrikaz(buffer, "alarm.on")) {
    alarmyPovoleny = true;
    oznacKonfiguraciJakoZmenenou();
    stream.println("Alarmy: ZAP");
  }

  // --- cas HH:MM:SS ---
  else if (strncmp(buffer, "cas ", 4) == 0) {
    int h, m, s;
    if (sscanf(buffer + 4, "%d:%d:%d", &h, &m, &s) == 3 &&
        h >= 0 && h <= 23 && m >= 0 && m <= 59 && s >= 0 && s <= 59) {
      RtcDateTime n = Rtc.GetDateTime();
      Rtc.SetDateTime(RtcDateTime(n.Year(), n.Month(), n.Day(), h, m, s));
      stream.println("Cas nastaven.");
    } else {
      stream.println("Pouziti: cas HH:MM:SS");
    }
  }

  // --- palivo.alarm.on / palivo.alarm.off ---
  else if (jePresnyPrikaz(buffer, "palivo.alarm.off")) {
    upozorneniNizkePalivoPovoleno = false;
    upozorneniNizkePalivo = false;
    zrusAnimaciStranky();
    stranka = (predchoziStranka >= 1 && predchoziStranka <= 5) ? predchoziStranka : 1;
    oznacKonfiguraciJakoZmenenou();
    stream.println("Alarm nizkeho paliva: VYP");
  }
  else if (jePresnyPrikaz(buffer, "palivo.alarm.on")) {
    upozorneniNizkePalivoPovoleno = true;
    oznacKonfiguraciJakoZmenenou();
    stream.println("Alarm nizkeho paliva: ZAP");
  }

  // --- offset.t <C>: manuální offset teploty vody ---
  else if (strncmp(buffer, "offset.t ", 9) == 0) {
    teplotaOffsetC = atof(buffer + 9);
    oznacKonfiguraciJakoZmenenou();
    stream.printf("Offset teploty: %.2f C\n", teplotaOffsetC);
  }

  // --- d: diagnostika ADS1115 ---
  else if (jePresnyPrikaz(buffer, "d")) {
    if (!adsPripraven) { stream.println("d: ADS neni pripraven."); return; }
    float tW = vypocetTeplotyVodyZOdporu(vodaDiag.ohms);
    float lF = vypocetPalivaZOdporu(palivoDiag.ohms);
    if (isfinite(tW)) tW += teplotaOffsetC;
    stream.println("--- DIAGNOSTIKA ---");
    stream.printf("VODA  : raw=%6d  Vpin=%.3f V  R=%.1f ohm  valid=%s  Tcalc=%.1f C  OLED=%.1f C\n",
                  vodaDiag.raw, vodaDiag.volts, vodaDiag.ohms, vodaDiag.valid ? "ANO" : "NE", tW, teplotaVody);
    stream.printf("PALIVO: raw=%6d  Vpin=%.3f V  R=%.1f ohm  valid=%s  Lcalc=%.1f L  OLED=%.1f L\n",
                  palivoDiag.raw, palivoDiag.volts, palivoDiag.ohms, palivoDiag.valid ? "ANO" : "NE", lF, hladinaPaliva);
    stream.printf("RYCHL.: pulzy=%lu  okno=%lu ms  medPeriod=%lu us  age=%lu ms  count=%.1f km/h  period=%.1f km/h  OLED=%.1f km/h  fresh=%s reject=%lu\n",
                  (unsigned long)diagRychlostPulzy, diagRychlostOknoMs, (unsigned long)diagRychlostMedianUs,
                  (unsigned long)diagRychlostStariMs, diagRychlostPocetKmh, diagRychlostPeriodaKmh, rychlostVozu,
                  diagRychlostAktualni ? "ANO" : "NE", (unsigned long)rychlostOdmitnutoGlitch);
    stream.printf("KVALT : odhad=%d  pomer=%.1f RPM/km/h\n",
                  odhadnutyStupen, (rychlostVozu >= 1.0f) ? (otackyMotoru / rychlostVozu) : 0.0f);
    stream.printf("VSTRIK: pulzy=%lu open=%lu us raw=%.2f L/h filt=%.2f L/h mapa=%d/%d flow=%.1f cc/min min=%lu us reject=%lu/%lu\n",
                  (unsigned long)diagVstrikPulzy, (unsigned long)diagVstrikOknoUs,
                  diagPalivoSyreLh, palivoTokInst, merenyPocetVstriku, motorPocetVstriku, VSTRIK_TOK_CC_MIN,
                  (unsigned long)vstrikFiltrUs,
                  (unsigned long)diagVstrikOdmitnutoKratke, (unsigned long)diagVstrikOdmitnutoDlouhe);
    stream.println("-------------------");
  }

  // --- diag: kompaktní diagnostika pro BT telefon ---
  else if (jePresnyPrikaz(buffer, "diag")) {
    stream.println("=== DIAG ===");
    stream.printf("Voda  : %.0f ohm  %.1f C\n", vodaDiag.ohms, teplotaVody);
    stream.printf("Palivo: %.0f ohm  %.1f L\n", palivoDiag.ohms, hladinaPaliva);
    stream.printf("Venku : %.1f C\n", isnan(teplotaVenku) ? -99.0f : teplotaVenku);
    stream.printf("Rychl.: %.1f km/h\n", rychlostVozu);
    stream.printf("RPM   : %.0f\n", otackyMotoru);
    stream.printf("Kvalt : %d  %.1f RPM/km/h\n", odhadnutyStupen, (rychlostVozu >= 1.0f) ? (otackyMotoru / rychlostVozu) : 0.0f);
    stream.printf("Vstrik: %.2f L/h (inst)\n", palivoTokInst);
    stream.printf("Batt  : %.2f V\n", napetiBaterie);
    stream.printf("Pulzy : %.0f/km\n", pulzyNaKm);
    stream.println("===========");
  }

  // --- kal.voda <C>: kalibrace offsetu teploty ---
  else if (strncmp(buffer, "kal.voda ", 9) == 0) {
    float tKnown = atof(buffer + 9);
    if (!adsPripraven) { stream.println("kal.voda: ADS neni pripraven."); return; }
    float rNtc = NAN, vPin = NAN;
    int16_t raw = 0;
    rNtc = ctejOdporCidlaAds(ADS_KAN_VODA, nullptr, &vPin, &raw);
    float tCalc = vypocetTeplotyVodyZOdporu(rNtc);
    if (isnan(tCalc)) {
      stream.println("kal.voda: chyba cteni ADS A0");
    } else {
      teplotaOffsetC = tKnown - tCalc;
      oznacKonfiguraciJakoZmenenou();
      stream.printf("kal.voda: R=%.1f ohm  tCalc=%.1f C  offset=%.1f C\n", rNtc, tCalc, teplotaOffsetC);
    }
  }

  // --- civka.v <ohm>: odpor cívky ukazatele teploty ---
  else if (strncmp(buffer, "civka.v ", 8) == 0) {
    float v = atof(buffer + 8);
    if (v >= CIDLO_OHM_VODA_CFG_MIN && v <= CIDLO_OHM_VODA_CFG_MAX) {
      cidloOhmVoda = v;
      oznacKonfiguraciJakoZmenenou();
      stream.printf("civka.v = %.1f ohm\n", cidloOhmVoda);
    } else {
      stream.printf("Pouziti: civka.v <%.0f..%.0f ohm>\n", CIDLO_OHM_VODA_CFG_MIN, CIDLO_OHM_VODA_CFG_MAX);
    }
  }

  // --- civka.p <ohm>: odpor cívky ukazatele paliva ---
  else if (strncmp(buffer, "civka.p ", 8) == 0) {
    float v = atof(buffer + 8);
    if (v >= CIDLO_OHM_PALIVO_CFG_MIN && v <= CIDLO_OHM_PALIVO_CFG_MAX) {
      cidloOhmPalivo = v;
      oznacKonfiguraciJakoZmenenou();
      stream.printf("civka.p = %.1f ohm\n", cidloOhmPalivo);
    } else {
      stream.printf("Pouziti: civka.p <%.0f..%.0f ohm>\n", CIDLO_OHM_PALIVO_CFG_MIN, CIDLO_OHM_PALIVO_CFG_MAX);
    }
  }

  // --- vcc <V>: napájecí napětí větve čidel ---
  else if (strncmp(buffer, "vcc ", 4) == 0) {
    float v = atof(buffer + 4);
    if (v >= 1.0f && v <= 16.0f) {
      cidloNapajeni = v;
      oznacKonfiguraciJakoZmenenou();
      stream.printf("vcc = %.2f V\n", cidloNapajeni);
    } else {
      stream.println("Pouziti: vcc <V>  (napr. 8.0)");
    }
  }

  // --- vstrik <cc/min>: průtok injektoru ---
  else if (strncmp(buffer, "vstrik ", 7) == 0) {
    float v = atof(buffer + 7);
    if (v > 0.0f && v <= 1000.0f) {
      VSTRIK_TOK_CC_MIN = v;
      oznacKonfiguraciJakoZmenenou();
      stream.printf("vstrik = %.1f cc/min\n", VSTRIK_TOK_CC_MIN);
    } else {
      stream.println("Pouziti: vstrik <1..1000 cc/min>");
    }
  }

  // --- spotr <50..150>: procentni korekce vypoctu spotreby ---
  else if (strncmp(buffer, "spotr ", 6) == 0) {
    float pct = atof(buffer + 6);
    if (pct >= SPOTREBA_KOREKCE_MIN * 100.0f && pct <= SPOTREBA_KOREKCE_MAX * 100.0f) {
      spotrebaKorekce = pct / 100.0f;
      oznacKonfiguraciJakoZmenenou();
      stream.printf("spotr = %.0f %%\n", pct);
    } else {
      stream.printf("Pouziti: spotr <%.0f..%.0f>\n", SPOTREBA_KOREKCE_MIN * 100.0f, SPOTREBA_KOREKCE_MAX * 100.0f);
    }
  }

  else if (strncmp(buffer, "vstrik.filtr ", 14) == 0) {
    uint32_t filtr = 0;
    if (parsujUint32Presne(buffer + 14, filtr) && filtr >= VSTRIK_FILTR_MIN_US && filtr <= VSTRIK_FILTR_MAX_US) {
      vstrikFiltrUs = filtr;
      oznacKonfiguraciJakoZmenenou();
      stream.printf("Filtr vstriku: %lu us\n", (unsigned long)vstrikFiltrUs);
    } else {
      stream.printf("Pouziti: vstrik.filtr <%lu..%lu us>\n",
                    (unsigned long)VSTRIK_FILTR_MIN_US, (unsigned long)VSTRIK_FILTR_MAX_US);
    }
  }

  // --- stav ---
  else if (jePresnyPrikaz(buffer, "stav")) {
    stream.println("--- Stav ---");
    stream.printf("Firmware        : %s %s (%s)\n", FW_NAME, FW_VERSION, FW_DATE);
    stream.printf("Teplota vody    : %.1f C\n", teplotaVody);
    stream.printf("Baterie         : %.2f V\n", napetiBaterie);
    stream.printf("Rychlost        : %.1f km/h\n", rychlostVozu);
    stream.printf("Otacky          : %.0f RPM\n", otackyMotoru);
    stream.printf("Spotreba        : %.2f L/h\n", spotrebaLh);
    stream.printf("Prumer lifetime : %.2f L/100km\n", prumernaSpotreba);
    stream.printf("Prumer trip     : %.2f L/100km\n", tripPrumernaSpotreba);
    stream.printf("Palivo          : %.1f L\n", hladinaPaliva);
    stream.printf("Dojezd          : %.0f km\n", odhadovanyDojezd);
    stream.printf("Vzdalenost life : %.1f km  %.1f L\n", celkovaVzdalenost, celkovePalivo);
    stream.printf("Vzdalenost trip : %.1f km  %.1f L  denni %.1f km\n", tripVzdalenost, tripPalivo, denniVzdalenost);
    stream.printf("Alarmy          : %s\n", alarmyPovoleny ? "ZAP" : "VYP");
    stream.printf("Alarm paliva    : %s\n", upozorneniNizkePalivoPovoleno ? "ZAP" : "VYP");
    stream.printf("ADS             : %s\n", adsPripraven ? "OK" : "CHYBA");
    {
      RtcDateTime rtcStabilni;
      bool rtcStabilniOk = nactiStabilniRtcCas(rtcStabilni);
      bool rtcLiveOk = false;
      if (Rtc.IsDateTimeValid()) {
        RtcDateTime rtcLive = Rtc.GetDateTime();
        rtcLiveOk = rtcCasPouzitelny(rtcLive);
      }
      stream.printf("RTC             : %s\n", rtcLiveOk ? "OK" : (rtcStabilniOk ? "CACHE" : "CHYBA"));
    }
    stream.printf("Offset pal/batt : %.1f L / %.2f V\n", palivoOffsetL, napetiOffsetV);
    stream.printf("RPM razeni      : nahoru %.0f / dolu %.0f\n", gearRpmNahoru, gearRpmDolu);
    stream.printf("vcc/civka.v/civka.p: %.1fV / %.0fohm / %.0fohm\n", cidloNapajeni, cidloOhmVoda, cidloOhmPalivo);
    stream.printf("Vstriky         : %d/%d\n", motorPocetVstriku, merenyPocetVstriku);
    stream.printf("Korekce spotr.  : %.0f %%\n", spotrebaKorekce * 100.0f);
    stream.printf("Filtr vstriku   : %lu us, reject %lu/%lu\n",
                  (unsigned long)vstrikFiltrUs,
                  (unsigned long)diagVstrikOdmitnutoKratke,
                  (unsigned long)diagVstrikOdmitnutoDlouhe);
#if PPFELDA_MA_BT
    stream.printf("Bluetooth       : %s\n", btKlientPripojen ? "PRIPOJEN" : (btAktivni ? "CEKA" : "CHYBA"));
#endif
    if (vozidloNajetoNastaveno) stream.printf("Najeto vozidla  : %.1f km\n", (double)vozidloNajetoM_u64 / 1000.0);
    else stream.println("Najeto vozidla  : NENASTAVENO");
    stream.printf("Varovani mask   : 0x%03X\n", (unsigned int)aktivniVarovaniMask());
    stream.println("---");
  }

  // --- ? / pomoc ---
  else if (jePresnyPrikaz(buffer, "?") || jePresnyPrikaz(buffer, "pomoc")) {
    stream.println("--- Prikazy ---");
    stream.println("stav                - aktualni hodnoty");
    stream.println("d                   - diagnostika ADS1115");
    stream.println("diag                - kompaktni diagnostika (BT)");
    stream.println("nuluj               - vynulovat trip/denni statistiky");
    stream.println("km [hodnota]        - zobrazit/nastavit celkovy najezd");
    stream.println("servis              - stav oleje a rozvodu");
    stream.println("servis.olej <km>    - nastavit interval oleje");
    stream.println("servis.olej.reset   - reset po vymene oleje");
    stream.println("servis.rozvody <km> - nastavit interval rozvodu");
    stream.println("servis.rozvody.reset- reset po vymene rozvodu");
    stream.println("cas HH:MM:SS        - nastavit cas RTC");
    stream.println("alarm.on/off        - alarmy prehrati vody");
    stream.println("palivo.alarm.on/off - alarm nizkeho paliva");
    stream.println("tvoda / tvoda.off   - test prehrati vody");
    stream.println("offset.t <C>        - offset teploty vody");
    stream.println("kal.voda <C>        - kalibrace offsetu vody");
    stream.println("vstrik <cc/min>     - prutok vstrikovace");
    stream.println("vstrik.filtr <us>   - minimalni sirka pulzu");
    stream.println("spotr <50..150>     - korekce spotreby v procentech");
    stream.println("civka.v <ohm>       - civka ukazatele teploty");
    stream.println("civka.p <ohm>       - civka ukazatele paliva");
    stream.println("vcc <V>             - napajeni vetve cidel");
    stream.println("?                   - tato napoveda");
  }

  else if (jePresnyPrikaz(buffer, "sex")) {
    stream.println("NO SEX :C");
  }

  else {
    stream.println("Neznamy prikaz. Zadej ? pro napovedu.");
  }
}

void obsluzSerialABluetooth() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialBufferIdx > 0 && !serialPreteceni) {
        serialBuffer[serialBufferIdx] = '\0';
        zpracujPrikaz(serialBuffer, Serial);
      }
      serialBufferIdx = 0;
      serialPreteceni = false;
    } else if (serialBufferIdx < (int)sizeof(serialBuffer) - 1) {
      serialBuffer[serialBufferIdx++] = c;
    } else {
      serialPreteceni = true;  // přeteklý buffer – příkaz zahodit
    }
  }
#if PPFELDA_MA_BT
  if (btAktivni) {
    static unsigned long btPosledniKontrolaMs = 0;
    unsigned long now = millis();
    if ((now - btPosledniKontrolaMs) >= 250UL) {
      btPosledniKontrolaMs = now;
      bool klient = SerialBT.hasClient();
      if (klient != btKlientPripojen) {
        btKlientPripojen = klient;
        uiPrekreslit = true;
        zapisErrorLog("INFO", klient ? "BT_CONNECTED" : "BT_DISCONNECTED", "Bluetooth client state changed");
      }
    }
    while (SerialBT.available()) {
      char c = SerialBT.read();
      if (c == '\r') continue;
      if (c == '\n') {
        if (btBufferIdx > 0 && !btPreteceni) {
          btBuffer[btBufferIdx] = '\0';
          zpracujPrikaz(btBuffer, SerialBT);
        }
        btBufferIdx = 0;
        btPreteceni = false;
      } else if (btBufferIdx < (int)sizeof(btBuffer) - 1) {
        btBuffer[btBufferIdx++] = c;
      } else {
        btPreteceni = true;
      }
    }
  }
#endif
}
