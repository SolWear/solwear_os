idf_component_register(
    SRCS "st7789.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_lcd
        esp_timer
        log
        heap
)
