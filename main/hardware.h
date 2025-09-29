#ifndef __hardware_h__
#define __hardware_h__

#include <stdint.h>
#include <stddef.h>
#include "FAT32.h"

#define PACKED __attribute__ ((packed))

uint8_t spi_cs();
void init_led();
void set_led(bool value);
void hDelayMs(int ms);

void spi_init();
uint8_t spi_transmit(uint8_t data);
void spi_cs_select();
void spi_cs_unselect();

bool isFirmwareFile(char* fname);

#endif