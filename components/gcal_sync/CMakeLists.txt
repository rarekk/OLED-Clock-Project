idf_component_register(
    SRCS "gcal_sync.c"
    INCLUDE_DIRS "include"
    REQUIRES event_manager
    PRIV_REQUIRES esp_http_client esp-tls mbedtls nvs_flash esp_timer
)
