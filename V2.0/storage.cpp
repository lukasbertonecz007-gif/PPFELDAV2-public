#include <Arduino.h>
#include "globals.h"
#include <esp_system.h>

static bool obnovenaKonfiguraceZeZalohy = false;
static bool obnovenyStatistikyZeZalohy = false;

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

static uint32_t crc32Aktualizuj(uint32_t crc, uint8_t data) {
  crc ^= data;
  for (uint8_t i = 0; i < 8; ++i) {
    crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
  }
  return crc;
}

static bool nactiCrcSouboru(const char* cesta, uint32_t& vypoctene, bool& maCrc, uint32_t& ulozene) {
  File f = SD.open(cesta, FILE_READ);
  if (!f) return false;

  uint32_t crc = 0xFFFFFFFFUL;
  bool crcNalezeno = false;
  uint32_t crcZeSouboru = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;

    if (line.startsWith("crc32=")) {
      if (crcNalezeno) {
        f.close();
        return false;
      }
      String hodnota = line.substring(6);
      hodnota.trim();
      if (hodnota.length() != 8) {
        f.close();
        return false;
      }
      for (size_t i = 0; i < hodnota.length(); ++i) {
        if (!isxdigit((unsigned char)hodnota[i])) {
          f.close();
          return false;
        }
      }
      crcZeSouboru = strtoul(hodnota.c_str(), nullptr, 16);
      crcNalezeno = true;
      continue;
    }

    if (crcNalezeno) {
      f.close();
      return false;
    }

    for (size_t i = 0; i < line.length(); ++i) {
      crc = crc32Aktualizuj(crc, (uint8_t)line[i]);
    }
    crc = crc32Aktualizuj(crc, (uint8_t)'\n');
  }

  f.close();
  vypoctene = ~crc;
  maCrc = crcNalezeno;
  ulozene = crcZeSouboru;
  return true;
}

static bool souborMaPlatneCrc(const char* cesta, bool* maCrcOut = nullptr) {
  if (!SD.exists(cesta)) return false;
  uint32_t vypoctene = 0;
  uint32_t ulozene = 0;
  bool maCrc = false;
  if (!nactiCrcSouboru(cesta, vypoctene, maCrc, ulozene)) return false;
  if (maCrcOut) *maCrcOut = maCrc;
  return !maCrc || vypoctene == ulozene;
}

static bool souborObsahujeKlic(const char* cesta, const char* hledanyKlic) {
  File f = SD.open(cesta, FILE_READ);
  if (!f) return false;

  bool nalezen = false;
  while (f.available() && !nalezen) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    String k, v;
    if (parsujKv(line, k, v) && k == hledanyKlic) nalezen = true;
  }
  f.close();
  return nalezen;
}

static bool konfiguraceSouborPlatny(const char* cesta) {
  return souborMaPlatneCrc(cesta) &&
         (souborObsahujeKlic(cesta, "stranka") || souborObsahujeKlic(cesta, "page"));
}

static bool statistikySouborPlatny(const char* cesta) {
  if (!souborMaPlatneCrc(cesta)) return false;
  bool maLifetime = souborObsahujeKlic(cesta, "lifetime_m") &&
                    souborObsahujeKlic(cesta, "lifetime_fuel_ul");
  bool maLegacy = souborObsahujeKlic(cesta, "total_m") &&
                  souborObsahujeKlic(cesta, "total_fuel_ul");
  return maLifetime || maLegacy;
}

static bool pridejCrcSouboru(const char* cesta) {
  uint32_t vypoctene = 0;
  uint32_t ignorovane = 0;
  bool maCrc = false;
  if (!nactiCrcSouboru(cesta, vypoctene, maCrc, ignorovane) || maCrc) return false;

  File f = SD.open(cesta, FILE_APPEND);
  if (!f) return false;
  char radek[24];
  snprintf(radek, sizeof(radek), "crc32=%08lX", (unsigned long)vypoctene);
  f.println(radek);
  f.flush();
  f.close();
  return true;
}

static bool kopirujSoubor(const char* zdroj, const char* cil) {
  File in = SD.open(zdroj, FILE_READ);
  if (!in) return false;
  if (SD.exists(cil)) SD.remove(cil);
  File out = SD.open(cil, FILE_WRITE);
  if (!out) {
    in.close();
    return false;
  }

  uint8_t buffer[128];
  bool ok = true;
  while (in.available()) {
    int n = in.read(buffer, sizeof(buffer));
    if (n <= 0 || out.write(buffer, (size_t)n) != (size_t)n) {
      ok = false;
      break;
    }
  }
  out.flush();
  out.close();
  in.close();
  if (!ok) SD.remove(cil);
  return ok;
}

static bool rtcDatumPouzitelny(const RtcDateTime& dt) {
  return dt.Year() >= 2020 && dt.Year() <= 2099 && dt.TotalSeconds64() > 0;
}

static void formatCasLogu(char* buf, size_t sz) {
  if (Rtc.IsDateTimeValid()) {
    RtcDateTime dt = Rtc.GetDateTime();
    if (rtcDatumPouzitelny(dt)) {
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

static const char* nazevDuvoduResetu(esp_reset_reason_t duvod) {
  switch (duvod) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

void zalogujStartSystemu() {
  esp_reset_reason_t duvod = esp_reset_reason();
  bool zavazny = duvod == ESP_RST_PANIC ||
                 duvod == ESP_RST_INT_WDT ||
                 duvod == ESP_RST_TASK_WDT ||
                 duvod == ESP_RST_WDT ||
                 duvod == ESP_RST_BROWNOUT;
  char detail[96];
  snprintf(detail, sizeof(detail), "fw=%s reason=%s code=%d",
           FW_VERSION, nazevDuvoduResetu(duvod), (int)duvod);
  Serial.printf("BOOT: %s\n", detail);
  if (!sdPripravena) return;

  zapisErrorLog(zavazny ? "WARN" : "INFO", "BOOT", detail);

  if (obnovenaKonfiguraceZeZalohy) {
    zapisErrorLog("WARN", "FILE_RECOVERED", "config.txt restored from config.bak");
  }
  if (obnovenyStatistikyZeZalohy) {
    zapisErrorLog("WARN", "FILE_RECOVERED", "consumption.txt restored from consumption.bak");
  }
}

bool jePresnyPrikaz(const char* vstup, const char* prikaz) {
  return strcmp(vstup, prikaz) == 0;
}

void oznacKonfiguraciJakoZmenenou() {
  konfigZmenena = true;
  konfigZmenenaCas = millis();
}

bool vynulujStatistiky(bool ulozitNaSd) {
  uint64_t predTripM = tripMetry_u64;
  uint64_t predTripUl = tripPalivoUl_u64;
  tripPalivo = 0.0f;
  tripVzdalenost = 0.0f;
  tripMetry_u64 = 0;
  tripPalivoUl_u64 = 0;
  denniVzdalenost = 0.0f;
  denniMetry_u64 = 0;
  tripPrumernaSpotreba = 0.0f;

  char detail[96];
  snprintf(detail, sizeof(detail), "trip_m=%llu fuel_ul=%llu",
           (unsigned long long)predTripM,
           (unsigned long long)predTripUl);
  zapisErrorLog("INFO", "TRIP_RESET", detail);

  if (!ulozitNaSd) return true;
  if (!sdPripravena) return false;
  return ulozStatistikySD();
}

// float -> u64 (po startu/sync)
static void synchronizujFloatDoU64() {
  if (!isnan(celkovaVzdalenost) && celkovaVzdalenost >= 0) celkoveMetry_u64 = (uint64_t)llround(celkovaVzdalenost * 1000.0);
  if (!isnan(celkovePalivo) && celkovePalivo >= 0) celkovePalivoUl_u64 = (uint64_t)llround(celkovePalivo * 1000000.0);
  if (!isnan(tripVzdalenost) && tripVzdalenost >= 0) tripMetry_u64 = (uint64_t)llround(tripVzdalenost * 1000.0);
  if (!isnan(tripPalivo) && tripPalivo >= 0) tripPalivoUl_u64 = (uint64_t)llround(tripPalivo * 1000000.0);
  if (!isnan(denniVzdalenost) && denniVzdalenost >= 0) denniMetry_u64 = (uint64_t)llround(denniVzdalenost * 1000.0);
  posledniUlozeneMetry_u64 = celkoveMetry_u64;
  posledniUlozeniMs = millis();
}

// u64 -> float (po načtení)
static void synchronizujU64DoFloat() {
  celkovaVzdalenost = (float)(celkoveMetry_u64 / 1000.0);
  celkovePalivo = (float)(celkovePalivoUl_u64 / 1000000.0);
  tripVzdalenost = (float)(tripMetry_u64 / 1000.0);
  tripPalivo = (float)(tripPalivoUl_u64 / 1000000.0);
  denniVzdalenost = (float)(denniMetry_u64 / 1000.0);
  prumernaSpotreba = (celkoveMetry_u64 > 0) ? (float)((double)celkovePalivoUl_u64 / (10.0 * (double)celkoveMetry_u64)) : 0.0f;
  tripPrumernaSpotreba = (tripMetry_u64 > 0) ? (float)((double)tripPalivoUl_u64 / (10.0 * (double)tripMetry_u64)) : 0.0f;
}

typedef bool (*OvereniSouboru)(const char*);

static bool obnovSouborZeZalohy(const char* cil, const char* zaloha, OvereniSouboru overeni) {
  if (overeni(cil)) return true;
  if (!overeni(zaloha)) return false;

  if (SD.exists(cil)) SD.remove(cil);
  return kopirujSoubor(zaloha, cil) && overeni(cil);
}

static bool nahradSouborAtomicky(const char* zdrojTmp, const char* cil, const char* zaloha,
                                 OvereniSouboru overeni) {
  if (!overeni(zdrojTmp)) {
    if (SD.exists(zdrojTmp)) SD.remove(zdrojTmp);
    return false;
  }

  bool puvodniPlatny = overeni(cil);
  if (SD.exists(cil)) {
    if (puvodniPlatny) {
      if (SD.exists(zaloha)) SD.remove(zaloha);
      if (!SD.rename(cil, zaloha)) return false;
    } else {
      SD.remove(cil);
    }
  }

  if (SD.rename(zdrojTmp, cil) && overeni(cil)) {
    return true;
  }

  if (SD.exists(zdrojTmp)) SD.remove(zdrojTmp);
  if (SD.exists(cil)) SD.remove(cil);
  if (puvodniPlatny && SD.exists(zaloha)) {
    SD.rename(zaloha, cil);
  }
  return false;
}

// načtení statistik
static bool nactiStatistikySD() {
  if (!statistikySouborPlatny(SOUBOR_STATISTIKY)) return false;
  File f = SD.open(SOUBOR_STATISTIKY, FILE_READ);
  if (!f) return false;
  bool maLegacyM = false, maLegacyFuel = false;
  bool maLifetimeM = false, maLifetimeFuel = false;
  bool maTripM = false, maTripFuel = false;
  uint64_t legacyM = 0, legacyFuel = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    String k, v;
    if (!parsujKv(line, k, v)) continue;
    if (k == "total_m") { legacyM = strtoull(v.c_str(), nullptr, 10); maLegacyM = true; }
    else if (k == "total_fuel_ul") { legacyFuel = strtoull(v.c_str(), nullptr, 10); maLegacyFuel = true; }
    else if (k == "lifetime_m") { celkoveMetry_u64 = strtoull(v.c_str(), nullptr, 10); maLifetimeM = true; }
    else if (k == "lifetime_fuel_ul") { celkovePalivoUl_u64 = strtoull(v.c_str(), nullptr, 10); maLifetimeFuel = true; }
    else if (k == "trip_m") { tripMetry_u64 = strtoull(v.c_str(), nullptr, 10); maTripM = true; }
    else if (k == "trip_fuel_ul") { tripPalivoUl_u64 = strtoull(v.c_str(), nullptr, 10); maTripFuel = true; }
    else if (k == "daily_m") denniMetry_u64 = strtoull(v.c_str(), nullptr, 10);
    else if (k == "engine_time_s") casMotoru_s_u32 = (uint32_t)v.toInt();
  }
  f.close();

  if (!maLifetimeM && maLegacyM) celkoveMetry_u64 = legacyM;
  if (!maLifetimeFuel && maLegacyFuel) celkovePalivoUl_u64 = legacyFuel;
  if (!maTripM) tripMetry_u64 = celkoveMetry_u64;
  if (!maTripFuel) tripPalivoUl_u64 = celkovePalivoUl_u64;

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
  f.println("file_version=3");
  zapisKv(f, "total_m", celkoveMetry_u64);
  zapisKv(f, "total_fuel_ul", celkovePalivoUl_u64);
  zapisKv(f, "lifetime_m", celkoveMetry_u64);
  zapisKv(f, "lifetime_fuel_ul", celkovePalivoUl_u64);
  zapisKv(f, "trip_m", tripMetry_u64);
  zapisKv(f, "trip_fuel_ul", tripPalivoUl_u64);
  zapisKv(f, "daily_m", denniMetry_u64);
  zapisKv(f, "engine_time_s", casMotoru_s_u32);
  f.flush();
  f.close();
  if (!pridejCrcSouboru(SOUBOR_STATISTIKY_TMP)) {
    SD.remove(SOUBOR_STATISTIKY_TMP);
    return false;
  }
  bool ok = nahradSouborAtomicky(SOUBOR_STATISTIKY_TMP, SOUBOR_STATISTIKY,
                                 SOUBOR_STATISTIKY_BAK, statistikySouborPlatny);
  if (ok) {
    posledniUlozeneMetry_u64 = celkoveMetry_u64;
    posledniUlozeniMs = millis();
  }
  return ok;
}

// načtení konfigurace
static bool nactiKonfiguraciSD() {
  if (!konfiguraceSouborPlatny(SOUBOR_KONFIGURACE)) return false;
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
    else if (k == "oledJasRezim") oledJasRezim = (uint8_t)v.toInt();
    else if (k == "animaceRezim") animaceRezim = (uint8_t)v.toInt();
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
  if (oledJasRezim > OLED_JAS_AUTO_REZIM) oledJasRezim = OLED_JAS_REZIM_DEFAULT;
  if (animaceRezim > ANIMACE_POMALA_REZIM) animaceRezim = ANIMACE_REZIM_DEFAULT;

  return true;
}

// uložení konfigurace
static bool ulozKonfiguraciSD() {
  if (SD.exists(SOUBOR_KONFIGURACE_TMP)) SD.remove(SOUBOR_KONFIGURACE_TMP);

  File f = SD.open(SOUBOR_KONFIGURACE_TMP, FILE_WRITE);
  if (!f) return false;

  f.println("file_version=2");
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
  zapisKvI(f, "oledJasRezim", oledJasRezim);
  zapisKvI(f, "animaceRezim", animaceRezim);

  f.flush();
  f.close();
  if (!pridejCrcSouboru(SOUBOR_KONFIGURACE_TMP)) {
    SD.remove(SOUBOR_KONFIGURACE_TMP);
    return false;
  }
  return nahradSouborAtomicky(SOUBOR_KONFIGURACE_TMP, SOUBOR_KONFIGURACE,
                              SOUBOR_KONFIGURACE_BAK, konfiguraceSouborPlatny);
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
  bool konfigPredObnovouPlatna = konfiguraceSouborPlatny(SOUBOR_KONFIGURACE);
  bool statistikyPredObnovouPlatne = statistikySouborPlatny(SOUBOR_STATISTIKY);
  if (!konfigPredObnovouPlatna &&
      obnovSouborZeZalohy(SOUBOR_KONFIGURACE, SOUBOR_KONFIGURACE_BAK, konfiguraceSouborPlatny)) {
    obnovenaKonfiguraceZeZalohy = true;
    Serial.println("SD: config.txt obnoven z config.bak");
  }
  if (!statistikyPredObnovouPlatne &&
      obnovSouborZeZalohy(SOUBOR_STATISTIKY, SOUBOR_STATISTIKY_BAK, statistikySouborPlatny)) {
    obnovenyStatistikyZeZalohy = true;
    Serial.println("SD: consumption.txt obnoven z consumption.bak");
  }
  if (SD.exists(SOUBOR_KONFIGURACE_TMP)) SD.remove(SOUBOR_KONFIGURACE_TMP);
  if (SD.exists(SOUBOR_STATISTIKY_TMP)) SD.remove(SOUBOR_STATISTIKY_TMP);

  bool konfigMaCrc = false;
  bool statistikyMajiCrc = false;
  souborMaPlatneCrc(SOUBOR_KONFIGURACE, &konfigMaCrc);
  souborMaPlatneCrc(SOUBOR_STATISTIKY, &statistikyMajiCrc);

  if (!nactiKonfiguraciSD()) {
    if (!ulozKonfiguraciSD()) return false;
  } else if (!konfigMaCrc) {
    if (!ulozKonfiguraciSD()) return false;
  }
  if (!nactiStatistikySD()) {
    synchronizujFloatDoU64();
    if (!ulozStatistikySD()) return false;
  } else {
    synchronizujU64DoFloat();
    if (!statistikyMajiCrc && !ulozStatistikySD()) return false;
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

struct SledovanyStav {
  bool inicializovan;
  bool stabilniOk;
  bool kandidatOk;
  unsigned long kandidatOdMs;
};

static void sledujStav(SledovanyStav& stav, bool aktualneOk, unsigned long now,
                       unsigned long chybaZpozdeniMs, unsigned long obnovaZpozdeniMs,
                       const char* kodChyby, const char* kodObnovy,
                       const char* detailChyby, const char* detailObnovy) {
  if (!stav.inicializovan) {
    stav.inicializovan = true;
    stav.stabilniOk = true;
    stav.kandidatOk = aktualneOk;
    stav.kandidatOdMs = aktualneOk ? 0 : now;
    return;
  }

  if (aktualneOk == stav.stabilniOk) {
    stav.kandidatOk = aktualneOk;
    stav.kandidatOdMs = 0;
    return;
  }

  if (stav.kandidatOdMs == 0 || stav.kandidatOk != aktualneOk) {
    stav.kandidatOk = aktualneOk;
    stav.kandidatOdMs = now;
    return;
  }

  unsigned long pozadovanaDoba = aktualneOk ? obnovaZpozdeniMs : chybaZpozdeniMs;
  if ((now - stav.kandidatOdMs) < pozadovanaDoba) return;

  stav.stabilniOk = aktualneOk;
  stav.kandidatOdMs = 0;
  zapisErrorLog(aktualneOk ? "INFO" : "WARN",
                aktualneOk ? kodObnovy : kodChyby,
                aktualneOk ? detailObnovy : detailChyby);
}

void obsluzSystemovyLog() {
  static unsigned long posledniKontrolaMs = 0;
  unsigned long now = millis();
  if (now - posledniKontrolaMs < 1000UL) return;
  posledniKontrolaMs = now;

  static bool rtcStavInicializovan = false;
  static bool rtcPredchoziOk = false;
  static uint64_t posledniRtcSekundy = 0;

  bool rtcValid = Rtc.IsDateTimeValid();
  bool rtcOk = false;
  uint64_t rtcSekundy = 0;
  uint16_t rtcRok = 0;
  if (rtcValid) {
    RtcDateTime dt = Rtc.GetDateTime();
    rtcSekundy = dt.TotalSeconds64();
    rtcRok = dt.Year();
    rtcOk = rtcDatumPouzitelny(dt);
  }

  if (!rtcStavInicializovan) {
    rtcStavInicializovan = true;
    rtcPredchoziOk = rtcOk;
    if (!rtcOk) {
      if (!rtcValid) {
        zapisErrorLog("WARN", "RTC_INVALID", "RTC invalid at diagnostics start");
      } else {
        char detail[64];
        snprintf(detail, sizeof(detail), "year=%u total=%llu", (unsigned)rtcRok, (unsigned long long)rtcSekundy);
        zapisErrorLog("WARN", "RTC_BAD_READ", detail);
      }
    }
  } else if (rtcOk != rtcPredchoziOk) {
    if (rtcOk) {
      zapisErrorLog("INFO", "RTC_RECOVERED", "RTC became usable again");
    } else if (!rtcValid) {
      zapisErrorLog("WARN", "RTC_INVALID", "RTC became invalid");
    } else {
      char detail[64];
      snprintf(detail, sizeof(detail), "year=%u total=%llu", (unsigned)rtcRok, (unsigned long long)rtcSekundy);
      zapisErrorLog("WARN", "RTC_BAD_READ", detail);
    }
    rtcPredchoziOk = rtcOk;
  }

  if (rtcOk) {
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

  static SledovanyStav adsStav = {};
  static SledovanyStav vodaStav = {};
  static SledovanyStav palivoStav = {};
  static SledovanyStav venkuStav = {};
  static SledovanyStav baterieStav = {};

  sledujStav(adsStav, adsPripraven, now,
             CIDLO_LOG_ZPOZDENI_MS, CIDLO_LOG_OBNOVA_MS,
             "ADS_LOST", "ADS_RECOVERED",
             "ADS1115 unavailable", "ADS1115 responding");

  if (adsPripraven) {
    sledujStav(vodaStav, vodaDiag.valid, now,
               CIDLO_LOG_ZPOZDENI_MS, CIDLO_LOG_OBNOVA_MS,
               "WATER_SENSOR_LOST", "WATER_SENSOR_RECOVERED",
               "water sender invalid", "water sender valid");
    sledujStav(palivoStav, palivoDiag.valid, now,
               CIDLO_LOG_ZPOZDENI_MS, CIDLO_LOG_OBNOVA_MS,
               "FUEL_SENSOR_LOST", "FUEL_SENSOR_RECOVERED",
               "fuel sender invalid", "fuel sender valid");
    sledujStav(venkuStav, isfinite(teplotaVenku), now,
               CIDLO_LOG_ZPOZDENI_MS, CIDLO_LOG_OBNOVA_MS,
               "OUTSIDE_SENSOR_LOST", "OUTSIDE_SENSOR_RECOVERED",
               "outside temperature invalid", "outside temperature valid");
  }

  if (isfinite(napetiBaterie) && napetiBaterie > 1.0f) {
    char detailNizke[48];
    char detailObnoveno[48];
    snprintf(detailNizke, sizeof(detailNizke), "voltage=%.2fV threshold=%.2fV",
             napetiBaterie, NAPETI_LOG_NIZKE_V);
    snprintf(detailObnoveno, sizeof(detailObnoveno), "voltage=%.2fV threshold=%.2fV",
             napetiBaterie, NAPETI_LOG_OBNOVENO_V);

    bool baterieOk = (!baterieStav.inicializovan || baterieStav.stabilniOk)
                       ? napetiBaterie >= NAPETI_LOG_NIZKE_V
                       : napetiBaterie >= NAPETI_LOG_OBNOVENO_V;
    sledujStav(baterieStav, baterieOk, now,
               NAPETI_LOG_ZPOZDENI_MS, NAPETI_LOG_OBNOVA_MS,
               "BATTERY_LOW", "BATTERY_RECOVERED",
               detailNizke, detailObnoveno);
  }

  static bool rychlostVypadekZalogovan = false;
  static unsigned long rychlostVypadekOdMs = 0;
  bool motorBezi = (otackyMotoru > 500.0f) || (palivoTokInst > 0.25f);
  bool rychlostVypadek = motorBezi && !diagRychlostAktualni && rychlostVozu > 10.0f;
  if (rychlostVypadek) {
    if (rychlostVypadekOdMs == 0) rychlostVypadekOdMs = now;
    if (!rychlostVypadekZalogovan && (now - rychlostVypadekOdMs) >= RYCHLOST_LOG_VYPADEK_MS) {
      char detail[192];
      snprintf(detail, sizeof(detail),
               "speed=%.1f rpm=%.0f fuel=%.2f count=%.1f period=%.1f age=%lu pulzy=%lu okno=%lu",
               rychlostVozu, otackyMotoru, palivoTokInst,
               diagRychlostPocetKmh, diagRychlostPeriodaKmh,
               (unsigned long)diagRychlostStariMs,
               (unsigned long)diagRychlostPulzy,
               (unsigned long)diagRychlostOknoMs);
      zapisErrorLog("WARN", "SPEED_SIGNAL_LOST", detail);
      rychlostVypadekZalogovan = true;
    }
  } else if (diagRychlostAktualni) {
    if (rychlostVypadekZalogovan) {
      char detail[96];
      snprintf(detail, sizeof(detail), "speed=%.1f age=%lu pulzy=%lu",
               rychlostVozu,
               (unsigned long)diagRychlostStariMs,
               (unsigned long)diagRychlostPulzy);
      zapisErrorLog("INFO", "SPEED_SIGNAL_RECOVERED", detail);
    }
    rychlostVypadekOdMs = 0;
    rychlostVypadekZalogovan = false;
  } else if (rychlostVozu < 3.0f || !motorBezi) {
    rychlostVypadekOdMs = 0;
  }
}
