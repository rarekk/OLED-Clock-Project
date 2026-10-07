idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES clock_ui ssd1306 esp_timer esp_driver_gpio esp_wifi nvs_flash esp_netif
)