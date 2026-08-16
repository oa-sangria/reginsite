/* ============================================================================
   reginsite — TEST: Ultrasonic (HC-SR04) reader  (Arduino Mega 2560)
   ----------------------------------------------------------------------------
   Purpose: verify each per-slot sensor, map its ECHO pin, and find the
   distance that means "tool present" vs "slot empty" so I can set PRESENT_CM.

   Wiring model (matches the main firmware): ONE shared TRIG pin to all sensors,
   one ECHO pin per slot. Set TRIG_PIN once; change ECHO_PIN (in Serial Monitor)
   to test each sensor.

   Open Serial Monitor @115200, line ending "Newline":
     - it prints the distance from the current ECHO pin ~3x/sec
     - type a number  -> switch to that ECHO pin (e.g. 24)
     - type t=6.0     -> change the present/absent threshold to 6.0 cm
   Put a tool in the slot, then remove it, and note the two distances.
   ============================================================================ */

uint8_t TRIG_PIN = 0;     // <<< set your SHARED trigger pin here (e.g. 30)
uint8_t ECHO_PIN = 0;     // <<< set a slot's echo pin here to start (e.g. 31)
float   THRESHOLD = 8.0;  // <= this cm = "tool present"

float readCm(uint8_t trig, uint8_t echo) {
  if (!trig || !echo) return -1;
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 30000);
  return dur == 0 ? -1 : dur * 0.0343 / 2.0;
}

void applyPins() {
  if (TRIG_PIN) pinMode(TRIG_PIN, OUTPUT);
  if (ECHO_PIN) pinMode(ECHO_PIN, INPUT);
  Serial.print("TRIG="); Serial.print(TRIG_PIN);
  Serial.print("  ECHO="); Serial.print(ECHO_PIN);
  Serial.print("  threshold="); Serial.print(THRESHOLD); Serial.println(" cm");
}

void setup() {
  Serial.begin(115200);
  Serial.println("== Ultrasonic tester ==");
  Serial.println("Set TRIG_PIN/ECHO_PIN at top, or type an echo pin number / t=<cm>.");
  applyPins();
}

void loop() {
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n'); s.trim();
    if (s.startsWith("t=")) { THRESHOLD = s.substring(2).toFloat(); applyPins(); }
    else if (s.length()) { ECHO_PIN = s.toInt(); applyPins(); }
  }

  float d = readCm(TRIG_PIN, ECHO_PIN);
  if (d < 0) Serial.println("no echo (check TRIG/ECHO pins & 5V/GND)");
  else {
    Serial.print(d, 1); Serial.print(" cm  -> ");
    Serial.println(d <= THRESHOLD ? "TOOL PRESENT" : "slot EMPTY");
  }
  delay(300);
}
