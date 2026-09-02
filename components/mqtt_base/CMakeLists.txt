idf_component_register(
    SRCS "mqtt_base.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__mqtt
    PRIV_REQUIRES esp_event esp_netif esp_timer esp_hw_support
)