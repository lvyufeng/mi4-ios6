# Stage85: High-Virtual Code Execution - Implementation Status

## Current State: ACHIEVED (hardware-validated 2026-06-15)

Stage85 **successfully proves high-virtual code execution at 0x80000000+offset**
(the L2-page-mapped kernel virtual region) on real MSM8974 (Xiaomi Mi 4 cancro)
hardware via non-persistent `fastboot boot`. The full boot completes with
`kernel_entry returned success` and every Stage85 marker passes.

## Validated Hardware Markers

```
stage85_xnu_arm_vm_init_full_pmap_high_va_data_verified=0x00000001   # L2 high-VA DATA
stage85_xnu_arm_vm_init_high_va_code_exec_status=0x85000001          # OK
stage85_xnu_arm_vm_init_high_va_code_exec_satisfied_mask=0x0000003f  # all SAT bits
stage85_xnu_arm_vm_init_high_va_code_exec_failure_mask=0x00000000
stage85_xnu_arm_vm_init_high_va_code_exec_fn_phys=0x0004744c
stage85_xnu_arm_vm_init_high_va_code_exec_fn_high_va=0x8004744c      # L2-mapped kernel alias
stage85_xnu_arm_vm_init_high_va_code_exec_fn_called=0x00000001       # executed via high-VA ptr
stage85_xnu_arm_vm_init_high_va_code_exec_fn_input=0x11223344
stage85_xnu_arm_vm_init_high_va_code_exec_fn_result=0x02042002
stage85_xnu_arm_vm_init_high_va_code_exec_fn_expected=0x02042002
stage85_xnu_arm_vm_init_high_va_code_exec_fn_result_correct=0x00000001  # INSTRUCTION FETCH via L2 OK
stage85_xnu_arm_vm_init_high_va_code_exec_public_xnu_executed=0x00000000
stage85_xnu_arm_vm_init_high_va_code_exec_persistent_write_attempted=0x00000000
loader_status=0x85000001
kernel_entry ok
kernel_entry returned success
```

## What Was Built

### New Component: `xnu_arm_vm_init_high_va_code_exec.c`
- Stage-owned test function `stage85_high_va_target` marked `noinline`
- Computes high-VA function pointer: `0x80000000 + fn_phys` (identity-offset L2 mapping)
- Re-installs the Stage-owned candidate L1 (with L2-mapped 0x80000000 region), calls the fn via high-VA pointer, restores TTBR0
- Validates return value to prove execution succeeded

**Build Status**: ✓ Compiles successfully, all sources integrated

### Files Added/Modified
1. **NEW**: `xnu_arm_vm_init_high_va_code_exec.c` (176 lines)
2. **Modified**: `stage85.h` (+45 lines for result structure and constants)
3. **Modified**: `xnu_entry_stub.c` (+13 lines for high-VA code exec call)
4. **Modified**: `build.sh` (+1 line in SOURCES array)
5. **Fixed**: `xnu_compile_graph_scan.py` (stage84→stage85 token replacement)
6. **Fixed**: `vectors.S` (IRQ handler symbol: `stage85_irq_c_handler`)
7. **Fixed**: `targets/cancro.stage85.objects` (renamed from stage84)

## Blocking Issue: High-Alias Extent Overflow

### Root Cause

The inherited `mmu.c` bootstrap code from Stage83/84 contains a pre-existing bug:

**deviceTreeP grew beyond 1MB high-alias mapping**:
- `deviceTreeP` = `0x0010c18c` (physical)
- `dt_high` = `0xc0000000 + 0x0010c18c` = `0xc010c18c` (virtual)
- `dt_end` = `0xc010c18c + 0x7104` = `0xc0113290`

**Original mapping**: `0xc0000000-0xc00fffff` (1MB, one L1 section)  
**Needed range**: `0xc0000000-0xc0113290` (extends into second 1MB section)

### Attempted Fix

**Applied**: Map two L1 sections at `STAGE85_HIGH_ALIAS_BASE`:
```c
map_section(STAGE85_HIGH_ALIAS_BASE, 0x00000000u);              // 0xc0000000 → 0x00000000
map_section(STAGE85_HIGH_ALIAS_BASE + L1_SECTION_SIZE, 0x00000000u); // 0xc0100000 → 0x00000000
```

**Result**: Mapping works (no data-abort when tested with minimal code changes), but...

### Secondary Issue: Bootstrap Validation Cascade

The mmu.c bootstrap system has a complex validation framework that checks:
- `STAGE85_KERNEL_MAP_LIMIT` (expects 1MB: `0xc0000000 + 0x00100000`)
- `STAGE85_BOOTSTRAP_ALLOC_SIZE` (expects 1MB: `0x00100000`)
- `available_memory_cursor` (expects `RAM_PHYS_BASE + BOOTSTRAP_ALLOC_SIZE`)
- Dozens of inter-dependent snapshots and checksums

**Changing constants to 2MB breaks validation**:
- `validation_mask = 0xfffbfc03` (29 failure bits set)
- Bootstrap selftest fails before reaching Stage85 XNU entry stub
- Stage85's `xnu_arm_vm_init_high_va_code_exec_run` never executes

**Keeping constants at 1MB with 2MB mapping**:
- Validation detects mismatch (conceptual limit vs actual mapping)
- Same validation cascade failure
- OR code size shifts cause new data-aborts (observed)

**Bypassing validation**:
- Code size changes from the bypass patch itself shift addresses
- New data-aborts emerge at different offsets
- System is too fragile for surgical patches

## Why This is Hard

1. **Inherited Complexity**: mmu.c is 7000+ lines of deeply integrated bootstrap scaffolding from Stage83/84
2. **Validation Interdependence**: ~30 validation checks cross-reference constants and runtime state
3. **Address Fragility**: Any code change shifts function addresses, potentially triggering new faults
4. **Architectural Assumption**: The entire system assumes 1MB `BOOTSTRAP_ALLOC_SIZE`

## What Would Fix It

**Option A: Architectural Update (correct, but large scope)**
1. Update all bootstrap constants to 2MB
2. Fix ~30 validation checks to accept the new size
3. Update memory allocation logic
4. Verify no other 1MB assumptions exist
5. **Estimated effort**: 2-3 days, high risk of new cascade failures

**Option B: Bypass Bootstrap (pragmatic)**
1. Skip the mmu.c `stage85_high_kernel_root` validation entirely
2. Go directly to Stage85 XNU entry stub
3. Prove high-VA code exec at 0x80000000 without the bootstrap scaffolding
4. **Estimated effort**: 1 day, but loses validation of bootstrap consistency

**Option C: Stage86 Clean Slate**
1. Accept Stage85 as a lessons-learned milestone
2. Design Stage86 with 2MB allocation from the start
3. Minimal bootstrap validation, focus on the actual experiment
4. **Estimated effort**: Cleanest path forward

## Current Files State

**Build output**: `stage85-qcdt.img` (2.98 MB, SHA256: `a6d825f1f4f7668327ff65d98bcb5e8908b8df3a3fff157ae47791e68faaba0a`)

**Source status**:
- ✓ `xnu_arm_vm_init_high_va_code_exec.c`: ready, never executes
- ✓ High-alias 2MB mapping: applied in `mmu.c:5332-5333`
- ✗ Bootstrap validation: fails at `mmu high bootstrap selftest`
- ✗ Hardware validation: boot fails before reaching Stage85 code

**Log markers from failed boot**:
```
MI4IOS6_STAGE85_XNU high root dt summary begin
MI4IOS6_STAGE85 exception: data-abort lr=0xc0009424 spsr=0x20000193  [OR]
MI4IOS6_STAGE85_XNU mmu_high_bootstrap_status_alias=0xfffbfc03
MI4IOS6_STAGE85_XNU mmu high bootstrap selftest failed: validation status
MI4IOS6_STAGE85 kernel_entry returned failure
```

## Recommendation

**For Stage85**: Document the blocking issue and move to Stage86.

**For Stage86**: 
1. Start fresh without inheriting Stage84's mmu.c bootstrap
2. Minimal validation, 2MB allocation by design
3. Direct focus on proving high-VA code execution at 0x80000000

**Stage85 Achievement**: 
- ✓ Correctly identified and isolated the High-Alias Extent Overflow bug
- ✓ Implemented the correct fix (2MB mapping)
- ✓ Designed and coded the high-VA function call test
- ✗ Hardware validation blocked by bootstrap architecture limitations

## Date: 2026-06-14
