idf_component_register(
    SRCS  "app_main.c"
          "claude_logo.c"
          "app_config.c"
          "app_wifi.c"
          "app_portal.c"
          "app_admin.c"
          "app_claude_api.c"
          "app_display.c"
          "app_touch.c"
          "ui_dashboard.c"
          "ui_setup.c"
          "app_sleep.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi nvs_flash esp_lcd driver esp_timer
                  esp_http_client esp_http_server esp-tls json
                  mbedtls esp_netif esp_event lwip mdns
                  esp_bsp esp_lcd_touch_axs5106 esp_lcd_jd9853
)
