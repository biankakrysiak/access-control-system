
// RC522 RFID reader driver implementation
// communicates with the RC522 chip over SPI
// implements card detection (REQA command) and UID reading (anti-collision).
/* REQA flow
RC522 broadcasts:  "REQA - is anyone there?"
Card responds:     "ATQA - yes, I'm here"
RC522 then asks:   "anti-collision - what's your UID?"
Card responds:     "my UID is A1:B2:C3:D4"
*/

#include "rfid_rc522.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// RC522 register map
#define CommandReg       0x01
#define ComIEnReg        0x02
#define DivIEnReg        0x03
#define ComIrqReg        0x04
#define DivIrqReg        0x05
#define ErrorReg         0x06
#define FIFODataReg      0x09
#define FIFOLevelReg     0x0A
#define ControlReg       0x0C
#define BitFramingReg    0x0D
#define ModeReg          0x11
#define TxControlReg     0x14
#define TxASKReg         0x15
#define CRCResultRegH    0x21
#define CRCResultRegL    0x22
#define TModeReg         0x2A
#define TPrescalerReg    0x2B
#define TReloadRegH      0x2C
#define TReloadRegL      0x2D

// RC522 commands written to CommandReg
#define PCD_Idle         0x00
#define PCD_CalcCRC      0x03
#define PCD_Transceive   0x0C
#define PCD_SoftReset    0x0F

// RFID card commands
#define PICC_REQA        0x26
#define PICC_SEL1        0x93

static spi_device_handle_t spi_dev;

// write a single byte to an RC522 register
// SPI frame: address byte (reg<<1, bit0=0 for write) + data byte
static void rc522_write(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { (reg << 1) & 0x7E, val };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx };
    spi_device_transmit(spi_dev, &t);
}

// read a single byte from an RC522 register
// SPI frame: address byte (reg<<1 | 0x80 for read) + dummy byte to clock in response
static uint8_t rc522_read(uint8_t reg) {
    uint8_t tx[2] = { ((reg << 1) & 0x7E) | 0x80, 0x00 };
    uint8_t rx[2] = {0};
    spi_transaction_t t = { .length = 16, .tx_buffer = tx, .rx_buffer = rx };
    spi_device_transmit(spi_dev, &t);
    return rx[1];
}

// set specific bits in a register without changing others
static void rc522_set_bits(uint8_t reg, uint8_t mask) {
    rc522_write(reg, rc522_read(reg) | mask);
}
// clear specific bits in a register without changing others
static void rc522_clear_bits(uint8_t reg, uint8_t mask) {
    rc522_write(reg, rc522_read(reg) & (~mask));
}

// initialize the RC522: reset, configure SPI, set timer, enable antenna
void rc522_init(void) {
    // hardware reset via RST pin
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RC522_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(RC522_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(RC522_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // initialize SPI bus with MOSI MISO SCK pins
    spi_bus_config_t buscfg = {
        .mosi_io_num   = RC522_PIN_MOSI,
        .miso_io_num   = RC522_PIN_MISO,
        .sclk_io_num   = RC522_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_bus_initialize(RC522_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // add RC522 as SPI device: 5 MHz, SPI mode 0, CS on RC522_PIN_CS
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 5000000,
        .mode           = 0,
        .spics_io_num   = RC522_PIN_CS,
        .queue_size     = 1,
    };
    spi_bus_add_device(RC522_SPI_HOST, &devcfg, &spi_dev);

    // software reset to clear all registers to default values
    rc522_write(CommandReg, PCD_SoftReset);
    vTaskDelay(pdMS_TO_TICKS(50));

    // configure timer: auto-start after last bit sent, ~25ms timeout
    // this timer is used as communication timeout
    rc522_write(TModeReg,     0x8D);
    rc522_write(TPrescalerReg,0x3E);
    rc522_write(TReloadRegH,  0x00);
    rc522_write(TReloadRegL,  0x1E);
    rc522_write(TxASKReg,     0x40);
    rc522_write(ModeReg,      0x3D);

    // turn on antenna - turn on tx1 and tx2 driver pins
    rc522_set_bits(TxControlReg, 0x03);
}

// send REQA command and check if any card responds.
// returns true if a card is detected in the RF field
bool rc522_is_new_card_present(void) {
    // 7-bit frame for REQA (short frame)
    rc522_write(BitFramingReg, 0x07);
    uint8_t buf[2] = { PICC_REQA, 0 };
    rc522_write(CommandReg, PCD_Idle);
    rc522_write(FIFOLevelReg, 0x80); // flush FIFO buffer
    rc522_write(FIFODataReg,  PICC_REQA); // load REQA command into FIFO
    rc522_write(CommandReg,   PCD_Transceive); // start transmit+receive
    rc522_set_bits(BitFramingReg, 0x80); // start send

    vTaskDelay(pdMS_TO_TICKS(25)); // wait for card response

    // check interrupt flags: RxIRq (bit 5) = data received, IdleIRq (bit 4) = command done
    uint8_t irq = rc522_read(ComIrqReg);
    if (irq & 0x30) return true; // RxIRq or IdleIRq
    return false;
}

// read the 4-byte UID of the card using anti-collision sequence (SEL1)
// returns true if 4+ bytes were received successfully
bool rc522_read_card_serial(rc522_uid_t *uid) {
    // Anti-collision loop
    rc522_write(BitFramingReg, 0x00); // full byte framing
    uint8_t buf[9];
    buf[0] = PICC_SEL1;
    buf[1] = 0x20;

    rc522_write(CommandReg,   PCD_Idle);
    rc522_write(FIFOLevelReg, 0x80); // flush FIFO
    for (int i = 0; i < 2; i++)
        rc522_write(FIFODataReg, buf[i]);
    rc522_write(CommandReg, PCD_Transceive);
    rc522_set_bits(BitFramingReg, 0x80); // start sending

    vTaskDelay(pdMS_TO_TICKS(25));

    // check how many bytes arrived in FIFO
    uint8_t level = rc522_read(FIFOLevelReg) & 0x7F;
    if (level < 4) return false; // need at least 4 UID bytes

    // read UID bytes from FIFO
    for (int i = 0; i < 4 && i < (int)sizeof(uid->bytes); i++)
        uid->bytes[i] = rc522_read(FIFODataReg);
    uid->size = 4;
    return true;
}
// stop active communication with the card (put RC522 back to idle)
void rc522_halt(void) {
    rc522_write(CommandReg, PCD_Idle);
}