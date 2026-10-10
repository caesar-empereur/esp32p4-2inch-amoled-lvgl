/*
 * board_beeper.c —— 无源蜂鸣器
 *
 * 硬件：GPIO48 经三极管驱动无源蜂鸣器，低电平响，空闲必须保持高电平。
 * 用 LEDC 输出 PWM 方波：高电平占大部分周期，低电平的那一段让蜂鸣器发声，
 * 低电平占比 = 音量 / 2（音量 100 时为 50% 方波，最响）。
 * 时长由 esp_timer 单次定时器控制，调用 beep 后立即返回。
 *
 * SPDX-License-Identifier: MIT
 */
#include "board_beeper.h"

#include "board_config.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BEEPER_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BEEPER_LEDC_TIMER       LEDC_TIMER_0
#define BEEPER_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BEEPER_LEDC_RES         LEDC_TIMER_10_BIT
#define BEEPER_DUTY_MAX         ((1U << 10) - 1)
#define BEEPER_IDLE_LEVEL       (!BOARD_BEEPER_ACTIVE_LEVEL)

#define BEEPER_FREQ_MIN         200
#define BEEPER_FREQ_MAX         8000

static const char *TAG = "board_beeper";

static bool s_ready;
static bool s_enabled = true;
static uint8_t s_volume = 60;
static esp_timer_handle_t s_stop_timer;

static void beeper_output_idle(void)
{
    ledc_stop(BEEPER_LEDC_MODE, BEEPER_LEDC_CHANNEL, BEEPER_IDLE_LEVEL);
}

static void stop_timer_cb(void *arg)
{
    (void)arg;
    beeper_output_idle();
}

esp_err_t board_beeper_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    const ledc_timer_config_t timer_cfg = {
        .speed_mode = BEEPER_LEDC_MODE,
        .duty_resolution = BEEPER_LEDC_RES,
        .timer_num = BEEPER_LEDC_TIMER,
        .freq_hz = BOARD_BEEPER_DEFAULT_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc timer config failed");

    const ledc_channel_config_t ch_cfg = {
        .gpio_num = BOARD_BEEPER_GPIO,
        .speed_mode = BEEPER_LEDC_MODE,
        .channel = BEEPER_LEDC_CHANNEL,
        .timer_sel = BEEPER_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = BEEPER_DUTY_MAX,      /* 先输出接近全高电平，避免上电"咔"一声 */
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_cfg), TAG, "ledc channel config failed");
    beeper_output_idle();

    const esp_timer_create_args_t targs = {
        .callback = stop_timer_cb,
        .name = "beeper_stop",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_stop_timer), TAG, "timer create failed");

    s_ready = true;
    ESP_LOGI(TAG, "ready on GPIO%d (LEDC PWM, active %s)", BOARD_BEEPER_GPIO,
             BOARD_BEEPER_ACTIVE_LEVEL ? "high" : "low");
    return ESP_OK;
}

void board_beeper_set_enabled(bool enabled)
{
    s_enabled = enabled;
    if (!enabled) {
        board_beeper_stop();
    }
}

bool board_beeper_is_enabled(void)
{
    return s_enabled;
}

void board_beeper_set_volume(uint8_t volume)
{
    s_volume = volume > 100 ? 100 : volume;
    if (s_volume == 0) {
        board_beeper_stop();
    }
}

uint8_t board_beeper_get_volume(void)
{
    return s_volume;
}

void board_beeper_stop(void)
{
    if (!s_ready) {
        return;
    }
    esp_timer_stop(s_stop_timer);
    beeper_output_idle();
}

void board_beeper_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!s_ready || !s_enabled || s_volume == 0 || freq_hz == 0 || duration_ms == 0) {
        return;
    }
    if (freq_hz < BEEPER_FREQ_MIN) {
        freq_hz = BEEPER_FREQ_MIN;
    } else if (freq_hz > BEEPER_FREQ_MAX) {
        freq_hz = BEEPER_FREQ_MAX;
    }

    esp_timer_stop(s_stop_timer);   /* 上一声还没结束就重新开始 */

    /* 有效电平（发声）在一个周期里的占比 = volume / 200 */
    uint32_t active = (BEEPER_DUTY_MAX + 1) * s_volume / 200;
    uint32_t duty = BOARD_BEEPER_ACTIVE_LEVEL ? active : (BEEPER_DUTY_MAX + 1 - active);

    ledc_set_freq(BEEPER_LEDC_MODE, BEEPER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BEEPER_LEDC_MODE, BEEPER_LEDC_CHANNEL, duty);
    ledc_update_duty(BEEPER_LEDC_MODE, BEEPER_LEDC_CHANNEL);

    esp_timer_start_once(s_stop_timer, (uint64_t)duration_ms * 1000);
}

void board_beeper_play_click(void)
{
    board_beeper_beep(BOARD_BEEPER_DEFAULT_FREQ_HZ, BOARD_BEEPER_CLICK_MS);
}

void board_beeper_play_startup(void)
{
    board_beeper_beep(BOARD_BEEPER_DEFAULT_FREQ_HZ, 75);
    vTaskDelay(pdMS_TO_TICKS(75 + 65));
    board_beeper_beep(BOARD_BEEPER_DEFAULT_FREQ_HZ, 75);
    vTaskDelay(pdMS_TO_TICKS(80));
}
