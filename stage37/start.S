/*
 * Xiaomi Mi 4 (cancro) Stage37 entry trampoline.
 *
 * Stage37 builds on the Stage3 C runtime and read-only hardware probes.
 * It installs our own exception vectors through VBAR and exercises the
 * ARM generic timer as an early timebase.
 *
 * This trampoline does the minimum to enter C:
 *   - mask interrupts
 *   - set up a private stack
 *   - zero .bss
 *   - install stage37_vectors in VBAR
 *   - call stage37_main()
 *
 * It is packaged as the Android boot image kernel and entered at 0x00008000
 * through non-persistent `fastboot boot`.
 */

.syntax unified
.cpu cortex-a15
.arch armv7-a
.arm

.equ STAGE37_STACK_SIZE, 16384

.section .text._start,"ax",%progbits
.global _start
.type _start, %function
_start:
    cpsid   if

    ldr     sp, =stage37_stack_top

    /* Give exception modes valid banked stacks before installing VBAR. */
    cps     #0x17                    /* ABT */
    ldr     sp, =stage37_abt_stack_top
    cps     #0x1b                    /* UND */
    ldr     sp, =stage37_und_stack_top
    cps     #0x12                    /* IRQ */
    ldr     sp, =stage37_irq_stack_top
    cps     #0x11                    /* FIQ */
    ldr     sp, =stage37_fiq_stack_top
    cps     #0x13                    /* SVC */
    ldr     sp, =stage37_stack_top

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
    ldr     r0, =stage37_vectors
    mcr     p15, 0, r0, c12, c0, 0      /* VBAR */
    dsb     sy
    isb

    bl      stage37_main

    /* If stage37_main ever returns, spin safely. */
3:  wfe
    b       3b
.size _start, . - _start

.section .bss
.align 4
stage37_stack:
    .space  STAGE37_STACK_SIZE
.global stage37_stack_top
stage37_stack_top:

.align 4
stage37_abt_stack:
    .space  1024
.global stage37_abt_stack_top
stage37_abt_stack_top:

.align 4
stage37_und_stack:
    .space  1024
.global stage37_und_stack_top
stage37_und_stack_top:

.align 4
stage37_irq_stack:
    .space  1024
.global stage37_irq_stack_top
stage37_irq_stack_top:

.align 4
stage37_fiq_stack:
    .space  1024
.global stage37_fiq_stack_top
stage37_fiq_stack_top:
