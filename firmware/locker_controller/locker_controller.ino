/* ============================================================================
   reginsite — Locker Controller firmware  (Arduino Mega 2560)
   Smart Tool Lending Cabinet · SCREEN-DRIVEN · PER-SLOT ultrasonic detection
   ----------------------------------------------------------------------------
   The touchscreen (mini PC) decides everything. The Arduino obeys OPEN
   commands, watches the PER-SLOT ultrasonic sensors to see which tool moved,
   reads the tool's RFID tag, and reports back. It does NOT touch the database
   — the mini-PC bridge does.

   FLOW (borrow):
     PC -> OPEN,<cabinet>,borrow
     Mega: unlock cabinet, buzzer beep
     student removes a tool -> that SLOT's ultrasonic sees it leave
     student scans the tool's RFID tag on the RC522
     Mega -> DONE,<cabinet>,<uid>,<slot>   -> bridge records the borrow
     (return waits for a slot to become FILLED again)
     timeout -> TIMEOUT,<cabinet>, relock

   --- Serial protocol --------------------------------------------------------
   PC  -> Mega : OPEN,<cabinet>,<borrow|return>
                 WHO                      (re-announce identity, no reset)
                 ABORT                    (relock the open cabinet now)
   Mega-> PC   : #<banner>                (informational, bridge ignores)
                 READY,<controller_id>
                 OPENED,<cabinet>
                 SCAN,<uid>
                 DONE,<cabinet>,<uid>,<slot>   (slot 1..N, 0 = unknown)
                 TIMEOUT,<cabinet>
                 NOWIRE,<cabinet>         (that cabinet is on the OTHER board)
                 ERR,<line>

   ----------------------------------------------------------------------------
   TWO CONTROLLERS
   The build splits across two Megas so every sensor gets its own TRIG+ECHO:
     CONTROLLER_ID 1 -> cabinets 1-5  (20 sensors) + the ONLY RC522 + buzzer
     CONTROLLER_ID 2 -> cabinets 6-10 (14 sensors), no RFID reader
   One bridge process owns both COM ports and routes by cabinet number.

   Cabinet 9 and 10 hold one Makita Drill each, hence a single slot apiece.

   >>> Cross-cabinet crosstalk is impossible BY CONSTRUCTION: handleOpen() is
   fully blocking, so only one cabinet is ever sensing at a time. Within a
   cabinet, sensors fire one per tick, never in a batch. Do not make
   handleOpen() non-blocking without re-solving this.
   ============================================================================ */

#ifndef CONTROLLER_ID
#define CONTROLLER_ID 1     // 1 = cabinets 1-5 (RC522 + buzzer) · 2 = cabinets 6-10
#endif
#if (CONTROLLER_ID != 1) && (CONTROLLER_ID != 2)
  #error "CONTROLLER_ID must be 1 or 2"
#endif

#define HAS_RFID   (CONTROLLER_ID == 1)
#define HAS_BUZZER (CONTROLLER_ID == 1)

#if HAS_RFID
  #include <SPI.h>
  #include <MFRC522.h>
  #define SS_PIN  53
  #define RST_PIN 5
  MFRC522 rfid(SS_PIN, RST_PIN);
#endif

#if HAS_BUZZER
  #define BUZZER_PIN 16     // NOTE: D16 is TX2 — never add a Serial2 device on this board
#endif

/* Flip to true once a cabinet's sensors are physically wired. While false the
   firmware confirms on the RFID tag alone (slot reported as 0), which is the
   safe bring-up path before the ultrasonics go in. */
const bool SENSORS_ENABLED = false;

/* BRING-UP DIAGNOSTIC — set false for production.
   Allows "SIMTAG,<uid>" over serial to stand in for a physical tag scan, so the
   whole chain (bridge -> OPEN -> DONE -> Laravel) can be exercised without a
   tool in hand. It bypasses the reader, so anyone with serial access could
   trigger a borrow — which is why it must be off in the field. */
const bool ALLOW_SIMTAG = true;

/* Idle tag reporting, toggleable at runtime with "IDLESCAN,0|1".
   It MUST be off while running RF diagnostics: the idle poll and a diagnostic
   REQA are two independent anticollision sequences aimed at the same tag, and
   they collide into a malformed ATQA that looks exactly like a hardware fault.
   Any measurement taken with this on is measuring the firmware, not the reader. */
bool idleScanEnabled = true;

const uint8_t MAX_SLOTS = 4;

struct Slot    { uint8_t trig, echo; };
struct Cabinet { uint8_t number, relayPin, slots; Slot slot[MAX_SLOTS]; };

/* Debounced per-slot occupancy. Declared up here with the other structs because
   the Arduino IDE injects auto-generated function prototypes near the top of the
   file — anything they reference must already be known at that point. */
struct SlotState { bool filled, cand; uint8_t agree, miss; };

/* A0..A15 == D54..D69 on the Mega; ALL of them are full digital I/O.
   (The "A6/A7 are analog-input-only" rule is Uno/Nano — it does NOT apply here.)
   Written as A0.. rather than 54.. so this table proofreads line-by-line
   against the wiring document and the board silkscreen. */
#if CONTROLLER_ID == 1
const uint8_t NUM_CABS = 5;
const Cabinet CABS[NUM_CABS] = {
  //  cab  relay  slots  {TRIG,ECHO} per slot
  {    1,  A12,   4, { {22,23}, {24,25}, {26,27}, {28,29}   } },  // USS 1-4   Pliers
  {    2,  A13,   4, { {30,31}, {32,33}, {34,35}, {36,37}   } },  // USS 5-8   Side Cutter
  {    3,    6,   4, { {38,39}, {40,41}, {42,43}, {44,45}   } },  // USS 9-12  Wire Crimper
  {    4,    7,   4, { {46,47}, {48,49}, {A0,A1}, {A2,A3}   } },  // USS 13-16 Clamp Meter
  {    5,    8,   4, { {A4,A5}, {A6,A7}, {A8,A9}, {A10,A11} } },  // USS 17-20 Multimeter
};
#else
const uint8_t NUM_CABS = 5;
/* Relays on A0-A4, NOT the wiring doc's D50-D53: those four are the SPI bus
   (MISO/MOSI/SCK/SS). If SPI.begin() ever gets compiled into this build — a
   future SD card, a second RC522, an attached ICSP programmer — it would drive
   SCK/MOSI as outputs and fire cabinets 7 and 8. A0-A15 are otherwise unused
   on this board, so the SPI bus stays free. */
const Cabinet CABS[NUM_CABS] = {
  {    6,   A0,   4, { {22,23}, {24,25}, {26,27}, {28,29} } },    // USS 21-24
  {    7,   A1,   4, { {30,31}, {32,33}, {34,35}, {36,37} } },    // USS 25-28
  {    8,   A2,   4, { {38,39}, {40,41}, {42,43}, {44,45} } },    // USS 29-32
  {    9,   A3,   1, { {46,47} } },                               // USS 33  Makita Drill A
  {   10,   A4,   1, { {48,49} } },                               // USS 34  Makita Drill B
};
#endif

const bool  ACTIVE_LOW = true;                 // relay board polarity
const float PRESENT_CM = 8.0;                  // <= this = tool in the slot
const float ABSENT_CM  = 12.0;                 // >= this = slot empty
                                               // between the two: hold previous state
const uint8_t       AGREE_N          = 2;      // samples that must agree to flip a slot
const uint8_t       MISS_LIMIT       = 3;      // consecutive no-echoes before flagging
const unsigned long ECHO_TIMEOUT_US  = 12000;  // ~2 m; a dead sensor costs 12ms not 30
const uint8_t       SENSOR_SETTLE_MS = 60;     // HC-SR04 datasheet measurement cycle
const unsigned long OPEN_TIMEOUT_MS  = 20000;

/* ---- Cabinet lookup ------------------------------------------------------- */
int8_t cabIndex(uint8_t cabNumber) {
  for (uint8_t i = 0; i < NUM_CABS; i++) if (CABS[i].number == cabNumber) return i;
  return -1;
}

/* ---- Relay ---------------------------------------------------------------- */
inline uint8_t relayIdle()   { return ACTIVE_LOW ? HIGH : LOW; }
inline uint8_t relayActive() { return ACTIVE_LOW ? LOW  : HIGH; }

void lockCabinet(uint8_t i)   { digitalWrite(CABS[i].relayPin, relayIdle());   }
void unlockCabinet(uint8_t i) { digitalWrite(CABS[i].relayPin, relayActive()); }

/* Must run BEFORE anything else in setup(). At reset every pin is a high-Z
   input; calling pinMode(OUTPUT) latches whatever PORTx holds, which is 0 —
   LOW — and with ACTIVE_LOW relays that FIRES THE SOLENOID. Writing the idle
   level while the pin is still an input sets PORTx (enabling the internal
   pull-up immediately), so the following pinMode(OUTPUT) starts out driving
   HIGH instead of glitching LOW.
   This cannot close the power-on -> setup() window (bootloader, ~0.5-2s, and
   every USB DTR auto-reset when the bridge opens the port). That needs a 10k
   pull-up from each relay IN to +5V in hardware — see firmware/README.md. */
void relaysSafeInit() {
  for (uint8_t i = 0; i < NUM_CABS; i++) {
    uint8_t p = CABS[i].relayPin;
    digitalWrite(p, relayIdle());
    pinMode(p, OUTPUT);
    digitalWrite(p, relayIdle());
  }

  /* BRING-UP ONLY. The old bench harness had relays on D22/D23, which this
     firmware does not drive — and an un-driven pin on an ACTIVE-LOW relay board
     can float low and hold the solenoid on. While SENSORS_ENABLED is false those
     pins cannot be ultrasonics yet, so parking them at the de-energized level is
     safe and stops a half-rewired rig from energizing a solenoid.
     This block disappears automatically once SENSORS_ENABLED is turned on, at
     which point D22/D23 become cabinet 1's first TRIG/ECHO pair. */
  if (!SENSORS_ENABLED) {
    const uint8_t legacyRelayPins[] = { 22, 23 };
    for (uint8_t i = 0; i < sizeof(legacyRelayPins) / sizeof(legacyRelayPins[0]); i++) {
      digitalWrite(legacyRelayPins[i], relayIdle());
      pinMode(legacyRelayPins[i], OUTPUT);
      digitalWrite(legacyRelayPins[i], relayIdle());
    }
  }
}

/* ---- Buzzer (blocking; never call from inside the sensing loop) ----------- */
void beep(int ms, int times = 1) {
#if HAS_BUZZER
  for (int k = 0; k < times; k++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(ms);
    digitalWrite(BUZZER_PIN, LOW);  if (k < times - 1) delay(ms);
  }
#else
  (void) ms; (void) times;
#endif
}

/* ---- Per-slot ultrasonic -------------------------------------------------- */
float pingCm(const Slot &s) {
  if (!s.trig || !s.echo) return -1;
  digitalWrite(s.trig, LOW);  delayMicroseconds(2);
  digitalWrite(s.trig, HIGH); delayMicroseconds(10);
  digitalWrite(s.trig, LOW);
  unsigned long dur = pulseIn(s.echo, HIGH, ECHO_TIMEOUT_US);
  return dur == 0 ? -1.0 : dur * 0.0343 / 2.0;
}

/* One trigger + one echo + debounce. A missed echo is NOT "absent" — it is no
   sample at all, so it must not feed the debouncer. */
void sampleSlot(const Cabinet &c, uint8_t s, SlotState *st) {
  float d = pingCm(c.slot[s]);
  if (d < 0) { if (st[s].miss < 255) st[s].miss++; return; }
  st[s].miss = 0;

  bool raw;
  if      (d <= PRESENT_CM) raw = true;
  else if (d >= ABSENT_CM)  raw = false;
  else return;                                  // hysteresis band — hold previous

  if (raw == st[s].cand) {
    if (st[s].agree < AGREE_N) st[s].agree++;
    if (st[s].agree >= AGREE_N) st[s].filled = raw;
  } else {
    st[s].cand = raw;
    st[s].agree = 1;
  }
}

/* Blocking baseline sweep, once, before the door opens. */
void baselineSweep(const Cabinet &c, SlotState *st) {
  for (uint8_t s = 0; s < c.slots; s++) { st[s].filled = false; st[s].cand = false; st[s].agree = 0; st[s].miss = 0; }
  for (uint8_t pass = 0; pass < AGREE_N; pass++) {
    for (uint8_t s = 0; s < c.slots; s++) {
      sampleSlot(c, s, st);
      delay(SENSOR_SETTLE_MS);                  // never fire two sensors back to back
    }
  }
}

/* ---- RFID ----------------------------------------------------------------- */
#if HAS_RFID
inline char hexDigit(uint8_t v) { return v < 10 ? ('0' + v) : ('A' + v - 10); }

/* No String anywhere: at a ~5ms poll cadence a String-based reader would churn
   thousands of heap allocations per open window on an 8KB heap. Returns false
   without allocating in the overwhelmingly common no-card case. */
/* PICC_IsNewCardPresent() sends REQA, which only answers cards in IDLE state.
   Every successful read ends with PICC_HaltA(), so a tag left sitting on the
   reader is HALTed and REQA can no longer see it — it would have to be lifted
   and re-tapped. WUPA wakes halted cards too, so try REQA first and fall back
   to WUPA. Without this, a tag already read while idle is invisible for the
   whole OPEN window. */
bool cardPresent() {
  byte atqa[2];
  byte size = sizeof(atqa);
  MFRC522::StatusCode s = rfid.PICC_RequestA(atqa, &size);
  if (s == MFRC522::STATUS_OK || s == MFRC522::STATUS_COLLISION) return true;
  size = sizeof(atqa);
  s = rfid.PICC_WakeupA(atqa, &size);
  return (s == MFRC522::STATUS_OK || s == MFRC522::STATUS_COLLISION);
}

bool readTagInto(char *out, uint8_t cap) {
  if (!cardPresent())              return false;
  if (!rfid.PICC_ReadCardSerial()) return false;
  uint8_t n = 0;
  for (uint8_t i = 0; i < rfid.uid.size; i++) {
    if (n + 3 >= cap) break;
    if (i) out[n++] = ' ';
    out[n++] = hexDigit(rfid.uid.uidByte[i] >> 4);
    out[n++] = hexDigit(rfid.uid.uidByte[i] & 0x0F);
  }
  out[n] = '\0';
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return n > 0;
}
#endif

/* ---- Line-based serial reader (shared by loop() and handleOpen()) ---------- */
char    rxBuf[40];
uint8_t rxLen = 0;

bool readLine(char *out, uint8_t cap) {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (rxLen == 0) continue;
      rxBuf[rxLen] = '\0';
      strncpy(out, rxBuf, cap - 1);
      out[cap - 1] = '\0';
      rxLen = 0;
      return true;
    }
    if (rxLen < sizeof(rxBuf) - 1) rxBuf[rxLen++] = c;
  }
  return false;
}

void announce() {
  Serial.print(F("#reginsite locker-controller controller="));
  Serial.print(CONTROLLER_ID);
  Serial.print(F(" cabinets="));
  Serial.print(CABS[0].number); Serial.print('-'); Serial.print(CABS[NUM_CABS - 1].number);
  Serial.print(F(" rfid=")); Serial.print(HAS_RFID ? 1 : 0);
  Serial.print(F(" sensors=")); Serial.println(SENSORS_ENABLED ? 1 : 0);
  Serial.print(F("READY,")); Serial.println(CONTROLLER_ID);
}

/* ---- SELFTEST: is the reader actually talking? ---------------------------- */
void selfTest() {
#if HAS_RFID
  /* The RC522 version register is the one honest answer about SPI wiring.
     0x91/0x92 = a real chip responding. 0x00 or 0xFF means the bus is dead:
     wrong SS/RST pin, MISO not connected, or VCC on 5V instead of 3.3V. */
  uint8_t v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("#rc522 version=0x"));
  if (v < 0x10) Serial.print('0');
  Serial.print(v, HEX);
  if (v == 0x91 || v == 0x92) Serial.println(F(" OK"));
  else if (v == 0x00 || v == 0xFF) Serial.println(F(" BAD - check SS=53 RST=5 MISO=50 and VCC=3.3V"));
  else Serial.println(F(" unexpected (clone?) - may still work"));

  /* A healthy chip that still reads nothing usually means the ANTENNA is off or
     the gain is low. TxControlReg bits 0-1 drive the two antenna pins; both must
     be set or there is no RF field and no tag can ever answer. */
  uint8_t tx = rfid.PCD_ReadRegister(MFRC522::TxControlReg);
  Serial.print(F("#rc522 TxControlReg=0x"));
  if (tx < 0x10) Serial.print('0');
  Serial.print(tx, HEX);
  Serial.println((tx & 0x03) == 0x03 ? F(" antenna ON") : F(" ANTENNA OFF"));

  uint8_t gain = (rfid.PCD_ReadRegister(MFRC522::RFCfgReg) >> 4) & 0x07;
  Serial.print(F("#rc522 rx gain=")); Serial.print(gain);
  Serial.println(gain >= 7 ? F(" (max)") : F(" (not max - raise for range)"));

  /* Probe with the SAME path the real read uses. A REQA-only probe is useless
     here: every successful read ends in PICC_HaltA(), and a HALTed card ignores
     REQA forever — the probe would report "Timeout" against a perfectly good tag
     sitting on the coil. cardPresent() falls back to WUPA, which wakes it. */
  byte atqa[2]; byte n = sizeof(atqa);
  MFRC522::StatusCode st = rfid.PICC_RequestA(atqa, &n);
  Serial.print(F("#rc522 REQA -> "));
  Serial.print(rfid.GetStatusCodeName(st));
  n = sizeof(atqa);
  MFRC522::StatusCode stw = rfid.PICC_WakeupA(atqa, &n);
  Serial.print(F("  WUPA -> "));
  Serial.println(rfid.GetStatusCodeName(stw));

  /* The measurement that actually matters: can we complete a full read? */
  char probe[32];
  Serial.print(F("#rc522 full read -> "));
  if (readTagInto(probe, sizeof(probe))) Serial.println(probe);
  else                                   Serial.println(F("FAILED"));
#else
  Serial.println(F("#no rfid on this controller"));
#endif
  Serial.print(F("#relay pins:"));
  for (uint8_t i = 0; i < NUM_CABS; i++) {
    Serial.print(' '); Serial.print(CABS[i].number);
    Serial.print('='); Serial.print(CABS[i].relayPin);
  }
  Serial.println();
  Serial.println(F("SELFTEST,done"));
}

/* ---- Handle one OPEN command --------------------------------------------- */
void handleOpen(uint8_t cabNum, const char *mode) {
  int8_t ci = cabIndex(cabNum);
  if (ci < 0) {                                  // routing error: other board's cabinet
    Serial.print(F("NOWIRE,")); Serial.println(cabNum);
    beep(50, 3);
    return;
  }
  const Cabinet &c = CABS[ci];
  bool wantFill    = (strcmp(mode, "return") == 0);
  bool haveSensors = SENSORS_ENABLED && c.slots > 0 && c.slot[0].trig != 0;

  SlotState st[MAX_SLOTS];
  bool baseline[MAX_SLOTS];
  if (haveSensors) {
    baselineSweep(c, st);
    for (uint8_t s = 0; s < c.slots; s++) baseline[s] = st[s].filled;
  }

  unlockCabinet(ci);
  Serial.print(F("OPENED,")); Serial.println(cabNum);   // report first, beep after
  beep(120);

  unsigned long start = millis(), lastPing = 0;
  char tag[32]; tag[0] = '\0';
  bool haveTag = false;
  int8_t changedSlot = -1;
  uint8_t nextSlot = 0;
  char cmd[40];

#if !HAS_RFID
  haveTag = true;      // no reader on this board; the bridge supplies the UID
#endif

  while (millis() - start < OPEN_TIMEOUT_MS) {
    /* 1) RFID every tick (~5ms). An RC522 polled slowly misses a tag that is
          tapped and lifted in under 300ms, which people do constantly. */
#if HAS_RFID
    if (!haveTag && readTagInto(tag, sizeof(tag))) {
      haveTag = true;
      Serial.print(F("SCAN,")); Serial.println(tag);
    }
#endif

    /* 2) ONE sensor per settle interval, round-robin. */
    if (haveSensors && changedSlot < 0 && millis() - lastPing >= SENSOR_SETTLE_MS) {
      lastPing = millis();
      sampleSlot(c, nextSlot, st);
      nextSlot = (nextSlot + 1) % c.slots;
      for (uint8_t s = 0; s < c.slots; s++) {
        if (wantFill ? (!baseline[s] && st[s].filled) : (baseline[s] && !st[s].filled)) {
          changedSlot = s;
          break;
        }
      }
    }

    /* 3) Confirm once we have the tag AND a sensor change (or no sensors yet). */
    if (haveTag && (changedSlot >= 0 || !haveSensors)) {
      /* Report BEFORE beeping. beep() blocks (~240ms here), and every one of
         those milliseconds is dead time before the server can record the
         transaction and the kiosk can move on. Locking is a single digitalWrite,
         so it still happens first. */
      lockCabinet(ci);
      Serial.print(F("DONE,"));  Serial.print(cabNum);
      Serial.print(',');         Serial.print(tag);
      Serial.print(',');         Serial.println(changedSlot + 1);   // 1-based, 0 = unknown
      beep(80, 2);
      return;
    }

    /* 4) The kiosk's Cancel button reaches us as ABORT — relock immediately
          instead of leaving the door open for the rest of the window. */
    if (readLine(cmd, sizeof(cmd))) {
      if (strcmp(cmd, "ABORT") == 0) {
        lockCabinet(ci);
        Serial.print(F("TIMEOUT,")); Serial.println(cabNum);   // report first
        beep(400);
        return;
      }
      if (strcmp(cmd, "WHO") == 0) announce();
      /* Stand in for a physical scan so the rest of the chain can be tested. */
      if (ALLOW_SIMTAG && !haveTag && strncmp(cmd, "SIMTAG,", 7) == 0) {
        strncpy(tag, cmd + 7, sizeof(tag) - 1);
        tag[sizeof(tag) - 1] = '\0';
        haveTag = true;
        Serial.print(F("SCAN,")); Serial.println(tag);
      }
    }

    delay(3);
  }

  lockCabinet(ci);
  Serial.print(F("TIMEOUT,")); Serial.println(cabNum);   // report first
  beep(400);
}

/* ---- Serial command parsing ---------------------------------------------- */
void handleSerial() {
  char line[40];
  if (!readLine(line, sizeof(line))) return;

  if (strncmp(line, "OPEN,", 5) == 0) {
    char *p = line + 5;
    char *comma = strchr(p, ',');
    const char *mode = "borrow";
    if (comma) { *comma = '\0'; mode = comma + 1; }
    handleOpen((uint8_t) atoi(p), mode);
    return;
  }
  if (strcmp(line, "WHO") == 0)   { announce(); return; }
  if (strcmp(line, "ABORT") == 0) { return; }        // nothing is open
  if (strcmp(line, "SELFTEST") == 0) { selfTest(); return; }
#if HAS_RFID
  /* GAIN,<0-7> — tune receiver gain live. Too LOW and a tag out of range never
     answers (REQA Timeout); too HIGH and a tag pressed against the coil can
     overload the receiver, so the ATQA comes back malformed (REQA Error).
     The right value is hardware- and mounting-specific, so sweep it in place. */
  if (strncmp(line, "IDLESCAN,", 9) == 0) {
    idleScanEnabled = (atoi(line + 9) != 0);
    Serial.print(F("#idlescan=")); Serial.println(idleScanEnabled ? 1 : 0);
    return;
  }
  if (strncmp(line, "GAIN,", 5) == 0) {
    uint8_t g = (uint8_t) atoi(line + 5);
    if (g > 7) g = 7;
    rfid.PCD_SetAntennaGain(g << 4);
    rfid.PCD_AntennaOn();
    Serial.print(F("#gain set to ")); Serial.println((rfid.PCD_GetAntennaGain() >> 4) & 0x07);
    return;
  }
#endif
  Serial.print(F("ERR,")); Serial.println(line);
}

/* ========================================================================== */
void setup() {
  relaysSafeInit();                 // FIRST — before Serial/SPI init floats the pins

  Serial.begin(115200);

#if HAS_RFID
  SPI.begin();
  rfid.PCD_Init();
  /* Leave the receiver at the library default gain. Cranking it to RxGain_max
     was tried and made things WORSE: with a tag held against the coil the
     receiver overloads and the reply comes back malformed, which surfaces as
     "Error in communication" on REQA and a failed read. The stock reference
     sketch (firmware/rfid_read_test) reads reliably at the default, so match it.
     Use "GAIN,<0-7>" at runtime to experiment; don't hardcode a raise without
     measuring full reads, not just REQA. */
#endif
#if HAS_BUZZER
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
#endif

  /* Only claim the sensor pins once the sensors are actually wired. Until then
     leave them as high-Z inputs: if the cabinet is still on the OLD harness
     (relays were on D22-D25), driving D22/D24 LOW as a TRIG would energize
     those solenoids continuously. */
  if (SENSORS_ENABLED) {
    for (uint8_t i = 0; i < NUM_CABS; i++) {
      for (uint8_t s = 0; s < CABS[i].slots; s++) {
        if (CABS[i].slot[s].trig) { pinMode(CABS[i].slot[s].trig, OUTPUT); digitalWrite(CABS[i].slot[s].trig, LOW); }
        if (CABS[i].slot[s].echo) pinMode(CABS[i].slot[s].echo, INPUT);
      }
    }
  }

  beep(60, 2);
  announce();
}

void loop() {
  handleSerial();

#if HAS_RFID
  /* Idle tag reporting. Useful on its own (tap a tag any time to see its UID,
     no 20s window to race), and it is the hook the bridge will need once
     Mega 2 exists: that board has no reader, so a slot change reported there
     has to be paired with a SCAN seen here.
     Unsolicited SCAN lines are informational — the bridge only acts on DONE. */
  static char idleTag[32];
  static char lastIdleTag[32] = "";
  static unsigned long lastIdleScan = 0, lastIdleReport = 0;
  if (idleScanEnabled && millis() - lastIdleScan >= 200) {
    lastIdleScan = millis();
    if (readTagInto(idleTag, sizeof(idleTag))) {
      /* cardPresent() now wakes HALTed cards, so a tag parked on the reader
         reads on every pass. Report a given UID at most once every 2s. */
      bool repeat = (strcmp(idleTag, lastIdleTag) == 0) &&
                    (millis() - lastIdleReport < 2000);
      if (!repeat) {
        strncpy(lastIdleTag, idleTag, sizeof(lastIdleTag) - 1);
        lastIdleTag[sizeof(lastIdleTag) - 1] = '\0';
        lastIdleReport = millis();
        Serial.print(F("SCAN,")); Serial.println(idleTag);
        beep(40);
      }
    }
  }
#endif
}
