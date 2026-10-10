/*
 * board_button.h —— 4 个物理按键（轮询 + 消抖 + 长按）
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_KEY_ID_CONFIRM = 0,   /* KEY1 GPIO47 */
    BOARD_KEY_ID_DOWN,          /* KEY2 GPIO4  */
    BOARD_KEY_ID_UP,            /* KEY3 GPIO5  */
    BOARD_KEY_ID_RETURN,        /* KEY4 GPIO35 */
    BOARD_KEY_ID_COUNT,
} board_key_id_t;

typedef enum {
    BOARD_KEY_EVENT_PRESS = 0,      /* 按下（消抖后） */
    BOARD_KEY_EVENT_RELEASE,        /* 松开 */
    BOARD_KEY_EVENT_LONG_PRESS,     /* 按住超过 BOARD_KEY_LONG_PRESS_MS，只触发一次 */
} board_key_event_t;

#define BOARD_KEY_DEBOUNCE_MS       30
#define BOARD_KEY_LONG_PRESS_MS     800

/**
 * @brief 按键事件回调。在按键任务里调用，不是中断上下文；
 *        如果要操作 LVGL，记得先 board_lvgl_lock()。
 */
typedef void (*board_key_cb_t)(board_key_id_t key, board_key_event_t event, void *user_ctx);

/**
 * @brief 初始化 4 个按键 GPIO 并启动扫描任务
 */
esp_err_t board_button_init(board_key_cb_t cb, void *user_ctx);

/**
 * @brief 读取按键当前状态（消抖后），true = 按下
 */
bool board_button_is_pressed(board_key_id_t key);

/**
 * @brief 按键名字和对应的 GPIO，方便界面显示
 */
const char *board_button_name(board_key_id_t key);
int board_button_gpio(board_key_id_t key);

#ifdef __cplusplus
}
#endif
