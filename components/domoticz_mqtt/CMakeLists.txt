idf_component_register(
    SRCS "domoticz_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__mqtt esp_event esp_timer bmx280 nvs_flash bsec
    PRIV_REQUIRES esp_netif esp_wifi esp_hw_support
)
