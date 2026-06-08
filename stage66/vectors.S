/*
 * Stage66 ARMv7 exception vector table.
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
.global stage66_vectors
.type stage66_vectors, %function
stage66_vectors:
    b   vector_reset
    b   vector_undef
    b   vector_svc
    b   vector_prefetch_abort
    b   vector_data_abort
    b   vector_unused
    b   vector_irq
    b   vector_fiq
.size stage66_vectors, . - stage66_vectors

.section .text,"ax",%progbits
.align 2
vector_reset:
    b   vector_reset

vector_undef:
    ldr r0, =msg_undef
    b   stage66_exception_common

vector_svc:
    ldr r0, =msg_svc
    b   stage66_exception_common

vector_prefetch_abort:
    sub lr, lr, #4
    ldr r0, =msg_pabort
    b   stage66_exception_common

vector_data_abort:
    sub lr, lr, #8
    ldr r0, =msg_dabort
    b   stage66_exception_common

vector_unused:
    ldr r0, =msg_unused
    b   stage66_exception_common

vector_irq:
    /*
     * IRQ is the first returnable exception path in this experiment.
     * ARM IRQ entry sets LR_irq to the interrupted PC + 4; adjust it to
     * the actual resume PC, preserve the volatile interrupted registers,
     * let the C handler acknowledge/end the GIC interrupt, then return by
     * restoring CPSR from SPSR_irq through `subs pc, lr, #0`.
     */
    sub lr, lr, #4
    stmdb sp!, {r0-r3, r12, lr}
    bl  stage66_irq_c_handler
    ldmia sp!, {r0-r3, r12, lr}
    subs pc, lr, #0

vector_fiq:
    ldr r0, =msg_fiq
    b   stage66_exception_common

/*
 * r0 = message. Preserve enough state to print the exception type plus LR/CPSR.
 * The stack was installed by start.S before VBAR is set. We keep IRQ/FIQ masked.
 */
.global stage66_exception_common
.type stage66_exception_common, %function
stage66_exception_common:
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
.size stage66_exception_common, . - stage66_exception_common

.align 2
msg_exception_prefix:
    .asciz "MI4IOS6_STAGE66 exception: "
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
.global trigger_stage66_undef_test
.type trigger_stage66_undef_test, %function
trigger_stage66_undef_test:
    .word 0xe7f000f0          /* ARM UDF/undefined instruction */
    bx lr
.size trigger_stage66_undef_test, . - trigger_stage66_undef_test

.align 2
.global trigger_stage66_data_abort_test
.type trigger_stage66_data_abort_test, %function
trigger_stage66_data_abort_test:
    ldr r0, =0xffffffff
    ldr r1, [r0]              /* expected to data-abort on MSM8974 */
    bx lr
.size trigger_stage66_data_abort_test, . - trigger_stage66_data_abort_test
