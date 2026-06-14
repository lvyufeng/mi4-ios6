# Experiment 85: Stage82 Renumbering Validation Baseline

## Summary

Stage82 is a mechanical renumbering of the hardware-validated Stage81 live-pmap install/verify/restore window. It serves as a validation checkpoint to confirm the renumbering infrastructure works correctly before implementing the next major architectural step (Stage83+ full kernel virtual address space with L1+L2 pmap).

Stage82 is functionally identical to Stage81, with all `stage81`/`Stage81`/`STAGE81`/`0x81000001` tokens mechanically replaced by `stage82`/`Stage82`/`STAGE82`/`0x82000001`. This includes source files, build scripts, ABI constants, status codes, and file names.

## Validation Purpose

Confirm that:
1. Renumbering does not introduce compile/link errors
2. Status codes are correctly updated throughout the codebase
3. Hardware validation recovers the same functional behavior with new status codes
4. The Stage81 → Stage82 renumbering pattern is reliable for future stages

## Renumbering Scope

### File Names
- `stage81/stage81.h` → `stage82/stage82.h`
- `stage81/stage81_main.c` → `stage82/stage82_main.c`
- `stage81/targets/cancro.stage81.objects` → `stage82/targets/cancro.stage82.objects`

### Source Content (82 files)
- All `.c`, `.h`, `.S`, `.sh`, `.py`, `.md`, `.mk`, `.ld`, `.objects` files
- Replaced: `stage81` → `stage82`, `Stage81` → `Stage82`, `STAGE81` → `STAGE82`
- Status codes: `0x81000000` → `0x82000000`, `0x81000001` → `0x82000001`
- Build markers: `ST81-` → `ST82-`, `st81dt=0x81` → `st82dt=0x82`
- Boot args: `mi4ios6.stage=81` → `mi4ios6.stage=82`, `mi4ios6=stage81` → `mi4ios6=stage82`

### Build Outputs
- `out/stage81/` → `out/stage82/`
- All artifact paths updated in `build.sh`

## Local Validation

Build command:
```bash
cd /mnt/data/mi4-ios6/stage82
./build.sh
```

Results:
- ✓ Zero undefined symbols in `stage82.elf`
- ✓ Zero undefined symbols in `xnu-link/stage82-xnu-link.elf`
- ✓ Real disassembly calls to all stage82 functions:
  - `bl stage82_xnu_start_stub`
  - `bl stage82_xnu_arm_init_stub`
  - `bl stage82_xnu_early_pmap_platform_init_run`
  - `bl stage82_xnu_pe_init_platform_false_run`
  - `bl stage82_xnu_arm_init_post_pe_bootstrap_run`
  - `bl stage82_xnu_arm_vm_init_live_pmap_run`

Build artifacts:
```
stage82_fixture.macho:   1744 bytes
stage82.elf:           509592 bytes
stage82.bin:           456140 bytes
stage82.img:           458752 bytes
stage82-qcdt.img:     2979840 bytes
```

SHA256:
```
c890f41654e674422d218b389e1f15457545066fb70b07365528f3ad0a34e24f  stage82_fixture.macho
1de24030b97322c1c8bc264aebee270ad6fc9daec549c4eb9afcfa0e1485c75d  stage82.elf
132bff404a88ac0d694d0e2c94109e13ebec4a60df079bfaba1042ccb5bd060d  stage82.bin
5408e17b292b47ef592cce958d774eae655eb29925555956dc7ab79752cc90d9  stage82.img
5277cf8173e6a41bebdb45122c8537420ffcf510b72b9debc85051af118b27e5  stage82-qcdt.img
```

## Hardware Validation

Non-persistent boot on real hardware (`cancro`, serial `4a2fe00b`):

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage82/stage82-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage82-renumbering-validation-last_kmsg.txt
```

Recovered log size:
```
275980 bytes
```

### Key Hardware Validation Markers

**Overall status**:
```
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_status=0x82000001
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_satisfied_mask=0x01007fff
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_failure_mask=0x00000000
MI4IOS6_STAGE82_XNU loader_status=0x82000001
MI4IOS6_STAGE82 kernel_entry returned success
```

**TTBR0 transitions** (identical to Stage81):
```
stage82_xnu_arm_vm_init_live_pmap_original_ttbr0=0x000b0000
stage82_xnu_arm_vm_init_live_pmap_live_ttbr0=0x000e4000
stage82_xnu_arm_vm_init_live_pmap_restored_ttbr0=0x000b0000
```

**Control register preservation**:
```
stage82_xnu_arm_vm_init_live_pmap_restored_ttbcr=0x00000000
stage82_xnu_arm_vm_init_live_pmap_restored_dacr=0x00000003
stage82_xnu_arm_vm_init_live_pmap_restored_sctlr=0x00c5487b
```

**Live pmap operations**:
```
stage82_xnu_arm_vm_init_live_pmap_ttbr0_write_count=0x00000002
stage82_xnu_arm_vm_init_live_pmap_tlb_invalidate_count=0x00000002
stage82_xnu_arm_vm_init_live_pmap_live_pmap_installed=0x00000001
stage82_xnu_arm_vm_init_live_pmap_live_pmap_verified=0x00000001
stage82_xnu_arm_vm_init_live_pmap_live_pmap_restored=0x00000001
```

**Translation verification**:
```
stage82_xnu_arm_vm_init_live_pmap_high_alias_verified=0x00000001
stage82_xnu_arm_vm_init_live_pmap_ram_console_verified=0x00000001
stage82_xnu_arm_vm_init_live_pmap_gic_verified=0x00000001
```

**Safety counters**:
```
stage82_xnu_arm_vm_init_live_pmap_public_arm_vm_init_executed=0x00000000
stage82_xnu_arm_vm_init_live_pmap_public_pmap_runtime_executed=0x00000000
stage82_xnu_arm_vm_init_live_pmap_persistent_write_attempted=0x00000000
stage82_xnu_arm_vm_init_live_pmap_safety_boundary_preserved=0x00000001
```

**Device recovery**: Clean return to Android, no persistent change.

## Functional Equivalence with Stage81

Stage82 hardware validation confirms identical behavior to Stage81 except for status codes:

| Metric | Stage81 | Stage82 |
|--------|---------|---------|
| Status code | `0x81000001` | `0x82000001` |
| Satisfied mask | `0x01007fff` | `0x01007fff` |
| Failure mask | `0x00000000` | `0x00000000` |
| Original TTBR0 | `0x000b0000` | `0x000b0000` |
| Live TTBR0 | `0x000e4000` | `0x000e4000` |
| Restored TTBR0 | `0x000b0000` | `0x000b0000` |
| TTBR0 writes | 2 | 2 |
| TLB invalidations | 2 | 2 |
| High-alias verified | ✓ | ✓ |
| RAM-console verified | ✓ | ✓ |
| GIC verified | ✓ | ✓ |
| Live pmap installed | ✓ | ✓ |
| Live pmap restored | ✓ | ✓ |
| Public runtime executed | ✗ | ✗ |
| Persistent writes | ✗ | ✗ |
| Recovery | Clean | Clean |

## Renumbering Pattern Validation

The Stage81 → Stage82 mechanical renumbering is confirmed successful:

✓ **Compile/link**: No undefined symbols, all references resolved  
✓ **Status codes**: All `0x81` codes correctly updated to `0x82`  
✓ **Function calls**: Disassembly shows real calls to stage82 functions  
✓ **Hardware execution**: Device boots, runs, and recovers with new status codes  
✓ **Functional behavior**: Identical to Stage81 except status code namespace  
✓ **Safety boundaries**: All preserved (no public runtime, no persistent writes)  

## Next Steps

Stage82 establishes a validated baseline for:

1. **Stage83+ full kernel virtual address space pmap**: Extend Stage82's minimal 10-section L1 candidate pmap to a complete L1+L2 two-level translation with:
   - 4KB page granularity via L2 tables
   - High-virtual kernel code/data mapping at `virtBase=0x80000000`
   - Complete physical memory window
   - Device MMIO mappings
   - High-virtual execution verification

2. **Renumbering infrastructure confidence**: The Stage81 → Stage82 pattern is proven reliable for future incremental stages.

3. **Hardware validation workflow**: The non-persistent `fastboot boot` → `/proc/last_kmsg` recovery pattern continues to work across stage boundaries.

## Conclusion

Stage82 successfully validates the mechanical renumbering infrastructure. The Stage81 → Stage82 transition introduced zero functional regressions and correctly updated all status code references. This confirms the renumbering pattern is safe and repeatable for future Method-C stages.
