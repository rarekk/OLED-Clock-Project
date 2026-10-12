idf_component_register(
    SRCS "notifier.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client esp-tls mbedtls
)
