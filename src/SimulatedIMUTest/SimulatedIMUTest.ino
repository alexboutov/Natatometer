/*
 * Natatometer - Simulated IMU Test v2
 * 
 * Tests pace feedback algorithm using acceleration RMS as speed proxy.
 * (HP-filtered velocity doesn't work because it removes the DC component
 * which IS the average velocity we're trying to measure!)
 * 
 * Real-time approach: Faster walking = stronger push-offs = higher accel RMS
 * 
 * Wiring:
 *   Speaker + -> GPIO 25
 *   Speaker - -> GND
 */

#define BUZZER_PIN 25
#define LED_PIN 2

// ============== PACE DETECTION ==============
float accelRMS = 0.0;           // Rolling RMS of acceleration
float targetRMS = 2.0;          // Target RMS for normal pace (~2 m/s² for 1.4 m/s walk)

// Exponential moving average for RMS
float rmsAlpha = 0.1;           // Smoothing factor (lower = smoother)
float accelSquaredAvg = 0.0;    // Running average of accel²

// ============== FEEDBACK THRESHOLDS ==============
float fastThreshold = 1.15;     // 15% above target RMS = FAST
float slowThreshold = 0.85;     // 15% below target RMS = SLOW

// ============== TIMING ==============
unsigned long lastSampleTime = 0;
unsigned long lastFeedbackTime = 0;
unsigned long lastDebugTime = 0;
unsigned long simulationStartTime = 0;
#define SAMPLE_RATE_MS 10         // 100 Hz sampling
#define FEEDBACK_INTERVAL_MS 500  // Min time between feedback beeps
#define DEBUG_INTERVAL_MS 200     // Debug output rate

// ============== SIMULATION PARAMETERS ==============
float strideFrequency = 1.0;      // Hz (base)
float currentPace = 1.0;          // Multiplier: 1.0 = normal, 1.3 = fast, 0.7 = slow
String currentPhaseName = "NORMAL";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("Natatometer - SIMULATED IMU Test v2");
  Serial.println("========================================");
  Serial.println();
  Serial.println("Algorithm: Acceleration RMS as pace proxy");
  Serial.println("  - Faster walking = stronger push-offs");
  Serial.println("  - Higher accel RMS = faster pace");
  Serial.println();
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  // Startup indication
  digitalWrite(LED_PIN, HIGH);
  beepStartup();
  beepReady();
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("Target RMS: " + String(targetRMS) + " m/s^2");
  Serial.println("Fast threshold: >" + String(targetRMS * fastThreshold, 1) + " m/s^2");
  Serial.println("Slow threshold: <" + String(targetRMS * slowThreshold, 1) + " m/s^2");
  Serial.println();
  Serial.println("Simulation phases (10s each):");
  Serial.println("  0-10s:  NORMAL pace (should be quiet)");
  Serial.println("  10-20s: FAST pace (expect HIGH beeps)");
  Serial.println("  20-30s: SLOW pace (expect LOW beeps)");
  Serial.println("  30s+:   Repeats...");
  Serial.println();
  Serial.println("========================================");
  Serial.println();
  
  lastSampleTime = millis();
  simulationStartTime = millis();
}

void loop() {
  unsigned long currentTime = millis();
  
  // Sample at fixed rate
  if (currentTime - lastSampleTime >= SAMPLE_RATE_MS) {
    lastSampleTime = currentTime;
    
    // Update simulation pace based on time
    updateSimulationPace(currentTime);
    
    // Generate simulated acceleration
    float accel = generateSimulatedAccel(currentTime);
    
    // === CORE ALGORITHM: RMS-based pace detection ===
    
    // Update running average of acceleration squared
    accelSquaredAvg = rmsAlpha * (accel * accel) + (1.0 - rmsAlpha) * accelSquaredAvg;
    
    // Compute RMS
    accelRMS = sqrt(accelSquaredAvg);
    
    // Compare to target and provide feedback
    if (currentTime - lastFeedbackTime >= FEEDBACK_INTERVAL_MS) {
      float ratio = accelRMS / targetRMS;
      
      if (ratio > fastThreshold) {
        beepFast();
        lastFeedbackTime = currentTime;
        Serial.println(">>> FAST! RMS=" + String(accelRMS, 2) + " m/s^2 (+" + 
                       String((ratio - 1.0) * 100, 0) + "%)");
      } else if (ratio < slowThreshold) {
        beepSlow();
        lastFeedbackTime = currentTime;
        Serial.println(">>> SLOW! RMS=" + String(accelRMS, 2) + " m/s^2 (" + 
                       String((ratio - 1.0) * 100, 0) + "%)");
      }
    }
    
    // Debug output
    if (currentTime - lastDebugTime >= DEBUG_INTERVAL_MS) {
      lastDebugTime = currentTime;
      float elapsed = (currentTime - simulationStartTime) / 1000.0;
      Serial.print("t=");
      Serial.print(elapsed, 1);
      Serial.print("s  [");
      Serial.print(currentPhaseName);
      Serial.print("]  a=");
      Serial.print(accel, 2);
      Serial.print("  RMS=");
      Serial.print(accelRMS, 2);
      Serial.print("  ratio=");
      Serial.print(accelRMS / targetRMS, 2);
      Serial.println();
    }
  }
}

// ============== SIMULATION FUNCTIONS ==============

void updateSimulationPace(unsigned long currentTime) {
  // Cycle through pace changes every 10 seconds
  unsigned long elapsed = (currentTime - simulationStartTime) % 30000;
  
  if (elapsed < 10000) {
    currentPace = 1.0;
    currentPhaseName = "NORMAL";
  } else if (elapsed < 20000) {
    currentPace = 1.3;
    currentPhaseName = "FAST  ";
  } else {
    currentPace = 0.7;
    currentPhaseName = "SLOW  ";
  }
}

float generateSimulatedAccel(unsigned long currentTime) {
  float t = currentTime / 1000.0;  // Time in seconds
  
  // Walking acceleration pattern
  // Amplitude scales with pace (faster = harder push-off)
  float frequency = strideFrequency * currentPace;
  float amplitude = 2.0 * currentPace;  // Key: amplitude proportional to pace!
  
  // Main stride component
  float accel = amplitude * sin(2 * PI * frequency * t);
  
  // Add realistic noise
  float noise = (random(-100, 100) / 100.0) * 0.3;
  
  return accel + noise;
}

// ============== AUDIO FEEDBACK ==============

void beepStartup() {
  for (int freq = 500; freq <= 1500; freq += 100) {
    tone(BUZZER_PIN, freq, 50);
    delay(60);
  }
  noTone(BUZZER_PIN);
}

void beepReady() {
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
