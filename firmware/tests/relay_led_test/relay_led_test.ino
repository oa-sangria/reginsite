/* ============================================================================
   reginsite — TEST: Relay & LED mapper  (Arduino Mega 2560)
   ----------------------------------------------------------------------------
   Purpose: figure out WHICH Arduino pin drives WHICH locker's solenoid/LED,
   and the relay POLARITY (active-low vs active-high). Feed the results back
   and I'll drop them into locker_controller.ino.

   Open Serial Monitor @115200, set line ending to "Newline", then type:

     22            -> toggle pin 22 (watch which solenoid clicks / LED lights)
     sweep         -> pulse pins 22..53 one at a time (each ~800ms) so you can
                      note which locker each pin controls
     alllow        -> drive every test pin LOW
     allhigh       -> drive every test pin HIGH
     help          -> show this menu

   POLARITY: after a toggle, note whether the solenoid UNLOCKS on HIGH or LOW.
   If it unlocks on LOW, your board is ACTIVE-LOW (set ACTIVE_LOW=true in the
   main firmware — which is the current default).
   ============================================================================ */

// Pins that are safe to test as outputs (skip 0/1 = USB serial).
const uint8_t FIRST_PIN = 22;
const uint8_t LAST_PIN  = 53;

bool state[70];

void setPin(uint8_t p, bool high) {
  pinMode(p, OUTPUT);
  digitalWrite(p, high ? HIGH : LOW);
  state[p] = high;
  Serial.print("pin "); Serial.print(p); Serial.println(high ? " = HIGH" : " = LOW");
}

void menu() {
  Serial.println();
  Serial.println("== Relay/LED mapper ==");
  Serial.println("Type a pin number to toggle it, or: sweep | alllow | allhigh | help");
}

void setup() {
  Serial.begin(115200);
  for (uint8_t p = FIRST_PIN; p <= LAST_PIN; p++) { pinMode(p, OUTPUT); digitalWrite(p, LOW); state[p] = false; }
  menu();
}

void loop() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "help") { menu(); return; }
  if (cmd == "alllow")  { for (uint8_t p = FIRST_PIN; p <= LAST_PIN; p++) setPin(p, false); return; }
  if (cmd == "allhigh") { for (uint8_t p = FIRST_PIN; p <= LAST_PIN; p++) setPin(p, true);  return; }
  if (cmd == "sweep") {
    Serial.println("Sweeping " + String(FIRST_PIN) + ".." + String(LAST_PIN) + " — note which locker reacts:");
    for (uint8_t p = FIRST_PIN; p <= LAST_PIN; p++) {
      Serial.print("  -> pin "); Serial.println(p);
      digitalWrite(p, HIGH); delay(800); digitalWrite(p, LOW); delay(250);
    }
    Serial.println("sweep done.");
    return;
  }
  // Otherwise treat it as a pin number to toggle.
  int p = cmd.toInt();
  if (p >= FIRST_PIN && p <= LAST_PIN) setPin(p, !state[p]);
  else Serial.println("Enter a pin " + String(FIRST_PIN) + ".." + String(LAST_PIN) + ", or: sweep/alllow/allhigh/help");
}
