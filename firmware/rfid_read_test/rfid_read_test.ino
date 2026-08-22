/* ============================================================================
   reginsite — RFID Read Test  (Arduino Mega 2560)
   ----------------------------------------------------------------------------
   Minimal sketch: just prints the UID of any RFID card/tag you scan.
   Use it to re-read a tag whose UID has stopped matching, then correct it on
   the Inventory page (tools.rfid_tag). All 34 tags are already seeded.

   Wiring (RC522 -> Mega 1):
     SS/SDA = 53   SCK = 52   MOSI = 51   MISO = 50   RST = 5
     VCC = 3.3V (NOT 5V)      GND = GND
   NOTE: RST moved 9 -> 5 in the two-controller rewire; D9 is now free and the
   buzzer lives on D16. The RC522 is on Mega 1 only.

   Library: MFRC522 (Arduino IDE > Library Manager > "MFRC522")
   Serial Monitor: 115200 baud
   ============================================================================ */

#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN  53
#define RST_PIN 5

MFRC522 rfid(SS_PIN, RST_PIN);

/* Relay pins are held OFF while this sketch runs.
   This test has nothing to do with the solenoids, but a pin it never configures
   stays high-impedance, and a floating input on an ACTIVE-LOW relay board can
   drift low and energize the solenoid for as long as the sketch is loaded.
   Both pin sets are listed so this is safe whether the rig is still on the old
   bench wiring (IN1=D22, IN2=D23) or already moved to the production pins. */
const uint8_t RELAY_PINS[] = { 22, 23, A12, A13 };
const bool    ACTIVE_LOW   = true;

void holdRelaysOff() {
  for (uint8_t i = 0; i < sizeof(RELAY_PINS) / sizeof(RELAY_PINS[0]); i++) {
    uint8_t p = RELAY_PINS[i];
    digitalWrite(p, ACTIVE_LOW ? HIGH : LOW);   // still INPUT -> enables the pull-up
    pinMode(p, OUTPUT);                         // latches that level, no LOW glitch
    digitalWrite(p, ACTIVE_LOW ? HIGH : LOW);
  }
}

void setup() {
  holdRelaysOff();                              // FIRST, before anything slow
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  Serial.println();
  Serial.println("=================================");
  Serial.println(" RFID READ TEST");
  Serial.println("=================================");
  Serial.print("RC522 Version : ");
  rfid.PCD_DumpVersionToSerial();   // 0x92 / 0x91 = OK, 0x00 / 0xFF = wiring problem
  Serial.println("Scan a tag...");
  Serial.println();
}

void loop() {
  // Nothing new on the reader? do nothing.
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  // Build the UID as uppercase hex, space-separated (e.g. "DE AD BE EF").
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += " ";
  }
  uid.toUpperCase();

  Serial.print("UID: ");
  Serial.println(uid);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(1000);   // avoid spamming while a tag is held on the reader
}
