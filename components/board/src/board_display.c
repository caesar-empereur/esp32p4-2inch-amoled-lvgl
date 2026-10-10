/*
 * board_display.c  —— CO5300 2.1 寸 AMOLED，MIPI-DSI 1 lane，DPI 视频模式
 *
 * 这是"基础版"驱动：单帧缓冲 + DMA2D 拷贝，代码尽量简单，方便二次开发。
 * 不包含多缓冲、异步提交、PPA 整帧旋转等刷新率优化。
 *
 * SPDX-License-Identifier: MIT
 */
#include "board_display.h"

#include "board_config.h"
#include "co5300_init_cmds.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_display";

static esp_ldo_channel_handle_t s_phy_ldo;
static esp_lcd_dsi_bus_handle_t s_dsi_bus;
static esp_lcd_panel_io_handle_t s_panel_io;   /* DBI 通道：发命令 */
static esp_lcd_panel_handle_t s_panel;         /* DPI 通道：送像素 */

static void lcd_hw_reset(void)
{
    if (BOARD_LCD_PIN_RST == GPIO_NUM_NC) {
        return;
    }
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_LCD_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(BOARD_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(BOARD_LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static esp_err_t lcd_send_init_cmds(void)
{
    for (size_t i = 0; i < sizeof(co5300_init_cmds) / sizeof(co5300_init_cmds[0]); i++) {
        const co5300_init_cmd_t *c = &co5300_init_cmds[i];
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, c->cmd, c->data, c->data_bytes),
                            TAG, "send cmd 0x%02X failed", c->cmd);
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }
    return ESP_OK;
}

esp_err_t board_display_init(esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(ret_panel, ESP_ERR_INVALID_ARG, TAG, "ret_panel is NULL");

    /* 1. MIPI DSI PHY 供电 */
    const esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BOARD_MIPI_PHY_LDO_CHAN,
        .voltage_mv = BOARD_MIPI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_phy_ldo), TAG, "MIPI PHY LDO failed");

    /* 2. 屏幕硬件复位 */
    lcd_hw_reset();

    /* 3. DSI 总线 */
    const esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = BOARD_MIPI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BOARD_MIPI_LANE_BITRATE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus), TAG, "new DSI bus failed");

    /* 4. DBI 命令通道 */
    const esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_panel_io), TAG, "new DBI IO failed");

    /* 5. DPI 视频通道：驱动在 PSRAM 里分配 1 块整屏显存，DMA2D 负责把 LVGL 的局部缓冲拷进去 */
    const esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BOARD_MIPI_DPI_CLK_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = BOARD_LCD_H_RES,
            .v_size = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_MIPI_HSYNC_PW,
            .hsync_back_porch = BOARD_MIPI_HBP,
            .hsync_front_porch = BOARD_MIPI_HFP,
            .vsync_pulse_width = BOARD_MIPI_VSYNC_PW,
            .vsync_back_porch = BOARD_MIPI_VBP,
            .vsync_front_porch = BOARD_MIPI_VFP,
        },
        .flags = {
            .use_dma2d = true,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(s_dsi_bus, &dpi_cfg, &s_panel), TAG, "new DPI panel failed");

    /* 6. 发送 CO5300 初始化指令，然后启动视频流 */
    ESP_RETURN_ON_ERROR(lcd_send_init_cmds(), TAG, "LCD init cmds failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "DPI panel init failed");

    ESP_LOGI(TAG, "CO5300 %dx%d ready (MIPI %d lane @ %d Mbps, DPI %d MHz)",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_MIPI_LANE_NUM,
             BOARD_MIPI_LANE_BITRATE_MBPS, BOARD_MIPI_DPI_CLK_MHZ);

    *ret_panel = s_panel;
    return ESP_OK;
}

esp_err_t board_display_set_brightness(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(s_panel_io, ESP_ERR_INVALID_STATE, TAG, "display not initialized");
    if (percent > 100) {
        percent = 100;
    }
    uint8_t level = (uint8_t)((uint32_t)percent * 255 / 100);
    return esp_lcd_panel_io_tx_param(s_panel_io, 0x51, &level, 1);
}

esp_err_t board_display_register_trans_done_cb(esp_lcd_dpi_panel_color_trans_done_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(s_panel, ESP_ERR_INVALID_STATE, TAG, "display not initialized");
    const esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = cb,
    };
    return esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, user_ctx);
}
