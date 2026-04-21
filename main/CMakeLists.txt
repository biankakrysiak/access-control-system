idf_component_register(
    SRCS "main.c" "rfid_rc522.c" "zigbee_handler.c"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp-zigbee-lib
)