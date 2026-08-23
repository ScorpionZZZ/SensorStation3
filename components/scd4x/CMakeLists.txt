idf_component_register(
    SRCS "scd4x.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_driver_i2c bmx280 sensor_hub
)