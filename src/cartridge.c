#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "log.h"
#include "cpu.h"
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
    unsigned char *chr;     //pointer to the CHR section data.. need to free if i'm CHR ram
    bool chr_is_ram;
    unsigned int prg_ram_size;
    unsigned char *prg_ram;
    uint32_t prg_map[4];
    uint32_t chr_map[8];
    bool nes_test;
    struct {
        int write_n;
        uint8_t register_tmp;
        uint8_t registers[4];
    } mapper1;
    struct {
        bool prg_size_16k;
        uint8_t registers[1];
    } mapper3;
    struct {
        uint8_t register8000;
        uint8_t registers[8];
        bool horizontal_mirroring;
        uint8_t irq_period;
        uint8_t irq_counter;
        bool irq_enabled;
    } mapper4;
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
    if (cartridge.chr_is_ram && cartridge.chr != NULL) {
        free(cartridge.chr);
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

static void
cartridge_apply_mapper1() {
    //PRG
    if (cartridge.mapper1.registers[0] & 0b1000) {
        //16KB PRG
        if (cartridge.mapper1.registers[0] & 0b100) {
            cartridge_map_prg(16, 0, cartridge.mapper1.registers[3] & 0xF);
            cartridge_map_prg(16, 1, 0xF);
        }
        else {
            cartridge_map_prg(16, 0, 0);
            cartridge_map_prg(16, 1, cartridge.mapper1.registers[3] & 0xF);
        }
    }
    else {
        //32KB PRG
        cartridge_map_prg(32, 0, (cartridge.mapper1.registers[3] & 0xF) >> 1);
    }

    //CHR
    if (cartridge.mapper1.registers[0] & 0b10000) {
        //4KB CHR
        cartridge_map_chr(4, 0, cartridge.mapper1.registers[1]);
        cartridge_map_chr(4, 1, cartridge.mapper1.registers[2]);
    }
    else {
        //8KB CHR
        cartridge_map_chr(8, 0, cartridge.mapper1.registers[1] >> 1);
    }

    //mirroring
    switch (cartridge.mapper1.registers[0] & 0b11) {
        case 2:
            ppu_set_mirroring(PPU_MIRRORING_VERTICAL);
            break;
        case 3:
            ppu_set_mirroring(PPU_MIRRORING_HORIZONTAL);
            break;
        default:
            log_err(MODULE, "Error setting mirroring for mapper 1: Invalid register value %u (%u)", cartridge.mapper1.registers[0], cartridge.mapper1.registers[0] & 0b11);
            break;
    }
}

static void
cartridge_apply_mapper3() {
    if (cartridge.mapper3.prg_size_16k) {
        cartridge_map_prg(16, 0, 0);
        cartridge_map_prg(16, 1, 0);
    }
    else {
        cartridge_map_prg(16, 0, 0);
        cartridge_map_prg(16, 1, 1);
    }

    cartridge_map_chr(8, cartridge.mapper3.registers[0] & 0b11, 0);
}

static void
cartridge_apply_mapper4() {
    int i;

    cartridge_map_prg(8, 1, cartridge.mapper4.registers[7]);

    if (!(cartridge.mapper4.register8000 & 1 << 6)) {
        //PRG mode 0
        cartridge_map_prg(8, 0, cartridge.mapper4.registers[6]);
        cartridge_map_prg(8, 2, -2);
    }
    else {
        //PRG mode 
        cartridge_map_prg(8, 2, -2);
        cartridge_map_prg(8, 0, cartridge.mapper4.registers[6]);
    }

    if (!(cartridge.mapper4.register8000 & (1 << 7))) {
        //CHR mode 0
        cartridge_map_chr(2, 0, cartridge.mapper4.registers[0] >> 1);
        cartridge_map_chr(2, 1, cartridge.mapper4.registers[1] >> 1);
        for (i = 0; i < 4; i++) {
            cartridge_map_chr(1, 4 + i, cartridge.mapper4.registers[2 + i]);
        }
    }
    else {
        //CHR mode 1
        for (i = 0; i < 4; i++) {
            cartridge_map_chr(1, i, cartridge.mapper4.registers[2 + i]);
        }
        cartridge_map_chr(2, 2, cartridge.mapper4.registers[0] >> 1);
        cartridge_map_chr(2, 3, cartridge.mapper4.registers[1] >> 1);
    }

    ppu_set_mirroring(cartridge.mapper4.horizontal_mirroring ? PPU_MIRRORING_HORIZONTAL : PPU_MIRRORING_VERTICAL);
}

//TODO: Handle reloading cartridges
bool
cartridge_load(const char *path) {
    unsigned int data_size;
    bool trainer;
    char header[CARTRIDGE_HEADER_SIZE];
    bool success = false;
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

    if (cartridge.chr_size > 0) {
        cartridge.chr = cartridge.prg + cartridge.prg_size;
    }
    else {
        cartridge.chr_size = 0x2000;
        cartridge.chr = malloc(0x2000);
        cartridge.chr_is_ram = true;
    }

    switch (cartridge.mapper) {
        case 0:            
            cartridge_map_prg(32, 0, 0);
            cartridge_map_chr(8, 0, 0);

            ppu_set_mirroring(header[6] & 0x01 ? PPU_MIRRORING_VERTICAL : PPU_MIRRORING_HORIZONTAL);
            break;
        case 1:
            cartridge.mapper1.registers[0] = 0x0C;
            cartridge_apply_mapper1();
            break;
        case 3:
            cartridge.mapper3.prg_size_16k = header[4] == 1;
            ppu_set_mirroring(header[6] & 0x01 ? PPU_MIRRORING_VERTICAL : PPU_MIRRORING_HORIZONTAL);
            cartridge_apply_mapper3();
            break;
        case 4:
            cartridge.mapper4.horizontal_mirroring = true;
            cartridge_map_prg(8, 3, -1);
            cartridge_apply_mapper4();
            break;
        default:
            log_err(MODULE, "Mapper %d not supported", cartridge.mapper);
            goto done;
            break;
    }

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
    if (cartridge.chr_is_ram && cartridge.chr != NULL) {
        free(cartridge.chr);
    }

    memset(&cartridge, 0, sizeof(cartridge));
}

uint8_t
cartridge_read(uint16_t address) {
    switch (cartridge.mapper) {
        case 0:
        case 1:
        case 3:
        case 4:
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
        case 1:
        case 3:
        case 4:
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
        case 1:
            if (address < 0x8000) {
                cartridge.prg_ram[address - 0x6000] = value;
            }
            else if (address & 0x8000) {
                if (value & 0x80) {
                    cartridge.mapper1.write_n = 0;
                    cartridge.mapper1.register_tmp = 0;
                    cartridge.mapper1.registers[0] |= 0x0C;
                    cartridge_apply_mapper1();
                }
                else {
                    cartridge.mapper1.register_tmp = ((value & 1) << 4) | (cartridge.mapper1.register_tmp >> 1);
                    if (++cartridge.mapper1.write_n == 5) {
                        cartridge.mapper1.registers[(address >> 13) & 0xb11] = cartridge.mapper1.register_tmp;
                        cartridge.mapper1.write_n = 0;
                        cartridge.mapper1.register_tmp = 0;
                        cartridge_apply_mapper1();
                    }
                }
            }

            break;
        case 3:
            if (address & 0x8000) {
                cartridge.mapper3.registers[0] = value;
                cartridge_apply_mapper3();
            }

            break;
        case 4:
            if (address < 0x8000) {
                cartridge.prg_ram[address - 0x6000] = value;
            }
            else if (address & 0x8000) {
                switch (address & 0xE001) {
                    case 0x8000:
                        cartridge.mapper4.register8000 = value;
                        break;
                    case 0x8001:
                        cartridge.mapper4.registers[cartridge.mapper4.register8000 & 0b111] = value;
                        break;
                    case 0xA000:
                        cartridge.mapper4.horizontal_mirroring = value & 1;
                        break;
                    case 0xC000:
                        cartridge.mapper4.irq_period = value;
                        break;
                    case 0xC001:
                        cartridge.mapper4.irq_counter = 0;
                        break;
                    case 0xE000:
                        cpu_set_irq();
                        break;
                    case 0xE001:
                        cartridge.mapper4.irq_enabled = true;
                        break;
                    default:
                        log_err(MODULE, "Error writing to mapper4 address 0x%04X: Unhandled address", address);
                        break;
                }

                cartridge_apply_mapper4();
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
        case 1:
        case 3:
        case 4:
            cartridge.chr[address] = value;
            break;
    }
}

void
cartridge_signal_scanline() {
    switch (cartridge.mapper) {
        case 0:
        case 1:
        case 3:
            break;
        case 4:
            if (cartridge.mapper4.irq_counter == 0) {
                cartridge.mapper4.irq_counter = cartridge.mapper4.irq_period;
            }
            else {
                --cartridge.mapper4.irq_counter;
            }

            if (cartridge.mapper4.irq_enabled && cartridge.mapper4.irq_counter == 0) {
                cpu_set_irq();
            }

            break;
    }
}