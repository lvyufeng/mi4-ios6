# Stage89: Mach-O Kernel Loader

## Overview

Stage89 implements a **Mach-O kernel loader** that parses a Mach-O binary,
loads its segments into high-VA memory at 0x80000000, and prepares to transfer
control to XNU's entry point.

This is the **critical transition** from "simulating XNU boot" to "executing
real XNU code."

## Current Status: DRY-RUN MODE ⚠️

**Build**: ✅ Compiles successfully  
**Hardware**: ⚠️ Awaiting device recovery for testing  
**Mode**: DRY-RUN (parsing only, no actual memory writes)

The current implementation:
- ✅ Parses Mach-O headers and load commands
- ✅ Validates segment addresses against mapped range
- ✅ Resolves entry point from LC_UNIXTHREAD
- ✅ Logs all operations in detail
- ⚠️ Does NOT execute memcpy/memset (safety measure)

## Architecture

### Mach-O Loader Flow

```
1. Parse Mach-O header (magic, ncmds, etc.)
2. Iterate load commands:
   - LC_SEGMENT: Load segment to high-VA
   - LC_UNIXTHREAD: Extract entry point
3. Validate all addresses in mapped range (0x80000000-0x800fffff)
4. Load segments (currently dry-run only)
5. Compute XNU entry point VA
6. Mark ready for handoff
```

### Memory Map Constraint

The candidate L1 (from earlier stages) maps:
- **0x80000000 - 0x800fffff** (1MB) via L2 4KB pages

All segments must fit within this range. Segments outside are logged and skipped.

### Current Fixture

The embedded Mach-O fixture has segments at:
```
__TEXT:          0x80008000 - 0x8000a000  (8KB)   ✓ Mapped
__DATA:          0x8000a000 - 0x8000c000  (8KB)   ✓ Mapped  
__LINKEDIT:      0x8000c000 - 0x8000d000  (4KB)   ✓ Mapped
__PRELINK_TEXT:  0x8000d000 - 0x8000e000  (4KB)   ✓ Mapped
__PRELINK_INFO:  0x8000e000 - 0x80010000  (8KB)   ✓ Mapped
__PRELINK_STATE: 0x80010000 - 0x80011000  (4KB)   ✗ Outside (skipped)
```

The loader gracefully handles the out-of-range segment.

## Files

- **xnu_macho_loader.c** - Core loader implementation
- **stage90.h** - Structures and constants
- **xnu_entry_stub.c** - Integration into boot flow
- **IMPLEMENTATION.md** - Detailed architecture
- **IMPLEMENTATION_STATUS.md** - Current status and next steps

## Testing

### Prerequisites

1. Device must be recovered (manual reboot if hung)
2. Device accessible via ADB or fastboot
3. Root access on device (for /proc/last_kmsg)

### Quick Test

```bash
cd /mnt/data/mi4-ios6

# Ensure device is in fastboot
adb reboot bootloader  # if in ADB
# or manually enter fastboot mode

# Boot Stage89
sudo fastboot -s 4a2fe00b boot out/stage90/stage90-qcdt.img

# Wait for boot and capture log
adb wait-for-device
sleep 5
adb shell 'su -c "cat /proc/last_kmsg"' > /tmp/stage90.txt

# Check results
grep "stage90_xnu_macho_loader" /tmp/stage90.txt
```

### Using Helper Script

```bash
/tmp/wait_and_test_stage90.sh
```

This script:
- Waits for device recovery
- Automatically reboots to fastboot
- Boots Stage89
- Captures and displays results

### Expected Success Markers

```
stage90_xnu_macho_loader_status=0x90000001
stage90_xnu_macho_loader_segments_loaded=5  (or 6)
stage90_xnu_macho_loader_text_segment_va=0x80008000
stage90_xnu_macho_loader_xnu_entry_va=0x800083c0
stage90_xnu_macho_loader_ready_for_handoff=0x00000001
loader_status=0x90000001
kernel_entry returned success
```

## Next Steps

### Phase 1: Validate Dry-Run (CURRENT)

Test current implementation:
1. Boot on hardware
2. Verify all segments are parsed
3. Confirm address validation works
4. Check "ready_for_handoff" flag

### Phase 2: Enable Actual Loading

If dry-run succeeds:
1. Uncomment memcpy/memset in load_segment()
2. Rebuild and test
3. Verify segments are loaded correctly
4. Check data integrity

### Phase 3: XNU Handoff (FUTURE)

Once loading works:
1. Set up boot environment (r0 = boot_args)
2. Jump to XNU entry point
3. Catch first XNU crash (expected)
4. Iterate on missing dependencies

## Safety

**Current risk: LOW** (dry-run mode)
- No memory writes
- No jumps to unknown code
- Only parsing and validation

**Next phase risk: MEDIUM** (actual loading)
- Will write to high-VA memory
- Could trigger data aborts if validation fails

**Future risk: HIGH** (XNU handoff)
- Transfers control to unknown code
- XNU will likely crash initially

## Implementation Notes

### Why Dry-Run?

The first test caused device hang, indicating a crash during execution.
Dry-run mode allows us to:
- Verify parsing logic without risk
- Validate addresses before attempting writes
- See complete execution flow in logs
- Identify exactly which operation would fail

### Address Validation

The loader checks every segment:
- vmaddr must be >= 0x80000000
- vmaddr + vmsize must be < 0x80100000
- Segments outside range are skipped (not failed)

This prevents data aborts from accessing unmapped memory.

### Detailed Logging

Every operation is logged:
- Segment addresses and sizes
- File offsets and copy sizes
- Validation results
- Skip reasons

This makes debugging straightforward from serial logs.

## Troubleshooting

**Device hangs after boot**:
- Likely a data abort or infinite loop
- Check address validation logic
- Verify dry-run mode is enabled
- Review logs for last operation before hang

**"segments_loaded=0"**:
- All segments were outside mapped range
- Check fixture segment addresses
- Verify L1 mapping is correct

**Entry point incorrect**:
- LC_UNIXTHREAD may be missing
- Falls back to start of __TEXT
- Check fixture has LC_UNIXTHREAD command

## References

- Mach-O format: `external/xnu-4570.1.46/EXTERNAL_HEADERS/mach-o/loader.h`
- ARM XNU boot: `external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c`
- Earlier stages: Stage86-88 (exception handling validation)
