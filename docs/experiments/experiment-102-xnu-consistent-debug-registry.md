# Experiment 102 — XNU's crash-log registry runs on MSM8974, and it writes a record

Date: 2026-09-17
Commit under test: `d720481`, plus the changes described below
Build switch: `STAGE90_XNU_REAL_DT = 1` (default off)
Other switches: `HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/kmsg-cd1.txt`

## What this adds

`experiment-100` got public-XNU code *observing* — walking a tree and reporting what it found.
This gets one of its subsystems *doing* something: `pexpert/arm/pe_consistent_debug.c`, the
crash-log registry iBoot hands the kernel, now runs end to end and writes a real record.

```
xnu_real_dt_checks=0x0000000a   xnu_real_dt_failures=0x00000000   status=0x90000001
xnu_real_dt_cd_inherit_ok=0x00000001
xnu_real_dt_cd_enabled=0x00000001
xnu_real_dt_cd_register_ok=0x00000001
xnu_real_dt_cd_record_readback_ok=0x00000001
xnu_real_dt_cd_header_intact=0x00000001
stage90 xnu_real_dt: XNU's device-tree code walked this tree and agreed
```

Three XNU calls, each a different part of the subsystem:

- **`PE_consistent_debug_inherit()`** looks up `/chosen`'s `consistent-debug-root` through XNU's
  own `DTLookupEntry`/`DTGetProperty`, takes the first word as a physical address, and maps it
  with `ml_map_high_window`. Its `-1` return is how XNU says "this platform has no registry";
  it returns 0 here, which means it found the property, read the address, and mapped it.
- **`PE_consistent_debug_enabled()`** reports the registry pointer is live.
- **`PE_consistent_debug_register()`** allocates an entry — CASing `kDbgIdUnusedEntry` to
  `kDbgIdReservedEntry` through the shim's `OSCompareAndSwap64`, since `ldrexd` is not ours to
  take — and writes `record_id`, `length` and `physaddr` into it.

The record is then **read back** rather than trusted: `cd_record_readback_ok=1` means the three
fields are what was asked for, and `cd_header_intact=1` means the registry's own top-level header
survived the allocation. A `register` that returned 0 without writing anything would look
identical from outside, which is why both are checked.

Four of the five public-XNU objects now execute: `device_tree.o`, `arm_pe_bootargs.o`,
`bootargs.o` and `arm_pe_consistent_debug.o`. Only `pe_gen.o` does not, and it is a compile/link
proof rather than a runtime one.

## The payload stood in for iBoot, and that is the point

`PE_consistent_debug_inherit` only *maps* the registry; it does not create it. On a real device
iBoot allocates the region and fills in the top-level header, and XNU reads `num_records` and
`record_size_bytes` from what it finds. So the payload has to do iBoot's half, and this is the
first time any part of the project has had to:

- The region is a `dbg_registry_t` in the shim's `.bss` — 12,704 bytes
  (`16 + 512 × 24 + 400`), inside the existing 16 KB window with room to spare.
- `stage90_xnu_consistent_debug_region_init()` zeroes it and writes the three fields XNU reads.
  `records[]` is left zeroed deliberately: `consistent_debug_allocate_entry()` claims an entry by
  CASing `kDbgIdUnusedEntry` (`0`) to `kDbgIdReservedEntry`, so zero *is* the claimable state.
- The device tree gains `/chosen/consistent-debug-root`, pointing at that region — the property
  `pe_consistent_debug.c:35-42` looks up. It is emitted only when the switch is on, because the
  region it names is part of the same change.

That is Phase 2's "iBoot-equivalent" job appearing for the first time in a concrete form: not a
structure we validate at arm's length, but a structure we *supply* and XNU consumes.

## `ml_map_high_window` is guarded, and the guard is the interesting part

The first version returned a fixed `.bss` pointer and ignored its `phys_addr` argument. That is
wrong in a specific way: it would have made the shim hand out a readable pointer to *any*
physical address a caller named, which is precisely the property a real `ml_map_high_window` does
not have — the real one creates a mapping in a window the kernel chose, and refuses addresses it
should not map.

The version that shipped answers only for the one region it owns, and returns 0 for anything else.
On this payload the high window is the identity map — the region lives in `.bss` inside PA 0–2 MB,
which the identity table maps `VA = PA` — so returning the address is correct *for that region*,
and the comparison is what keeps it honest.

## A constant Apple never published

`kDbgIdTopLevelHeader` is named in XNU's `consistent_debug.h:62` **in a comment only** —
`uint64_t record_id; // = kDbgIdTopLevelHeader` — and searching the whole tarball finds the name
in that comment and nowhere else. iBoot writes that value, so it lives on the other side of the
boundary. The shim defines it as `DEBUG_RECORD_ID_LONG('D','B','G','R','E','G','H','D')`,
following the convention every other id in the header uses, and the comment says plainly that it
is **chosen, not sourced**, so nobody goes looking for a definition that is not there.

Nothing depends on the choice: XNU reads only `num_records` and `record_size_bytes` from the
header, never `record_id`.

## What this does and does not establish

**Does:** XNU's crash-log subsystem functions on MSM8974, driven by XNU's own code, over a device
tree this payload built and a region this payload supplied. That is the first XNU subsystem to do
work here rather than report a measurement.

**Does not:** make it *useful* — the record this run writes is ours, and nothing reads the registry
back after a reboot yet. A real use would be XNU recording something on its own, which needs XNU
running further than it does. Nor does it make the payload's region persist anywhere useful: it is
`.bss`, so it is gone on reboot, where iBoot's would be in reserved memory.

This is the shape to keep growing, though: an XNU subsystem plus the iBoot-side half of its
contract, each one checkable against data we control.
