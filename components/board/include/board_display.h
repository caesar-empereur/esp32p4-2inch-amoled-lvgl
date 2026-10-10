/*
 * board_display.h  —— CO5300 MIPI-DSI 屏幕驱动（基础版）
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化屏幕：MIPI PHY 供电 -> 复位 -> DSI 总线 -> 发送初始化指令 -> 打开 DPI 视频输出
 *
 * @param[out] ret_panel 返回 esp_lcd 面板句柄，之后用 esp_lcd_panel_draw_bitmap() 刷图
 */
esp_err_t board_display_init(esp_lcd_panel_handle_t *ret_panel);

/**
 * @brief 设置屏幕亮度（AMOLED 通过 DCS 0x51 指令调节）
 * @param percent 0~100
 */
esp_err_t board_display_set_brightness(uint8_t percent);

/**
 * @brief 注册"一块数据已经拷贝进显存"的回调，LVGL 移植层用它通知刷新完成
 */
esp_err_t board_display_register_trans_done_cb(esp_lcd_dpi_panel_color_trans_done_cb_t cb, void *user_ctx);

#ifdef __cplusplus
}
#endif
