idf_component_register(SRCS "test.c" "Perry_the_platypus.c" "ir_ctrl.c" "pn532_ctrl.c" "pn532.c" "spi.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_adc driver esp_driver_rmt esp_lcd nvs_flash espressif__esp_lcd_ili9341 espressif__esp_lvgl_port lvgl)