
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint64_t get_absolute_time(void);
uint32_t to_ms_since_boot(uint64_t time);
uint32_t time_us_32(void);
void sleep_ms(uint32_t ms);
#ifdef __cplusplus
}
#endif
