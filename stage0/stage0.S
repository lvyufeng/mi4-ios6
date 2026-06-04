/*
 * Xiaomi Mi 4 (cancro) Stage0 payload
 *
 * This is a tiny non-Linux ARMv7 payload intended to be loaded by the
 * stock Android bootloader through `fastboot boot` as the Android boot image
 * kernel. It writes a marker into the Android ram_console persistent RAM
 * buffer, then attempts to warm-reset the device through MSM8974 PS_HOLD.
 *
 * It is deliberately pure assembly and self-contained: no stack, no libc,
 * no MMU setup, no external symbols.
 */

.syntax unified
.cpu cortex-a15
.arch armv7-a
.arm

/* Android bootloader loads/jumps to kernel_addr = 0x00008000 on cancro. */
.equ STAGE0_BASE,        0x00008000

/* Android persistent_ram_buffer at top-of-DRAM minus 2 MiB.
 * Observed System RAM ends at 0xde6fffff, so top is 0xde700000 and
 * ram_console start is likely 0xde500000.
 */
.equ RAM_CONSOLE_BASE,   0xde500000
.equ RAM_CONSOLE_SIG,    0x43474244      /* 'DBGC' little-endian */

/* MSM8974 restart v2 PS_HOLD physical address. */
.equ MSM8974_PSHOLD,     0xfc4ab000

/* Optional restart reason IMEM physical address.
 * Linux maps MSM_IMEM_BASE from DT; source default phys is 0x0fa00000.
 * restart reason offset is 0x65c. This is best-effort only.
 */
.equ MSM_IMEM_BASE_PHYS, 0x0fa00000
.equ RESTART_REASON,     (MSM_IMEM_BASE_PHYS + 0x65c)

.global _start
.type _start, %function
_start:
    cpsid   if

    /* Write Android persistent_ram_buffer header. */
    ldr     r0, =RAM_CONSOLE_BASE
    ldr     r1, =RAM_CONSOLE_SIG
    str     r1, [r0, #0]          /* sig */
    mov     r1, #0
    str     r1, [r0, #4]          /* start */

    ldr     r2, =marker
    ldr     r3, =marker_end
    sub     r3, r3, r2            /* marker length */
    str     r3, [r0, #8]          /* size */

    add     r0, r0, #12           /* data[] */
1:  cmp     r3, #0
    beq     2f
    ldrb    r1, [r2], #1
    strb    r1, [r0], #1
    subs    r3, r3, #1
    b       1b

2:
    dsb     sy
    isb

    /* Best-effort restart reason: normal reboot. */
    ldr     r0, =RESTART_REASON
    ldr     r1, =0x77665501
    str     r1, [r0]
    dsb     sy

    /* Best-effort MSM8974 restart v2: lower PS_HOLD. */
    ldr     r0, =MSM8974_PSHOLD
    mov     r1, #0
    str     r1, [r0]
    dsb     sy

    /* If reset did not happen, wait forever. User can long-press power. */
3:
    wfe
    b       3b

.align 4
marker:
    .ascii "MI4IOS6_STAGE0 v1 entered; wrote Android ram_console; attempting MSM8974 PS_HOLD reset\n"
marker_end:

.size _start, . - _start
