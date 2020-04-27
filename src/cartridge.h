#pragma once

#include <stdbool.h>
#include <stdint.h>

void cartridge_init();
void cartridge_free();

bool cartridge_is_nes_test();

bool cartridge_load(const char *path);
void cartridge_unload();

uint8_t cartridge_read(uint16_t address);
uint8_t cartridge_read_chr(uint16_t address);

void cartridge_write(uint16_t address, uint8_t value);
void cartridge_write_chr(uint16_t address, uint8_t value);

void cartridge_signal_scanline();