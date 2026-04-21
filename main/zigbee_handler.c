// Zigbee communication handler for the RFID access control node
// Flow:
// 1. RC522 detects a card -> main.c calls zigbee_send_uid()
// 2. UID is written to custom cluster attribute and reported to coordinator
// 3. Coordinator (raspberry pi / Zigbee2MQTT) passes UID to node red
// 4. node red checks PostgreSQL database
// 5. node red writes access decision (1/0) back to RFID_ATTR_ACCESS
// 6. zb_action_handler is called -> sends value to led_queue -> led_task lights led

#include "zigbee_handler.h"
#include "esp_zigbee_core.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>


static const char *TAG = "ZB";
static bool zb_connected = false;        // set to true once network join succeeds
static QueueHandle_t s_led_queue = NULL; // reference to the shared LED queue

#define LED_GREEN 18
#define LED_RED   20

// store the led queue reference before starting Zigbee
void zigbee_init_with_queue(QueueHandle_t queue) {
    s_led_queue = queue;
    zigbee_init();
}

// Zigbee core action callback - called by the Zigbee stack for various events
// we only care about ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID which runs when
// the coordinator writes a value to one of our attributes.
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message) {
    // ignore all events except attribute write
    if (callback_id != ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) return ESP_OK;
    
    const esp_zb_zcl_set_attr_value_message_t *msg =
        (const esp_zb_zcl_set_attr_value_message_t *)message;

    if (!msg) return ESP_FAIL;

    // only handle writes to our custom cluster's ACCESS attribute
    if (msg->info.cluster == RFID_CUSTOM_CLUSTER &&
        msg->attribute.id == RFID_ATTR_ACCESS) {

        uint8_t val = *(uint8_t *)msg->attribute.data.value;
        ESP_LOGI(TAG, "Access decision received: %d", val); // 1=granted, 0=denied

        // send to led_task via queue (non-blocking - 0 timeout)
        // led_task will light the correct LED
        if (s_led_queue) {
            xQueueSend(s_led_queue, &val, 0);
        }
    }
    return ESP_OK;
}

// handles Zigbee stack signals (network join, reboot, etc.)
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_s) {
    uint32_t *p_sg_p = signal_s->p_app_signal;
    esp_err_t err = signal_s->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            // stack is ready - start initialization sequence
            ESP_LOGI(TAG, "Init done, starting commissioning");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;
        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            // device started - try to join a network
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Joining network...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGW(TAG, "Device started but no network, retrying...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
            break;
        case ESP_ZB_BDB_SIGNAL_STEERING:
            // network join attempt result
            if (err == ESP_OK) {
                esp_zb_ieee_addr_t extended_pan_id;
                esp_zb_get_extended_pan_id(extended_pan_id);
                ESP_LOGI(TAG, "Joined network, PAN ID: 0x%04hx", esp_zb_get_pan_id());
                zb_connected = true; // allow main loop to proceed
            } else {
                ESP_LOGW(TAG, "Steering failed, retrying...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
            break;
        default:
            ESP_LOGI(TAG, "Signal: %d, status: %s", sig_type, esp_err_to_name(err));
            break;
    }
}

// FreeRTOS task that runs the Zigbee stack
// sets up all Zigbee clusters, registers the device, and enters the main loop
static void zigbee_task(void *pvParameters) {
    // configure LED pins as outputs (used by led_task via queue)
    gpio_set_direction(LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_RED, GPIO_MODE_OUTPUT);

    // Configure esp32-c6 as a Zigbee End Device (not router or coordinator)
    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg.max_children = 0,
    };
    esp_zb_init(&zb_cfg);

    // basic cluster, required by Zigbee spec
    // provides device info like manufacturer name and model
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version   = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source  = 0x01, // mains powered
    };
    esp_zb_attribute_list_t *basic_attrs = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_basic_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, "Espressif");
    esp_zb_basic_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, "RFID_Node");

    // Custom cluster 0xFF01, contains two attributes
    // 0x0001 (UID)    - esp32 writes here and reports to coordinator
    // 0x0002 (ACCESS) - coordinator writes here with the access decision
    uint8_t default_uid[11] = {0};
    uint8_t default_access = 0;

    // UID attribute: read-only + reportable (esp32 pushes it to coordinator)
    esp_zb_attribute_list_t *custom_attrs = esp_zb_zcl_attr_list_create(RFID_CUSTOM_CLUSTER);
    esp_zb_custom_cluster_add_custom_attr(
        custom_attrs,
        RFID_ATTR_UID,
        ESP_ZB_ZCL_ATTR_TYPE_OCTET_STRING,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        default_uid
    );
    // ACCESS attribute: read-write (coordinator can write 0 or 1 here)
    esp_zb_custom_cluster_add_custom_attr(
        custom_attrs, RFID_ATTR_ACCESS,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,  // <-- write_only, so node red can write it
        &default_access
    );

    // build cluster list and attach both clusters to endpoint 10
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, custom_attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // endpoint
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = RFID_ENDPOINT,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_CUSTOM_ATTR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg);
    esp_zb_device_register(ep_list);

    // register our callback so the stack calls zb_action_handler whenever an attribute write or other action occurs
    esp_zb_core_action_handler_register(zb_action_handler);

    // scan all Zigbee channels when looking for a network to join
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    // start the Zigbee stack (false = don't erase previous network info)
    ESP_ERROR_CHECK(esp_zb_start(false));

    // hand control to the Zigbee stack - this never returns
    esp_zb_stack_main_loop();
}

// configure the Zigbee radio and start the zigbee_task
void zigbee_init(void) {
    esp_zb_platform_config_t platform_cfg = {
        .radio_config.radio_mode          = ZB_RADIO_MODE_NATIVE,
        .host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));
    xTaskCreate(zigbee_task, "zigbee_task", 8192, NULL, 5, NULL);
}

// send the card's UID to the Zigbee coordinator using attribute reporting
// called from main.c after a successful card read
void zigbee_send_uid(const uint8_t *uid_bytes, uint8_t uid_len) {
    if (!zb_connected) {
        ESP_LOGW(TAG, "Not connected, skipping UID send");
        return;
    }

    // build octet-string payload: first byte is length, then UID bytes
    uint8_t payload[11];
    payload[0] = uid_len;
    memcpy(&payload[1], uid_bytes, uid_len);

    // write the UID into the local attribute value
    esp_zb_zcl_set_attribute_val(
        RFID_ENDPOINT,
        RFID_CUSTOM_CLUSTER,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        RFID_ATTR_UID,
        payload,
        false
    );

    // send an attribute report to the coordinator (address 0x0000)
    // this triggers the Zigbee2MQTT external extension which publishes
    // the UID to the MQTT topic 'zigbee2mqtt/rfid'
    esp_zb_zcl_report_attr_cmd_t report_cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,
            .dst_endpoint = 255,
            .src_endpoint = RFID_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID    = RFID_CUSTOM_CLUSTER,
        .attributeID  = RFID_ATTR_UID,
        .direction    = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
    };

    // acquire Zigbee stack lock before calling stack API from outside the stack task
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_err_t ret = esp_zb_zcl_report_attr_cmd_req(&report_cmd);
    esp_zb_lock_release();
    
    ESP_LOGI(TAG, "report_attr result: %s (0x%x)", esp_err_to_name(ret), ret);
    ESP_LOGI(TAG, "UID sent, len=%d", uid_len);
}

// returns true if the device has joined the Zigbee network
bool zigbee_is_connected(void) {
    return zb_connected;
}