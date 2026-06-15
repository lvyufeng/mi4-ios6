# Stage88: High-Virtual Undefined Instruction Handler

## Goal

Prove that **ARM undefined instruction exception handlers work at high-VA** by
intentionally executing an undefined instruction with VBAR pointing to the high-VA
exception vectors, and verifying the undefined instruction handler executes correctly
from the L2-page-mapped region at `0x80000000+offset`.

## What Stage86-87 Proved

- **Stage86**: Asynchronous exceptions (IRQ) at high-VA ✓
- **Stage87**: Synchronous exceptions (data abort) at high-VA ✓

## What Stage88 Will Prove

Stage88 extends high-VA exception handling to **undefined instruction exceptions**:
- Exception vectors remain at high-VA (reuse Stage86-87 VBAR setup)
- **Undefined instruction handler** executes from high-VA vector (new)
- PC-relative branch from high-VA `vector_undef` to C handler works
- Exception stack access under high-VA candidate L1 works
- Handler can decode the undefined instruction and return gracefully

This completes validation of the three primary synchronous exception types under
high-VA pmap (data abort, prefetch abort, undefined instruction).

## Architecture

### Exception Vector Table (vectors.S)

```asm
stage88_vectors:
    b   vector_reset       @ 0x00: Reset
    b   vector_undef       @ 0x04: Undefined Instruction ← **Stage88 target**
    b   vector_swi         @ 0x08: Software Interrupt (SWI/SVC)
    b   vector_pabt        @ 0x0c: Prefetch Abort
    b   vector_dabt        @ 0x10: Data Abort (Stage87 ✓)
    nop                    @ 0x14: Reserved
    b   vector_irq         @ 0x18: IRQ (Stage86 ✓)
    b   vector_fiq         @ 0x1c: FIQ
```

Currently `vector_undef` hangs. Stage88 will implement a real handler.

### Undefined Instruction Handler Design

**New handler**: `stage88_undef_c_handler()`

**ARM undefined instruction entry convention**:
- LR_und = next instruction address (PC + 4)
- SPSR_und = CPSR at exception time
- Undefined mode stack = separate stack (like IRQ stack)

**Handler algorithm**:
1. Record that undefined instruction was triggered
2. Optionally decode the instruction at LR-4 (the undefined instruction)
3. Mark exception as handled
4. Return via `movs pc, lr` (restore CPSR from SPSR_und and return to next instruction)

### Test Scenario

**Setup**:
1. Re-install candidate L1 (with L2-mapped `0x80000000` region)
2. Set VBAR to high-VA exception vectors (`0x800080a0`)
3. Record pre-exception state

**Trigger undefined instruction**:
4. Execute an undefined instruction (e.g., `.word 0xe7f000f0` - ARM undefined)
5. Undefined instruction exception fires → CPU vectors through high-VA VBAR + 0x04
6. `vector_undef` handler executes from `0x800080a0 + 0x04 = 0x800080a4`
7. Handler does `bl stage88_undef_c_handler` (PC-relative from high-VA)
8. C handler records the exception, returns

**Verify**:
9. Check that exception was handled (no hang)
10. Verify exception count incremented
11. Execution continues after the undefined instruction

**Restore**:
12. Restore original VBAR and TTBR0

## Implementation Plan

### 1. Update `vectors.S` Undefined Instruction Handler

Replace the hang-on-undef stub with a real handler:

```asm
vector_undef:
    /*
     * Undefined Instruction is Stage88's returnable exception handler.
     * ARM undef entry sets LR_und to the next instruction (PC + 4).
     * We return to the next instruction after the undefined opcode.
     */
    stmdb sp!, {r0-r3, r12, lr}
    mov r0, sp              @ Pass stack pointer to C handler
    bl  stage88_undef_c_handler
    ldmia sp!, {r0-r3, r12, lr}
    movs pc, lr             @ Return, restoring CPSR from SPSR_und
```

### 2. Implement `stage88_undef_c_handler` in new file

**New file**: `stage88/xnu_arm_vm_init_high_va_undef_handler.c`

**Key functions**:
```c
/* Global undef handler (called from vector_undef) */
void stage88_undef_c_handler(uint32_t *stack_ptr) {
    uint32_t lr = stack_ptr[5];  /* LR is at offset [5] after r0-r3, r12 */
    uint32_t undef_instr_addr = lr - 4;  /* The undefined instruction address */
    
    /* Record exception in global state */
    g_undef_count++;
    g_undef_last_addr = undef_instr_addr;
    g_undef_handled = 1;
    
    /* Log for debugging (first few exceptions only) */
    if (g_undef_count < 4) {
        log_puts("stage88 undef: addr=");
        log_hex32(undef_instr_addr);
        log_puts(" lr=");
        log_hex32(lr);
        log_puts("\n");
    }
    
    /* No LR adjustment needed - return to next instruction (LR already correct) */
}

/* Stage88 test function */
int stage88_xnu_arm_vm_init_high_va_undef_handler_run(...) {
    /* ... setup: re-install candidate L1, set VBAR to high-VA ... */
    
    /* Reset undef globals */
    g_undef_handled = 0;
    g_undef_count = 0;
    g_undef_last_addr = 0;
    
    /* Trigger undefined instruction exception.
     * Use inline asm to embed a guaranteed-undefined instruction. */
    __asm__ volatile (
        ".word 0xe7f000f0\n"  /* ARM undefined instruction pattern */
        ::: "memory"
    );
    
    /* If we reach here, exception was handled and we continued to next instruction */
    if (g_undef_handled && g_undef_count > 0) {
        r->undef_triggered = 1;
        r->undef_handled = 1;
        r->satisfied_mask |= SAT_UNDEF_TRIGGERED | SAT_UNDEF_HANDLED;
    } else {
        r->failure_mask |= FAIL_UNDEF_NOT_TRIGGERED;
    }
    
    /* ... restore VBAR, TTBR0 ... */
}
```

### 3. Update Result Structure in `stage88.h`

```c
#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_VERSION 1u

#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_FULL_PMAP_OK            0x00000001u
#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_VBAR_SET                0x00000002u
#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_UNDEF_TRIGGERED         0x00000004u
#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_UNDEF_HANDLED           0x00000008u
#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_UNDEF_ADDR_VALID        0x00000010u
#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_VBAR_RESTORED           0x00000020u
#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_REQUIRED_MASK               0x0000003fu

#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_FAIL_FULL_PMAP              0x00000001u
#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_FAIL_UNDEF_NOT_TRIGGERED    0x00000002u
#define STAGE88_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_FAIL_UNDEF_NOT_HANDLED      0x00000004u

struct stage88_xnu_arm_vm_init_high_va_undef_handler_result {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t full_pmap_status;

    /* Vector addresses */
    uint32_t vectors_phys;
    uint32_t vectors_high_va;

    /* VBAR state */
    uint32_t original_vbar;
    uint32_t high_va_vbar_set;
    uint32_t vbar_restored;
    uint32_t restored_vbar;

    /* Undefined instruction verification */
    uint32_t undef_triggered;
    uint32_t undef_handled;
    uint32_t undef_count;
    uint32_t undef_last_addr;
    uint32_t undef_addr_valid;

    uint32_t checksum;
};
```

### 4. Update `xnu_entry_stub.c`

Add call to Stage88 undefined instruction handler window after Stage87 data abort handler.

### 5. Update `build.sh`

Add new source file to SOURCES array:
```bash
xnu_arm_vm_init_high_va_undef_handler.c  # NEW
```

## Expected Hardware Validation Markers

```
stage88_xnu_arm_vm_init_high_va_undef_handler_status=0x88000001
stage88_xnu_arm_vm_init_high_va_undef_handler_satisfied_mask=0x0000003f
stage88_xnu_arm_vm_init_high_va_undef_handler_failure_mask=0x00000000
stage88_xnu_arm_vm_init_high_va_undef_handler_undef_triggered=0x00000001
stage88_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x00000001
stage88_xnu_arm_vm_init_high_va_undef_handler_undef_count > 0
stage88_xnu_arm_vm_init_high_va_undef_handler_undef_addr_valid=0x00000001
loader_status=0x88000001
kernel_entry returned success
```

**Undefined instruction handler invocation log** (expected):
```
MI4IOS6_STAGE88 undef: addr=0x000XXXXX lr=0x000XXXXX
```

## Safety Boundaries

All Stage88 safety invariants (same as Stage86-87):
- ✅ Stage-owned code only
- ✅ No public XNU execution
- ✅ No Mach-O execution
- ✅ No persistent writes
- ✅ Non-persistent boot via `fastboot boot`
- ✅ TTBR0 roundtrip: candidate L1 installed, used, restored
- ✅ VBAR roundtrip: high-VA VBAR set, used, restored
- ✅ **NEW**: Undefined instruction handled gracefully, execution continues
- ✅ Clean device recovery to Android

## What This Completes

The **high-virtual undefined instruction exception handling** milestone. Combined with:
- **Stage86**: IRQ (asynchronous) ✓
- **Stage87**: Data abort (synchronous) ✓
- **Stage88**: Undefined instruction (synchronous) ✓

This validates the core exception types needed for a functional high-VA kernel.
