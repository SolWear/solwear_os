idf_component_register(
    SRCS "hal_battery.c"
    INCLUDE_DIRS "."
    REQUIRES log esp_adc esp_hw_support
)
