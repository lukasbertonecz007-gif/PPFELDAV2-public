// „Vlídnost Panovníka, Boha našeho, buď s námi. Upevni nám dílo našich rukou, dílo našich rukou učiň pevným!“ Žalm 90:17
//----------------------------------------------------------------------------------------------------------------------
#include "globals.h"

Adafruit_ADS1115 ads;

// ====== Spotřeba z šířky pulzů vstřiku ======
volatile uint32_t vstrikOtevreniUsAkum = 0;
volatile uint32_t vstrikPosledniHranaUs = 0;
volatile bool vstrikOtevreno = false;
volatile uint32_t vstrikPocetPulzu = 0;
volatile bool vstrikAktivniNizko = true;
bool vstrikPolaritaAutoOtocena = false;

// pro autokalibraci průtoku
float PALIVO_NA_US = 0.0f;                       // L/us pro jeden měřený vstřik
float VSTRIK_TOK_CC_MIN = VSTRIK_CC_ZA_MIN;
int motorPocetVstriku = MOTOR_POCET_VSTRIKU_DEFAULT;
int merenyPocetVstriku = MERENY_POCET_VSTRIKU_DEFAULT;
float cidloNapajeni = CIDLO_NAPAJENI_DEFAULT;
float cidloOhmVoda = CIDLO_OHM_VODA_DEFAULT;
float cidloOhmPalivo = CIDLO_OHM_PALIVO_DEFAULT;

// akumulace otevření vstřiku v aktuálním okně
uint32_t palivoOknoUs = 0;
uint32_t palivoOknoPulzu = 0;
unsigned long palivoPosledniAktualizace = 0;


float palivoTokInst = 0.0f;   // rychleji reagující L/h pro okamžitou spotřebu


// Načítací obrazovka
bool startovani = true;
unsigned long startZacatek = 0;
bool sdPripravena = false, rtcPripravena = false, cidlaPripravena = false;
float bootFaze = 0.0f;              // posledni dosazena faze loading screeny (0.0 .. 1.0)
const char* bootFazeText = "Spouštění...";

// --- Globální proměnné ---
#if PPFELDA_MA_BT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
BluetoothSerial SerialBT;
#pragma GCC diagnostic pop
bool btAktivni = false;
#endif
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
RtcDS3231<TwoWire> Rtc(Wire);
bool adsPripraven = false;
uint8_t adsChybPocet = 0;
unsigned long i2cPosledniObnova = 0;
volatile uint32_t rychlostPeriodaBuf[RYCHLOST_PERIODA_N] = { 0 };
volatile uint8_t rychlostPeriodaZapisIdx = 0;
volatile uint8_t rychlostPeriodaPlatnych = 0;

volatile uint32_t rychlostPulzuPocet = 0;      // rychlost
volatile uint32_t rychlostPosledniPulzUs = 0;  // čas posledního validního pulzů (us)
volatile uint32_t rychlostPeriodaUs = 0;       // perioda mezi dvěma posledními pulzy (us)

unsigned long dverePosledniCas = 0;            // ms
unsigned long displayPosledniAktualizace = 0;  // ms
unsigned long nizkePalivoZobrazeni = 0;        // ms
unsigned long blikaniPosledniCas = 0;          // ms

uint32_t rychlostPosledniPulzuPocet = 0;
uint32_t vstrikPosledniPocetPulzu = 0;
uint32_t rychlostOknoZakladPocet = 0;
unsigned long rychlostOknoZacatek = 0;
float rychlostOknoKmh = 0.0f;
unsigned long rychlostPosledniVypocet = 0;

bool dverePosledniStav = HIGH;
bool uiPrekreslit = true;
bool tlacitkoStisknuto = false;
unsigned long tlacitkoStisknutiCas = 0;
bool blikaniZapnuto = false;
bool rezimTestovani = false;
float pulzyNaKm = 7840.0f;

int stranka = 1;
int predchoziStranka = 1;
int posledniKlicData = -1;
bool alarmyPovoleny = false;
bool upozorneniNizkePalivoPovoleno = false;
bool upozorneniNizkePalivo = false;
bool dvereOtevrene = false;          // dveře otevřené (jeden společný pin)

float teplotaVody = 0.0f;
float teplotaOffsetC = 0.0f;     // kalibrace teploty vody (není potřeba asi protože se dá kalibrovat v servis menu)
bool teplotaInicializovana = false;

esp_adc_cal_characteristics_t batAdcChars;
bool batAdcKalibrovano = false;    // pro měření napětí baterie přes dělič a ADC
float batFiltrovana = NAN;         // filtrovaná napětí baterie pro stabilnější zobrazení a výpočty
float napetiBaterie = 0.0f;        // V
float rychlostVozu = 0.0f;         // km/h
float otackyMotoru = 0.0f;         // RPM
int   odhadnutyStupen = 0;         // 0=neznámý, 1-5=odhadnutý stupeň
float gearRpmNahoru = GEAR_RPM_NAHORU;
float gearRpmDolu = GEAR_RPM_DOLU;
float spotrebaLh = 0.0f;           // L/h
float prumernaSpotreba = 0.0f;     // L/100 km
float tripPrumernaSpotreba = 0.0f; // L/100 km od posledniho resetu
float hladinaPaliva = 0.0f;        // L
float celkovePalivo = 0.0f;        // L
float celkovaVzdalenost = 0.0f;    // km
float tripPalivo = 0.0f;           // L od posledniho resetu
float tripVzdalenost = 0.0f;       // km od posledniho resetu
float denniVzdalenost = 0.0f;      // km
float odhadovanyDojezd = 0.0f;     // km

float teplotaVenku = 0.0f;
float teplotaVenkuOffsetC = 0.0f;
float betaVenku = VENKU_BETA;
bool teplotaVenkuInicializovana = false;
float palivoOffsetL = 0.0f;
float napetiOffsetV = 0.0f;
uint8_t oledJasRezim = OLED_JAS_REZIM_DEFAULT;
uint8_t animaceRezim = ANIMACE_REZIM_DEFAULT;

CidloDiag vodaDiag = { 0, NAN, NAN, false };
CidloDiag palivoDiag = { 0, NAN, NAN, false };

uint32_t diagRychlostPulzy = 0;
unsigned long diagRychlostOknoMs = 0;
uint32_t diagRychlostMedianUs = 0;
uint32_t diagRychlostStariMs = 0;
float diagRychlostPocetKmh = 0.0f;
float diagRychlostPeriodaKmh = 0.0f;
bool diagRychlostAktualni = false;

uint32_t diagVstrikOknoUs = 0;
uint32_t diagVstrikPulzy = 0;
float diagPalivoSyreLh = 0.0f;

// RPM dělič a filtr
volatile uint32_t otackyHrany = 0;
volatile uint32_t otackyPosledniUs = 0;
uint32_t otackyHranyPred = 0;
uint32_t otackyT0 = 0;

volatile uint32_t rychlostPosledniUs = 0;

// Buffer pro Serial/BT
char serialBuffer[64];
int serialBufferIdx = 0;
bool serialPreteceni = false;
#if PPFELDA_MA_BT
char btBuffer[64];
int btBufferIdx = 0;
bool btPreteceni = false;
#endif

// --- SD karta (HSPI) ---
SPIClass spiSD(HSPI);

// integer akumulátory
uint64_t celkoveMetry_u64 = 0;     // metry
uint64_t celkovePalivoUl_u64 = 0;  // mikrolitry
uint64_t tripMetry_u64 = 0;        // metry od posledniho resetu
uint64_t tripPalivoUl_u64 = 0;     // mikrolitry od posledniho resetu
uint64_t denniMetry_u64 = 0;       // metry/den
uint32_t casMotoru_s_u32 = 0;      // rezerva

uint64_t posledniUlozeneMetry_u64 = 0;
unsigned long posledniUlozeniMs = 0;
bool konfigZmenena = false;
unsigned long konfigZmenenaCas = 0;

void setup() {
  Serial.begin(115200);

  // Displej jako první – zobrazí "Spouštění..." okamžitě, před BT a SD
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.setTimeOut(50);
  u8g2.begin();
  u8g2.setBusClock(400000);
  u8g2.enableUTF8Print();
  vykresliStartovniObrazovku(0.0f, "Spouštění...");  // zobraz hned při startu

#if PPFELDA_MA_BT
  btAktivni = SerialBT.begin("OpenFelicia");  // ~300-600ms blokace – po displeji
  if (!btAktivni) {
    Serial.println("Bluetooth se nepodarilo inicializovat.");
  }
  bootFazeText = btAktivni ? "Bluetooth OK" : "Bluetooth err";
#else
  Serial.println("Bluetooth neni v tomto buildu dostupny.");
  bootFazeText = "BT vypnut";
#endif
  bootFaze = 0.25f;
  vykresliStartovniObrazovku(bootFaze, bootFazeText);
#if PPFELDA_MA_BT
  if (!btAktivni) { for (uint8_t _b = 0; _b < 8; _b++) { vykresliStartovniObrazovku(bootFaze, _b & 1 ? "" : bootFazeText); delay(250); } }
#endif

  // start čtečky SD karty
  if (!inicializujSD()) {
    Serial.println("CHYBA: inicializace SD (HSPI). Zkontroluj CS=5, SCK=14, MISO=12, MOSI=13 a format FAT32/FAT16.");
    sdPripravena = false;
  } else {
    sdPripravena = true;
    Serial.println(pulzyNaKm, 2);  // pulzyNaKm
  }
  bootFaze = 0.50f;
  bootFazeText = sdPripravena ? "SD karta OK" : "SD karta err";
  vykresliStartovniObrazovku(bootFaze, bootFazeText);
  if (!sdPripravena) { for (uint8_t _b = 0; _b < 8; _b++) { vykresliStartovniObrazovku(bootFaze, _b & 1 ? "" : bootFazeText); delay(250); } }

  // start ADS1115
  if (!ads.begin()) {
    Serial.println("CHYBA: ADS1115 nenalezeno! Zkontroluj zapojeni SDA/SCL.");
    adsPripraven = false;
  } else {
    Serial.println("ADS1115 OK.");
    // Větve v budičích umi jit vys nez 3V3, necháme větší rezervu.
    ads.setGain(GAIN_TWOTHIRDS);
    ads.setDataRate(RATE_ADS1115_860SPS);  // 860 SPS → ~1.2 ms/konverze místo výchozích ~7.8 ms
    adsPripraven = true;
  }
  bootFaze = 0.75f;
  bootFazeText = adsPripraven ? "Čidla OK" : "ADS chyba";
  vykresliStartovniObrazovku(bootFaze, bootFazeText);
  if (!adsPripraven) { for (uint8_t _b = 0; _b < 8; _b++) { vykresliStartovniObrazovku(bootFaze, _b & 1 ? "" : bootFazeText); delay(250); } }

  pinMode(DOOR_PIN, INPUT_PULLUP);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);

  pinMode(FUEL_PIN, INPUT_PULLUP);
  pinMode(SPEED_PIN, INPUT_PULLUP);
  pinMode(RPM_PIN, INPUT_PULLUP);

  dvereOtevrene = (digitalRead(DOOR_PIN) == LOW);
  dverePosledniStav = !dvereOtevrene ? HIGH : LOW;

  attachInterrupt(digitalPinToInterrupt(FUEL_PIN), fuelPulse, CHANGE);
  attachInterrupt(digitalPinToInterrupt(SPEED_PIN), speedPulse, RISING);
  attachInterrupt(digitalPinToInterrupt(RPM_PIN), rpmIsr, CHANGE);

  analogReadResolution(12);
  analogSetPinAttenuation(BATT_PIN, ADC_11db);
  batAdcKalibrovano = (esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &batAdcChars) != ESP_ADC_CAL_VAL_NOT_SUPPORTED);
  Serial.printf("ADC kalibrace baterie: %s\n", batAdcKalibrovano ? "OK (eFuse)" : "neni podporovana, pouzivam vychozi");

  Rtc.Begin();
  if (!Rtc.GetIsRunning()) Rtc.SetIsRunning(true);
  rtcPripravena = Rtc.IsDateTimeValid();
  if (!rtcPripravena) {
    Serial.println("RTC neni validni, datum/cas nebyl automaticky prepsan.");
  }
  aktualizujJasDispleje(true);
  bootFaze = 0.90f;
  bootFazeText = rtcPripravena ? "RTC OK" : "RTC chyba";
  vykresliStartovniObrazovku(bootFaze, bootFazeText);
  if (!rtcPripravena) { for (uint8_t _b = 0; _b < 8; _b++) { vykresliStartovniObrazovku(bootFaze, _b & 1 ? "" : bootFazeText); delay(250); } }
  startZacatek = millis();
  posledniKlicData = rtcPripravena ? datumKlic(Rtc.GetDateTime()) : -1;
  startovani = true;
  zalogujStartSystemu();

  // WDT – inicializace a přihlášení (10 s timeout, restart při přetečení)
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = 10000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdtConfig);
#else
  esp_task_wdt_init(10, true);
#endif
  esp_task_wdt_add(NULL);

  displayPosledniAktualizace = millis();
  palivoPosledniAktualizace = millis();
  rychlostOknoZacatek = millis();
  rychlostOknoZakladPocet = rychlostPulzuPocet;
}

void loop() {
  obsluzSerialABluetooth();
  spocitejVoduAPalivo();
  if (!cidlaPripravena && teplotaInicializovana) cidlaPripravena = true;

  if (startovani) {
    unsigned long elapsed = millis() - startZacatek;
    bool allReady = (sdPripravena && rtcPripravena && cidlaPripravena);

    if ((elapsed < SPUSTENI_MIN_MS) || (!allReady && elapsed < SPUSTENI_MAX_MS)) {
      float elapsedP = (float)elapsed / (float)SPUSTENI_MIN_MS;
      float p = elapsedP > bootFaze ? elapsedP : bootFaze;
      if (p > 1.0f) p = 1.0f;
      const char* t = allReady ? "Hotovo!" : bootFazeText;
      vykresliStartovniObrazovku(p, t);
      esp_task_wdt_reset();
      return;
    } else {
      startovani = false;
      uiPrekreslit = true;
    }
  }

  obsluzVstupy();
  vypocetPalivoRychlost();
  vypocetVenkovniTeploty();
  obsluzDenniReset();
  obsluzSystemovyLog();

  // Engine time – inkrementuj když motor běží (RPM > 300)
  // Akumuluje celé ms v 32bit proměnné, do casMotoru_s_u32 se dělí až při ukládání
  {
    static unsigned long engineTimerLastMs = 0;
    static uint32_t engineMsAccum = 0;
    unsigned long nowEng = millis();
    if (otackyMotoru > 300.0f) {
      if (engineTimerLastMs > 0) {
        uint32_t deltaMs = (uint32_t)(nowEng - engineTimerLastMs);
        engineMsAccum += deltaMs;
        if (engineMsAccum >= 1000) {
          casMotoru_s_u32 += engineMsAccum / 1000;
          engineMsAccum %= 1000;
        }
      }
      engineTimerLastMs = nowEng;
    } else {
      engineTimerLastMs = 0;
      engineMsAccum = 0;
    }
  }

  unsigned long currentTime = millis();
  const unsigned long RENDER_INTERVAL = animaceStrankyAktivni() ? 33UL : 100UL;

  if (currentTime - displayPosledniAktualizace >= RENDER_INTERVAL) {
    if (potrebujePrekreslitUI()) {
      vykresliDisplej();
      uiPrekreslit = false;
    }
    displayPosledniAktualizace = currentTime;
  }

  obsluzBlikani();
  aktualizujJasDispleje();
  moznaUlozKonfiguraciSD();
  moznaUlozSD();
  esp_task_wdt_reset();
}
