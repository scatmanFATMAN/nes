#pragma once

#include <stdbool.h>
#include "cpu.h"

void cpu_test_init();
void cpu_test_free();

bool cpu_test_load();
bool cpu_test_check(uint16_t PC, uint8_t opcode, uint8_t A, uint8_t X, uint8_t Y, uint8_t SP, uint8_t flags, int cycles);