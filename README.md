# ESP32 Zigbee Access Control Node

Firmware for the ESP32-C6 access node in a distributed RFID-based access control 
system. This is part of a group project - the ESP32 acts as a field node 
that reads RFID cards and communicates with a central Raspberry Pi server over 
Zigbee to authorize or deny access.

---

## Project Overview

Traditional access control systems rely on wired connections or local 
verification, which limits scalability. This system uses Zigbee for reliable, 
low-power wireless communication between distributed access nodes and a central 
server - making it easy to scale across multiple entry points.

The full system consists of:
- **ESP32-C6 access node** (this repository) - reads RFID, communicates via 
  Zigbee, controls the door lock
- **Raspberry Pi central server** - Zigbee coordinator via Zigbee2MQTT, user 
  database, backend logic, camera recording

---

## My Responsibilities

- Wiring and integrating the RC522 RFID reader with ESP32-C6
- Implementing Zigbee End Device firmware using the Arduino Zigbee library
- Integrating video recording
- Setting up and configuring Zigbee2MQTT on the coordinator side
- Connecting servo for physical lock control
- Status LED logic (access granted / denied)

---

## Hardware

| Component | Purpose |
|---|---|
| Raspberry Pi | Central server - Zigbee coordinator, user database, backend, camera recording |
| ESP32-C6 | Main microcontroller (has native Zigbee support) |
| RC522 RFID reader | Reads user cards (UID) |
| Servo | Controls door lock mechanism |
| LEDs (green/red) | Visual access feedback |

---

## How It Works

1. User scans RFID card at the reader
2. ESP32-C6 reads the card UID
3. UID is published via Zigbee to the Zigbee2MQTT broker on Raspberry Pi
4. Raspberry Pi checks the UID against the user database
5. Authorization result is sent back to ESP32-C6 via Zigbee
6. If granted: servo unlocks the door, green LED on, event logged. Raspberry Pi triggers the camera to record a short clip of the entry. Recording is saved locally and linked to the access log entry.
7. If denied: lock stays closed, red LED on, event logged

---

## Setup & Flashing

**Requirements:**
- Arduino IDE
- ESP32 Arduino core with Zigbee support

**Arduino IDE settings:**
- Tools → Partition Scheme -> **Zigbee 4MB with spiffs**
- Tools → Zigbee Mode -> **Zigbee End Device**
- Board: `ESP32-C6 Dev Module`

**Steps:**
1. Clone the repository
2. Open the .ino file in Arduino IDE
3. Apply settings above
4. Upload to ESP32-C6

---

## Tech Stack

- **Language:** C++ (Arduino framework)
- **Communication:** Zigbee (native ESP32-C6) via Zigbee2MQTT
- **Protocols:** SPI (RFID)
- **Hardware:** ESP32-C6, RC522, servo

---

## Status

Work in progress - university group project
