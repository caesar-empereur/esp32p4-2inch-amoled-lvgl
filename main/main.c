/*
 * ESP32-P4 2.1" AMOLED LVGL 开发板 —— 示例主程序
 *
 * 上电后：
 *   1. 初始化蜂鸣器、屏幕、触摸、LVGL
 *   2. 显示 LVGL 官方 Widgets demo
 *   3. 创建硬件测试页面（按键 / 蜂鸣器 / TF 卡），右下角按钮或长按 RETURN 切换
 *   4. 启动按键扫描，后台挂载 TF 卡
 *
 * SPDX-License-Identifier: MIT
 */
#include "board_beeper.h"
#include "board_button.h"
#include "board_lvgl.h"
#include "esp_log.h"
#include "hw_test_ui.h"
#include "lvgl.h"
#include "demos/lv_demos.h"

static const char *TAG = "app";

static void on_key(board_key_id_t key, board_key_event_t event, void *user_ctx)
{
    (void)user_ctx;
    hw_test_ui_on_key(key, event);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4 2.1\" AMOLED LVGL board demo");

    ESP_ERROR_CHECK(board_beeper_init());
    ESP_ERROR_CHECK(board_lvgl_init());

    board_lvgl_lock(0);
    lv_demo_widgets();      /* LVGL 官方控件演示，可换成 lv_demo_music() / lv_demo_benchmark() 等 */
    hw_test_ui_init();
    board_lvgl_unlock();

    ESP_ERROR_CHECK(board_button_init(on_key, NULL));
    board_beeper_play_startup();
    hw_test_ui_sd_mount_async();
}
