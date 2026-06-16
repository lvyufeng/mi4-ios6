# Stage89: Mach-O Kernel Loader and XNU Handoff

## Goal

**Load a real XNU Mach-O kernel image into memory at the high-VA address 0x80000000
and prepare to transfer control to XNU's _start entry point.**

This is the critical transition from "simulating XNU boot" to "executing real XNU code."

## What Stage86-88 Proved

- **Stage86**: IRQ handling at high-VA ✓
- **Stage87**: Data abort handling at high-VA ✓
- **Stage88**: Undefined instruction handling at high-VA ✓

All three core exception types work at high-VA. The foundation is ready.

## What Stage89 Will Do

Stage89 implements a **real Mach-O loader** that:
1. Parses a real XNU Mach-O kernel binary
2. Allocates memory for each segment at high-VA (0x80000000 + offset)
3. Copies segment data from the Mach-O file to high-VA
4. Processes relocations (if needed)
5. Sets up the execution environment XNU expects
6. **Transfers control to XNU's _start entry point**

### Success Criteria

**Minimal success**: XNU _start executes and we capture the first instruction/crash  
**Full success**: XNU completes early initialization without panic

## Current Mach-O Infrastructure

We already have Mach-O parsing code (from earlier stages):
- `macho_probe.c`: Parses Mach-O headers, load commands, segments
- `macho_fixture.c`: Generates inert Mach-O fixture for testing
- `xnu_link.c`: Links subset of XNU object files (proof of concept)

**Current limitation**: This code validates Mach-O structure but never **executes** it.

## Stage89 Architecture

### Phase 1: Mach-O Loading (NEW)

**New file**: `stage89/xnu_macho_loader.c`

Key functions:
- `stage89_xnu_macho_loader_run()`: Main loader entry point
- `load_segment()`: Load one Mach-O segment to high-VA
- `resolve_entry_point()`: Find XNU _start address
- `prepare_handoff()`: Set up boot environment
- `jump_to_xnu()`: Transfer control

### Phase 2: Memory Allocation for Segments

XNU Mach-O has multiple segments:
- `__TEXT`: Executable code (read-only, executable)
- `__DATA`: Read-write data
- `__LINKEDIT`: Symbol table and relocations (read-only)
- `__PRELINK_TEXT`: Kext prelinking (if present)

**Loader algorithm**:
```
For each LC_SEGMENT load command:
    1. Read vmaddr, vmsize, fileoff, filesize
    2. Allocate pages at high-VA (vmaddr is already 0x80000000-based)
    3. Map pages in candidate L1 with appropriate permissions
    4. Copy filesize bytes from Mach-O[fileoff] to VA[vmaddr]
    5. Zero-fill remaining (vmsize - filesize) bytes
```

### Phase 3: Entry Point Resolution

The Mach-O header contains the entry point offset. For XNU kernels:
- `mach_header.entry` or `LC_UNIXTHREAD.entry_point` gives the offset
- Add to __TEXT vmaddr to get the absolute VA of _start

### Phase 4: Handoff Preparation

Before jumping to XNU:
1. Install the candidate L1 (already done)
2. Set up boot_args - XNU expects a pointer in r0
3. Set SP to a valid stack
4. Set VBAR (already at high-VA from Stage86-88)
5. Disable IRQs

### Phase 5: The Jump

```c
typedef void (*xnu_entry_t)(struct boot_args *);

xnu_entry_t xnu_start = (xnu_entry_t)xnu_entry_va;

/* Transfer control to XNU */
xnu_start(args);

/* If XNU returns (shouldn't happen) */
xnu_log_puts("XNU returned unexpectedly\n");
```

## Safety Strategy

Stage89 is **the most dangerous stage yet** because we're executing unknown code.

**Safety boundaries**:
1. Start with inert fixture first
2. Non-persistent boot (`fastboot boot`)
3. Extensive logging before the jump
4. Catch early crashes with abort handlers

**Expected**: XNU will likely crash on first attempt. Our job is to capture the crash,
identify what's missing, add it, and retry.

## Implementation Steps

1. Create `xnu_macho_loader.c` - Core loader logic
2. Extend candidate L1 - Add pages for XNU segments dynamically
3. Test with inert fixture - Verify loader works
4. Add XNU handoff sequence - Prepare environment and jump
5. Capture first XNU execution - Even if it crashes
6. Iterate on missing dependencies

## Expected Hardware Validation Markers

```
stage89_xnu_macho_loader_status=0x89000001
stage89_xnu_macho_loader_text_segment_loaded=0x00000001
stage89_xnu_macho_loader_entry_va=0x80001000
stage89_xnu_macho_loader_ready_for_handoff=0x00000001
stage89_xnu_macho_loader_xnu_started=0x00000001
```

**If XNU crashes** (expected initially):
```
stage89_xnu_crash_pc=0x800XXXXX
stage89_xnu_crash_type=data_abort/undef/etc
```

## What This Unlocks

Once Stage89 works (even if XNU crashes immediately):
- We've crossed the threshold from simulation to real execution
- We can iterate on missing dependencies
- We have a platform for debugging real XNU code

This is the **most important stage** in the entire project.
