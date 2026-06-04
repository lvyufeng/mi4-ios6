/*
 * Xiaomi Mi 4 (cancro) Stage5 entry trampoline.
 *
 * Stage5 builds on the Stage3 C runtime and read-only hardware probes.
 * It installs our own exception vectors through VBAR and exercises the
 * ARM generic timer as an early timebase.
 *
 * This trampoline does the minimum to enter C:
 *   - mask interrupts
 *   - set up a private stack
 *   - zero .bss
 *   - install stage5_vectors in VBAR
 *   - call stage5_main()
 *
 * It is packaged as the Android boot image kernel and entered at 0x00008000
 * through non-persistent `fastboot boot`.
 */

.syntax unified
.cpu cortex-a15
.arch armv7-a
.arm

.equ STAGE5_STACK_SIZE, 16384

.section .text._start,"ax",%progbits
.global _start
.type _start, %function
_start:
    cpsid   if

    ldr     sp, =stage5_stack_top

    /* Give exception modes valid banked stacks before installing VBAR. */
    cps     #0x17                    /* ABT */
    ldr     sp, =stage5_abt_stack_top
    cps     #0x1b                    /* UND */
    ldr     sp, =stage5_und_stack_top
    cps     #0x12                    /* IRQ */
    ldr     sp, =stage5_irq_stack_top
    cps     #0x11                    /* FIQ */
    ldr     sp, =stage5_fiq_stack_top
    cps     #0x13                    /* SVC */
    ldr     sp, =stage5_stack_top

    /* zero .bss */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    bcs     2f
    str     r2, [r0], #4
    b       1b
2:
    /* Install our exception vector table after stack/.bss are valid. */
    ldr     r0, =stage5_vectors
    mcr     p15, 0, r0, c12, c0, 0      /* VBAR */
    dsb     sy
    isb

    bl      stage5_main

    /* If stage5_main ever returns, spin safely. */
3:  wfe
    b       3b
.size _start, . - _start

.section .bss
.align 4
stage5_stack:
    .space  STAGE5_STACK_SIZE
.global stage5_stack_top
stage5_stack_top:

.align 4
stage5_abt_stack:
    .space  1024
.global stage5_abt_stack_top
stage5_abt_stack_top:

.align 4
stage5_und_stack:
    .space  1024
.global stage5_und_stack_top
stage5_und_stack_top:

.align 4
stage5_irq_stack:
    .space  1024
.global stage5_irq_stack_top
stage5_irq_stack_top:

.align 4
stage5_fiq_stack:
    .space  1024
.global stage5_fiq_stack_top
stage5_fiq_stack_top:
