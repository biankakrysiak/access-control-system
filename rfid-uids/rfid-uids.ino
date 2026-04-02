#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN   8
#define RST_PIN  7

MFRC522 rfid(SS_PIN, RST_PIN);

int tagCount = 0;
const int MAX_TAGS = 20;
String savedUIDs[MAX_TAGS];

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);  // wait until serial is ready
  delay(1000);

  Serial.println("=================================");
  Serial.println("  RFID Tag UID Scanner Ready");
  Serial.println("=================================");s

  SPI.begin(6, 4, 5, 8);
  rfid.PCD_Init();
  Serial.println("SPI and RFID initialized");
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += ":";
  }
  uid.toUpperCase();

  for (int i = 0; i < tagCount; i++) {
    if (savedUIDs[i] == uid) {
      Serial.println("Already scanned. Try a different tag.");
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      delay(1500);
      return;
    }
  }

  if (tagCount < MAX_TAGS) {
    savedUIDs[tagCount] = uid;
    tagCount++;
  }

  Serial.print("Tag ");
  Serial.print(tagCount);
  Serial.print(" -> UID: ");
  Serial.println(uid);

  printSummary();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(1000);
}

void printSummary() {
  Serial.println("\n--- UIDs so far ---");
  for (int i = 0; i < tagCount; i++) {
    Serial.print("Tag ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(savedUIDs[i]);
  }
  Serial.println("-------------------\n");
}