/*
 * Stage5 ARMv7 exception vector table.
 *
 * Installed through VBAR by start.S after stack/.bss setup. The abort handlers
 * log a persistent breadcrumb and reboot, so later bring-up failures are not
 * silent hangs.
 */

.syntax unified
.cpu cortex-a15
.arch armv7-a
.arm

.section .vectors,"ax",%progbits
.align 5
.global stage5_vectors
.type stage5_vectors, %function
stage5_vectors:
    b   vector_reset
    b   vector_undef
    b   vector_svc
    b   vector_prefetch_abort
    b   vector_data_abort
    b   vector_unused
    b   vector_irq
    b   vector_fiq
.size stage5_vectors, . - stage5_vectors

.section .text,"ax",%progbits
.align 2
vector_reset:
    b   vector_reset

vector_undef:
    ldr r0, =msg_undef
    b   stage5_exception_common

vector_svc:
    ldr r0, =msg_svc
    b   stage5_exception_common

vector_prefetch_abort:
    sub lr, lr, #4
    ldr r0, =msg_pabort
    b   stage5_exception_common

vector_data_abort:
    sub lr, lr, #8
    ldr r0, =msg_dabort
    b   stage5_exception_common

vector_unused:
    ldr r0, =msg_unused
    b   stage5_exception_common

vector_irq:
    ldr r0, =msg_irq
    b   stage5_exception_common

vector_fiq:
    ldr r0, =msg_fiq
    b   stage5_exception_common

/*
 * r0 = message. Preserve enough state to print the exception type plus LR/CPSR.
 * The stack was installed by start.S before VBAR is set. We keep IRQ/FIQ masked.
 */
.global stage5_exception_common
.type stage5_exception_common, %function
stage5_exception_common:
    cpsid if
    mov r4, r0
    mov r5, lr
    mrs r6, spsr

    ldr r0, =msg_exception_prefix
    bl  log_puts
    mov r0, r4
    bl  log_puts
    ldr r0, =msg_exception_lr
    bl  log_puts
    mov r0, r5
    bl  log_hex32
    ldr r0, =msg_exception_spsr
    bl  log_puts
    mov r0, r6
    bl  log_hex32
    ldr r0, =msg_exception_nl
    bl  log_puts

    bl  platform_reboot
1:  wfe
    b   1b
.size stage5_exception_common, . - stage5_exception_common

.align 2
msg_exception_prefix:
    .asciz "MI4IOS6_STAGE5 exception: "
msg_exception_lr:
    .asciz " lr="
msg_exception_spsr:
    .asciz " spsr="
msg_exception_nl:
    .asciz "\n"
msg_undef:
    .asciz "undefined"
msg_svc:
    .asciz "svc"
msg_pabort:
    .asciz "prefetch-abort"
msg_dabort:
    .asciz "data-abort"
msg_unused:
    .asciz "unused"
msg_irq:
    .asciz "irq"
msg_fiq:
    .asciz "fiq"

.align 2
.global trigger_stage5_undef_test
.type trigger_stage5_undef_test, %function
trigger_stage5_undef_test:
    .word 0xe7f000f0          /* ARM UDF/undefined instruction */
    bx lr
.size trigger_stage5_undef_test, . - trigger_stage5_undef_test
