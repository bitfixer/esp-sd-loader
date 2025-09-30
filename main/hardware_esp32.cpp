#include "hardware.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <string.h>
#include <esp_log.h>

#define LED_PIN     2

static constexpr const char* TAG = "HW";

void init_led()
{
    // nothing for esp32
    gpio_set_direction((gpio_num_t)LED_PIN, GPIO_MODE_OUTPUT);
}

void set_led(bool value)
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

void hDelayMs(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

bool isFirmwareFile(char* fname)
{
    if (fname == NULL)
    {
        return false;
    }

    if (strlen(fname) != 12)
    {
        return false;
    }

    if (fname[0] == 'F' && 
        fname[1] == 'I' && 
        fname[2] == 'R' && 
        fname[3] == 'M' &&
        fname[9] == 'P' &&
        fname[10] == 'D' &&
        fname[11] == '2')
    {
        return true;
    }

    return false;
}