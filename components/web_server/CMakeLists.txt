idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES event_manager gcal_sync buzzer esp_http_server
    PRIV_REQUIRES lwip esp_netif
)
