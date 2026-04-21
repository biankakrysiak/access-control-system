// public interface for the Zigbee communication module
// esp32-c6 acts as a Zigbee End Device using a custom cluster (0xFF02)
// to send RFID UIDs to the coordinator and receive access decisions back

#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// custom cluster for sending the UID
#define RFID_ENDPOINT          10      // Zigbee endpoint number for this device
#define RFID_CUSTOM_CLUSTER    0xFF02  // custom Zigbee cluster ID for RFID data 
#define RFID_ATTR_UID          0x0001  // read-only, reporting: raw UID bytes sent to coordinator
#define RFID_ATTR_ACCESS       0x0002  // read-write: coordinator writes 1=granted or 0=denied

// initialize Zigbee platform and start the Zigbee task
void zigbee_init(void);

// send raw UID bytes to the Zigbee coordinator via attribute report
void zigbee_send_uid(const uint8_t *uid_bytes, uint8_t uid_len);

// returns true once the device has successfully joined the Zigbee network
bool zigbee_is_connected(void);

// initialize Zigbee and store the LED queue reference so the access decision callback can notify led_task
void zigbee_init_with_queue(QueueHandle_t queue);