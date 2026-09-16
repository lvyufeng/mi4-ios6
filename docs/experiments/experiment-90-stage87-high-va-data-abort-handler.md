# Experiment 90: Stage87 — High-VA Data Abort Handler

**Date:** 2026-06-15  
**Device:** Xiaomi Mi 4 cancro (MSM8974)  
**Boot method:** Non-persistent `fastboot boot` (no flash, clean recovery to Android)  
**Status:** ✅ **ACHIEVED** — `kernel_entry returned success`

## Goal

Prove that ARM synchronous exception handlers (data aborts) can execute from the
**L2-page-mapped high-virtual kernel address** at `0x80000000+offset` by intentionally
triggering a data abort with VBAR pointing to high-VA exception vectors.

Stage86 proved asynchronous exceptions (IRQ) at high-VA. Stage87 extends this to
**synchronous exceptions** — specifically MMU data abort faults.

## Result

The high-VA data abort handler runs to completion and correctly handles the fault:

```
stage87_xnu_arm_vm_init_high_va_data_abort_handler_status=0x87000001
stage87_xnu_arm_vm_init_high_va_data_abort_handler_required_mask=0x0000007f
stage87_xnu_arm_vm_init_high_va_data_abort_handler_satisfied_mask=0x0000007f
stage87_xnu_arm_vm_init_high_va_data_abort_handler_failure_mask=0x00000000
stage87_xnu_arm_vm_init_high_va_data_abort_handler_abort_triggered=0x00000001
stage87_xnu_arm_vm_init_high_va_data_abort_handler_abort_handled=0x00000001
stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfar=0xdeadc000
stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfar_expected=0xdeadc000
stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfar_correct=0x00000001
stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfsr=0x00000005
stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfsr_valid=0x00000001
loader_status=0x87000001
kernel_entry ok
kernel_entry returned success
```

**Data abort handler log:**
```
MI4IOS6_STAGE87 data abort: dfar=0xdeadc000 dfsr=0x00000005 lr=0x00048084
```

- **DFAR=0xdeadc000**: Faulting virtual address (our test address)
- **DFSR=0x00000005**: Translation fault (unmapped page, bits[3:0]=0b0101)
- **LR=0x00048084**: Address of faulting instruction

The handler successfully:
1. Executed from high-VA (`0x800080b0` = VBAR + 0x10 data abort offset)
2. Read fault status registers (DFSR/DFAR)
3. Identified the fault as a translation fault
4. Adjusted LR to skip the faulting instruction
5. Returned, allowing execution to continue

## Implementation

### 1. Updated `vectors.S`

Replaced the hang-on-abort stub with a returnable handler:

```asm
vector_data_abort:
    /*
     * Data Abort is Stage87's returnable synchronous exception handler.
     * ARM data abort entry sets LR_abt to the faulting PC + 8; adjust to
     * faulting instruction address. The C handler will read DFSR/DFAR and
     * may adjust LR on the stack to skip the faulting instruction.
     */
    sub lr, lr, #8              @ Adjust to faulting instruction address
    stmdb sp!, {r0-r3, r12, lr} @ Save volatile regs and adjusted LR
    mov r0, sp                  @ Pass stack pointer to C handler
    bl  stage87_data_abort_c_handler
    ldmia sp!, {r0-r3, r12, lr} @ Restore (possibly modified LR)
    subs pc, lr, #0             @ Return, restoring CPSR from SPSR_abt
```

### 2. Created `xnu_arm_vm_init_high_va_data_abort_handler.c`

**`stage87_data_abort_c_handler(uint32_t *stack_ptr)`**: Global abort handler

```c
void stage87_data_abort_c_handler(uint32_t *stack_ptr)
{
    uint32_t dfsr = hvda_read_dfsr();  /* Data Fault Status Register */
    uint32_t dfar = hvda_read_dfar();  /* Data Fault Address Register */
    uint32_t lr = stack_ptr[5];        /* LR is at offset [5] after r0-r3, r12 */

    /* Record abort in global state */
    g_abort_dfsr = dfsr;
    g_abort_dfar = dfar;
    g_abort_handled = 1;

    /* Log for debugging */
    log_puts("MI4IOS6_STAGE87 data abort: dfar=");
    log_hex32(dfar);
    log_puts(" dfsr=");
    log_hex32(dfsr);
    log_puts(" lr=");
    log_hex32(lr);
    log_puts("\n");

    /* Skip the faulting instruction by advancing LR by 4 bytes */
    stack_ptr[5] = lr + 4;
}
```

**`stage87_xnu_arm_vm_init_high_va_data_abort_handler_run()`**: Test function

```c
int stage87_xnu_arm_vm_init_high_va_data_abort_handler_run(...)
{
    volatile uint32_t *bad_ptr = (volatile uint32_t *)0xdeadc000;  /* 4-byte aligned */
    uint32_t dummy;

    /* Re-install candidate L1 (with L2-mapped 0x80000000 region) */
    hvda_write_ttbr0(candidate_l1_base);
    hvda_invalidate_tlbs();

    /* Set VBAR to high-VA exception vectors */
    hvda_write_vbar(vectors_high_va);  /* 0x800080a0 */

    /* Reset abort globals */
    g_abort_handled = 0;
    g_abort_dfsr = 0;
    g_abort_dfar = 0;

    /* Trigger data abort by accessing unmapped address */
    dummy = *bad_ptr;  /* This will vector through high-VA VBAR + 0x10 */

    /* If we reach here, abort was handled and we skipped the faulting instruction */
    if (g_abort_handled && g_abort_dfar == 0xdeadc000) {
        /* Success */
    }

    /* Restore original VBAR and TTBR0 */
    hvda_write_vbar(original_vbar);
    hvda_write_ttbr0(original_ttbr0);
    hvda_invalidate_tlbs();

    return 0;
}
```

### 3. ARMv7 Fault Registers

**DFSR (Data Fault Status Register, CP15 c5, c0, 0)**:
- Bits[3:0]: Fault status
  - `0b0001` = Alignment fault
  - `0b0101` = Translation fault, section
  - `0b0111` = Translation fault, page

**DFAR (Data Fault Address Register, CP15 c6, c0, 0)**:
- Faulting virtual address

**Read via inline asm**:
```c
static inline uint32_t hvda_read_dfsr(void) {
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t hvda_read_dfar(void) {
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(v));
    return v;
}
```

## Two Root Causes Fixed

### 1. Macho Marker Validation (macho_probe.c)

**Symptom:** `loader preflight failed`, contracts cascaded failure.

**Root cause:** `macho_probe.c` expected section prefixes `"ST86-TEXT"`, but the
Stage87 Mach-O fixture embeds `ST87-*` markers. The token replacement
(`stage86→stage87`) updated the fixture but not the validation code in
`macho_probe.c:980-984`.

**Fix:** Update marker prefixes to `"ST87-TEXT"`, `"ST87-DATA"`, `"ST87-PRELINK-TEXT"`.

### 2. Unaligned Test Address

**Symptom:** `dfsr=0x00000001` (alignment fault), not translation fault.

**Root cause:** Test address `0xdeadbeef` is not 4-byte aligned. ARMv7 load
instructions require natural alignment; unaligned accesses trigger alignment
faults **before** MMU translation, so we never see the translation fault.

**Fix:** Use aligned unmapped address `0xdeadc000`. Result: `dfsr=0x00000005`
(translation fault, as expected).

## Safety Boundaries Preserved

All Stage87 safety invariants hold:
- ✅ Stage-owned code only
- ✅ No public XNU execution
- ✅ No Mach-O execution
- ✅ No persistent writes
- ✅ Non-persistent boot via `fastboot boot`
- ✅ TTBR0 roundtrip: candidate L1 installed, used, restored
- ✅ VBAR roundtrip: high-VA VBAR set, used, restored
- ✅ Data abort handled gracefully, execution continues
- ✅ Clean device recovery to Android (`boot_completed=1`)

## How the Test Works

1. kernel_entry → MMU/IRQ/TTBR0 selftests (all pass)
2. → loader preflight → 20 dryrun contracts (all OK)
3. → entry stub:
   a. early_pmap_platform_init, pe_init_platform_false, post_pe_bootstrap
   b. arm_vm_init_full_pmap: install candidate L1, verify L2 high-VA data
   c. arm_vm_init_high_va_code_exec: prove code execution at high-VA
   d. arm_vm_init_high_va_irq_handler: prove IRQ handling at high-VA (Stage86)
   e. **arm_vm_init_high_va_data_abort_handler: trigger abort, handle, verify** (Stage87 NEW)
4. → preflight ok, kernel_entry ok, kernel_entry returned success

## Verification Commands

```bash
# Build
cd stage87 && ./build.sh

# Non-persistent boot
sudo fastboot -s 4a2fe00b boot out/stage87/stage87-qcdt.img

# Capture log
sudo adb -s 4a2fe00b shell 'su -c "cat /proc/last_kmsg"' > /tmp/stage87-last_kmsg.txt

# Validate
grep "high_va_data_abort_handler_status=0x87000001" /tmp/stage87-last_kmsg.txt
grep "abort_triggered=0x00000001" /tmp/stage87-last_kmsg.txt
grep "abort_handled=0x00000001" /tmp/stage87-last_kmsg.txt
grep "dfar_correct=0x00000001" /tmp/stage87-last_kmsg.txt
grep "dfsr_valid=0x00000001" /tmp/stage87-last_kmsg.txt
grep "kernel_entry returned success" /tmp/stage87-last_kmsg.txt
```

## What This Completes

The **high-virtual synchronous exception handling** milestone. Combined with
Stage86 (asynchronous/IRQ exceptions), Stage87 proves:

- ✅ **Asynchronous exceptions** (IRQ) at high-VA (Stage86)
- ✅ **Synchronous exceptions** (data abort) at high-VA (Stage87)
- ✅ **Fault register access** in high-VA exception context
- ✅ **Exception return with PC adjustment** (skip faulting instruction)
- ✅ **MMU fault handling** under high-VA kernel pmap

This is the foundation for full exception handling in a high-VA XNU kernel.

## Next Steps

With high-VA exception handling complete (both async and sync), the next
milestones could include:
1. **Stage88**: High-VA undefined instruction handler
2. **Stage89**: High-VA prefetch abort handler
3. **Stage90**: Full high-VA exception vector table (all 8 vectors)
4. **Stage91**: High-VA kernel bootstrap (real XNU `_start` at high-VA)
