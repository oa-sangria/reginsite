/* ============================================================================
   reginsite — TEST: Ultrasonic (HC-SR04) reader  (Arduino Mega 2560)
   ----------------------------------------------------------------------------
   Purpose: verify each per-slot sensor against the wiring table, and find the
   distances that mean "tool present" vs "slot empty" so PRESENT_CM / ABSENT_CM
   can be set in locker_controller.ino.

   Wiring model: EVERY sensor has its OWN TRIG and its OWN ECHO (the shared-TRIG
   design is gone — splitting the build across two Megas made per-sensor TRIG
   affordable, and firing sensors one at a time is what avoids cross-talk).

   Open Serial Monitor @115200, line ending "Newline":
     22/23        -> test the sensor on TRIG=22, ECHO=23
     a0/a1        -> analog pins work too (A0..A15 == D54..D69 on the Mega)
     cab1         -> walk all 4 slots of cabinet 1, one at a time
     t=6.0        -> set the "present" threshold to 6.0 cm
     help         -> show this menu

   Put a tool in the slot, then remove it, and note BOTH distances.

   Mega 1 wiring table (cabinets 1-5, USS 1-20) — TRIG/ECHO:
     cab1: 22/23  24/25  26/27  28/29
     cab2: 30/31  32/33  34/35  36/37
     cab3: 38/39  40/41  42/43  44/45
     cab4: 46/47  48/49  A0/A1  A2/A3
     cab5: A4/A5  A6/A7  A8/A9  A10/A11
   ============================================================================ */

uint8_t TRIG_PIN = 22;
uint8_t ECHO_PIN = 23;
float   THRESHOLD = 8.0;                   // <= this cm = "tool present"

const unsigned long ECHO_TIMEOUT_US  = 12000;  // ~2 m; a dead sensor costs 12ms, not 30
const uint8_t       SENSOR_SETTLE_MS = 60;     // HC-SR04 datasheet measurement cycle

/* Mega 1 cabinet -> {trig, echo} per slot, mirroring locker_controller.ino. */
const uint8_t CAB_PINS[5][4][2] = {
  { {22,23}, {24,25}, {26,27}, {28,29} },
  { {30,31}, {32,33}, {34,35}, {36,37} },
  { {38,39}, {40,41}, {42,43}, {44,45} },
  { {46,47}, {48,49}, {A0,A1}, {A2,A3} },
  { {A4,A5}, {A6,A7}, {A8,A9}, {A10,A11} },
};

float readCm(uint8_t trig, uint8_t echo) {
  if (!trig || !echo) return -1;
  pinMode(trig, OUTPUT);
  pinMode(echo, INPUT);
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long dur = pulseIn(echo, HIGH, ECHO_TIMEOUT_US);
  return dur == 0 ? -1 : dur * 0.0343 / 2.0;
}

void report(float d) {
  if (d < 0) Serial.println("  no echo (check TRIG/ECHO pins & 5V/GND)");
  else {
    Serial.print("  "); Serial.print(d, 1); Serial.print(" cm  -> ");
    Serial.println(d <= THRESHOLD ? "TOOL PRESENT" : "slot EMPTY");
  }
}

void applyPins() {
  Serial.print("TRIG="); Serial.print(TRIG_PIN);
  Serial.print("  ECHO="); Serial.print(ECHO_PIN);
  Serial.print("  threshold="); Serial.print(THRESHOLD); Serial.println(" cm");
}

void menu() {
  Serial.println();
  Serial.println("== Ultrasonic tester (per-sensor TRIG) ==");
  Serial.println("Type: <trig>/<echo>  e.g. 22/23 or a0/a1  |  cab1..cab5  |  t=<cm>  |  help");
}

/* Parse one pin token: "22" or "a0". Returns 255 on failure. */
uint8_t parsePin(String s) {
  s.trim();
  if (s.length() == 0) return 255;
  if (s.startsWith("a")) {
    int a = s.substring(1).toInt();
    return (a >= 0 && a <= 15) ? (uint8_t)(A0 + a) : 255;
  }
  int n = s.toInt();
  return (n > 1 && n <= 69) ? (uint8_t) n : 255;
}

void sweepCabinet(uint8_t cab) {          // cab is 1-based
  Serial.print("Cabinet "); Serial.print(cab); Serial.println(" — one sensor at a time:");
  for (uint8_t s = 0; s < 4; s++) {
    uint8_t t = CAB_PINS[cab - 1][s][0], e = CAB_PINS[cab - 1][s][1];
    Serial.print("  slot "); Serial.print(s + 1);
    Serial.print("  TRIG="); Serial.print(t);
    Serial.print(" ECHO=");  Serial.println(e);
    report(readCm(t, e));
    delay(SENSOR_SETTLE_MS);              // never fire two sensors back to back
  }
}

void setup() {
  Serial.begin(115200);
  menu();
  applyPins();
}

void loop() {
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    s.toLowerCase();

    if (s == "help") { menu(); }
    else if (s.startsWith("t=")) { THRESHOLD = s.substring(2).toFloat(); applyPins(); }
    else if (s.startsWith("cab")) {
      int c = s.substring(3).toInt();
      if (c >= 1 && c <= 5) sweepCabinet(c);
      else Serial.println("cab1..cab5 only");
      return;
    }
    else if (s.indexOf('/') > 0) {
      uint8_t t = parsePin(s.substring(0, s.indexOf('/')));
      uint8_t e = parsePin(s.substring(s.indexOf('/') + 1));
      if (t == 255 || e == 255) Serial.println("Bad pins. Use <trig>/<echo>, e.g. 22/23 or a0/a1");
      else { TRIG_PIN = t; ECHO_PIN = e; applyPins(); }
    }
    else if (s.length()) Serial.println("Use <trig>/<echo> (e.g. 22/23), cab1..cab5, or t=<cm>");
  }

  report(readCm(TRIG_PIN, ECHO_PIN));
  delay(300);
}
