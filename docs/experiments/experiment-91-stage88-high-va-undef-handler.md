# Experiment 91: Stage88 — High-VA Undefined Instruction Handler

**Date:** 2026-06-15  
**Device:** Xiaomi Mi 4 cancro (MSM8974)  
**Boot method:** Non-persistent `fastboot boot` (no flash, clean recovery to Android)  
**Status:** ✅ **ACHIEVED** — `kernel_entry returned success`

## Goal

Prove that ARM undefined instruction exception handlers can execute from the
**L2-page-mapped high-virtual kernel address** at `0x80000000+offset` by intentionally
executing an undefined instruction with VBAR pointing to high-VA exception vectors.

Stage86 proved asynchronous exceptions (IRQ). Stage87 proved data aborts (synchronous).
Stage88 extends this to **undefined instruction exceptions** — the third core exception type.

## Result

```
stage88_xnu_arm_vm_init_high_va_undef_handler_status=0x88000001
stage88_xnu_arm_vm_init_high_va_undef_handler_satisfied_mask=0x0000003f
stage88_xnu_arm_vm_init_high_va_undef_handler_failure_mask=0x00000000
stage88_xnu_arm_vm_init_high_va_undef_handler_undef_triggered=0x00000001
stage88_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x00000001
stage88_xnu_arm_vm_init_high_va_undef_handler_undef_count=0x00000001
stage88_xnu_arm_vm_init_high_va_undef_handler_undef_last_addr=0x000479b4
stage88_xnu_arm_vm_init_high_va_undef_handler_undef_addr_valid=0x00000001
loader_status=0x88000001
kernel_entry ok
kernel_entry returned success
```

**Undefined instruction handler log:**
```
MI4IOS6_STAGE88 undef: addr=0x000479b4 lr=0x000479b8
```

- **addr=0x000479b4**: Address of the undefined instruction
- **lr=0x000479b8**: Return address (next instruction, addr+4)

## Implementation

### 1. Updated `vectors.S`

```asm
vector_undef:
    /*
     * Undefined Instruction is Stage88's returnable exception handler.
     * ARM undef entry sets LR_und to the next instruction (PC + 4).
     * We return to the next instruction after the undefined opcode.
     */
    stmdb sp!, {r0-r3, r12, lr} @ Save volatile regs and LR
    mov r0, sp                  @ Pass stack pointer to C handler
    bl  stage88_undef_c_handler
    ldmia sp!, {r0-r3, r12, lr} @ Restore
    movs pc, lr                 @ Return, restoring CPSR from SPSR_und
```

### 2. Created `xnu_arm_vm_init_high_va_undef_handler.c`

**`stage88_undef_c_handler(uint32_t *stack_ptr)`**: Global undef handler

```c
void stage88_undef_c_handler(uint32_t *stack_ptr)
{
    uint32_t lr = stack_ptr[5];  /* LR is at offset [5] after r0-r3, r12 */
    uint32_t undef_instr_addr = lr - 4;  /* The undefined instruction address */

    /* Record exception in global state */
    g_undef_last_addr = undef_instr_addr;
    g_undef_handled = 1;

    /* Log for debugging */
    log_puts("MI4IOS6_STAGE88 undef: addr=");
    log_hex32(undef_instr_addr);
    log_puts(" lr=");
    log_hex32(lr);
    log_puts("\n");

    g_undef_count++;

    /* No LR adjustment needed - LR already points to next instruction */
}
```

**Trigger mechanism** (precise address via `adr`):
```c
__asm__ volatile (
    "adr %[before], 0f\n"          /* address of the undef instruction below */
    "0:\n"
    ".word 0xe7f000f0\n"           /* ARM undefined instruction pattern */
    : [before] "=r"(undef_instr_addr_before)
    :
    : "memory"
);
```

## Root Cause Fixed: Address Validation Precision

**Symptom:** `undef_addr_valid=0x00000000` even though handler executed correctly.

**Root cause:** Used `mov %0, pc` to capture the PC before the trigger. ARM's pipeline
makes PC read as current instruction + 8, and compiler instruction scheduling placed
the `.word` instruction several instructions away. The 32-byte validation window missed.

**Fix:** Use `adr` with a local label to capture the exact address of the undefined
instruction. `adr` resolves at assembly time to the precise address of the label.

## What Stage88 Completes

The **three core exception types** are now validated at high-VA:

| Exception | Type | Stage | Status |
|-----------|------|-------|--------|
| IRQ | Asynchronous | Stage86 | ✅ |
| Data Abort | Synchronous (MMU fault) | Stage87 | ✅ |
| Undefined Instruction | Synchronous (decode fault) | Stage88 | ✅ |

This is the foundation for full exception handling in a high-VA XNU kernel.

## Verification Commands

```bash
cd stage88 && ./build.sh
sudo fastboot -s 4a2fe00b boot out/stage88/stage88-qcdt.img
sudo adb -s 4a2fe00b shell 'su -c "cat /proc/last_kmsg"' > /tmp/stage88-last_kmsg.txt
grep "high_va_undef_handler_status=0x88000001" /tmp/stage88-last_kmsg.txt
grep "undef_triggered=0x00000001" /tmp/stage88-last_kmsg.txt
grep "kernel_entry returned success" /tmp/stage88-last_kmsg.txt
```
