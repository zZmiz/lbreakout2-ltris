
#include "rp_platform.h"
#include "launch_request.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include <cstdio>

#ifdef RP_LGAMES_CONTAINER
#include "RP2350.h"

__attribute__((naked,noreturn)) static void jump_vectors(uint32_t base,uint32_t sp,uint32_t pc)
{
    __asm volatile(
        "ldr r3, =0xe000ed08\n"
        "str r0, [r3]\n"
        "movs r3, #0\n"
        "msr msplim, r3\n"
        "msr control, r3\n"
        "isb\n"
        "msr msp, r1\n"
        "dsb\n"
        "isb\n"
        "cpsie i\n"
        "bx r2\n");
}
#endif

extern "C" void rp_platform_launch_slot(int game)
{
#ifdef RP_LGAMES_CONTAINER
    watchdog_hw->scratch[2]=rp_launch_token(game);
    watchdog_hw->scratch[3]=~rp_launch_token(game);

    watchdog_hw->scratch[7]=0;

    watchdog_enable(10,false);
    while(true) tight_loop_contents();
#else
    (void)game;
    rp_exit(0);
#endif
}

extern "C" void rp_system_dispatch_early(void)
{
#ifdef RP_LGAMES_CONTAINER
    int game=rp_decode_launch(watchdog_hw->scratch[2],watchdog_hw->scratch[3],
                              watchdog_enable_caused_reboot());
    if(game==-2) return;
    watchdog_hw->scratch[2]=watchdog_hw->scratch[3]=0;
    if(game==-1) return;
    uint32_t base=0x10180000u+uint32_t(game)*0x100000u;
    const volatile uint32_t *vt=reinterpret_cast<const volatile uint32_t *>(base);
    uint32_t sp=vt[0],pc=vt[1];
    stdio_init_all();
    if(sp<0x20000000u || sp>0x20082000u || (sp&7u) ||
       pc<base || pc>=base+0x100000u || !(pc&1u)) {
        std::printf("[lgames] resume rejected: invalid slot %d\n",game);
        return;
    }
    std::printf("[lgames] XIP ready; early dispatch slot %d at %08lx\n",game,
                static_cast<unsigned long>(base));
    stdio_flush();

    __disable_irq();
    SysTick->CTRL=0; SysTick->VAL=0;
    for(unsigned i=0;i<sizeof(NVIC->ICER)/sizeof(NVIC->ICER[0]);++i) {
        NVIC->ICER[i]=0xffffffffu; NVIC->ICPR[i]=0xffffffffu;
    }
    jump_vectors(base,sp,pc);
#endif
}
