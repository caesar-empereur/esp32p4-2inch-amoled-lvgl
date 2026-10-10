/*
 * board_lvgl.c —— LVGL 9 移植层
 *
 * 渲染方式：LVGL 局部刷新（PARTIAL）+ 两块绘制缓冲。
 * flush 回调里如需旋转，用 LVGL 自带的 lv_draw_sw_rotate() 先转到旋转缓冲，
 * 再交给 esp_lcd_panel_draw_bitmap()，由 DMA2D 拷进 MIPI 显存，
 * 拷贝完成后在回调里通知 LVGL（lv_display_flush_ready）。
 *
 * 这是最常规、最容易看懂的写法，没有做额外的刷新率优化。
 *
 * SPDX-License-Identifier: MIT
 */
#include "board_lvgl.h"

#include "board_config.h"
#include "board_display.h"
#include "board_touch.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LVGL_BUF_LINES          60      /* 每块绘制缓冲的行数 */
#define LVGL_TASK_STACK         (16 * 1024)
#define LVGL_TASK_PRIO          4
#define LVGL_TASK_MAX_DELAY_MS  30
#define LVGL_TASK_MIN_DELAY_MS  2

static const char *TAG = "board_lvgl";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static lv_display_t *s_disp;
static void *s_rotate_buf;
static SemaphoreHandle_t s_lock;

/* ---------------- 心跳 ---------------- */
static uint32_t lvgl_tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---------------- 显示 ---------------- */
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_handle_t panel,
                                          esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    lv_area_t a = *area;
    lv_display_rotation_t rot = lv_display_get_rotation(disp);

    if (rot != LV_DISPLAY_ROTATION_0) {
        lv_color_format_t cf = lv_display_get_color_format(disp);
        int32_t w = lv_area_get_width(area);
        int32_t h = lv_area_get_height(area);
        uint32_t src_stride = lv_draw_buf_width_to_stride(w, cf);
        bool swap_wh = rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270;
        uint32_t dst_stride = lv_draw_buf_width_to_stride(swap_wh ? h : w, cf);

        lv_draw_sw_rotate(px_map, s_rotate_buf, w, h, src_stride, dst_stride, rot, cf);
        lv_display_rotate_area(disp, &a);   /* 逻辑坐标 -> 屏幕物理坐标 */
        px_map = s_rotate_buf;
    }

    /* 异步：DMA2D 拷贝完成后在 on_color_trans_done 里调用 lv_display_flush_ready */
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, a.x1, a.y1, a.x2 + 1, a.y2 + 1, px_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(disp);
    }
}

/* ---------------- 触摸 ---------------- */
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x[1], y[1];
    uint8_t cnt = 0;

    esp_lcd_touch_read_data(s_touch);
    bool pressed = esp_lcd_touch_get_coordinates(s_touch, x, y, NULL, &cnt, 1);
    if (pressed && cnt > 0) {
        /* 给物理坐标即可，LVGL 会按显示旋转自动换算 */
        data->point.x = x[0];
        data->point.y = y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ---------------- 任务 ---------------- */
static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");
    for (;;) {
        uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;
        if (board_lvgl_lock(0)) {
            delay_ms = lv_timer_handler();
            board_lvgl_unlock();
        }
        if (delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void *alloc_draw_buf(size_t size)
{
    /* 优先放内部 RAM（渲染更快），不够再用 PSRAM */
    void *buf = heap_caps_aligned_alloc(64, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return buf;
}

static lv_display_rotation_t rotation_from_deg(int deg)
{
    switch (deg) {
    case 90:  return LV_DISPLAY_ROTATION_90;
    case 180: return LV_DISPLAY_ROTATION_180;
    case 270: return LV_DISPLAY_ROTATION_270;
    default:  return LV_DISPLAY_ROTATION_0;
    }
}

esp_err_t board_lvgl_init(void)
{
    ESP_RETURN_ON_FALSE(s_disp == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialized");

    s_lock = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "create mutex failed");

    ESP_RETURN_ON_ERROR(board_display_init(&s_panel), TAG, "display init failed");

    if (board_touch_init(&s_touch) != ESP_OK) {
        ESP_LOGW(TAG, "touch init failed, continue without touch");
        s_touch = NULL;
    }

    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    s_disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    ESP_RETURN_ON_FALSE(s_disp, ESP_ERR_NO_MEM, TAG, "create display failed");
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    size_t buf_size = BOARD_LCD_H_RES * LVGL_BUF_LINES * sizeof(uint16_t);
    void *buf1 = alloc_draw_buf(buf_size);
    void *buf2 = alloc_draw_buf(buf_size);
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "alloc draw buffers failed");
    lv_display_set_buffers(s_disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_rotation_t rot = rotation_from_deg(BOARD_LCD_ROTATION_DEG);
    if (rot != LV_DISPLAY_ROTATION_0) {
        s_rotate_buf = alloc_draw_buf(buf_size);
        ESP_RETURN_ON_FALSE(s_rotate_buf, ESP_ERR_NO_MEM, TAG, "alloc rotate buffer failed");
    }
    lv_display_set_rotation(s_disp, rot);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    ESP_RETURN_ON_ERROR(board_display_register_trans_done_cb(on_color_trans_done, s_disp),
                        TAG, "register display callback failed");

    if (s_touch) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
        lv_indev_set_display(indev, s_disp);
    }

    BaseType_t ok = xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "create LVGL task failed");

    ESP_LOGI(TAG, "LVGL %d.%d.%d ready, %dx%d, rotation %d deg", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH, BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_LCD_ROTATION_DEG);
    return ESP_OK;
}

bool board_lvgl_lock(uint32_t timeout_ms)
{
    TickType_t ticks = timeout_ms == 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lock, ticks) == pdTRUE;
}

void board_lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

lv_display_t *board_lvgl_get_display(void)
{
    return s_disp;
}
