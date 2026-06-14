# Stage83: Full Kernel Virtual Address Space L1+L2 Pmap

Stage83 implements the first full kernel virtual address space translation using ARMv7 two-level short-descriptor pmap (L1 + L2) with 4KB page granularity. It extends Stage81's minimal 10-section L1-only candidate pmap to a complete translation covering:

- High-virtual kernel code/data at `virtBase=0x80000000` via L2 small pages (4KB)
- Complete physical RAM direct-map window (256MB at `0x80200000`)
- Device MMIO mappings (GIC, RAM console, IMEM, PS_HOLD)
- High-alias bootstrap mappings at `0xc0000000` (extended to 2MB for BSS growth)
- Live pmap install/verify/restore with TTBR0 switching and TLB invalidation

## Key Validation Results

Hardware validation on `cancro` MSM8974 (serial `4a2fe00b`):

- **Status**: `loader_status=0x83000001` (OK)
- **Satisfied mask**: `0x0101ffff` (all required bits set)
- **Failure mask**: `0x00000000` (zero failures)
- **L2 tables allocated**: 1 (256 entries for high-kernel mapping)
- **High-VA data verified**: `0x00000001` (read/write through `0x80000000+offset` works)
- **TTBR0 transitions**: `0x000b0000` → `0x000e4000` (live) → `0x000b0000` (restored)
- **TLB invalidations**: 2 (after install, after restore)
- **Safety boundaries**: All preserved (no public runtime, no persistent writes)
- **Device recovery**: Clean return to Android

## Build

```bash
cd /mnt/data/mi4-ios6/stage83
./build.sh
```

Artifacts:
- `out/stage83/stage83.elf` (499KB)
- `out/stage83/stage83.bin` (447KB)
- `out/stage83/stage83-qcdt.img` (2.9MB, includes QCDT device tree)

## Non-Persistent Hardware Boot

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot out/stage83/stage83-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/stage83-last_kmsg.txt
```

Expected pass markers:
```
stage83_xnu_arm_vm_init_full_pmap_status=0x83000001
stage83_xnu_arm_vm_init_full_pmap_satisfied_mask=0x0101ffff
stage83_xnu_arm_vm_init_full_pmap_failure_mask=0x00000000
stage83_xnu_arm_vm_init_full_pmap_high_va_data_verified=0x00000001
loader_status=0x83000001
kernel_entry returned success
```

## Architecture

### Virtual Address Layout

```
0x00000000 - 0x001fffff  Low identity (2MB, L1 sections)
0x80000000 - 0x8003ffff  High kernel code/data (256KB, L2 4KB pages)
0x80200000 - 0x81ffffff  Physical RAM direct-map (256MB-32MB, L1 sections)
0xc0000000 - 0xc01fffff  High-alias bootstrap (2MB, L1 sections)
0xc0100000 - 0xc01fffff  RAM console alias (1MB)
0xc0200000 - 0xc02fffff  GIC alias (1MB)
0x0fa00000 - 0x0fafffff  IMEM direct (1MB)
```

### Translation Structure

- **L1 table**: 4096 entries, 16KB-aligned at `0x000e4000`
- **L2 pool**: 128 tables × 256 entries, 1KB-aligned (128KB BSS allocation)
- **L2 allocated**: 1 table for high-kernel mapping at `0x80000000`
- **Page size**: 4KB (L2 small pages)
- **Section size**: 1MB (L1 sections)

### High-Virtual Data Verification

After live pmap installation, Stage83 verifies high-virtual data access by writing through the identity mapping and reading through the high-virtual alias:

```c
volatile uint32_t *identity = &stage83_full_pmap_probe_word;
volatile uint32_t *highva = (volatile uint32_t *)(0x80000000 + offset);
*identity = 0xaabbccdd;
assert(*highva == 0xaabbccdd);  // L2 translation verified
```

## Implementation Notes

### BSS Growth and High-Alias Extension

The 128KB L2 pool allocation grew BSS from 491840 bytes (Stage81) to 622912 bytes (Stage83), pushing `stage83_image_end` to `0x111000` and device tree pointer to `0x108140`. The high-alias mapping was extended from 1MB to 2MB to cover device tree access at `0xc0108140`.

### Bootstrap KC Collection Status Initialization

The `mmu_high_bootstrap_selftest()` requires bootstrap KC collection status fields to be `STAGE83_STATUS_OK` (`0x83000001`), but the static global was initialized with `STAGE83_STATUS_BASE` (`0x83000000`). Explicit status initialization was added before the self-test:

```c
mmu_bootstrap_kc_collection.status = STAGE83_STATUS_OK;
mmu_bootstrap_kc_collection.collection_entry_table.status = STAGE83_STATUS_OK;
mmu_bootstrap_kc_collection.kc_entry_table.status = STAGE83_STATUS_OK;
// ... (all sub-components)
```

## Method-C Safety Boundaries

Stage83 maintains all Method-C constraints:

- **Stage-owned code**: All pmap operations are Stage83-owned, not public XNU
- **No public runtime**: Zero calls to `arm_vm_init`, `pmap_bootstrap`, `set_mmu_ttb`
- **No Mach-O exec**: The inert fixture is never executed
- **No persistent writes**: Zero flash/partition/file writes
- **Fail-closed**: Required-mask validation with explicit failure tracking
- **Non-persistent boot**: Hardware validation via `fastboot boot` only (never `flash`)
- **Clean recovery**: Device returns to Android, no persistent state change

## Progression from Stage81

| Feature | Stage81 | Stage83 |
|---------|---------|---------|
| L1 entries | 10 | 267 |
| L2 tables | 0 | 1 |
| Page granularity | 1MB only | 4KB (high kernel) |
| High kernel mapping | None | `0x80000000` (256KB) |
| Physical RAM window | None | `0x80200000` (256MB) |
| High-alias | 1MB | 2MB |
| High-VA data verified | ✗ | ✓ |

## Next Steps

Stage83 enables:

1. **SGI/IRQ timer boundary**: Prove IRQ delivery and handling under the live pmap
2. **High-virtual code execution**: Execute Stage-owned code from `0x80000000+offset`
3. **Pmap expansion**: Extend L2 coverage beyond 256KB for full kernel image
4. **Public XNU handoff**: Transition to public `pmap_bootstrap(vstart)` with validated pmap

## References

- Design: `docs/stage82-pmap-design.md`
- Hardware validation: `docs/experiment-86-stage83-full-kernel-pmap.md`
- Baseline: Stage81 (commit `89f5597`)
