# Experiment 92: Stage89 — Mach-O Kernel Loader

**Date:** 2026-06-15  
**Device:** Xiaomi Mi 4 cancro (MSM8974)  
**Boot method:** Non-persistent `fastboot boot` (no flash, clean recovery to Android)  
**Status:** ✅ **ACHIEVED** — `kernel_entry returned success`

## Goal

Implement a **Mach-O kernel loader** that parses a Mach-O binary, loads its
segments into high-VA memory at 0x80000000, and prepares to transfer control
to XNU's entry point.

This is the **critical transition** from "simulating XNU boot" to "executing
real XNU code."

## Result

```
stage89_xnu_macho_loader_status=0x89000001
stage89_xnu_macho_loader_segments_loaded=0x00000006
stage89_xnu_macho_loader_text_segment_va=0x80008000
stage89_xnu_macho_loader_data_segment_va=0x8000a000
stage89_xnu_macho_loader_linkedit_segment_va=0x8000c000
stage89_xnu_macho_loader_xnu_entry_va=0x80008000
stage89_xnu_macho_loader_ready_for_handoff=0x00000001
loader_status=0x89000001
kernel_entry returned success
```

All 6 Mach-O segments were successfully parsed and loaded to high-VA memory.

## Implementation

### Phase 1: Dry-Run Mode (Safety First)

Initial implementation used **dry-run mode**:
- Parsed all Mach-O structures
- Validated all segment addresses
- Logged intended operations
- **Did NOT execute memcpy/memset**

**Result**: Dry-run validation passed with all addresses valid.

### Phase 2: Actual Loading (Enabled After Validation)

After dry-run success, enabled actual memory operations:
- `memcpy()` from Mach-O file to high-VA
- `memset()` for BSS zero-fill

**Result**: All segments loaded successfully, no crashes.

## Segments Loaded

| Segment | VA Start | Size | Content |
|---------|----------|------|---------|
| __TEXT | 0x80008000 | 8KB | Executable code |
| __DATA | 0x8000a000 | 8KB | Read-write data |
| __LINKEDIT | 0x8000c000 | 4KB | Symbol table |
| __PRELINK_TEXT | 0x8000d000 | 4KB | Kext code |
| __PRELINK_INFO | 0x8000e000 | 8KB | Kext metadata |
| __PRELINK_STATE | 0x80010000 | 4KB | Kext state |

All segments fit within the candidate L1 mapping (0x80000000-0x80100000).

## Key Features

### Address Validation

Every segment address is validated before loading:
```c
if (seg->vmaddr < 0x80000000 || seg->vmaddr >= 0x80100000) {
    // Skip segment, log warning
    return 0;
}
```

This prevents data aborts from accessing unmapped memory.

### Graceful Degradation

Segments outside the mapped range are **skipped, not failed**. The loader
continues with available segments and succeeds as long as __TEXT is loaded.

### Detailed Logging

Every operation is logged:
- Mach-O base address and size
- Each segment's vmaddr, vmsize, fileoff, filesize
- Copy/zero operations
- Entry point resolution

## Root Causes Fixed

### Issue 1: Device Hang on First Test

**Symptom**: Device hung after first Stage89 boot (before dry-run was added).

**Root cause**: Attempted memcpy to address outside L1 mapping.

**Fix**: Added strict address validation + dry-run mode for safe testing.

### Issue 2: Missing Logging Functions

**Symptom**: Compilation errors for `xnu_log_hex32()` and `strncmp()`.

**Root cause**: These functions were not available in the bare-metal environment.

**Fix**: 
- Replaced `xnu_log_hex32()` with `xnu_log_kv32()`
- Implemented custom `seg_name_eq()` for string comparison

## What This Achieves

Stage89 proves:
1. ✅ **Mach-O parsing works** - Headers and load commands correctly read
2. ✅ **Segment loading works** - All 6 segments loaded to high-VA
3. ✅ **Address validation works** - No crashes from invalid addresses
4. ✅ **Entry point resolution works** - XNU entry VA correctly computed
5. ✅ **Ready for XNU handoff** - All prerequisites met

## What's Next: XNU Handoff (Stage90?)

With segments loaded, the next step is to **transfer control to XNU**:

1. **Set up environment**:
   - Ensure candidate L1 is installed (already done)
   - Set r0 = boot_args pointer
   - Set SP to valid stack
   - Set VBAR (already at high-VA)

2. **Jump to XNU entry point**:
   ```c
   typedef void (*xnu_entry_t)(struct boot_args *);
   xnu_entry_t xnu_start = (xnu_entry_t)0x80008000;
   xnu_start(args);
   ```

3. **Expect immediate crash** (this is normal):
   - XNU will expect services we don't provide yet
   - Catch the crash address and registers
   - Identify what XNU tried to call
   - Iterate on adding minimal stubs

## Safety Boundaries Preserved

- ✅ Stage-owned code only (no XNU execution yet)
- ✅ Non-persistent boot (fastboot boot)
- ✅ No public XNU runtime invoked
- ✅ No persistent writes
- ✅ Dry-run validation before actual loading
- ✅ Address range validation

## Verification Commands

```bash
cd /mnt/data/mi4-ios6
adb reboot bootloader
sudo fastboot -s 4a2fe00b boot out/stage89/stage89-qcdt.img
adb wait-for-device && sleep 5
adb shell 'su -c "cat /proc/last_kmsg"' > /tmp/stage89.txt
grep "stage89_xnu_macho_loader" /tmp/stage89.txt
```

## Related Stages

- **Stage86**: High-VA IRQ handling ✓
- **Stage87**: High-VA data abort handling ✓
- **Stage88**: High-VA undefined instruction handling ✓
- **Stage89**: Mach-O kernel loader ✓
- **Stage90**: XNU handoff (next)

With Stage86-89 complete, we have:
- Full exception handling at high-VA
- Complete Mach-O loader
- All prerequisites for executing real XNU code

The foundation is ready. Time to jump to XNU.
