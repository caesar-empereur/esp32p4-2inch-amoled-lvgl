/*
 * esp_lcd_touch_cst9220.c —— CST9220 电容触摸驱动
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_lcd_touch_cst9220.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POINT_NUM_MAX           (2)

#define DATA_START_REG          (0xD000)
#define DATA_ACK                (0xAB)
#define CHIP_TYPE_REG           (0xD204)

#define DATA_BUFFER_SIZE        (15)
#define FIRST_POINT_OFFSET      (0)
#define NEXT_POINT_OFFSET       (7)
#define POINT_DATA_SIZE         (5)
#define POINT_EVENT_TOUCH       (0x06)
#define POINT_ID_MAX            (15)

static const char *TAG = "CST9220";

static esp_err_t cst9220_read_data(esp_lcd_touch_handle_t tp);
static bool cst9220_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                           uint8_t *point_num, uint8_t max_point_num);
static esp_err_t cst9220_del(esp_lcd_touch_handle_t tp);

static esp_err_t cst9220_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

static esp_err_t cst9220_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, const uint8_t *data, uint8_t len)
{
    return esp_lcd_panel_io_tx_param(tp->io, reg, data, len);
}

static esp_err_t cst9220_reset(esp_lcd_touch_handle_t tp)
{
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG, "set rst failed");
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG, "set rst failed");
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    return ESP_OK;
}

static esp_err_t cst9220_read_id(esp_lcd_touch_handle_t tp)
{
    uint8_t id[4] = {0};
    ESP_RETURN_ON_ERROR(cst9220_i2c_read(tp, CHIP_TYPE_REG, id, sizeof(id)), TAG, "read id failed");
    ESP_LOGI(TAG, "IC type: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8, id[0], id[1], id[2], id[3]);
    return ESP_OK;
}

esp_err_t esp_lcd_touch_new_i2c_cst9220(const esp_lcd_panel_io_handle_t io,
                                        const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *tp)
{
    ESP_RETURN_ON_FALSE(io && config && tp, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t ret = ESP_OK;
    esp_lcd_touch_handle_t h = calloc(1, sizeof(esp_lcd_touch_t));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "no mem");

    h->io = io;
    h->read_data = cst9220_read_data;
    h->get_xy = cst9220_get_xy;
    h->del = cst9220_del;
    h->data.lock.owner = portMUX_FREE_VAL;
    memcpy(&h->config, config, sizeof(esp_lcd_touch_config_t));

    if (h->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_cfg = {
            .pin_bit_mask = BIT64(h->config.int_gpio_num),
            .mode = GPIO_MODE_INPUT,
            .intr_type = h->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&int_cfg), err, TAG, "int gpio config failed");
        if (h->config.interrupt_callback) {
            esp_lcd_touch_register_interrupt_callback(h, h->config.interrupt_callback);
        }
    }

    if (h->config.rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst_cfg = {
            .pin_bit_mask = BIT64(h->config.rst_gpio_num),
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&rst_cfg), err, TAG, "rst gpio config failed");
    }

    ESP_GOTO_ON_ERROR(cst9220_reset(h), err, TAG, "reset failed");
    if (cst9220_read_id(h) != ESP_OK) {
        ESP_LOGW(TAG, "read chip id failed, continue anyway");
    }

    *tp = h;
    return ESP_OK;

err:
    cst9220_del(h);
    return ret;
}

static esp_err_t cst9220_read_data(esp_lcd_touch_handle_t tp)
{
    uint8_t data[DATA_BUFFER_SIZE] = {0};
    uint8_t ack = DATA_ACK;
    uint8_t point_num = 0;

    ESP_RETURN_ON_ERROR(cst9220_i2c_read(tp, DATA_START_REG, data, sizeof(data)), TAG, "i2c read failed");

    if (data[0] != DATA_ACK && data[6] == DATA_ACK && !(data[4] & 0xF0)) {
        point_num = data[5] & 0x7F;
        if (point_num > POINT_NUM_MAX) {
            point_num = POINT_NUM_MAX;
        }
        if (point_num > CONFIG_ESP_LCD_TOUCH_MAX_POINTS) {
            point_num = CONFIG_ESP_LCD_TOUCH_MAX_POINTS;
        }
    }

    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = 0;
    for (uint8_t i = 0; i < point_num; i++) {
        uint8_t off = (i == 0) ? FIRST_POINT_OFFSET : (NEXT_POINT_OFFSET + (i - 1) * POINT_DATA_SIZE);
        uint8_t event = data[off] & 0x0F;
        uint8_t track_id = data[off] >> 4;
        uint16_t x = ((uint16_t)data[off + 1] << 4) | (data[off + 3] >> 4);
        uint16_t y = ((uint16_t)data[off + 2] << 4) | (data[off + 3] & 0x0F);
        bool valid_coord = x <= tp->config.x_max && y <= tp->config.y_max;
        bool valid_event = event == POINT_EVENT_TOUCH || point_num > 1;

        if (valid_event && valid_coord && track_id <= POINT_ID_MAX) {
            uint8_t n = tp->data.points;
            tp->data.coords[n].x = x;
            tp->data.coords[n].y = y;
            tp->data.coords[n].strength = 0;
            tp->data.points++;
        }
    }
    portEXIT_CRITICAL(&tp->data.lock);

    ESP_RETURN_ON_ERROR(cst9220_i2c_write(tp, DATA_START_REG, &ack, 1), TAG, "i2c ack failed");
    return ESP_OK;
}

static bool cst9220_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                           uint8_t *point_num, uint8_t max_point_num)
{
    portENTER_CRITICAL(&tp->data.lock);
    *point_num = tp->data.points > max_point_num ? max_point_num : tp->data.points;
    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }
    tp->data.points = 0;
    portEXIT_CRITICAL(&tp->data.lock);
    return *point_num > 0;
}

static esp_err_t cst9220_del(esp_lcd_touch_handle_t tp)
{
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        if (tp->config.interrupt_callback) {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
        gpio_reset_pin(tp->config.int_gpio_num);
    }
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }
    free(tp);
    return ESP_OK;
}
