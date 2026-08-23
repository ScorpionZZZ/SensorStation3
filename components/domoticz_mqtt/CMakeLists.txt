idf_component_register(
    SRCS "domoticz_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__mqtt mqtt_base sensor_hub sensor_history nvs_flash
    PRIV_REQUIRES esp_wifi
)