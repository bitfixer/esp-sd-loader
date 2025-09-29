#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"

static const char* TAG = "sdcard";

// Mount SD card over SPI (C++-safe)
int mount_sdcard_spi(int pin_miso, int pin_mosi, int pin_sck, int pin_cs)
{
    esp_err_t ret;
    sdmmc_card_t* card = nullptr;

    // 1) Configure SPI bus (zero-initialize struct first)
    spi_bus_config_t bus_cfg{}; // C++-safe: zero all fields
    bus_cfg.mosi_io_num = pin_mosi;
    bus_cfg.miso_io_num = pin_miso;
    bus_cfg.sclk_io_num = pin_sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000; // optional, default is usually fine

    spi_host_device_t host_id = SPI2_HOST; // S2/S3; on classic ESP32 you can use HSPI_HOST/VSPI_HOST

    ret = spi_bus_initialize(host_id, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %d", ret);
        return -1;
    }

    // 2) Configure SD SPI device (only CS pin)
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)pin_cs;
    slot_config.host_id = host_id;

    // 3) Mount config
    esp_vfs_fat_sdmmc_mount_config_t mount_config{};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    // 4) Mount SD card
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem: %d", ret);
        return -2;
    }

    ESP_LOGI(TAG, "SD card mounted at /sdcard");
    sdmmc_card_print_info(stdout, card);

    return 0;
}
