/*
 * Natatometer - Data Logger for Calibration
 * 
 * Logs BNO055 accelerometer data to ESP32 internal flash (SPIFFS)
 * for later analysis and calibration.
 * 
 * Controls:
 *   SHORT press: Start/Stop recording
 *   LONG press (3s): Dump data to Serial as CSV
 *   
 * LED Indicators:
 *   Slow blink: Idle, ready to record
 *   Fast blink: Recording
 *   Solid: Dumping data
 *   
 * Audio Feedback:
 *   Rising tone: Recording started
 *   Falling tone: Recording stopped
 *   Two beeps: Data dump starting
 * 
 * Wiring:
 *   BNO055 VIN -> 3.3V
 *   BNO055 GND -> GND
 *   BNO055 SDA -> GPIO 21
 *   BNO055 SCL -> GPIO 22
 *   Button     -> GPIO 4 (other leg to GND)
 *   Speaker +  -> GPIO 25
 *   Speaker -  -> GND
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <SPIFFS.h>

// ============== PIN DEFINITIONS ==============
#define BUTTON_PIN 4      // Tactile button (internal pullup used)
#define BUZZER_PIN 25
#define LED_PIN 2

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

// RMS calculation
float rmsAlpha = 0.1;
float accelSquaredAvg = 0.0;

// ============== BUTTON HANDLING ==============
bool lastButtonState = HIGH;  // Pullup = HIGH when not pressed
unsigned long buttonPressStart = 0;
bool buttonHandled = false;
#define LONG_PRESS_MS 3000    // 3 seconds for long press

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
  Serial.println("Natatometer - Data Logger");
  Serial.println("========================================");
  Serial.println();
  
  // Initialize pins
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Internal pullup, button connects to GND
  
  // Initialize SPIFFS
  Serial.print("Initializing SPIFFS... ");
  if (!SPIFFS.begin(true)) {  // true = format if failed
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
  Wire.setClock(100000);  // 100 kHz for reliability
  
  if (!bno.begin()) {
    Serial.println("NOT FOUND!");
    Serial.println("Check wiring. Continuing in SIMULATION mode.");
    Serial.println();
    sensorReady = false;
  } else {
    Serial.println("OK");
    bno.setExtCrystalUse(false);  // Use internal oscillator for clones
    sensorReady = true;
    
    // Wait for calibration
    Serial.println("Calibrating... move sensor gently");
    waitForCalibration();
  }
  
  // Ready indication
  beepReady();
  
  Serial.println();
  Serial.println("Controls:");
  Serial.println("  SHORT press: Start/Stop recording");
  Serial.println("  LONG press (3s): Dump CSV to Serial");
  Serial.println();
  Serial.println("Ready. LED blinking slowly = idle");
  Serial.println("========================================");
  Serial.println();
}

void loop() {
  unsigned long currentTime = millis();
  
  // Handle button
  handleButton(currentTime);
  
  // Handle LED blinking
  handleLED(currentTime);
  
  // Sample and log data if recording
  if (isRecording && (currentTime - lastSampleTime >= SAMPLE_RATE_MS)) {
    lastSampleTime = currentTime;
    sampleData(currentTime);
  }
}

// ============== BUTTON HANDLING ==============

void handleButton(unsigned long currentTime) {
  bool buttonState = digitalRead(BUTTON_PIN);
  
  // Button just pressed
  if (buttonState == LOW && lastButtonState == HIGH) {
    buttonPressStart = currentTime;
    buttonHandled = false;
  }
  
  // Button held - check for long press
  if (buttonState == LOW && !buttonHandled) {
    if (currentTime - buttonPressStart >= LONG_PRESS_MS) {
      // Long press - dump data
      buttonHandled = true;
      dumpDataToSerial();
    }
  }
  
  // Button released - check for short press
  if (buttonState == HIGH && lastButtonState == LOW) {
    if (!buttonHandled && (currentTime - buttonPressStart < LONG_PRESS_MS)) {
      // Short press - toggle recording
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
  // Open file for append
  logFile = SPIFFS.open(LOG_FILE, "a");
  if (!logFile) {
    Serial.println("ERROR: Cannot open log file!");
    errorBeep();
    return;
  }
  
  // Write header if file is empty/new
  if (logFile.size() == 0) {
    logFile.println("timestamp_ms,accel_x,accel_y,accel_z,accel_mag,rms");
  }
  
  // Mark session start
  logFile.println("# SESSION_START");
  
  isRecording = true;
  recordingStartTime = millis();
  sampleCount = 0;
  accelSquaredAvg = 0.0;  // Reset RMS
  
  Serial.println(">>> RECORDING STARTED");
  Serial.println("    Walk at your target pace...");
  
  beepStartRecording();
}

void stopRecording() {
  if (logFile) {
    logFile.println("# SESSION_END");
    logFile.close();
  }
  
  isRecording = false;
  
  unsigned long duration = (millis() - recordingStartTime) / 1000;
  Serial.println(">>> RECORDING STOPPED");
  Serial.print("    Duration: ");
  Serial.print(duration);
  Serial.print("s, Samples: ");
  Serial.println(sampleCount);
  
  // Show file size
  File f = SPIFFS.open(LOG_FILE, "r");
  Serial.print("    File size: ");
  Serial.print(f.size() / 1024.0, 1);
  Serial.println(" KB");
  f.close();
  
  beepStopRecording();
}

// ============== DATA SAMPLING ==============

void sampleData(unsigned long currentTime) {
  float ax, ay, az, mag, rms;
  
  if (sensorReady) {
    // Read from real BNO055
    imu::Vector<3> accel = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    ax = accel.x();
    ay = accel.y();
    az = accel.z();
  } else {
    // Simulate walking data for testing without sensor
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
  rms = sqrt(accelSquaredAvg);
  
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
  logFile.println(rms, 3);
  
  sampleCount++;
  
  // Periodic flush to prevent data loss
  if (sampleCount % 100 == 0) {
    logFile.flush();
    
    // Progress indicator
    Serial.print(".");
    if (sampleCount % 1000 == 0) {
      Serial.print(" ");
      Serial.print(sampleCount / 100);
      Serial.println("s");
    }
  }
}

float randomNoise() {
  return (random(-100, 100) / 100.0) * 0.3;
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
  digitalWrite(LED_PIN, HIGH);  // Solid LED during dump
  
  Serial.println();
  Serial.println("========== CSV DATA START ==========");
  
  File f = SPIFFS.open(LOG_FILE, "r");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();
  
  Serial.println("========== CSV DATA END ==========");
  Serial.println();
  
  // Offer to delete
  Serial.println("Data dumped. To delete file and start fresh,");
  Serial.println("send 'DELETE' via Serial Monitor.");
  Serial.println();
  
  digitalWrite(LED_PIN, LOW);
  
  // Wait for DELETE command (with timeout)
  unsigned long waitStart = millis();
  while (millis() - waitStart < 10000) {  // 10 second window
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

// ============== LED HANDLING ==============

void handleLED(unsigned long currentTime) {
  int blinkRate = isRecording ? BLINK_FAST_MS : BLINK_SLOW_MS;
  
  if (currentTime - lastBlinkTime >= blinkRate) {
    lastBlinkTime = currentTime;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
}

void blinkError() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
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
    
    // Need at least gyro=2 and accel=1 for reasonable data
    if (gyro >= 2 && accel >= 1) {
      Serial.println(" - Good enough!");
      break;
    }
    
    Serial.println(" - Keep moving...");
    delay(500);
    
    // Timeout after 30 seconds
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
  // Rising tone = starting
  for (int freq = 800; freq <= 1600; freq += 200) {
    tone(BUZZER_PIN, freq, 80);
    delay(100);
  }
  noTone(BUZZER_PIN);
}

void beepStopRecording() {
  // Falling tone = stopping
  for (int freq = 1600; freq >= 800; freq -= 200) {
    tone(BUZZER_PIN, freq, 80);
    delay(100);
  }
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
