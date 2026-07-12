idf_component_register(
    SRCS "ota_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_https_ota esp_http_client app_update bootloader_support freertos esp_timer
)