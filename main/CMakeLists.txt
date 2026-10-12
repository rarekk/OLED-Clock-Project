idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        clock_ui
        ssd1306
        event_manager
        web_server
        notifier
        esp_timer
        esp_driver_gpio
        esp_driver_i2c
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
)
