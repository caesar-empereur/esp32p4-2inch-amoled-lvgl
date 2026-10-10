/*
 * board_sdcard.c —— TF 卡，SDMMC 4-bit
 *
 * ESP32-P4 的 SD 卡 IO 电源由片内 LDO 提供（BOARD_SD_LDO_CHAN），
 * 挂载前要先创建 LDO 电源控制句柄交给 SDMMC host。
 *
 * SPDX-License-Identifier: MIT
 */
#include "board_sdcard.h"

#include <string.h>
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_protocol_defs.h"
#include "soc/soc_caps.h"

#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

static const char *TAG = "board_sdcard";

static sdmmc_card_t *s_card;
#if SOC_SDMMC_IO_POWER_EXTERNAL
static sd_pwr_ctrl_handle_t s_pwr_ctrl;
#endif

esp_err_t board_sdcard_mount(void)
{
    if (s_card) {
        return ESP_OK;
    }

    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,   /* 挂载失败时不自动格式化，避免误删用户数据 */
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;    /* 20 MHz，稳定优先；可改 SDMMC_FREQ_HIGHSPEED */

#if SOC_SDMMC_IO_POWER_EXTERNAL
    if (s_pwr_ctrl == NULL) {
        const sd_pwr_ctrl_ldo_config_t ldo_cfg = {
            .ldo_chan_id = BOARD_SD_LDO_CHAN,
        };
        ESP_RETURN_ON_ERROR(sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s_pwr_ctrl), TAG, "SD LDO init failed");
    }
    host.pwr_ctrl_handle = s_pwr_ctrl;
#endif

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = BOARD_SD_PIN_CLK;
    slot.cmd = BOARD_SD_PIN_CMD;
    slot.d0 = BOARD_SD_PIN_D0;
    slot.d1 = BOARD_SD_PIN_D1;
    slot.d2 = BOARD_SD_PIN_D2;
    slot.d3 = BOARD_SD_PIN_D3;
#endif
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "mounting at %s (CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d)", BOARD_SD_MOUNT_POINT,
             BOARD_SD_PIN_CLK, BOARD_SD_PIN_CMD, BOARD_SD_PIN_D0, BOARD_SD_PIN_D1,
             BOARD_SD_PIN_D2, BOARD_SD_PIN_D3);

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(BOARD_SD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(ret));
        s_card = NULL;
        return ret;
    }
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t board_sdcard_unmount(void)
{
    if (!s_card) {
        return ESP_OK;
    }
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(BOARD_SD_MOUNT_POINT, s_card);
    s_card = NULL;
    return ret;
}

bool board_sdcard_is_mounted(void)
{
    return s_card != NULL;
}

esp_err_t board_sdcard_get_info(board_sdcard_info_t *info)
{
    ESP_RETURN_ON_FALSE(info, ESP_ERR_INVALID_ARG, TAG, "info is NULL");
    ESP_RETURN_ON_FALSE(s_card, ESP_ERR_INVALID_STATE, TAG, "not mounted");

    memset(info, 0, sizeof(*info));
    strlcpy(info->name, s_card->cid.name, sizeof(info->name));
    if (s_card->is_mmc) {
        info->type = "MMC";
    } else if (s_card->ocr & SD_OCR_SDHC_CAP) {
        info->type = "SDHC/SDXC";
    } else {
        info->type = "SDSC";
    }
    info->freq_khz = s_card->real_freq_khz;
    info->bus_width = (uint8_t)(1 << s_card->log_bus_width);
    info->card_bytes = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
    esp_vfs_fat_info(BOARD_SD_MOUNT_POINT, &info->fs_total, &info->fs_free);
    return ESP_OK;
}
