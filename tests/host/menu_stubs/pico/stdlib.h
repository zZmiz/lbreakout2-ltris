
#pragma once
#include <cstdint>
uint32_t time_us_32();
uint64_t get_absolute_time();
uint32_t to_ms_since_boot(uint64_t time);
void sleep_ms(uint32_t ms);
void stdio_flush();
void tight_loop_contents();
