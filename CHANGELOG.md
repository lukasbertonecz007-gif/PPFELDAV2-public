# Historie verzi

## PPFELDA V2 V1.2E experimental

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

- Firmware se identifikuje jako `PPFELDA V2 V1.2E`.
- Verze je stale experimental, proto je vhodne ji otestovat v aute pred oznacenim jako stabilni.
- Data na SD karte zustavaji ve stejnem formatu textovych souboru, novy firmware pouze doplni kontrolni CRC radek.
