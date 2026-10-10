/*
 * board_beeper.h —— 无源蜂鸣器（LEDC PWM 驱动，非阻塞）
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_BEEPER_DEFAULT_FREQ_HZ    2600    /* 该蜂鸣器的谐振频率附近，最响 */
#define BOARD_BEEPER_CLICK_MS           55

esp_err_t board_beeper_init(void);

/** 打开 / 关闭蜂鸣器（关闭后 beep 调用直接忽略） */
void board_beeper_set_enabled(bool enabled);
bool board_beeper_is_enabled(void);

/** 音量 0~100（通过占空比调节，100 对应 50% 占空比） */
void board_beeper_set_volume(uint8_t volume);
uint8_t board_beeper_get_volume(void);

/**
 * @brief 响一声，立即返回，到时间后自动停止（不会卡住调用者）
 * @param freq_hz     频率，200~8000 Hz
 * @param duration_ms 时长
 */
void board_beeper_beep(uint32_t freq_hz, uint32_t duration_ms);

/** 立即停止 */
void board_beeper_stop(void);

/** 按键音 */
void board_beeper_play_click(void);

/** 开机提示音（"嘀嘀"两声，会阻塞约 220ms） */
void board_beeper_play_startup(void);

#ifdef __cplusplus
}
#endif
