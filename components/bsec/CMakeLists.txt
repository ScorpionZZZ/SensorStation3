# REQUIRES/PRIV_REQUIRES are resolved by ESP-IDF in a separate, Kconfig-blind
# "early expansion" pass where CONFIG_* macros are never defined — they must
# stay config-independent. Only SRCS and target_link_libraries (real build
# pass only) can safely depend on CONFIG_SS3_USE_BSEC, so that's what we gate
# to skip compiling bsec_iaq.c / linking the precompiled BSEC library.
set(SS3_BSEC_SRCS "")
if(CONFIG_SS3_USE_BSEC)
    list(APPEND SS3_BSEC_SRCS
        "bsec_sensor.c"
        "algo/bsec_IAQ/config/bme680/bme680_iaq_33v_3s_4d/bsec_iaq.c")
endif()

idf_component_register(
    SRCS ${SS3_BSEC_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "algo/bsec_IAQ/inc"
                      "algo/bsec_IAQ/config/bme680/bme680_iaq_33v_3s_4d"
    REQUIRES bmx280 sensor_hub
    PRIV_REQUIRES esp_driver_i2c nvs_flash esp_timer freertos
)

if(CONFIG_SS3_USE_BSEC)
    # Link the precompiled BSEC library for ESP32 Xtensa LX6 (GCC ABI)
    target_link_libraries(${COMPONENT_LIB} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/algo/bsec_IAQ/bin/esp/esp32/libalgobsec.a")
endif()