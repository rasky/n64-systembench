#include <stdio.h>
#include <stdalign.h>
#include <libdragon.h>
#include "../libdragon/include/regsinternal.h"

uint8_t rambuf[1024*1024] alignas(64);

static volatile struct PI_regs_s * const PI_regs = (struct PI_regs_s *)0xa4600000;
#define PI_STATUS_DMA_BUSY ( 1 << 0 )
#define PI_STATUS_IO_BUSY  ( 1 << 1 )

int bench_pidma_single(int size)
{
    while (PI_regs->status & (PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY)) {}
    MEMORY_BARRIER();
    PI_regs->ram_address = rambuf;
    MEMORY_BARRIER();
    PI_regs->pi_address = 0x10000000;
    MEMORY_BARRIER();
    PI_regs->write_length = size-1;
    MEMORY_BARRIER();
    uint32_t t0 = TICKS_READ();
    while (PI_regs->status & (PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY)) {}
    uint32_t t1 = TICKS_READ();
    return TICKS_DISTANCE(t0, t1);
}

void bench_pidma(void)
{
    debugf("PI DMA 8b:   %7d cycles\n", bench_pidma_single(8));
    debugf("PI DMA 64b:  %7d cycles\n", bench_pidma_single(64));
    debugf("PI DMA 512b: %7d cycles\n", bench_pidma_single(512));
    debugf("PI DMA 4k:   %7d cycles\n", bench_pidma_single(4*1024));
    debugf("PI DMA 16k:  %7d cycles\n", bench_pidma_single(16*1024));
    debugf("PI DMA 1Mb:  %7d cycles\n", bench_pidma_single(1024*1024));
}

int main(void)
{
    debug_init_isviewer();
    debug_init_usblog();
    debugf("n64-systembench is alive\n");

    // Disable interrupts. VI is already disabled, so the RCP should be pretty idle
    // now. We could add some asserts to make sure that all peripherals are idle at this point.
    disable_interrupts();

    // Run benchmarks
    bench_pidma();

    debugf("Benchmarks done\n");
    enable_interrupts();
    console_init();
    while(1) {}
}
