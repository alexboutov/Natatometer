/*
 * Natatometer - BNO055 Sensor Test
 * 
 * Tests BNO055 IMU communication and displays:
 *   - Calibration status
 *   - Linear acceleration (gravity removed)
 *   - Orientation (Euler angles)
 * 
 * Use this to verify wiring before running full Natatometer sketch.
 * 
 * Wiring:
 *   BNO055 VIN  -> ESP32 3.3V
 *   BNO055 GND  -> ESP32 GND
 *   BNO055 SDA  -> ESP32 GPIO 21
 *   BNO055 SCL  -> ESP32 GPIO 22
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

#define LED_PIN 2

// Try default address 0x28, change to 0x29 if not detected
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  Serial.println("========================================");
  Serial.println("Natatometer - BNO055 Sensor Test");
  Serial.println("========================================");
  Serial.println();
  
  // Initialize I2C with specific pins (ESP32)
  Wire.begin(21, 22);
  Wire.setClock(100000);  // 100kHz - slower for reliability
  
  Serial.println("Scanning for BNO055...");
  Serial.println("(If this hangs, check wiring: VIN->3.3V, GND->GND, SDA->21, SCL->22)");
  Serial.println();
  
  if (!bno.begin()) {
    Serial.println("========================================");
    Serial.println("ERROR: BNO055 not detected!");
    Serial.println("========================================");
    Serial.println();
    Serial.println("Troubleshooting:");
    Serial.println("1. Check wiring connections");
    Serial.println("2. Verify using 3.3V (not 5V)");
    Serial.println("3. Try adding 10k pull-up resistors on SDA/SCL");
    Serial.println("4. Try address 0x29 instead of 0x28");
    Serial.println();
    
    // Blink LED rapidly to indicate error
    while (1) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  Serial.println("========================================");
  Serial.println("BNO055 DETECTED! Sensor is working.");
  Serial.println("========================================");
  Serial.println();
  
  // Display sensor info
  sensor_t sensor;
  bno.getSensor(&sensor);
  Serial.print("Sensor:       "); Serial.println(sensor.name);
  Serial.print("Driver Ver:   "); Serial.println(sensor.version);
  Serial.print("Unique ID:    "); Serial.println(sensor.sensor_id);
  Serial.println();
  
  // Use external crystal for better accuracy (if available on board)
  bno.setExtCrystalUse(true);
  
  Serial.println("Starting sensor readings...");
  Serial.println("Move the sensor to see values change.");
  Serial.println("For best results, calibrate by moving in figure-8 pattern.");
  Serial.println();
  Serial.println("========================================");
  Serial.println();
  
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  // Get calibration status
  uint8_t sys, gyro, accel, mag;
  bno.getCalibration(&sys, &gyro, &accel, &mag);
  
  // Get linear acceleration (gravity removed - what we need for velocity)
  imu::Vector<3> linearAccel = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
  
  // Get orientation (Euler angles)
  imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  
  // Get raw acceleration (includes gravity)
  imu::Vector<3> accelRaw = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
  
  // Print calibration status
  Serial.print("CAL: Sys=");
  Serial.print(sys);
  Serial.print(" Gyro=");
  Serial.print(gyro);
  Serial.print(" Accel=");
  Serial.print(accel);
  Serial.print(" Mag=");
  Serial.print(mag);
  
  // Calibration indicator
  if (gyro >= 2 && accel >= 2) {
    Serial.print(" [OK]");
    digitalWrite(LED_PIN, HIGH);
  } else {
    Serial.print(" [Calibrating...]");
    digitalWrite(LED_PIN, LOW);
  }
  Serial.println();
  
  // Print linear acceleration (gravity removed)
  Serial.print("LINEAR ACCEL (m/s²): X=");
  Serial.print(linearAccel.x(), 2);
  Serial.print("  Y=");
  Serial.print(linearAccel.y(), 2);
  Serial.print("  Z=");
  Serial.print(linearAccel.z(), 2);
  Serial.println();
  
  // Print orientation
  Serial.print("ORIENTATION (deg):   Heading=");
  Serial.print(euler.x(), 1);
  Serial.print("  Roll=");
  Serial.print(euler.y(), 1);
  Serial.print("  Pitch=");
  Serial.print(euler.z(), 1);
  Serial.println();
  
  // Print magnitude of linear acceleration (useful for detecting movement)
  float accelMagnitude = sqrt(linearAccel.x()*linearAccel.x() + 
                               linearAccel.y()*linearAccel.y() + 
                               linearAccel.z()*linearAccel.z());
  Serial.print("ACCEL MAGNITUDE:     ");
  Serial.print(accelMagnitude, 2);
  Serial.print(" m/s²");
  if (accelMagnitude > 0.5) {
    Serial.print("  ** MOVEMENT DETECTED **");
  }
  Serial.println();
  
  Serial.println("----------------------------------------");
  
  delay(200);  // 5 Hz update rate for readability
}
