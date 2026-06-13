/*
 * Stage80 Stage-owned XNU-like entry stub.
 *
 * This is deliberately not public XNU _start.  It is a callable loader probe
 * that mimics the early ARM entry shape: r0 carries boot_args*, the stub records
 * entry, then branch-and-link calls a C arm_init-shaped function and returns to
 * the Stage80 loader.
 */

.syntax unified
.cpu cortex-a15
.arch armv7-a
.arm

.equ STAGE80_XNU_ENTRY_RESULT_START_ENTERED_OFFSET, 48
.equ STAGE80_XNU_ENTRY_RESULT_ARM_INIT_CALLED_OFFSET, 52
.equ STAGE80_XNU_ENTRY_RESULT_ARM_INIT_RETURNED_OFFSET, 56
.equ STAGE80_XNU_ENTRY_RESULT_ARM_INIT_STATUS_OFFSET, 64

.section .text.stage80_xnu_start_stub,"ax",%progbits
.global stage80_xnu_start_stub
.type stage80_xnu_start_stub, %function
stage80_xnu_start_stub:
    push    {r4-r8, lr}

    mov     r4, r0          /* boot_args* */
    mov     r5, r1          /* result* */
    cmp     r5, #0
    beq     1f

    mov     r2, #1
    str     r2, [r5, #STAGE80_XNU_ENTRY_RESULT_START_ENTERED_OFFSET]
    str     r2, [r5, #STAGE80_XNU_ENTRY_RESULT_ARM_INIT_CALLED_OFFSET]

    mov     r0, r4
    mov     r1, r5
    bl      stage80_arm_init_stub

    mov     r4, r0
    mov     r2, #1
    str     r2, [r5, #STAGE80_XNU_ENTRY_RESULT_ARM_INIT_RETURNED_OFFSET]
    str     r4, [r5, #STAGE80_XNU_ENTRY_RESULT_ARM_INIT_STATUS_OFFSET]
    mov     r0, r4
    pop     {r4-r8, lr}
    bx      lr

1:
    movw    r0, #0x0000
    movt    r0, #0x8000
    pop     {r4-r8, lr}
    bx      lr
.size stage80_xnu_start_stub, . - stage80_xnu_start_stub
