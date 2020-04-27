#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "log.h"
#include "ppu.h"
#include "cartridge.h"

#define MODULE "Cartridge"

#define CARTRIDGE_HEADER_SIZE 16

typedef struct {
    int mapper;
    unsigned char *data;
    unsigned int prg_size;
    unsigned char *prg;     //pointer to the PRG section of data, do not free me
    unsigned int chr_size;
    unsigned char *chr;     //pointer to the CHR section data, for not free me
    unsigned int prg_ram_size;
    unsigned char *prg_ram;
    uint32_t prg_map[4];
    uint32_t chr_map[8];
    bool nes_test;
    struct {
        uint8_t registers[1];
    } mapper3;
} cartridge_t;

static cartridge_t cartridge;

void
cartridge_init() {
    memset(&cartridge, 0, sizeof(cartridge));
}

void 
cartridge_free() {
    if (cartridge.data != NULL) {
        free(cartridge.data);
    }
    if (cartridge.prg_ram != NULL) {
        free(cartridge.prg_ram);
    }
}

bool
cartridge_is_nes_test() {
    return cartridge.nes_test;
}

static void
cartridge_map_prg(int page_kbs, int slot, int bank) {
    int i;

    if (bank < 0) {
        bank = (cartridge.prg_size / (0x400 * page_kbs)) + bank;
    }

    for (i = 0; i < page_kbs / 8; i++) {
        cartridge.prg_map[(page_kbs / 8) * slot + i] = (page_kbs * 0x400 * bank + 0x2000 * i) % cartridge.prg_size;
    }
}

static void
cartridge_map_chr(int page_kbs, int slot, int bank) {
    int i;

    for (i = 0; i < page_kbs; i++) {
        cartridge.chr_map[page_kbs * slot + i] = (page_kbs * 0x400 * bank + 0x400 * i) % cartridge.chr_size;
    }
}

//TODO: Handle reloading cartridges
bool
cartridge_load(const char *path) {
    unsigned int data_size;
    bool trainer;
    char header[CARTRIDGE_HEADER_SIZE];
    bool success = false, prg_size_16k;
    size_t count;
    FILE *f;

    log_info(MODULE, "Loading ROM %s", path);

    f = fopen(path, "rb");
    if (f == NULL) {
        log_err(MODULE, "Error opening ROM: %s", strerror(errno));
        return false;
    }

    count = fread(header, sizeof(char), sizeof(header), f);
    if (count != sizeof(header)) {
        log_err(MODULE, "Error reading ROM: Tried to read %zu bytes for the file's header but only read %zu", sizeof(header), count);
        goto done;
    }

    if (memcmp(header, "NES\x1A", 4) != 0) {
        log_err(MODULE, "Error reading ROM: Not a valid iNES format");
        goto done;
    }

    if ((header[7] & 0x0C) == 0x08) {
        log_err(MODULE, "Error reading ROM: iNES version 2 not supported");
        goto done;
    }

    //header[4] is the number of 16KB blocks of PRG ROM
    cartridge.prg_size = header[4] * 0x4000;

    //header[5] is the number of 8KB blocks of CHR ROM
    cartridge.chr_size = header[5] * 0x2000;

    //if a trainer is present, then the data is a 512 byte block between the header and the PRG ROM
    trainer = (header[6] >> 3) & 0x01;

    cartridge.mapper = (header[7] & 0xF0) | (header[6] >> 4);

    //PRG RAM size, if any exists
    cartridge.prg_ram_size = header[8];

    switch (cartridge.mapper) {
        case 0:            
            cartridge_map_prg(32, 0, 0);
            cartridge_map_chr(8, 0, 0);

            ppu_set_mirroring(header[6] & 0x01 ? PPU_MIRRORING_VERTICAL : PPU_MIRRORING_HORIZONTAL);
            break;
        case 3:
            prg_size_16k = header[4] == 1;

            if (prg_size_16k) {
                cartridge_map_prg(16, 0, 0);
                cartridge_map_prg(16, 1, 0);
            }
            else {
                cartridge_map_prg(16, 0, 0);
                cartridge_map_prg(16, 1, 1);
            }

            cartridge_map_chr(8, cartridge.mapper3.registers[0] & 0b11, 0);

            ppu_set_mirroring(header[6] & 0x01 ? PPU_MIRRORING_VERTICAL : PPU_MIRRORING_HORIZONTAL);
            break;
        default:
            log_err(MODULE, "Mapper %d not supported", cartridge.mapper);
            goto done;
            break;
    }

    log_info(MODULE, "Mapper %d, PRG Size: %d, CHR Size: %d, Trainer: %s, PRG RAM Size; %d", cartridge.mapper, cartridge.prg_size, cartridge.chr_size, trainer ? "Yes" : "No", cartridge.prg_ram_size);

    data_size = CARTRIDGE_HEADER_SIZE + (trainer ? 512 : 0) + cartridge.prg_size + cartridge.chr_size;
    cartridge.data = malloc(data_size);
    if (cartridge.data == NULL) {
        log_err(MODULE, "Failed to allocate %u bytes for data storage", data_size);
        goto done;
    }

    rewind(f);
    count = fread(cartridge.data, sizeof(uint8_t), data_size, f);
    if (count != data_size) {
        log_err(MODULE, "Tried to read %zu bytes but only read %zu", data_size, count);
        goto done;
    }

    if (cartridge.prg_ram_size > 0) {
        cartridge.prg_ram = malloc(cartridge.prg_ram_size);
        if (cartridge.prg_ram == NULL) {
            log_err(MODULE, "Failed to allocate %u bytes for PRG RAM", cartridge.prg_ram_size);
            goto done;
        }
    }

    //set pointers to specific data regions
    cartridge.prg = cartridge.data + CARTRIDGE_HEADER_SIZE + (trainer ? 512 : 0);
    cartridge.chr = cartridge.prg + cartridge.prg_size;

    cartridge.nes_test = strstr(path, "nestest.nes") != NULL;
    if (cartridge.nes_test) {
        log_info(MODULE, "Using test cartridge");
    }
    
    log_info(MODULE, "ROM loaded");
    success = true;

done:
    if (!success) {
        cartridge_unload();
    }

    fclose(f);

    return success;
}

void
cartridge_unload() {
    if (cartridge.data != NULL) {
        free(cartridge.data);
    }
    if (cartridge.prg_ram != NULL) {
        free(cartridge.prg_ram);
    }

    memset(&cartridge, 0, sizeof(cartridge));
}

//TODO: support different mapper version
uint8_t
cartridge_read(uint16_t address) {
    switch (cartridge.mapper) {
        case 0:
        case 3:
            if (address >= 0x8000) {
                return cartridge.prg[cartridge.prg_map[(address - 0x8000) / 0x2000] + ((address - 0x8000) % 0x2000)];
            }

            return cartridge.prg_ram[address - 0x6000];

            break;
    }
    
    return 0;
}

uint8_t
cartridge_read_chr(uint16_t address) {
    switch (cartridge.mapper) {
        case 0:
        case 3:
            return cartridge.chr[cartridge.chr_map[address / 0x400] + (address % 0x400)];
    }

    return 0;
}

void
cartridge_write(uint16_t address, uint8_t value) {
    switch (cartridge.mapper) {
        case 0:
            //mapper 0 is read only
            break;
        case 3:
            if (address & 0x8000) {
                cartridge.mapper3.registers[0] = value;
                cartridge_map_chr(8, cartridge.mapper3.registers[0] & 0b11, 0);
            }

            break;
    }
}

void
cartridge_write_chr(uint16_t address, uint8_t value) {
    switch (cartridge.mapper) {
        case 0:
            //mapper 0 is read only
            break;
        case 3:
            cartridge.chr[address] = value;
            break;
    }
}

void
cartridge_signal_scanline() {
    switch (cartridge.mapper) {
        case 0:
        case 3:
            break;
    }
}