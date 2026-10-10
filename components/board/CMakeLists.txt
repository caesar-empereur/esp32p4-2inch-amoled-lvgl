idf_component_register(
    SRCS
        "src/board_display.c"
        "src/board_touch.c"
        "src/esp_lcd_touch_cst9220.c"
        "src/board_lvgl.c"
        "src/board_button.c"
        "src/board_beeper.c"
        "src/board_sdcard.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES
        esp_lcd
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_driver_sdmmc
        sdmmc
        fatfs
        esp_timer
)
