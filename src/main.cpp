#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// === OLED DISPLAY ===
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// === PIN DEFINITIONS ===
#define TRIG_PIN    5
#define ECHO_PIN    18
#define BUZZER_PIN  25
#define POT_PIN     34
#define BUTTON_PIN  4

// === PWM CHANNEL ===
#define BUZZER_CH   0

// === DISTANCE BOUNDS (cm) ===
#define DIST_MIN    3.0
#define DIST_MAX    40.0

// === MOVING AVERAGE FILTER ===
#define NUM_READINGS   5
float readings[NUM_READINGS];
int readIndex = 0;

// === MUSICAL NOTES ===
const char* noteNames[] = {
  "Do", "Do#", "Re", "Re#", "Mi", "Fa",
  "Fa#", "Sol", "Sol#", "La", "La#", "Si"
};

// Base frequencies for octave 4 (equal temperament)
const float octave4Freq[] = {
  261.63, 277.18, 293.66, 311.13, 329.63, 349.23,
  369.99, 392.00, 415.30, 440.00, 466.16, 493.88
};

// === STATE VARIABLES ===
volatile bool muted = false;
volatile unsigned long lastButtonPress = 0;
int previousNote = -1;
int previousOctave = -1;
float currentFrequency = 0;

// === OLED REFRESH TIMING ===
unsigned long lastOLEDRefresh = 0;
#define OLED_INTERVAL 100  // refresh every 100ms

// === INTERRUPT SERVICE ROUTINE — Mute Button ===
// Uses hardware external interrupt on GPIO4 (FALLING edge).
// Debounce handled in software with a 200ms guard window.
void IRAM_ATTR buttonISR() {
  unsigned long now = millis();
  if (now - lastButtonPress > 200) {
    muted = !muted;
    lastButtonPress = now;
  }
}

// === DISTANCE MEASUREMENT ===
// Sends a 10us trigger pulse, then measures echo pulse duration.
// Distance formula: d = (duration_us * 0.034) / 2
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  if (duration == 0) return -1;
  return duration * 0.034 / 2.0;
}

// === MOVING AVERAGE FILTER ===
// Averages the last NUM_READINGS valid distance measurements
// to reduce sensor noise and prevent frequency jitter.
float getSmoothedDistance() {
  float newReading = measureDistance();
  if (newReading < 0) return -1;

  readings[readIndex] = newReading;
  readIndex = (readIndex + 1) % NUM_READINGS;

  float sum = 0;
  int validCount = 0;
  for (int i = 0; i < NUM_READINGS; i++) {
    if (readings[i] > 0) {
      sum += readings[i];
      validCount++;
    }
  }
  if (validCount == 0) return -1;
  return sum / validCount;
}

// === OCTAVE SELECTION ===
// Reads 12-bit ADC value (0–4095) from potentiometer
// and maps it linearly to octave range 2–7.
int readOctave() {
  int adcValue = analogRead(POT_PIN);
  return map(adcValue, 0, 4095, 2, 7);
}

// === FREQUENCY CALCULATION ===
// Uses equal temperament formula: f = base_freq * 2^(octave - 4)
// where base_freq is the note frequency in octave 4.
float calculateFrequency(int noteIndex, int octave) {
  return octave4Freq[noteIndex] * pow(2, octave - 4);
}

// === OLED DISPLAY UPDATE ===
// Communicates with SSD1306 via I2C (GPIO21=SDA, GPIO22=SCL).
// Rate-limited to every OLED_INTERVAL ms to prevent flickering.
void updateOLED(const char* note, float freq, int octave, float dist, bool isMuted, bool inRange) {
  unsigned long now = millis();
  if (now - lastOLEDRefresh < OLED_INTERVAL) return;
  lastOLEDRefresh = now;

  display.clearDisplay();

  if (isMuted) {
    display.setTextSize(2);
    display.setCursor(30, 25);
    display.print("MUTE");
  } else if (!inRange) {
    display.setTextSize(1);
    display.setCursor(25, 28);
    display.print("Move hand!");
  } else {
    // Note name — large, centered
    display.setTextSize(3);
    int nameLen = strlen(note);
    int x = (128 - nameLen * 18) / 2;
    display.setCursor(x, 5);
    display.print(note);

    // Octave number — small, next to note
    display.setTextSize(1);
    display.setCursor(x + nameLen * 18 + 2, 12);
    display.print(octave);

    // Frequency
    display.setTextSize(1);
    display.setCursor(0, 38);
    display.print("Freq: ");
    display.print(freq, 1);
    display.print(" Hz");

    // Distance and octave
    display.setCursor(0, 50);
    display.print("Dist: ");
    display.print(dist, 1);
    display.print(" cm  Oct:");
    display.print(octave);
  }

  display.display();
}

void setup() {
  Serial.begin(115200);

  // --- OLED initialization ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED failed to start!");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 10);
    display.print("THEREMIN");
    display.setTextSize(1);
    display.setCursor(25, 40);
    display.print("Digital v1.0");
    display.display();
    delay(1500);
  }

  // --- GPIO pin configuration ---
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // --- PWM setup for buzzer ---
  // 12-bit resolution for cleaner tone generation.
  // Initial frequency 440 Hz is overwritten immediately in loop().
  ledcSetup(BUZZER_CH, 440, 12);
  ledcAttachPin(BUZZER_PIN, BUZZER_CH);
  ledcWriteTone(BUZZER_CH, 0); // start silent

  // --- External interrupt for mute button ---
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);

  // --- Initialize reading buffer ---
  for (int i = 0; i < NUM_READINGS; i++) readings[i] = 0;

  Serial.println("=== DIGITAL THEREMIN ===");
}

void loop() {
  // 1. Read smoothed distance from HC-SR04
  float distance = getSmoothedDistance();

  // 2. Read octave from potentiometer (ADC)
  int octave = readOctave();

  // 3. Process: mute, out of range, or play note
  if (muted || distance < 0 || distance < DIST_MIN || distance > DIST_MAX) {
    // --- Silence ---
    ledcWriteTone(BUZZER_CH, 0);
    previousNote = -1;
    previousOctave = -1;

    // Update OLED
    updateOLED("", 0, octave, distance, muted, false);

    // Serial debug
    Serial.print("Dist: ");
    if (distance < 0) Serial.print("---");
    else { Serial.print(distance, 1); Serial.print(" cm"); }
    Serial.print(" | Octave: ");
    Serial.print(octave);
    Serial.println(muted ? " | MUTE" : " | ---");

  } else {
    // 4. Map distance to note index (0–11)
    //    Close (3 cm) = highest note (Si), Far (40 cm) = lowest (Do)
    int noteIndex = map((int)(distance * 10), DIST_MIN * 10, DIST_MAX * 10, 11, 0);
    noteIndex = constrain(noteIndex, 0, 11);

    // 5. Update tone only when note or octave changes (optimization)
    if (noteIndex != previousNote || octave != previousOctave) {
      previousNote = noteIndex;
      previousOctave = octave;
      currentFrequency = calculateFrequency(noteIndex, octave);
      ledcWriteTone(BUZZER_CH, (int)currentFrequency);
    }

    // 6. Update OLED
    updateOLED(noteNames[noteIndex], currentFrequency, octave, distance, false, true);

    // 7. Serial debug
    Serial.print("Dist: ");
    Serial.print(distance, 1);
    Serial.print(" cm | ");
    Serial.print(noteNames[noteIndex]);
    Serial.print(octave);
    Serial.print(" | ");
    Serial.print(currentFrequency, 1);
    Serial.println(" Hz");
  }

  delay(50); // 20 readings per second
}