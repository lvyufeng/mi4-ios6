/*
 * Xiaomi Mi 4 (cancro) Stage3 entry trampoline.
 *
 * Stage3 is the third bring-up milestone. Stage0 proved the bootloader runs
 * our raw ARMv7 code; Stage1 proved we can build an XNU-style boot_args and
 * hand off to a kernel-entry stub. Stage3 moves the logic into C so we can
 * build and walk a fuller Apple-style device tree.
 *
 * This trampoline does the minimum to enter C:
 *   - mask interrupts
 *   - set up a private stack
 *   - zero .bss
 *   - call stage3_main()
 *
 * It is packaged as the Android boot image kernel and entered at 0x00008000
 * through non-persistent `fastboot boot`.
 */

.syntax unified
.cpu cortex-a15
.arch armv7-a
.arm

.equ STAGE3_STACK_SIZE, 16384

.section .text._start,"ax",%progbits
.global _start
.type _start, %function
_start:
    cpsid   if

    ldr     sp, =stage3_stack_top

    /* zero .bss */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    bcs     2f
    str     r2, [r0], #4
    b       1b
2:
    bl      stage3_main

    /* If stage3_main ever returns, spin safely. */
3:  wfe
    b       3b
.size _start, . - _start

.section .bss
.align 4
stage3_stack:
    .space  STAGE3_STACK_SIZE
.global stage3_stack_top
stage3_stack_top:
