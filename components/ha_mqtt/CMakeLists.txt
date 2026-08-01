idf_component_register(
    SRCS "ha_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__mqtt mqtt_base sensor_hub sensor_history nvs_flash
    PRIV_REQUIRES esp_hw_support
)