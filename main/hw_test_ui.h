/*
 * hw_test_ui.h —— 板载硬件测试界面（按键 / 蜂鸣器 / TF 卡）
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "board_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建硬件测试界面和右下角的切换按钮。
 *        调用前需要已经持有 LVGL 锁，并且 LVGL demo 已经加载到当前屏幕。
 */
void hw_test_ui_init(void);

/**
 * @brief 按键事件入口（在按键任务里调用，函数内部会自己加 LVGL 锁）
 */
void hw_test_ui_on_key(board_key_id_t key, board_key_event_t event);

/**
 * @brief 在后台挂载 TF 卡并刷新界面
 */
void hw_test_ui_sd_mount_async(void);

/**
 * @brief 在 demo 页面和硬件测试页面之间切换（需持有 LVGL 锁）
 */
void hw_test_ui_toggle_screen(void);

#ifdef __cplusplus
}
#endif
