/*
 * Natatometer - Data Logger with LED Indicators
 * 
 * Visual feedback via 3 LEDs:
 *   GREEN  = On target pace
 *   RED    = Too fast
 *   YELLOW = Too slow
 * 
 * Controls:
 *   SHORT press: Start/Stop recording
 *   LONG press (3s): Dump data to Serial as CSV
 *   
 * Wiring:
 *   BNO055 VIN -> 3.3V
 *   BNO055 GND -> GND
 *   BNO055 SDA -> GPIO 21
 *   BNO055 SCL -> GPIO 22
 *   Button     -> GPIO 4 (other leg to GND)
 *   Speaker +  -> GPIO 25
 *   Speaker -  -> GND
 *   Green LED  -> GPIO 13 (with 220Ω resistor to GND)
 *   Red LED    -> GPIO 14 (with 220Ω resistor to GND)
 *   Yellow LED -> GPIO 5  (with 220Ω resistor to GND)
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <SPIFFS.h>

// ============== PIN DEFINITIONS ==============
#define BUTTON_PIN 4
#define BUZZER_PIN 25
#define LED_BUILTIN_PIN 2    // Onboard LED for recording status

// Feedback LEDs
#define LED_GREEN_PIN 13     // On target
#define LED_RED_PIN 14       // Too fast
#define LED_YELLOW_PIN 5     // Too slow

// ============== BNO055 SENSOR ==============
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);
bool sensorReady = false;

// ============== DATA LOGGING ==============
#define LOG_FILE "/walk_data.csv"
#define SAMPLE_RATE_MS 10    // 100 Hz
File logFile;
bool isRecording = false;
unsigned long recordingStartTime = 0;
unsigned long sampleCount = 0;

// ============== PACE DETECTION ==============
float accelRMS = 0.0;
float rmsAlpha = 0.1;
float accelSquaredAvg = 0.0;

// Calibration values (adjust after analysis)
float targetRMS = 1.38;        // From analyze_walk_data.py
float fastThreshold = 1.25;    // +25% = too fast
float slowThreshold = 0.75;    // -25% = too slow

// Computed thresholds
float fastRMS;
float slowRMS;

// ============== FEEDBACK CONTROL ==============
bool enableAudioFeedback = true;   // Set false for silent operation
bool enableLEDFeedback = true;     // Visual feedback via LEDs
unsigned long lastFeedbackTime = 0;
#define FEEDBACK_INTERVAL_MS 500   // Min time between audio beeps

// ============== BUTTON HANDLING ==============
bool lastButtonState = HIGH;
unsigned long buttonPressStart = 0;
bool buttonHandled = false;
#define LONG_PRESS_MS 3000

// ============== LED BLINKING ==============
unsigned long lastBlinkTime = 0;
bool ledState = false;
#define BLINK_SLOW_MS 1000
#define BLINK_FAST_MS 200

// ============== TIMING ==============
unsigned long lastSampleTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("Natatometer - Data Logger + LED Feedback");
  Serial.println("========================================");
  Serial.println();
  
  // Initialize pins
  pinMode(LED_BUILTIN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Feedback LEDs
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  
  // Test LEDs at startup
  testLEDs();
  
  // Compute threshold values
  fastRMS = targetRMS * fastThreshold;
  slowRMS = targetRMS * slowThreshold;
  
  Serial.println("Pace thresholds:");
  Serial.print("  Target RMS: ");
  Serial.println(targetRMS);
  Serial.print("  FAST when RMS > ");
  Serial.println(fastRMS);
  Serial.print("  SLOW when RMS < ");
  Serial.println(slowRMS);
  Serial.println();
  
  // Initialize SPIFFS
  Serial.print("Initializing SPIFFS... ");
  if (!SPIFFS.begin(true)) {
    Serial.println("FAILED!");
    errorBeep();
    while (1) { blinkError(); }
  }
  Serial.println("OK");
  
  // Show available space
  Serial.print("SPIFFS Total: ");
  Serial.print(SPIFFS.totalBytes() / 1024);
  Serial.print(" KB, Used: ");
  Serial.print(SPIFFS.usedBytes() / 1024);
  Serial.println(" KB");
  
  // Check for existing data
  if (SPIFFS.exists(LOG_FILE)) {
    File f = SPIFFS.open(LOG_FILE, "r");
    Serial.print("Existing data file: ");
    Serial.print(f.size() / 1024.0, 1);
    Serial.println(" KB");
    f.close();
  }
  Serial.println();
  
  // Initialize BNO055
  Serial.print("Initializing BNO055... ");
  Wire.begin();
  Wire.setClock(100000);
  
  if (!bno.begin()) {
    Serial.println("NOT FOUND!");
    Serial.println("Check wiring. Continuing in SIMULATION mode.");
    Serial.println();
    sensorReady = false;
  } else {
    Serial.println("OK");
    bno.setExtCrystalUse(false);
    sensorReady = true;
    waitForCalibration();
  }
  
  // Ready indication
  beepReady();
  setFeedbackLED('G');  // Start with green
  
  Serial.println();
  Serial.println("Controls:");
  Serial.println("  SHORT press: Start/Stop recording");
  Serial.println("  LONG press (3s): Dump CSV to Serial");
  Serial.println();
  Serial.println("LED Indicators:");
  Serial.println("  GREEN  = On target");
  Serial.println("  RED    = Too fast");
  Serial.println("  YELLOW = Too slow");
  Serial.println();
  Serial.println("Ready!");
  Serial.println("========================================");
  Serial.println();
  
  lastSampleTime = millis();
}

void loop() {
  unsigned long currentTime = millis();
  
  // Handle button
  handleButton(currentTime);
  
  // Handle recording LED blinking
  handleRecordingLED(currentTime);
  
  // Sample and process data if recording
  if (isRecording && (currentTime - lastSampleTime >= SAMPLE_RATE_MS)) {
    lastSampleTime = currentTime;
    sampleAndProcess(currentTime);
  }
}

// ============== LED FEEDBACK ==============

void testLEDs() {
  Serial.println("Testing LEDs...");
  
  // Cycle through each LED
  digitalWrite(LED_GREEN_PIN, HIGH);
  delay(300);
  digitalWrite(LED_GREEN_PIN, LOW);
  
  digitalWrite(LED_YELLOW_PIN, HIGH);
  delay(300);
  digitalWrite(LED_YELLOW_PIN, LOW);
  
  digitalWrite(LED_RED_PIN, HIGH);
  delay(300);
  digitalWrite(LED_RED_PIN, LOW);
  
  Serial.println("LED test complete.");
}

void setFeedbackLED(char state) {
  // state: 'G' = green (on target), 'R' = red (fast), 'Y' = yellow (slow), 'O' = off
  
  if (!enableLEDFeedback) {
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, LOW);
    digitalWrite(LED_YELLOW_PIN, LOW);
    return;
  }
  
  switch (state) {
    case 'G':  // On target - Green
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_RED_PIN, LOW);
      digitalWrite(LED_YELLOW_PIN, LOW);
      break;
      
    case 'R':  // Too fast - Red
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_YELLOW_PIN, LOW);
      break;
      
    case 'Y':  // Too slow - Yellow
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_RED_PIN, LOW);
      digitalWrite(LED_YELLOW_PIN, HIGH);
      break;
      
    case 'O':  // Off
    default:
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_RED_PIN, LOW);
      digitalWrite(LED_YELLOW_PIN, LOW);
      break;
  }
}

// ============== DATA SAMPLING & FEEDBACK ==============

void sampleAndProcess(unsigned long currentTime) {
  float ax, ay, az, mag;
  
  if (sensorReady) {
    imu::Vector<3> accel = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    ax = accel.x();
    ay = accel.y();
    az = accel.z();
  } else {
    // Simulate walking data
    float t = currentTime / 1000.0;
    float amplitude = 2.0;
    ax = amplitude * sin(2 * PI * 1.0 * t) + randomNoise();
    ay = randomNoise() * 0.5;
    az = randomNoise() * 0.5;
  }
  
  // Calculate magnitude
  mag = sqrt(ax*ax + ay*ay + az*az);
  
  // Update RMS
  accelSquaredAvg = rmsAlpha * (mag * mag) + (1.0 - rmsAlpha) * accelSquaredAvg;
  accelRMS = sqrt(accelSquaredAvg);
  
  // Write to file
  unsigned long relativeTime = currentTime - recordingStartTime;
  logFile.print(relativeTime);
  logFile.print(",");
  logFile.print(ax, 3);
  logFile.print(",");
  logFile.print(ay, 3);
  logFile.print(",");
  logFile.print(az, 3);
  logFile.print(",");
  logFile.print(mag, 3);
  logFile.print(",");
  logFile.println(accelRMS, 3);
  
  sampleCount++;
  
  // Periodic flush
  if (sampleCount % 100 == 0) {
    logFile.flush();
    Serial.print(".");
    if (sampleCount % 1000 == 0) {
      Serial.print(" ");
      Serial.print(sampleCount / 100);
      Serial.println("s");
    }
  }
  
  // Provide feedback
  provideFeedback(currentTime);
}

void provideFeedback(unsigned long currentTime) {
  float ratio = accelRMS / targetRMS;
  
  // Update LED immediately (visual feedback is continuous)
  if (ratio > fastThreshold) {
    setFeedbackLED('R');  // Too fast
  } else if (ratio < slowThreshold) {
    setFeedbackLED('Y');  // Too slow
  } else {
    setFeedbackLED('G');  // On target
  }
  
  // Audio feedback with rate limiting
  if (enableAudioFeedback && (currentTime - lastFeedbackTime >= FEEDBACK_INTERVAL_MS)) {
    if (ratio > fastThreshold) {
      beepFast();
      lastFeedbackTime = currentTime;
      Serial.println(">>> FAST! RMS=" + String(accelRMS, 2));
    } else if (ratio < slowThreshold) {
      beepSlow();
      lastFeedbackTime = currentTime;
      Serial.println(">>> SLOW! RMS=" + String(accelRMS, 2));
    }
  }
}

float randomNoise() {
  return (random(-100, 100) / 100.0) * 0.3;
}

// ============== BUTTON HANDLING ==============

void handleButton(unsigned long currentTime) {
  bool buttonState = digitalRead(BUTTON_PIN);
  
  if (buttonState == LOW && lastButtonState == HIGH) {
    buttonPressStart = currentTime;
    buttonHandled = false;
  }
  
  if (buttonState == LOW && !buttonHandled) {
    if (currentTime - buttonPressStart >= LONG_PRESS_MS) {
      buttonHandled = true;
      dumpDataToSerial();
    }
  }
  
  if (buttonState == HIGH && lastButtonState == LOW) {
    if (!buttonHandled && (currentTime - buttonPressStart < LONG_PRESS_MS)) {
      toggleRecording();
    }
  }
  
  lastButtonState = buttonState;
}

void toggleRecording() {
  if (!isRecording) {
    startRecording();
  } else {
    stopRecording();
  }
}

void startRecording() {
  logFile = SPIFFS.open(LOG_FILE, "a");
  if (!logFile) {
    Serial.println("ERROR: Cannot open log file!");
    errorBeep();
    return;
  }
  
  if (logFile.size() == 0) {
    logFile.println("timestamp_ms,accel_x,accel_y,accel_z,accel_mag,rms");
  }
  
  logFile.println("# SESSION_START");
  
  isRecording = true;
  recordingStartTime = millis();
  sampleCount = 0;
  accelSquaredAvg = 0.0;
  
  Serial.println(">>> RECORDING STARTED");
  beepStartRecording();
}

void stopRecording() {
  if (logFile) {
    logFile.println("# SESSION_END");
    logFile.close();
  }
  
  isRecording = false;
  setFeedbackLED('O');  // Turn off feedback LEDs
  
  unsigned long duration = (millis() - recordingStartTime) / 1000;
  Serial.println(">>> RECORDING STOPPED");
  Serial.print("    Duration: ");
  Serial.print(duration);
  Serial.print("s, Samples: ");
  Serial.println(sampleCount);
  
  File f = SPIFFS.open(LOG_FILE, "r");
  Serial.print("    File size: ");
  Serial.print(f.size() / 1024.0, 1);
  Serial.println(" KB");
  f.close();
  
  beepStopRecording();
}

// ============== DATA DUMP ==============

void dumpDataToSerial() {
  if (isRecording) {
    Serial.println("Stop recording first!");
    errorBeep();
    return;
  }
  
  if (!SPIFFS.exists(LOG_FILE)) {
    Serial.println("No data file found!");
    errorBeep();
    return;
  }
  
  beepDumpStart();
  digitalWrite(LED_BUILTIN_PIN, HIGH);
  
  Serial.println();
  Serial.println("========== CSV DATA START ==========");
  
  File f = SPIFFS.open(LOG_FILE, "r");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();
  
  Serial.println("========== CSV DATA END ==========");
  Serial.println();
  Serial.println("Data dumped. To delete file and start fresh,");
  Serial.println("send 'DELETE' via Serial Monitor.");
  Serial.println();
  
  digitalWrite(LED_BUILTIN_PIN, LOW);
  
  // Wait for DELETE command
  unsigned long waitStart = millis();
  while (millis() - waitStart < 10000) {
    if (Serial.available()) {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      if (cmd == "DELETE") {
        SPIFFS.remove(LOG_FILE);
        Serial.println("File deleted!");
        beepReady();
        return;
      }
    }
    delay(100);
  }
}

// ============== RECORDING LED ==============

void handleRecordingLED(unsigned long currentTime) {
  int blinkRate = isRecording ? BLINK_FAST_MS : BLINK_SLOW_MS;
  
  if (currentTime - lastBlinkTime >= blinkRate) {
    lastBlinkTime = currentTime;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN_PIN, ledState);
  }
}

void blinkError() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_BUILTIN_PIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN_PIN, LOW);
    delay(100);
  }
  delay(500);
}

// ============== CALIBRATION ==============

void waitForCalibration() {
  uint8_t sys, gyro, accel, mag;
  int dots = 0;
  
  while (true) {
    bno.getCalibration(&sys, &gyro, &accel, &mag);
    
    Serial.print("  Gyro=");
    Serial.print(gyro);
    Serial.print(" Accel=");
    Serial.print(accel);
    
    if (gyro >= 2 && accel >= 1) {
      Serial.println(" - Good enough!");
      break;
    }
    
    Serial.println(" - Keep moving...");
    delay(500);
    
    if (++dots > 60) {
      Serial.println("  Calibration timeout - continuing anyway");
      break;
    }
  }
}

// ============== AUDIO FEEDBACK ==============

void beepReady() {
  tone(BUZZER_PIN, 1500, 100);
  delay(150);
  tone(BUZZER_PIN, 1500, 100);
  delay(150);
  noTone(BUZZER_PIN);
}

void beepStartRecording() {
  for (int freq = 800; freq <= 1600; freq += 200) {
    tone(BUZZER_PIN, freq, 80);
    delay(100);
  }
  noTone(BUZZER_PIN);
}

void beepStopRecording() {
  for (int freq = 1600; freq >= 800; freq -= 200) {
    tone(BUZZER_PIN, freq, 80);
    delay(100);
  }
  noTone(BUZZER_PIN);
}

void beepFast() {
  tone(BUZZER_PIN, 2000, 100);
  delay(100);
  noTone(BUZZER_PIN);
}

void beepSlow() {
  tone(BUZZER_PIN, 800, 200);
  delay(200);
  noTone(BUZZER_PIN);
}

void beepDumpStart() {
  tone(BUZZER_PIN, 1000, 150);
  delay(200);
  tone(BUZZER_PIN, 1000, 150);
  delay(200);
  noTone(BUZZER_PIN);
}

void errorBeep() {
  tone(BUZZER_PIN, 400, 500);
  delay(500);
  noTone(BUZZER_PIN);
}
