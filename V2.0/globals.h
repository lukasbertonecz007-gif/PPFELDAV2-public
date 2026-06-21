#pragma once
// ======================================================================
// globals.h – sdílené konstanty, extern deklarace a forward deklarace
// Includovat ve všech .cpp souborech
// ======================================================================

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <RtcDS3231.h>
#include <Adafruit_ADS1X15.h>
#include <SPI.h>
#include <SD.h>
#include <esp_adc_cal.h>
#include <esp_task_wdt.h>
#include <pgmspace.h>
#include <math.h>

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
#include <BluetoothSerial.h>
#ifndef PPFELDA_MA_BT
#define PPFELDA_MA_BT 1
#endif
#else
#ifndef PPFELDA_MA_BT
#define PPFELDA_MA_BT 0
#endif
#endif

constexpr const char* FW_NAME    = "PPFELDA V2";
constexpr const char* FW_VERSION = "V1.2E";

// ===== KONFIGURACE RPM =====
#ifndef RPM_MODE_FREQ
#define RPM_MODE_FREQ 1   // 1 = čítání hran (ISR), 0 = pulseIn (blokující fallback)
#endif

// ===== PINY =====
#ifndef PIN_SDA
#define PIN_SDA 21
#define PIN_SCL 22
#define BTN_PIN 27
#define RESET_PIN 15
#define DOOR_PIN 32
#define FUEL_PIN 4
#define SPEED_PIN 17
#define RPM_PIN 16
#define BATT_PIN 36
#endif

// ===== KONSTANTY – VODA/PALIVO =====
// Napájení větve čidel (8V z budíků) + dělič R1/R2 pro ochranu ADS1115
constexpr float CIDLO_NAPAJENI_DEFAULT               = 8.0f;        // napájení větve [V]
constexpr float CIDLO_OHM_VODA_DEFAULT               = 243.0f;      // G2 sensor: zpětný výpočet z Vpin=1.431V při 90°C (ventilátor) → 243Ω
constexpr float CIDLO_OHM_PALIVO_DEFAULT             = 203.0f;      // zpětný výpočet: Vpin=1.710V při 7.4L → 203Ω
constexpr float CIDLO_OHM_VODA_CFG_MIN               = 50.0f;
constexpr float CIDLO_OHM_VODA_CFG_MAX               = 1400.0f;
constexpr float CIDLO_OHM_PALIVO_CFG_MIN             = 10.0f;
constexpr float CIDLO_OHM_PALIVO_CFG_MAX             = 500.0f;
constexpr float DELIC_R1                             = 15900.0f;    // horní odpor děliče [Ω] (12K+3.9K v sérii)
constexpr float DELIC_R2                             = 10000.0f;    // dolní odpor děliče [Ω] (10K v sérii)
constexpr int   ADS_MEDIAN_VZORKY                    = 3;
constexpr int   VODA_TAB_N                           = 7;
// Kalibrovana tabulka pro mereni pres vetev budiku. Hodnoty nejsou cisty odpor G2 cidla,
// ale odpor dopocteny modelem v ctejOdporCidlaAds(). Bod 550 ohm ~= 25 C je z realne diagnostiky.
static constexpr float vodaOhmTab[7]                 = { 1500.0f, 900.0f, 550.0f, 420.0f, 283.0f, 210.0f, 160.0f };
static constexpr float vodaTeplotaTab[7]             = {  -20.0f,   0.0f,  25.0f,  50.0f,  80.0f,  90.0f, 100.0f };
constexpr float VODA_MIN_OHM                         = 20.0f;       // pod 20Ω = cidlo odpojeno
constexpr float VODA_MAX_OHM                         = 15000.0f;    // nad 15kΩ = cidlo odpojeno (G2 při -20°C ≈ 28kΩ, přes dělič saturuje dříve)
constexpr float VODA_FILTR_ALFA                      = 0.18f;
constexpr int   ADS_KAN_VODA                         = 0;
constexpr int   ADS_KAN_PALIVO                       = 1;
constexpr int   ADS_KAN_TEPLOTA_VENKU                = 2;

constexpr int   PALIVO_TAB_N                         = 6;
static constexpr float palivoOhmTab[6]               = { 300.0f, 255.0f, 225.0f, 150.0f, 75.0f, 10.0f };
static constexpr float palivoLitruTab[6]             = { 3.5f, 7.0f, 11.0f, 22.0f, 33.0f, 44.0f };
constexpr float PALIVO_MIN_OHM                       = 5.0f;
constexpr float PALIVO_MAX_OHM                       = 50000.0f;
constexpr float PALIVO_FILTR_NAHORU_ALFA             = 0.06f;
constexpr float PALIVO_FILTR_DOLU_ALFA               = 0.02f;
constexpr float PALIVO_OFFSET_MIN_L                  = -10.0f;
constexpr float PALIVO_OFFSET_MAX_L                  = 10.0f;


//Lehká kalibrace na skutečnou spotřebu – mění se s časem, teplotou, kvalitou paliva atd. – umožňuje nastavit skutečnou spotřebu
//----------------------------------------------------------------------------------------------------
constexpr float    VSTRIK_CC_ZA_MIN                  = 155.0f; // Felicia 1.3 AMH vstřikovač 047 906 031 ~125 cc/min @ 2.5–3.0 bar (EV1 hi-Z 18.5Ω) (původně 125c)
constexpr uint32_t VSTRIK_FILTR_US                   = 500;   // filtr pro měření otevření vstřiku – 1000µs = 1ms (na test 500)
constexpr int      MOTOR_POCET_VSTRIKU_DEFAULT       = 4;
constexpr int      MERENY_POCET_VSTRIKU_DEFAULT      = 1;
//----------------------------------------------------------------------------------------------------

// ===== KONSTANTY – ČASOVÁNÍ =====
constexpr unsigned long PALIVO_VZORKOVANI_MS         = 750;  // 750ms = ~5 pulzů při volnoběhu → stabilní průměr
constexpr float         RYCHLOST_BLEND_KMH           = 12.0f;
constexpr unsigned long RYCHLOST_OKNO_MS             = 250;
constexpr unsigned long RYCHLOST_OKNO_MAX_MS         = 450;
constexpr float         RYCHLOST_NIZKA_KMH           = 30.0f;
constexpr unsigned long RYCHLOST_OKNO_NIZKA_MS       = 750;
constexpr unsigned long RYCHLOST_OKNO_NIZKA_MAX_MS   = 1100;
constexpr uint8_t       RYCHLOST_PERIODA_N           = 5;
constexpr unsigned long RYCHLOST_LOG_VYPADEK_MS      = 2500;

// ===== KONSTANTY – DISPLEJ/IKONY =====
constexpr uint8_t VODA_W     = 15;
constexpr uint8_t VODA_H     = 13;
constexpr uint8_t PALIVO_W   = 15;
constexpr uint8_t PALIVO_H   = 13;
constexpr uint8_t RYCHLOST_W = 15;
constexpr uint8_t RYCHLOST_H = 13;
constexpr uint8_t OTACKY_W   = 15;
constexpr uint8_t OTACKY_H   = 13;
constexpr uint8_t BATERIE_W  = 15;
constexpr uint8_t BATERIE_H  = 13;
constexpr uint8_t SPOTREBA_W = 15;
constexpr uint8_t SPOTREBA_H = 13;
constexpr uint8_t NACTENI_W  = 106;
constexpr uint8_t NACTENI_H  = 41;

constexpr uint8_t OLED_JAS_MAX_REZIM   = 0;
constexpr uint8_t OLED_JAS_STRED_REZIM = 1;
constexpr uint8_t OLED_JAS_NOC_REZIM   = 2;
constexpr uint8_t OLED_JAS_AUTO_REZIM  = 3;
constexpr uint8_t OLED_JAS_REZIM_DEFAULT = OLED_JAS_MAX_REZIM;
constexpr uint8_t OLED_KONTRAST_MAX    = 255;
constexpr uint8_t OLED_KONTRAST_STRED  = 110;
constexpr uint8_t OLED_KONTRAST_NOC    = 28;
constexpr uint8_t OLED_AUTO_NOC_OD_H   = 20;
constexpr uint8_t OLED_AUTO_NOC_DO_H   = 6;

constexpr uint8_t ANIMACE_VYP_REZIM    = 0;
constexpr uint8_t ANIMACE_RYCHLA_REZIM = 1;
constexpr uint8_t ANIMACE_NORMAL_REZIM = 2;
constexpr uint8_t ANIMACE_POMALA_REZIM = 3;
constexpr uint8_t ANIMACE_REZIM_DEFAULT = ANIMACE_NORMAL_REZIM;
constexpr unsigned long ANIMACE_RYCHLA_MS = 180UL;
constexpr unsigned long ANIMACE_NORMAL_MS = 260UL;
constexpr unsigned long ANIMACE_POMALA_MS = 380UL;

// ===== KONSTANTY – ADC/NAPÁJENÍ =====
constexpr float ADC_MAX                = 4095.0f;
constexpr float VREF                   = 3.3f;
constexpr float NAPETOVY_DELIC_POMER   = 4.67f;
constexpr float VYCHOZI_SPOTREBA_LH    = 7.0f;
constexpr float NAPETI_OFFSET_MIN_V     = -3.0f;
constexpr float NAPETI_OFFSET_MAX_V     = 3.0f;

// ===== KONSTANTY – VSTUPY/UI =====
constexpr unsigned long CAS_DLOUHEHO_STISKU       = 3000;  // ms pro aktivaci dlouhého stisku (servisní menu)
constexpr unsigned long DVERE_ANTIZKMIT           = 50;    // ms pro eliminaci zkratu při detekci otevření dveří
constexpr unsigned long BLIKANI_INTERVAL          = 500;   // ms interval pro blikání
constexpr float         EMA_ALFA                  = 0.10f; // EMA pro rychlejší blikání při nízkém palivu
constexpr unsigned long TLACITKO_DRZI_ANTIZKMIT   = 50;    // ms pro eliminaci zkratu při detekci držení tlačítka (servisní menu)
constexpr float         NIZKE_PALIVO_ZAP_L        = 5.0f;  // Pod tuto hladinu se zapne upozornění na nízké palivo (s hysterezí, vypne se až nad 5.8L)
constexpr float         NIZKE_PALIVO_VYP_L        = 5.8f;  // Nad tuto hladinu se vypne upozornění na nízké palivo (s hysterezí, zapne se až pod 5.0L)
constexpr float         NAPETI_NIZKE_V            = 11.5f;  // varování nízké baterie [V]
constexpr float         NAPETI_LOG_NIZKE_V        = 11.0f;
constexpr float         NAPETI_LOG_OBNOVENO_V     = 11.8f;
constexpr unsigned long NAPETI_LOG_ZPOZDENI_MS    = 3000UL;
constexpr unsigned long NAPETI_LOG_OBNOVA_MS      = 3000UL;
constexpr unsigned long CIDLO_LOG_ZPOZDENI_MS     = 5000UL;
constexpr unsigned long CIDLO_LOG_OBNOVA_MS       = 3000UL;

// ===== KONSTANTY – BOOT =====
constexpr unsigned long SPUSTENI_MIN_MS             = 800;
constexpr unsigned long SPUSTENI_MAX_MS             = 3000;
constexpr unsigned long NIZKE_PALIVO_UPOZORNENI_MS  = 7500;

// ===== KONSTANTY – VENKOVNÍ TEPLOTA =====
constexpr float VENKU_SERIOVY_OHM     = 10000.0f;
constexpr float VENKU_R0              = 10000.0f;
constexpr float VENKU_BETA            = 3950.0f;  // MF-52 10K NTC (B25/85)
constexpr float T0_KELVIN             = 298.15f;

// ===== KONSTANTY – RYCHLOST / RPM =====
constexpr uint32_t      RYCHLOST_VYPADEK_MS             = 1000;
constexpr uint32_t      OTACKY_FILTR_US                 = 150;
constexpr uint32_t      OTACKY_PRUMER_MS                = 150;
constexpr float         OTACKY_ALFA                     = 0.35f;
constexpr float         HRANY_NA_OTACKU                 = 4.0f;
constexpr uint8_t       ADS_CHYBA_LIMIT                 = 5;
constexpr unsigned long I2C_OBNOVA_MIN_INTERVAL_MS      = 30000UL;
constexpr uint32_t      RYCHLOST_FILTR_US               = 1500;
constexpr float         MAX_KMH_FYZICKY                 = 220.0f;
constexpr float         RYCHLOST_REZERVA                = 1.4f;
constexpr float         PULZY_NA_KM_DEFAULT             = 7840.0f;   // výchozí hodnota (Felicia 1.3 AMH s 185/60 R14, 2x snímač)
extern float            pulzyNaKm;                                   // runtime – nastavitelné v servis menu (3920 nebo 7840)

// Odhad řazího stupně (14SK, stálý převod 3.833, 175/65 R13, prahy RPM/km/h: 1.>99, 2.>60, 3.>41, 4.>34, 5.<34)
constexpr float         GEAR_RPM_NAHORU          = 2400.0f;  // RPM nad touto hodnotou → šipka "řadit výš"
constexpr float         GEAR_RPM_DOLU            = 1500.0f;  // RPM pod touto hodnotou → šipka "řadit níž"
constexpr float         GEAR_RPM_NAHORU_MIN      = 1500.0f;
constexpr float         GEAR_RPM_NAHORU_MAX      = 5000.0f;
constexpr float         GEAR_RPM_DOLU_MIN        = 700.0f;
constexpr float         GEAR_RPM_DOLU_MAX        = 3500.0f;
constexpr unsigned long GEAR_STABILNI_MS         = 400;      // nový kvalt musí chvíli vycházet, než se zobrazí
constexpr float         GEAR_POMER_MIN_PLATNY    = 20.0f;    // pod tímto RPM/km/h typicky spojka/neutrál za jízdy
extern float gearRpmNahoru;
extern float gearRpmDolu;
extern int odhadnutyStupen;                                  // 0 = neznámý, 1–5 = odhadnutý stupeň

constexpr uint64_t      DENNI_RESET_LIMIT_M       = 1000000ULL;  // denní počitadlo se protočí po 1000 km
constexpr unsigned long DOJEZD_AKTUALIZACE_MS     = 20000UL;

// ===== KONSTANTY – SD KARTA =====
constexpr int SD_CS   = 5;
constexpr int SD_SCK  = 14;
constexpr int SD_MISO = 12;
constexpr int SD_MOSI = 13;

constexpr unsigned long ULOZ_INTERVAL_MS   = 10000;
constexpr uint32_t      ULOZ_KAZDE_M       = 200;
constexpr unsigned long KONFIG_ULOZ_ZPOZDENI_MS = 1500;
constexpr uint32_t      ERROR_LOG_MAX_BYTES = 65536UL;

#define SOUBOR_STATISTIKY       "/consumption.txt"
#define SOUBOR_STATISTIKY_TMP   "/consumption.tmp"
#define SOUBOR_STATISTIKY_BAK   "/consumption.bak"
#define SOUBOR_KONFIGURACE      "/config.txt"
#define SOUBOR_KONFIGURACE_TMP  "/config.tmp"
#define SOUBOR_KONFIGURACE_BAK  "/config.bak"
#define SOUBOR_ERROR_LOG        "/error.log"
#define SOUBOR_ERROR_LOG_BAK    "/error.bak"

// ===== STRUCT =====
struct CidloDiag {
  int16_t raw;
  float   volts;
  float   ohms;
  bool    valid;
};

// ===== EXTERN – GLOBÁLNÍ PROMĚNNÉ =====
extern Adafruit_ADS1115 ads;

#if PPFELDA_MA_BT
extern BluetoothSerial SerialBT;
extern bool btAktivni;
#endif

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;
extern RtcDS3231<TwoWire> Rtc;

extern bool          adsPripraven;
extern uint8_t       adsChybPocet;
extern unsigned long i2cPosledniObnova;

extern volatile uint32_t rychlostPeriodaBuf[5];
extern volatile uint8_t  rychlostPeriodaZapisIdx;
extern volatile uint8_t  rychlostPeriodaPlatnych;
extern volatile uint32_t rychlostPulzuPocet;
extern volatile uint32_t rychlostPosledniPulzUs;
extern volatile uint32_t rychlostPeriodaUs;

extern volatile uint32_t vstrikOtevreniUsAkum;
extern volatile uint32_t vstrikPosledniHranaUs;
extern volatile bool     vstrikOtevreno;
extern volatile uint32_t vstrikPocetPulzu;
extern volatile bool     vstrikAktivniNizko;
extern bool              vstrikPolaritaAutoOtocena;
extern float             PALIVO_NA_US;
extern float             VSTRIK_TOK_CC_MIN;
extern int               motorPocetVstriku;
extern int               merenyPocetVstriku;
extern float             cidloNapajeni;
extern float             cidloOhmVoda;     // cívka ukazatele teploty vody [Ω]
extern float             cidloOhmPalivo;   // cívka ukazatele paliva [Ω]
extern uint32_t      palivoOknoUs;
extern uint32_t      palivoOknoPulzu;
extern unsigned long palivoPosledniAktualizace;

extern unsigned long dverePosledniCas;
extern unsigned long displayPosledniAktualizace;
extern unsigned long nizkePalivoZobrazeni;
extern unsigned long blikaniPosledniCas;

extern uint32_t      rychlostPosledniPulzuPocet;
extern uint32_t      vstrikPosledniPocetPulzu;
extern uint32_t      rychlostOknoZakladPocet;
extern unsigned long rychlostOknoZacatek;
extern float         rychlostOknoKmh;
extern unsigned long rychlostPosledniVypocet;

extern bool          dverePosledniStav;
extern bool          uiPrekreslit;
extern bool          tlacitkoStisknuto;
extern unsigned long tlacitkoStisknutiCas;
extern bool          blikaniZapnuto;
extern bool          rezimTestovani;

extern int  stranka;
extern int  predchoziStranka;
extern int  posledniKlicData;
extern bool alarmyPovoleny;
extern bool upozorneniNizkePalivoPovoleno;
extern bool upozorneniNizkePalivo;
extern bool dvereOtevrene;

extern float teplotaVody;
extern float teplotaOffsetC;
extern bool  teplotaInicializovana;

extern bool          startovani;
extern unsigned long startZacatek;
extern bool          sdPripravena;
extern bool          rtcPripravena;
extern bool          cidlaPripravena;

extern float teplotaVenku;
extern float teplotaVenkuOffsetC;
extern float betaVenku;
extern bool  teplotaVenkuInicializovana;
extern float palivoOffsetL;
extern float napetiOffsetV;
extern uint8_t oledJasRezim;
extern uint8_t animaceRezim;

extern CidloDiag vodaDiag;
extern CidloDiag palivoDiag;

extern uint32_t      diagRychlostPulzy;
extern unsigned long diagRychlostOknoMs;
extern uint32_t      diagRychlostMedianUs;
extern uint32_t      diagRychlostStariMs;
extern float         diagRychlostPocetKmh;
extern float         diagRychlostPeriodaKmh;
extern bool          diagRychlostAktualni;

extern uint32_t diagVstrikOknoUs;
extern uint32_t diagVstrikPulzy;
extern float    diagPalivoSyreLh;

extern volatile uint32_t otackyHrany;
extern volatile uint32_t otackyPosledniUs;
extern uint32_t          otackyHranyPred;
extern uint32_t          otackyT0;
extern volatile uint32_t rychlostPosledniUs;

extern char serialBuffer[64];
extern int  serialBufferIdx;
extern bool serialPreteceni;
#if PPFELDA_MA_BT
extern char btBuffer[64];
extern int  btBufferIdx;
extern bool btPreteceni;
#endif

extern SPIClass spiSD;

extern uint64_t      celkoveMetry_u64;
extern uint64_t      celkovePalivoUl_u64;
extern uint64_t      tripMetry_u64;
extern uint64_t      tripPalivoUl_u64;
extern uint64_t      denniMetry_u64;
extern uint32_t      casMotoru_s_u32;
extern uint64_t      posledniUlozeneMetry_u64;
extern unsigned long posledniUlozeniMs;
extern bool          konfigZmenena;
extern unsigned long konfigZmenenaCas;

extern float palivoTokInst;
extern esp_adc_cal_characteristics_t batAdcChars;
extern bool  batAdcKalibrovano;
extern float batFiltrovana;
extern float napetiBaterie;
extern float rychlostVozu;
extern float otackyMotoru;
extern float spotrebaLh;
extern float prumernaSpotreba;
extern float tripPrumernaSpotreba;
extern float hladinaPaliva;
extern float celkovePalivo;
extern float celkovaVzdalenost;
extern float tripPalivo;
extern float tripVzdalenost;
extern float denniVzdalenost;
extern float odhadovanyDojezd;

// ===== EXTERN – BITMAPY (definovány v bitmaps.cpp) =====
extern const unsigned char loading[];
extern const unsigned char water_icon_bits[];
extern const unsigned char fuel_icon_bits[];
extern const unsigned char speed_icon_bits[];
extern const unsigned char rpm_icon_bits[];
extern const unsigned char batt_icon_bits[];
extern const unsigned char SPOTREBA_bits[];

// ===== FORWARD DEKLARACE FUNKCÍ =====

// utils.cpp
float    interpolaceN(float x, const float* xv, const float* yv, int n);
float    omezNaRozsah(float x, float a, float b);
bool     senderOdporValidni(float rOhm, float minOhm, float maxOhm);
int      datumKlic(const RtcDateTime& dt);
int16_t  medianAdsSyrovy(int channel);
uint32_t medianPeriodaUs(const uint32_t* data, uint8_t n);
float    vypocetPalivaZOdporu(float rOhm);
bool     zmenenaHodnota(float current, float& previous, float tolerance);

// sensors.cpp
float vypocetTeplotyVodyZOdporu(float rOhm);
void  spocitejVoduAPalivo();
void  vypocetVenkovniTeploty();

// isr.cpp
void IRAM_ATTR fuelPulse();
void IRAM_ATTR speedPulse();
void IRAM_ATTR rpmIsr();

// storage.cpp
bool jePresnyPrikaz(const char* vstup, const char* prikaz);
void oznacKonfiguraciJakoZmenenou();
bool inicializujSD();
void moznaUlozSD();
void moznaUlozKonfiguraciSD();
bool vynulujStatistiky(bool ulozitNaSd = true);
bool ulozStatistikySD();
bool ulozKonfiguraciSdNyni();
void zapisErrorLog(const char* uroven, const char* kod, const char* detail = nullptr);
void zalogujStartSystemu();
void obsluzSystemovyLog();

// commands.cpp
void zpracujPrikaz(char* buffer, Stream& stream);
void obsluzSerialABluetooth();

// display.cpp
void blikniDisplej(uint8_t times = 3, uint16_t off_ms = 120, uint16_t on_ms = 120);  // default args POUZE zde
void obsluzBlikani();
float ctejOdporCidlaAds(int channel, float* rOut = nullptr, float* vOut = nullptr, int16_t* rawOut = nullptr);
void vykresliStartovniObrazovku(float progress, const char* stavText);
void vykresliDisplej();
bool potrebujePrekreslitUI();
void prepniStrankuSAnimaci(int smer);
void zrusAnimaciStranky();
bool animaceStrankyAktivni();
unsigned long dobaAnimaceStrankyMs();
void aktualizujJasDispleje(bool vynutit = false);

// inputs.cpp
void obsluzVstupy();
void obsluzDenniReset();

// service_menu.cpp
extern bool servisMenuAktivni;
extern int  servisMenuPolozka;
extern bool smZpravaAktivni;
extern bool smEditRezim;         // true = editace hodnoty, false = navigace položek
void servisMenuOtevri();
void servisMenuNext();
void servisMenuPrev();
void servisMenuPlus();
void servisMenuMinus();
void servisMenuVstupEditu();
void servisMenuEdit();           // zachováno pro zpětnou kompatibilitu
void servisMenuUlozAZavri();
void vykresliServisMenu();

// speed_fuel_rpm.cpp
void vypocetPalivoRychlost();
