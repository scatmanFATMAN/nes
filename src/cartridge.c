#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "log.h"
#include "cartridge.h"

#define CARTRIDGE_HEADER_SIZE 16

typedef struct {
    int mapper;
    uint8_t *data;
    unsigned int prg_size;
    uint8_t *prg;     //pointer to the PRG section of data, do not free me
    unsigned int chr_size;
    uint8_t *chr;     //pointer to the CHR section data, for not free me
    bool nes_test;
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
}

bool
cartridge_is_nes_test() {
    return cartridge.nes_test;
}

//TODO: Handle reloading cartridges
bool
cartridge_load(const char *path) {
    unsigned int prg_ram_size, data_size;
    bool trainer;
    char header[CARTRIDGE_HEADER_SIZE];
    bool success = false;
    size_t count;
    FILE *f;

    log_info("Loading ROM %s", path);

    f = fopen(path, "rb");
    if (f == NULL) {
        log_err("Error opening ROM: %s", strerror(errno));
        return false;
    }

    count = fread(header, sizeof(char), sizeof(header), f);
    if (count != sizeof(header)) {
        log_err("Error reading ROM: Tried to read %zu bytes for the file's header but only read %zu", sizeof(header), count);
        goto done;
    }

    if (memcmp(header, "NES\x1A", 4) != 0) {
        log_err("Error reading ROM: Not a valid iNES format");
        goto done;
    }

    if ((header[7] & 0x0C) == 0x08) {
        log_err("Error reading ROM: iNES version 2 not supported");
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
    prg_ram_size = header[8];

    switch (cartridge.mapper) {
        case 0:
            break;
        default:
            log_err("Mapper %d not supported", cartridge.mapper);
            goto done;
            break;
    }

    log_info("Mapper %d, PRG Size: %d, CHR Size: %d, Trainer: %s, PRG RAM Size; %d", cartridge.mapper, cartridge.prg_size, cartridge.chr_size, trainer ? "Yes" : "No", prg_ram_size);

    data_size = CARTRIDGE_HEADER_SIZE + (trainer ? 512 : 0) + cartridge.prg_size + cartridge.chr_size;
    cartridge.data = malloc(data_size);
    if (cartridge.data == NULL) {
        log_err("Failed to allocate %u bytes for data storage", data_size);
        goto done;
    }

    rewind(f);
    count = fread(cartridge.data, sizeof(uint8_t), data_size, f);
    if (count != data_size) {
        log_err("Tried to read %zu bytes but only read %zu", data_size, count);
        free(cartridge.data);
        goto done;
    }

    //set pointers to specific data regions
    cartridge.prg = cartridge.data + CARTRIDGE_HEADER_SIZE + (trainer ? 512 : 0);
    cartridge.chr = cartridge.prg + cartridge.prg_size;

    cartridge.nes_test = strstr(path, "nestest.nes") != NULL;
    if (cartridge.nes_test) {
        log_info("Using test cartridge");
    }
    
    log_info("ROM loaded");
    success = true;

done:
    fclose(f);

    return success;
}

//TODO: support different mapper version
uint8_t
cartridge_read(uint16_t address) {
    uint8_t value = 0;

    switch (cartridge.mapper) {
        case 0:
            //TODO: FIX (maybe)?
            if (address >= 0xC000) {
                value = cartridge.prg[address - 0xC000];
            }
            else if (address >= 0x8000) {
                value = cartridge.prg[address - 0x8000];
            }
            else {
                log_err("Tried to read from cartridge address %04X but PRG RAM not supported yet", address);
            }

            break;
    }
    
    return value;
}