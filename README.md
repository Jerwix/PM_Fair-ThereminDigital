# 🎵 Theremin Digital cu ESP32

Un instrument muzical fără contact fizic, inspirat din thereminul clasic. Controlează frecvența sunetului prin mișcarea mâinii deasupra unui senzor ultrasonic.

---

## 📖 Cuprins

- [Explicația codului](#-explicația-codului)
  - [Includeri și configurare inițială](#includeri-și-configurare-inițială)
  - [buttonISR()](#buttonisr--întreruperea-pentru-butonul-mute)
  - [measureDistance()](#measuredistance--măsurarea-distanței)
  - [getSmoothedDistance()](#getsmoothedistance--filtrul-de-medie-mobilă)
  - [readOctave()](#readoctave--citirea-octavei-de-la-potențiometru)
  - [calculateFrequency()](#calculatefrequency--calculul-frecvenței)
  - [updateOLED()](#updateoled--actualizarea-display-ului)
  - [setup()](#setup--inițializarea)
  - [loop()](#loop--bucla-principală)
- [Librării folosite](#-librării-folosite)

---

## 🔍 Explicația codului

### Includeri și configurare inițială
> Liniile 1–53

Codul începe cu includerea a 4 librării:
- `Arduino.h` — funcțiile de bază Arduino
- `Wire.h` — protocolul I2C
- `Adafruit_GFX.h` — funcții grafice
- `Adafruit_SSD1306.h` — driver-ul OLED-ului

Apoi se definesc toți pinii hardware ca constante (`TRIG_PIN`, `ECHO_PIN`, etc.) pentru a fi ușor de modificat. Se declară array-urile cu numele notelor muzicale și frecvențele din octava 4 conform scalei temperate egale. Variabilele de stare (`muted`, `previousNote`, `currentFrequency`) țin evidența stării curente a instrumentului.

---

### `buttonISR()` — Întreruperea pentru butonul mute

Funcția este marcată cu `IRAM_ATTR` care îi spune ESP32-ului să o stocheze în RAM-ul rapid, nu în flash — necesar pentru ISR-uri pe ESP32.

La fiecare apăsare, verifică dacă au trecut minim **200ms** de la ultima apăsare (debounce software) și comută variabila `muted`. Variabila e declarată `volatile` pentru că e modificată din ISR și citită din `loop()` — fără `volatile`, compilatorul ar putea optimiza citirea și `loop()` nu ar vedea niciodată schimbarea.

---

### `measureDistance()` — Măsurarea distanței

Trimite un puls de **10μs** pe pinul TRIGGER, apoi așteaptă ecoul pe pinul ECHO cu `pulseIn()`. Timeout-ul de 30ms previne blocarea dacă nu vine ecou (obiect prea departe sau absent).

Durata ecou-ului e convertită în centimetri cu formula:

```
distanță = durată × 0.034 / 2
```

`0.034 cm/μs` este viteza sunetului, împărțit la 2 pentru că sunetul face dus-întors.

---

### `getSmoothedDistance()` — Filtrul de medie mobilă

Stochează ultimele **5 citiri** într-un buffer circular. La fiecare apel:
1. Adaugă o citire nouă la poziția curentă
2. Avansează indexul cu modulo
3. Calculează media tuturor citirilor valide (> 0)

Acest filtru elimină fluctuațiile mici ale senzorului — fără el, distanța sare cu **±2 cm** între citiri consecutive, ceea ce ar face buzzerul să schimbe nota continuu.

---

### `readOctave()` — Citirea octavei de la potențiometru

Citește valoarea ADC pe 12 biți (**0–4095**) de la potențiometru și o mapează la intervalul **2–7** cu funcția `map()`.

| Potențiometru | Octava | Exemplu (nota La) |
|---|---|---|
| Minim (0) | Octava 2 | 110 Hz |
| Centru (~2048) | Octava 4 | 440 Hz |
| Maxim (4095) | Octava 7 | 3520 Hz |

---

### `calculateFrequency()` — Calculul frecvenței

Folosește formula:

```
frecvență = frecvența_din_octava_4 × 2^(octavă - 4)
```

De exemplu, **La** în octava 4 e 440 Hz, în octava 5 e 880 Hz (dublu), în octava 3 e 220 Hz (jumătate). Multiplicarea cu puteri ale lui 2 e principiul fundamental al octavelor muzicale.

---

### `updateOLED()` — Actualizarea display-ului

Mai întâi verifică dacă au trecut minim **100ms** de la ultimul refresh — dacă nu, returnează imediat. Asta previne flickering-ul și economisește timp de procesor (un refresh I2C complet durează ~5ms).

Afișează una din trei stări:
- **`MUTE`** — mare, pe centru (când butonul e apăsat)
- **`Move hand!`** — dacă mâna nu e în intervalul 3–40 cm
- **Nota curentă** — mare, cu frecvența și distanța dedesubt

---

### `setup()` — Inițializarea

1. Pornește OLED-ul și afișează splash screen-ul *"THEREMIN Digital v1.0"* timp de 1.5s
2. Configurează pinii GPIO (TRIGGER ca output, ECHO ca input, buton cu pull-up intern)
3. Inițializează canalul PWM cu rezoluție de **12 biți** pe pinul buzzerului
4. Atașează întreruperea externă pe buton cu trigger pe **FALLING edge** (când pinul trece din HIGH în LOW = apăsare)
5. Inițializează bufferul de citiri cu zerouri

---

### `loop()` — Bucla principală

Rulează la fiecare **50ms** (20 de ori pe secundă). Secvența:

```
Citește distanța mediată (HC-SR04)
         ↓
Citește octava (potențiometru ADC)
         ↓
    ┌─ Mute ON sau mâna în afara range-ului?
    │     → Oprește buzzerul
    │
    └─ Mâna în range (3–40 cm)?
          → Mapează distanța la index notă (0–11)
          → Calculează frecvența
          → Actualizează buzzerul (doar dacă nota/octava s-au schimbat)
          ↓
   Actualizează OLED + trimite date pe Serial
```

---

## 📚 Librării folosite

### Adafruit SSD1306

Driver-ul hardware pentru controlerul SSD1306 de pe plăcuța OLED-ului. Gestionează comunicarea I2C cu chipul: inițializarea registrelor interne, configurarea contrastului, orientării și modului de adresare a memoriei.

Menține un **buffer de 1024 bytes** în RAM-ul ESP32 (`128×64 pixeli / 8 biți = 1024 bytes`) care reprezintă conținutul ecranului. Când apelezi `display.display()`, întregul buffer e trimis prin I2C la adresa `0x3C`.

Funcțiile principale:
- `begin()` — inițializare hardware
- `clearDisplay()` — golește bufferul
- `display()` — trimite bufferul la ecran

### Adafruit GFX

Librărie grafică generică care funcționează cu orice display (OLED, LCD, TFT) ce implementează interfața Adafruit. Oferă funcții de desenare de nivel înalt:

- `setCursor(x, y)` — poziționarea textului
- `setTextSize(n)` — scalarea fontului (multiplicare de n ori)
- `print()` — afișarea textului
- Funcții pentru linii, dreptunghiuri, cercuri

Este o **dependință obligatorie** a librăriei SSD1306 — fără ea, SSD1306 nu ar ști cum să deseneze text sau forme, ci doar cum să trimită pixeli bruti la ecran.

### Wire.h

Librăria standard Arduino pentru protocolul **I2C** (Two-Wire Interface). Gestionează comunicarea sincronă master-slave pe două fire: **SDA** (date) și **SCL** (ceas). Pe ESP32, SDA e pe GPIO21 și SCL pe GPIO22 implicit.

Librăria Adafruit SSD1306 o folosește intern — nu o apelăm noi direct în cod, dar trebuie inclusă pentru ca SSD1306 să poată trimite comenzi și date pe magistrala I2C.
