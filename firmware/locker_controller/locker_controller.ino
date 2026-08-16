/* ============================================================================
   reginsite — Locker Controller firmware  (Arduino Mega 2560)
   Smart Tool Lending Cabinet · SCREEN-DRIVEN · PER-SLOT ultrasonic detection
   ----------------------------------------------------------------------------
   The touchscreen (mini PC) decides everything. The Arduino obeys OPEN
   commands, watches the PER-SLOT ultrasonic sensors to see which tool moved,
   reads the tool's RFID tag, and reports back. It does NOT touch the database
   — the mini-PC bridge does.

   FLOW (borrow):
     PC -> OPEN,<locker>,borrow
     Mega: unlock locker, buzzer beep, red LED
     student removes a tool -> that SLOT's ultrasonic sees it leave
     student scans the tool's RFID tag on the RC522
     Mega -> DONE,<locker>,<uid>,<slot>   -> bridge records the borrow
     (return waits for a slot to become FILLED again)
     timeout -> TIMEOUT,<locker>, relock

   --- Serial protocol --------------------------------------------------------
   PC  -> Mega : OPEN,<locker>,<borrow|return>
   Mega-> PC   : READY
                 OPENED,<locker>
                 SCAN,<uid>
                 DONE,<locker>,<uid>,<slot>   (slot = 1..N, or 0 if unknown)
                 TIMEOUT,<locker>
                 NOWIRE,<locker>

   ----------------------------------------------------------------------------
   >>> PIN BUDGET NOTE <<<
   Full build = 34 ultrasonic sensors. Per-sensor trig+echo (68 pins) will NOT
   fit a Mega alongside relays + RC522 + buzzer. Recommended wiring: ONE shared
   TRIG line to every sensor (SHARED_TRIG_PIN) + one ECHO pin per slot (~35
   pins). This sketch is built for that. Fill the pin tables when wired.
   ============================================================================ */

#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN  53
#define RST_PIN 9
#define BUZZER_PIN 40
// The buzzer is a TESTING aid only. Real status signaling is the LED.
// Leave false for production; set true if you want audible feedback while testing.
const bool BUZZER_ENABLED = false;
MFRC522 rfid(SS_PIN, RST_PIN);

const uint8_t NUM_LOCKERS = 10;
const uint8_t MAX_SLOTS   = 4;

/* Solenoid relay + status LED, one per locker. 0 = not wired yet.
   Lockers 1-4 use your tested wiring; fill 5-10 when built. */
const uint8_t relayPins[NUM_LOCKERS] = { 22, 23, 24, 25, 0, 0, 0, 0, 0, 0 };
const uint8_t ledPins  [NUM_LOCKERS] = { 26, 27, 28, 38, 0, 0, 0, 0, 0, 0 };

/* How many tool slots each locker has (matches the seeded inventory). */
const uint8_t slotCount[NUM_LOCKERS] = { 4, 4, 4, 4, 4, 4, 2, 4, 4, 0 };

/* Per-slot ultrasonic ECHO pins.  slotEcho[locker][slot]. 0 = not wired.
   >>> FILL THESE WHEN YOU WIRE THE SENSORS (one echo pin per slot). <<< */
const uint8_t slotEcho[NUM_LOCKERS][MAX_SLOTS] = {
  { 0, 0, 0, 0 },  // Locker 1  Soldering Iron  (4 slots)
  { 0, 0, 0, 0 },  // Locker 2  Plier
  { 0, 0, 0, 0 },  // Locker 3  Clamp Ammeter
  { 0, 0, 0, 0 },  // Locker 4  Multitester
  { 0, 0, 0, 0 },  // Locker 5  Screwdriver Set
  { 0, 0, 0, 0 },  // Locker 6  Side Cutter
  { 0, 0, 0, 0 },  // Locker 7  Makita Drill (2 slots)
  { 0, 0, 0, 0 },  // Locker 8  Wire Stripper
  { 0, 0, 0, 0 },  // Locker 9  Wire Crimper
  { 0, 0, 0, 0 },  // Locker 10 spare
};

/* One shared TRIG line to all sensors (set the pin once you wire it; 0 = none). */
const uint8_t SHARED_TRIG_PIN = 0;

const bool  ACTIVE_LOW = true;              // relay board polarity
const float PRESENT_CM = 8.0;               // <= this = a tool is in the slot
const unsigned long OPEN_TIMEOUT_MS = 20000;

bool wired(uint8_t i)         { return i < NUM_LOCKERS && relayPins[i] != 0; }
bool sensorsWired(uint8_t i)  { for (uint8_t s = 0; s < slotCount[i]; s++) if (slotEcho[i][s]) return true; return false; }

/* ---- Relay / LED / buzzer ------------------------------------------------- */
void relay(uint8_t i, bool energized) {
  digitalWrite(relayPins[i], (ACTIVE_LOW ? !energized : energized) ? HIGH : LOW);
}
void lockLocker(uint8_t i)   { relay(i, false); if (ledPins[i]) digitalWrite(ledPins[i], LOW); }
void unlockLocker(uint8_t i) { relay(i, true);  if (ledPins[i]) digitalWrite(ledPins[i], HIGH); }
void beep(int ms, int times = 1) {
  if (!BUZZER_ENABLED) return;            // test-only; production uses the LED
  for (int k = 0; k < times; k++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(ms);
    digitalWrite(BUZZER_PIN, LOW);  if (k < times - 1) delay(ms);
  }
}

/* ---- Per-slot ultrasonic ------------------------------------------------- */
float slotDistance(uint8_t locker, uint8_t slot) {
  uint8_t echo = slotEcho[locker][slot];
  if (!echo || !SHARED_TRIG_PIN) return -1;
  digitalWrite(SHARED_TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(SHARED_TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(SHARED_TRIG_PIN, LOW);
  long dur = pulseIn(echo, HIGH, 30000);
  return dur == 0 ? -1 : dur * 0.0343 / 2.0;
}
bool slotFilled(uint8_t locker, uint8_t slot) {
  float d = slotDistance(locker, slot);
  return d >= 0 && d <= PRESENT_CM;
}

/* ---- RFID ---------------------------------------------------------------- */
String uidString() {
  String s = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) s += "0";
    s += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) s += " ";
  }
  s.toUpperCase();
  return s;
}
String readTagOrEmpty() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return "";
  String u = uidString();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return u;
}

/* ---- Handle one OPEN command -------------------------------------------- */
void handleOpen(uint8_t locker1, const String &mode) {
  uint8_t i = locker1 - 1;
  if (!wired(i)) { Serial.print("NOWIRE,"); Serial.println(locker1); return; }

  bool wantFill = (mode == "return");   // return = a slot becomes FILLED again
  bool haveSensors = sensorsWired(i);

  // Snapshot which slots are filled right now (baseline for change detection).
  bool baseline[MAX_SLOTS];
  for (uint8_t s = 0; s < slotCount[i]; s++) baseline[s] = haveSensors ? slotFilled(i, s) : false;

  unlockLocker(i);
  beep(120);
  Serial.print("OPENED,"); Serial.println(locker1);

  unsigned long start = millis();
  String tag = "";
  int changedSlot = -1;

  while (millis() - start < OPEN_TIMEOUT_MS) {
    // 1) capture the tool's tag when scanned
    if (tag == "") {
      String u = readTagOrEmpty();
      if (u != "") { tag = u; Serial.print("SCAN,"); Serial.println(u); }
    }
    // 2) find a slot whose state made the expected transition
    if (haveSensors && changedSlot < 0) {
      for (uint8_t s = 0; s < slotCount[i]; s++) {
        bool now = slotFilled(i, s);
        if (wantFill ? (!baseline[s] && now) : (baseline[s] && !now)) { changedSlot = s; break; }
      }
    }
    // Confirm when we have the tag AND (a sensor change, or no sensors wired yet)
    if (tag != "" && (changedSlot >= 0 || !haveSensors)) {
      lockLocker(i);
      beep(80, 2);
      Serial.print("DONE,"); Serial.print(locker1); Serial.print(",");
      Serial.print(tag); Serial.print(","); Serial.println(changedSlot + 1); // 1-based, 0 = unknown
      return;
    }
    delay(40);
  }

  lockLocker(i);
  beep(400);
  Serial.print("TIMEOUT,"); Serial.println(locker1);
}

/* ---- Serial command parsing --------------------------------------------- */
void handleSerial() {
  static String buf = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (buf.startsWith("OPEN,")) {
        int c1 = buf.indexOf(',');
        int c2 = buf.indexOf(',', c1 + 1);
        uint8_t locker = buf.substring(c1 + 1, c2 > 0 ? c2 : buf.length()).toInt();
        String mode = c2 > 0 ? buf.substring(c2 + 1) : "borrow";
        mode.trim();
        handleOpen(locker, mode);
      }
      buf = "";
    } else {
      buf += c;
    }
  }
}

/* ========================================================================== */
void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(BUZZER_PIN, OUTPUT);
  if (SHARED_TRIG_PIN) pinMode(SHARED_TRIG_PIN, OUTPUT);
  for (uint8_t i = 0; i < NUM_LOCKERS; i++) {
    if (!wired(i)) continue;
    pinMode(relayPins[i], OUTPUT);
    if (ledPins[i]) pinMode(ledPins[i], OUTPUT);
    for (uint8_t s = 0; s < slotCount[i]; s++)
      if (slotEcho[i][s]) pinMode(slotEcho[i][s], INPUT);
    lockLocker(i);
  }

  Serial.println("READY");
}

void loop() {
  handleSerial();
}
