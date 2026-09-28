
#pragma once
#include <stddef.h>
void sram_budget_begin(size_t capacity, size_t reserved);
void sram_budget_end();
size_t sram_budget_peak();
unsigned sram_budget_failures();
