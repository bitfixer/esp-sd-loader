#include <stdint.h>
#include <inttypes.h>
#include "esp_ota_ops.h"
#include "SPI_routines.h"
#include "SD_routines.h"
#include "hardware.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "string.h"
#include "FAT32.h"
#include "sd_updater.h"


uint8_t _buffer[1024];
static char _firmwareFilename[13];
static constexpr const char *TAG = "SD_UPDATE";

static bSPI _spi;
static SD _sd;
static bitfixer::FAT32 _fat32;

void blink_led(int count, int ms_on, int ms_off)
{
    set_led(false);
    for (int i = 0; i < count; i++)
    {
        set_led(true);
        hDelayMs(ms_on);
        set_led(false);
        hDelayMs(ms_off);
    }
}

// SD card updater
int sd_card_update(bitfixer::FAT32* fs)
{
    int ret = 0;
    int bytesWritten = 0;

    // open firmware file
    fs->openFileForReading((uint8_t*)_firmwareFilename);
    uint32_t fileSize = fs->getFileSize();

    ESP_LOGI(TAG, "file size: %" PRIu32, fileSize);
    set_led(false);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition available!");
        return -1;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%" PRIx32,
             (int)update_partition->subtype, update_partition->address);

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, fileSize, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "Flashing...");

    int lastFlashingPct = -1;
    while (true) {
        uint16_t numBytes = fs->getNextFileBlock();
        if (numBytes <= 0) {
            break; // end of file
        }

        err = esp_ota_write(ota_handle, fs->getBuffer(), numBytes);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed (%s)!", esp_err_to_name(err));
            ret = -1;
            break;
        }

        bytesWritten += numBytes;
        int pct = (100 * bytesWritten) / fileSize;
        if (pct > 0 && pct > lastFlashingPct && pct % 10 == 0) {
            blink_led(1, 150, 150);
            ESP_LOGI(TAG, "%d%%", pct);
        }
        lastFlashingPct = pct;
    }

    if (ret == 0) {
        err = esp_ota_end(ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
            ret = -1;
        } else if (bytesWritten != fileSize) {
            ESP_LOGE(TAG, "Written size (%d) does not match file size (%" PRIu32 ")", bytesWritten, fileSize);
            ret = -1;
        } else {
            ESP_LOGI(TAG, "Flashing done! Setting boot partition.");
            err = esp_ota_set_boot_partition(update_partition);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
                ret = -1;
            } else {
                ESP_LOGI(TAG, "Update successful, reboot required.");
                ret = 1;
            }
        }
    }

    return ret;
}


void firmware_detected_action(bitfixer::FAT32* fs)
{
    // firmware detected, attempt to perform update
    int ret = sd_card_update(fs);
    if (ret >= 0)
    {
        ESP_LOGI(TAG, "SD update complete.");
        // reset into the new firmware
        esp_restart();
    }
    else
    {
        ESP_LOGI(TAG, "SD update failed.");
    }
}

// reboot into other boot partition.
void no_firmware_action()
{
    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
    esp_err_t err = esp_ota_set_boot_partition(partition);
    ESP_LOGI(TAG, "update boot partition, result %d", err);
    esp_restart();    
}

bool checkForFirmware(char* buffer, bitfixer::FAT32* fat32)
{
    ESP_LOGI(TAG, "checking");
    if (!fat32->init())
    {
        ESP_LOGI(TAG, "noinit");
        return false;
    }

    // check for existence of firmware file
    fat32->openCurrentDirectory();
    
    // try to find a firmware file
    bool found = false;
    while (fat32->getNextDirectoryEntry())
    {
        uint8_t* fname = fat32->getFilename();
        upperStringInPlace((char*)fname);
        ESP_LOGI(TAG, "checking %s", fname);
        if (isFirmwareFile((char*)fname))
        {
            ESP_LOGI(TAG, "%s is firmware", fname);
            strcpy(_firmwareFilename, (char*)fname);
            found = true;
            break;
        }
    }

    if (!found)
    {
        ESP_LOGI(TAG, "no firmware");
        return false;
    }
    
    // get filename
    ESP_LOGI(TAG, "found firmware, filename: %s", _firmwareFilename);

    // now find firmware file
    fat32->openCurrentDirectory();
    if (!fat32->findFile(_firmwareFilename))
    {
        ESP_LOGI(TAG, "no firmware");
        return false;
    }

    ESP_LOGI(TAG, "got firmware: %s", _firmwareFilename);
    return true;
}

#define CS_PIN      4
#define MISO_PIN    37
#define MOSI_PIN    35
#define SCK_PIN     36

extern "C" void app_main()
{
    memset(_firmwareFilename, 0, 13);
    
    // initialize led
    init_led();
    set_led(false);

    ESP_LOGI(TAG, "checking for firmware");

    /*
    _spi.init();
    _sd.initWithSPI(&_spi, spi_cs());
    _fat32.initWithParams(&_sd, _buffer, &_buffer[512]);

    bool hasFirmware = checkForFirmware((char*)&_buffer[769], &_fat32);

    if (hasFirmware)
    {
        ESP_LOGI(TAG, "has firmware: %s", _fat32.getFilename());
        blink_led(2, 150, 150);
        firmware_detected_action(&_fat32);
    }
    else
    {
        ESP_LOGI(TAG, "no firmware");
        blink_led(3, 150, 150);
        no_firmware_action();
    }
    */

    int res = mount_sdcard_spi(MISO_PIN, MOSI_PIN, SCK_PIN, CS_PIN);
    ESP_LOGI(TAG, "mount result: %d", res);
}