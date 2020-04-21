#pragma once

#include <stdbool.h>
#include <stdint.h>

void cpu_init();
void cpu_free();

void cpu_power();
void cpu_reset();
void cpu_pause();

void cpu_begin_frame();
bool cpu_has_more_cycles();

void cpu_execute();