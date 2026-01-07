/*
 * Natatometer - Buzzer Feedback Test
 * 
 * Tests audio feedback tones without IMU
 * Cycles through: Startup -> Ready -> simulated FAST/SLOW feedback
 * 
 * Wiring:
 *   Buzzer + (long leg) -> GPIO 25
 *   Buzzer - (short leg) -> GND
 */

#define BUZZER_PIN 25
#define LED_PIN 2

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("Natatometer - Buzzer Feedback Test");
  Serial.println("========================================");
  Serial.println();
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Test 1: Startup tone
  Serial.println("1. Playing STARTUP tone (rising pitch)...");
  digitalWrite(LED_PIN, HIGH);
  beepStartup();
  digitalWrite(LED_PIN, LOW);
  delay(1000);
  
  // Test 2: Ready tone
  Serial.println("2. Playing READY tone (two high beeps)...");
  digitalWrite(LED_PIN, HIGH);
  beepReady();
  digitalWrite(LED_PIN, LOW);
  delay(1000);
  
  Serial.println();
  Serial.println("3. Starting FAST/SLOW feedback simulation...");
  Serial.println("   FAST = high pitch (2000 Hz) - you're going too fast");
  Serial.println("   SLOW = low pitch (800 Hz) - you're going too slow");
  Serial.println();
}

void loop() {
  // Simulate alternating FAST and SLOW feedback
  // In real use, this would be triggered by velocity deviation
  
  static unsigned long lastBeepTime = 0;
  static bool isFast = true;
  
  if (millis() - lastBeepTime > 2000) {  // Every 2 seconds
    lastBeepTime = millis();
    
    if (isFast) {
      Serial.println(">> FAST! (high pitch) - Slow down!");
      digitalWrite(LED_PIN, HIGH);
      beepFast();
      digitalWrite(LED_PIN, LOW);
    } else {
      Serial.println(">> SLOW! (low pitch) - Speed up!");
      digitalWrite(LED_PIN, HIGH);
      beepSlow();
      digitalWrite(LED_PIN, LOW);
    }
    
    isFast = !isFast;  // Alternate
  }
}

// ============== AUDIO FEEDBACK FUNCTIONS ==============

void beepStartup() {
  // Rising tone - indicates device is starting
  for (int freq = 500; freq <= 1500; freq += 100) {
    tone(BUZZER_PIN, freq, 50);
    delay(60);
  }
  noTone(BUZZER_PIN);
}

void beepReady() {
  // Two short high beeps - indicates ready to use
  tone(BUZZER_PIN, 1500, 100);
  delay(150);
  tone(BUZZER_PIN, 1500, 100);
  delay(150);
  noTone(BUZZER_PIN);
}

void beepFast() {
  // High pitch = too fast, need to slow down
  tone(BUZZER_PIN, 2000, 100);
  delay(100);
  noTone(BUZZER_PIN);
}

void beepSlow() {
  // Low pitch = too slow, need to speed up
  tone(BUZZER_PIN, 800, 200);
  delay(200);
  noTone(BUZZER_PIN);
}

// ============== OPTIONAL: TEST ALL FREQUENCIES ==============
// Uncomment this function and call it from setup() to hear frequency range

/*
void testFrequencyRange() {
  Serial.println("Testing frequency range 200-3000 Hz...");
  for (int freq = 200; freq <= 3000; freq += 200) {
    Serial.print("Frequency: ");
    Serial.print(freq);
    Serial.println(" Hz");
    tone(BUZZER_PIN, freq, 200);
    delay(300);
  }
  noTone(BUZZER_PIN);
  Serial.println("Frequency test complete.");
}
*/
