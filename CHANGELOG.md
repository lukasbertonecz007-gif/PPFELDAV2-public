# Historie verzi

## OpenFelicia V1.4A/E experimental - 2026-08-24

Verejna experimentalni sestava vychazejici z interne otestovane V1.4A.

### Servis a najezd

- Samostatny celkovy najezd vozidla pro servisni vypocty.
- Servisni interval oleje a rozvodu bez pridani nove informacni stranky.
- Zobrazeni intervalu, posledniho resetu a zbyvajicich kilometru v servisnim menu.
- Potvrzovane vynulovani oleje a rozvodu v servisnim menu.
- Nastaveni najezdu a intervalu pres USB Serial i Bluetooth.

### Bluetooth, varovani a displej

- Bluetooth ikona se zobrazi pouze pri skutecne pripojenem klientovi.
- Pridana centralni varovani pro servis, prehrati, baterii, SD, RTC, ADS, cidla a palivo.
- Servisni a bezna upozorneni sviti trvale, kriticke poruchy blikaji.
- Detail aktivnich varovani je dostupny v servisnim menu.
- Ikony varovani a Bluetooth maji 20x22 px a centruji se podle skutecne sirky hodin.
- Hodiny a nadpis servisniho menu se automaticky centruji.

### Rychlost a vstrikovani

- Lepsi potlaceni izolovanych chybnych pulzu rychlosti.
- Stabilnejsi vypocet pod 30 km/h a krokovani po 0,5 km/h.
- Hystereze rezimu nizke rychlosti mezi 28 a 34 km/h.
- Nastavitelny filtr pulzu vstriku 200 az 2000 us, vychozi hodnota 800 us.
- Pulzy vstriku nad 30 ms se odmitnou a diagnostika pocita odmitnute pulzy.
- Okamzita spotreba se po 750ms okne bez platneho pulzu nastavi primo na nulu.

### SD karta a kompatibilita

- Servisni data pouzivaji samostatny soubor `service.txt` s CRC a zalohou.
- Zapis probiha pres docasny soubor a automatickou obnovu `service.bak`.
- Stare `config.txt` a `consumption.txt` zustavaji kompatibilni.
- Chybejici `service.txt` nezpusobi falesne servisni upozorneni.
- Zachovan rezim `RPM_MODE_FREQ` i puvodni easter egg prikaz.

### Nove prikazy

```text
km [hodnota]
servis
servis.olej <km>
servis.olej.reset
servis.rozvody <km>
servis.rozvody.reset
vstrik.filtr <200..2000 us>
```

### Aktualizace

- PlatformIO pouziva partition tabulku `ota_nofs_4MB.csv` a Arduino ESP32 core 3.3.9.
- Pri prechodu ze starsi partition tabulky je doporuceno jednou vymazat flash a pote firmware nahrat znovu.
- Data o spotrebe, najezdu a konfiguraci ulozena na SD karte zustanou zachovana.
- Verze je experimentalni; pred beznym provozem zkontrolujte zapojeni a funkce na konkretnim vozidle.

## OpenFelicia V1.2E experimental

Testovaci verejna verze palubniho pocitace pro Skoda Felicia 1.3 MPI.

### Co je nove

- Plynula animace pri prepinani informacnich stranek.
- Nastavitelny jas OLED displeje: MAX, STRED, NOC a AUTO.
- Nastavitelna rychlost animace: VYP, RYCH, NORMAL a POMAL.
- Bezpecnejsi ukladani konfigurace a statistik na SD kartu pomoci CRC kontroly.
- Automaticka obnova `config.txt` a `consumption.txt` ze zalohy, pokud je hlavni soubor poskozeny.
- Rozsirene diagnosticke logy pro restart ESP32, brownout, napeti, ADS/I2C, cidla a rychlostni signal.
- Stabilnejsi mereni nizkych rychlosti a lepsi potlaceni skokovych hodnot.
- Oddelena lifetime a trip statistika spotreby.
- Denni najezd se resetuje az po protoceni 1000 km.

### Poznamky

- Firmware se identifikuje jako `OpenFelicia V1.2E`.
- Verze je stale experimental, proto je vhodne ji otestovat v aute pred oznacenim jako stabilni.
- Data na SD karte zustavaji ve stejnem formatu textovych souboru, novy firmware pouze doplni kontrolni CRC radek.
