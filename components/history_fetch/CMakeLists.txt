idf_component_register(
    SRCS "history_fetch.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_event esp_netif esp_timer esp_http_client mbedtls ntp_clock sensor_history nvs_flash espressif__cjson tb_mqtt domoticz_mqtt ha_mqtt
)