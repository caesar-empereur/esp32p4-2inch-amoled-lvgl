/*
 * board_config.h
 *
 * ESP32-P4 + 2.1 寸 480x480 AMOLED（CO5300, MIPI-DSI 1 lane）开发板 —— 全部引脚定义
 *
 * 所有物理 GPIO 都集中在这里，改板或者自己接外设时只需要看这一个文件。
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "driver/gpio.h"
#include "driver/i2c_types.h"

/* ============================================================
 * 显示屏：2.1 寸 480x480 AMOLED，驱动 IC CO5300，MIPI-DSI 1 lane
 * ============================================================ */
#define BOARD_LCD_H_RES                 480
#define BOARD_LCD_V_RES                 480
#define BOARD_LCD_PIN_RST               GPIO_NUM_27   /* 屏幕复位 */
#define BOARD_LCD_PIN_TE                GPIO_NUM_20   /* TE 撕裂同步信号（本示例未使用，预留） */

/* MIPI DSI PHY 由芯片内部 LDO 供电 */
#define BOARD_MIPI_PHY_LDO_CHAN         3
#define BOARD_MIPI_PHY_LDO_MV           2500

/* MIPI 时序参数（来自屏厂资料） */
#define BOARD_MIPI_LANE_NUM             1
#define BOARD_MIPI_LANE_BITRATE_MBPS    320
#define BOARD_MIPI_DPI_CLK_MHZ          16
#define BOARD_MIPI_HSYNC_PW             12
#define BOARD_MIPI_HBP                  20
#define BOARD_MIPI_HFP                  20
#define BOARD_MIPI_VSYNC_PW             2
#define BOARD_MIPI_VBP                  2
#define BOARD_MIPI_VFP                  8

/*
 * 屏幕安装方向。屏幕在板子上是倒装的，默认旋转 180° 才是正向。
 * 可选：0 / 90 / 180 / 270
 */
#define BOARD_LCD_ROTATION_DEG          180

/* ============================================================
 * 触摸：CST9220，I2C 地址 0x5A
 * ============================================================ */
#define BOARD_TOUCH_I2C_PORT            I2C_NUM_0
#define BOARD_TOUCH_I2C_SDA             GPIO_NUM_7
#define BOARD_TOUCH_I2C_SCL             GPIO_NUM_8
#define BOARD_TOUCH_I2C_FREQ_HZ         (400 * 1000)
#define BOARD_TOUCH_PIN_RST             GPIO_NUM_NC   /* 板上未接 */
#define BOARD_TOUCH_PIN_INT             GPIO_NUM_NC   /* 板上未接，使用轮询 */

/* ============================================================
 * 4 个物理按键：按下接地（低电平有效），使用内部上拉
 * ============================================================ */
#define BOARD_KEY_CONFIRM               GPIO_NUM_47   /* KEY1 确认 */
#define BOARD_KEY_DOWN                  GPIO_NUM_4    /* KEY2 向右 / 向下 */
#define BOARD_KEY_UP                    GPIO_NUM_5    /* KEY3 向左 / 向上 */
#define BOARD_KEY_RETURN                GPIO_NUM_35   /* KEY4 返回（GPIO35 也是 strapping 脚，上电时不要按住） */

/* ============================================================
 * 无源蜂鸣器：经三极管驱动，低电平响，空闲保持高电平
 * ============================================================ */
#define BOARD_BEEPER_GPIO               GPIO_NUM_48
#define BOARD_BEEPER_ACTIVE_LEVEL       0

/* ============================================================
 * TF 卡：SDMMC 4-bit
 * ============================================================ */
#define BOARD_SD_PIN_CLK                GPIO_NUM_43
#define BOARD_SD_PIN_CMD                GPIO_NUM_44
#define BOARD_SD_PIN_D0                 GPIO_NUM_39
#define BOARD_SD_PIN_D1                 GPIO_NUM_40
#define BOARD_SD_PIN_D2                 GPIO_NUM_41
#define BOARD_SD_PIN_D3                 GPIO_NUM_42
#define BOARD_SD_LDO_CHAN               4             /* SD 卡 IO 电源由片内 LDO4 提供 */
#define BOARD_SD_MOUNT_POINT            "/sdcard"

/* ============================================================
 * 扩展接口（本示例没有使用，列出来方便二次开发）
 * ============================================================ */
#define BOARD_UART1_RX                  GPIO_NUM_46
#define BOARD_UART1_TX                  GPIO_NUM_32
#define BOARD_UART2_RX                  GPIO_NUM_21
#define BOARD_UART2_TX                  GPIO_NUM_22
#define BOARD_EXT_I2C_SDA               GPIO_NUM_3
#define BOARD_EXT_I2C_SCL               GPIO_NUM_2
