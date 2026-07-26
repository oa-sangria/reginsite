/* ============================================================================
   reginsite — RFID Read Test  (Arduino Mega 2560)
   ----------------------------------------------------------------------------
   Minimal sketch: just prints the UID of any RFID card/tag you scan.
   Use it to read your 4 tag UIDs, then paste them into the tagUID[] table
   in locker_controller.ino.

   Wiring (RC522 -> Mega):
     SS/SDA = 53   SCK = 52   MOSI = 51   MISO = 50   RST = 9
     VCC = 3.3V (NOT 5V)      GND = GND

   Library: MFRC522 (Arduino IDE > Library Manager > "MFRC522")
   Serial Monitor: 115200 baud
   ============================================================================ */

#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN  53
#define RST_PIN 9

MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
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
