/*
 * CO5300 2.1 寸 480x480 AMOLED 初始化指令（MIPI DCS，经 DBI 通道发送）
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t cmd;
    const uint8_t *data;
    size_t data_bytes;
    uint32_t delay_ms;
} co5300_init_cmd_t;

static const co5300_init_cmd_t co5300_init_cmds[] = {
    {0xFE, (const uint8_t[]){0x00}, 1, 0},        /* 切到 User Command 页 */
    {0x3A, (const uint8_t[]){0x55}, 1, 0},        /* 像素格式 RGB565 */
    {0x35, (const uint8_t[]){0x00}, 1, 0},        /* 打开 TE 输出 */
    {0x53, (const uint8_t[]){0x20}, 1, 0},        /* 打开亮度控制 */
    {0x51, (const uint8_t[]){0xFF}, 1, 0},        /* 亮度 0x00~0xFF */
    {0x63, (const uint8_t[]){0xFF}, 1, 0},        /* HBM 亮度 */
    {0x2A, (const uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0}, /* 列地址 0~479 */
    {0x2B, (const uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0}, /* 行地址 0~479 */
    {0x11, NULL, 0, 120},                          /* 退出睡眠 */
    {0x29, NULL, 0, 0},                            /* 打开显示 */
};
