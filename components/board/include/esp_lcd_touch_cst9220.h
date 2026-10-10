/*
 * esp_lcd_touch_cst9220.h —— CST9220 电容触摸驱动（基于乐鑫 esp_lcd_touch 框架）
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LCD_TOUCH_IO_I2C_CST9220_ADDRESS    (0x5A)

/**
 * @brief CST9220 的 panel IO 配置：16 bit 寄存器地址，无控制相位
 */
#define ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG()               \
    {                                                       \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST9220_ADDRESS,   \
        .control_phase_bytes = 1,                           \
        .dc_bit_offset = 0,                                 \
        .lcd_cmd_bits = 16,                                 \
        .lcd_param_bits = 0,                                \
        .flags = {                                          \
            .disable_control_phase = 1,                     \
        },                                                  \
    }

/**
 * @brief 创建 CST9220 触摸句柄
 *
 * @param io     由 esp_lcd_new_panel_io_i2c() 创建的 IO 句柄
 * @param config 触摸配置
 * @param tp     返回的触摸句柄
 */
esp_err_t esp_lcd_touch_new_i2c_cst9220(const esp_lcd_panel_io_handle_t io,
                                        const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *tp);

#ifdef __cplusplus
}
#endif
