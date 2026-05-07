idf_component_register(
    SRCS "hal_buttons.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio log esp_timer freertos
)
