#if PICO_RP2350
#include "pico/stdlib.h"
#include "stdio.h"
#include "hardware/regs/qmi.h"
#include "hardware/regs/xip.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

/// @brief Setup PSRAM
/// @param psramCS Chip select pin for the PSRAM
/// @return 0: no psram detected, > 0 psram size, -1 on failure
/// From: https://github.com/adafruit/circuitpython/blob/main/ports/raspberrypi/supervisor/port.c
///       https://github.com/AndrewCapon/PicoPlusPsram for calculating correct clock and timing values
int32_t __no_inline_not_in_flash_func(SetupPsram)(int psramCS)
{

    gpio_set_function(psramCS, GPIO_FUNC_XIP_CS1);
    int32_t _psram_size = 0;
    uint32_t intr_stash = save_and_disable_interrupts();
    // Try and read the PSRAM ID via direct_csr.
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB |
                         QMI_DIRECT_CSR_EN_BITS;
    // Need to poll for the cooldown on the last XIP transfer to expire
    // (via direct-mode BUSY flag) before it is safe to perform the first
    // direct-mode operation
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
    {
    }

    // Exit out of QMI in case we've inited already
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    // Transmit as quad.
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS |
                        QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB |
                        0xf5;
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
    {
    }
    (void)qmi_hw->direct_rx;
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);

    // Read the id
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    uint8_t kgd = 0;
    uint8_t eid = 0;
    for (size_t i = 0; i < 7; i++)
    {
        if (i == 0)
        {
            qmi_hw->direct_tx = 0x9f;
        }
        else
        {
            qmi_hw->direct_tx = 0xff;
        }
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0)
        {
        }
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
        {
        }
        if (i == 5)
        {
            kgd = qmi_hw->direct_rx;
        }
        else if (i == 6)
        {
            eid = qmi_hw->direct_rx;
        }
        else
        {
            (void)qmi_hw->direct_rx;
        }
    }
    // Disable direct csr.
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    if (kgd != 0x5D)
    {
        restore_interrupts(intr_stash);
        printf("PSRAM not detected, kgd = 0x%02x\n", kgd);
        return 0;
    }
    // never_reset_pin_number(CIRCUITPY_PSRAM_CHIP_SELECT->number);

    // Enable quad mode.
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB |
                         QMI_DIRECT_CSR_EN_BITS;
    // Need to poll for the cooldown on the last XIP transfer to expire
    // (via direct-mode BUSY flag) before it is safe to perform the first
    // direct-mode operation
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
    {
    }

    // RESETEN, RESET and quad enable
    for (uint8_t i = 0; i < 3; i++)
    {
        qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
        if (i == 0)
        {
            qmi_hw->direct_tx = 0x66;
        }
        else if (i == 1)
        {
            qmi_hw->direct_tx = 0x99;
        }
        else
        {
            qmi_hw->direct_tx = 0x35;
        }
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
        {
        }
        qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
        for (size_t j = 0; j < 20; j++)
        {
            asm("nop");
        }
        (void)qmi_hw->direct_rx;
    }
    // Disable direct csr.
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    // qmi_hw->m[1].timing =
    //     QMI_M0_TIMING_PAGEBREAK_VALUE_1024 << QMI_M0_TIMING_PAGEBREAK_LSB | // Break between pages.
    //     3 << QMI_M0_TIMING_SELECT_HOLD_LSB |                                // Delay releasing CS for 3 extra system cycles.
    //     1 << QMI_M0_TIMING_COOLDOWN_LSB |
    //     1 << QMI_M0_TIMING_RXDELAY_LSB |
    //     31 << QMI_M0_TIMING_MAX_SELECT_LSB |  //(16) In units of 64 system clock cycles. PSRAM says 8us max. 8 / 0.00752 / 64 = 16.62
    //     12 << QMI_M0_TIMING_MIN_DESELECT_LSB | //(7) In units of system clock cycles. PSRAM says 50ns.50 / 7.52 = 6.64
    //     2 << QMI_M0_TIMING_CLKDIV_LSB;
    // https://github.com/AndrewCapon/PicoPlusPsram
    // PSRAM (APS6404) timing. The default formula runs the PSRAM near 126 MHz
    // (divisor 2 at 252 MHz clk_sys), which on this board is marginal: depending
    // on RXDELAY it drops a bit or stalls the bus. Run it SLOWER for signal-
    // integrity margin -- PSRAM_CLKDIV defaults to 4 (63 MHz), plenty fast for
    // the emulator. Override with -DPSRAM_CLKDIV / -DPSRAM_RXDELAY. The timing
    // register MUST be programmed here, together with rfmt/rcmd/wfmt/wcmd and
    // inside the interrupts-disabled init sequence -- programming it later (after
    // restore_interrupts) hangs the QMI on the first PSRAM access.
#ifndef PSRAM_CLKDIV
#define PSRAM_CLKDIV 2
#endif
#ifndef PSRAM_RXDELAY
#define PSRAM_RXDELAY 3
#endif
    // SELECT_HOLD keeps CS asserted N extra system cycles after the transfer.
    // rxdelay >= 4 hangs the QMI on this PSRAM, but rxdelay=3 at 126 MHz drops
    // the LAST read bit (0x12345678 -> 0x12345670). SELECT_HOLD gives the final
    // bit more settle time without raising rxdelay -> fixes the dropped bit.
    // (The pico-infonesPlus #191 PSRAM fix used SELECT_HOLD for this reason.)
#ifndef PSRAM_SELECT_HOLD
#define PSRAM_SELECT_HOLD 3
#endif
    const int clock_hz = clock_get_hz(clk_sys);
    const int divisor = PSRAM_CLKDIV;
    const int rxdelay = PSRAM_RXDELAY;
    const int clock_period_fs = 1000000000000000ll / clock_hz;
    const int max_select = (125 * 1000000) / clock_period_fs;       // 8000ns / 64
    const int min_deselect =
        (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (divisor + 1) / 2;

    qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB |
                          PSRAM_SELECT_HOLD << QMI_M1_TIMING_SELECT_HOLD_LSB |
                          QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
                          max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
                          min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
                          rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
                          divisor << QMI_M1_TIMING_CLKDIV_LSB;

    qmi_hw->m[1].rfmt = (QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |
                         QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB |
                         QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |
                         QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB |
                         QMI_M0_RFMT_DUMMY_LEN_VALUE_24 << QMI_M0_RFMT_DUMMY_LEN_LSB |
                         QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB |
                         QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB |
                         QMI_M0_RFMT_SUFFIX_LEN_VALUE_NONE << QMI_M0_RFMT_SUFFIX_LEN_LSB);
    qmi_hw->m[1].rcmd = 0xeb << QMI_M0_RCMD_PREFIX_LSB |
                        0 << QMI_M0_RCMD_SUFFIX_LSB;
    qmi_hw->m[1].wfmt = (QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
                         QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
                         QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
                         QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
                         QMI_M0_WFMT_DUMMY_LEN_VALUE_NONE << QMI_M0_WFMT_DUMMY_LEN_LSB |
                         QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
                         QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB |
                         QMI_M0_WFMT_SUFFIX_LEN_VALUE_NONE << QMI_M0_WFMT_SUFFIX_LEN_LSB);
    qmi_hw->m[1].wcmd = 0x38 << QMI_M0_WCMD_PREFIX_LSB |
                        0 << QMI_M0_WCMD_SUFFIX_LSB;

    restore_interrupts(intr_stash);
    printf("[PSRAM] detect+regs done: kgd=0x%02x eid=0x%02x clkdiv=%d rxdelay=%d\n",
           kgd, eid, divisor, rxdelay);

    _psram_size = 1024 * 1024; // 1 MiB
    uint8_t size_id = eid >> 5;
    if (eid == 0x26 || size_id == 2)
    {
        _psram_size *= 8;
    }
    else if (size_id == 0)
    {
        _psram_size *= 2;
    }
    else if (size_id == 1)
    {
        _psram_size *= 4;
    }

    // Mark that we can write to PSRAM.
    xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;

    // Settle delay before the first PSRAM access (post reset/QPI-enable the
    // APS6404 needs recovery; without it the first access races and hangs).
    busy_wait_us(300);

    // One-flash timing MAP: rxdelay is fixed at 3 (>=4 hangs the QMI on this
    // PSRAM) and we already settled, so this loop is hang-safe. Sweep clkdiv
    // (PSRAM clock) x SELECT_HOLD (extra CS-hold cycles to settle the last read
    // bit) and print the readback for each, keeping the first that is clean.
    // 0x12345678 -> 0x12345670 means the last bit needs more SELECT_HOLD; wild
    // garbage means clkdiv/rxdelay mismatch.
    volatile uint32_t *psram_nocache = (volatile uint32_t *)0x15000000;
    const int fixed_rx = 3;
    int ok_div = 0, ok_sh = -1;
    for (int dv = 2; dv <= 4 && ok_div == 0; dv++)
    {
        const int mdsel =
            (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (dv + 1) / 2;
        for (int sh = 0; sh <= 7 && ok_div == 0; sh++)
        {
            qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB |
                                  sh << QMI_M1_TIMING_SELECT_HOLD_LSB |
                                  QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
                                  max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
                                  mdsel << QMI_M1_TIMING_MIN_DESELECT_LSB |
                                  fixed_rx << QMI_M1_TIMING_RXDELAY_LSB |
                                  dv << QMI_M1_TIMING_CLKDIV_LSB;
            __compiler_memory_barrier();
            busy_wait_us(5);
            psram_nocache[0] = 0x12345678u;
            psram_nocache[1] = 0xA5A5A5A5u;
            uint32_t rb0 = psram_nocache[0];
            uint32_t rb1 = psram_nocache[1];
            printf("[PSRAM] clkdiv=%d rx=%d sh=%d -> rb0=0x%08x rb1=0x%08x\n",
                   dv, fixed_rx, sh, rb0, rb1);
            if (rb0 == 0x12345678u && rb1 == 0xA5A5A5A5u)
            {
                ok_div = dv;
                ok_sh = sh;
            }
        }
    }
    if (ok_div == 0)
    {
        _psram_size = 0;
        printf("PSRAM: no clean timing found in sweep\n");
        return 0;
    }
    // Re-apply the winning timing.
    {
        const int mdsel =
            (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (ok_div + 1) / 2;
        qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB |
                              ok_sh << QMI_M1_TIMING_SELECT_HOLD_LSB |
                              QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
                              max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
                              mdsel << QMI_M1_TIMING_MIN_DESELECT_LSB |
                              fixed_rx << QMI_M1_TIMING_RXDELAY_LSB |
                              ok_div << QMI_M1_TIMING_CLKDIV_LSB;
        __compiler_memory_barrier();
    }
    printf("PSRAM ok: clkdiv=%d rxdelay=%d select_hold=%d\n", ok_div, fixed_rx, ok_sh);
    return _psram_size;
}
#endif // PICO_RP2350