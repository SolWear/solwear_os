idf_component_register(
    SRCS "hal_nfc.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c log freertos esp_timer
)
