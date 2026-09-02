idf_component_register(
    SRCS "photores.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_adc
    PRIV_REQUIRES board
)