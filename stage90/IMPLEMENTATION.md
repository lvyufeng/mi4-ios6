# Stage90: XNU Handoff - First Real XNU Execution

## Goal

**Transfer control to real XNU kernel code** loaded at 0x80008000 and capture
the first XNU execution, even if it crashes immediately.

This is the **most critical milestone** in the entire project: the transition
from "simulating XNU boot" to "executing real XNU code."

## What Stage86-89 Proved

- **Stage86**: IRQ handling at high-VA ✓
- **Stage87**: Data abort handling at high-VA ✓
- **Stage88**: Undefined instruction handling at high-VA ✓
- **Stage89**: Mach-O segments loaded to high-VA ✓

All prerequisites for executing XNU are met.

## Stage90 Architecture

### Phase 1: Environment Verification

Before jumping to XNU, verify the environment is correct:

```c
- TTBR0 = candidate_l1_base (high-VA pmap installed)
- VBAR = high-VA exception vectors
- SP = valid stack pointer
- CPSR.I = 1 (IRQs disabled, XNU will enable when ready)
- r0 = boot_args pointer
```

### Phase 2: The Jump

```c
typedef void (*xnu_entry_t)(struct boot_args *);

xnu_entry_t xnu_start = (xnu_entry_t)0x80008000;

/* This is it - the moment of truth */
xnu_start(args);

/* If XNU returns (it shouldn't), log and halt */
xnu_log_puts("XNU returned unexpectedly\n");
while (1) { __asm__ volatile ("wfi"); }
```

### Phase 3: Crash Capture

**XNU will almost certainly crash immediately.** This is expected and normal.

The crash handler (Stage87's data abort or Stage88's undef handler) will capture:
- **PC** - Where XNU crashed
- **LR** - What XNU was trying to return to
- **Registers** - What XNU was working with
- **DFAR/DFSR** - If data abort, what address and fault type

This tells us **what XNU expected but didn't find**.

### Phase 4: Iterative Fixes

Based on the crash:

1. **Identify missing service**
   - Look up PC in XNU source
   - See what function XNU tried to call
   - Understand what it needs

2. **Add minimal stub**
   - Implement just enough to satisfy XNU
   - Log when stub is called
   - Return plausible values

3. **Retry and repeat**
   - Each fix gets XNU a little further
   - Iterate until XNU completes early init

## Expected First Crash Scenarios

### Scenario A: XNU calls missing function

**Symptom**: Undefined instruction exception at PC in XNU code.

**Cause**: XNU branch-and-linked to address 0x00000000 (NULL function pointer).

**Fix**: Find what function XNU expected, add stub.

### Scenario B: XNU accesses missing data structure

**Symptom**: Data abort at unmapped address.

**Cause**: XNU dereferenced a pointer we didn't initialize.

**Fix**: Allocate and initialize the structure XNU expects.

### Scenario C: XNU expects hardware configured differently

**Symptom**: Hang or infinite loop.

**Cause**: XNU polling hardware that's in wrong state.

**Fix**: Configure hardware to expected state in earlier stages.

## Implementation Strategy

### Conservative First Attempt

For Stage90, we'll just **attempt the jump and capture the crash**.

No stubs, no guessing. Let XNU tell us what it needs.

**Implementation**:
```c
int stage90_xnu_handoff_run(
    struct boot_args *args,
    struct stage90_xnu_entry_stub_result *entry_result)
{
    const struct stage90_xnu_macho_loader_result *loader;
    typedef void (*xnu_entry_t)(struct boot_args *);
    xnu_entry_t xnu_start;
    
    loader = stage90_xnu_macho_loader_result();
    if (!loader || loader->status != STAGE90_STATUS_OK) {
        return -1;
    }
    
    xnu_start = (xnu_entry_t)loader->xnu_entry_va;
    
    xnu_log_puts("stage90_xnu_handoff: jumping to XNU at ");
    xnu_log_kv32("entry_va", loader->xnu_entry_va);
    
    /* Disable IRQs (safety) */
    __asm__ volatile ("cpsid i" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
    
    /* THE JUMP */
    xnu_start(args);
    
    /* Should never reach here */
    xnu_log_puts("stage90_xnu_handoff: XNU returned unexpectedly\n");
    return -1;
}
```

### What We'll Learn

From the first crash, we'll learn:
- Did XNU execute at all? (PC != 0x80008000 means yes)
- What was the first thing XNU tried to do?
- What specific service/structure is missing?
- How far did XNU get before crashing?

## Safety Boundaries

**This is the highest-risk stage yet:**
- Executing unknown code
- Code expects services we don't provide
- Will likely crash multiple times

**Safety measures**:
- Non-persistent boot (fastboot boot)
- Exception handlers capture crashes
- Detailed logging before jump
- Device can always recover to Android

**What we preserve**:
- No persistent writes
- No public XNU pmap/IOKit runtime (yet)
- Can always revert to Stage89

## Success Criteria

**Minimal success**: 
- XNU executes at least one instruction (PC != 0x80008000)
- Crash is captured with PC/LR/registers
- We identify what XNU tried to do

**Good success**:
- XNU executes several instructions
- Crash tells us exactly what's missing
- Clear path to add the missing piece

**Amazing success** (unlikely):
- XNU doesn't crash immediately
- Gets past early initialization
- Reaches a stable point

## Files to Modify

1. **xnu_macho_loader.c** - Already loads segments, no changes needed
2. **xnu_handoff.c** (NEW) - The jump implementation
3. **xnu_entry_stub.c** - Call handoff after loader succeeds
4. **stage90.h** - Add handoff result structure
5. **build.sh** - Add xnu_handoff.c to sources

## Hardware Validation Markers

**Before jump**:
```
stage90_xnu_handoff_ready=0x00000001
stage90_xnu_handoff_entry_va=0x80008000
stage90_xnu_handoff_boot_args_ptr=0x00XXXXXX
stage90_xnu_handoff_jumping=0x00000001
```

**After crash** (expected):
```
stage90_crash_pc=0x800XXXXX  (somewhere in XNU)
stage90_crash_type=data_abort or undef
stage90_crash_dfar=0xXXXXXXXX  (if data abort)
stage90_crash_lr=0x800XXXXX
```

## What This Unlocks

Once we have the first crash data:
- We know XNU can execute at high-VA
- We know what XNU's first dependency is
- We can start building the minimal runtime
- We enter the iterative "fix and retry" phase

This is where the project transforms from "proof of concept" to "actual XNU port."
