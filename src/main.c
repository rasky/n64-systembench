#include <stdio.h>
#include <stdalign.h>
#include <libdragon.h>
#include "../libdragon/include/regsinternal.h"

typedef uint64_t xcycle_t;

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

/**************************************************************************************/

benchmark_t benchs[] = {
    { bench_pidma, "PI DMA (default speed)",        8,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(193) },
    { bench_pidma, "PI DMA (default speed)",      128,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(1591) },
    { bench_pidma, "PI DMA (default speed)",     1024,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(12168) },
    { bench_pidma, "PI DMA (default speed)",  64*1024,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(777807) },

    { bench_piior, "PI I/O Read (default speed)",   4,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(144) },
    { bench_piiow, "PI I/O Write (default speed)",  4,   UNIT_BYTES, CYCLE_RCP,  XCYCLE_FROM_RCP(134) },
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

    const int num_benches = sizeof(benchs) / sizeof(benchmark_t);
    for (int i=0;i<num_benches;i++) {
        benchmark_t* b = &benchs[i];

        debugf("*** %s [%d]\n", b->name, b->qty);
        wait_ms(100);

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

        debugf("Expected:  %7lld %s cycles\n", expected, cycletype_name(b->cycletype));
        wait_ms(100);
        debugf("Found:     %7lld %s cycles\n", found,    cycletype_name(b->cycletype));
        wait_ms(100);
        debugf("Diff:      %+7lld (%+02.1f%%)\n", found - expected, (float)(found - expected) * 100.0f / (float)expected);
        wait_ms(100);
    }

    debugf("Benchmarks done\n");
    console_init();
    enable_interrupts();
    while(1) {}
}
