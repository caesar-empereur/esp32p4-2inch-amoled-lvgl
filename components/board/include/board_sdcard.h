/*
 * board_sdcard.h —— TF 卡（SDMMC 4-bit + FATFS）
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[8];           /* 卡的产品名 */
    const char *type;       /* SDSC / SDHC/SDXC / MMC */
    uint32_t freq_khz;      /* 当前总线频率 */
    uint8_t bus_width;      /* 1 / 4 bit */
    uint64_t card_bytes;    /* 卡物理容量 */
    uint64_t fs_total;      /* 文件系统总容量 */
    uint64_t fs_free;       /* 剩余空间 */
} board_sdcard_info_t;

/**
 * @brief 挂载 TF 卡到 BOARD_SD_MOUNT_POINT（"/sdcard"），之后直接用 fopen/fread 访问文件
 */
esp_err_t board_sdcard_mount(void);

/** 卸载 */
esp_err_t board_sdcard_unmount(void);

bool board_sdcard_is_mounted(void);

/** 获取卡信息，需要先挂载 */
esp_err_t board_sdcard_get_info(board_sdcard_info_t *info);

#ifdef __cplusplus
}
#endif
