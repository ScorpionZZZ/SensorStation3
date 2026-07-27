idf_component_register(
    SRCS "tb_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__mqtt esp_event esp_timer freertos bmx280 nvs_flash bsec
    PRIV_REQUIRES esp_netif esp_wifi esp_hw_support photores display
)