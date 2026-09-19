# Experiment 350 — `IOPMrootDomain.cpp`: a 4-byte function retires a 144 KB object, and `__DATA` moves

**Step:** link one object, `iokit/Kernel/IOPMrootDomain.cpp` (`iokit_Kernel_IOPMrootDomain.o`, 144744 bytes) —
the only object in the pool defining 349's stop `_ZN12IORootParent10initializeEv`, and the largest object this
walk has linked. Nothing else changes.

**Prediction:** *4 resolved / 30 added — 831 → **857** undefined, 728 → **747** function, 103 → **110** storage;
`.text` a band, **`.data` stepping a whole 0x4000 to 0x80184000**, `.sysctl_set` moving for the first time since
298;* and **the stop at `_ZN16IOPMinformeeList22getSharedRecursiveLockEv` on `iokit_post_constructor_init+0x2C`,
key `0x8011b118`.**

**Result:** all three counts exact, **the stop name and key exactly as predicted**, every derived row exact
except the `.bss` one that depends on the fill, and the row that had not moved since 298 — `.sysctl_set` — moved
by exactly the 0x2C this object brings. One row was a drafting slip (a hex-to-decimal conversion) and was
corrected before the build.

## The object

| `iokit_Kernel_IOPMrootDomain.o` | |
|---|---|
| `.text` | **59624** (0xE8E8), **565 definitions** |
| `.group` / COMDAT | 0x60 / **0x20** |
| `.bss` | **592** (0x250) |
| `.rodata` | **2684** (0xA7C) |
| `.rodata.str1.1` | **2908** (0xB5C) — the largest string input of the walk |
| `.data` | **824** (0x338) |
| `__DATA,__sysctl_set` | **44** (0x2C) — the first step of the walk to bring any |
| `.init_array` | 4 |
| definitions / references | **565 / 389**, of which 359 already satisfied |

**4 resolved / 30 added** — `_ZN12IORootParent10initializeEv` (349's stop), `_ZN14IOPMrootDomain10tracePointEh`,
`_ZN14IOPMrootDomain13startSpinDumpEj` and `hibernate_should_abort` out; **twenty-three functions and seven
storage stand-ins in** (`PEReadNVRAMProperty`, `_ZN9IOService20synchronizePowerTreeEmPS_`,
`_ZN9IOService23tellClientsWithResponseEi`, `sysctl__kern_iokittest`, `gIODTTargetTypeKey`, …) — for
831 + 30 − 4 = **857**, **728 → 747 function, 103 → 110 storage**, 747 + 110 = 857.

**Thirty added names is the largest creation side of the walk** after 347's twenty-one, and unlike 347's they
cost `.bss`: seven of them are storage stand-ins, which the generator sizes as 0x40-byte aligned slots rather
than at their own sizes (the tool prints that caveat, and the `.bss` row below is where it lands).

**And the whole 144744 bytes exist to retire four bytes.** `_ZN12IORootParent10initializeEv` is
`T e3d0 4` in the object and its entire body is `bx lr`, which is why the pre-run path prediction was one
`nm -S` away: the only new code the run can reach is a function that returns immediately.

## The stop: name and key as predicted, on the call 348 named two steps early

```
stub_hit=_ZN16IOPMinformeeList22getSharedRecursiveLockEv
xnu_entry_stub_caller_v=0x8011b118  (also _a and _e, all three agreeing)
caller-4 = 0x8011b114: bl 8015fd38 <_ZN16IOPMinformeeList22getSharedRecursiveLockEv>
abort_entries=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8011b118` → **`iokit_post_constructor_init+0x2c`** — the call 348's original
prediction named at its own step, the call 349's run stopped one instruction short of, and now 350's stop.
`digits=0x53` with `w0=0x31313038` (`"8011"`) and `w1=0x38313162` (`"b118"`).

**`abort_entries=0` is what makes the step a measurement.** Between 349's stop and this one the run had to enter
`_ZN12IORootParent10initializeEv` — now real, `T 0x4` at 0x8015ba10, its body the `bx lr` its object promised —
and return from it, then advance one call. Nothing else in the new code is on the path, because that function
makes no calls at all: **the object reading was the whole of the path prediction**, which is 349's correction
applied rather than re-learned.

## The layout: `.data` crosses, `__DATA` moves, and one row is the fill again

| | 349 | 350 measured | 350 predicted |
|---|---|---|---|
| `.text` | 0x80173380 (0x173380) | **0x801836E0** (0x1836E0) | 0x80182D18..0x80183874 ✓ (near the top) |
| `.data` | 0x80174000 (0x194A8) | **0x80184000** (**0x197E0**) | **0x80184000**, 0x197E0 ✓ |
| `.sysctl_set` | 0x8018D4A8 (0x10C) | **0x8019D7E0** (**0x138**) | **0x8019D7E0**, 0x138 ✓ |
| `.init_array` | 0x8018D5B4 (0x58, 22) | **0x8019D918** (**0x5C**, twenty-three) | **0x8019D918**, 0x5C, 23 ✓ |
| `.bss` | 0x8018D640 (0x37D58) | **0x8019D980** (0x38198) | **0x8019D980**, 0x38168 (0x30 low) |
| `__bss_end` | 0x801C5398 | **0x801D5B18** | ~0x801D5AE8 (0x30 low) |
| image | 1627660 | **1694068** | **1694068** ✓ |
| headroom | 1289320 | **1221864** | ~1221912 (0x30 high) |

**The crossing was predictable and was predicted.** The `.text` terms sum to **+0xF998 .. +0x104F4** (0xE8E8 of
object text, +0x20 COMDAT, +0xA7C of `.rodata`, 0x000..0xB5C of strings, −0x60 for four retired bodies, −0x84 for
four retired name slots, +0x348 for twenty-three created bodies, +0x3B0 for twenty-three created name slots whose
longest is a 72-character `IOService` method), so the text end had to land between 0x80182D18 and 0x80183874,
and the next 0x4000 boundary above that band is **0x80184000** with 0x78C of margin. It landed at **0x801836E0**,
0x1A0 below the band's top.

`.text` closed **+0x10360** with a fill delta of **+0xA** (67 rows summing 0xD44 against 349's 66 rows summing
0xD3A — both counted the same way, inside `.text` including the trailing pad, which is the convention 349's
block had to write down), so the placed term is **+0x10356** and **0x9BE of the object's 0xB5C string bytes were
placed**, the other 0x19E merged: 346's third mechanism again, and the reason the band's top end was not the
outcome.

**`.sysctl_set` had been 0x10C and unmoved since 298.** It moved to 0x8019D7E0 and its size grew to **0x138** —
exactly 0x10C + 0x2C, the object's own `__DATA,__sysctl_set`, with no fill. `.data`'s size is likewise exactly
0x194A8 + 0x338 with no fill, which is what let the four rows below it be stated as arithmetic: `.init_array`
0x58 + 4 = 0x5C with twenty-three entries, `.bss` at `align64(0x8019D974)` = **0x8019D980** for the eleventh step
running, and the image exactly `.init_array`'s end − ENTRY_BASE.

**The one row that missed is the fill, for the sixth step running**: `.bss` came in 0x30 above the placed model
(0x38198 against 0x38168 = 0x37D58 + 0x250 of object `.bss` + 0x1C0 of seven created storage stand-in slots),
so `__bss_end` and the headroom are 0x30 out. Every term of that row is measured; only the alignment behind them
is not, and it is the same row that has been the ledger's weakest for five steps.

## The corrected row: a hex-to-decimal slip

The prediction's image row read `0x19D974 = 1693044`. 0x19D974 is **1694068**, a 0x400 error in the one row of
the table that is a conversion rather than a derivation, and it was caught before the build by recomputing
`.init_array`'s end from the row above it (`0x8019D918 + 0x5C − 0x80000000`). The measured value is 1694068.
Worth one line because it is a different failure from the fill: the arithmetic was right and the base it was
written in was read wrong, which is 333's defect (a size read in the wrong base) in a row where the value is
printed in both bases side by side.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000080   xnu_entry_kv_in_dram=0x000000a4   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x80160d90          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8011b118   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN16IOPMinformeeList22getSharedRecursiveLockEv
```

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301659 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 351 — `iokit/Kernel/IOPMinformeeList.cpp`, and the line has been read past it

`iokit_Kernel_IOPMinformeeList.o` (6092 bytes) is the only object defining this step's stop: **1 resolved /
1 added** — `_ZN16IOPMinformeeList22getSharedRecursiveLockEv` out,
`_ZN12IOPMinformee10withObjectEP9IOService` in — for 857 → **857** undefined, **747 → 747 function,
110 → 110 storage**, one of each. `.text` 0x40C, `.group` 0xC, COMDAT 4, `.bss` 0x1C, `.rodata` 0x84,
`.rodata.str1.1` 0x11, `.init_array` 4.

**Predicted stop: `_ZN16IOKitDiagnostics11diagnosticsEv` at `iokit_post_constructor_init+0x74`, key
`0x8011b160`.** The constructor's line has been read *past* this step's callee in the 350 image, which is the
reading 349's correction taught — a stub's identity is an image fact:

```
+0x28  bl _ZN16IOPMinformeeList22getSharedRecursiveLockEv   0x18 STUB  <- 350's stop, retired here
+0x34  bl _ZN8OSString11withCStringEPKc                     0x74 real
+0x70  bl _ZN16IOKitDiagnostics11diagnosticsEv              0x18 STUB  <- 351's stop, key 0x8011b160
+0xDC  bl PE_parse_boot_argn                                0x08 real
```

and `getSharedRecursiveLock`'s body has been read **from its object** the way 348's rule requires: 0x24 bytes,
`push {r4, lr}`, `ldr r0, [sharedListLock]`, `cmp r0, #0`, `popne {r4, pc}`, `bl IORecursiveLockAlloc` (**real
since 328**), `str r0, [r4]`, `pop {r4, pc}` — **no stub call on any path through it**, so it returns and the
constructor advances two calls. There is also an early branch at `+0x38` (`beq` on `OSString::withCString`'s
return) that lands on `+0x70` anyway, so both paths reach the same stub.

The object defining 351's stop is **`iokit_Kernel_IOKitDebug.o`** — not `IOKitDiagnostics.o`, which is empty in
the pool — so 352 is a **7 resolved / 0 added** step (one function and six storage names: `gIOKitDebug`,
`gIOKitTrace`, `sysctl__debug_iokit`, `debug_iomalloc_size`, `debug_iomallocpageable_size`,
`debug_container_malloc_size`) for 857 → **850** undefined, **747 → 746 function, 110 → 104 storage**.
