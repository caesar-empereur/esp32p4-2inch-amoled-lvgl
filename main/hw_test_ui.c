/*
 * hw_test_ui.c —— 板载硬件测试界面
 *
 *   ┌ Keys   ── 按任意物理按键，对应方块亮起、计数加一，蜂鸣器发出不同音高
 *   ├ Beeper ── 开关按键音、调音量、试听不同频率、播放一段旋律
 *   └ TF Card ─ 挂载 / 卸载、显示卡信息、读写测速、列出根目录文件
 *
 * 屏幕切换：右下角悬浮按钮，或长按 RETURN 键。
 * 在测试页面里，UP / DOWN 键可以上下滚动页面。
 *
 * SPDX-License-Identifier: MIT
 */
#include "hw_test_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "board_beeper.h"
#include "board_config.h"
#include "board_lvgl.h"
#include "board_sdcard.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "hw_test_ui";

/* ---------- 配色（深色背景，AMOLED 更省电） ---------- */
#define COLOR_BG        lv_color_hex(0x0B0D12)
#define COLOR_CARD      lv_color_hex(0x181C24)
#define COLOR_BOX       lv_color_hex(0x262B36)
#define COLOR_TEXT      lv_color_hex(0xE8EAED)
#define COLOR_SUBTEXT   lv_color_hex(0x8A93A3)
#define COLOR_OK        lv_color_hex(0x2ECC71)
#define COLOR_ERR       lv_color_hex(0xE74C3C)
#define COLOR_WARN      lv_color_hex(0xF1C40F)

/* 每个按键对应的音高（C6 / E6 / G6 / C7）和高亮颜色 */
static const uint32_t s_key_freq[BOARD_KEY_ID_COUNT] = {1047, 1319, 1568, 2093};
static const lv_palette_t s_key_palette[BOARD_KEY_ID_COUNT] = {
    LV_PALETTE_GREEN, LV_PALETTE_BLUE, LV_PALETTE_ORANGE, LV_PALETTE_RED,
};

/* ---------- 界面对象 ---------- */
static lv_obj_t *s_demo_scr;
static lv_obj_t *s_test_scr;
static lv_obj_t *s_switch_btn;
static bool s_on_test;          /* 当前是否在测试页（不用 lv_screen_active，切换动画期间它还是旧屏幕） */

static lv_obj_t *s_key_box[BOARD_KEY_ID_COUNT];
static lv_obj_t *s_key_cnt_label[BOARD_KEY_ID_COUNT];
static uint32_t s_key_cnt[BOARD_KEY_ID_COUNT];
static lv_obj_t *s_key_log_label;

static lv_obj_t *s_key_sound_sw;
static lv_obj_t *s_volume_label;
static lv_timer_t *s_melody_timer;
static uint32_t s_melody_step;

static lv_obj_t *s_sd_led;
static lv_obj_t *s_sd_status_label;
static lv_obj_t *s_sd_info_label;
static lv_obj_t *s_sd_result_label;
static lv_obj_t *s_sd_files_label;

/* ---------- TF 卡后台任务 ---------- */
typedef enum {
    SD_CMD_MOUNT,
    SD_CMD_UNMOUNT,
    SD_CMD_RW_TEST,
} sd_cmd_t;

static QueueHandle_t s_sd_queue;

/* =====================================================================
 *  通用小工具
 * ===================================================================== */
static lv_obj_t *create_card(lv_obj_t *parent, const char *symbol, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text_fmt(label, "%s  %s", symbol, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    return card;
}

static lv_obj_t *create_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 44);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_sub_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, COLOR_SUBTEXT, 0);
    return label;
}

static void format_size(char *buf, size_t len, uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024 * 1024) {
        snprintf(buf, len, "%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    } else {
        snprintf(buf, len, "%.1f MB", (double)bytes / (1024.0 * 1024));
    }
}

/* =====================================================================
 *  屏幕切换
 * ===================================================================== */
void hw_test_ui_toggle_screen(void)
{
    bool to_test = !s_on_test;
    s_on_test = to_test;
    lv_screen_load_anim(to_test ? s_test_scr : s_demo_scr,
                        to_test ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        250, 0, false);
    /* 测试页有自己的返回按钮，悬浮按钮只在 demo 页显示 */
    if (to_test) {
        lv_obj_add_flag(s_switch_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_switch_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void switch_btn_cb(lv_event_t *e)
{
    (void)e;
    board_beeper_play_click();
    hw_test_ui_toggle_screen();
}

/* =====================================================================
 *  按键区
 * ===================================================================== */
static void build_key_card(lv_obj_t *parent)
{
    lv_obj_t *card = create_card(parent, LV_SYMBOL_KEYBOARD, "Keys");
    create_sub_label(card, "Press any of the 4 side keys");

    lv_obj_t *row = create_row(card);
    for (int i = 0; i < BOARD_KEY_ID_COUNT; i++) {
        lv_obj_t *box = lv_obj_create(row);
        lv_obj_set_height(box, 92);
        lv_obj_set_flex_grow(box, 1);
        lv_obj_set_style_bg_color(box, COLOR_BOX, 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_border_color(box, lv_palette_main(s_key_palette[i]), 0);
        lv_obj_set_style_radius(box, 10, 0);
        lv_obj_set_style_pad_all(box, 4, 0);
        lv_obj_set_style_pad_row(box, 2, 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *name = lv_label_create(box);
        lv_label_set_text(name, board_button_name(i));

        lv_obj_t *gpio = lv_label_create(box);
        lv_label_set_text_fmt(gpio, "GPIO%d", board_button_gpio(i));
        lv_obj_set_style_text_color(gpio, COLOR_SUBTEXT, 0);

        s_key_cnt_label[i] = lv_label_create(box);
        lv_label_set_text(s_key_cnt_label[i], "x0");
        lv_obj_set_style_text_font(s_key_cnt_label[i], &lv_font_montserrat_20, 0);

        s_key_box[i] = box;
    }

    s_key_log_label = create_sub_label(card, "Last event: -");
}

static void key_box_set_pressed(board_key_id_t key, bool pressed)
{
    lv_obj_set_style_bg_color(s_key_box[key],
                              pressed ? lv_palette_main(s_key_palette[key]) : COLOR_BOX, 0);
}

/* =====================================================================
 *  蜂鸣器区
 * ===================================================================== */
static void volume_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);
    board_beeper_set_volume((uint8_t)v);
    lv_label_set_text_fmt(s_volume_label, "%d%%", (int)v);
}

static void volume_released_cb(lv_event_t *e)
{
    (void)e;
    board_beeper_play_click();   /* 松手试听一下当前音量 */
}

static void key_sound_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        board_beeper_play_click();
    }
}

static void tone_btn_cb(lv_event_t *e)
{
    uint32_t freq = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    board_beeper_beep(freq, 300);
}

/* 一小段旋律：用 lv_timer 一个音一个音地播放，不阻塞界面 */
static const uint16_t s_melody[] = {1047, 1175, 1319, 1397, 1568, 1760, 1976, 2093};

static void melody_timer_cb(lv_timer_t *t)
{
    if (s_melody_step >= sizeof(s_melody) / sizeof(s_melody[0])) {
        lv_timer_delete(t);
        s_melody_timer = NULL;
        return;
    }
    board_beeper_beep(s_melody[s_melody_step++], 120);
}

static void melody_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_melody_timer) {
        return;
    }
    s_melody_step = 0;
    s_melody_timer = lv_timer_create(melody_timer_cb, 150, NULL);
    melody_timer_cb(s_melody_timer);
}

static void build_beeper_card(lv_obj_t *parent)
{
    char title[32];
    snprintf(title, sizeof(title), "Beeper  GPIO%d", BOARD_BEEPER_GPIO);
    lv_obj_t *card = create_card(parent, LV_SYMBOL_AUDIO, title);

    /* 按键音开关 */
    lv_obj_t *row = create_row(card);
    lv_label_set_text(lv_label_create(row), "Key sound");
    s_key_sound_sw = lv_switch_create(row);
    lv_obj_add_state(s_key_sound_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_key_sound_sw, key_sound_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 音量 */
    row = create_row(card);
    lv_label_set_text(lv_label_create(row), "Volume");
    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_style_margin_hor(slider, 12, 0);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, board_beeper_get_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, volume_released_cb, LV_EVENT_RELEASED, NULL);
    s_volume_label = lv_label_create(row);
    lv_obj_set_width(s_volume_label, 48);
    lv_label_set_text_fmt(s_volume_label, "%d%%", board_beeper_get_volume());

    /* 试听 */
    row = create_row(card);
    create_button(row, "1 kHz", tone_btn_cb, (void *)(uintptr_t)1000);
    create_button(row, "2.6 kHz", tone_btn_cb, (void *)(uintptr_t)BOARD_BEEPER_DEFAULT_FREQ_HZ);
    create_button(row, "4 kHz", tone_btn_cb, (void *)(uintptr_t)4000);
    create_button(row, LV_SYMBOL_PLAY " Song", melody_btn_cb, NULL);
}

/* =====================================================================
 *  TF 卡区
 * ===================================================================== */
static void sd_set_status(lv_color_t color, const char *text)
{
    lv_obj_set_style_bg_color(s_sd_led, color, 0);
    lv_label_set_text(s_sd_status_label, text);
}

/* 以下几个 sd_ui_xxx 在后台任务里调用，内部加锁 */
static void sd_ui_show_working(const char *what)
{
    board_lvgl_lock(0);
    sd_set_status(COLOR_WARN, what);
    board_lvgl_unlock();
}

static void sd_ui_refresh(const char *result)
{
    char info[192] = "";
    char files[256] = "";

    if (board_sdcard_is_mounted()) {
        board_sdcard_info_t ci;
        if (board_sdcard_get_info(&ci) == ESP_OK) {
            char cap[16], total[16], free_sz[16];
            format_size(cap, sizeof(cap), ci.card_bytes);
            format_size(total, sizeof(total), ci.fs_total);
            format_size(free_sz, sizeof(free_sz), ci.fs_free);
            snprintf(info, sizeof(info),
                     "Name: %s   Type: %s\nCard: %s   FS: %s   Free: %s\nBus: %u-bit @ %lu kHz",
                     ci.name, ci.type, cap, total, free_sz, ci.bus_width, (unsigned long)ci.freq_khz);
        }

        /* 列出根目录前 8 个条目 */
        DIR *dir = opendir(BOARD_SD_MOUNT_POINT);
        if (dir) {
            size_t used = snprintf(files, sizeof(files), "Files in %s:", BOARD_SD_MOUNT_POINT);
            int n = 0;
            struct dirent *ent;
            while ((ent = readdir(dir)) != NULL && n < 8 && used < sizeof(files) - 1) {
                used += snprintf(files + used, sizeof(files) - used, "\n  %s%s", ent->d_name,
                                 ent->d_type == DT_DIR ? "/" : "");
                n++;
            }
            if (n == 0) {
                snprintf(files + used, sizeof(files) - used, "\n  (empty)");
            }
            closedir(dir);
        }
    }

    board_lvgl_lock(0);
    if (board_sdcard_is_mounted()) {
        sd_set_status(COLOR_OK, "Mounted at " BOARD_SD_MOUNT_POINT);
    } else {
        sd_set_status(COLOR_ERR, "Not mounted (no card / unsupported format)");
    }
    lv_label_set_text(s_sd_info_label, info[0] ? info : "-");
    lv_label_set_text(s_sd_files_label, files);
    if (result) {
        lv_label_set_text(s_sd_result_label, result);
    }
    board_lvgl_unlock();
}

/* 写 1MB 再读回校验，测出读写速度 */
static void sd_rw_test(char *out, size_t out_len)
{
    const char *path = BOARD_SD_MOUNT_POINT "/p4_test.bin";
    const size_t chunk = 16 * 1024;
    const size_t total = 1024 * 1024;

    uint8_t *buf = heap_caps_malloc(chunk, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        snprintf(out, out_len, "R/W test: no memory");
        return;
    }

    /* 写 */
    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(out, out_len, "R/W test: open for write failed");
        free(buf);
        return;
    }
    int64_t t0 = esp_timer_get_time();
    for (size_t off = 0; off < total; off += chunk) {
        for (size_t i = 0; i < chunk; i++) {
            buf[i] = (uint8_t)((off + i) * 31 + 7);
        }
        if (fwrite(buf, 1, chunk, f) != chunk) {
            fclose(f);
            snprintf(out, out_len, "R/W test: write failed");
            free(buf);
            return;
        }
    }
    fclose(f);
    int64_t t_write = esp_timer_get_time() - t0;

    /* 读 + 校验 */
    f = fopen(path, "rb");
    if (!f) {
        snprintf(out, out_len, "R/W test: open for read failed");
        free(buf);
        return;
    }
    bool ok = true;
    t0 = esp_timer_get_time();
    for (size_t off = 0; off < total && ok; off += chunk) {
        if (fread(buf, 1, chunk, f) != chunk) {
            ok = false;
            break;
        }
        for (size_t i = 0; i < chunk; i++) {
            if (buf[i] != (uint8_t)((off + i) * 31 + 7)) {
                ok = false;
                break;
            }
        }
    }
    fclose(f);
    int64_t t_read = esp_timer_get_time() - t0;
    remove(path);
    free(buf);

    double w_mbs = (double)total / (1024.0 * 1024.0) / ((double)t_write / 1e6);
    double r_mbs = (double)total / (1024.0 * 1024.0) / ((double)t_read / 1e6);
    snprintf(out, out_len, "R/W 1 MB: write %.2f MB/s, read %.2f MB/s, verify %s",
             w_mbs, r_mbs, ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "%s", out);
}

static void sd_task(void *arg)
{
    (void)arg;
    sd_cmd_t cmd;
    char result[128];

    for (;;) {
        if (xQueueReceive(s_sd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        result[0] = '\0';
        switch (cmd) {
        case SD_CMD_MOUNT: {
            sd_ui_show_working("Mounting...");
            esp_err_t err = board_sdcard_mount();
            snprintf(result, sizeof(result), "Mount: %s", esp_err_to_name(err));
            board_beeper_beep(err == ESP_OK ? 2600 : 600, err == ESP_OK ? 80 : 300);
            break;
        }
        case SD_CMD_UNMOUNT:
            board_sdcard_unmount();
            snprintf(result, sizeof(result), "Unmounted, safe to remove the card");
            break;
        case SD_CMD_RW_TEST:
            if (!board_sdcard_is_mounted()) {
                snprintf(result, sizeof(result), "Mount the card first");
                break;
            }
            sd_ui_show_working("Testing read / write...");
            sd_rw_test(result, sizeof(result));
            break;
        }
        sd_ui_refresh(result);
    }
}

static void sd_send(sd_cmd_t cmd)
{
    if (s_sd_queue) {
        xQueueSend(s_sd_queue, &cmd, 0);
    }
}

static void sd_btn_cb(lv_event_t *e)
{
    sd_send((sd_cmd_t)(uintptr_t)lv_event_get_user_data(e));
}

void hw_test_ui_sd_mount_async(void)
{
    sd_send(SD_CMD_MOUNT);
}

static void build_sd_card(lv_obj_t *parent)
{
    lv_obj_t *card = create_card(parent, LV_SYMBOL_SD_CARD, "TF Card  SDMMC 4-bit");

    lv_obj_t *row = create_row(card);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_sd_led = lv_obj_create(row);
    lv_obj_set_size(s_sd_led, 14, 14);
    lv_obj_set_style_radius(s_sd_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_sd_led, 0, 0);
    lv_obj_set_style_bg_color(s_sd_led, COLOR_SUBTEXT, 0);
    s_sd_status_label = lv_label_create(row);
    lv_label_set_text(s_sd_status_label, "Waiting...");

    s_sd_info_label = create_sub_label(card, "-");

    row = create_row(card);
    create_button(row, "Mount", sd_btn_cb, (void *)(uintptr_t)SD_CMD_MOUNT);
    create_button(row, "Unmount", sd_btn_cb, (void *)(uintptr_t)SD_CMD_UNMOUNT);
    create_button(row, "R/W Test", sd_btn_cb, (void *)(uintptr_t)SD_CMD_RW_TEST);

    s_sd_result_label = lv_label_create(card);
    lv_obj_set_width(s_sd_result_label, lv_pct(100));
    lv_label_set_long_mode(s_sd_result_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_sd_result_label, "");

    s_sd_files_label = create_sub_label(card, "");
}

/* =====================================================================
 *  页面整体
 * ===================================================================== */
static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    board_beeper_play_click();
    hw_test_ui_toggle_screen();
}

static void build_test_screen(void)
{
    s_test_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_test_scr, COLOR_BG, 0);
    lv_obj_set_style_text_color(s_test_scr, COLOR_TEXT, 0);
    lv_obj_set_style_pad_all(s_test_scr, 14, 0);
    lv_obj_set_style_pad_row(s_test_scr, 12, 0);
    lv_obj_set_flex_flow(s_test_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_test_scr, LV_DIR_VER);

    /* 标题栏 */
    lv_obj_t *header = create_row(s_test_scr);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *back = lv_button_create(header);
    lv_obj_set_size(back, 48, 40);
    lv_obj_add_event_cb(back, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Hardware Test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    create_sub_label(s_test_scr, "Long-press RETURN to switch screens, UP / DOWN to scroll");

    build_key_card(s_test_scr);
    build_beeper_card(s_test_scr);
    build_sd_card(s_test_scr);
}

static void build_switch_button(void)
{
    s_switch_btn = lv_button_create(lv_layer_top());
    lv_obj_set_size(s_switch_btn, 56, 56);
    lv_obj_set_style_radius(s_switch_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_switch_btn, LV_OPA_80, 0);
    lv_obj_align(s_switch_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    lv_obj_add_event_cb(s_switch_btn, switch_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(s_switch_btn);
    lv_label_set_text(label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
}

void hw_test_ui_init(void)
{
    s_demo_scr = lv_screen_active();
    build_test_screen();
    build_switch_button();

    s_sd_queue = xQueueCreate(4, sizeof(sd_cmd_t));
    xTaskCreate(sd_task, "sd_test", 6 * 1024, NULL, 3, NULL);
}

/* =====================================================================
 *  按键事件（按键任务上下文）
 * ===================================================================== */
void hw_test_ui_on_key(board_key_id_t key, board_key_event_t event)
{
    static const char *ev_name[] = {"pressed", "released", "long press"};

    board_lvgl_lock(0);

    if (event == BOARD_KEY_EVENT_PRESS) {
        if (lv_obj_has_state(s_key_sound_sw, LV_STATE_CHECKED)) {
            board_beeper_beep(s_key_freq[key], 80);
        }
        s_key_cnt[key]++;
        lv_label_set_text_fmt(s_key_cnt_label[key], "x%lu", (unsigned long)s_key_cnt[key]);
        key_box_set_pressed(key, true);
    } else if (event == BOARD_KEY_EVENT_RELEASE) {
        key_box_set_pressed(key, false);
    }
    lv_label_set_text_fmt(s_key_log_label, "Last event: %s (GPIO%d) %s",
                          board_button_name(key), board_button_gpio(key), ev_name[event]);

    bool on_test = s_on_test;
    if (event == BOARD_KEY_EVENT_LONG_PRESS && key == BOARD_KEY_ID_RETURN) {
        hw_test_ui_toggle_screen();
    } else if (on_test && event == BOARD_KEY_EVENT_PRESS) {
        if (key == BOARD_KEY_ID_DOWN) {
            lv_obj_scroll_by_bounded(s_test_scr, 0, -120, LV_ANIM_ON);
        } else if (key == BOARD_KEY_ID_UP) {
            lv_obj_scroll_by_bounded(s_test_scr, 0, 120, LV_ANIM_ON);
        }
    }

    board_lvgl_unlock();
}
