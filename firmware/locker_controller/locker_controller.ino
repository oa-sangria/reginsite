/* ============================================================================
   reginsite — Locker Controller firmware  (Arduino Mega 2560)
   ----------------------------------------------------------------------------
   RFID borrow/return TOGGLE for a single solenoid locker (Locker 10).

   Scan an RFID card  -> relay energizes the 12V solenoid (UNLOCK / borrow)
   Scan the same card -> relay releases the solenoid    (LOCK / return)

   The Mega has no network + no clock, so it does NOT touch the database.
   It talks over USB serial (115200 baud) to a bridge program on the mini PC,
   which calls the Laravel API. See firmware/README.md.

   --- Wiring -----------------------------------------------------------------
   RC522 RFID reader        Arduino Mega
     SDA / SS ............. D53
     SCK ................. D52   (hardware SPI)
     MOSI ................ D51   (hardware SPI)
     MISO ................ D50   (hardware SPI)
     RST ................. D9
     VCC ................. 3.3V   (NOT 5V)
     GND ................. GND

   Relay module (Locker 10)  Arduino Mega
     IN .................. D31
     VCC / GND ........... 5V / GND
     COM / NO ............ switches the 12V brick to the solenoid

   --- Libraries --------------------------------------------------------------
   SPI      (bundled with the Arduino IDE)
   MFRC522  (Library Manager: "MFRC522" by GithubCommunity)

   --- Serial protocol (Mega -> PC) -------------------------------------------
   READY                       on boot
   SCAN,<uid>                  a card was read (uid = hex, no spaces)
   EVENT,OPEN,<uid>            lock just opened  -> bridge should BORROW
   EVENT,CLOSE,<uid>           lock just closed  -> bridge should RETURN

   --- Serial protocol (PC -> Mega) -------------------------------------------
   ACK                         DB write succeeded  (LED double-blink)
   NAK,<msg>                   DB write failed     (LED triple-blink; lock NOT reversed)
   ============================================================================ */

#include <SPI.h>
#include <MFRC522.h>

/* ---- Pins ---------------------------------------------------------------- */
#define RST_PIN   9
#define SS_PIN    53
#define STATUS_LED 13          // onboard LED mirrors lock state (on = open)

// Relay input pins, one per locker. Only Locker 10 (D31) is wired for now;
// add more pins here to grow to more lockers later.
const uint8_t RELAY_PINS[] = { 31 };
const uint8_t LOCKER_COUNT = sizeof(RELAY_PINS) / sizeof(RELAY_PINS[0]);

/* Most cheap relay boards are ACTIVE-LOW (LOW = energized). If your solenoid
   opens when it should close, flip this to false. */
#define RELAY_ACTIVE_LOW true

/* How long to ignore repeat reads of the same card, in ms. */
#define SCAN_COOLDOWN_MS 2000

MFRC522 rfid(SS_PIN, RST_PIN);

bool     lockOpen[LOCKER_COUNT];      // false = locked, true = unlocked
String   lastUid = "";
uint32_t lastScanMs = 0;

/* ---- Relay helpers ------------------------------------------------------- */
void relayWrite(uint8_t idx, bool energized) {
  uint8_t level = RELAY_ACTIVE_LOW ? (energized ? LOW : HIGH)
                                   : (energized ? HIGH : LOW);
  digitalWrite(RELAY_PINS[idx], level);
}

void setLock(uint8_t idx, bool open) {
  lockOpen[idx] = open;
  relayWrite(idx, open);                 // open = solenoid energized = unlocked
  if (idx == 0) digitalWrite(STATUS_LED, open ? HIGH : LOW);
}

/* ---- LED feedback -------------------------------------------------------- */
void blink(uint8_t times, uint16_t ms) {
  bool restore = lockOpen[0];
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(STATUS_LED, HIGH); delay(ms);
    digitalWrite(STATUS_LED, LOW);  delay(ms);
  }
  digitalWrite(STATUS_LED, restore ? HIGH : LOW);
}

/* ---- Read the current card's UID as an uppercase hex string -------------- */
String readUid() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

/* ---- Handle replies coming back from the PC bridge ----------------------- */
void handleSerial() {
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (line == "ACK")            blink(2, 90);   // saved OK
    else if (line.startsWith("NAK")) blink(3, 220); // save failed
  }
}

/* ========================================================================== */
void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED, OUTPUT);
  for (uint8_t i = 0; i < LOCKER_COUNT; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    setLock(i, false);                   // start locked
  }

  SPI.begin();
  rfid.PCD_Init();

  Serial.println("READY");
}

void loop() {
  handleSerial();

  // Nothing new on the reader? bail out cheaply.
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String uid = readUid();
  uint32_t now = millis();

  // Debounce: ignore the same card presented again within the cooldown window.
  if (uid == lastUid && (now - lastScanMs) < SCAN_COOLDOWN_MS) {
    rfid.PICC_HaltA();
    return;
  }
  lastUid = uid;
  lastScanMs = now;

  Serial.print("SCAN,");
  Serial.println(uid);

  // Single-locker toggle for now (index 0 = Locker 10 on D31).
  uint8_t idx = 0;
  bool nowOpen = !lockOpen[idx];
  setLock(idx, nowOpen);

  // Tell the bridge which way we just went so it can borrow/return.
  Serial.print("EVENT,");
  Serial.print(nowOpen ? "OPEN," : "CLOSE,");
  Serial.println(uid);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
