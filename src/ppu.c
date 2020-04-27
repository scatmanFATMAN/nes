#include <stdio.h>
#include <string.h>
#include "log.h"
#include "cpu.h"
#include "cartridge.h"
#include "ppu.h"

#define MODULE "PPU"

#define PPU_SPRITES 8

#define NTH_BIT(x, n) (((x) >> (n)) & 1)

static uint32_t nes_rgb[] ={
    0x7C7C7C, 0x0000FC, 0x0000BC, 0x4428BC, 0x940084, 0xA80020, 0xA81000, 0x881400,
    0x503000, 0x007800, 0x006800, 0x005800, 0x004058, 0x000000, 0x000000, 0x000000,
    0xBCBCBC, 0x0078F8, 0x0058F8, 0x6844FC, 0xD800CC, 0xE40058, 0xF83800, 0xE45C10,
    0xAC7C00, 0x00B800, 0x00A800, 0x00A844, 0x008888, 0x000000, 0x000000, 0x000000,
    0xF8F8F8, 0x3CBCFC, 0x6888FC, 0x9878F8, 0xF878F8, 0xF85898, 0xF87858, 0xFCA044,
    0xF8B800, 0xB8F818, 0x58D854, 0x58F898, 0x00E8D8, 0x787878, 0x000000, 0x000000,
    0xFCFCFC, 0xA4E4FC, 0xB8B8F8, 0xD8B8F8, 0xF8B8F8, 0xF8A4C0, 0xF0D0B0, 0xFCE0A8,
    0xF8D878, 0xD8F878, 0xB8F8B8, 0xB8F8D8, 0x00FCFC, 0xF8D8F8, 0x000000, 0x000000
};

typedef struct {
    uint8_t id;
    uint8_t x;
    uint8_t y;
    uint8_t title;
    uint8_t attr;
    uint8_t data_low;
    uint8_t data_high;
} ppu_sprite_t;

typedef union {
    struct {
        unsigned int nt: 2;
        unsigned int increment: 1;
        unsigned int sprite_pattern_table: 1;
        unsigned int background_pattern_table: 1;
        unsigned int sprite_size: 1;
        unsigned int slave: 1;
        unsigned int nmi: 1;
    };
    uint8_t value;
} ppu_control_t;

typedef union {
    struct {
        unsigned int bus: 5;
        unsigned int sprite_overflow: 1;
        unsigned int sprite0_hit: 1;
        unsigned int vblank: 1;
    };
    uint8_t value;
} ppu_status_t;

typedef union {
    struct {
        unsigned int grayscale: 1;              //Greyscale (0: normal color, 1: produce a greyscale display)
        unsigned int show_background_left: 1;   //Show background in leftmost 8 pixels of screen, 0: Hide
        unsigned int show_sprites_left: 1;      //Show sprites in leftmost 8 pixels of screen, 0: Hide
        unsigned int show_background: 1;
        unsigned int show_sprites: 1;
        unsigned int emphasize_red: 1;
        unsigned int emphasize_green: 1;
        unsigned int emphasize_blue: 1;
    };
    uint8_t value;
} ppu_mask_t;

typedef union {
    struct {
        unsigned int coarse_x: 5;               //Coarse X
        unsigned int coarse_y: 5;               //Coarse Y
        unsigned int nametable: 2;              //Nametable
        unsigned int fine_y: 3;                 //Fine Y
    };
    struct {
        unsigned int low: 8;
        unsigned int high: 7;
    };
    unsigned int address: 14;
    unsigned int r: 15;
} ppu_address_t;

typedef struct {
    SDL_Texture *texture;
    unsigned char ci[0x800];                //VRAM for nametables
    unsigned char cg[0x100];                //VRAM for palettes
    unsigned char oam[0x100];               //VRAM for sprite properties
    uint16_t oam_address;
    uint32_t pixels[256 * 240];
    ppu_sprite_t sprites[PPU_SPRITES];
    ppu_sprite_t sprites2[PPU_SPRITES];
    int scanline;
    int dot;
    bool odd_frame;
    ppu_control_t control;                  //PPUCTRL ($2000) register
    ppu_status_t status;                    //PPUSTATUS ($2002) register
    ppu_mask_t mask;                        //PPUMASK ($2001) register
    ppu_mirroring_t mirroring;
    // Background latches:
    uint8_t latch_nametable;
    uint8_t latch_at;
    uint8_t latch_background_low;
    uint8_t latch_background_high;
    // Background shift registers:
    uint8_t at_shift_low;
    uint8_t at_shift_high;
    uint8_t bg_shift_low;
    uint8_t bg_shift_high;
    bool at_latch_low;
    bool at_latch_high;
    uint8_t fine_x;                             //Fine X
    ppu_address_t v_address;                    //Loopy V
    ppu_address_t t_address;                    //Loopy T
} ppu_t;

typedef enum {
    PPU_SCANLINE_TYPE_PRE,
    PPU_SCANLINE_TYPE_VISIBLE,
    PPU_SCANLINE_TYPE_POST,
    PPU_SCANLINE_TYPE_VBLANK
} ppu_scanline_type_t;

static ppu_t ppu;

#define ppu_rendering()     (ppu.mask.show_background || ppu.mask.show_sprites)
#define ppu_sprite_height() (ppu.control.sprite_size ? 16 : 8)

void
ppu_init() {
    memset(&ppu, 0, sizeof(ppu));
}

void
ppu_free() {
}

void
ppu_set_texture(SDL_Texture *texture) {
    ppu.texture = texture;
}

void
ppu_set_mirroring(ppu_mirroring_t mirroring) {
    ppu.mirroring = mirroring;

    switch (mirroring) {
        case PPU_MIRRORING_VERTICAL:
            log_debug(MODULE, "Using vertical mirroring");
            break;
        case PPU_MIRRORING_HORIZONTAL:
            log_debug(MODULE, "Using horizontal mirroring");
            break;
        case PPU_MIRRORING_NONE:
            log_debug(MODULE, "Not using mirroring");
            break;
    }
}

static uint16_t
ppu_nametable_mirroring_address(uint16_t address) {
    switch (ppu.mirroring) {
        case PPU_MIRRORING_VERTICAL:
            return address & 0x800;
        case PPU_MIRRORING_HORIZONTAL:
            return ((address  / 2) & 0x400) + (address % 0x400);
        case PPU_MIRRORING_NONE:
            return address - 0x2000;
    }

    return 0;
}

static uint16_t
ppu_nametable_address() {
    return 0x2000 | (ppu.v_address.r & 0xFFF);
}

static uint16_t
at_address() {
    return 0x23C0 | (ppu.v_address.nametable << 10) | ((ppu.v_address.coarse_y / 4) << 3) | (ppu.v_address.coarse_x / 4);

}
static uint16_t
bg_address() {
    return (ppu.control.background_pattern_table * 0x1000) + (ppu.v_address.nametable * 16) + ppu.v_address.fine_y;
}

static uint8_t
ppu_read(uint16_t address) {
    if (address >= 0x0000 && address <= 0x1FFF) {
        return cartridge_read_chr(address);
    }
    else if (address >= 0x2000 && address <= 0x3FFF) {
        return ppu.ci[ppu_nametable_mirroring_address(address)];
    }
    else if (address >= 0x3FFF && address <= 0x3FFF) {
        if ((address & 0x31) == 0x10) {
            address &= ~0x10;
        }

        return ppu.cg[address & 0x1F] & (ppu.mask.grayscale ? 0x30 : 0xFF);
    }

    log_err(MODULE, "Attempt to read at invalid address 0x%04X", address);
    return 0;
}

static void
ppu_write(uint16_t address, uint8_t value) {
    if (address >= 0x0000 && address <= 0x1FFF) {
        cartridge_write_chr(address, value);
    }
    else if (address >= 0x2000 && address <= 0x3FFF) {
        ppu.ci[ppu_nametable_mirroring_address(address)] = value;
    }
    else if (address >= 0x3FFF && address <= 0x3FFF) {
        if ((address & 0x31) == 0x10) {
            address &= ~0x10;
        }

        ppu.cg[address & 0x1F] = value;
    }

    log_err(MODULE, "Attempt to write at invalid address 0x%04X", address);
}

static uint8_t res = 0;
static uint8_t buffer = 0;
static bool latch = false;

uint8_t
ppu_read_register(uint16_t index) {
    switch (index) {
        case 2:
            ppu.status.vblank = 0;
            latch = false;
            res = (res & 0x1F) | ppu.status.value;
            break;
        case 4:
            res = ppu.oam[ppu.oam_address];
            break;
        case 7:
            if (ppu.v_address.address <= 0x3EFF) {
                res = buffer;
                buffer = ppu_read(ppu.v_address.address);
            }
            else {
                buffer = ppu_read(ppu.v_address.address);
                res = buffer;
            }

            ppu.v_address.address += ppu.control.increment ? 32 : 1;

            break;
    }

    return res;
}

void
ppu_write_register(uint16_t index, uint8_t value) {
    switch (index) {
        case 0:
            ppu.control.value = value;
            ppu.t_address.nametable = ppu.control.nt;
            break;
        case 1:
            ppu.mask.value = value;
            break;
        case 3:
            ppu.oam_address = value;
            break;
        case 4:
            ppu.oam[ppu.oam_address++] = value;
            break;
        case 5:
            if (latch) {
                ppu.t_address.fine_y = value & 7;
                ppu.t_address.coarse_y = value >> 3;
            }
            else {
                ppu.fine_x = value & 7;
                ppu.t_address.coarse_x = value >> 3;
            }

            latch = !latch;

            break;
        case 6:
            if (latch) {
                ppu.t_address.low = value;
                ppu.v_address.r = ppu.t_address.r;
            }
            else {
                ppu.t_address.high = value & 0x3F;
            }

            latch = !latch;

            break;
        case 7:
            ppu_write(ppu.v_address.address, value);
            ppu.v_address.address += ppu.control.increment ? 32 : 1;
            break;
    }
}

static void
ppu_clear_oam2() {
    int i;

    for (i = 0; i < PPU_SPRITES; i++) {
        ppu.sprites2[i].id = 64;
        ppu.sprites2[i].y = 0xFF;
        ppu.sprites2[i].title = 0xFF;
        ppu.sprites2[i].attr = 0xFF;
        ppu.sprites2[i].x = 0xFF;
        ppu.sprites2[i].data_low = 0;
        ppu.sprites2[i].data_high = 0;
    }
}

static void
ppu_reload_shift() {
    ppu.bg_shift_low = (ppu.bg_shift_low & 0xFF00) | ppu.latch_background_low;
    ppu.bg_shift_high = (ppu.bg_shift_high & 0xFF00) | ppu.latch_background_low;

    ppu.at_latch_low = (ppu.latch_at & 1);
    ppu.at_shift_high = (ppu.latch_at & 2);
}
static void
ppu_horizontal_scroll() {
    if (!ppu_rendering()) {
        return; 
    }
    
    if (ppu.v_address.coarse_x == 31) {
        ppu.v_address.r ^= 0x41F;
    }
    else {
        ++ppu.v_address.coarse_x;
    }
}

static void
ppu_vertical_scroll() {
    if (!ppu_rendering()) {
        return;
    }

    if (ppu.v_address.fine_y < 7) {
        ++ppu.v_address.fine_y;
    }
    else {
        ppu.v_address.fine_y = 0;
        if (ppu.v_address.coarse_y == 31) {
            ppu.v_address.coarse_y = 0;
        }
        else if (ppu.v_address.coarse_y == 29) {
            ppu.v_address.coarse_y = 0;
            ppu.v_address.nametable ^= 0b10;
        }
        else {
            ++ppu.v_address.coarse_y;
        }
    }
}

static void
ppu_horizontal_scroll_update() {
    if (!ppu_rendering()) {
        return;
    }
    
    ppu.v_address.r = (ppu.v_address.r & ~0x041F) | (ppu.t_address.r & 0x041F);
}

static void
ppu_vertical_scroll_update() {
    if (!ppu_rendering()) {
        return;
    }
    
    ppu.v_address.r = (ppu.v_address.r & ~0x7BE0) | (ppu.t_address.r & 0x7BE0);
}

static void
ppu_evaluate_sprites() {
    int i, line, count = 0;

    for (i = 0; i < 64; i++) {
        line = ppu.scanline;

        if (line == 261) {
            line = -1;
        }

        line -= ppu.oam[i * 4];

        if (line >= 0 && line < ppu_sprite_height()) {
            ppu.sprites2[count].id = i;
            ppu.sprites2[count].y = ppu.oam[i * 4 + 0];
            ppu.sprites2[count].title = ppu.oam[i * 4 + 1];
            ppu.sprites2[count].attr = ppu.oam[i * 4 + 2];
            ppu.sprites2[count].x = ppu.oam[i * 4 + 3];

            if (++count > 8) {
                ppu.status.sprite_overflow = 1;
                break;
            }
        }
    }
}

static void
ppu_load_sprites() {
    uint16_t address;
    int i, y;

    for (i = 0; i < PPU_SPRITES; i++) {
        memcpy(&ppu.sprites[i], &ppu.sprites2[i], sizeof(ppu.sprites[i]));

        if (ppu_sprite_height() == 16) {
            address = ((ppu.sprites[i].title & 1) * 0x1000) + ((ppu.sprites[i].title & ~1) * 16);
        }
        else {
            address = (ppu.control.sprite_pattern_table * 0x1000) + (ppu.sprites[i].title * 16);
        }

        y = (ppu.scanline - ppu.sprites[i].y) % ppu_sprite_height();

        if (ppu.sprites[i].attr & 0x80) {
            y ^= ppu_sprite_height() - 1;
        }

        address += y + (y & 8);

        ppu.sprites[i].data_low = ppu_read(address);
        ppu.sprites[i].data_high = ppu_read(address + 8);
    }
}

static void
ppu_process_pixel() {
    uint8_t palette = 0, obj_palette = 0, sprite_palette;
    int i, x;
    bool obj_priority;

    x = ppu.dot - 2;

    if (ppu.scanline < 240 && x >= 0 && x < 256) {
        if (ppu.mask.show_background && !(!ppu.mask.show_background_left && x < 8)) {
            palette = (NTH_BIT(ppu.bg_shift_high, 15 - ppu.fine_x) << 1) | NTH_BIT(ppu.bg_shift_low, 15 - ppu.fine_x);

            if (palette > 0) {
                palette |= ((NTH_BIT(ppu.at_shift_high, 7 - ppu.fine_x) << 1) | NTH_BIT(ppu.at_shift_low, 7 - ppu.fine_x)) << 2;
            }
        }

        if (ppu.mask.show_sprites && !(!ppu.mask.show_sprites_left && x < 8)) {
            for (i = 7; i >= 0; i--) {
                if (ppu.sprites[i].id == 64) {
                    //void entry
                    continue;
                }

                x = x - ppu.sprites[i].x;

                if (x >= 8) {
                    //out of range
                    continue;
                }

                if (ppu.sprites[i].attr & 0x40) {
                    //horizontal flip
                    x ^= 7;
                }

                sprite_palette = (NTH_BIT(ppu.sprites[i].data_high, 7 - x) << 1) || NTH_BIT(ppu.sprites[i].data_low, 7 - x);

                if (sprite_palette == 0) {
                    //transparent
                    continue;
                }

                if (ppu.sprites[i].id == 0 && palette > 0 && x != 255) {
                    ppu.status.sprite0_hit = 1;
                }

                sprite_palette |= (ppu.sprites[i].attr & 3) << 2;
                obj_palette = sprite_palette + 16;
                obj_priority = ppu.sprites[i].attr & 0x20;
            }
        }

        if (obj_palette > 0 && (palette == 0 || !obj_priority)) {
            palette = obj_palette;
        }

        ppu.pixels[ppu.scanline * 256 + x] = nes_rgb[ppu_read(0x3F00 + (ppu_rendering() ? palette : 0))];
    }

    ppu.bg_shift_low <<= 1;
    ppu.bg_shift_high <<= 1;

    ppu.at_shift_high = (ppu.at_shift_low << 1) | ppu.at_latch_low;
    ppu.at_shift_high = (ppu.at_shift_high << 1) | ppu.at_latch_high;
}

static void
ppu_cycle_execute(ppu_scanline_type_t type) {
    uint16_t address = 0;
    int ret;

    switch (type) {
        case PPU_SCANLINE_TYPE_VBLANK:
            if (ppu.dot == 1) {
                ppu.status.vblank = 1;
                if (ppu.control.nmi) {
                    cpu_set_nmi();
                }
            }
            break;
        case PPU_SCANLINE_TYPE_POST:
            if (ppu.dot == 0) {
                ret = SDL_UpdateTexture(ppu.texture, NULL, ppu.pixels, 256 * sizeof(uint32_t));
                if (ret == -1) {
                    log_err(MODULE, "Error updating texture: %s", SDL_GetError());
                }
            }
            break;
        case PPU_SCANLINE_TYPE_PRE:
        case PPU_SCANLINE_TYPE_VISIBLE:
            if (ppu.dot == 1) {
               ppu_clear_oam2();

                if (type == PPU_SCANLINE_TYPE_PRE) {
                    ppu.status.sprite_overflow = 0;
                    ppu.status.sprite0_hit = 0;
                }
            }
            else if (ppu.dot == 257) {
                ppu_evaluate_sprites();
            }
            else if (ppu.dot == 321) {
                ppu_load_sprites();
            }

            if (ppu.dot == 1) {
                address = ppu_nametable_address();
                if (type == PPU_SCANLINE_TYPE_PRE) {
                    ppu.status.vblank = 0;
                }
            }
            else if ((ppu.dot >= 2 && ppu.dot <= 255) || (ppu.dot >= 322 && ppu.dot <= 337)) {
                ppu_process_pixel();

                //nametable
                switch (ppu.dot % 8) {
                    case 1:
                        address = ppu_nametable_address();
                        ppu_reload_shift();
                        break;
                    case 2:
                        ppu.latch_nametable = ppu_read(address);
                        break;
                    case 3:
                        address = at_address();
                        break;
                    case 4:
                        ppu.latch_at = ppu_read(address);
                        if (ppu.v_address.coarse_y & 2) {
                            ppu.latch_at >>= 4;
                        }
                        if (ppu.v_address.coarse_x & 2) {
                            ppu.latch_at >>= 2;
                        }
                        break;
                    case 5:
                        address = bg_address();
                        break;
                    case 6:
                        ppu.latch_background_low = ppu_read(address);
                        break;
                    case 7:
                        address += 8;
                        break;
                    case 0:
                        ppu.latch_background_high = ppu_read(address);
                        ppu_horizontal_scroll();
                        break;

                }
            }
            else if (ppu.dot == 256) {
                ppu_process_pixel();
                ppu.latch_background_high = ppu_read(address);
                ppu_vertical_scroll();
            }
            else if (ppu.dot == 257) {
                ppu_process_pixel();
                ppu_reload_shift();
                ppu_horizontal_scroll_update();
            }
            else if (ppu.dot >= 280 && ppu.dot <= 304) {
                if (type == PPU_SCANLINE_TYPE_PRE) {
                    ppu_vertical_scroll_update();
                }
            }
            else if (ppu.dot == 321 || ppu.dot == 339) {
                address = ppu_nametable_address();
            }
            else if (ppu.dot == 338) {
                ppu.latch_nametable = ppu_read(address);
            }
            else if (ppu.dot == 340) {
                ppu.latch_nametable = ppu_read(address);
                if (type == PPU_SCANLINE_TYPE_PRE && ppu_rendering() && ppu.odd_frame) {
                    ++ppu.dot;
                }
            }

            if (ppu.dot == 260 && ppu_rendering()) {
                cartridge_signal_scanline();
            }

            break;
    }
}

void
ppu_cycle() {
    if (ppu.scanline >= 0 && ppu.scanline <= 239) {
        ppu_cycle_execute(PPU_SCANLINE_TYPE_VISIBLE);
    }
    else if (ppu.scanline == 240) {
        ppu_cycle_execute(PPU_SCANLINE_TYPE_POST);
    }
    else if (ppu.scanline == 241) {
        ppu_cycle_execute(PPU_SCANLINE_TYPE_VBLANK);
    }
    else if (ppu.scanline == 261) {
        ppu_cycle_execute(PPU_SCANLINE_TYPE_PRE);
    }

    if (++ppu.dot > 340) {
        ppu.dot %= 341;

        if (++ppu.scanline > 261) {
            ppu.scanline = 0;
            ppu.odd_frame = !ppu.odd_frame;
        }
    }
}