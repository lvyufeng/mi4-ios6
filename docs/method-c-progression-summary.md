# Method-C Staged XNU Bring-Up Progression Summary

## Overview

Method-C is a fail-closed, Stage-owned, incremental approach to XNU kernel bring-up on Xiaomi Mi 4 (cancro) MSM8974 ARMv7 hardware. Each stage proves one additional capability boundary while maintaining strict safety constraints: no public XNU runtime execution, no generated Mach-O execution, no persistent writes, and non-persistent hardware validation only.

All stages boot via `sudo fastboot boot <stage.img>` and recover cleanly to Android via `/proc/last_kmsg` collection. No persistent flash/partition writes occur at any stage.

## Target Hardware

- **Device**: Xiaomi Mi 4 (cancro)
- **SoC**: Qualcomm MSM8974 (Snapdragon 801)
- **Architecture**: ARMv7-A (Cortex-A15-like Krait 400, 4 cores @ 2.5GHz)
- **RAM**: 3GB LPDDR3
- **Bootloader**: Fastboot (unlocked)
- **Validation method**: Non-persistent `fastboot boot`, `/proc/last_kmsg` recovery
- **Serial**: `4a2fe00b`

## Stage Progression

### Stage76: Live XNU Entry Probe (Level 3)
**Commit**: `4c02404`  
**Status**: Hardware-validated ✓

**Proved**:
- Stage-owned `_start` / `arm_init` stub can call public XNU `_start(boot_args)` and return with status
- Entry ABI contract: `start_entered`, `arm_init_called`, `arm_init_returned`, `arm_init_status` at fixed offsets
- Public XNU `_start` executes but does not reach `arm_vm_init` (stopped explicitly after return)

**Boundaries crossed**: Public XNU entry point execution

**Key validation**: `loader_status=0x76000001`, clean Android recovery

---

### Stage77-79: Platform Init Boundaries (Level 3-4)
**Commits**: `ee6f5b9` (Stage79), earlier for 77/78  
**Status**: Hardware-validated ✓

**Stage78: Early Pmap/Platform Init**:
- Stage-owned `arm_init` micro-sequence calls XNU `arm_init()` pexpert/platform bootstrap shape
- Proves early CPU/platform initialization without full `arm_vm_init`

**Stage79: PE_init_platform(FALSE) Handoff**:
- Stage-owned `PE_init_platform(FALSE, args)` call shape
- Proves platform expert pre-VM initialization contract
- Device tree parsing, boot args validation, PE_state capture

**Boundaries crossed**: Platform expert initialization, pre-VM handoff contract

**Key validation**: `stage79_xnu_pe_init_platform_false_status=0x79000001`

---

### Stage80: Post-PE Bootstrap/Timebase Boundary (Level 4)
**Commit**: `8950117`  
**Status**: Hardware-validated ✓

**Proved**:
- Stage-owned `arm_init` path can run early pmap/platform init, `PE_init_platform(FALSE)`, and post-PE bootstrap micro-sequence
- CPU topology discovery, boot CPU identification, timebase registration
- Explicit stop before `arm_vm_init` (the VM/pmap installation boundary)

**Boundaries crossed**: Pre-VM platform bootstrap complete, ready for VM init

**Key validation**: `loader_status=0x80000001`, all post-PE contracts satisfied

---

### Stage81: Live Pmap Installation Window (Level 4-5)
**Commit**: `89f5597`  
**Status**: Hardware-validated ✓

**Proved**:
- First Stage-owned live MMU mutation: TTBR0 write, TLB invalidate
- Stage-owned minimal L1 candidate pmap (10 sections, 1MB granularity)
- Live pmap install/verify/restore window: TTBR0 `0x000b0000→0x000e4000→0x000b0000`
- High-alias mapping verification at `0xc0000000`
- Control register preservation: TTBCR/DACR/SCTLR unchanged
- Fail-closed: restore original TTBR0 before returning to loader

**Boundaries crossed**: Live MMU control, TTBR0 switching, TLB invalidation

**Key validation**:
- `stage81_xnu_arm_vm_init_live_pmap_status=0x81000001`
- `ttbr0_write_count=0x00000002` (install + restore)
- `tlb_invalidate_count=0x00000002`
- `live_pmap_installed=0x00000001`, `live_pmap_restored=0x00000001`

---

### Stage82: Renumbering Validation Baseline (Level 4-5)
**Commit**: `8abe158` (part 1)  
**Status**: Hardware-validated ✓

**Proved**:
- Mechanical renumbering (Stage81 → Stage82, `0x81→0x82`) introduces zero functional regressions
- Renumbering infrastructure is reliable for future stages
- Stage82 behavior identical to Stage81 except status code namespace

**Purpose**: Validation checkpoint before major architectural step (Stage83)

**Key validation**: `loader_status=0x82000001`, satisfied/failure masks identical to Stage81

---

### Stage83: Full Kernel Virtual Address Space L1+L2 Pmap (Level 5)
**Commit**: `8abe158` (part 2)  
**Status**: Hardware-validated ✓ **← CURRENT**

**Proved**:
- ARMv7 two-level short-descriptor pmap: L1 (4096 entries, 16KB-aligned) + L2 (256 entries, 1KB-aligned, 4KB pages)
- High-virtual kernel code/data mapping at `virtBase=0x80000000` via L2 small pages (256KB coverage)
- Complete physical RAM direct-map window (256MB at `0x80200000`, 1MB L1 sections)
- Device MMIO mappings: GIC, RAM console, IMEM, PS_HOLD
- High-alias extended to 2MB (covers BSS growth from 128KB L2 pool)
- High-virtual data verification: read/write through `0x80000000+offset` works
- L2 page table allocation and descriptor population
- TLB invalidation for L1+L2 translation updates

**Boundaries crossed**: Two-level translation, 4KB page granularity, high-virtual data access

**Key validation**:
- `stage83_xnu_arm_vm_init_full_pmap_status=0x83000001`
- `satisfied_mask=0x0101ffff` (all required bits)
- `failure_mask=0x00000000`
- `l2_tables_allocated=0x00000001`
- `high_va_data_verified=0x00000001`
- `live_pmap_installed=0x00000001`, `live_pmap_restored=0x00000001`

**Implementation challenges**:
1. High-alias overflow: BSS +131KB → device tree at `0x108140` beyond 1MB alias → extended to 2MB
2. Bootstrap KC collection status validation: static BASE status → explicit OK initialization

---

## Method-C Safety Boundaries (All Stages)

✓ **Stage-owned execution**: All MMU/pmap/IRQ code is Stage-owned, not public XNU  
✓ **No public XNU runtime**: Zero calls to public `arm_vm_init`, `pmap_bootstrap`, `set_mmu_ttb`, `flush_mmu_tlb`, `PE_init_clock`, `rtclock_intr`  
✓ **No generated Mach-O execution**: Inert Mach-O fixture is never executed  
✓ **No persistent writes**: Zero flash/erase/partition writes, zero persistent file writes  
✓ **No external mutation**: XNU checkouts (`external/xnu-*`) remain clean and detached  
✓ **Fail-closed**: All validation is required-mask driven with explicit failure tracking  
✓ **Non-persistent boot**: Hardware validation via `sudo fastboot boot` only, never `fastboot flash`  
✓ **Clean recovery**: Device returns to Android after `kernel_entry`, no persistent state change  

## Progression Metrics

| Stage | Level | Status Code | L1 Entries | L2 Tables | Page Size | High-VA Mapping | IRQ Enabled | Commit |
|-------|-------|-------------|------------|-----------|-----------|-----------------|-------------|--------|
| 76 | 3 | 0x76000001 | 0 | 0 | - | ✗ | ✗ | 4c02404 |
| 77-78 | 3-4 | 0x78000001 | 0 | 0 | - | ✗ | ✗ | (earlier) |
| 79 | 4 | 0x79000001 | 0 | 0 | - | ✗ | ✗ | ee6f5b9 |
| 80 | 4 | 0x80000001 | 0 | 0 | - | ✗ | ✗ | 8950117 |
| 81 | 4-5 | 0x81000001 | 10 | 0 | 1MB | ✗ (1MB alias) | ✗ | 89f5597 |
| 82 | 4-5 | 0x82000001 | 10 | 0 | 1MB | ✗ (1MB alias) | ✗ | 8abe158 |
| **83** | **5** | **0x83000001** | **267** | **1** | **4KB** | **✓ (0x80000000)** | **✗** | **8abe158** |

## Next Boundaries

### Stage84+: SGI/IRQ Timer Boundary (Level 5-6)
**Planning**: `docs/stage84-sgi-irq-timer-planning.md`  
**Status**: Not started

**Goals**:
- GIC initialization for interrupt delivery (distributor + CPU interface)
- Stage-owned IRQ vector handler installation (vector table offset `0x18`)
- ARM generic timer or QTimer configuration for periodic interrupts
- IRQ enable under live pmap (`cpsie i`)
- Verify IRQ handler fires, services interrupt, and returns correctly
- IRQ disable and original state restore before loader return

**Boundaries to cross**: Interrupt delivery, Stage-owned IRQ handling under live pmap

**Key proof**: `stage84_irq_count_after > stage84_irq_count_before`, IRQs fire and are handled

---

### Stage85+: High-Virtual Code Execution (Level 6)
**Status**: Future

**Goals**:
- Prove Stage-owned code can execute from `0x80000000+offset` (not just data access)
- Jump to high-virtual function pointer, execute, and return
- Verify PC-relative addressing and branch instructions work under L2 translation

**Boundaries to cross**: Instruction fetch via high-virtual L2 translation

---

### Stage86+: Pmap Expansion (Level 6)
**Status**: Future

**Goals**:
- Extend L2 coverage beyond 256KB to map full kernel image
- Allocate additional L2 tables for heap, IPC, or device windows
- Prove L2 table allocation and descriptor population scales

---

### Stage87+: Public XNU Pmap Handoff (Level 7)
**Status**: Future

**Goals**:
- Transition from Stage-owned pmap to public XNU `pmap_bootstrap(vstart)`
- Pass Stage83-validated candidate pmap to public XNU
- Prove handoff contract preserves MMU state and allows public runtime to continue

**Boundaries to cross**: Stage-owned → public XNU pmap ownership transition

---

## Repository Structure

```
mi4-ios6/
├── stage76/          # Live XNU entry probe
├── stage77-79/       # Platform init boundaries (not committed separately)
├── stage80/          # Post-PE bootstrap boundary
├── stage81/          # Live pmap installation window
├── stage82/          # Renumbering validation
├── stage83/          # Full kernel L1+L2 pmap (CURRENT)
├── docs/
│   ├── experiment-84-stage81-arm-vm-init-live-pmap.md
│   ├── experiment-85-stage82-renumbering-validation.md
│   ├── experiment-86-stage83-full-kernel-pmap.md
│   ├── stage82-pmap-design.md
│   ├── stage82-next-step-analysis.md
│   ├── stage84-sgi-irq-timer-planning.md
│   └── method-c-progression-summary.md (this file)
├── external/
│   ├── xnu-4570.1.46/         # XNU 12.3.0 reference (iOS 6 era)
│   └── xnu-upstream/          # Darwin 12 baseline (read-only)
└── tools/
    ├── mkbootimg_v0_qcdt.py   # Android boot image packer with QCDT
    └── parse_android_bootimg.py
```

## Key Design Principles

1. **Incremental boundaries**: Each stage proves one additional capability before moving to the next
2. **Fail-closed validation**: Required-mask driven, explicit failure tracking, zero tolerance for undefined state
3. **Stage-owned code**: All MMU/pmap/IRQ/timer code is Stage-owned until explicit handoff to public XNU
4. **Non-persistent validation**: Hardware boots via `fastboot boot`, recovers via `/proc/last_kmsg`, never `fastboot flash`
5. **Safety boundaries**: No public runtime, no Mach-O execution, no persistent writes, no external mutation
6. **Hardware ground truth**: Every stage is hardware-validated on real MSM8974 before commit
7. **Mechanical renumbering**: Stage numbering can advance via mechanical renumbering (e.g., Stage82) to validate infrastructure

## References

- **XNU baseline**: `external/xnu-4570.1.46` (Darwin 12, iOS 6 era, commit `cc8a9b0c`)
- **ARMv7 architecture**: ARM Architecture Reference Manual ARMv7-A/R edition
- **MSM8974 documentation**: Qualcomm Snapdragon 801 technical reference (limited public availability)
- **GIC specification**: ARM Generic Interrupt Controller Architecture Specification v2.0
- **Device tree**: `xiaomi4-cancro-backup-20260604-112053/boot-unpacked/dt.img`

## Conclusion

Method-C Stage83 proves that a full ARMv7 L1+L2 two-level pmap with 4KB page granularity, high-virtual kernel mapping at `0x80000000`, and complete physical/device address space works on real MSM8974 hardware. High-virtual data access is verified. The next major boundary is IRQ delivery and handling (Stage84+).

All stages maintain strict fail-closed, Stage-owned, non-persistent safety boundaries. Device recovery remains clean across all stages. The progression demonstrates incremental, hardware-validated XNU kernel bring-up on Xiaomi Mi 4 without persistent flash writes or public XNU runtime dependency until explicit handoff.

**Current state**: Stage83 committed (`8abe158`), pushed to `origin/stage8-sgi-irq`.
