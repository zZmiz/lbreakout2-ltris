#include "pcm5102_audio.h"
#include "pcm5102_audio.pio.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <stdio.h>

#define PCM5102_PIO pio1
#define PCM5102_BCLK_PIN 20
#define PCM5102_LRCLK_PIN 21
#define PCM5102_DATA_PIN 22
#define PCM5102_RING_SIZE 1024u
#define PCM5102_RING_MASK (PCM5102_RING_SIZE - 1u)
#define PCM5102_DMA_BLOCK 128u

static uint32_t audio_ring[PCM5102_RING_SIZE];
static volatile unsigned write_index;
static volatile unsigned read_index;
static int dma_channel = -1;
static int state_machine = -1;
static int dma_irq = -1;
static volatile bool dma_running;
static volatile uint32_t starvation_count;

static unsigned ring_used(void)
{
    return (write_index - read_index) & PCM5102_RING_MASK;
}

static void start_dma_if_ready(void)
{
    if (dma_channel < 0 || dma_running || ring_used() < PCM5102_DMA_BLOCK)
        return;
    dma_running = true;
    __dmb();
    dma_channel_set_read_addr((uint)dma_channel, &audio_ring[read_index], false);
    dma_channel_set_trans_count((uint)dma_channel, PCM5102_DMA_BLOCK, true);
}

static void __isr pcm5102_dma_handler(void)
{
    uint32_t mask = 1u << dma_channel;
    if (dma_irq == 0) {
        if (!(dma_hw->ints0 & mask)) return;
        dma_hw->ints0 = mask;
    } else {
        if (!(dma_hw->ints1 & mask)) return;
        dma_hw->ints1 = mask;
    }
    read_index = (read_index + PCM5102_DMA_BLOCK) & PCM5102_RING_MASK;
    __dmb();
    if (ring_used() >= PCM5102_DMA_BLOCK) {
        dma_channel_set_read_addr((uint)dma_channel, &audio_ring[read_index], false);
        dma_channel_set_trans_count((uint)dma_channel, PCM5102_DMA_BLOCK, true);
    } else {
        dma_running = false;
        starvation_count++;
        __dmb();
    }
}

bool pcm5102_audio_init(uint32_t sample_rate)
{
    if (dma_channel >= 0) return true;
    state_machine = pio_claim_unused_sm(PCM5102_PIO, false);
    if (state_machine < 0 || !pio_can_add_program(PCM5102_PIO, &pcm5102_audio_program)) {
        printf("PCM5102A: no free PIO1 resource\n");
        return false;
    }
    // The launcher leaves these pads disconnected from PIO.
    gpio_set_function(PCM5102_DATA_PIN, GPIO_FUNC_PIO1);
    gpio_set_function(PCM5102_BCLK_PIN, GPIO_FUNC_PIO1);
    gpio_set_function(PCM5102_LRCLK_PIN, GPIO_FUNC_PIO1);
    uint offset = pio_add_program(PCM5102_PIO, &pcm5102_audio_program);
    pcm5102_audio_program_init(PCM5102_PIO, (uint)state_machine, offset,
                               PCM5102_DATA_PIN, PCM5102_BCLK_PIN);
    uint32_t divider = clock_get_hz(clk_sys) * 4u / sample_rate;
    pio_sm_set_clkdiv_int_frac(PCM5102_PIO, (uint)state_machine,
                               divider >> 8u, divider & 0xffu);
    pio_sm_set_enabled(PCM5102_PIO, (uint)state_machine, true);

    // HDMI reserves DMA IRQ0.
    for (int channel = 4; channel < NUM_DMA_CHANNELS; ++channel) {
        if (!dma_channel_is_claimed((uint)channel)) {
            dma_channel_claim((uint)channel);
            dma_channel = channel;
            break;
        }
    }
    if (dma_channel < 0) {
        printf("PCM5102A: no free DMA channel\n");
        return false;
    }
    dma_channel_config config = dma_channel_get_default_config((uint)dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config,
        pio_get_dreq(PCM5102_PIO, (uint)state_machine, true));
    dma_channel_configure((uint)dma_channel, &config,
                          &PCM5102_PIO->txf[state_machine], audio_ring,
                          PCM5102_DMA_BLOCK, false);

    dma_irq = dma_channel >= 4 ? 1 : 0;
    if (dma_irq == 0) {
        dma_channel_set_irq0_enabled((uint)dma_channel, true);
        irq_add_shared_handler(DMA_IRQ_0, pcm5102_dma_handler,
                               PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        irq_set_enabled(DMA_IRQ_0, true);
    } else {
        dma_channel_set_irq1_enabled((uint)dma_channel, true);
        irq_add_shared_handler(DMA_IRQ_1, pcm5102_dma_handler,
                               PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        irq_set_enabled(DMA_IRQ_1, true);
    }
    write_index = read_index = 0;
    dma_running = false;
    starvation_count = 0;
    printf("PCM5102A audio: 44100 Hz GPIO20/21/22, PIO1 SM%d DMA%d IRQ%d\n",
           state_machine, dma_channel, dma_irq);
    return true;
}

unsigned pcm5102_audio_free(void)
{
    return (read_index - write_index - 1u) & PCM5102_RING_MASK;
}

unsigned pcm5102_audio_level(void)
{
    return ring_used();
}

uint32_t pcm5102_audio_get_starvation_count(void)
{
    return starvation_count;
}

void __not_in_flash_func(pcm5102_audio_push)(int16_t left, int16_t right)
{
    if (dma_channel < 0) return;
    unsigned next = (write_index + 1u) & PCM5102_RING_MASK;
    if (next == read_index) return;
    audio_ring[write_index] = ((uint32_t)(uint16_t)left << 16) |
                              (uint16_t)right;
    __dmb();
    write_index = next;
    start_dma_if_ready();
}
