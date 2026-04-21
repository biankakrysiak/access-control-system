// header for the RC522 RFID reader driver
// defines SPI pin mapping for esp32-c6 and the public API
// used by main.c to detect and read RFID cards

#pragma once
#include <stdint.h>
#include <stdbool.h>

// SPI peripheral and pin assignments for RC522 on esp32-c6
#define RC522_SPI_HOST   SPI2_HOST
#define RC522_PIN_MOSI   7  
#define RC522_PIN_MISO   5  
#define RC522_PIN_SCK    6  
#define RC522_PIN_CS     10 
#define RC522_PIN_RST    4  

// holds the raw bytes of a card's UID and how many bytes it has (4)
typedef struct {
    uint8_t bytes[10];
    uint8_t size;
} rc522_uid_t;

// initialize SPI bus and RC522 hardware, configure antenna
void     rc522_init(void);
// returns true if a new card is detected in the RF field
bool     rc522_is_new_card_present(void);
// reads the UID of the card currently in the field into *uid
// returns true on success, false if no valid response
bool     rc522_read_card_serial(rc522_uid_t *uid);
// puts the card into halt state so it won't respond until removed and re-presented
void     rc522_halt(void);