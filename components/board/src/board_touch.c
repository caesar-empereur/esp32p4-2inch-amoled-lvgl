/*
 * board_touch.c —— 触摸 I2C 总线 + CST9220
 * SPDX-License-Identifier: MIT
 */
#include "board_touch.h"

#include "board_config.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_log.h"

static const char *TAG = "board_touch";
static i2c_master_bus_handle_t s_i2c_bus;

esp_err_t board_touch_init(esp_lcd_touch_handle_t *ret_tp)
{
    ESP_RETURN_ON_FALSE(ret_tp, ESP_ERR_INVALID_ARG, TAG, "ret_tp is NULL");

    if (s_i2c_bus == NULL) {
        const i2c_master_bus_config_t bus_cfg = {
            .i2c_port = BOARD_TOUCH_I2C_PORT,
            .sda_io_num = BOARD_TOUCH_I2C_SDA,
            .scl_io_num = BOARD_TOUCH_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c bus init failed");
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
    io_cfg.scl_speed_hz = BOARD_TOUCH_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &io), TAG, "touch IO init failed");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_PIN_RST,
        .int_gpio_num = BOARD_TOUCH_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst9220(io, &tp_cfg, ret_tp), TAG, "CST9220 init failed");
    ESP_LOGI(TAG, "CST9220 ready on I2C%d SDA=%d SCL=%d", BOARD_TOUCH_I2C_PORT,
             BOARD_TOUCH_I2C_SDA, BOARD_TOUCH_I2C_SCL);
    return ESP_OK;
}

i2c_master_bus_handle_t board_touch_get_i2c_bus(void)
{
    return s_i2c_bus;
}
