/*
 * board_lvgl.h —— LVGL 9 移植层（显示 + 触摸 + 心跳 + 任务）
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化屏幕、触摸和 LVGL，并启动 LVGL 任务
 */
esp_err_t board_lvgl_init(void);

/**
 * @brief LVGL 不是线程安全的。在 LVGL 任务以外的地方（按键回调、自己的任务）
 *        调用任何 lv_xxx 函数之前，必须先加锁，用完解锁。
 *
 * @param timeout_ms 0 表示一直等
 */
bool board_lvgl_lock(uint32_t timeout_ms);
void board_lvgl_unlock(void);

lv_display_t *board_lvgl_get_display(void);

#ifdef __cplusplus
}
#endif
