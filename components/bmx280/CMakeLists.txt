idf_component_register(
    SRCS "bmx280.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_driver_i2c
    PRIV_REQUIRES board driver
)