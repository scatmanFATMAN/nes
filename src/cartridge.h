#pragma once

#include <stdbool.h>
#include <stdint.h>

void cartridge_init();
void cartridge_free();

bool cartridge_is_nes_test();

bool cartridge_load(const char *path);

uint8_t cartridge_read(uint16_t address);