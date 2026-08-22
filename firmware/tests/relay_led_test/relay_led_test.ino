/* ============================================================================
   reginsite — TEST: Relay mapper  (Arduino Mega 2560)
   ----------------------------------------------------------------------------
   Purpose: confirm WHICH Arduino pin opens WHICH cabinet, and the relay
   POLARITY (active-low vs active-high).

   >>> SAFETY — why this sketch no longer sweeps a pin RANGE <<<
   In the two-Mega production wiring, D22-D49 are ultrasonic TRIG *and ECHO*
   lines and D50-D53 are the RC522 SPI bus. An HC-SR04 actively drives its ECHO
   pin, and the RC522 actively drives MISO. Driving those pins as outputs puts
   two push-pull drivers in opposition and can damage either device. So this
   sketch only ever touches the RELAY pins listed below.

   Open Serial Monitor @115200, line ending "Newline", then type:

     1..5          -> toggle that CABINET's relay
     a12 / 6       -> toggle by pin name/number (must be a relay pin)
     sweep         -> pulse each relay ON ~800ms, one at a time, in order
     alloff        -> drive every relay to its DE-ENERGIZED state
     help          -> show this menu

   POLARITY: if a cabinet unlocks when the pin reads LOW, the board is
   ACTIVE-LOW -> keep ACTIVE_LOW = true here and in locker_controller.ino.
   ============================================================================ */

const bool ACTIVE_LOW = true;   // relay board polarity — must match the main firmware

/* Relay pin per cabinet. Mega 1 owns cabinets 1-5.
   Mega 2 (cabinets 6-10) uses: { A0, A1, A2, A3, A4 } — swap the line below.
   A0..A15 == D54..D69 on the Mega; all are full digital I/O. */
const uint8_t RELAY_PINS[] = { A12, A13, 6, 7, 8 };
const uint8_t FIRST_CABINET = 1;

const uint8_t N_RELAYS = sizeof(RELAY_PINS) / sizeof(RELAY_PINS[0]);
bool energized[N_RELAYS];

uint8_t idleLevel()   { return ACTIVE_LOW ? HIGH : LOW; }
uint8_t activeLevel() { return ACTIVE_LOW ? LOW  : HIGH; }

void applyRelay(uint8_t i, bool on) {
  digitalWrite(RELAY_PINS[i], on ? activeLevel() : idleLevel());
  energized[i] = on;
  Serial.print("cabinet "); Serial.print(FIRST_CABINET + i);
  Serial.print("  (pin ");  Serial.print(RELAY_PINS[i]);
  Serial.print(") -> ");    Serial.print(on ? "ENERGIZED" : "off");
  Serial.print("  pin is "); Serial.println((on ? activeLevel() : idleLevel()) == HIGH ? "HIGH" : "LOW");
}

int8_t indexForPin(uint8_t p) {
  for (uint8_t i = 0; i < N_RELAYS; i++) if (RELAY_PINS[i] == p) return i;
  return -1;
}

void menu() {
  Serial.println();
  Serial.println("== Relay mapper ==");
  Serial.print("Relay pins: ");
  for (uint8_t i = 0; i < N_RELAYS; i++) {
    Serial.print("cab"); Serial.print(FIRST_CABINET + i);
    Serial.print("=");   Serial.print(RELAY_PINS[i]);
    if (i < N_RELAYS - 1) Serial.print(", ");
  }
  Serial.println();
  Serial.print("Polarity: ACTIVE_"); Serial.println(ACTIVE_LOW ? "LOW" : "HIGH");
  Serial.println("Type a cabinet number, a relay pin number, or: sweep | alloff | help");
}

void setup() {
  /* De-energized level FIRST, while the pin is still an input: writing a HIGH
     to an input enables the internal pull-up, so the subsequent pinMode(OUTPUT)
     latches HIGH instead of glitching LOW. With ACTIVE_LOW relays, a LOW glitch
     is a solenoid firing. Same ordering as relaysSafeInit() in the firmware. */
  for (uint8_t i = 0; i < N_RELAYS; i++) {
    digitalWrite(RELAY_PINS[i], idleLevel());
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], idleLevel());
    energized[i] = false;
  }
  Serial.begin(115200);
  menu();
}

void loop() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;

  if (cmd == "help") { menu(); return; }

  if (cmd == "alloff") {
    for (uint8_t i = 0; i < N_RELAYS; i++) applyRelay(i, false);
    return;
  }

  if (cmd == "sweep") {
    Serial.println("Sweeping relays — note which cabinet opens for each:");
    for (uint8_t i = 0; i < N_RELAYS; i++) {
      applyRelay(i, true);  delay(800);
      applyRelay(i, false); delay(250);
    }
    Serial.println("sweep done.");
    return;
  }

  // "a12" -> analog pin 12
  int8_t idx = -1;
  if (cmd.startsWith("a")) {
    int a = cmd.substring(1).toInt();
    if (a >= 0 && a <= 15) idx = indexForPin(A0 + a);
  } else {
    int n = cmd.toInt();
    // A bare 1..N_RELAYS means a cabinet number; anything else is a raw pin.
    if (n >= FIRST_CABINET && n < FIRST_CABINET + N_RELAYS) idx = n - FIRST_CABINET;
    else if (n > 0) idx = indexForPin((uint8_t) n);
  }

  if (idx >= 0) applyRelay(idx, !energized[idx]);
  else Serial.println("Not a relay pin. Type a cabinet number, a relay pin, or: sweep/alloff/help");
}
