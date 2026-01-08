/*
 * BNO055Logger - Comprehensive IMU Data Logger
 * 
 * Captures ALL data types from BNO055 9-axis sensor:
 *   - Linear Acceleration (gravity removed)
 *   - Raw Accelerometer (includes gravity)
 *   - Gyroscope (rotation rate)
 *   - Magnetometer (compass)
 *   - Euler Angles (orientation)
 *   - Quaternion (orientation, no gimbal lock)
 *   - Gravity Vector
 *   - Calibration Status
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
 * 
 * CSV Format (27 columns):
 *   timestamp_ms - Time since recording started
 *   lin_x/y/z    - Linear acceleration, gravity removed (m/s²)
 *   acc_x/y/z    - Raw accelerometer, includes gravity (m/s²)
 *   gyr_x/y/z    - Gyroscope rotation rate (°/s)
 *   mag_x/y/z    - Magnetometer (µT)
 *   eul_h/r/p    - Euler angles: heading, roll, pitch (°)
 *   quat_w/x/y/z - Quaternion orientation
 *   grav_x/y/z   - Gravity vector (m/s²)
 *   cal_sys/gyr/acc/mag - Calibration status (0-3, 3=fully calibrated)
 *   rms          - Running RMS of linear acceleration magnitude
 * 
 * Storage Note:
 *   At 100Hz, each row is ~200 bytes → ~20 KB/s → ~1.2 MB/min
 *   SPIFFS has ~1.3 MB total. Keep sessions under 1 minute or
 *   dump data frequently.
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <SPIFFS.h>

// ============== PIN DEFINITIONS ==============
#define BUTTON_PIN 4
#define BUZZER_PIN 25
#define LED_BUILTIN_PIN 2    // Onboard LED for recording status

// Feedback LEDs (WROVER-safe pins)
#define LED_GREEN_PIN 13     // On target
#define LED_RED_PIN 14       // Too fast
#define LED_YELLOW_PIN 5     // Too slow

// ============== BNO055 SENSOR ==============
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);
bool sensorReady = false;

// ============== DATA LOGGING ==============
#define LOG_FILE "/bno055_data.csv"
#define SAMPLE_RATE_MS 10    // 100 Hz
File logFile;
bool isRecording = false;
unsigned long recordingStartTime = 0;
unsigned long sampleCount = 0;

// ============== PACE DETECTION (using linear accel magnitude) ==============
float accelRMS = 0.0;
float rmsAlpha = 0.1;
float accelSquaredAvg = 0.0;

// Calibration values (adjust after analysis with real swim data)
float targetRMS = 1.38;        // Placeholder - calibrate with real data
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

// ============== CSV HEADER ==============
const char* CSV_HEADER = "timestamp_ms,lin_x,lin_y,lin_z,acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z,mag_x,mag_y,mag_z,eul_h,eul_r,eul_p,quat_w,quat_x,quat_y,quat_z,grav_x,grav_y,grav_z,cal_sys,cal_gyr,cal_acc,cal_mag,rms";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("BNO055Logger - Comprehensive IMU Logger");
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
  
  Serial.println("Pace thresholds (placeholder values):");
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
  Serial.print(" KB, Free: ");
  Serial.print((SPIFFS.totalBytes() - SPIFFS.usedBytes()) / 1024);
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
    Serial.println("Check wiring:");
    Serial.println("  VIN -> 3.3V");
    Serial.println("  GND -> GND");
    Serial.println("  SDA -> GPIO 21");
    Serial.println("  SCL -> GPIO 22");
    Serial.println();
    Serial.println("Continuing in SIMULATION mode (limited data).");
    Serial.println();
    sensorReady = false;
  } else {
    Serial.println("OK");
    bno.setExtCrystalUse(false);
    sensorReady = true;
    
    // Display sensor info
    displaySensorDetails();
    
    // Wait for calibration
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
  Serial.println("Data captured (27 columns):");
  Serial.println("  Linear Accel, Raw Accel, Gyro, Mag,");
  Serial.println("  Euler, Quaternion, Gravity, Calibration, RMS");
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
  
  // Refresh currentTime before sampling (fixes underflow after beep)
  currentTime = millis();
  
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

// ============== SENSOR INFO ==============

void displaySensorDetails() {
  sensor_t sensor;
  bno.getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.print("Sensor:       "); Serial.println(sensor.name);
  Serial.print("Driver Ver:   "); Serial.println(sensor.version);
  Serial.print("Unique ID:    "); Serial.println(sensor.sensor_id);
  Serial.print("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" m/s²");
  Serial.print("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" m/s²");
  Serial.print("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" m/s²");
  Serial.println("------------------------------------");
}

// ============== DATA SAMPLING ==============

void sampleAndProcess(unsigned long currentTime) {
  // Data variables
  float lin_x, lin_y, lin_z;       // Linear acceleration
  float acc_x, acc_y, acc_z;       // Raw accelerometer
  float gyr_x, gyr_y, gyr_z;       // Gyroscope
  float mag_x, mag_y, mag_z;       // Magnetometer
  float eul_h, eul_r, eul_p;       // Euler angles
  float quat_w, quat_x, quat_y, quat_z;  // Quaternion
  float grav_x, grav_y, grav_z;    // Gravity vector
  uint8_t cal_sys, cal_gyr, cal_acc, cal_mag;  // Calibration
  
  if (sensorReady) {
    // Get all sensor data
    imu::Vector<3> linearAccel = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    imu::Vector<3> accel = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
    imu::Vector<3> gyro = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
    imu::Vector<3> magnetometer = bno.getVector(Adafruit_BNO055::VECTOR_MAGNETOMETER);
    imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
    imu::Quaternion quat = bno.getQuat();
    imu::Vector<3> gravity = bno.getVector(Adafruit_BNO055::VECTOR_GRAVITY);
    
    // Linear Acceleration (m/s², gravity removed)
    lin_x = linearAccel.x();
    lin_y = linearAccel.y();
    lin_z = linearAccel.z();
    
    // Raw Accelerometer (m/s², includes gravity)
    acc_x = accel.x();
    acc_y = accel.y();
    acc_z = accel.z();
    
    // Gyroscope (°/s)
    gyr_x = gyro.x();
    gyr_y = gyro.y();
    gyr_z = gyro.z();
    
    // Magnetometer (µT)
    mag_x = magnetometer.x();
    mag_y = magnetometer.y();
    mag_z = magnetometer.z();
    
    // Euler Angles (°)
    eul_h = euler.x();  // Heading (yaw)
    eul_r = euler.y();  // Roll
    eul_p = euler.z();  // Pitch
    
    // Quaternion (unitless)
    quat_w = quat.w();
    quat_x = quat.x();
    quat_y = quat.y();
    quat_z = quat.z();
    
    // Gravity Vector (m/s²)
    grav_x = gravity.x();
    grav_y = gravity.y();
    grav_z = gravity.z();
    
    // Calibration Status (0-3 each, 3 = fully calibrated)
    bno.getCalibration(&cal_sys, &cal_gyr, &cal_acc, &cal_mag);
    
  } else {
    // Simulation mode - generate fake walking data
    float t = currentTime / 1000.0;
    float amplitude = 2.0;
    
    // Simulated linear acceleration (walking pattern)
    lin_x = amplitude * sin(2 * PI * 1.0 * t) + randomNoise();
    lin_y = randomNoise() * 0.5;
    lin_z = randomNoise() * 0.5;
    
    // Simulated raw accel (add gravity on Z)
    acc_x = lin_x;
    acc_y = lin_y;
    acc_z = lin_z + 9.81;
    
    // Simulated gyroscope
    gyr_x = randomNoise() * 10;
    gyr_y = randomNoise() * 10;
    gyr_z = randomNoise() * 10;
    
    // Simulated magnetometer
    mag_x = 25.0 + randomNoise() * 2;
    mag_y = -5.0 + randomNoise() * 2;
    mag_z = -40.0 + randomNoise() * 2;
    
    // Simulated Euler (relatively stable)
    eul_h = 45.0 + randomNoise() * 2;
    eul_r = randomNoise() * 5;
    eul_p = randomNoise() * 5;
    
    // Simulated Quaternion (roughly identity with small variation)
    quat_w = 0.92 + randomNoise() * 0.01;
    quat_x = randomNoise() * 0.05;
    quat_y = randomNoise() * 0.05;
    quat_z = 0.38 + randomNoise() * 0.01;
    
    // Simulated gravity (mostly on Z)
    grav_x = randomNoise() * 0.2;
    grav_y = randomNoise() * 0.2;
    grav_z = 9.81;
    
    // Simulated calibration (fully calibrated)
    cal_sys = 3;
    cal_gyr = 3;
    cal_acc = 3;
    cal_mag = 3;
  }
  
  // Calculate linear acceleration magnitude
  float lin_mag = sqrt(lin_x*lin_x + lin_y*lin_y + lin_z*lin_z);
  
  // Update RMS (for feedback)
  accelSquaredAvg = rmsAlpha * (lin_mag * lin_mag) + (1.0 - rmsAlpha) * accelSquaredAvg;
  accelRMS = sqrt(accelSquaredAvg);
  
  // Write to file - relative timestamp
  unsigned long relativeTime = currentTime - recordingStartTime;
  
  // Write all data as CSV row
  logFile.print(relativeTime);
  logFile.print(",");
  
  // Linear acceleration
  logFile.print(lin_x, 3); logFile.print(",");
  logFile.print(lin_y, 3); logFile.print(",");
  logFile.print(lin_z, 3); logFile.print(",");
  
  // Raw accelerometer
  logFile.print(acc_x, 3); logFile.print(",");
  logFile.print(acc_y, 3); logFile.print(",");
  logFile.print(acc_z, 3); logFile.print(",");
  
  // Gyroscope
  logFile.print(gyr_x, 3); logFile.print(",");
  logFile.print(gyr_y, 3); logFile.print(",");
  logFile.print(gyr_z, 3); logFile.print(",");
  
  // Magnetometer
  logFile.print(mag_x, 3); logFile.print(",");
  logFile.print(mag_y, 3); logFile.print(",");
  logFile.print(mag_z, 3); logFile.print(",");
  
  // Euler angles
  logFile.print(eul_h, 3); logFile.print(",");
  logFile.print(eul_r, 3); logFile.print(",");
  logFile.print(eul_p, 3); logFile.print(",");
  
  // Quaternion
  logFile.print(quat_w, 4); logFile.print(",");
  logFile.print(quat_x, 4); logFile.print(",");
  logFile.print(quat_y, 4); logFile.print(",");
  logFile.print(quat_z, 4); logFile.print(",");
  
  // Gravity vector
  logFile.print(grav_x, 3); logFile.print(",");
  logFile.print(grav_y, 3); logFile.print(",");
  logFile.print(grav_z, 3); logFile.print(",");
  
  // Calibration status
  logFile.print(cal_sys); logFile.print(",");
  logFile.print(cal_gyr); logFile.print(",");
  logFile.print(cal_acc); logFile.print(",");
  logFile.print(cal_mag); logFile.print(",");
  
  // RMS
  logFile.println(accelRMS, 3);
  
  sampleCount++;
  
  // Periodic flush (every 50 samples = 0.5s at 100Hz)
  if (sampleCount % 50 == 0) {
    logFile.flush();
    Serial.print(".");
    if (sampleCount % 500 == 0) {
      Serial.print(" ");
      Serial.print(sampleCount / 100);
      Serial.print("s (");
      Serial.print(logFile.size() / 1024.0, 1);
      Serial.println(" KB)");
    }
  }
  
  // Provide feedback (based on linear accel RMS)
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
  // Check available space
  unsigned long freeSpace = SPIFFS.totalBytes() - SPIFFS.usedBytes();
  if (freeSpace < 50000) {  // Less than 50KB free
    Serial.println("WARNING: Low storage space!");
    Serial.print("Free: ");
    Serial.print(freeSpace / 1024);
    Serial.println(" KB");
    Serial.println("Consider dumping and deleting old data.");
  }
  
  // Open file (this takes time due to flash operations)
  logFile = SPIFFS.open(LOG_FILE, "a");
  if (!logFile) {
    Serial.println("ERROR: Cannot open log file!");
    errorBeep();
    return;
  }
  
  // Write header if new file
  if (logFile.size() == 0) {
    logFile.println(CSV_HEADER);
  }
  
  // Write session marker
  logFile.println("# SESSION_START");
  
  // Reset counters (but NOT timestamps yet!)
  sampleCount = 0;
  accelSquaredAvg = 0.0;
  
  // Print message and play beep (these are blocking operations)
  Serial.println(">>> RECORDING STARTED");
  Serial.println("    Capturing: Linear Accel, Raw Accel, Gyro, Mag,");
  Serial.println("               Euler, Quaternion, Gravity, Calibration");
  beepStartRecording();
  
  // SET TIMESTAMPS LAST - after ALL blocking operations complete
  unsigned long now = millis();
  recordingStartTime = now;
  lastSampleTime = now;
  
  // Enable recording as the very last step
  isRecording = true;
}

void stopRecording() {
  // Disable recording first
  isRecording = false;
  
  if (logFile) {
    logFile.println("# SESSION_END");
    logFile.close();
  }
  
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
  
  // Show storage warning if needed
  unsigned long freeSpace = SPIFFS.totalBytes() - SPIFFS.usedBytes();
  if (freeSpace < 200000) {
    Serial.print("    WARNING: Only ");
    Serial.print(freeSpace / 1024);
    Serial.println(" KB free");
  }
  
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
  
  Serial.println("Calibrating BNO055...");
  Serial.println("  Move sensor in figure-8 pattern for magnetometer");
  Serial.println("  Hold still for accelerometer");
  
  while (true) {
    bno.getCalibration(&sys, &gyro, &accel, &mag);
    
    Serial.print("  Sys=");
    Serial.print(sys);
    Serial.print(" Gyro=");
    Serial.print(gyro);
    Serial.print(" Accel=");
    Serial.print(accel);
    Serial.print(" Mag=");
    Serial.print(mag);
    
    // Good enough for basic operation
    if (gyro >= 2 && accel >= 1) {
      Serial.println(" - Good enough!");
      break;
    }
    
    Serial.println(" - Keep calibrating...");
    delay(500);
    
    if (++dots > 60) {
      Serial.println("  Calibration timeout - continuing anyway");
      Serial.println("  (Magnetometer may need more calibration)");
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
