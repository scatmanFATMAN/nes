#pragma once

#include <stdint.h>
#include <SDL2/SDL.h>

typedef enum {
    PPU_MIRRORING_NONE,
    PPU_MIRRORING_VERTICAL,
    PPU_MIRRORING_HORIZONTAL
} ppu_mirroring_t;

void ppu_init();
void ppu_free();

void ppu_set_texture(SDL_Texture *texture);
void ppu_set_mirroring(ppu_mirroring_t mirroring);

uint8_t ppu_read_register(uint16_t index);
void ppu_write_register(uint16_t index, uint8_t value);

void ppu_cycle();