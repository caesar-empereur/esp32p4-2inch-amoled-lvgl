/*
 * board_touch.h —— 板载触摸初始化（I2C 总线 + CST9220）
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化触摸 I2C 总线和 CST9220
 *
 * 返回的坐标是屏幕物理坐标（未旋转），LVGL 会根据显示旋转自动换算。
 */
esp_err_t board_touch_init(esp_lcd_touch_handle_t *ret_tp);

/**
 * @brief 获取触摸所在的 I2C 总线句柄（同一总线上还可以挂其他 I2C 设备）
 */
i2c_master_bus_handle_t board_touch_get_i2c_bus(void);

#ifdef __cplusplus
}
#endif
