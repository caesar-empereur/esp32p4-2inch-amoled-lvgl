/*
 * board_button.c —— 4 个物理按键
 *
 * 按键接法：一端接 GPIO，一端接 GND，GPIO 打开内部上拉，按下读到低电平。
 * 使用 10ms 周期轮询 + 消抖，比中断方式简单可靠，也方便扩展组合键。
 *
 * SPDX-License-Identifier: MIT
 */
#include "board_button.h"

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define KEY_SCAN_PERIOD_MS  10

static const char *TAG = "board_button";

typedef struct {
    gpio_num_t gpio;
    const char *name;
} key_desc_t;

static const key_desc_t s_keys[BOARD_KEY_ID_COUNT] = {
    [BOARD_KEY_ID_CONFIRM] = {BOARD_KEY_CONFIRM, "CONFIRM"},
    [BOARD_KEY_ID_DOWN]    = {BOARD_KEY_DOWN,    "DOWN"},
    [BOARD_KEY_ID_UP]      = {BOARD_KEY_UP,      "UP"},
    [BOARD_KEY_ID_RETURN]  = {BOARD_KEY_RETURN,  "RETURN"},
};

typedef struct {
    bool raw;           /* 上一次读到的原始电平（true=按下） */
    bool stable;        /* 消抖后的状态 */
    bool long_sent;
    uint32_t raw_change_ms;
    uint32_t press_ms;
} key_state_t;

static key_state_t s_state[BOARD_KEY_ID_COUNT];
static board_key_cb_t s_cb;
static void *s_cb_ctx;
static TaskHandle_t s_task;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline bool key_read_raw(board_key_id_t id)
{
    return gpio_get_level(s_keys[id].gpio) == 0;  /* 低电平 = 按下 */
}

static void emit(board_key_id_t id, board_key_event_t ev)
{
    if (s_cb) {
        s_cb(id, ev, s_cb_ctx);
    }
}

static void button_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t t = now_ms();
        for (int i = 0; i < BOARD_KEY_ID_COUNT; i++) {
            key_state_t *st = &s_state[i];
            bool raw = key_read_raw(i);
            if (raw != st->raw) {
                st->raw = raw;
                st->raw_change_ms = t;
            }

            /* 电平稳定超过消抖时间才认为状态改变 */
            if (raw != st->stable && (t - st->raw_change_ms) >= BOARD_KEY_DEBOUNCE_MS) {
                st->stable = raw;
                if (raw) {
                    st->press_ms = t;
                    st->long_sent = false;
                    emit(i, BOARD_KEY_EVENT_PRESS);
                } else {
                    emit(i, BOARD_KEY_EVENT_RELEASE);
                }
            }

            if (st->stable && !st->long_sent && (t - st->press_ms) >= BOARD_KEY_LONG_PRESS_MS) {
                st->long_sent = true;
                emit(i, BOARD_KEY_EVENT_LONG_PRESS);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(KEY_SCAN_PERIOD_MS));
    }
}

esp_err_t board_button_init(board_key_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(s_task == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialized");

    uint64_t mask = 0;
    for (int i = 0; i < BOARD_KEY_ID_COUNT; i++) {
        mask |= 1ULL << s_keys[i].gpio;
    }
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio config failed");

    /* 以当前电平作为初始状态，避免上电时误报 */
    uint32_t t = now_ms();
    for (int i = 0; i < BOARD_KEY_ID_COUNT; i++) {
        bool raw = key_read_raw(i);
        s_state[i] = (key_state_t){
            .raw = raw, .stable = raw, .long_sent = true, .raw_change_ms = t, .press_ms = t,
        };
    }

    s_cb = cb;
    s_cb_ctx = user_ctx;
    BaseType_t ok = xTaskCreate(button_task, "button", 4096, NULL, 5, &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "create task failed");

    ESP_LOGI(TAG, "keys: CONFIRM=GPIO%d DOWN=GPIO%d UP=GPIO%d RETURN=GPIO%d",
             BOARD_KEY_CONFIRM, BOARD_KEY_DOWN, BOARD_KEY_UP, BOARD_KEY_RETURN);
    return ESP_OK;
}

bool board_button_is_pressed(board_key_id_t key)
{
    return key < BOARD_KEY_ID_COUNT ? s_state[key].stable : false;
}

const char *board_button_name(board_key_id_t key)
{
    return key < BOARD_KEY_ID_COUNT ? s_keys[key].name : "?";
}

int board_button_gpio(board_key_id_t key)
{
    return key < BOARD_KEY_ID_COUNT ? (int)s_keys[key].gpio : -1;
}
