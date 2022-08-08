#include <rsp.inc>

    .globl _start
    .data
RESULT: .long 0,0

    .text

_start:
    mfc0 t0, COP0_DP_CLOCK

    mfc0 v1, COP0_SEMAPHORE   # get the semaphore, so the below loop will time out
    lui  a1,0x20
1:
    bltz a1, finish
    mfc0 v1, COP0_SEMAPHORE
    bne  v1, 0, 1b
    addi a1, a1, -1

finish:
    mfc0 t1, COP0_DP_CLOCK
    sw t0, %lo(RESULT)+0
    sw t1, %lo(RESULT)+4
    break
    nop

#include <rsp_assert.inc>
