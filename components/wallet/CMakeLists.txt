idf_component_register(
    SRCS "wallet.c" "crypto.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash mbedtls log esp_hw_support
)
