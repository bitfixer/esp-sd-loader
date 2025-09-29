#include <stdint.h>
#include <MD5Builder.h>
#include "esp_ota_ops.h"
#include "SerialLogger.h"
#include "SPI_routines.h"
#include "SD_routines.h"
#include "hardware.h"

uint8_t _buffer[1024];
char _expectedMd5[33];
char _firmwareFilename[13];

bitfixer::Serial1 _logSerial;
bitfixer::SerialLogger _logger;
bSPI _spi;
SD _sd;
bitfixer::FAT32 _fat32;

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

#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "string.h"

static const char *TAG = "SD_UPDATE";

// assumes these are defined elsewhere
extern const char *_firmwareFilename;

// SD card updater
int sd_card_update(bitfixer::FAT32* fs)
{
    int ret = 0;
    int bytesWritten = 0;

    // open firmware file
    fs->openFileForReading((uint8_t*)_firmwareFilename);
    uint32_t fileSize = fs->getFileSize();

    ESP_LOGI(TAG, "file size: %d", fileSize);
    set_led(false);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition available!");
        return -1;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);

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
            ESP_LOGE(TAG, "Written size (%d) does not match file size (%d)", bytesWritten, fileSize);
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


void firmware_detected_action(bitfixer::FAT32* fs, Logger* logger)
{
    // firmware detected, attempt to perform update
    int ret = sd_card_update(fs, logger);
    if (ret == 1)
    {
        logger->printf("SD update complete.\n");
        // reset into the new firmware
        ESP.restart();
    }
    else
    {
        logger->printf("SD update failed.\n");
    }
}

// reboot into other boot partition.
void no_firmware_action(Logger* logger)
{
    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
    esp_err_t err = esp_ota_set_boot_partition(partition);
    logger->printf("update boot partition, result %d\n", err);
    ESP.restart();    
}

bool checkForFirmware(char* buffer, bitfixer::FAT32* fat32, bitfixer::SerialLogger* log)
{
    char md5Filename[13];

    log->printf("checking\r\n");
    if (!fat32->init())
    {
        log->printf("noinit\r\n");
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
        log->printf("checking %s\r\n", fname);
        if (isFirmwareFile((char*)fname))
        {
            log->printf("%s is firmware\n", fname);
            strcpy(_firmwareFilename, (char*)fname);
            found = true;
            break;
        }
    }

    if (!found)
    {
        log->printf("no firmware\r\n");
        return false;
    }
    
    // get filename
    log->printf("found firmware, filename: %s\n", _firmwareFilename);

    // check for md5 file with the same prefix
    strcpy(md5Filename, _firmwareFilename);
    md5Filename[9] = 'M';
    md5Filename[10] = 'D';
    md5Filename[11] = '5';

    fat32->openCurrentDirectory();
    if (fat32->findFile(md5Filename))
    {
        log->printf("found md5 file: %s\n", buffer);
        // read from this file and store the md5
        if (fat32->openFileForReading((uint8_t*)md5Filename))
        {
            fat32->getNextFileBlock();
            memcpy(_expectedMd5, fat32->getBuffer(), 32);
            log->printf("md5: %s\n", _expectedMd5);
        }
    }
    else
    {
        log->printf("md5 file not found: %s\n", md5Filename);
    }

    // now find firmware file
    fat32->openCurrentDirectory();
    if (!fat32->findFile(_firmwareFilename))
    {
        log->printf("no firmware\r\n");
        return false;
    }

    log->printf("got firmware: %s\n", _firmwareFilename);

    return true;
}

void setup()
{
    // clear expected md5
    memset(_expectedMd5, 0, 33);
    memset(_firmwareFilename, 0, 13);
    
    // initialize led
    init_led();
    set_led(false);

    _logSerial.init(115200);
    _logger.initWithSerial(&_logSerial);
    _logger.printf("checking for firmware\n");

    _spi.init();
    _sd.initWithSPI(&_spi, spi_cs());
    _fat32.initWithParams(&_sd, _buffer, &_buffer[512], &_logger);

    bool hasFirmware = checkForFirmware((char*)&_buffer[769], &_fat32, &_logger);

    if (hasFirmware)
    {
        _logger.printf("has firmware: %s\n", _fat32.getFilename());
        blink_led(2, 150, 150);
        firmware_detected_action(&_fat32, &_logger);
    }
    else
    {
        _logger.printf("no firmware\n");
        blink_led(3, 150, 150);
        no_firmware_action(&_logger);
    }
}

void loop()
{
    _logger.printf("loop\n");
    hDelayMs(1000);
}