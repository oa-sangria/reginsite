/* ============================================================================
   reginsite — Locker Controller firmware  (Arduino Mega 2560)
   4-locker smart tool cabinet · RFID borrow/return toggle
   ----------------------------------------------------------------------------
   Each tool has its own RFID tag. Scan a tag:
       tool is IN  -> that locker UNLOCKS, logs a BORROW  (LED on)
       tool is OUT -> that locker LOCKS,   logs a RETURN  (LED off)

   The Mega has no network/clock, so it doesn't touch the database. It talks
   over USB serial (115200) to bridge.py on the mini PC, which calls the
   Laravel API. See firmware/README.md.

   Ultrasonic sensors are read + printed for monitoring but do NOT gate the
   borrow/return logic yet (that's the next step).

   --- Pins (your wiring) -----------------------------------------------------
   RC522:  SS/SDA=53  SCK=52  MOSI=51  MISO=50  RST=9   VCC=3.3V  GND=GND
                        Locker1  Locker2  Locker3  Locker4
     Relay ............   22       23       24       25
     LED ..............   26       27       28       38
     Ultrasonic TRIG ..   30       32       34       36
     Ultrasonic ECHO ..   29       33       35       37

   --- Serial protocol --------------------------------------------------------
   Mega -> PC:  READY
                SCAN,<uid>                    a known tag was read
                UNKNOWN,<uid>                 tag not in the table below
                EVENT,OPEN,<locker>,<uid>     locker opened  -> bridge BORROWs
                EVENT,CLOSE,<locker>,<uid>    locker closed  -> bridge RETURNs
   PC -> Mega:  ACK                           DB write ok  (LED confirm blink)
                NAK,<msg>                      DB write failed (LED error blink)
   ============================================================================ */

#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN  53
#define RST_PIN 9
MFRC522 rfid(SS_PIN, RST_PIN);

/* ===========================================================================
   >>> STEP 1: register your 4 tags here. <<<
   Upload once with these blank, open Serial Monitor @115200, and tap each tag.
   You'll see e.g.  UNKNOWN,DE AD BE EF   — copy that UID (uppercase, spaces ok)
   into the slot for the locker that tag belongs to, then re-upload.
   =========================================================================== */
String tagUID[4] = {
  "",   // Locker 1 tag
  "",   // Locker 2 tag
  "",   // Locker 3 tag
  ""    // Locker 4 tag
};

/* ---- Pins ---------------------------------------------------------------- */
const byte relayPins[4] = { 22, 23, 24, 25 };
const byte ledPins[4]   = { 26, 27, 28, 38 };
const byte trigPins[4]  = { 30, 32, 34, 36 };
const byte echoPins[4]  = { 29, 33, 35, 37 };

/* ---- Settings ------------------------------------------------------------ */
const bool  ACTIVE_LOW = true;              // your relay board is active-low
const float TOOL_PRESENT_THRESHOLD = 8.0;   // cm (used by sensors, not yet in logic)
const bool  DEBUG_DISTANCE = false;         // true = print sensor distances every 2s
const unsigned long SCAN_COOLDOWN_MS = 2000;

/* ---- State --------------------------------------------------------------- */
bool lockerState[4] = { false, false, false, false };   // false = locked
String lastUID = "";
unsigned long lastScanMs = 0;
unsigned long lastDistMs = 0;

/* ---- Relay / LED --------------------------------------------------------- */
void lockLocker(byte i) {
  digitalWrite(relayPins[i], ACTIVE_LOW ? HIGH : LOW);   // de-energize = locked
  digitalWrite(ledPins[i], LOW);
  lockerState[i] = false;
  Serial.print("Locker "); Serial.print(i + 1); Serial.println(" LOCKED");
}
void unlockLocker(byte i) {
  digitalWrite(relayPins[i], ACTIVE_LOW ? LOW : HIGH);   // energize = unlocked
  digitalWrite(ledPins[i], HIGH);
  lockerState[i] = true;
  Serial.print("Locker "); Serial.print(i + 1); Serial.println(" UNLOCKED");
}

/* ---- Ultrasonic (monitoring only for now) -------------------------------- */
float readDistance(byte s) {
  digitalWrite(trigPins[s], LOW);  delayMicroseconds(2);
  digitalWrite(trigPins[s], HIGH); delayMicroseconds(10);
  digitalWrite(trigPins[s], LOW);
  long duration = pulseIn(echoPins[s], HIGH, 30000);
  if (duration == 0) return -1;
  return duration * 0.0343 / 2.0;
}
void printDistances() {
  Serial.println("-------------- distances --------------");
  for (byte i = 0; i < 4; i++) {
    float d = readDistance(i);
    Serial.print("Locker "); Serial.print(i + 1); Serial.print(" : ");
    if (d < 0) Serial.println("No Echo");
    else { Serial.print(d); Serial.println(" cm"); }
  }
}

/* ---- UID helpers --------------------------------------------------------- */
// Uppercase hex, space-separated, e.g. "DE AD BE EF"
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
// Which locker (0..3) owns this UID, or -1 if unknown.
int lockerForUID(const String &uid) {
  for (byte i = 0; i < 4; i++)
    if (tagUID[i].length() && tagUID[i] == uid) return i;
  return -1;
}

/* ---- LED feedback from the bridge (ACK/NAK) ------------------------------ */
void blink(byte pin, byte times, int ms) {
  for (byte i = 0; i < times; i++) {
    digitalWrite(pin, HIGH); delay(ms);
    digitalWrite(pin, LOW);  delay(ms);
  }
}
void handleSerial() {
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    // Confirm on the most-recently-toggled locker's LED, then restore it.
    int i = lockerForUID(lastUID);
    byte pin = (i >= 0) ? ledPins[i] : LED_BUILTIN;
    bool restore = (i >= 0) ? lockerState[i] : false;
    if (line == "ACK")            blink(pin, 2, 90);
    else if (line.startsWith("NAK")) blink(pin, 3, 200);
    if (i >= 0) digitalWrite(pin, restore ? HIGH : LOW);
  }
}

/* ========================================================================== */
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("==========================================");
  Serial.println(" SMART TOOL LENDING CABINET  (reginsite)");
  Serial.println("==========================================");

  SPI.begin();
  rfid.PCD_Init();
  Serial.print("RC522 Version : ");
  rfid.PCD_DumpVersionToSerial();

  for (byte i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    pinMode(ledPins[i], OUTPUT);
    pinMode(trigPins[i], OUTPUT);
    pinMode(echoPins[i], INPUT);
    lockLocker(i);                 // start locked
  }
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println("READY");
  Serial.println("Waiting for RFID tag...");
}

void loop() {
  handleSerial();

  if (DEBUG_DISTANCE && millis() - lastDistMs > 2000) {
    lastDistMs = millis();
    printDistances();
  }

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String uid = uidString();
  unsigned long now = millis();

  // Debounce the same tag held on the reader.
  if (uid == lastUID && (now - lastScanMs) < SCAN_COOLDOWN_MS) {
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    return;
  }
  lastUID = uid;
  lastScanMs = now;

  int i = lockerForUID(uid);
  if (i < 0) {
    // Unknown tag: print it so you can register it in tagUID[] above.
    Serial.print("UNKNOWN,"); Serial.println(uid);
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    return;
  }

  Serial.print("SCAN,"); Serial.println(uid);

  bool willOpen = !lockerState[i];
  if (willOpen) unlockLocker(i); else lockLocker(i);

  // Tell the bridge: OPEN -> borrow, CLOSE -> return.
  Serial.print("EVENT,");
  Serial.print(willOpen ? "OPEN," : "CLOSE,");
  Serial.print(i + 1);            // 1-based locker number
  Serial.print(",");
  Serial.println(uid);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
