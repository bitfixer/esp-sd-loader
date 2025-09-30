#include <stdint.h>
#include <stdio.h>
#include <dirent.h>
#include <inttypes.h>
#include <esp_ota_ops.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_flash_partitions.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <string.h>
#include <ctype.h>
#include "sd_updater.h"

static constexpr const char *TAG = "SD_UPDATE";

#if CONFIG_IDF_TARGET_ESP32
    // Code specific to ESP32
    #define LED_PIN     2
    #define CS_PIN      4
    #define MISO_PIN    19
    #define MOSI_PIN    23
    #define SCK_PIN     18
    static constexpr const char* FIRMWARE_EXT = "PD2";
#elif CONFIG_IDF_TARGET_ESP32S2
    #define LED_PIN     15
    #define CS_PIN      4
    #define MISO_PIN    37
    #define MOSI_PIN    35
    #define SCK_PIN     36
    static constexpr const char* FIRMWARE_EXT = "PD3";
#else
    #error "Unsupported target"
#endif

static void init_led()
{
    // nothing for esp32
    gpio_set_direction((gpio_num_t)LED_PIN, GPIO_MODE_OUTPUT);
}

static void set_led(bool value)
{
    if (value == true)
    {
        gpio_set_level((gpio_num_t)LED_PIN, 1);
    }
    else
    {
        gpio_set_level((gpio_num_t)LED_PIN, 0);
    }
}

static void hDelayMs(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void blink_led(int count, int ms_on, int ms_off)
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

static bool isFirmwareFile(const char* fname, const char* ext)
{
    if (fname == NULL || ext == NULL)
    {
        return false;
    }

    // extension must be exactly 3 chars
    if (strlen(ext) != 3)
    {
        return false;
    }

    // filename must be exactly 12 chars
    if (strlen(fname) != 12)
    {
        return false;
    }

    // check prefix "FIRM"
    if (strncmp(fname, "FIRM", 4) != 0)
    {
        return false;
    }

    // check extension (last 3 chars)
    if (strncmp(fname + 9, ext, 3) != 0)
    {
        return false;
    }

    return true;
}

int sd_card_update(FILE* fwFile)
{
    if (!fwFile) {
        ESP_LOGE(TAG, "Invalid file pointer");
        return -1;
    }

    // Determine file size
    if (fseek(fwFile, 0, SEEK_END) != 0) {
        ESP_LOGE(TAG, "fseek to end failed");
        return -1;
    }
    long fileSizeLong = ftell(fwFile);
    if (fileSizeLong < 0) {
        ESP_LOGE(TAG, "ftell failed");
        return -1;
    }
    size_t fileSize = static_cast<size_t>(fileSizeLong);
    rewind(fwFile); // reset file pointer to start

    ESP_LOGI(TAG, "Firmware file size: %zu bytes", fileSize);
    set_led(false);

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
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

    const size_t BUF_SIZE = 4096;
    uint8_t buffer[BUF_SIZE];
    size_t bytesWritten = 0;
    int lastFlashingPct = -1;
    int ret = 0;

    while (bytesWritten < fileSize) {
        size_t toRead = (fileSize - bytesWritten > BUF_SIZE) ? BUF_SIZE : (fileSize - bytesWritten);
        size_t readBytes = fread(buffer, 1, toRead, fwFile);
        if (readBytes == 0) {
            if (feof(fwFile)) break;
            ESP_LOGE(TAG, "fread error");
            ret = -1;
            break;
        }

        err = esp_ota_write(ota_handle, buffer, readBytes);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed (%s)!", esp_err_to_name(err));
            ret = -1;
            break;
        }

        bytesWritten += readBytes;
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
            ESP_LOGE(TAG, "Written size (%zu) does not match file size (%zu)", bytesWritten, fileSize);
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

void firmware_detected_action(FILE* fp)
{
    // firmware detected, attempt to perform update
    int ret = sd_card_update(fp);
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

static void upperStringInPlace(char* str)
{
    char* c = str;
    while (*c != 0)
    {
        *c = toupper(*c);
        c++;
    }
}

FILE* checkForFirmware(const char* mountPoint)
{
    ESP_LOGI(TAG, "Checking for firmware in %s", mountPoint);

    DIR* dir = opendir(mountPoint);
    if (!dir) {
        ESP_LOGI(TAG, "Failed to open mount point");
        return nullptr;
    }

    struct dirent* entry;
    char firmwareFilename[256] = {0};

    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) continue; // skip non-regular files

        strncpy(firmwareFilename, entry->d_name, sizeof(firmwareFilename) - 1);
        upperStringInPlace(firmwareFilename);
        ESP_LOGI(TAG, "Checking %s", firmwareFilename);

        if (isFirmwareFile(firmwareFilename, FIRMWARE_EXT)) {
            ESP_LOGI(TAG, "Found firmware: %s", firmwareFilename);
            closedir(dir);

            // Build full path
            char fullPath[512];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", mountPoint, entry->d_name);

            FILE* f = fopen(fullPath, "rb");
            if (!f) {
                ESP_LOGW(TAG, "Failed to open firmware file %s", fullPath);
            }
            return f; // returns FILE* or nullptr if fopen failed
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "No firmware file found");
    return nullptr;
}

extern "C" void app_main()
{
    // initialize led
    init_led();
    set_led(false);

    ESP_LOGI(TAG, "checking for firmware");

    int res = mount_sdcard_spi(MISO_PIN, MOSI_PIN, SCK_PIN, CS_PIN);
    ESP_LOGI(TAG, "mount result: %d", res);

    if (res < 0) {
        ESP_LOGI(TAG, "could not mount card, set partition and reboot");
        no_firmware_action();
        return;
    }

    FILE* firmware_fp = checkForFirmware("/sdcard");
    if (!firmware_fp) {
        ESP_LOGI(TAG, "no firmware found.");
        no_firmware_action();
        return;
    }

    blink_led(2, 150, 150);
    firmware_detected_action(firmware_fp);
    return;
}