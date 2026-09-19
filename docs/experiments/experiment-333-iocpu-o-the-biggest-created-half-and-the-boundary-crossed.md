# Experiment 333 — `IOCPU.cpp` linked: the biggest created half in the walk, the 16 KB boundary crossed, and the stop inside the newly real body

**Step:** link one object, `iokit/Kernel/IOCPU.cpp` (`iokit_Kernel_IOCPU.o`, manifest:301) — the only object
in the pool that defines 332's stop, `_Z15IOCPUInitializev`. Nothing else changes.

**Prediction:** *9 resolved / 221 added — 781 → **993** undefined, 674 → **880** function, 107 → **113**
storage; `.text` +0x5CD8 before strings and fill, crossing the 16 KB boundary at 0x80144000 and so
re-baselining every address above it; `.init_array` 0x20 → 0x24 with `_GLOBAL__sub_I_IOCPU.cpp` as entry #8
and `last_kernel_constructor` as #9; `.bss` +0x230 placed (the object's 0xB0 plus six 0x40 stand-in slots)
plus fill; and the stop inside the newly real body at `_Z15IOCPUInitializev+0x1C`, on
`_ZN7OSArray12withCapacityEj`.*

**Result:** **all of it.** The counts are exact to the name, every `.text` and `.bss` term closes with no
residual, the boundary crossed as predicted, and the run stops at `stub_hit=_ZN7OSArray12withCapacityEj`
with `xnu_entry_stub_caller=0x80122c58` = `_Z15IOCPUInitializev+0x1c`.

## The object, and a size read in the wrong base

| `iokit_Kernel_IOCPU.o` | |
|---|---|
| `.text` | **6592** (0x19C0), 24 functions |
| `.text._ZN5IOCPU9MetaClassD0Ev` / `.text._ZN24IOCPUInterruptController9MetaClassD0Ev` | 4 + 4 (COMDAT) |
| `.rodata` | **1956** (0x7A4 — the two vtables, the two MetaClass vtables, `_ZN5IOCPU9metaClassE`) |
| `.rodata.str1.1` | **500** (0x1F4) |
| `.bss` | **176** (0xB0 — the two `gMetaClass` objects, `_ZL13gActionQueues` 0x30, eight 4-byte statics) |
| `.init_array` | 4 (`_GLOBAL__sub_I_IOCPU.cpp`) |
| definitions / references | 116 / 280 |

The first reading of this object took `nm -S -P`'s sizes for decimal and so read `_Z15IOCPUInitializev` as
184 bytes ending at 0x3CC — while the disassembly keeps going to 0x498. **`nm` prints hex**, and `184` is
0x184: the function is 388 bytes. The check that caught it is the one this walk has used since 307 —
compare a size against **the distance to the next symbol** (`IOInstallServicePlatformActions` at 0x498),
not against what the number looks like. Nothing downstream depended on the wrong reading, because the
prediction that mattered was an offset *inside* the function.

## The counts, and why the created half is 221

**Predicted 9 resolved / 221 added; measured 9 / 221.** The 9 are the eight `PE_cpu_*` shims plus
`IOCPUInitialize` itself, all function stubs. The 221 all come from one thing: this object is written
against the IOKit class hierarchy (`IORegistryEntry::*`, `IOService::*`, `IOInterruptController::*`), none
of which is linked yet, so linking it creates a stub for every method it can see.

| | 332 | 333 | delta |
|---|---|---|---|
| undefined / function / storage | 781 / 674 / 107 | **993 / 880 / 113** | +212 / +206 / +6 |

Of the 221 created, **215 are functions and 6 are storage** — classified not by eye but by the same
`nm -S`-over-the-pool table the generator itself uses:
`_ZN21IOInterruptController10gMetaClassE` (0x18), `_ZN9IOService10gMetaClassE` (0x18),
`gIOInterruptControllersKey`, `gIOInterruptSpecifiersKey`, `gIOServicePlane` and
`gPlatformInterruptControllerName` (4 each). None of the six is dereferenced before this step's stop: the
two metaclasses are **addresses** handed to `OSMetaClass::OSMetaClass`, which stores `superClassLink` and
never follows it (`OSMetaClass.cpp:384-388`), and the four pointers are written by `IOService::initialize`
— the caller's *third* call, after the stop.

## `.text`: nine terms, no residual

| term | bytes |
|---|---|
| this object's `.text` | **+0x19C0** |
| its two COMDAT `.text._ZN*9MetaClassD0Ev` | **+0x008** |
| its `.rodata`, placed whole | **+0x7A4** |
| its string bytes, as placed | **+0x104** of 0x1F4 (0xF0 deduped) |
| the nine retired stub bodies | **−0x0D8** |
| their nine name slots | **−0x0E0** |
| the 215 created stub bodies | **+0x1428** |
| their 215 name slots | **+0x2688** |
| `.text` fill | **−0x008** |

Sum **+0x5B60**, which is the measured `.text` delta (0x142FC0 → **0x148B20**), and the map's own placed
total moved +0x5B68 with the fill falling 0xD2C → 0xD24 across **59 records in both builds**.

The four stub terms are checked twice over, because `realstubs.o` is a separate measurement of the same
thing: its `.text` goes 16176 → **21120** (+0x1350 = +0x1428 − 0x0D8) and its `.rodata.str1.4` goes
14091 → **23731** (+0x25A8 = +0x2688 − 0x0E0). Both exact.

## The 16 KB boundary, and what crossing it does and does not move

`.text` ends at 0x80148B20, past the 0x80144000 boundary, so `.data` steps to the next one:

| | 332 | 333 | delta |
|---|---|---|---|
| `.data` | 0x80144000 (0x19398) | **0x8014C000 (0x19398)** | **+0x8000**, size 0 |
| `.sysctl_set` | 0x8015D398 (0x10C) | **0x80165398 (0x10C)** | +0x8000, size 0 |
| `.init_array` | 0x8015D4A4 (0x20) | **0x801654A4 (0x24)** | +0x8000, **+4** |
| `.bss` | 0x8015D500 (0x37858) | **0x80165500 (0x37A98)** | **+0x8000**, size **+0x240** |
| `__bss_end` | 0x80194D58 | **0x8019CF98** | +0x8240 |
| image | 1430724 | **1463496** | +0x8004 |
| headroom | 1487528 | **1454184** | −0x8240 |

`.data`'s **content is untouched** — its size (0x19398), its own fill (0x7AA3) and its placed total
(0x118F5) are all unmoved; only its address changed. Everything after it follows by the same 0x8000, and
the headroom falls by the `__bss_end` move, since it is the room *below* `topOfKernelData`.

`.init_array` is **0x24 — nine entries**, resolved against the image the build just made:

```
0x801654a4  _GLOBAL__sub_I_OSKext.cpp      0x80120f74  _GLOBAL__sub_I_OSCollection.cpp
0x801654a8  _GLOBAL__sub_I_OSMetaClass.cpp 0x80122018  _GLOBAL__sub_I_OSSymbol.cpp
0x801654ac  _GLOBAL__sub_I_OSDictionary.cpp 0x801228d0 _GLOBAL__sub_I_OSString.cpp
0x801654b0  _GLOBAL__sub_I_OSObject.cpp    0x80124248  _GLOBAL__sub_I_IOCPU.cpp     <- #8, new
                                           0x801242f0  last_kernel_constructor      <- #9
```

The *order* is what the step depends on: IOCPU.o is appended before the last kernel constructor, so its
class constructors run before the call they enable, and `last_kernel_constructor` stays last — which is
the only thing its name is about.

## `.bss`: +0x240 in three measured parts

- **+0x180 placed slots.** The six new storage stand-ins each take a **0x40-byte 64-aligned slot**
  whatever their `nm -S` size — the rule 331 measured — and `realstubs.o`'s own `.bss` proves it
  independently: 7044 → **7428**.
- **+0x0B0 placed.** The object's own `.bss`, which it defines rather than stands in for.
- **+0x010 fill.** 0x109 → **0x119**.

`realstubs.o` +0x180, object +0xB0 and fill +0x10 make the section's +0x240.

## The stop, read off the object before the build

`IOCPUInitialize`'s four distinct callees, in the object's own order:

```
314 <_Z15IOCPUInitializev>:
 +0x04  0x318  bl IOLockAlloc                          real (322)
 +0x18  0x32c  bl _ZN7OSArray12withCapacityEj         STUB    <- return address +0x1C
 +0x78  0x38c  bl _ZN8OSSymbol17withCStringNoCopyEPKc real (330)
 +0x90  0x3a4  bl _ZN8OSString17withCStringNoCopyEPKc real (331)
```

and in the built image, at 0x80122c3c:

```
80122c3c <_Z15IOCPUInitializev>:  push {r4, lr}
 +0x04  80122c40  bl 8011cd74 <IOLockAlloc>
 +0x0C  80122c48  movt r1, #0x8019
 +0x10  80122c4c  str  r0, [r1]        ; -> _ZL11gIOCPUsLock at 0x8019B200
 +0x18  80122c54  bl 801284e8 <_ZN7OSArray12withCapacityEj>
```

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_why_byte=0x00000061
 xnu_entry_kv_written=0x0000006c
 xnu_entry_stub_caller=0x80122c58                    (also _a and _e)
 xnu_entry_stub_caller_digits=0x0000003f
 xnu_entry_abort_entries=0x00000000                  <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN7OSArray12withCapacityEj
```

`tools/host_resolve_entry_addr.sh 0x80122c58` → **`_Z15IOCPUInitializev+0x1c`**, `caller-4` =
`0x80122c54: bl 801284e8 <_ZN7OSArray12withCapacityEj>`.

**What the one key measures beyond the stop.** Reaching +0x1C means the walk went *into* `IOCPUInitialize`
— so `iokit_post_constructor_init`'s first call is real and returned — that its own first call
`IOLockAlloc` returned, and that the store between the two calls executed (`str r0, [r1]`, three
instructions later, into `_ZL11gIOCPUsLock`): a fault at that store would have been a `data abort` with
`abort_entries=1` rather than a stub. And reaching it at all means entry #8 ran — the constructor table of
a real kernel, seven class metamorphoses plus this object's two class constructors plus the last kernel
constructor, is now being walked to its last entry.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img` (4378 KB), nothing flashed,
`25` records of `persistent_write_attempted=0x00000000` and `87` of `failure_mask=0x00000000` with **no
non-zero reading of either**, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301639 bytes, and the
device back on Android on its own (`MI 4LTE`, release 10).

Of 7820 symbols the previous image had, **0 are retired and 304 are added**: the object's 116 definitions,
the 215 names it made stubs, and its `_GLOBAL__sub_I_IOCPU.cpp`. Nothing leaves the image, because a
resolved stub name does not disappear — it is replaced by its real definition. The nine move:
`_Z15IOCPUInitializev` 0x801265b0 (a stub) → **0x80122c3c**, the eight `PE_cpu_*` shims with it.

## What this step measures

The frontier is `OSArray::withCapacity` — the first stub `IOCPUInitialize` calls, and the same name
`OSKext::initialize` reaches six calls in. So `libkern/c++/OSArray.cpp` is the object that unblocks both
paths at once, and it is also where the `OSArray`/`OSSet` container machinery `OSKext::initialize` builds
its seven registries out of lives.

```
    # 334: `libkern/c++/OSArray.cpp` - the container both paths now stop on, and the object that defines
    #      the array `OSKext::initialize` and `IOCPUInitialize` each build first
```
