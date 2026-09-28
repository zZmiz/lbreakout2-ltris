
#pragma once
#include <stdint.h>

constexpr uint32_t rp_launch_token(int game) {return 0x4c474d30u+game+1;}
constexpr int rp_decode_launch(uint32_t token,uint32_t check,bool resume) {
    return resume && token>=rp_launch_token(-1) && token<=rp_launch_token(1) &&
           check==~token ? int(token-rp_launch_token(-1))-1 : -2;
}
