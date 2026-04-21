// main app entry point for rfid access control system
// reads rfid cards, sends uid over zigbee to raspberry pi
// lights up green or red LED, depending on access (granted/denied)
// based on the response from node red

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "rfid_rc522.h"
#include "zigbee_handler.h"
#include <string.h>

static const char *TAG = "MAIN";

#define LED_GREEN 18            // gpio pin for green led - access granted
#define LED_RED   20            // gpio pin for red led - access denied
#define SCAN_COOLDOWN_MS 4000   // cooldown between cards scans

// shared queque between main task and led task
// zigbee handler sends 1 (granted) or 0 (denied) into this queque
// and led task reads it to control the leds
QueueHandle_t led_queue;

// configures led gpio pins as outputs, turns both leds off
static void led_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_GREEN) | (1ULL << LED_RED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(LED_GREEN, 0);
    gpio_set_level(LED_RED, 0);
}

// dedicated FreeRTOS task for LED control
// runs independently so it can use vTaskDelay without blocking the Zigbee stack or the RFID scanning loop
// waits for a value from led_queue:
// 1 = green LED on for SCAN_COOLDOWN_MS, then off
// 0 = red LED on for SCAN_COOLDOWN_MS, then off
static void led_task(void *pvParameters) {
    uint8_t val;
    while (1) {
        // block indefinitely until a value arrives in the queue
        if (xQueueReceive(led_queue, &val, portMAX_DELAY)) {
            if (val == 1) {
                // access granted - light green LED
                gpio_set_level(LED_GREEN, 1);
                gpio_set_level(LED_RED, 0);
                vTaskDelay(pdMS_TO_TICKS(SCAN_COOLDOWN_MS));
                gpio_set_level(LED_GREEN, 0);
            } else {
                // access denied - light red LED
                gpio_set_level(LED_RED, 1);
                gpio_set_level(LED_GREEN, 0);
                vTaskDelay(pdMS_TO_TICKS(SCAN_COOLDOWN_MS));
                gpio_set_level(LED_RED, 0);
            }
        }
    }
}

void app_main(void) {
    // Initialize NVS (Non-Volatile Storage) - required by Zigbee stack to store network credentials and configuration across reboots
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // set up leds
    led_init();
    // create queue that holds up to 4 single byte values (0 or 1)
    led_queue = xQueueCreate(4, sizeof(uint8_t));
    // start led task on core 0, stack 2048 bytes, priority 5
    xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);

    // initialize rc522 rfid reader over SPI
    rc522_init();
    // initialize Zigbee and pass the led queue so the Zigbee callback
    // can signal the led task when an access decision arrives
    zigbee_init_with_queue(led_queue);

    // wait until esp successfully joins the Zigbee network
    ESP_LOGI(TAG, "Waiting for Zigbee connection...");
    while (!zigbee_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ESP_LOGI(TAG, "Ready - scan RFID card");

    rc522_uid_t uid;
    TickType_t last_scan = 0; // tick count of the last accepted scan

    while (1) {
        // check if a card is in range and read its serial number
        if (rc522_is_new_card_present() && rc522_read_card_serial(&uid)) {
            TickType_t now = xTaskGetTickCount();
            // only process the scan if enough time has passed since the last one
            if ((now - last_scan) >= pdMS_TO_TICKS(SCAN_COOLDOWN_MS)) {
                // build a readable UID string, format: "A1:B2:C3:D4"
                char uid_str[32] = {0};
                for (int i = 0; i < uid.size; i++) {
                    char byte_str[4];
                    snprintf(byte_str, sizeof(byte_str), i > 0 ? ":%02X" : "%02X", uid.bytes[i]);
                    strcat(uid_str, byte_str);
                }
                ESP_LOGI(TAG, "Card detected: %s", uid_str);


                // send raw UID bytes over Zigbee to the coordinator (raspberry pi)
                // node red will check the UID against PostgreSQL and send back
                // access decision (1=granted, 0=denied) via Zigbee write attribute
                
                zigbee_send_uid(uid.bytes, uid.size);
                last_scan = now;
            } else {
                // too soon after last scan - ignore
                ESP_LOGI(TAG, "Cooldown active, ignoring scan");
            }
            // stop communication with the current card so it can be detected again
            rc522_halt();
        }
        // poll every 100ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}