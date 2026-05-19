#include <Arduino.h>
#include "globals.h"

// ====== SD karta – helpery a I/O ======

static void zapisKv(File& f, const char* key, uint64_t val) {
  f.print(key);
  f.print('=');
  f.println((unsigned long long)val);
}
static void zapisKvI(File& f, const char* key, int val) {
  f.print(key);
  f.print('=');
  f.println(val);
}
static void zapisKvF(File& f, const char* key, double val, int prec = 6) {
  f.print(key);
  f.print('=');
  f.println(val, prec);
}
static bool parsujKv(const String& line, String& k, String& v) {
  int eq = line.indexOf('=');
  if (eq < 0) return false;
  k = line.substring(0, eq);
  k.trim();
  v = line.substring(eq + 1);
  v.trim();
  return k.length() > 0;
}

static void formatCasLogu(char* buf, size_t sz) {
  if (Rtc.IsDateTimeValid()) {
    RtcDateTime dt = Rtc.GetDateTime();
    if (dt.Year() >= 2020 && dt.Year() <= 2099) {
      snprintf(buf, sz, "%04u-%02u-%02u %02u:%02u:%02u",
               (unsigned)dt.Year(), (unsigned)dt.Month(), (unsigned)dt.Day(),
               (unsigned)dt.Hour(), (unsigned)dt.Minute(), (unsigned)dt.Second());
      return;
    }
  }
  snprintf(buf, sz, "millis=%lu", (unsigned long)millis());
}

static void rotujErrorLogPokudJePotreba() {
  if (!SD.exists(SOUBOR_ERROR_LOG)) return;
  File f = SD.open(SOUBOR_ERROR_LOG, FILE_READ);
  if (!f) return;
  size_t sz = f.size();
  f.close();
  if (sz < ERROR_LOG_MAX_BYTES) return;

  if (SD.exists(SOUBOR_ERROR_LOG_BAK)) SD.remove(SOUBOR_ERROR_LOG_BAK);
  SD.rename(SOUBOR_ERROR_LOG, SOUBOR_ERROR_LOG_BAK);
}

void zapisErrorLog(const char* uroven, const char* kod, const char* detail) {
  if (!sdPripravena) return;
  rotujErrorLogPokudJePotreba();

  File f = SD.open(SOUBOR_ERROR_LOG, FILE_APPEND);
  if (!f) return;

  char ts[32];
  formatCasLogu(ts, sizeof(ts));
  f.print(ts);
  f.print(" | ");
  f.print(uroven ? uroven : "INFO");
  f.print(" | ");
  f.print(kod ? kod : "EVENT");
  if (detail && detail[0]) {
    f.print(" | ");
    f.print(detail);
  }
  f.println();
  f.close();
}

bool jePresnyPrikaz(const char* vstup, const char* prikaz) {
  return strcmp(vstup, prikaz) == 0;
}

void oznacKonfiguraciJakoZmenenou() {
  konfigZmenena = true;
  konfigZmenenaCas = millis();
}

bool vynulujStatistiky(bool ulozitNaSd) {
  celkovePalivo = 0.0f;
  celkovaVzdalenost = 0.0f;
  celkoveMetry_u64 = 0;
  celkovePalivoUl_u64 = 0;
  denniVzdalenost = 0.0f;
  denniMetry_u64 = 0;
  prumernaSpotreba = 0.0f;
  odhadovanyDojezd = 0.0f;
  zapisErrorLog("INFO", "STATS_RESET", "manual statistics reset");

  if (!ulozitNaSd) return true;
  if (!sdPripravena) return false;
  return ulozStatistikySD();
}

// float -> u64 (po startu/sync)
static void synchronizujFloatDoU64() {
  if (!isnan(celkovaVzdalenost) && celkovaVzdalenost >= 0) celkoveMetry_u64 = (uint64_t)llround(celkovaVzdalenost * 1000.0);
  if (!isnan(celkovePalivo) && celkovePalivo >= 0) celkovePalivoUl_u64 = (uint64_t)llround(celkovePalivo * 1000000.0);
  if (!isnan(denniVzdalenost) && denniVzdalenost >= 0) denniMetry_u64 = (uint64_t)llround(denniVzdalenost * 1000.0);
  posledniUlozeneMetry_u64 = celkoveMetry_u64;
  posledniUlozeniMs = millis();
}

// u64 -> float (po načtení)
static void synchronizujU64DoFloat() {
  celkovaVzdalenost = (float)(celkoveMetry_u64 / 1000.0);
  celkovePalivo = (float)(celkovePalivoUl_u64 / 1000000.0);
  denniVzdalenost = (float)(denniMetry_u64 / 1000.0);
}

static void obnovSouborZeZalohy(const char* cil, const char* zaloha) {
  if (!SD.exists(cil) && SD.exists(zaloha)) {
    SD.rename(zaloha, cil);
  }
}

static bool nahradSouborAtomicky(const char* zdrojTmp, const char* cil, const char* zaloha) {
  if (SD.exists(zaloha)) SD.remove(zaloha);

  bool melPuvodni = SD.exists(cil);
  if (melPuvodni) {
    if (!SD.rename(cil, zaloha)) return false;
  }

  if (SD.rename(zdrojTmp, cil)) {
    if (SD.exists(zaloha)) SD.remove(zaloha);
    return true;
  }

  if (SD.exists(zdrojTmp)) SD.remove(zdrojTmp);
  if (melPuvodni && SD.exists(zaloha)) SD.rename(zaloha, cil);
  return false;
}

// načtení statistik
static bool nactiStatistikySD() {
  if (!SD.exists(SOUBOR_STATISTIKY)) return false;
  File f = SD.open(SOUBOR_STATISTIKY, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    String k, v;
    if (!parsujKv(line, k, v)) continue;
    if (k == "total_m") celkoveMetry_u64 = strtoull(v.c_str(), nullptr, 10);
    else if (k == "total_fuel_ul") celkovePalivoUl_u64 = strtoull(v.c_str(), nullptr, 10);
    else if (k == "daily_m") denniMetry_u64 = strtoull(v.c_str(), nullptr, 10);
    else if (k == "engine_time_s") casMotoru_s_u32 = (uint32_t)v.toInt();
  }
  f.close();
  synchronizujU64DoFloat();
  posledniUlozeneMetry_u64 = celkoveMetry_u64;
  posledniUlozeniMs = millis();
  return true;
}

// uložení statistik
bool ulozStatistikySD() {
  if (SD.exists(SOUBOR_STATISTIKY_TMP)) SD.remove(SOUBOR_STATISTIKY_TMP);
  File f = SD.open(SOUBOR_STATISTIKY_TMP, FILE_WRITE);
  if (!f) return false;
  f.println("file_version=1");
  zapisKv(f, "total_m", celkoveMetry_u64);
  zapisKv(f, "total_fuel_ul", celkovePalivoUl_u64);
  zapisKv(f, "daily_m", denniMetry_u64);
  zapisKv(f, "engine_time_s", casMotoru_s_u32);
  f.flush();
  f.close();
  bool ok = nahradSouborAtomicky(SOUBOR_STATISTIKY_TMP, SOUBOR_STATISTIKY, SOUBOR_STATISTIKY_BAK);
  if (ok) {
    posledniUlozeneMetry_u64 = celkoveMetry_u64;
    posledniUlozeniMs = millis();
  }
  return ok;
}

// načtení konfigurace
static bool nactiKonfiguraciSD() {
  if (!SD.exists(SOUBOR_KONFIGURACE)) return false;
  File f = SD.open(SOUBOR_KONFIGURACE, FILE_READ);
  if (!f) return false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;

    String k, v;
    if (!parsujKv(line, k, v)) continue;

    if (k == "stranka" || k == "page") stranka = v.toInt();
    else if (k == "alarmyPovoleny" || k == "alarmsEnabled") alarmyPovoleny = (v.toInt() != 0);
    else if (k == "upozorneniNizkePalivoPovoleno" || k == "lowFuelWarningEnabled") upozorneniNizkePalivoPovoleno = (v.toInt() != 0);
    else if (k == "teplotaOffsetC") teplotaOffsetC = v.toFloat();
    else if (k == "teplotaVenkuOffsetC") teplotaVenkuOffsetC = v.toFloat();
    else if (k == "palivoOffsetL") palivoOffsetL = v.toFloat();
    else if (k == "napetiOffsetV") napetiOffsetV = v.toFloat();
    else if (k == "gearRpmNahoru") gearRpmNahoru = v.toFloat();
    else if (k == "gearRpmDolu") gearRpmDolu = v.toFloat();
    else if (k == "PALIVO_NA_US") PALIVO_NA_US = v.toFloat();
    else if (k == "VSTRIK_TOK_CC_MIN") VSTRIK_TOK_CC_MIN = v.toFloat();
    else if (k == "cidloNapajeni") cidloNapajeni = v.toFloat();
    else if (k == "cidloOhmVoda") cidloOhmVoda = v.toFloat();
    else if (k == "cidloOhmPalivo")  cidloOhmPalivo  = v.toFloat();
    // zpětná kompatibilita se starým klíčem (přečte do obou)
    else if (k == "senderSeriesOhm") { cidloOhmVoda = v.toFloat(); cidloOhmPalivo = v.toFloat(); }
    else if (k == "pulzyNaKm") pulzyNaKm = v.toFloat();
    else if (k == "motorPocetVstriku") motorPocetVstriku = v.toInt();
    else if (k == "merenyPocetVstriku") merenyPocetVstriku = v.toInt();
  }

  f.close();

  if (stranka < 1 || stranka > 5) stranka = 1;

  if (isnan(teplotaOffsetC)) teplotaOffsetC = 0.0f;
  if (isnan(teplotaVenkuOffsetC)) teplotaVenkuOffsetC = 0.0f;
  if (isnan(palivoOffsetL) || palivoOffsetL < PALIVO_OFFSET_MIN_L || palivoOffsetL > PALIVO_OFFSET_MAX_L) palivoOffsetL = 0.0f;
  if (isnan(napetiOffsetV) || napetiOffsetV < NAPETI_OFFSET_MIN_V || napetiOffsetV > NAPETI_OFFSET_MAX_V) napetiOffsetV = 0.0f;
  if (isnan(gearRpmNahoru) || gearRpmNahoru < GEAR_RPM_NAHORU_MIN || gearRpmNahoru > GEAR_RPM_NAHORU_MAX) gearRpmNahoru = GEAR_RPM_NAHORU;
  if (isnan(gearRpmDolu) || gearRpmDolu < GEAR_RPM_DOLU_MIN || gearRpmDolu > GEAR_RPM_DOLU_MAX) gearRpmDolu = GEAR_RPM_DOLU;
  if (gearRpmDolu > gearRpmNahoru - 100.0f) gearRpmDolu = gearRpmNahoru - 100.0f;
  if (gearRpmDolu < GEAR_RPM_DOLU_MIN) gearRpmDolu = GEAR_RPM_DOLU_MIN;
  if (isnan(PALIVO_NA_US) || PALIVO_NA_US < 0.0f) PALIVO_NA_US = 0.0f;
  if (isnan(VSTRIK_TOK_CC_MIN) || VSTRIK_TOK_CC_MIN <= 0.0f) VSTRIK_TOK_CC_MIN = VSTRIK_CC_ZA_MIN;
  if (isnan(cidloNapajeni) || cidloNapajeni < 1.0f || cidloNapajeni > 16.0f) cidloNapajeni = CIDLO_NAPAJENI_DEFAULT;
  if (isnan(cidloOhmVoda) || cidloOhmVoda < CIDLO_OHM_VODA_CFG_MIN || cidloOhmVoda > CIDLO_OHM_VODA_CFG_MAX) {
    cidloOhmVoda = CIDLO_OHM_VODA_DEFAULT;
    konfigZmenena = true;
    konfigZmenenaCas = millis();
  }
  if (isnan(cidloOhmPalivo) || cidloOhmPalivo < CIDLO_OHM_PALIVO_CFG_MIN || cidloOhmPalivo > CIDLO_OHM_PALIVO_CFG_MAX) {
    cidloOhmPalivo = CIDLO_OHM_PALIVO_DEFAULT;
    konfigZmenena = true;
    konfigZmenenaCas = millis();
  }
  if (motorPocetVstriku <= 0 || motorPocetVstriku > 16) motorPocetVstriku = MOTOR_POCET_VSTRIKU_DEFAULT;
  if (merenyPocetVstriku <= 0 || merenyPocetVstriku > motorPocetVstriku) merenyPocetVstriku = MERENY_POCET_VSTRIKU_DEFAULT;
  if (pulzyNaKm != 3920.0f && pulzyNaKm != 7840.0f) pulzyNaKm = PULZY_NA_KM_DEFAULT;

  return true;
}

// uložení konfigurace
static bool ulozKonfiguraciSD() {
  if (SD.exists(SOUBOR_KONFIGURACE_TMP)) SD.remove(SOUBOR_KONFIGURACE_TMP);

  File f = SD.open(SOUBOR_KONFIGURACE_TMP, FILE_WRITE);
  if (!f) return false;

  f.println("file_version=1");
  zapisKvI(f, "stranka", stranka);
  zapisKvI(f, "alarmyPovoleny", alarmyPovoleny ? 1 : 0);
  zapisKvI(f, "upozorneniNizkePalivoPovoleno", upozorneniNizkePalivoPovoleno ? 1 : 0);
  zapisKvF(f, "teplotaOffsetC", teplotaOffsetC, 2);
  zapisKvF(f, "teplotaVenkuOffsetC", teplotaVenkuOffsetC, 2);
  zapisKvF(f, "palivoOffsetL", palivoOffsetL, 2);
  zapisKvF(f, "napetiOffsetV", napetiOffsetV, 2);
  zapisKvF(f, "gearRpmNahoru", gearRpmNahoru, 0);
  zapisKvF(f, "gearRpmDolu", gearRpmDolu, 0);
  zapisKvF(f, "PALIVO_NA_US", PALIVO_NA_US, 10);
  zapisKvF(f, "VSTRIK_TOK_CC_MIN", VSTRIK_TOK_CC_MIN, 3);
  zapisKvF(f, "cidloNapajeni", cidloNapajeni, 3);
  zapisKvF(f, "cidloOhmVoda", cidloOhmVoda, 1);
  zapisKvF(f, "cidloOhmPalivo",  cidloOhmPalivo,  1);
  zapisKvF(f, "pulzyNaKm", pulzyNaKm, 0);
  zapisKvI(f, "motorPocetVstriku", motorPocetVstriku);
  zapisKvI(f, "merenyPocetVstriku", merenyPocetVstriku);

  f.flush();
  f.close();
  return nahradSouborAtomicky(SOUBOR_KONFIGURACE_TMP, SOUBOR_KONFIGURACE, SOUBOR_KONFIGURACE_BAK);
}

// SD inicializace
bool inicializujSD() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(5);
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);  // HSPI piny
  const uint32_t sdFreqs[] = { 10000000UL, 4000000UL, 1000000UL };
  bool sdMounted = false;
  for (uint8_t i = 0; i < (sizeof(sdFreqs) / sizeof(sdFreqs[0])); ++i) {
    SD.end();
    delay(10);
    if (SD.begin(SD_CS, spiSD, sdFreqs[i])) {
      Serial.printf("SD OK @ %lu Hz\n", (unsigned long)sdFreqs[i]);
      sdMounted = true;
      break;
    }
    Serial.printf("SD retry failed @ %lu Hz\n", (unsigned long)sdFreqs[i]);
    delay(40);
  }
  if (!sdMounted) return false;
  obnovSouborZeZalohy(SOUBOR_KONFIGURACE, SOUBOR_KONFIGURACE_BAK);
  obnovSouborZeZalohy(SOUBOR_STATISTIKY, SOUBOR_STATISTIKY_BAK);
  if (SD.exists(SOUBOR_KONFIGURACE_TMP)) SD.remove(SOUBOR_KONFIGURACE_TMP);
  if (SD.exists(SOUBOR_STATISTIKY_TMP)) SD.remove(SOUBOR_STATISTIKY_TMP);

  if (!nactiKonfiguraciSD()) {
    if (!ulozKonfiguraciSD()) return false;
  }
  if (!nactiStatistikySD()) {
    synchronizujFloatDoU64();
    if (!ulozStatistikySD()) return false;
  } else {
    synchronizujU64DoFloat();
  }
  return true;
}

void moznaUlozSD() {
  if (!sdPripravena) return;
  unsigned long now = millis();
  bool timeUp = (now - posledniUlozeniMs) >= ULOZ_INTERVAL_MS;
  uint64_t distDelta = (celkoveMetry_u64 >= posledniUlozeneMetry_u64) ? (celkoveMetry_u64 - posledniUlozeneMetry_u64) : 0;
  bool distUp = distDelta >= ULOZ_KAZDE_M;
  if (timeUp || distUp) {
    // Nastav čas před pokusem – i při neúspěchu se příští pokus odloží o ULOZ_INTERVAL_MS
    posledniUlozeniMs = now;
    posledniUlozeneMetry_u64 = celkoveMetry_u64;
    if (!ulozStatistikySD()) {
      zapisErrorLog("ERROR", "SD_SAVE_FAIL", "statistics save failed");
    }
  }
}

bool ulozKonfiguraciSdNyni() {
  if (!sdPripravena) return false;
  return ulozKonfiguraciSD();
}

void moznaUlozKonfiguraciSD() {
  if (!sdPripravena || !konfigZmenena) return;
  if ((millis() - konfigZmenenaCas) < KONFIG_ULOZ_ZPOZDENI_MS) return;

  if (ulozKonfiguraciSD()) {
    konfigZmenena = false;
  } else {
    zapisErrorLog("ERROR", "SD_SAVE_FAIL", "config save failed");
    // Neúspěch – odlož příští pokus o další KONFIG_ULOZ_ZPOZDENI_MS
    konfigZmenenaCas = millis();
  }
}

void obsluzSystemovyLog() {
  static unsigned long posledniKontrolaMs = 0;
  unsigned long now = millis();
  if (now - posledniKontrolaMs < 1000UL) return;
  posledniKontrolaMs = now;

  static bool rtcStavInicializovan = false;
  static bool rtcPredchoziValid = false;
  static uint64_t posledniRtcSekundy = 0;

  bool rtcValid = Rtc.IsDateTimeValid();
  if (!rtcStavInicializovan) {
    rtcStavInicializovan = true;
    rtcPredchoziValid = rtcValid;
    if (!rtcValid) {
      zapisErrorLog("WARN", "RTC_INVALID", "RTC invalid at diagnostics start");
    }
  } else if (rtcValid != rtcPredchoziValid) {
    zapisErrorLog(rtcValid ? "INFO" : "WARN",
                  rtcValid ? "RTC_RECOVERED" : "RTC_INVALID",
                  rtcValid ? "RTC became valid again" : "RTC became invalid");
    rtcPredchoziValid = rtcValid;
  }

  if (rtcValid) {
    RtcDateTime dt = Rtc.GetDateTime();
    uint64_t rtcSekundy = dt.TotalSeconds64();

    if (posledniRtcSekundy > 0) {
      bool casZpet = (rtcSekundy + 5ULL < posledniRtcSekundy);
      bool casDopreduMoc = (rtcSekundy > posledniRtcSekundy + 3600ULL);
      if (casZpet || casDopreduMoc) {
        char detail[96];
        snprintf(detail, sizeof(detail), "prev=%llu current=%llu",
                 (unsigned long long)posledniRtcSekundy,
                 (unsigned long long)rtcSekundy);
        zapisErrorLog("WARN", "RTC_JUMP", detail);
      }
    }

    posledniRtcSekundy = rtcSekundy;
  }

  static bool rychlostVypadekZalogovan = false;
  bool motorBezi = (otackyMotoru > 500.0f) || (palivoTokInst > 0.25f);
  if (motorBezi && !diagRychlostAktualni && rychlostVozu > 10.0f) {
    if (!rychlostVypadekZalogovan) {
      char detail[80];
      snprintf(detail, sizeof(detail), "speed=%.1f rpm=%.0f fuel=%.2f", rychlostVozu, otackyMotoru, palivoTokInst);
      zapisErrorLog("WARN", "SPEED_SIGNAL_LOST", detail);
      rychlostVypadekZalogovan = true;
    }
  } else if (diagRychlostAktualni || rychlostVozu < 3.0f) {
    rychlostVypadekZalogovan = false;
  }
}
