#pragma once

#include <stdint.h>
#include <stddef.h>

void init_led();
void set_led(bool value);
void hDelayMs(int ms);
bool isFirmwareFile(char* fname);