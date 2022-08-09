#include <stdio.h>
#include <stdalign.h>
#include <libdragon.h>
#include "../libdragon/include/regsinternal.h"

typedef uint64_t xcycle_t;

#define MEASUREMENT_ERROR_CPU    XCYCLE_FROM_CPU(1)    // Sampling error when measuring CPU cycles
#define MEASUREMENT_ERROR_RCP    XCYCLE_FROM_CPU(4)    // Sampling error when measuring RCP cycles

#define XCYCLES_PER_SECOND   XCYCLE_FROM_CPU(CPU_FREQUENCY)
#define XCYCLE_FROM_COP0(t)  ((t)*4)
#define XCYCLE_FROM_CPU(t)   ((t)*2)
#define XCYCLE_FROM_RCP(t)   ((t)*3)

typedef enum {
    CYCLE_RCP,
    CYCLE_CPU,
    CYCLE_COP0,
} cycletype_t;

typedef enum {
    UNIT_BYTES
} unit_t;

struct benchmark_s;
typedef struct benchmark_s benchmark_t;

typedef struct benchmark_s {
    xcycle_t (*func)(benchmark_t* b);
    const char *name;
    int qty;
    unit_t unit;
    cycletype_t cycletype;

    xcycle_t expected;
    float tolerance;
    xcycle_t found;
    bool passed_0p, passed_5p, passed_10p, passed_30p;
} benchmark_t;

DEFINE_RSP_UCODE(rsp_bench);

uint8_t rambuf[1024*1024] alignas(64);

static volatile struct VI_regs_s * const VI_regs = (struct VI_regs_s *)0xa4400000;
static volatile struct PI_regs_s * const PI_regs = (struct PI_regs_s *)0xa4600000;
#define PI_STATUS_DMA_BUSY ( 1 << 0 )
#define PI_STATUS_IO_BUSY  ( 1 << 1 )

#define TIMEIT(setup, stmt) ({ \
    MEMORY_BARRIER(); \
    setup; \
    uint32_t __t0 = TICKS_READ(); \
    stmt; \
    uint32_t __t1 = TICKS_READ(); \
    MEMORY_BARRIER(); \
    XCYCLE_FROM_COP0(TICKS_DISTANCE(__t0, __t1)); \
})

#define TIMEIT_WHILE(setup, stmt, cond) ({ \
    register uint32_t __t1,__t2,__t3,__t4,__t5,__t6, __t7, __t8; \
    register bool __c1, __c2, __c3, __c4, __c5, __c6, __c7, __c8; \
    MEMORY_BARRIER(); \
    setup; \
    uint32_t __t0 = TICKS_READ(); \
    stmt; \
    do { \
        __t1 = TICKS_READ(); __c1 = (cond); \
        __t2 = TICKS_READ(); __c2 = (cond); \
        __t3 = TICKS_READ(); __c3 = (cond); \
        __t4 = TICKS_READ(); __c4 = (cond); \
        __t5 = TICKS_READ(); __c5 = (cond); \
        __t6 = TICKS_READ(); __c6 = (cond); \
        __t7 = TICKS_READ(); __c7 = (cond); \
        __t8 = TICKS_READ(); __c8 = (cond); \
    } while (__c8); \
    uint32_t __tend; \
    if (!__c1) __tend = __t1; \
    else if (!__c2) __tend = __t2; \
    else if (!__c3) __tend = __t3; \
    else if (!__c4) __tend = __t4; \
    else if (!__c5) __tend = __t5; \
    else if (!__c6) __tend = __t6; \
    else if (!__c7) __tend = __t7; \
    else __tend = __t8; \
    MEMORY_BARRIER(); \
    XCYCLE_FROM_COP0(TICKS_DISTANCE(__t0, __tend)); \
})

__attribute__((noinline))
xcycle_t timeit_average(xcycle_t *samples, int n) {
    int min=0,max=0;
    for (int i=1;i<n;i++) {
        if (samples[i] < samples[min]) min=i;
        if (samples[i] > samples[max]) max=i;
    }
    xcycle_t total = 0;
    for (int i=0;i<n;i++)
        if (i != min && i != max)
            total += samples[i];
    return total / (n-2);
}

#define TIMEIT_MULTI(n, setup, stmt) ({ \
    int __n = (n); \
    xcycle_t __samples[__n]; \
    for (int __i=0; __i < __n; __i++) \
        __samples[__i] = TIMEIT(setup, stmt); \
    timeit_average(__samples, __n); \
})

#define TIMEIT_WHILE_MULTI(n, setup, stmt, cond) ({ \
    int __n = (n); \
    xcycle_t __samples[__n]; \
    for (int __i=0; __i < __n; __i++) \
        __samples[__i] = TIMEIT_WHILE(setup, stmt, cond); \
    timeit_average(__samples, __n); \
})

static inline void fill_out_buffer(void) {
    uint32_t *__buf = UncachedAddr(rambuf);
    __buf[0] = 0;
    __buf[1] = 0;
    __buf[2] = 0;
    __buf[3] = 0;
    __buf[4] = 0;
    __buf[5] = 0;
}

xcycle_t bench_rcp_io_r(benchmark_t *b) {
    return TIMEIT_MULTI(50, ({ }), ({ (void)VI_regs->control; }));
}

xcycle_t bench_rcp_io_w(benchmark_t *b) {
    return TIMEIT_MULTI(50, fill_out_buffer(), ({ VI_regs->control = 0; }));
}

xcycle_t bench_pidma(benchmark_t* b) {
    return TIMEIT_WHILE_MULTI(10, ({
        PI_regs->ram_address = rambuf;
        PI_regs->pi_address = 0x10000000;
    }), ({
        PI_regs->write_length = b->qty-1;        
    }), ({
        PI_regs->status & (PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY);
    }));
}

xcycle_t bench_piior(benchmark_t* b) {
    volatile uint32_t *ROM = (volatile uint32_t*)0xB0000000;
    return TIMEIT_MULTI(50, ({ }), ({ (void)*ROM; }));
}

xcycle_t bench_piiow(benchmark_t* b) {
    volatile uint32_t *ROM = (volatile uint32_t*)0xB0000000;
    return TIMEIT_WHILE_MULTI(50, ({ }), ({ 
        ROM[0] = 0;
    }), ({  
        PI_regs->status & (PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY);
    }));
}

xcycle_t bench_ram_cached_r8(benchmark_t *b) {
    volatile uint8_t *RAM = (volatile uint8_t*)(rambuf);
    return TIMEIT_MULTI(50, ({ (void)*RAM; }), ({ (void)*RAM; }));
}

xcycle_t bench_ram_cached_r16(benchmark_t *b) {
    volatile uint16_t *RAM = (volatile uint16_t*)(rambuf);
    return TIMEIT_MULTI(50, ({ (void)*RAM; }), ({ (void)*RAM; }));
}

xcycle_t bench_ram_cached_r32(benchmark_t *b) {
    volatile uint32_t *RAM = (volatile uint32_t*)(rambuf);
    return TIMEIT_MULTI(50, ({ (void)*RAM; }), ({ (void)*RAM; }));
}

xcycle_t bench_ram_cached_r64(benchmark_t *b) {
    volatile uint64_t *RAM = (volatile uint64_t*)(rambuf);
    return TIMEIT_MULTI(50, ({ (void)*RAM; }), ({ (void)*RAM; }));
}

xcycle_t bench_ram_uncached_r8(benchmark_t *b) {
    volatile uint8_t *RAM = (volatile uint8_t*)UncachedAddr(rambuf);
    return TIMEIT_MULTI(50, ({ }), ({ (void)*RAM; }));
}

xcycle_t bench_ram_uncached_r16(benchmark_t *b) {
    volatile uint16_t *RAM = (volatile uint16_t*)UncachedAddr(rambuf);
    return TIMEIT_MULTI(50, ({ }), ({ (void)*RAM; }));
}

xcycle_t bench_ram_uncached_r32(benchmark_t *b) {
    volatile uint32_t *RAM = (volatile uint32_t*)UncachedAddr(rambuf);
    return TIMEIT_MULTI(50, ({ }), ({ (void)*RAM; }));
}

xcycle_t bench_ram_uncached_r64(benchmark_t *b) {
    volatile uint64_t *RAM = (volatile uint64_t*)UncachedAddr(rambuf);
    return TIMEIT_MULTI(50, ({ }), ({ (void)*RAM; }));
}

xcycle_t bench_ram_uncached_r32_seq(benchmark_t *b) {
    volatile uint32_t *RAM0 = (volatile uint32_t*)UncachedAddr(rambuf);
    volatile uint32_t *RAM1 = (volatile uint32_t*)UncachedAddr(rambuf+4);
    volatile uint32_t *RAM2 = (volatile uint32_t*)UncachedAddr(rambuf+8);
    volatile uint32_t *RAM3 = (volatile uint32_t*)UncachedAddr(rambuf+12);
    return TIMEIT_MULTI(50, ({ }), ({ (void)*RAM0; (void)*RAM1; (void)*RAM2; (void)*RAM3; }));
}

xcycle_t bench_ram_uncached_r32_random(benchmark_t *b) {
    volatile uint32_t *RAM0 = (volatile uint32_t*)UncachedAddr(rambuf+1024);
    volatile uint32_t *RAM1 = (volatile uint32_t*)UncachedAddr(rambuf+12);
    volatile uint32_t *RAM2 = (volatile uint32_t*)UncachedAddr(rambuf+568);
    volatile uint32_t *RAM3 = (volatile uint32_t*)UncachedAddr(rambuf+912);
    return TIMEIT_MULTI(50, ({ }), ({ (void)*RAM0; (void)*RAM1; (void)*RAM2; (void)*RAM3; }));
}

xcycle_t bench_ram_uncached_r32_multibank(benchmark_t *b) {
    volatile uint32_t *RAM0 = (volatile uint32_t*)UncachedAddr(0x80000000);
    volatile uint32_t *RAM1 = (volatile uint32_t*)UncachedAddr(0x80100000);
    volatile uint32_t *RAM2 = (volatile uint32_t*)UncachedAddr(0x80200000);
    volatile uint32_t *RAM3 = (volatile uint32_t*)UncachedAddr(0x80300000);
    return TIMEIT_MULTI(50, ({ }), ({ (void)*RAM0; (void)*RAM1; (void)*RAM2; (void)*RAM3; }));
}

/**************************************************************************************/

benchmark_t benchs[] = {
    { bench_ram_cached_r8,  "RDRAM C8R",     1,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(3) },
    { bench_ram_cached_r16, "RDRAM C16R",    2,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(3) },
    { bench_ram_cached_r32, "RDRAM C32R",    4,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(3) },
    { bench_ram_cached_r64, "RDRAM C64R",    8,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(3) },

    { bench_ram_uncached_r8,  "RDRAM U8R",     1,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(34) },
    { bench_ram_uncached_r16, "RDRAM U16R",    2,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(34) },
    { bench_ram_uncached_r32, "RDRAM U32R",    4,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(34) },
    { bench_ram_uncached_r64, "RDRAM U64R",    8,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(37) },

    { bench_ram_uncached_r32_seq,       "RDRAM U32R seq",    4*4,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(134) },
    { bench_ram_uncached_r32_random,    "RDRAM U32R rand",   4*4,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(134) },
    { bench_ram_uncached_r32_multibank, "RDRAM U32R banked", 4*4,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(136) },

    { bench_rcp_io_r, "RCP I/O R",    1,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(24) },
    // { bench_rcp_io_w, "RCP I/O W",   1,   UNIT_BYTES, CYCLE_CPU,  XCYCLE_FROM_CPU(193) },  // FIXME: flush buffer

    { bench_pidma, "PI DMA",        8,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(193) },
    { bench_pidma, "PI DMA",      128,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(1591) },
    { bench_pidma, "PI DMA",     1024,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(12168) },
    { bench_pidma, "PI DMA",  64*1024,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(777807) },

    { bench_piior, "PI I/O R",   4,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(144) },
    { bench_piiow, "PI I/O W",   4,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(134) },
};

void bench_rsp(void)
{
    volatile uint32_t t1;
    volatile bool finish = false;
    void sp_done(void) {
        t1 = TICKS_READ();
        finish = true;
    }

    register_SP_handler(sp_done);
    rsp_load(&rsp_bench);

    enable_interrupts();
    uint32_t t0 = TICKS_READ();
    rsp_run_async();
    while (!finish) {}

    disable_interrupts();
    unregister_SP_handler(sp_done);
    debugf("RSP loop:    %7ld cycles\n", TICKS_DISTANCE(t0, t1));
}

const char *cycletype_name(cycletype_t ct) {
    switch (ct) {
    case CYCLE_COP0: return "COP0";
    case CYCLE_CPU: return "CPU";
    case CYCLE_RCP: return "RCP";
    default: return "";
    }
}

int64_t xcycle_to_cycletype(xcycle_t cycles, cycletype_t ct) {
    switch (ct) {
    case CYCLE_COP0: return cycles/4;
    case CYCLE_CPU: return cycles/2;
    case CYCLE_RCP: return cycles/3;
    default: return 0;
    }
}

void format_speed(char *buf, int nbytes, xcycle_t time) {
    if (time == 0) {
        sprintf(buf, "inf");
        return;
    }
    int64_t bps = (int64_t)XCYCLES_PER_SECOND * (int64_t)nbytes / time;
    if (bps < 1024) {
        sprintf(buf, "%lld byte/s", bps);
    } else if (bps < 1024*1024) {
        sprintf(buf, "%.1f Kbyte/s", (float)bps / 1024);
    } else {
        sprintf(buf, "%.1f Mbyte/s", (float)bps / (1024*1024));
    }
}

int main(void)
{
    rsp_init();
    debug_init_isviewer();
    debug_init_usblog();
    debugf("n64-systembench is alive\n");

    // Disable interrupts. VI is already disabled, so the RCP should be pretty idle
    // now. We could add some asserts to make sure that all peripherals are idle at this point.
    disable_interrupts();
    VI_regs->control = 0;

    int passed_0p  = 0;
    int passed_5p  = 0;
    int passed_10p = 0;
    int passed_30p = 0;
    const int num_benches = sizeof(benchs) / sizeof(benchmark_t);
    for (int i=0;i<num_benches;i++) {
        benchmark_t* b = &benchs[i];

        debugf("*** %s [%d]\n", b->name, b->qty);
        // wait_ms(100);

        // Run the benchmark
        xcycle_t cycles = b->func(b);

        // Truncate the found value, depending on the exact cycle type it should be expressed in.
        switch (b->cycletype) {
        case CYCLE_COP0: cycles = (cycles / 4) * 4; break;
        case CYCLE_CPU:  cycles = (cycles / 2) * 2; break;
        case CYCLE_RCP:  cycles = (cycles / 3) * 3; break;
        }
        b->found = cycles;

        // Dump the results
        int64_t expected = xcycle_to_cycletype(b->expected, b->cycletype);
        int64_t found    = xcycle_to_cycletype(b->found,    b->cycletype);

        char exp_speed[128]={0}, found_speed[128]={0};
        format_speed(exp_speed,   b->qty, b->expected);
        format_speed(found_speed, b->qty, b->found);

        debugf("Expected:  %7lld %s cycles     (%s)\n", expected, cycletype_name(b->cycletype), exp_speed);
        // wait_ms(100);
        debugf("Found:     %7lld %s cycles     (%s)\n", found,    cycletype_name(b->cycletype), found_speed);
        // wait_ms(100);
        debugf("Diff:      %+7lld (%+02.1f%%)\n", found - expected, (float)(found - expected) * 100.0f / (float)expected);
        // wait_ms(100);

        int meas_error = xcycle_to_cycletype((b->cycletype == CYCLE_RCP) ? MEASUREMENT_ERROR_RCP : MEASUREMENT_ERROR_CPU, b->cycletype);

        int diff = found - expected;
        if (diff < 0) diff = -diff;
        float pdiff = (float)diff * 100.0f / (float)expected;
        if (diff <= meas_error || pdiff < 0.2f)  b->passed_0p  = true, passed_0p++;
        if (diff <= meas_error || pdiff < 5.0f)  b->passed_5p  = true, passed_5p++;
        if (diff <= meas_error || pdiff < 10.0f) b->passed_10p = true, passed_10p++;
        if (diff <= meas_error || pdiff < 30.0f) b->passed_30p = true, passed_30p++;
    }

    uint32_t colors[5] = { 0xffffffff, 0x8fb93500, 0xe6e22e00, 0xe09c3b00, 0xe6474700 };
    int page = 0;

    debugf("Benchmarks done\n");
    enable_interrupts();
    controller_init();
    display_init( RESOLUTION_640x240, DEPTH_32_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE );
    while(1) {
        char sbuf[1024];
        display_context_t disp;
        while (!(disp = display_lock())) {}
        controller_scan();
        struct controller_data cont = get_keys_down();

        switch (page) {
        case 0: { // Summary page
            graphics_fill_screen(disp, 0);
            graphics_draw_text(disp, 320-70, 10, "n64-systembench");
            graphics_draw_text(disp, 320-70, 20, "v1.0 - by Rasky");

            sprintf(sbuf, "Tests: %d", num_benches);
            graphics_draw_text(disp, 270, 50, sbuf);

            graphics_draw_text(disp, 200, 70, "Results:");

            graphics_set_color(colors[0], 0);
            sprintf(sbuf, "    +/-  0%%: %2d (%3d %%)", passed_0p, passed_0p * 100 / num_benches);
            graphics_draw_text(disp, 200, 80, sbuf);

            graphics_set_color(colors[1], 0);
            sprintf(sbuf, "    +/-  5%%: %2d (%3d %%)", passed_5p, passed_5p * 100 / num_benches);
            graphics_draw_text(disp, 200, 90, sbuf);

            graphics_set_color(colors[2], 0);
            sprintf(sbuf, "    +/- 10%%: %2d (%3d %%)", passed_10p, passed_10p * 100 / num_benches);
            graphics_draw_text(disp, 200, 100, sbuf);

            graphics_set_color(colors[3], 0);
            sprintf(sbuf, "    +/- 30%%: %2d (%3d %%)", passed_30p, passed_30p * 100 / num_benches);
            graphics_draw_text(disp, 200, 110, sbuf);

            graphics_set_color(colors[4], 0);
            sprintf(sbuf, "     Failed: %2d (%3d %%)", num_benches-passed_30p, (num_benches-passed_30p) * 100 / num_benches);
            graphics_draw_text(disp, 200, 120, sbuf);

            graphics_set_color(0xFFFFFFFF, 0);

            graphics_draw_text(disp, 320-110, 140, "Press START to see details");

            display_show(disp);
            if (cont.c->start) page = 1;
        } break;
        default: { // Details
            enum { BENCH_PER_PAGE = 20 };
            graphics_fill_screen(disp, 0);

            int y = 10;
            for (int i=0;i<BENCH_PER_PAGE;i++) {
                if (i >= num_benches) break;
                benchmark_t* b = &benchs[i];
                int64_t expected = xcycle_to_cycletype(b->expected, b->cycletype);
                int64_t found    = xcycle_to_cycletype(b->found,    b->cycletype);

                     if (b->passed_0p)  graphics_set_color(colors[0], 0);
                else if (b->passed_5p)  graphics_set_color(colors[1], 0);
                else if (b->passed_10p) graphics_set_color(colors[2], 0);
                else if (b->passed_30p) graphics_set_color(colors[3], 0);
                else                    graphics_set_color(colors[4], 0);

                sprintf(sbuf, "%20s %7d | %4s | %7lld | %7lld | %+7lld (%+02.1f%%)",
                    b->name, b->qty, cycletype_name(b->cycletype),
                    expected, found, found-expected, (float)(found - expected) * 100.0f / (float)expected);
                graphics_draw_text(disp, 20, y, sbuf);
                y += 10;
            }
            graphics_set_color(0xFFFFFFFF, 0);

            display_show(disp);
            if (cont.c->start) page = 0;
        } break;
        }
    }
}
