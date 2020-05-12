#pragma once

#include <stdbool.h>
#include <stdint.h>

void cpu_init();
void cpu_free();

void cpu_power();
void cpu_reset();
void cpu_pause();

void cpu_set_nmi();
void cpu_set_irq();

void cpu_run_frame();