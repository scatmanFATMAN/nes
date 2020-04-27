#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include "os.h"
#include "log.h"
#include "cartridge.h"
#include "ppu.h"
#include "cpu_test.h"
#include "cpu.h"

//description of opcodes and addressing modes: http://www.obelisk.me.uk/6502/reference.html

#define MODULE "CPU"

#define CPU_CYCLES_PER_FRAME 29781

#define CPU_FLAG_CARRY             (1 << 0)
#define CPU_FLAG_ZERO              (1 << 1)
#define CPU_FLAG_INTERRUPT_DISABLE (1 << 2)
#define CPU_FLAG_DECIMAL_MODE      (1 << 3)
#define CPU_FLAG_BREAK_COMMAND     (1 << 4)
#define CPU_FLAG_UNUSED            (1 << 5)
#define CPU_FLAG_OVERFLOW          (1 << 6)
#define CPU_FLAG_NEGATIVE          (1 << 7)

typedef enum {
    //invald instruction
    CPU_INSTRUCTION_INV,
    //official instructions
    CPU_INSTRUCTION_ADC, CPU_INSTRUCTION_AND, CPU_INSTRUCTION_ASL, CPU_INSTRUCTION_BCC, CPU_INSTRUCTION_BCS, CPU_INSTRUCTION_BEQ, 
    CPU_INSTRUCTION_BIT, CPU_INSTRUCTION_BMI, CPU_INSTRUCTION_BNE, CPU_INSTRUCTION_BPL, CPU_INSTRUCTION_BRK, CPU_INSTRUCTION_BVC,
    CPU_INSTRUCTION_BVS, CPU_INSTRUCTION_CLC, CPU_INSTRUCTION_CLD, CPU_INSTRUCTION_CLI, CPU_INSTRUCTION_CLV, CPU_INSTRUCTION_CMP,
    CPU_INSTRUCTION_CPX, CPU_INSTRUCTION_CPY, CPU_INSTRUCTION_DEC, CPU_INSTRUCTION_DEX, CPU_INSTRUCTION_DEY, CPU_INSTRUCTION_EOR,
    CPU_INSTRUCTION_INC, CPU_INSTRUCTION_INX, CPU_INSTRUCTION_INY, CPU_INSTRUCTION_JMP, CPU_INSTRUCTION_JSR, CPU_INSTRUCTION_LDA,
    CPU_INSTRUCTION_LDX, CPU_INSTRUCTION_LDY, CPU_INSTRUCTION_LSR, CPU_INSTRUCTION_NOP, CPU_INSTRUCTION_ORA, CPU_INSTRUCTION_PHA, 
    CPU_INSTRUCTION_PHP, CPU_INSTRUCTION_PLA, CPU_INSTRUCTION_PLP, CPU_INSTRUCTION_ROL, CPU_INSTRUCTION_ROR, CPU_INSTRUCTION_RTI,
    CPU_INSTRUCTION_RTS, CPU_INSTRUCTION_SBC, CPU_INSTRUCTION_SEC, CPU_INSTRUCTION_SED, CPU_INSTRUCTION_SEI, CPU_INSTRUCTION_STA,
    CPU_INSTRUCTION_STX, CPU_INSTRUCTION_STY, CPU_INSTRUCTION_TAX, CPU_INSTRUCTION_TAY, CPU_INSTRUCTION_TSX, CPU_INSTRUCTION_TXA,
    CPU_INSTRUCTION_TXS, CPU_INSTRUCTION_TYA,
    //unnofficial instructions
    CPU_INSTRUCTION_DCP, CPU_INSTRUCTION_IGN, CPU_INSTRUCTION_ISC, CPU_INSTRUCTION_LAX, CPU_INSTRUCTION_RLA, CPU_INSTRUCTION_RRA,
    CPU_INSTRUCTION_SAX, CPU_INSTRUCTION_SKB, CPU_INSTRUCTION_SLO, CPU_INSTRUCTION_SRE
} cpu_instruction_t;

typedef enum {  
    CPU_ADDR_MODE_IMP,      //implicit
    CPU_ADDR_MODE_ACC,      //accumulator
    CPU_ADDR_MODE_IMM,      //immediate
    CPU_ADDR_MODE_ZPG,      //zero paging
    CPU_ADDR_MODE_ZPX,      //zero paging x
    CPU_ADDR_MODE_ZPY,      //zero paging y
    CPU_ADDR_MODE_REL,      //relative
    CPU_ADDR_MODE_ABS,      //absolute
    CPU_ADDR_MODE_ABX,      //absolute x
    CPU_ADDR_MODE_ABY,      //absolute y
    CPU_ADDR_MODE_IND,      //indirect
    CPU_ADDR_MODE_IDX,      //indexed indirect
    CPU_ADDR_MODE_IDY       //indirect indexed
} cpu_addr_mode_t;

typedef enum {
    CPU_INTERRUPT_NMI,
    CPU_INTERRUPT_RESET,
    CPU_INTERRUPT_IRQ,
    CPU_INTERRUPT_BRK
} cpu_interrupt_t;

typedef struct {
    cpu_instruction_t instruction;
    cpu_addr_mode_t mode;
    void (*func)(cpu_addr_mode_t mode, int cycles);
    int cycles;
} cpu_instruction_map_t;

typedef struct {
    unsigned char memory[2048];
    uint16_t PC;                    //program counter
    uint8_t SP;                     //stack pointer
    uint8_t A;                      //accumulator
    uint8_t X;                      //x register
    uint8_t Y;                      //y register
    uint8_t flags;                  //processor flags
    int cycles_left;                //number of cycles left to process the frame
    bool nmi;
    bool paused;
} cpu_t;

static cpu_instruction_map_t instruction_map[0xFF + 1];
static cpu_t cpu;

static const char *
cpu_instruction_str(cpu_instruction_t instruction) {
    switch (instruction) {
        case CPU_INSTRUCTION_ADC: return "ADC";
        case CPU_INSTRUCTION_AND: return "AND";
        case CPU_INSTRUCTION_ASL: return "ASL";
        case CPU_INSTRUCTION_BCC: return "BCC";
        case CPU_INSTRUCTION_BCS: return "BCS";
        case CPU_INSTRUCTION_BEQ: return "BEQ";
        case CPU_INSTRUCTION_BIT: return "BIT";
        case CPU_INSTRUCTION_BMI: return "BMI";
        case CPU_INSTRUCTION_BNE: return "BNE";
        case CPU_INSTRUCTION_BPL: return "BPL";
        case CPU_INSTRUCTION_BRK: return "BRK";
        case CPU_INSTRUCTION_BVC: return "BVC";
        case CPU_INSTRUCTION_BVS: return "BVS";
        case CPU_INSTRUCTION_CLC: return "CLC";
        case CPU_INSTRUCTION_CLD: return "CLD";
        case CPU_INSTRUCTION_CLI: return "CLI";
        case CPU_INSTRUCTION_CLV: return "CLV";
        case CPU_INSTRUCTION_CMP: return "CMP";
        case CPU_INSTRUCTION_CPX: return "CPX";
        case CPU_INSTRUCTION_CPY: return "CPY";
        case CPU_INSTRUCTION_DCP: return "DCP";
        case CPU_INSTRUCTION_DEC: return "DEC";
        case CPU_INSTRUCTION_DEX: return "DEX";
        case CPU_INSTRUCTION_DEY: return "DEY";
        case CPU_INSTRUCTION_EOR: return "EOR";
        case CPU_INSTRUCTION_IGN: return "IGN";
        case CPU_INSTRUCTION_INC: return "INC";
        case CPU_INSTRUCTION_INX: return "INX";
        case CPU_INSTRUCTION_INY: return "INY";
        case CPU_INSTRUCTION_JMP: return "JMP";
        case CPU_INSTRUCTION_ISC: return "ISC";
        case CPU_INSTRUCTION_JSR: return "JSR";
        case CPU_INSTRUCTION_LAX: return "LAX";
        case CPU_INSTRUCTION_LDA: return "LDA";
        case CPU_INSTRUCTION_LDX: return "LDX";
        case CPU_INSTRUCTION_LDY: return "LDY";
        case CPU_INSTRUCTION_LSR: return "LSR";
        case CPU_INSTRUCTION_NOP: return "NOP";
        case CPU_INSTRUCTION_ORA: return "ORA";
        case CPU_INSTRUCTION_PHA: return "PHA";
        case CPU_INSTRUCTION_PHP: return "PHP";
        case CPU_INSTRUCTION_PLA: return "PLA";
        case CPU_INSTRUCTION_PLP: return "PLP";
        case CPU_INSTRUCTION_RLA: return "RLA";
        case CPU_INSTRUCTION_ROL: return "ROL";
        case CPU_INSTRUCTION_ROR: return "ROR";
        case CPU_INSTRUCTION_RRA: return "RRA";
        case CPU_INSTRUCTION_RTI: return "RTI";
        case CPU_INSTRUCTION_RTS: return "RTS";
        case CPU_INSTRUCTION_SAX: return "SAX";
        case CPU_INSTRUCTION_SBC: return "SBC";
        case CPU_INSTRUCTION_SEC: return "SEC";
        case CPU_INSTRUCTION_SED: return "SED";
        case CPU_INSTRUCTION_SEI: return "SEI";
        case CPU_INSTRUCTION_SKB: return "SKB";
        case CPU_INSTRUCTION_SLO: return "SLO";
        case CPU_INSTRUCTION_SRE: return "SRE";
        case CPU_INSTRUCTION_STA: return "STA";
        case CPU_INSTRUCTION_STX: return "STX";
        case CPU_INSTRUCTION_STY: return "STY";
        case CPU_INSTRUCTION_TAX: return "TAX";
        case CPU_INSTRUCTION_TAY: return "TAY";
        case CPU_INSTRUCTION_TSX: return "TSX";
        case CPU_INSTRUCTION_TXA: return "TXA";
        case CPU_INSTRUCTION_TXS: return "TXS";
        case CPU_INSTRUCTION_TYA: return "TYA";
        case CPU_INSTRUCTION_INV: break;
    }

    return "INV";
}

static const char *
cpu_address_mode_str(cpu_addr_mode_t mode) {
    switch (mode) {
        case CPU_ADDR_MODE_IMP: return "IMP";
        case CPU_ADDR_MODE_ACC: return "ACC";
        case CPU_ADDR_MODE_IMM: return "IMM";
        case CPU_ADDR_MODE_ZPG: return "ZPG";
        case CPU_ADDR_MODE_ZPX: return "ZPX";
        case CPU_ADDR_MODE_ZPY: return "ZPY";
        case CPU_ADDR_MODE_REL: return "REL";
        case CPU_ADDR_MODE_ABS: return "ABS";
        case CPU_ADDR_MODE_ABX: return "ABX";
        case CPU_ADDR_MODE_ABY: return "ABY";
        case CPU_ADDR_MODE_IND: return "IND";
        case CPU_ADDR_MODE_IDX: return "IDX";
        case CPU_ADDR_MODE_IDY: return "IDY";
    }

    return "UNK";
}

static void
cpu_instruction_map_set(int opcode, cpu_instruction_t instruction, cpu_addr_mode_t mode, void (*func)(cpu_addr_mode_t mode, int cycles), int cycles) {
    if (instruction_map[opcode].instruction != CPU_INSTRUCTION_INV) {
        log_err(MODULE, "Error mapping opcode 0x%02X: One already exists", opcode);
        return;
    }

    instruction_map[opcode].instruction = instruction;
    instruction_map[opcode].mode = mode;
    instruction_map[opcode].func = func;
    instruction_map[opcode].cycles = cycles;
}

static uint8_t
cpu_read(uint16_t address) {
    if (address >= 0x0000 && address <= 0x1FFF) {
        return cpu.memory[address];
    }
    if (address >= 0x2000 && address <= 0x3FFF) {
        return ppu_read_register(address % 8);
    }
    if (address >= 0x4000 && address <= 0x4013) {
    }
    if (address == 0x4014) {
    }
    if (address == 0x4015) {
    }
    if (address == 0x4016) {
    }
    if (address == 0x4017) {
    }
    if (address >= 0x4018 && address <= 0xFFFF) {
        return cartridge_read(address);
    }

    log_err(MODULE, "Tried to read memory at address 0x%04X, but address range is not supported yet", address);

    return 0;
}

//we can't just pass one address and read 2 bytes from the beginning because if this is a zero
//page 2 byte address, address1 could be the last address of the zero page and then address2
//would wrap to the first address of the zero page
static uint16_t
cpu_read_uint16(uint16_t address1, uint16_t address2) {
    return cpu_read(address1) | (cpu_read(address2) << 8);
}

static void
cpu_write(uint16_t address, uint8_t value) {
    if (address >= 0x0000 && address <= 0x1FFF) {
        cpu.memory[address] = value;
        return;
    }
    if (address >= 0x2000 && address <= 0x3FFF) {
        ppu_write_register(address % 8, value);
        return;
    }
    if (address >= 0x4000 && address <= 0x4013) {
    }
    if (address == 0x4014) {
    }
    if (address == 0x4015) {
    }
    if (address == 0x4016) {
    }
    if (address == 0x4017) {
    }
    if (address >= 0x4018 && address <= 0xFFFF) {
        cartridge_write(address, value);
        return;
    }

    log_err(MODULE, "Tried to write memory at address 0x%04X, but address range is not supported yet", address);
}

static void
cpu_stack_push(uint8_t value) {
    cpu_write(0x100 + cpu.SP--, value);
}

static void
cpu_stack_push_uint16(uint16_t value) {
    //stack grows down so we can't use cpu_write_uint16()
    cpu_write(0x100 + cpu.SP--, value >> 8);
    cpu_write(0x100 + cpu.SP--, value);
}

static uint8_t
cpu_stack_pop() {
    return cpu_read(0x100 + ++cpu.SP);
}

static uint16_t
cpu_stack_pop_uint16() {
    uint16_t value;

    //stack grows down so we can't use cpu_read_uint16()
    value = cpu_read(0x100 + ++cpu.SP);
    value |= cpu_read(0x100 + ++cpu.SP) << 8;

    return value;
}

static void
cpu_flag_set(uint8_t flag, bool value) {
    if (value) {
        cpu.flags |= flag;
    }
    else {
        cpu.flags &= ~flag;
    }
}

static bool
cpu_flag_is_set(uint8_t flag) {
    return cpu.flags & flag;
}

static void
cpu_cycle(int cycles) {
    int i;

    for (i = 0; i < cycles; i++) {
        ppu_cycle();
        ppu_cycle();
        ppu_cycle();

        --cpu.cycles_left;
    }
}

static void
cpu_interrupt(cpu_interrupt_t type) {
    static uint16_t vector[] = {0xFFFA, 0xFFFC, 0xFFFE, 0xFFFE};
    uint8_t flags;

    if (type != CPU_INTERRUPT_RESET) {
        flags = cpu.flags;

        //only modify a copy of the flags
        if (type == CPU_INTERRUPT_BRK) {
            flags |= CPU_FLAG_BREAK_COMMAND;
        }

        cpu_stack_push_uint16(cpu.PC);
        cpu_stack_push(flags);
    }
    else {
        cpu.SP -= 3;
    }

    cpu_flag_set(CPU_FLAG_INTERRUPT_DISABLE, true);

    if (cartridge_is_nes_test()) {
        cpu.PC = 0xC000;
    }
    else {
        cpu.PC = cpu_read_uint16(vector[type], vector[type] + 1);
    }

    if (type == CPU_INTERRUPT_NMI) {
        cpu.nmi = false;
    }

    //the BRK cycle maintenance is handled in the instructon map function (even though it's the same value)
    if (type != CPU_INTERRUPT_BRK) {
        cpu_cycle(7);
    }
}

static bool
cpu_page_cross2(uint16_t address1, uint16_t address2) {
    return (address1 & 0xFF00) != (address2 & 0xFF00);
}

//only supports going forward
static bool
cpu_page_cross(uint16_t address, uint8_t offset) {
    return cpu_page_cross2(address, address + offset);
}

static uint16_t
cpu_read_address(cpu_addr_mode_t mode, bool *page_crossed) {
    uint16_t address = 0;

    if (page_crossed != NULL) {
        *page_crossed = false;
    }

    switch (mode) {
        case CPU_ADDR_MODE_ABS:
            //memory location is the 16 bit value in the instruction
            address = cpu_read_uint16(cpu.PC, cpu.PC + 1);
            cpu.PC += sizeof(address);
            break;
        case CPU_ADDR_MODE_ABX:
            //memory location is the 16 bit value in the instruction
            address = cpu_read_uint16(cpu.PC, cpu.PC + 1);
            cpu.PC += sizeof(address);
            
            if (page_crossed != NULL && cpu_page_cross(address, cpu.X)) {
                *page_crossed = true;
            }

            address += cpu.X;
            break;
        case CPU_ADDR_MODE_ABY:
            //memory location is the 16 bit value in the instruction
            address = cpu_read_uint16(cpu.PC, cpu.PC + 1);
            cpu.PC += sizeof(address);

            if (page_crossed != NULL && cpu_page_cross(address, cpu.Y)) {
                *page_crossed = true;
            }

            address += cpu.Y;
            break;
        case CPU_ADDR_MODE_ACC:
            //do nothing
            break;
        case CPU_ADDR_MODE_IDX:
            //zero page address comes from the instruction, which is the location of another 16 bit address in the zero page
            //the X register is applied before reading the indirect address
            //the address must wrap if there's overflow so it stays in the zero page
            address = (cpu_read(cpu.PC++) + cpu.X) & 0xFF;
            address = cpu_read_uint16(address, (address + 1) & 0xFF);
            break;
        case CPU_ADDR_MODE_IDY:
            //zero page address comes from the instruction, which is the location of another 16 bit address in the zero page
            //the Y register is applied after reading the indirect address
            address = cpu_read(cpu.PC++);
            address = cpu_read_uint16(address, (address + 1) & 0xFF);

            if (page_crossed != NULL && cpu_page_cross(address, cpu.Y)) {
                *page_crossed = true;
            }

            //Y register is applied after
            address = address + cpu.Y;
            break;
        case CPU_ADDR_MODE_IMM:
            address = cpu.PC++;
            break;
        case CPU_ADDR_MODE_IMP:
            //do nothing
            break;
        case CPU_ADDR_MODE_IND:
            //memory location is the 16 bit value in the instruction
            address = cpu_read_uint16(cpu.PC, cpu.PC + 1);
            cpu.PC += sizeof(address);

            //read the address at that address
            address = cpu_read_uint16(address, (address & 0xFF00) | ((address + 1) & 0xFF));
            break;
        case CPU_ADDR_MODE_REL:
            //the offset for the branch instructions comes from the instruction, which is the next byte
            address = cpu.PC++;
            break;
        case CPU_ADDR_MODE_ZPG:
            //zero page address comes from the instruction
            address = cpu_read(cpu.PC++);
            break;
        case CPU_ADDR_MODE_ZPX:
            //zero page address comes from the instruction
            //the address wraps if it's passed the zero page addressable space
            address = (cpu_read(cpu.PC++) + cpu.X) & 0xFF;
            break;
        case CPU_ADDR_MODE_ZPY:
            //zero page address comes from the instruction
            //the address wraps if it's passed the zero page addressable space
            address = (cpu_read(cpu.PC++) + cpu.Y) & 0xFF;
            break;
        default:
            log_err(MODULE, "Unhandled addressing mode %d", mode);
            break;
    }

    return address;
}

static void
cpu_execute_adc_sbc(cpu_addr_mode_t mode, int cycles, bool subtract) {
    uint16_t address;
    uint8_t value;
    int16_t value2;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);

    value = cpu_read(address);
    if (subtract) {
        value ^= 0xFF;
    }

    value2 = cpu.A + value;
    if (cpu_flag_is_set(CPU_FLAG_CARRY)) {
        ++value2;
    }

    cpu_flag_set(CPU_FLAG_CARRY, value2 > 0xFF);
    cpu_flag_set(CPU_FLAG_OVERFLOW, ~(cpu.A ^ value) & (cpu.A ^ value2) & 0x80);

    cpu.A = value2;

    cpu_flag_set(CPU_FLAG_ZERO, cpu.A == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, cpu.A & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//add with carry
static void
cpu_execute_adc(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_adc_sbc(mode, cycles, false);
}

//subtract with carry
static void
cpu_execute_sbc(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_adc_sbc(mode, cycles, true);
}

//bitwise AND (and)
//exclusive OR (eor)
//include OR (ora)
static void
cpu_execute_bitwise(uint8_t value, int cycles, bool page_crossed) {
    cpu_flag_set(CPU_FLAG_ZERO, value == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//bitwise AND
static void
cpu_execute_and(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    cpu_execute_bitwise(cpu.A &= cpu_read(address), cycles, page_crossed);
}

//exclusive OR
static void
cpu_execute_eor(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    cpu_execute_bitwise(cpu.A ^= cpu_read(address), cycles, page_crossed);
}

//logical inclusive OR
static void
cpu_execute_ora(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    cpu_execute_bitwise(cpu.A |= cpu_read(address), cycles, page_crossed);
}

//logical shift left (asl)
//logical shift right (lsr)
static void
cpu_execute_shift(cpu_addr_mode_t mode, int cycles, bool left) {
    uint16_t address;
    uint8_t value;
    bool page_crossed;

    if (mode == CPU_ADDR_MODE_ACC) {
        page_crossed = false;

        cpu_flag_set(CPU_FLAG_CARRY, cpu.A & (left ? 0x80 : 0x01));

        cpu.A = left ? (cpu.A << 1) : (cpu.A >> 1);
        value = cpu.A;
    }
    else {
        address = cpu_read_address(mode, &page_crossed);
        value = cpu_read(address);

        cpu_flag_set(CPU_FLAG_CARRY, value & (left ? 0x80 : 0x01));

        value = left ? (value << 1) : (value >> 1);
        cpu_write(address, value);
    }

    cpu_flag_set(CPU_FLAG_ZERO, value == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//shift left
static void
cpu_execute_asl(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_shift(mode, cycles, true);
}

//logical shift right
static void
cpu_execute_lsr(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_shift(mode, cycles, false);
}

//branch if carry clear (bcc)
//branch if carry set (bcs)
//branch if equal (beq)
//branch if minus (bmi)
//branch if not equal (bne)
//branch if positive (bpl)
//branch if overflow clear (bvc)
//branch if overflow set (bvs)
static void
cpu_execute_branch(cpu_addr_mode_t mode, int cycles, uint8_t flag, bool flag_value) {
    uint16_t address;
    int8_t value;

    address = cpu_read_address(mode, NULL);

    //important that value is signed to support going backwards
    if (cpu_flag_is_set(flag) == flag_value) {
        value = cpu_read(address);

        cpu_cycle(1);

        //this page check has to support going backwards!!
        if (cpu_page_cross2(cpu.PC, cpu.PC + value)) {
            cpu_cycle(1);
        }

        cpu.PC += value;
    }

    cpu_cycle(cycles);
}

//branch if carry clear
static void
cpu_execute_bcc(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_branch(mode, cycles, CPU_FLAG_CARRY, false);
}

//branch if carry set
static void
cpu_execute_bcs(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_branch(mode, cycles, CPU_FLAG_CARRY, true);
}

//branch if equal
static void
cpu_execute_beq(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_branch(mode, cycles, CPU_FLAG_ZERO, true);
}

//branch if minus
static void
cpu_execute_bmi(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_branch(mode, cycles, CPU_FLAG_NEGATIVE, true);
}

//branch if not equal
static void
cpu_execute_bne(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_branch(mode, cycles, CPU_FLAG_ZERO, false);
}

//branch if positive
static void
cpu_execute_bpl(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_branch(mode, cycles, CPU_FLAG_NEGATIVE, false);
}

//branch if overflow clear
static void
cpu_execute_bvc(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_branch(mode, cycles, CPU_FLAG_OVERFLOW, false);
}

//branch if overflow set
static void
cpu_execute_bvs(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_branch(mode, cycles, CPU_FLAG_OVERFLOW, true);
}

//bit test
static void
cpu_execute_bit(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    uint16_t value;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    value = cpu_read(address);

    cpu_flag_set(CPU_FLAG_ZERO, (cpu.A & value) == 0);
    cpu_flag_set(CPU_FLAG_OVERFLOW, value & 0x40);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    cpu_cycle(cycles);
}

//force interrupt
static void
cpu_execute_brk(cpu_addr_mode_t mode, int cycles) {
    cpu_interrupt(CPU_INTERRUPT_BRK);

    cpu_flag_set(CPU_FLAG_BREAK_COMMAND, true);

    cpu_cycle(cycles);
}

//clear carry flag
static void
cpu_execute_clc(cpu_addr_mode_t mode, int cycles) {
    cpu_flag_set(CPU_FLAG_CARRY, false);
    cpu_cycle(cycles);
}

//clear decimal mode
static void
cpu_execute_cld(cpu_addr_mode_t mode, int cycles) {
    cpu_flag_set(CPU_FLAG_DECIMAL_MODE, false);
    cpu_cycle(cycles);
}

//clear interrupt disable
static void
cpu_execute_cli(cpu_addr_mode_t mode, int cycles) {
    cpu_flag_set(CPU_FLAG_INTERRUPT_DISABLE, false);
    cpu_cycle(cycles);
}

//clear overflow flag
static void
cpu_execute_clv(cpu_addr_mode_t mode, int cycles) {
    cpu_flag_set(CPU_FLAG_OVERFLOW, false);
    cpu_cycle(cycles);
}

//compare (cmp)
//compare x register (cpx)
//compare y register (cpy)
static void
cpu_execute_compare(cpu_addr_mode_t mode, int cycles, uint8_t value_compare) {
    uint16_t address;
    uint8_t value;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    value = cpu_read(address);

    cpu_flag_set(CPU_FLAG_CARRY, value_compare >= value);
    cpu_flag_set(CPU_FLAG_ZERO, value_compare == value);
    cpu_flag_set(CPU_FLAG_NEGATIVE, (value_compare - value) & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//compare
static void
cpu_execute_cmp(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_compare(mode, cycles, cpu.A);
}

//compare x register
static void
cpu_execute_cpx(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_compare(mode, cycles, cpu.X);
}

//compare y register
static void
cpu_execute_cpy(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_compare(mode, cycles, cpu.Y);
}

//decrement and compare (DEC + CMP)
static void
cpu_execute_dcp(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    uint8_t value;

    address = cpu_read_address(mode, NULL);
    value = cpu_read(address) - 1;

    cpu_write(address, value);

    //TODO: do we need to check these?
    cpu_flag_set(CPU_FLAG_CARRY, cpu.A >= value);
    cpu_flag_set(CPU_FLAG_ZERO, cpu.A == value);
    cpu_flag_set(CPU_FLAG_NEGATIVE, (cpu.A - value) & 0x80);

    cpu_cycle(cycles);
}

//ignore value
static void
cpu_execute_ign(cpu_addr_mode_t mode, int cycles) {
    bool page_crossed;
    uint16_t address;

    address = cpu_read_address(mode, &page_crossed);
    cpu_read(address);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//decrement memory (dec)
//decrement x register (dex)
//decrement y register (dey)
//increment memory (inc)
//increment x register (inx)
//increment y register (iny)
static void
cpu_execute_inc_dec(cpu_addr_mode_t mode, int cycles, uint8_t *register_value, bool inc) {
    uint16_t address;
    uint8_t value;
    bool page_crossed;

    //are we setting a register or a memory value?
    if (register_value == NULL) {
        address = cpu_read_address(mode, &page_crossed);
        value = cpu_read(address);
        value = inc ? (value + 1) : (value - 1);

        cpu_write(address, value);
    }
    else {
        page_crossed = false;
        value = inc ? ++(*register_value) : --(*register_value);
    }

    cpu_flag_set(CPU_FLAG_ZERO, value == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//decrement memory
static void
cpu_execute_dec(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_inc_dec(mode, cycles, NULL, false);
}

//decrement x register
static void
cpu_execute_dex(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_inc_dec(mode, cycles, &cpu.X, false);
}

//decrement y register
static void
cpu_execute_dey(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_inc_dec(mode, cycles, &cpu.Y, false);
}

//increment memory
static void
cpu_execute_inc(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_inc_dec(mode, cycles, NULL, true);
}

//increment x register
static void
cpu_execute_inx(cpu_addr_mode_t mode, int cycles) {
   cpu_execute_inc_dec(mode, cycles, &cpu.X, true);
}

//increment y register
static void
cpu_execute_iny(cpu_addr_mode_t mode, int cycles) {
   cpu_execute_inc_dec(mode, cycles, &cpu.Y, true);
}

//INC + SBC
//TODO: are the flags set correctly?
static void
cpu_execute_isc(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    int16_t value2;
    uint8_t value;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    value = cpu_read(address);

    //inc
    ++value;
    cpu_write(address, value);
    cpu_flag_set(CPU_FLAG_ZERO, value == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    //sbc
    value2 = cpu.A + (value ^ 0xFF);
    if (cpu_flag_is_set(CPU_FLAG_CARRY)) {
        ++value2;
    }

    cpu_flag_set(CPU_FLAG_CARRY, value2 > 0xFF);
    cpu_flag_set(CPU_FLAG_OVERFLOW, ~(cpu.A ^ value) & (cpu.A ^ value2) & 0x80);
        
    cpu.A = value2;

    cpu_flag_set(CPU_FLAG_ZERO, cpu.A == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, cpu.A & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        //TODO: don't care about page crossing?
        //cpu_cycle(1);
    }
}

//jump
static void
cpu_execute_jmp(cpu_addr_mode_t mode, int cycles) {
    cpu.PC = cpu_read_address(mode, NULL);

    cpu_cycle(cycles);
}

//jump to subroutine
static void
cpu_execute_jsr(cpu_addr_mode_t mode, int cycles) {
    cpu_stack_push_uint16(cpu.PC + 1);

    cpu.PC = cpu_read_address(mode, NULL);

    cpu_cycle(cycles);
}

//load accumulator and X in one instruction
static void
cpu_execute_lax(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    cpu.A = cpu.X = cpu_read(address);

    cpu_flag_set(CPU_FLAG_ZERO, cpu.A == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, cpu.A & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//load accumulator (lda)
//load x register (ldx)
//load y register (ldy)
static void
cpu_execute_load(cpu_addr_mode_t mode, int cycles, uint8_t *reg) {
    uint16_t address;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    *reg = cpu_read(address);

    cpu_flag_set(CPU_FLAG_ZERO, *reg == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, *reg & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//load accumulator
static void
cpu_execute_lda(cpu_addr_mode_t mode, int cycles) {
   cpu_execute_load(mode, cycles, &cpu.A);
}

//load x register
static void
cpu_execute_ldx(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_load(mode, cycles, &cpu.X);
}

//load y register
static void
cpu_execute_ldy(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_load(mode, cycles, &cpu.Y);
}

//no operation
static void
cpu_execute_nop(cpu_addr_mode_t mode, int cycles) {
    cpu_cycle(cycles);
}

//push accumulator
static void
cpu_execute_pha(cpu_addr_mode_t mode, int cycles) {
    cpu_stack_push(cpu.A);

    cpu_cycle(cycles);
}

//push processor status
static void
cpu_execute_php(cpu_addr_mode_t mode, int cycles) {
    //this flag aways get set, and don't modify the original
    cpu_stack_push(cpu.flags | CPU_FLAG_BREAK_COMMAND);

    cpu_cycle(cycles);
}

//pull accumulator
static void
cpu_execute_pla(cpu_addr_mode_t mode, int cycles) {
    cpu.A = cpu_stack_pop();

    cpu_flag_set(CPU_FLAG_ZERO, cpu.A == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, cpu.A & 0x80);

    cpu_cycle(cycles);
}

//pull processor status
static void
cpu_execute_plp(cpu_addr_mode_t mode, int cycles) {
    uint8_t flags = cpu_stack_pop();

    cpu_flag_set(CPU_FLAG_CARRY, flags & CPU_FLAG_CARRY);
    cpu_flag_set(CPU_FLAG_ZERO, flags & CPU_FLAG_ZERO);
    cpu_flag_set(CPU_FLAG_INTERRUPT_DISABLE, flags & CPU_FLAG_INTERRUPT_DISABLE);
    cpu_flag_set(CPU_FLAG_DECIMAL_MODE, flags & CPU_FLAG_DECIMAL_MODE);
    //don't mess with 4 or 5
    cpu_flag_set(CPU_FLAG_OVERFLOW, flags & CPU_FLAG_OVERFLOW);
    cpu_flag_set(CPU_FLAG_NEGATIVE, flags & CPU_FLAG_NEGATIVE);

    cpu_cycle(cycles);
}

//ROL + AND
//TODO: are these flags are correctly?
static void
cpu_execute_rla(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    uint8_t value, wrap;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    value = cpu_read(address);

    //rol
    wrap = cpu_flag_is_set(CPU_FLAG_CARRY);
    cpu_flag_set(CPU_FLAG_CARRY, value & 0x80);

    value = (value << 1) | wrap;

    cpu_write(address, value);

    cpu_flag_set(CPU_FLAG_ZERO, value == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    //and
    cpu.A &= value;

    cpu_flag_set(CPU_FLAG_ZERO, cpu.A == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, cpu.A & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        //cpu_cycle(1);
        //TODO: ignore page crossing?
    }
}

//rotate left (rol)
//rotate right (ror)
static void
cpu_execute_rotate(cpu_addr_mode_t mode, int cycles, bool left) {
    uint16_t address;
    uint8_t value, wrap;
    bool page_crossed;

    wrap = cpu_flag_is_set(CPU_FLAG_CARRY);
    if (!left) {
        wrap <<= 7;
    }

    if (mode == CPU_ADDR_MODE_ACC) {
        page_crossed = false;

        cpu_flag_set(CPU_FLAG_CARRY, cpu.A & (left ? 0x80 : 0x01));

        cpu.A = left ? ((cpu.A << 1) | wrap) : (wrap | (cpu.A >> 1));
        value = cpu.A;
    }
    else {
        address = cpu_read_address(mode, &page_crossed);
        value = cpu_read(address);

        cpu_flag_set(CPU_FLAG_CARRY, value & (left ? 0x80 : 0x01));

        value = left ? ((value << 1) | wrap) : (wrap | (value >> 1));
        cpu_write(address, value);
    }

    cpu_flag_set(CPU_FLAG_ZERO, value == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//rotate left
static void
cpu_execute_rol(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_rotate(mode, cycles, true);
}

//rotate right
static void
cpu_execute_ror(cpu_addr_mode_t mode, int cycles) {
   cpu_execute_rotate(mode, cycles, false);
}

//ROR + ADC
//TODO: are these flags set correctly?
static void
cpu_execute_rra(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    uint8_t value, wrap;
    int16_t value2;
    bool page_crossed;

     address = cpu_read_address(mode, &page_crossed);
     value = cpu_read(address);

    //ror
    wrap = cpu_flag_is_set(CPU_FLAG_CARRY) << 7;

    cpu_flag_set(CPU_FLAG_CARRY, value & 0x01);

    value =  wrap | (value >> 1);
    cpu_write(address, value);

    cpu_flag_set(CPU_FLAG_ZERO, value == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    //adc
    value2 = cpu.A + value;
    if (cpu_flag_is_set(CPU_FLAG_CARRY)) {
        ++value2;
    }

    cpu_flag_set(CPU_FLAG_CARRY, value2 > 0xFF);
    cpu_flag_set(CPU_FLAG_OVERFLOW, ~(cpu.A ^ value) & (cpu.A ^ value2) & 0x80);

    cpu.A = value2;

    cpu_flag_set(CPU_FLAG_ZERO, cpu.A == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, cpu.A & 0x80);
    

    cpu_cycle(cycles);
    if (page_crossed) {
        //cpu_cycle(1);
        //TODO: is this ignored?
    }
}

//return from interrupt
static void
cpu_execute_rti(cpu_addr_mode_t mode, int cycles) {
    uint8_t flags = cpu_stack_pop();

    cpu_flag_set(CPU_FLAG_CARRY, flags & CPU_FLAG_CARRY);
    cpu_flag_set(CPU_FLAG_ZERO, flags & CPU_FLAG_ZERO);
    cpu_flag_set(CPU_FLAG_INTERRUPT_DISABLE, flags & CPU_FLAG_INTERRUPT_DISABLE);
    cpu_flag_set(CPU_FLAG_DECIMAL_MODE, flags & CPU_FLAG_DECIMAL_MODE);
    //don't mess with 4 or 5
    cpu_flag_set(CPU_FLAG_OVERFLOW, flags & CPU_FLAG_OVERFLOW);
    cpu_flag_set(CPU_FLAG_NEGATIVE, flags & CPU_FLAG_NEGATIVE);

    cpu.PC = cpu_stack_pop_uint16();

    cpu_cycle(cycles);
}

//return from subroutine
static void
cpu_execute_rts(cpu_addr_mode_t mode, int cycles) {
    cpu.PC = cpu_stack_pop_uint16() + 1;

    cpu_cycle(cycles);
}

//bitwise AND of A and X (AND + STX)
static void
cpu_execute_sax(cpu_addr_mode_t mode, int cycles) {
    bool page_crossed;
    uint16_t address;

    address = cpu_read_address(mode, &page_crossed);
    cpu_write(address, cpu.A & cpu.X);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//set carry flag
static void
cpu_execute_sec(cpu_addr_mode_t mode, int cycles) {
    cpu_flag_set(CPU_FLAG_CARRY, true);

    cpu_cycle(cycles);
}

//set decimal flag
static void
cpu_execute_sed(cpu_addr_mode_t mode, int cycles) {
    cpu_flag_set(CPU_FLAG_DECIMAL_MODE, true);

    cpu_cycle(cycles);
}

//set interrupt disable
static void
cpu_execute_sei(cpu_addr_mode_t mode, int cycles) {
    cpu_flag_set(CPU_FLAG_INTERRUPT_DISABLE, true);

    cpu_cycle(cycles);
}

//skb??
static void
cpu_execute_skb(cpu_addr_mode_t mode, int cycles) {
    bool page_crossed;
    uint16_t address;

    address = cpu_read_address(mode, &page_crossed);
    cpu_read(address);

    cpu_cycle(cycles);
    if (page_crossed) {
        cpu_cycle(1);
    }
}

//ASL + ORA
//TODO: Are these flags set correctly?
static void
cpu_execute_slo(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    uint8_t value;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    value = cpu_read(address);

    //asl
    cpu_flag_set(CPU_FLAG_CARRY, value & 0x80);
    value <<= 1;
    cpu_write(address, value);
    cpu_flag_set(CPU_FLAG_ZERO, value == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    //ora
    cpu.A |= value;
    cpu_flag_set(CPU_FLAG_ZERO, cpu.A == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, cpu.A & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        //cpu_cycle(1);
        //TODO: ignore page crossing?
    }
}

//LSR + EOR
//TODO: are these flags set correctly?
static void
cpu_execute_sre(cpu_addr_mode_t mode, int cycles) {
    uint16_t address;
    uint8_t value;
    bool page_crossed;

    address = cpu_read_address(mode, &page_crossed);
    value = cpu_read(address);

    //lsr
    cpu_flag_set(CPU_FLAG_CARRY, value & 0x01);
    value >>= 1;
    cpu_write(address, value);
    cpu_flag_set(CPU_FLAG_ZERO, value == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, value & 0x80);

    //eor
    cpu.A ^= value;
    cpu_flag_set(CPU_FLAG_ZERO, cpu.A == 0);
    cpu_flag_set(CPU_FLAG_NEGATIVE, cpu.A & 0x80);

    cpu_cycle(cycles);
    if (page_crossed) {
        //cpu_cycle(1);
        //TODO: ignore page crossing?
    }
}

//store accumulator (sta)
//store x register (stx)
//store y register (sty)
static void
cpu_execute_store(cpu_addr_mode_t mode, int cycles, uint8_t value) {
    uint16_t address;

    address = cpu_read_address(mode, NULL);
    cpu_write(address, value);

    cpu_cycle(cycles);
}

//store accumulator
static void
cpu_execute_sta(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_store(mode, cycles, cpu.A);
}

//store x register
static void
cpu_execute_stx(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_store(mode, cycles, cpu.X);
}

//store y register
static void
cpu_execute_sty(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_store(mode, cycles, cpu.Y);
}

//transfer accumulator to x (tax)
//transfer accumulator to y (tay)
//transfer stack pointer to x (tsx)
//transfer x to accumulator (txa)
//transfer x to stack pointer (txs)
//transfer y to accumulator (tya)
static void
cpu_execute_transfer(cpu_addr_mode_t mode, int cycles, uint8_t from, uint8_t *to) {
    *to = from;

    //don't check flags for TAX
    if (to != &cpu.SP) {
        cpu_flag_set(CPU_FLAG_ZERO, *to == 0);
        cpu_flag_set(CPU_FLAG_NEGATIVE, *to & 0x80);
    }

    cpu_cycle(cycles);
}

//transfer accumulator to x
static void
cpu_execute_tax(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_transfer(mode, cycles, cpu.A, &cpu.X);
}

//transfer accumulator to y
static void
cpu_execute_tay(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_transfer(mode, cycles, cpu.A, &cpu.Y);
}

//transfer stack pointer to x
static void
cpu_execute_tsx(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_transfer(mode, cycles, cpu.SP, &cpu.X);
}

//transfer x to accumulator
static void
cpu_execute_txa(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_transfer(mode, cycles, cpu.X, &cpu.A);
}

//transfer x to stack pointer
static void
cpu_execute_txs(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_transfer(mode, cycles, cpu.X, &cpu.SP);
}

//transfer y to accumulator
static void
cpu_execute_tya(cpu_addr_mode_t mode, int cycles) {
    cpu_execute_transfer(mode, cycles, cpu.Y, &cpu.A);
}

void
cpu_init() {
    memset(&cpu, 0, sizeof(cpu));
    memset(&instruction_map, 0, sizeof(instruction_map));

    cpu_instruction_map_set(0x69, CPU_INSTRUCTION_ADC, CPU_ADDR_MODE_IMM, cpu_execute_adc, 2);
    cpu_instruction_map_set(0x65, CPU_INSTRUCTION_ADC, CPU_ADDR_MODE_ZPG, cpu_execute_adc, 3);
    cpu_instruction_map_set(0x75, CPU_INSTRUCTION_ADC, CPU_ADDR_MODE_ZPX, cpu_execute_adc, 4);
    cpu_instruction_map_set(0x6D, CPU_INSTRUCTION_ADC, CPU_ADDR_MODE_ABS, cpu_execute_adc, 4);
    cpu_instruction_map_set(0x7D, CPU_INSTRUCTION_ADC, CPU_ADDR_MODE_ABX, cpu_execute_adc, 4);
    cpu_instruction_map_set(0x79, CPU_INSTRUCTION_ADC, CPU_ADDR_MODE_ABY, cpu_execute_adc, 4);
    cpu_instruction_map_set(0x61, CPU_INSTRUCTION_ADC, CPU_ADDR_MODE_IDX, cpu_execute_adc, 6);
    cpu_instruction_map_set(0x71, CPU_INSTRUCTION_ADC, CPU_ADDR_MODE_IDY, cpu_execute_adc, 5);
    
    cpu_instruction_map_set(0x29, CPU_INSTRUCTION_AND, CPU_ADDR_MODE_IMM, cpu_execute_and, 2);
    cpu_instruction_map_set(0x25, CPU_INSTRUCTION_AND, CPU_ADDR_MODE_ZPG, cpu_execute_and, 3);
    cpu_instruction_map_set(0x35, CPU_INSTRUCTION_AND, CPU_ADDR_MODE_ZPX, cpu_execute_and, 4);
    cpu_instruction_map_set(0x2D, CPU_INSTRUCTION_AND, CPU_ADDR_MODE_ABS, cpu_execute_and, 4);
    cpu_instruction_map_set(0x3D, CPU_INSTRUCTION_AND, CPU_ADDR_MODE_ABX, cpu_execute_and, 4);
    cpu_instruction_map_set(0x39, CPU_INSTRUCTION_AND, CPU_ADDR_MODE_ABY, cpu_execute_and, 4);
    cpu_instruction_map_set(0x21, CPU_INSTRUCTION_AND, CPU_ADDR_MODE_IDX, cpu_execute_and, 6);
    cpu_instruction_map_set(0x31, CPU_INSTRUCTION_AND, CPU_ADDR_MODE_IDY, cpu_execute_and, 5);

    cpu_instruction_map_set(0x0A, CPU_INSTRUCTION_ASL, CPU_ADDR_MODE_ACC, cpu_execute_asl, 2);
    cpu_instruction_map_set(0x06, CPU_INSTRUCTION_ASL, CPU_ADDR_MODE_ZPG, cpu_execute_asl, 5);
    cpu_instruction_map_set(0x16, CPU_INSTRUCTION_ASL, CPU_ADDR_MODE_ZPX, cpu_execute_asl, 6);
    cpu_instruction_map_set(0x0E, CPU_INSTRUCTION_ASL, CPU_ADDR_MODE_ABS, cpu_execute_asl, 6);
    cpu_instruction_map_set(0x1E, CPU_INSTRUCTION_ASL, CPU_ADDR_MODE_ABX, cpu_execute_asl, 7);

    cpu_instruction_map_set(0x90, CPU_INSTRUCTION_BCC, CPU_ADDR_MODE_REL, cpu_execute_bcc, 2);

    cpu_instruction_map_set(0xB0, CPU_INSTRUCTION_BCS, CPU_ADDR_MODE_REL, cpu_execute_bcs, 2);

    cpu_instruction_map_set(0xF0, CPU_INSTRUCTION_BEQ, CPU_ADDR_MODE_REL, cpu_execute_beq, 2);

    cpu_instruction_map_set(0x24, CPU_INSTRUCTION_BIT, CPU_ADDR_MODE_ZPG, cpu_execute_bit, 3);
    cpu_instruction_map_set(0x2C, CPU_INSTRUCTION_BIT, CPU_ADDR_MODE_ABS, cpu_execute_bit, 4);

    cpu_instruction_map_set(0x30, CPU_INSTRUCTION_BMI, CPU_ADDR_MODE_REL, cpu_execute_bmi, 2);

    cpu_instruction_map_set(0xD0, CPU_INSTRUCTION_BNE, CPU_ADDR_MODE_REL, cpu_execute_bne, 2);

    cpu_instruction_map_set(0x10, CPU_INSTRUCTION_BPL, CPU_ADDR_MODE_REL, cpu_execute_bpl, 2);

    cpu_instruction_map_set(0x00, CPU_INSTRUCTION_BRK, CPU_ADDR_MODE_IMP, cpu_execute_brk, 7);

    cpu_instruction_map_set(0x50, CPU_INSTRUCTION_BVC, CPU_ADDR_MODE_REL, cpu_execute_bvc, 2);

    cpu_instruction_map_set(0x70, CPU_INSTRUCTION_BVS, CPU_ADDR_MODE_REL, cpu_execute_bvs, 2);

    cpu_instruction_map_set(0x18, CPU_INSTRUCTION_CLC, CPU_ADDR_MODE_IMP, cpu_execute_clc, 2);

    cpu_instruction_map_set(0xD8, CPU_INSTRUCTION_CLD, CPU_ADDR_MODE_IMP, cpu_execute_cld, 2);

    cpu_instruction_map_set(0x58, CPU_INSTRUCTION_CLI, CPU_ADDR_MODE_IMP, cpu_execute_cli, 2);

    cpu_instruction_map_set(0xB8, CPU_INSTRUCTION_CLV, CPU_ADDR_MODE_IMP, cpu_execute_clv, 2);

    cpu_instruction_map_set(0xC9, CPU_INSTRUCTION_CMP, CPU_ADDR_MODE_IMM, cpu_execute_cmp, 2);
    cpu_instruction_map_set(0xC5, CPU_INSTRUCTION_CMP, CPU_ADDR_MODE_ZPG, cpu_execute_cmp, 3);
    cpu_instruction_map_set(0xD5, CPU_INSTRUCTION_CMP, CPU_ADDR_MODE_ZPX, cpu_execute_cmp, 4);
    cpu_instruction_map_set(0xCD, CPU_INSTRUCTION_CMP, CPU_ADDR_MODE_ABS, cpu_execute_cmp, 4);
    cpu_instruction_map_set(0xDD, CPU_INSTRUCTION_CMP, CPU_ADDR_MODE_ABX, cpu_execute_cmp, 4);
    cpu_instruction_map_set(0xD9, CPU_INSTRUCTION_CMP, CPU_ADDR_MODE_ABY, cpu_execute_cmp, 4);
    cpu_instruction_map_set(0xC1, CPU_INSTRUCTION_CMP, CPU_ADDR_MODE_IDX, cpu_execute_cmp, 6);
    cpu_instruction_map_set(0xD1, CPU_INSTRUCTION_CMP, CPU_ADDR_MODE_IDY, cpu_execute_cmp, 5);

    cpu_instruction_map_set(0xE0, CPU_INSTRUCTION_CPX, CPU_ADDR_MODE_IMM, cpu_execute_cpx, 2);
    cpu_instruction_map_set(0xE4, CPU_INSTRUCTION_CPX, CPU_ADDR_MODE_ZPG, cpu_execute_cpx, 3);
    cpu_instruction_map_set(0xEC, CPU_INSTRUCTION_CPX, CPU_ADDR_MODE_ABS, cpu_execute_cpx, 4);

    cpu_instruction_map_set(0xC0, CPU_INSTRUCTION_CPY, CPU_ADDR_MODE_IMM, cpu_execute_cpy, 2);
    cpu_instruction_map_set(0xC4, CPU_INSTRUCTION_CPY, CPU_ADDR_MODE_ZPG, cpu_execute_cpy, 3);
    cpu_instruction_map_set(0xCC, CPU_INSTRUCTION_CPY, CPU_ADDR_MODE_ABS, cpu_execute_cpy, 4);

    cpu_instruction_map_set(0xC6, CPU_INSTRUCTION_DEC, CPU_ADDR_MODE_ZPG, cpu_execute_dec, 5);
    cpu_instruction_map_set(0xD6, CPU_INSTRUCTION_DEC, CPU_ADDR_MODE_ZPX, cpu_execute_dec, 6);
    cpu_instruction_map_set(0xCE, CPU_INSTRUCTION_DEC, CPU_ADDR_MODE_ABS, cpu_execute_dec, 6);
    cpu_instruction_map_set(0xDE, CPU_INSTRUCTION_DEC, CPU_ADDR_MODE_ABX, cpu_execute_dec, 7);

    cpu_instruction_map_set(0xCA, CPU_INSTRUCTION_DEX, CPU_ADDR_MODE_IMP, cpu_execute_dex, 2);

    cpu_instruction_map_set(0x88, CPU_INSTRUCTION_DEY, CPU_ADDR_MODE_IMP, cpu_execute_dey, 2);

    cpu_instruction_map_set(0xC3, CPU_INSTRUCTION_DCP, CPU_ADDR_MODE_IDX, cpu_execute_dcp, 8);
    cpu_instruction_map_set(0xC7, CPU_INSTRUCTION_DCP, CPU_ADDR_MODE_ZPG, cpu_execute_dcp, 5);
    cpu_instruction_map_set(0xCF, CPU_INSTRUCTION_DCP, CPU_ADDR_MODE_ABS, cpu_execute_dcp, 6);
    cpu_instruction_map_set(0xD3, CPU_INSTRUCTION_DCP, CPU_ADDR_MODE_IDY, cpu_execute_dcp, 8);
    cpu_instruction_map_set(0xD7, CPU_INSTRUCTION_DCP, CPU_ADDR_MODE_ZPX, cpu_execute_dcp, 6);
    cpu_instruction_map_set(0xDB, CPU_INSTRUCTION_DCP, CPU_ADDR_MODE_ABY, cpu_execute_dcp, 7);
    cpu_instruction_map_set(0xDF, CPU_INSTRUCTION_DCP, CPU_ADDR_MODE_ABX, cpu_execute_dcp, 7);

    cpu_instruction_map_set(0x49, CPU_INSTRUCTION_EOR, CPU_ADDR_MODE_IMM, cpu_execute_eor, 2);
    cpu_instruction_map_set(0x45, CPU_INSTRUCTION_EOR, CPU_ADDR_MODE_ZPG, cpu_execute_eor, 3);
    cpu_instruction_map_set(0x55, CPU_INSTRUCTION_EOR, CPU_ADDR_MODE_ZPX, cpu_execute_eor, 4);
    cpu_instruction_map_set(0x4D, CPU_INSTRUCTION_EOR, CPU_ADDR_MODE_ABS, cpu_execute_eor, 4);
    cpu_instruction_map_set(0x5D, CPU_INSTRUCTION_EOR, CPU_ADDR_MODE_ABX, cpu_execute_eor, 4);
    cpu_instruction_map_set(0x59, CPU_INSTRUCTION_EOR, CPU_ADDR_MODE_ABY, cpu_execute_eor, 4);
    cpu_instruction_map_set(0x41, CPU_INSTRUCTION_EOR, CPU_ADDR_MODE_IDX, cpu_execute_eor, 6);
    cpu_instruction_map_set(0x51, CPU_INSTRUCTION_EOR, CPU_ADDR_MODE_IDY, cpu_execute_eor, 5);

    cpu_instruction_map_set(0x04, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_IMM, cpu_execute_ign, 3);
    cpu_instruction_map_set(0x0C, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ABS, cpu_execute_ign, 4);
    cpu_instruction_map_set(0x14, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ZPX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0x1C, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ABX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0x34, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ZPX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0x3C, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ABX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0x44, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_IMM, cpu_execute_ign, 3);
    cpu_instruction_map_set(0x54, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ZPX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0x5C, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ABX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0x64, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_IMM, cpu_execute_ign, 3);
    cpu_instruction_map_set(0x74, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ZPX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0x7C, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ABX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0xD4, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ZPX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0xDC, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ABX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0xF4, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ZPX, cpu_execute_ign, 4);
    cpu_instruction_map_set(0xFC, CPU_INSTRUCTION_IGN, CPU_ADDR_MODE_ABX, cpu_execute_ign, 4);

    cpu_instruction_map_set(0xE6, CPU_INSTRUCTION_INC, CPU_ADDR_MODE_ZPG, cpu_execute_inc, 5);
    cpu_instruction_map_set(0xF6, CPU_INSTRUCTION_INC, CPU_ADDR_MODE_ZPX, cpu_execute_inc, 6);
    cpu_instruction_map_set(0xEE, CPU_INSTRUCTION_INC, CPU_ADDR_MODE_ABS, cpu_execute_inc, 6);
    cpu_instruction_map_set(0xFE, CPU_INSTRUCTION_INC, CPU_ADDR_MODE_ABX, cpu_execute_inc, 7);

    cpu_instruction_map_set(0xE8, CPU_INSTRUCTION_INX, CPU_ADDR_MODE_IMP, cpu_execute_inx, 2);

    cpu_instruction_map_set(0xC8, CPU_INSTRUCTION_INY, CPU_ADDR_MODE_IMP, cpu_execute_iny, 2);

    cpu_instruction_map_set(0xE3, CPU_INSTRUCTION_ISC, CPU_ADDR_MODE_IDX, cpu_execute_isc, 8);
    cpu_instruction_map_set(0xE7, CPU_INSTRUCTION_ISC, CPU_ADDR_MODE_ZPG, cpu_execute_isc, 5);
    cpu_instruction_map_set(0xEF, CPU_INSTRUCTION_ISC, CPU_ADDR_MODE_ABS, cpu_execute_isc, 6);
    cpu_instruction_map_set(0xF3, CPU_INSTRUCTION_ISC, CPU_ADDR_MODE_IDY, cpu_execute_isc, 8);
    cpu_instruction_map_set(0xF7, CPU_INSTRUCTION_ISC, CPU_ADDR_MODE_ZPX, cpu_execute_isc, 6);
    cpu_instruction_map_set(0xFB, CPU_INSTRUCTION_ISC, CPU_ADDR_MODE_ABY, cpu_execute_isc, 7);
    cpu_instruction_map_set(0xFF, CPU_INSTRUCTION_ISC, CPU_ADDR_MODE_ABX, cpu_execute_isc, 7);

    cpu_instruction_map_set(0x4C, CPU_INSTRUCTION_JMP, CPU_ADDR_MODE_ABS, cpu_execute_jmp, 3);
    cpu_instruction_map_set(0x6C, CPU_INSTRUCTION_JMP, CPU_ADDR_MODE_IND, cpu_execute_jmp, 5);

    cpu_instruction_map_set(0x20, CPU_INSTRUCTION_JSR, CPU_ADDR_MODE_ABS, cpu_execute_jsr, 6);

    cpu_instruction_map_set(0xA3, CPU_INSTRUCTION_LAX, CPU_ADDR_MODE_IDX, cpu_execute_lax, 6);
    cpu_instruction_map_set(0xA7, CPU_INSTRUCTION_LAX, CPU_ADDR_MODE_ZPG, cpu_execute_lax, 3);
    cpu_instruction_map_set(0xAF, CPU_INSTRUCTION_LAX, CPU_ADDR_MODE_ABS, cpu_execute_lax, 4);
    cpu_instruction_map_set(0xB7, CPU_INSTRUCTION_LAX, CPU_ADDR_MODE_ZPY, cpu_execute_lax, 4);
    cpu_instruction_map_set(0xB3, CPU_INSTRUCTION_LAX, CPU_ADDR_MODE_IDY, cpu_execute_lax, 5);
    cpu_instruction_map_set(0xBF, CPU_INSTRUCTION_LAX, CPU_ADDR_MODE_ABY, cpu_execute_lax, 4);

    cpu_instruction_map_set(0xA9, CPU_INSTRUCTION_LDA, CPU_ADDR_MODE_IMM, cpu_execute_lda, 2);
    cpu_instruction_map_set(0xA5, CPU_INSTRUCTION_LDA, CPU_ADDR_MODE_ZPG, cpu_execute_lda, 3);
    cpu_instruction_map_set(0xB5, CPU_INSTRUCTION_LDA, CPU_ADDR_MODE_ZPX, cpu_execute_lda, 4);
    cpu_instruction_map_set(0xAD, CPU_INSTRUCTION_LDA, CPU_ADDR_MODE_ABS, cpu_execute_lda, 4);
    cpu_instruction_map_set(0xBD, CPU_INSTRUCTION_LDA, CPU_ADDR_MODE_ABX, cpu_execute_lda, 4);
    cpu_instruction_map_set(0xB9, CPU_INSTRUCTION_LDA, CPU_ADDR_MODE_ABY, cpu_execute_lda, 4);
    cpu_instruction_map_set(0xA1, CPU_INSTRUCTION_LDA, CPU_ADDR_MODE_IDX, cpu_execute_lda, 6);
    cpu_instruction_map_set(0xB1, CPU_INSTRUCTION_LDA, CPU_ADDR_MODE_IDY, cpu_execute_lda, 5);

    cpu_instruction_map_set(0xA2, CPU_INSTRUCTION_LDX, CPU_ADDR_MODE_IMM, cpu_execute_ldx, 2);
    cpu_instruction_map_set(0xA6, CPU_INSTRUCTION_LDX, CPU_ADDR_MODE_ZPG, cpu_execute_ldx, 3);
    cpu_instruction_map_set(0xB6, CPU_INSTRUCTION_LDX, CPU_ADDR_MODE_ZPY, cpu_execute_ldx, 4);
    cpu_instruction_map_set(0xAE, CPU_INSTRUCTION_LDX, CPU_ADDR_MODE_ABS, cpu_execute_ldx, 4);
    cpu_instruction_map_set(0xBE, CPU_INSTRUCTION_LDX, CPU_ADDR_MODE_ABY, cpu_execute_ldx, 4);

    cpu_instruction_map_set(0xA0, CPU_INSTRUCTION_LDY, CPU_ADDR_MODE_IMM, cpu_execute_ldy, 2);
    cpu_instruction_map_set(0xA4, CPU_INSTRUCTION_LDY, CPU_ADDR_MODE_ZPG, cpu_execute_ldy, 3);
    cpu_instruction_map_set(0xB4, CPU_INSTRUCTION_LDY, CPU_ADDR_MODE_ZPX, cpu_execute_ldy, 4);
    cpu_instruction_map_set(0xAC, CPU_INSTRUCTION_LDY, CPU_ADDR_MODE_ABS, cpu_execute_ldy, 4);
    cpu_instruction_map_set(0xBC, CPU_INSTRUCTION_LDY, CPU_ADDR_MODE_ABX, cpu_execute_ldy, 4);

    cpu_instruction_map_set(0x4A, CPU_INSTRUCTION_LSR, CPU_ADDR_MODE_ACC, cpu_execute_lsr, 2);
    cpu_instruction_map_set(0x46, CPU_INSTRUCTION_LSR, CPU_ADDR_MODE_ZPG, cpu_execute_lsr, 5);
    cpu_instruction_map_set(0x56, CPU_INSTRUCTION_LSR, CPU_ADDR_MODE_ZPX, cpu_execute_lsr, 6);
    cpu_instruction_map_set(0x4E, CPU_INSTRUCTION_LSR, CPU_ADDR_MODE_ABS, cpu_execute_lsr, 6);
    cpu_instruction_map_set(0x5E, CPU_INSTRUCTION_LSR, CPU_ADDR_MODE_ABX, cpu_execute_lsr, 7);

    cpu_instruction_map_set(0xEA, CPU_INSTRUCTION_NOP, CPU_ADDR_MODE_IMP, cpu_execute_nop, 2);
    cpu_instruction_map_set(0x1A, CPU_INSTRUCTION_NOP, CPU_ADDR_MODE_IMP, cpu_execute_nop, 2);
    cpu_instruction_map_set(0x3A, CPU_INSTRUCTION_NOP, CPU_ADDR_MODE_IMP, cpu_execute_nop, 2);
    cpu_instruction_map_set(0x5A, CPU_INSTRUCTION_NOP, CPU_ADDR_MODE_IMP, cpu_execute_nop, 2);
    cpu_instruction_map_set(0x7A, CPU_INSTRUCTION_NOP, CPU_ADDR_MODE_IMP, cpu_execute_nop, 2);
    cpu_instruction_map_set(0xDA, CPU_INSTRUCTION_NOP, CPU_ADDR_MODE_IMP, cpu_execute_nop, 2);
    cpu_instruction_map_set(0xFA, CPU_INSTRUCTION_NOP, CPU_ADDR_MODE_IMP, cpu_execute_nop, 2);

    cpu_instruction_map_set(0x09, CPU_INSTRUCTION_ORA, CPU_ADDR_MODE_IMM, cpu_execute_ora, 2);
    cpu_instruction_map_set(0x05, CPU_INSTRUCTION_ORA, CPU_ADDR_MODE_ZPG, cpu_execute_ora, 3);
    cpu_instruction_map_set(0x15, CPU_INSTRUCTION_ORA, CPU_ADDR_MODE_ZPX, cpu_execute_ora, 4);
    cpu_instruction_map_set(0x0D, CPU_INSTRUCTION_ORA, CPU_ADDR_MODE_ABS, cpu_execute_ora, 4);
    cpu_instruction_map_set(0x1D, CPU_INSTRUCTION_ORA, CPU_ADDR_MODE_ABX, cpu_execute_ora, 4);
    cpu_instruction_map_set(0x19, CPU_INSTRUCTION_ORA, CPU_ADDR_MODE_ABY, cpu_execute_ora, 4);
    cpu_instruction_map_set(0x01, CPU_INSTRUCTION_ORA, CPU_ADDR_MODE_IDX, cpu_execute_ora, 6);
    cpu_instruction_map_set(0x11, CPU_INSTRUCTION_ORA, CPU_ADDR_MODE_IDY, cpu_execute_ora, 5);

    cpu_instruction_map_set(0x48, CPU_INSTRUCTION_PHA, CPU_ADDR_MODE_IMP, cpu_execute_pha, 3);

    cpu_instruction_map_set(0x08, CPU_INSTRUCTION_PHP, CPU_ADDR_MODE_IMP, cpu_execute_php, 3);

    cpu_instruction_map_set(0x68, CPU_INSTRUCTION_PLA, CPU_ADDR_MODE_IMP, cpu_execute_pla, 4);

    cpu_instruction_map_set(0x28, CPU_INSTRUCTION_PLP, CPU_ADDR_MODE_IMP, cpu_execute_plp, 4);

    cpu_instruction_map_set(0x23, CPU_INSTRUCTION_RLA, CPU_ADDR_MODE_IDX, cpu_execute_rla, 8);
    cpu_instruction_map_set(0x27, CPU_INSTRUCTION_RLA, CPU_ADDR_MODE_ZPG, cpu_execute_rla, 5);
    cpu_instruction_map_set(0x2F, CPU_INSTRUCTION_RLA, CPU_ADDR_MODE_ABS, cpu_execute_rla, 6);
    cpu_instruction_map_set(0x33, CPU_INSTRUCTION_RLA, CPU_ADDR_MODE_IDY, cpu_execute_rla, 8);
    cpu_instruction_map_set(0x37, CPU_INSTRUCTION_RLA, CPU_ADDR_MODE_ZPX, cpu_execute_rla, 6);
    cpu_instruction_map_set(0x3B, CPU_INSTRUCTION_RLA, CPU_ADDR_MODE_ABY, cpu_execute_rla, 7);
    cpu_instruction_map_set(0x3F, CPU_INSTRUCTION_RLA, CPU_ADDR_MODE_ABX, cpu_execute_rla, 7);

    cpu_instruction_map_set(0x2A, CPU_INSTRUCTION_ROL, CPU_ADDR_MODE_ACC, cpu_execute_rol, 2);
    cpu_instruction_map_set(0x26, CPU_INSTRUCTION_ROL, CPU_ADDR_MODE_ZPG, cpu_execute_rol, 5);
    cpu_instruction_map_set(0x36, CPU_INSTRUCTION_ROL, CPU_ADDR_MODE_ZPX, cpu_execute_rol, 6);
    cpu_instruction_map_set(0x2E, CPU_INSTRUCTION_ROL, CPU_ADDR_MODE_ABS, cpu_execute_rol, 6);
    cpu_instruction_map_set(0x3E, CPU_INSTRUCTION_ROL, CPU_ADDR_MODE_ABX, cpu_execute_rol, 7);

    cpu_instruction_map_set(0x6A, CPU_INSTRUCTION_ROR, CPU_ADDR_MODE_ACC, cpu_execute_ror, 2);
    cpu_instruction_map_set(0x66, CPU_INSTRUCTION_ROR, CPU_ADDR_MODE_ZPG, cpu_execute_ror, 5);
    cpu_instruction_map_set(0x76, CPU_INSTRUCTION_ROR, CPU_ADDR_MODE_ZPX, cpu_execute_ror, 6);
    cpu_instruction_map_set(0x6E, CPU_INSTRUCTION_ROR, CPU_ADDR_MODE_ABS, cpu_execute_ror, 6);
    cpu_instruction_map_set(0x7E, CPU_INSTRUCTION_ROR, CPU_ADDR_MODE_ABX, cpu_execute_ror, 7);

    cpu_instruction_map_set(0x63, CPU_INSTRUCTION_RRA, CPU_ADDR_MODE_IDX, cpu_execute_rra, 8);
    cpu_instruction_map_set(0x67, CPU_INSTRUCTION_RRA, CPU_ADDR_MODE_ZPG, cpu_execute_rra, 5);
    cpu_instruction_map_set(0x6F, CPU_INSTRUCTION_RRA, CPU_ADDR_MODE_ABS, cpu_execute_rra, 6);
    cpu_instruction_map_set(0x73, CPU_INSTRUCTION_RRA, CPU_ADDR_MODE_IDY, cpu_execute_rra, 8);
    cpu_instruction_map_set(0x77, CPU_INSTRUCTION_RRA, CPU_ADDR_MODE_ZPX, cpu_execute_rra, 6);
    cpu_instruction_map_set(0x7B, CPU_INSTRUCTION_RRA, CPU_ADDR_MODE_ABY, cpu_execute_rra, 7);
    cpu_instruction_map_set(0x7F, CPU_INSTRUCTION_RRA, CPU_ADDR_MODE_ABX, cpu_execute_rra, 7);

    cpu_instruction_map_set(0x40, CPU_INSTRUCTION_RTI, CPU_ADDR_MODE_IMP, cpu_execute_rti, 6);

    cpu_instruction_map_set(0x60, CPU_INSTRUCTION_RTS, CPU_ADDR_MODE_IMP, cpu_execute_rts, 6);

    cpu_instruction_map_set(0x83, CPU_INSTRUCTION_SAX, CPU_ADDR_MODE_IDX, cpu_execute_sax, 6);
    cpu_instruction_map_set(0x87, CPU_INSTRUCTION_SAX, CPU_ADDR_MODE_ZPG, cpu_execute_sax, 3);
    cpu_instruction_map_set(0x8F, CPU_INSTRUCTION_SAX, CPU_ADDR_MODE_ABS, cpu_execute_sax, 4);
    cpu_instruction_map_set(0x97, CPU_INSTRUCTION_SAX, CPU_ADDR_MODE_ZPY, cpu_execute_sax, 4);

    cpu_instruction_map_set(0xE9, CPU_INSTRUCTION_SBC, CPU_ADDR_MODE_IMM, cpu_execute_sbc, 2);
    cpu_instruction_map_set(0xE5, CPU_INSTRUCTION_SBC, CPU_ADDR_MODE_ZPG, cpu_execute_sbc, 3);
    cpu_instruction_map_set(0xF5, CPU_INSTRUCTION_SBC, CPU_ADDR_MODE_ZPX, cpu_execute_sbc, 4);
    cpu_instruction_map_set(0xEB, CPU_INSTRUCTION_SBC, CPU_ADDR_MODE_IMM, cpu_execute_sbc, 2);
    cpu_instruction_map_set(0xED, CPU_INSTRUCTION_SBC, CPU_ADDR_MODE_ABS, cpu_execute_sbc, 4);
    cpu_instruction_map_set(0xFD, CPU_INSTRUCTION_SBC, CPU_ADDR_MODE_ABX, cpu_execute_sbc, 4);
    cpu_instruction_map_set(0xF9, CPU_INSTRUCTION_SBC, CPU_ADDR_MODE_ABY, cpu_execute_sbc, 4);
    cpu_instruction_map_set(0xE1, CPU_INSTRUCTION_SBC, CPU_ADDR_MODE_IDX, cpu_execute_sbc, 6);
    cpu_instruction_map_set(0xF1, CPU_INSTRUCTION_SBC, CPU_ADDR_MODE_IDY, cpu_execute_sbc, 5);

    cpu_instruction_map_set(0x38, CPU_INSTRUCTION_SEC, CPU_ADDR_MODE_IMP, cpu_execute_sec, 2);

    cpu_instruction_map_set(0xF8, CPU_INSTRUCTION_SED, CPU_ADDR_MODE_IMP, cpu_execute_sed, 2);

    cpu_instruction_map_set(0x78, CPU_INSTRUCTION_SEI, CPU_ADDR_MODE_IMP, cpu_execute_sei, 2);

    cpu_instruction_map_set(0x80, CPU_INSTRUCTION_SKB, CPU_ADDR_MODE_IMM, cpu_execute_skb, 2);
    cpu_instruction_map_set(0x82, CPU_INSTRUCTION_SKB, CPU_ADDR_MODE_IMM, cpu_execute_skb, 2);
    cpu_instruction_map_set(0x89, CPU_INSTRUCTION_SKB, CPU_ADDR_MODE_IMM, cpu_execute_skb, 2);
    cpu_instruction_map_set(0xC2, CPU_INSTRUCTION_SKB, CPU_ADDR_MODE_IMM, cpu_execute_skb, 2);
    cpu_instruction_map_set(0xE2, CPU_INSTRUCTION_SKB, CPU_ADDR_MODE_IMM, cpu_execute_skb, 2);

    cpu_instruction_map_set(0x03, CPU_INSTRUCTION_SLO, CPU_ADDR_MODE_IDX, cpu_execute_slo, 8);
    cpu_instruction_map_set(0x07, CPU_INSTRUCTION_SLO, CPU_ADDR_MODE_ZPG, cpu_execute_slo, 5);
    cpu_instruction_map_set(0x0F, CPU_INSTRUCTION_SLO, CPU_ADDR_MODE_ABS, cpu_execute_slo, 6);
    cpu_instruction_map_set(0x13, CPU_INSTRUCTION_SLO, CPU_ADDR_MODE_IDY, cpu_execute_slo, 8);
    cpu_instruction_map_set(0x17, CPU_INSTRUCTION_SLO, CPU_ADDR_MODE_ZPX, cpu_execute_slo, 6);
    cpu_instruction_map_set(0x1B, CPU_INSTRUCTION_SLO, CPU_ADDR_MODE_ABY, cpu_execute_slo, 7);
    cpu_instruction_map_set(0x1F, CPU_INSTRUCTION_SLO, CPU_ADDR_MODE_ABX, cpu_execute_slo, 7);

    cpu_instruction_map_set(0x43, CPU_INSTRUCTION_SRE, CPU_ADDR_MODE_IDX, cpu_execute_sre, 8);
    cpu_instruction_map_set(0x47, CPU_INSTRUCTION_SRE, CPU_ADDR_MODE_ZPG, cpu_execute_sre, 5);
    cpu_instruction_map_set(0x4F, CPU_INSTRUCTION_SRE, CPU_ADDR_MODE_ABS, cpu_execute_sre, 6);
    cpu_instruction_map_set(0x53, CPU_INSTRUCTION_SRE, CPU_ADDR_MODE_IDY, cpu_execute_sre, 8);
    cpu_instruction_map_set(0x57, CPU_INSTRUCTION_SRE, CPU_ADDR_MODE_ZPX, cpu_execute_sre, 6);
    cpu_instruction_map_set(0x5B, CPU_INSTRUCTION_SRE, CPU_ADDR_MODE_ABY, cpu_execute_sre, 7);
    cpu_instruction_map_set(0x5F, CPU_INSTRUCTION_SRE, CPU_ADDR_MODE_ABX, cpu_execute_sre, 7);

    cpu_instruction_map_set(0x85, CPU_INSTRUCTION_STA, CPU_ADDR_MODE_ZPG, cpu_execute_sta, 3);
    cpu_instruction_map_set(0x95, CPU_INSTRUCTION_STA, CPU_ADDR_MODE_ZPX, cpu_execute_sta, 4);
    cpu_instruction_map_set(0x8D, CPU_INSTRUCTION_STA, CPU_ADDR_MODE_ABS, cpu_execute_sta, 4);
    cpu_instruction_map_set(0x9D, CPU_INSTRUCTION_STA, CPU_ADDR_MODE_ABX, cpu_execute_sta, 5);
    cpu_instruction_map_set(0x99, CPU_INSTRUCTION_STA, CPU_ADDR_MODE_ABY, cpu_execute_sta, 5);
    cpu_instruction_map_set(0x81, CPU_INSTRUCTION_STA, CPU_ADDR_MODE_IDX, cpu_execute_sta, 6);
    cpu_instruction_map_set(0x91, CPU_INSTRUCTION_STA, CPU_ADDR_MODE_IDY, cpu_execute_sta, 6);

    cpu_instruction_map_set(0x86, CPU_INSTRUCTION_STX, CPU_ADDR_MODE_ZPG, cpu_execute_stx, 3);
    cpu_instruction_map_set(0x96, CPU_INSTRUCTION_STX, CPU_ADDR_MODE_ZPY, cpu_execute_stx, 4);
    cpu_instruction_map_set(0x8E, CPU_INSTRUCTION_STX, CPU_ADDR_MODE_ABS, cpu_execute_stx, 4);

    cpu_instruction_map_set(0x84, CPU_INSTRUCTION_STY, CPU_ADDR_MODE_ZPG, cpu_execute_sty, 3);
    cpu_instruction_map_set(0x94, CPU_INSTRUCTION_STY, CPU_ADDR_MODE_ZPX, cpu_execute_sty, 4);
    cpu_instruction_map_set(0x8C, CPU_INSTRUCTION_STY, CPU_ADDR_MODE_ABS, cpu_execute_sty, 4);

    cpu_instruction_map_set(0xAA, CPU_INSTRUCTION_TAX, CPU_ADDR_MODE_IMP, cpu_execute_tax, 2);

    cpu_instruction_map_set(0xA8, CPU_INSTRUCTION_TAY, CPU_ADDR_MODE_IMP, cpu_execute_tay, 2);

    cpu_instruction_map_set(0xBA, CPU_INSTRUCTION_TSX, CPU_ADDR_MODE_IMP, cpu_execute_tsx, 2);

    cpu_instruction_map_set(0x8A, CPU_INSTRUCTION_TXA, CPU_ADDR_MODE_IMP, cpu_execute_txa, 2);

    cpu_instruction_map_set(0x9A, CPU_INSTRUCTION_TXS, CPU_ADDR_MODE_IMP, cpu_execute_txs, 2);

    cpu_instruction_map_set(0x98, CPU_INSTRUCTION_TYA, CPU_ADDR_MODE_IMP, cpu_execute_tya, 2);
}

void
cpu_free() {
}

void
cpu_power() {
    cpu_reset();

    //TODO: disable NMI, IRQ
}

void
cpu_reset() {
    memset(&cpu, 0, sizeof(cpu));

    cpu.cycles_left = CPU_CYCLES_PER_FRAME;

    cpu_flag_set(CPU_FLAG_UNUSED, true);

    cpu_interrupt(CPU_INTERRUPT_RESET);

    //at this point, a cartridge should be loaded and will load from a memory mapped region in the cartrige
    if (cartridge_is_nes_test()) {
        cpu_test_load();
    }
}

void
cpu_pause() {
    cpu.paused = !cpu.paused;
}

void
cpu_set_nmi() {
    cpu.nmi = true;
}

void
cpu_run_frame() {
    cpu_instruction_map_t *map;
    uint8_t opcode;

    cpu.cycles_left += CPU_CYCLES_PER_FRAME;

    while (cpu.cycles_left > 0) {
        if (cpu.nmi) {
            cpu_interrupt(CPU_INTERRUPT_NMI);
        }

        if (cartridge_is_nes_test()) {
            if (CPU_CYCLES_PER_FRAME - cpu.cycles_left >= 26554) {
                printf("CPU test passed!\n");
                fflush(stdout);
                fgetc(stdin);
                exit(1);
            }
        }

        if (cartridge_is_nes_test()) {
            printf("%04X  ", cpu.PC);
        }

        opcode = cpu_read(cpu.PC++);
        map = &instruction_map[opcode];

        if (cartridge_is_nes_test()) {
            printf("%02X (%s-%s): ", opcode, cpu_instruction_str(map->instruction), cpu_address_mode_str(map->mode));
            printf("A: %02X  X: %02X  Y: %02X  SP: %02X  Cycles: %d  ", cpu.A, cpu.X, cpu.Y, cpu.SP, CPU_CYCLES_PER_FRAME - cpu.cycles_left);
            printf("Flags: %02X ", cpu.flags);
            printf("C[%d] ", cpu_flag_is_set(CPU_FLAG_CARRY) ? 1 : 0);
            printf("Z[%d] ", cpu_flag_is_set(CPU_FLAG_ZERO) ? 1 : 0);
            printf("I[%d] ", cpu_flag_is_set(CPU_FLAG_INTERRUPT_DISABLE) ? 1 : 0);
            printf("B[%d] ", cpu_flag_is_set(CPU_FLAG_BREAK_COMMAND) ? 1 : 0);
            printf("U[%d] ", cpu_flag_is_set(CPU_FLAG_UNUSED) ? 1 : 0);
            printf("V[%d] ", cpu_flag_is_set(CPU_FLAG_OVERFLOW) ? 1 : 0);
            printf("N[%d]\n", cpu_flag_is_set(CPU_FLAG_NEGATIVE) ? 1 : 0);

            if (!cpu_test_check(cpu.PC - 1, opcode, cpu.A, cpu.X, cpu.Y, cpu.SP, cpu.flags, CPU_CYCLES_PER_FRAME - cpu.cycles_left)) {
                fflush(stdout);
                fgetc(stdin);
                exit(1);
            }
        }

        if (map->instruction == CPU_INSTRUCTION_INV) {
            log_err(MODULE, "Unhandled opcode %02X", opcode);
            fflush(stdout);
            fgetc(stdin);
            exit(1);
            return;
        }

        map->func(map->mode, map->cycles);
    }
}