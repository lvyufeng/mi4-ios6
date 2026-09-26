# Stage86: High-Virtual IRQ Handler Relocation - Implementation Status

## Current State: ACHIEVED (hardware-validated 2026-06-15)

Stage86 **successfully proves ARM exception handlers can execute from L2-page-mapped
high-virtual addresses at 0x80000000+offset** on real MSM8974 (Xiaomi Mi 4 cancro)
hardware via non-persistent `fastboot boot`. Exception vectors relocated to high-VA,
timer IRQ successfully delivered and handled through high-VA vector table. The full
boot completes with `kernel_entry returned success` and every Stage86 marker passes.

## Validated Hardware Markers

```
stage87_xnu_arm_vm_init_full_pmap_high_va_data_verified=0x00000001   # L2 high-VA DATA
stage87_xnu_arm_vm_init_high_va_code_exec_status=0x87000001          # OK
stage87_xnu_arm_vm_init_high_va_code_exec_satisfied_mask=0x0000003f  # all SAT bits
stage87_xnu_arm_vm_init_high_va_code_exec_failure_mask=0x00000000
stage87_xnu_arm_vm_init_high_va_code_exec_fn_phys=0x00047430
stage87_xnu_arm_vm_init_high_va_code_exec_fn_high_va=0x80047430      # L2-mapped kernel alias
stage87_xnu_arm_vm_init_high_va_code_exec_fn_called=0x00000001       # executed via high-VA ptr
stage87_xnu_arm_vm_init_high_va_code_exec_fn_result_correct=0x00000001  # INSTRUCTION FETCH via L2 OK
stage87_xnu_arm_vm_init_high_va_irq_handler_status=0x87000001        # OK
stage87_xnu_arm_vm_init_high_va_irq_handler_satisfied_mask=0x0000003f  # all SAT bits
stage87_xnu_arm_vm_init_high_va_irq_handler_failure_mask=0x00000000
stage87_xnu_arm_vm_init_high_va_irq_handler_vectors_phys=0x000080a0
stage87_xnu_arm_vm_init_high_va_irq_handler_vectors_high_va=0x800080a0  # VBAR → high-VA
stage87_xnu_arm_vm_init_high_va_irq_handler_high_va_vbar_set=0x00000001
stage87_xnu_arm_vm_init_high_va_irq_handler_irq_delivered=0x00000001 # TIMER IRQ via HIGH-VA VECTORS ✓
stage87_xnu_arm_vm_init_high_va_irq_handler_vbar_restored=0x00000001
loader_status=0x87000001
kernel_entry ok
kernel_entry returned success
```

**IRQ Handler Invocations (4 total)**:
```
MI4IOS6_STAGE87 irq handler iar=0x00000000 id=0x00000000 count=0x00000001 timer_count=0x00000000  # SGI selftest
MI4IOS6_STAGE87 irq handler iar=0x00000013 id=0x00000013 count=0x00000001 timer_count=0x00000001  # gic_timer_selftest
MI4IOS6_STAGE87 irq handler iar=0x00000013 id=0x00000013 count=0x00000002 timer_count=0x00000002  # Stage86 low-VA test
MI4IOS6_STAGE87 irq handler iar=0x00000013 id=0x00000013 count=0x00000003 timer_count=0x00000003  # Stage86 HIGH-VA test ✓
```

## What Was Built

### Component 1: `xnu_arm_vm_init_high_va_code_exec.c` (Stage85 milestone)
- Stage-owned test function `stage87_high_va_target` marked `noinline`
- Computes high-VA function pointer: `0x80000000 + fn_phys` (identity-offset L2 mapping)
- Re-installs the Stage-owned candidate L1 (with L2-mapped 0x80000000 region), calls the fn via high-VA pointer, restores TTBR0
- Validates return value to prove execution succeeded
- **Status**: ✅ Validated on hardware, proves L2 code execution at 0x80000000

### Component 2: `xnu_arm_vm_init_high_va_irq_handler.c` (Stage86 milestone, NEW)
- Relocates VBAR to high-VA projection of exception vectors (`0x800080a0`)
- Re-installs candidate L1, sets VBAR to high-VA, arms timer IRQ
- **Diagnostic approach**: Tests low-VA VBAR first, then high-VA VBAR
- Re-enables timer PPI in GIC (gic_timer_selftest disables it after completion)
- Opens IRQ window, waits for timer IRQ, verifies delivery
- Restores original VBAR and TTBR0
- **Status**: ✅ Validated on hardware, proves exception handling at high-VA

**Build Status**: ✓ Compiles successfully, all sources integrated, hardware-validated

### Files Added/Modified for Stage86
1. **NEW**: `xnu_arm_vm_init_high_va_irq_handler.c` (292 lines) — IRQ handler relocation window
2. **Modified**: `stage87.h` (+52 lines for high_va_irq_handler result structure and constants)
3. **Modified**: `xnu_entry_stub.c` (+9 lines for high-VA IRQ handler call)
4. **Modified**: `build.sh` (+1 line in SOURCES array for high_va_irq_handler.c)
5. **Fixed**: `macho_probe.c` (ST85→ST86 marker token update, line 980-984)

## Root Cause Fixed: Timer PPI Disabled After Selftest

**Initial symptom**: Timer IRQ did not fire during Stage86 IRQ window, even with correct
VBAR/TTBR0 setup and timer configuration.

**Root cause**: `gic_timer_selftest` (in `mmu.c`) disables timer PPI in GIC after completion:
```c
// From gic_timer_selftest completion cleanup:
if ((enable_before & (1u << GIC_TIMER_PPI0_ID)) == 0u ||
    (enable_before & (1u << GIC_TIMER_PPI1_ID)) == 0u) {
    mmio_write32(dist_base + GICD_ICENABLER0, GIC_TIMER_PPI_MASK & ~enable_before);
}
```

**Evidence from hardware log**: `gic_timer_isenabler0_restored=0x00007fff` — timer PPI
bits 18/19 are cleared in `GICD_ISENABLER0`.

**Solution**: Re-enable timer PPI before arming timer in Stage86 window:
```c
/* Re-enable timer PPI in GIC (gic_timer_selftest disabled it) */
hvir_mmio_write32(0xf9000000u + GICD_ISENABLER0, GIC_TIMER_PPI_MASK);
hvir_dsb_isb();
```

**Result**: Timer IRQ successfully fires during Stage86 high-VA VBAR window, incrementing
`stage87_timer_irq_count` from 2→3 (proof of high-VA exception handling).

## Diagnostic Approach: Two-Stage IRQ Test

To isolate the root cause, Stage86 implements a two-stage IRQ test:

1. **Low-VA VBAR test**: Keep original VBAR, arm timer, open IRQ window
   - **Purpose**: Verify timer configuration works under candidate L1
   - **Result**: Timer fires successfully (count 1→2)

2. **High-VA VBAR test**: Set VBAR to `0x800080a0`, re-arm timer, open IRQ window
   - **Purpose**: Prove exception vectors work at high-VA
   - **Result**: Timer fires successfully (count 2→3) ✅

This approach confirmed the issue was **not** with high-VA vectors, but with timer PPI
enablement after selftest cleanup.

## What Stage86 Proves

1. ✅ **Exception vector table at high-VA**: VBAR can point to L2-page-mapped address (`0x800080a0`)
2. ✅ **IRQ handler execution from high-VA**: Exception vectors fetched from high-VA, handler runs
3. ✅ **PC-relative branch in exception context**: `bl stage87_irq_c_handler` works from high-VA vector
4. ✅ **IRQ stack access under high-VA pmap**: Register save/restore on IRQ stack works
5. ✅ **L2 page code execution in exception context**: XN=0 small pages executable for exception vectors

This completes **exception-handling validation** under the high-VA kernel pmap.

## Files State

**Build output**: `stage87-qcdt.img` (2.98 MB, SHA256: `2ce8409f4d6d0a11a23f3724513ea90068a81ac5a6ed7f03d8f6eb108ffae4c8`)

**Source status**:
- ✅ `xnu_arm_vm_init_high_va_code_exec.c`: hardware-validated (Stage85 milestone)
- ✅ `xnu_arm_vm_init_high_va_irq_handler.c`: hardware-validated (Stage86 milestone)
- ✅ All safety boundaries preserved (no public XNU, no persistent writes, clean recovery)

## Date: 2026-06-15
