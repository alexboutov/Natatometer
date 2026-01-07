/*
 * Natatometer - Simulated IMU Test
 * 
 * Tests the velocity integration and feedback algorithm
 * using simulated accelerometer data (no hardware needed).
 * 
 * Simulates a walking pattern:
 *   - Acceleration pulses mimicking footsteps
 *   - Varying pace (fast/normal/slow)
 * 
 * Wiring:
 *   Speaker + -> GPIO 25
 *   Speaker - -> GND
 */

#define BUZZER_PIN 25
#define LED_PIN 2

// ============== VELOCITY TRACKING ==============
float velocity = 0.0;          // Current integrated velocity (m/s)
float velocityFiltered = 0.0;  // High-pass filtered velocity
float targetVelocity = 1.4;    // Target walking speed (m/s)

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
unsigned long simulationStartTime = 0;
#define SAMPLE_RATE_MS 10         // 100 Hz sampling
#define FEEDBACK_INTERVAL_MS 500  // Min time between feedback beeps

// ============== SIMULATION PARAMETERS ==============
// Walking cadence: ~2 steps/second = 1 Hz stride frequency
float strideFrequency = 1.0;      // Hz
float currentPace = 1.0;          // Multiplier: 1.0 = normal, 1.3 = fast, 0.7 = slow

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("Natatometer - SIMULATED IMU Test");
  Serial.println("========================================");
  Serial.println();
  Serial.println("This sketch simulates walking acceleration data");
  Serial.println("to test the velocity algorithm without hardware.");
  Serial.println();
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  // Startup indication
  digitalWrite(LED_PIN, HIGH);
  beepStartup();
  beepReady();
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("Target velocity: " + String(targetVelocity) + " m/s");
  Serial.println();
  Serial.println("Simulation phases:");
  Serial.println("  0-10s:  Normal pace (should be quiet)");
  Serial.println("  10-20s: Fast pace (expect HIGH beeps)");
  Serial.println("  20-30s: Slow pace (expect LOW beeps)");
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
    float dt = (currentTime - lastSampleTime) / 1000.0;
    lastSampleTime = currentTime;
    
    // Update simulation pace based on time
    updateSimulationPace(currentTime);
    
    // Generate simulated acceleration
    float accelForward = generateSimulatedAccel(currentTime);
    
    // === CORE ALGORITHM (same as real Natatometer.ino) ===
    
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
        Serial.println(">>> FAST! v=" + String(velocityFiltered, 2) + " m/s (+" + 
                       String(deviationPercent * 100, 0) + "%)");
      } else if (deviationPercent < slowThreshold) {
        beepSlow();
        lastFeedbackTime = currentTime;
        Serial.println(">>> SLOW! v=" + String(velocityFiltered, 2) + " m/s (" + 
                       String(deviationPercent * 100, 0) + "%)");
      }
    }
    
    // Debug output (every 20 samples = 200ms)
    static int sampleCount = 0;
    if (++sampleCount >= 20) {
      sampleCount = 0;
      Serial.print("t=");
      Serial.print((currentTime - simulationStartTime) / 1000.0, 1);
      Serial.print("s  pace=");
      Serial.print(currentPace, 1);
      Serial.print("x  a=");
      Serial.print(accelForward, 2);
      Serial.print("  v_raw=");
      Serial.print(velocity, 2);
      Serial.print("  v_filt=");
      Serial.print(velocityFiltered, 2);
      Serial.println();
    }
  }
}

// ============== SIMULATION FUNCTIONS ==============

void updateSimulationPace(unsigned long currentTime) {
  // Cycle through pace changes every 10 seconds
  unsigned long elapsed = (currentTime - simulationStartTime) % 30000;
  
  if (elapsed < 10000) {
    // Normal pace
    currentPace = 1.0;
  } else if (elapsed < 20000) {
    // Fast pace
    currentPace = 1.3;
  } else {
    // Slow pace
    currentPace = 0.7;
  }
}

float generateSimulatedAccel(unsigned long currentTime) {
  // Simulate walking acceleration pattern
  // Walking produces roughly sinusoidal acceleration in forward direction
  // with peaks during push-off phase
  
  float t = currentTime / 1000.0;  // Time in seconds
  
  // Base acceleration pattern: sine wave at stride frequency
  // Amplitude scaled by pace (faster = harder push-off)
  float frequency = strideFrequency * currentPace;
  float amplitude = 2.0 * currentPace;  // ~2 m/s² peak at normal pace
  
  // Main stride component
  float accel = amplitude * sin(2 * PI * frequency * t);
  
  // Add some noise (simulates real sensor noise)
  float noise = (random(-100, 100) / 100.0) * 0.3;  // ±0.3 m/s² noise
  
  // Add slight bias drift (simulates real IMU drift)
  float drift = 0.01 * sin(0.05 * t);  // Very slow drift
  
  return accel + noise + drift;
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
  tone(BUZZER_PIN, 2000, 100);
  delay(100);
  noTone(BUZZER_PIN);
}

void beepSlow() {
  tone(BUZZER_PIN, 800, 200);
  delay(200);
  noTone(BUZZER_PIN);
}
