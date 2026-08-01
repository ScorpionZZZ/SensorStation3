idf_component_register(
    SRCS "sensor_hub.c" "sensor_json.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos
    PRIV_REQUIRES esp_wifi esp_timer photores display
)