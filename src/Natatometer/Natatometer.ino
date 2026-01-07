/*
 * Natatometer - Pedometer Prototype
 * 
 * Real-time velocity feedback using BNO055 IMU
 * 
 * Hardware:
 *   - ESP32-WROVER CAM
 *   - BNO055 9-DOF IMU (I2C)
 *   - Passive buzzer on GPIO 25
 * 
 * Algorithm:
 *   1. Read linear acceleration from BNO055 (gravity already removed)
 *   2. Integrate to get velocity
 *   3. Apply high-pass filter to remove drift
 *   4. Compare to target velocity -> audio feedback
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

// ============== PIN DEFINITIONS ==============
#define BUZZER_PIN 25          // Passive buzzer (PWM capable)
#define LED_PIN 2              // Onboard LED for status

// ============== IMU SETTINGS ==============
#define BNO055_SAMPLE_RATE_MS 10  // 100 Hz sampling
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);  // I2C address 0x28 (default)

// ============== VELOCITY TRACKING ==============
float velocity = 0.0;          // Current integrated velocity (m/s)
float velocityFiltered = 0.0;  // High-pass filtered velocity
float targetVelocity = 1.4;    // Target walking speed (m/s) - adjustable

// High-pass filter state
float hpFilterAlpha = 0.95;    // ~0.25 Hz cutoff at 100 Hz sample rate
float lastVelocity = 0.0;
float lastFilteredVelocity = 0.0;

// ============== FEEDBACK THRESHOLDS ==============
float fastThreshold = 0.1;     // +10% above target = FAST
float slowThreshold = -0.1;    // -10% below target = SLOW

// ============== TIMING ==============
unsigned long lastSampleTime = 0;
unsigned long lastFeedbackTime = 0;
#define FEEDBACK_INTERVAL_MS 500  // Min time between feedback beeps

// ============== STATE ==============
bool imuReady = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("Natatometer - Pedometer Prototype");
  Serial.println("========================================");
  
  // Initialize pins
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  // Startup indication
  digitalWrite(LED_PIN, HIGH);
  beepStartup();
  
  // Initialize I2C
  Wire.begin();
  
  // Initialize BNO055
  Serial.println("Initializing BNO055...");
  if (!bno.begin()) {
    Serial.println("ERROR: BNO055 not detected!");
    Serial.println("Check wiring: SDA->21, SCL->22, VIN->3.3V, GND->GND");
    imuReady = false;
    // Blink LED to indicate error
    while (1) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  Serial.println("BNO055 detected!");
  imuReady = true;
  
  // Use external crystal for better accuracy
  bno.setExtCrystalUse(true);
  
  // Wait for calibration
  Serial.println("Waiting for sensor calibration...");
  Serial.println("Move the sensor gently in figure-8 pattern");
  waitForCalibration();
  
  Serial.println("========================================");
  Serial.println("Setup complete! Starting velocity tracking...");
  Serial.println("========================================");
  Serial.println();
  Serial.println("Target velocity: " + String(targetVelocity) + " m/s");
  Serial.println();
  
  digitalWrite(LED_PIN, LOW);
  lastSampleTime = millis();
}

void loop() {
  if (!imuReady) return;
  
  unsigned long currentTime = millis();
  
  // Sample at fixed rate
  if (currentTime - lastSampleTime >= BNO055_SAMPLE_RATE_MS) {
    float dt = (currentTime - lastSampleTime) / 1000.0;  // Convert to seconds
    lastSampleTime = currentTime;
    
    // Get linear acceleration (gravity already removed by BNO055)
    imu::Vector<3> linearAccel = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    
    // Use forward axis (X in most mounting orientations)
    // Adjust based on how you mount the sensor
    float accelForward = linearAccel.x();
    
    // Integrate acceleration to get velocity
    velocity += accelForward * dt;
    
    // Apply high-pass filter to remove drift
    velocityFiltered = hpFilterAlpha * (lastFilteredVelocity + velocity - lastVelocity);
    lastVelocity = velocity;
    lastFilteredVelocity = velocityFiltered;
    
    // Calculate deviation from target
    float deviation = velocityFiltered - targetVelocity;
    float deviationPercent = deviation / targetVelocity;
    
    // Provide feedback
    if (currentTime - lastFeedbackTime >= FEEDBACK_INTERVAL_MS) {
      if (deviationPercent > fastThreshold) {
        beepFast();
        lastFeedbackTime = currentTime;
        Serial.println("FAST! v=" + String(velocityFiltered, 2) + " m/s");
      } else if (deviationPercent < slowThreshold) {
        beepSlow();
        lastFeedbackTime = currentTime;
        Serial.println("SLOW! v=" + String(velocityFiltered, 2) + " m/s");
      }
    }
    
    // Debug output (every 10 samples = 100ms)
    static int sampleCount = 0;
    if (++sampleCount >= 10) {
      sampleCount = 0;
      Serial.print("a=");
      Serial.print(accelForward, 2);
      Serial.print(" v_raw=");
      Serial.print(velocity, 2);
      Serial.print(" v_filt=");
      Serial.print(velocityFiltered, 2);
      Serial.print(" target=");
      Serial.print(targetVelocity, 2);
      Serial.println();
    }
  }
}

// ============== CALIBRATION ==============
void waitForCalibration() {
  uint8_t sys, gyro, accel, mag;
  
  while (true) {
    bno.getCalibration(&sys, &gyro, &accel, &mag);
    
    Serial.print("Calibration - Sys:");
    Serial.print(sys);
    Serial.print(" Gyro:");
    Serial.print(gyro);
    Serial.print(" Accel:");
    Serial.print(accel);
    Serial.print(" Mag:");
    Serial.println(mag);
    
    // Need at least gyro and accel calibrated for velocity tracking
    if (gyro >= 2 && accel >= 2) {
      Serial.println("Calibration sufficient!");
      beepReady();
      break;
    }
    
    delay(500);
  }
}

// ============== AUDIO FEEDBACK ==============
void beepStartup() {
  // Rising tone
  for (int freq = 500; freq <= 1500; freq += 100) {
    tone(BUZZER_PIN, freq, 50);
    delay(60);
  }
  noTone(BUZZER_PIN);
}

void beepReady() {
  // Two short high beeps
  tone(BUZZER_PIN, 1500, 100);
  delay(150);
  tone(BUZZER_PIN, 1500, 100);
  delay(150);
  noTone(BUZZER_PIN);
}

void beepFast() {
  // High pitch = too fast
  tone(BUZZER_PIN, 2000, 100);
  delay(100);
  noTone(BUZZER_PIN);
}

void beepSlow() {
  // Low pitch = too slow
  tone(BUZZER_PIN, 800, 200);
  delay(200);
  noTone(BUZZER_PIN);
}
