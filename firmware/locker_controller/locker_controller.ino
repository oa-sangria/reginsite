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
                 MOVED,<cabinet>,<slot>   (slot sensor saw the tool go/return;
                                           sent at once with slot 0 when the
                                           cabinet is running tag-only)
                 SCAN,<uid>               (tag read; inside a window it beeps)
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

/* Keep this TRUE. It makes setup() drive every TRIG LOW from boot, which these
   HC-SR04 clones require — a TRIG left floating and only claimed at read time
   leaves the module ignoring pulses. A cabinet whose sensors do not answer at
   open time automatically falls back to tag-only, so enabling is safe even for
   cabinets with no sensors wired yet. The `false` path only existed to protect
   the old D22/D23 relay harness, which is gone. */
const bool SENSORS_ENABLED = true;

/* BENCH_MODE — the ONE switch for bring-up behaviour. Set to 0 for the field.
   It is the default for the three flags below, which used to be flipped one by
   one and could be left half-done. announce() prints bench=<0|1> so the bridge
   log always shows which build is on the board. */
#define BENCH_MODE 1

/* Allows "SIMTAG,<uid>" over serial to stand in for a physical tag scan, so the
   whole chain (bridge -> OPEN -> DONE -> Laravel) can be exercised without a
   tool in hand. It bypasses the reader, so anyone with serial access could
   trigger a borrow — which is why it must be off in the field. */
const bool ALLOW_SIMTAG = BENCH_MODE;

/* Idle tag reporting (tap a tag any time to see its UID + a short beep),
   toggleable at runtime with "IDLESCAN,0|1".
   Off in the field on purpose: a student walking up with a tool to return hears
   that beep BEFORE tapping Return on the screen and assumes it already scanned.
   It MUST also be off while running RF diagnostics: the idle poll and a
   diagnostic REQA are two independent anticollision sequences aimed at the same
   tag, and they collide into a malformed ATQA that looks exactly like a hardware
   fault. Any measurement taken with this on is measuring the firmware. */
bool idleScanEnabled = BENCH_MODE;

/* SLOTDBG,0|1 — stream every slot sample during an OPEN window as
   "#slot,<cab>,<slot>,<cm>,<raw>,<filled>" so threshold problems are visible
   instead of inferred. Chatty (~16 lines/s), which is why the bridge drains its
   serial buffer in bulk; set false once slots are trusted. */
bool slotDebug = BENCH_MODE;

const uint8_t MAX_SLOTS = 4;

/* emptyCm: what this slot's sensor reads with NO tool in it (the shelf behind).
   0 = not measured yet -> the global PRESENT_CM/ABSENT_CM fallback applies. */
struct Slot    { uint8_t trig, echo; float emptyCm; };
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
  //  cab  relay  slots  {TRIG,ECHO,emptyCm} per slot A..D — emptyCm measured 2026-09-18, "not yet very accurate"
  {    1,  A12,   4, { {22,23,12.0}, {24,25,12.0}, {26,27,10.5}, {28,29,10.5}   } },  // USS 1-4   Pliers
  {    2,  A13,   4, { {30,31,14.0}, {32,33,14.0}, {34,35,10.5}, {36,37,10.5}   } },  // USS 5-8   Side Cutter
  {    3,    6,   4, { {38,39,13.5}, {40,41,13.5}, {42,43,11.5}, {44,45,11.5}   } },  // USS 9-12  Wire Crimper
  {    4,    7,   4, { {46,47,11.0}, {48,49,11.0}, {A0,A1,7.5},  {A2,A3,7.0}    } },  // USS 13-16 Clamp Meter
  {    5,    8,   4, { {A4,A5,11.0}, {A6,A7,11.5}, {A8,A9,9.0},  {A10,A11,9.0}  } },  // USS 17-20 Multimeter
};
#else
const uint8_t NUM_CABS = 5;
/* Relays on A0-A4, NOT the wiring doc's D50-D53: those four are the SPI bus
   (MISO/MOSI/SCK/SS). If SPI.begin() ever gets compiled into this build — a
   future SD card, a second RC522, an attached ICSP programmer — it would drive
   SCK/MOSI as outputs and fire cabinets 7 and 8. A0-A15 are otherwise unused
   on this board, so the SPI bus stays free. */
const Cabinet CABS[NUM_CABS] = {
  {    6,   A0,   4, { {22,23,8.5},  {24,25,8.5},  {26,27,6.5},  {28,29,6.5}  } },  // USS 21-24 Screwdriver Set
  {    7,   A1,   4, { {30,31,12.0}, {32,33,12.0}, {34,35,10.0}, {36,37,10.0} } },  // USS 25-28 Wire Stripper
  {    8,   A2,   4, { {38,39,12.5}, {40,41,12.5}, {42,43,10.0}, {44,45,10.0} } },  // USS 29-32 Soldering Iron
  {    9,   A3,   1, { {46,47,7.0} } },                                             // USS 33  Makita Drill A
  {   10,   A4,   1, { {48,49,7.0} } },                                             // USS 34  Makita Drill B
};
#endif

const bool  ACTIVE_LOW = true;                 // relay board polarity
/* Per-slot thresholds, derived from each slot's measured empty-shelf distance
   (Slot.emptyCm in the tables above):
     ABSENT  when  d >= emptyCm - ABSENT_MARGIN_CM    (reading is "the shelf")
     PRESENT when  d <= emptyCm - PRESENT_MARGIN_CM   (something is well in front of it)
   Between the two the slot holds its previous state (hysteresis). The margins
   are the two numbers to tune once the distances are trusted: raise
   PRESENT_MARGIN if empty slots flicker "in"; lower it if a tool sitting close
   to the shelf never registers. The shelf reads 6.5-14cm across the cabinets
   and a tool body ~4cm (Locker 2 slot 1, 2026-09-15: IN 4.0, 3.9-4.4), so a
   fixed pair could not fit every slot — the shallow ones would never read
   empty. A tool too small to reach the beam reads identically in and out;
   the sensor must see the tool's BODY, not the shelf beside it.
   Slots with emptyCm = 0 (not measured) fall back to the global pair. */
const float ABSENT_MARGIN_CM  = 1.0;
const float PRESENT_MARGIN_CM = 2.0;
const float PRESENT_CM = 5.9;                  // fallback: <= this = tool in the slot
const float ABSENT_CM  = 7.4;                  // fallback: >= this = slot empty
const uint8_t       AGREE_N          = 2;      // samples that must agree to flip a slot
const uint8_t       MISS_LIMIT       = 3;      // consecutive no-echoes before flagging
const unsigned long ECHO_TIMEOUT_US  = 12000;  // ~2 m; a dead sensor costs 12ms not 30
const uint8_t       SENSOR_SETTLE_MS = 60;     // HC-SR04 datasheet measurement cycle
/* How long a door stays unlocked waiting for lift + tag. 20s was tight for a
   first-timer hunting for the tag; 45s. The bridge's STALE_AFTER must stay
   above this (OPEN_WINDOW_S in bridge.py) — change both together. The kiosk's
   Cancel relocks early via ABORT, so the length only matters when nobody acts. */
const unsigned long OPEN_TIMEOUT_MS  = 45000;

/* ---- Cabinet lookup ------------------------------------------------------- */
int8_t cabIndex(uint8_t cabNumber) {
  for (uint8_t i = 0; i < NUM_CABS; i++) if (CABS[i].number == cabNumber) return i;
  return -1;
}

/* ---- Relay ---------------------------------------------------------------- */
inline uint8_t relayIdle()   { return ACTIVE_LOW ? HIGH : LOW; }
inline uint8_t relayActive() { return ACTIVE_LOW ? LOW  : HIGH; }

/* relayPin 0 means "no relay wired" — never touch pin 0, it is serial RX. */
void lockCabinet(uint8_t i)   { if (CABS[i].relayPin) digitalWrite(CABS[i].relayPin, relayIdle());   }
void unlockCabinet(uint8_t i) { if (CABS[i].relayPin) digitalWrite(CABS[i].relayPin, relayActive()); }

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
    if (!p) continue;                       // no relay on this cabinet
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
  if (d < 0) {
    if (st[s].miss < 255) st[s].miss++;
    if (slotDebug) { Serial.print(F("#slot,")); Serial.print(c.number); Serial.print(','); Serial.print(s + 1); Serial.println(F(",none")); }
    return;
  }
  st[s].miss = 0;

  float empty     = c.slot[s].emptyCm;
  float presentAt = empty > 0 ? empty - PRESENT_MARGIN_CM : PRESENT_CM;
  float absentAt  = empty > 0 ? empty - ABSENT_MARGIN_CM  : ABSENT_CM;

  bool raw; char rawc;
  if      (d <= presentAt) { raw = true;  rawc = 'P'; }
  else if (d >= absentAt)  { raw = false; rawc = 'A'; }
  else                     { rawc = '-'; }

  if (rawc != '-') {
    if (raw == st[s].cand) {
      if (st[s].agree < AGREE_N) st[s].agree++;
      if (st[s].agree >= AGREE_N) st[s].filled = raw;
    } else {
      st[s].cand = raw;
      st[s].agree = 1;
    }
  }
  if (slotDebug) {
    Serial.print(F("#slot,")); Serial.print(c.number); Serial.print(','); Serial.print(s + 1);
    Serial.print(','); Serial.print(d, 1); Serial.print(','); Serial.print(rawc);
    Serial.print(F(",filled=")); Serial.println(st[s].filled ? 1 : 0);
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

/* Clone RC522s go deaf: after EMI from a solenoid switching, or hours idle, the
   chip stops answering (VersionReg reads 0x00/0xFF) or silently drops the
   antenna drive — and nothing short of a soft reset brings it back. PCD_Init()
   IS that soft reset (~50-100ms), so run it at the top of every OPEN window,
   where a dead reader costs a 20s timeout, and from a periodic health check
   while idle. Prints the version byte so the bridge log shows whether the
   chip was alive at the moment that matters. Resets antenna gain to the
   library default (what setup() uses); see the GAIN command. */
void rfidReinit(const __FlashStringHelper *why) {
  rfid.PCD_Init();
  rfid.PCD_AntennaOn();
  uint8_t v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("#rc522 reinit (")); Serial.print(why); Serial.print(F(") v=0x"));
  if (v < 0x10) Serial.print('0');
  Serial.println(v, HEX);
}

/* True when the chip is answering AND both antenna drivers are on. Two register
   reads; cheap enough to call every few seconds. */
bool rfidHealthy() {
  uint8_t v  = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  uint8_t tx = rfid.PCD_ReadRegister(MFRC522::TxControlReg);
  return (v != 0x00 && v != 0xFF) && ((tx & 0x03) == 0x03);
}

/* No String anywhere: polled every loop pass, a String-based reader would churn
   thousands of heap allocations per open window on an 8KB heap. Returns false
   without allocating in the overwhelmingly common no-card case.

   WUPA, not REQA. REQA only answers cards in IDLE state, and every successful
   read ends with PICC_HaltA(), so a tag left sitting on the reader (or read by
   the idle scan a moment ago) is HALTed and invisible to REQA — it would have
   to be lifted and re-tapped. WUPA answers IDLE *and* HALT (ISO 14443-3), so it
   is a strict superset; the old REQA-then-WUPA pair just paid the library's
   25ms no-answer timeout twice per poll. One timeout (~25ms) is the cost of a
   no-card pass now, which is what sets the loop cadence in handleOpen(). */
bool cardPresent() {
  byte atqa[2];
  byte size = sizeof(atqa);
  MFRC522::StatusCode s = rfid.PICC_WakeupA(atqa, &size);
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
  Serial.print(F(" sensors=")); Serial.print(SENSORS_ENABLED ? 1 : 0);
  Serial.print(F(" bench=")); Serial.println(BENCH_MODE);
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

  /* Probe REQA and WUPA separately — diagnostics only; the real read path is
     WUPA alone (see cardPresent). Every successful read ends in PICC_HaltA(),
     and a HALTed card ignores REQA forever, so "REQA Timeout, WUPA OK" against
     a tag sitting on the coil is NORMAL, not a fault. Both timing out with a
     tag present is the fault. */
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

/* ---- USS: read every defined sensor once, for bring-up validation --------- */
void ussScan() {
  Serial.println(F("#uss scan"));
  for (uint8_t i = 0; i < NUM_CABS; i++) {
    for (uint8_t s = 0; s < CABS[i].slots; s++) {
      const Slot &sl = CABS[i].slot[s];
      if (!sl.trig || !sl.echo) continue;

      pinMode(sl.trig, OUTPUT); digitalWrite(sl.trig, LOW);
      pinMode(sl.echo, INPUT);
      delay(5);
      float d = pingCm(sl);

      Serial.print(F("USS,")); Serial.print(CABS[i].number);
      Serial.print(',');       Serial.print(s + 1);
      Serial.print(',');       Serial.print(sl.trig);
      Serial.print('/');       Serial.print(sl.echo);
      Serial.print(',');
      if (d < 0) Serial.print(F("none")); else Serial.print(d, 1);
      Serial.print(F(",empty=")); Serial.println(sl.emptyCm, 1);   // what the table expects

      /* While sensors are not yet enabled, put the pins back exactly as
         relaysSafeInit() left them: D22/D23 parked at the relay idle level in
         case the old harness is still on them, everything else high-Z. */
      if (!SENSORS_ENABLED) {
        uint8_t pins[2] = { sl.trig, sl.echo };
        for (uint8_t k = 0; k < 2; k++) {
          if (pins[k] == 22 || pins[k] == 23) { pinMode(pins[k], OUTPUT); digitalWrite(pins[k], relayIdle()); }
          else                                 { pinMode(pins[k], INPUT); }
        }
      }
      delay(SENSOR_SETTLE_MS);
    }
  }
  Serial.println(F("USS,done"));
}

/* ---- Handle one OPEN command --------------------------------------------- */
void handleOpen(uint8_t cabNum, const char *mode) {
  int8_t ci = cabIndex(cabNum);
  if (ci < 0 || CABS[ci].relayPin == 0) {        // other board's cabinet, or no relay wired
    Serial.print(F("NOWIRE,")); Serial.println(cabNum);
    beep(50, 3);
    return;
  }
  const Cabinet &c = CABS[ci];
  bool wantFill    = (strcmp(mode, "return") == 0);
  bool haveSensors = SENSORS_ENABLED && c.slots > 0 && c.slot[0].trig != 0;

#if HAS_RFID
  /* Fresh reader for the one window where it has to work. ~50-100ms, before the
     door moves, so the student never notices; the printed version byte tells
     the bridge log the chip was alive when the window opened. */
  rfidReinit(F("open"));
#endif

  SlotState st[MAX_SLOTS];
  bool baseline[MAX_SLOTS];
  if (haveSensors) {
    baselineSweep(c, st);
    /* If not one slot answered a single ping, this cabinet's sensors are not
       wired (or dead). Degrade to tag-only rather than demanding a slot change
       that can never be seen — otherwise every borrow here would time out. */
    bool anyAlive = false;
    for (uint8_t s = 0; s < c.slots; s++) if (st[s].miss < AGREE_N) anyAlive = true;
    if (!anyAlive) {
      haveSensors = false;
      Serial.print(F("#cab ")); Serial.print(cabNum);
      Serial.println(F(": no sensor echo - confirming on tag only"));
    } else {
      for (uint8_t s = 0; s < c.slots; s++) baseline[s] = st[s].filled;
      if (slotDebug) {
        Serial.print(F("#baseline,")); Serial.print(cabNum);
        for (uint8_t s = 0; s < c.slots; s++) { Serial.print(','); Serial.print(baseline[s] ? F("IN") : F("out")); }
        Serial.println();
      }
    }
  }

  unlockCabinet(ci);
  Serial.print(F("OPENED,")); Serial.println(cabNum);   // report first, beep after
  if (!haveSensors) {
    /* Tag-only fallback: the slot condition is already considered met, so tell
       the kiosk so it moves straight to "tap the tag" (slot 0 = unknown). */
    Serial.print(F("MOVED,")); Serial.print(cabNum); Serial.println(F(",0"));
  }
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
    /* 1) RFID every pass. A no-card pass costs one library timeout (~25ms), so
          this loop runs at roughly 30ms/pass and the reader is polled ~30x/s —
          an RC522 polled slowly misses a tag that is tapped and lifted in under
          300ms, which people do constantly. That same ~30ms is why the sensor
          check below fires every other pass rather than exactly every 60ms;
          with 4 slots and AGREE_N=2, a removal registers in roughly 0.5s.
          (It used to be ~50ms/pass and ~1s, when cardPresent did REQA+WUPA.) */
#if HAS_RFID
    if (!haveTag && readTagInto(tag, sizeof(tag))) {
      haveTag = true;
      Serial.print(F("SCAN,")); Serial.println(tag);
      /* The student's only proof the tap registered. Reads can be slow on this
         reader, so the kiosk tells them to hold the tag until this beep. Short,
         so a DONE that follows at once is not delayed by much. */
      beep(40);
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
          /* Informational, for the kiosk's step rail ("lift the tool" done). */
          Serial.print(F("MOVED,")); Serial.print(cabNum); Serial.print(','); Serial.println(s + 1);
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
  if (strcmp(line, "USS") == 0)      { ussScan();  return; }
  /* SCANECHO — find pins that have a sensor ECHO on them. An HC-SR04 actively
     holds ECHO LOW between pings; an unconnected pin floats and a pull-up reads
     it HIGH. So INPUT_PULLUP + read LOW means "something is driving this pin".
     Never drives anything, so it is safe on every pin we probe. Skips serial,
     SPI, the relay pins and the buzzer. */
  if (strcmp(line, "SCANECHO") == 0) {
    Serial.println(F("#scanecho"));
    for (uint8_t p = 2; p <= 69; p++) {
      if (p == 5 || p == 6 || p == 7 || p == 8 || p == 16) continue;      // RST, relays, buzzer
      if (p >= 50 && p <= 53) continue;                                    // SPI
      if (p == 66 || p == 67) continue;                                    // A12/A13 relays
      pinMode(p, INPUT_PULLUP);
      delayMicroseconds(200);
      int v = digitalRead(p);
      if (!SENSORS_ENABLED && (p == 22 || p == 23)) { pinMode(p, OUTPUT); digitalWrite(p, relayIdle()); }
      else pinMode(p, INPUT);
      if (v == LOW) { Serial.print(F("ECHOPIN,")); Serial.println(p); }
    }
    Serial.println(F("SCANECHO,done"));
    return;
  }
  /* SCANALL — like SCANECHO but skips only D0/D1, so a sensor wire that landed
     on a relay, SPI, RST or buzzer pin by mistake still shows up. Read-only
     with a pull-up (HIGH = relay off), and every pin is put back afterwards. */
  if (strcmp(line, "SCANALL") == 0) {
    Serial.println(F("#scanall"));
    for (uint8_t p = 2; p <= 69; p++) {
      pinMode(p, INPUT_PULLUP);
      delayMicroseconds(200);
      int v = digitalRead(p);
      if (v == LOW) { Serial.print(F("LOWPIN,")); Serial.println(p); }
    }
    relaysSafeInit();                                   // relays back to idle OUTPUT
#if HAS_BUZZER
    pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
#endif
#if HAS_RFID
    SPI.begin(); rfidReinit(F("scanall"));              // SPI pins back to the bus
#endif
    for (uint8_t i = 0; i < NUM_CABS; i++)
      for (uint8_t s = 0; s < CABS[i].slots; s++) {
        if (CABS[i].slot[s].trig) { pinMode(CABS[i].slot[s].trig, OUTPUT); digitalWrite(CABS[i].slot[s].trig, LOW); }
        if (CABS[i].slot[s].echo) pinMode(CABS[i].slot[s].echo, INPUT);
      }
    Serial.println(F("SCANALL,done"));
    return;
  }
  /* TRACE,<trig>,<echo> — trigger once and record what ECHO actually does for
     the next 60ms. Separates "never fired" (ECHO flat) from "fired, nothing in
     range" (ECHO rose and stayed up), which pulseIn reports identically. */
  if (strncmp(line, "TRACE,", 6) == 0) {
    char *p = line + 6; char *c = strchr(p, ',');
    if (!c) { Serial.println(F("ERR,TRACE needs trig,echo")); return; }
    *c = '\0';
    uint8_t t = (uint8_t) atoi(p), e = (uint8_t) atoi(c + 1);
    if (!t || !e) { Serial.println(F("ERR,bad pins")); return; }
    pinMode(t, OUTPUT); digitalWrite(t, LOW);
    pinMode(e, INPUT);
    delay(5);
    int before = digitalRead(e);
    digitalWrite(t, HIGH); delayMicroseconds(10); digitalWrite(t, LOW);
    unsigned long t0 = micros(), rose = 0, fell = 0;
    int last = digitalRead(e);
    while (micros() - t0 < 60000UL) {
      int v = digitalRead(e);
      if (v != last) {
        if (v == HIGH && !rose) rose = micros() - t0;
        if (v == LOW  && rose && !fell) { fell = micros() - t0; break; }
        last = v;
      }
    }
    int after = digitalRead(e);
    Serial.print(F("TRACE,")); Serial.print(t); Serial.print('/'); Serial.print(e);
    Serial.print(F(",before=")); Serial.print(before);
    Serial.print(F(",rose_us=")); Serial.print(rose);
    Serial.print(F(",fell_us=")); Serial.print(fell);
    Serial.print(F(",after=")); Serial.print(after);
    if (!rose)            Serial.println(F(",NEVER_FIRED"));
    else if (!fell)       Serial.println(F(",FIRED_NO_RETURN (echo still high at 60ms)"));
    else { Serial.print(F(",echo=")); Serial.print((fell - rose) * 0.0343 / 2.0, 1); Serial.println(F("cm")); }
    digitalWrite(t, LOW);                  // keep TRIG driven — see PING
    return;
  }
  /* HOLD,<pin>,<0|1> — drive a pin and leave it there, so a meter can be put
     on the far end of the wire. HOLD,<pin>,x releases it back to input. */
  if (strncmp(line, "HOLD,", 5) == 0) {
    char *p = line + 5; char *c = strchr(p, ',');
    if (!c) { Serial.println(F("ERR,HOLD needs pin,0|1|x")); return; }
    *c = '\0';
    uint8_t pin = (uint8_t) atoi(p);
    if (pin < 2 || pin > 69 || pin == 5 || (pin >= 50 && pin <= 53)) { Serial.println(F("ERR,unsafe pin")); return; }
    char mode = c[1];
    if (mode == 'x') { pinMode(pin, INPUT); Serial.print(F("HOLD,")); Serial.print(pin); Serial.println(F(",released")); return; }
    pinMode(pin, OUTPUT); digitalWrite(pin, mode == '1' ? HIGH : LOW);
    Serial.print(F("HOLD,")); Serial.print(pin); Serial.println(mode == '1' ? F(",HIGH") : F(",LOW"));
    return;
  }
  /* LINK,<a>,<b> — are two pins shorted together? Drive <a> HIGH then LOW and
     watch whether <b> (as input) follows it. A sensor between them presents
     high impedance so <b> stays put; a short drags it along. */
  if (strncmp(line, "LINK,", 5) == 0) {
    char *p = line + 5; char *c = strchr(p, ',');
    if (!c) { Serial.println(F("ERR,LINK needs a,b")); return; }
    *c = '\0';
    uint8_t a = (uint8_t) atoi(p), b = (uint8_t) atoi(c + 1);
    if (a < 2 || a > 69 || b < 2 || b > 69 || a == b || (a >= 50 && a <= 53)) { Serial.println(F("ERR,bad pins")); return; }
    pinMode(b, INPUT);
    pinMode(a, OUTPUT);
    digitalWrite(a, HIGH); delayMicroseconds(50); int hi = digitalRead(b);
    digitalWrite(a, LOW);  delayMicroseconds(50); int lo = digitalRead(b);
    pinMode(a, INPUT);
    Serial.print(F("LINK,")); Serial.print(a); Serial.print(','); Serial.print(b);
    Serial.print(F(",b_when_a_high=")); Serial.print(hi);
    Serial.print(F(",b_when_a_low="));  Serial.print(lo);
    Serial.println((hi == 1 && lo == 0) ? F(",SHORTED") : F(",independent"));
    return;
  }
  /* PULSE,<pin>,<ms> — drive one pin LOW for <ms> then back HIGH and release.
     For locating a relay IN wire: pulse a candidate and listen for the click. */
  if (strncmp(line, "PULSE,", 6) == 0) {
    char *p = line + 6; char *c = strchr(p, ',');
    uint8_t pin = (uint8_t) atoi(p);
    int ms = c ? atoi(c + 1) : 500;
    if (pin < 2 || pin > 69 || pin == 5 || (pin >= 50 && pin <= 53)) { Serial.println(F("ERR,unsafe pin")); return; }
    if (ms < 20) ms = 20; if (ms > 3000) ms = 3000;
    digitalWrite(pin, HIGH); pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);  delay(ms);
    digitalWrite(pin, HIGH); delay(20);
    pinMode(pin, INPUT);
    Serial.print(F("PULSE,")); Serial.print(pin); Serial.println(F(",done"));
    return;
  }
  /* PING,<trig>,<echo> — read one arbitrary pair. Finds sensors wired to pins
     the table doesn't expect (e.g. the old bench order with ECHO below TRIG). */
  if (strncmp(line, "PING,", 5) == 0) {
    char *p = line + 5; char *c = strchr(p, ',');
    if (!c) { Serial.println(F("ERR,PING needs trig,echo[,pulse_us]")); return; }
    *c = '\0';
    char *c2 = strchr(c + 1, ',');
    unsigned int pulseUs = 10;
    if (c2) { *c2 = '\0'; pulseUs = (unsigned int) atoi(c2 + 1); if (pulseUs < 5) pulseUs = 5; if (pulseUs > 5000) pulseUs = 5000; }
    Slot sl = { (uint8_t) atoi(p), (uint8_t) atoi(c + 1) };
    if (!sl.trig || !sl.echo) { Serial.println(F("ERR,bad pins")); return; }
    pinMode(sl.trig, OUTPUT); digitalWrite(sl.trig, LOW);
    pinMode(sl.echo, INPUT);
    delay(5);
    /* Optional third arg: trigger pulse width. HC-SR04 spec is 10us; some
       clone chips need much longer, and the only way to find out is to try. */
    float d;
    if (pulseUs == 10) d = pingCm(sl);
    else {
      digitalWrite(sl.trig, LOW);  delayMicroseconds(2);
      digitalWrite(sl.trig, HIGH); delayMicroseconds(pulseUs);
      digitalWrite(sl.trig, LOW);
      unsigned long dur = pulseIn(sl.echo, HIGH, 30000);
      d = dur == 0 ? -1.0 : dur * 0.0343 / 2.0;
    }
    Serial.print(F("PING,")); Serial.print(sl.trig); Serial.print('/'); Serial.print(sl.echo);
    Serial.print(',');
    if (d < 0) Serial.println(F("none")); else Serial.println(d, 1);
    /* Leave TRIG driven LOW. Releasing it to a floating input between reads
       puts these HC-SR04 clones into a state where they ignore the next pulse —
       which made this very command report "no echo" on perfectly good sensors. */
    digitalWrite(sl.trig, LOW);
    return;
  }
  /* SLOTDBG,0|1 — sensor sample stream on/off. Not RFID-related, so it lives
     outside the HAS_RFID block and works on controller 2 as well. */
  if (strncmp(line, "SLOTDBG,", 8) == 0) {
    slotDebug = (atoi(line + 8) != 0);
    Serial.print(F("#slotdbg=")); Serial.println(slotDebug ? 1 : 0);
    return;
  }
#if HAS_RFID
  if (strncmp(line, "IDLESCAN,", 9) == 0) {
    idleScanEnabled = (atoi(line + 9) != 0);
    Serial.print(F("#idlescan=")); Serial.println(idleScanEnabled ? 1 : 0);
    return;
  }
  /* GAIN,<0-7> — tune receiver gain live. Too LOW and a tag out of range never
     answers (REQA Timeout); too HIGH and a tag pressed against the coil can
     overload the receiver, so the ATQA comes back malformed (REQA Error).
     The right value is hardware- and mounting-specific, so sweep it in place.
     NOTE: rfidReinit() at each OPEN puts the gain back to the library default,
     so a GAIN value found here has to be hardcoded in setup() to stick. */
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
  rfidReinit(F("boot"));
  /* Leave the receiver at the library default gain. Cranking it to RxGain_max
     was tried and made things WORSE: with a tag held against the coil the
     receiver overloads and the reply comes back malformed, which surfaces as
     "Error in communication" on REQA and a failed read. The stock reference
     sketch (firmware/rfid_read_test) reads reliably at the default, so match it.
     Use "GAIN,<0-7>" at runtime to experiment; don't hardcode a raise without
     measuring full reads, not just REQA.
     One init at boot is NOT enough on its own — see rfidReinit(): the chip is
     re-initialised at every OPEN and whenever the idle health check finds it
     deaf, because clone RC522s lock up and stay locked up. */
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
  /* Reader health check. A chip that has gone deaf while idle would otherwise
     only be noticed by the next student, as a 20s timeout. Two register reads
     every 10s; reinit only when they say the chip or the antenna is gone. */
  static unsigned long lastHealth = 0;
  if (millis() - lastHealth >= 10000) {
    lastHealth = millis();
    if (!rfidHealthy()) rfidReinit(F("health"));
  }

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
