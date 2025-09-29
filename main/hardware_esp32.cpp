#include "hardware.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <string.h>
#include <esp_log.h>

#define LED_PIN     2

// esp32
// TODO: add pins

// esp32s2
#define CS_PIN      4
#define MISO_PIN    37
#define MOSI_PIN    35
#define SCK_PIN     36

static constexpr const char* TAG = "HW";

uint8_t spi_cs()
{
    return CS_PIN;
}

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

// SPI

static spi_device_handle_t _spi;
static bool _spi_init = false;

void spi_init() {
    ESP_LOGW(TAG, "spi_init");
    if (_spi_init) {
        return;
    }
    gpio_set_direction((gpio_num_t)CS_PIN, GPIO_MODE_OUTPUT);
    spi_cs_unselect();

    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .mosi_io_num = MOSI_PIN,
        .miso_io_num = MISO_PIN,
        .sclk_io_num = SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,  // default
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);
    _spi_init = true;
    ESP_ERROR_CHECK(ret);

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 25 * 1000 * 1000;  // 25 MHz
    devcfg.mode = 0;
    devcfg.spics_io_num = -1;  // <--- NO CS pin
    devcfg.queue_size = 1;

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &_spi);
    ESP_ERROR_CHECK(ret);
    ESP_LOGW(TAG, "SPI initialized without CS");
}

uint8_t spi_transmit(uint8_t data) {
    uint8_t rx_byte = 0;

    spi_transaction_t trans = {
        .length = 8,  // bits
        .tx_buffer = &data,
        .rx_buffer = &rx_byte,
    };

    esp_err_t ret = spi_device_transmit(_spi, &trans);
    ESP_ERROR_CHECK(ret);

    return rx_byte;
}

void spi_cs_select()
{
    gpio_set_level((gpio_num_t)CS_PIN, 0);
}

void spi_cs_unselect()
{
    gpio_set_level((gpio_num_t)CS_PIN, 1);
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