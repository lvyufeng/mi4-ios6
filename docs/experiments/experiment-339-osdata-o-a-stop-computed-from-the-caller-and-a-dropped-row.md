# Experiment 339 — `OSData.cpp` linked: the object 338 stopped on, a stop computed from the caller, and one dropped row that moved four addresses

**Step:** link one object, `libkern/c++/OSData.cpp` (`libkern_c++_OSData.o`) — the only object in the pool that
defines 338's stop, `_ZN6OSData15withBytesNoCopyEPvj`. Nothing else changes.

**Prediction:** *5 resolved / 0 added — 849 → **844** undefined, 739 → **735** function, 110 → **109**
storage; `.text` +0xEEC to +0xF4D, ending between 0x8015B30C and 0x8015B36D, so `.data` stays at 0x8015C000
with its size and fill untouched — this object brings no `.data` at all; `.bss` placed exactly **−0x28** to
0x37B98;* and **the stop at `_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_` on
`_ZN9IOService10initializeEv+0x544` — computed from the *caller*, because this object runs to completion.**

**Result:** the counts exact, all six `.text` terms closing, `.bss` placed exactly −0x28, and the run stops at
`stub_hit=_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_` with
`xnu_entry_stub_caller_v=0x8012aa1c` = `_ZN9IOService10initializeEv+0x544` — symbol and offset both as
predicted. **One row of the object table was silently dropped and it moved four addresses**: the prediction
said the object brings no `.data`, and it brings a `__DATA, __data` of 0x60.

## The object

| `libkern_c++_OSData.o` | |
|---|---|
| `.text` | **3820** (0xEEC), 30 functions |
| `.text._ZN6OSData9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **208** (0xD0) |
| `.rodata.str1.1` | **97** (0x61) |
| `__DATA, __data` | **96** (0x60) ← **dropped by the object-table script, see below** |
| `.bss` | **24** (0x18 — `OSData::gMetaClass`) |
| `.init_array` | 4 (`_GLOBAL__sub_I_OSData.cpp`) |
| definitions / references | 55 / 46 |

**5 resolved, 0 added** — four functions and one storage stand-in, exactly the four `with*` constructors plus
`metaClass` — all 46 references already satisfied. This is the walk's **second zero created half** (334 was the
first), and it means the string pool loses four slots and gains none:

| resolved | object | stand-in was | name slot |
|---|---|---|---|
| `_ZN6OSData15withBytesNoCopyEPvj` (31 chars) | `T` | `func T` | 0x20 |
| `_ZN6OSData12withCapacityEj` (26) | `T` | `func T` | 0x1C |
| `_ZN6OSData8withDataEPKS_` (24) | `T` | `func T` | 0x1C |
| `_ZN6OSData9withBytesEPKvj` (25) | `T` | `func T` | 0x1C |
| `_ZN6OSData9metaClassE` | `R`, 4 | `data R 0x4` | — (storage) |

| | 338 | 339 | delta |
|---|---|---|---|
| undefined / function / storage | 849 / 739 / 110 | **844 / 735 / 109** | −5 / −4 / −1 |

## `.text`: six terms, all closing

| term | predicted | measured |
|---|---|---|
| this object's `.text` | +0xEEC | **+0xEEC** |
| its COMDAT | +0x004 | **+0x004** |
| its `.rodata`, placed whole | +0x0D0 | **+0x0D0** |
| its string bytes, as placed | +0x000 .. +0x061 | **+0x048** (25 bytes deduped) |
| the four retired stub bodies | −0x060 | **−0x060** |
| their four name slots | −0x074 | **−0x074** |

Sum **+0xF34**, the measured placed move (0x1596F4 → **0x15A628**), with the fill 0xD2C → **0xD38** (+0xC)
so `.text` is **+0xF40** (0x15A420 → **0x15B360**). `realstubs.o` confirms all three retired terms by section,
to the byte: `.text` 0x4548 → **0x44E8** (−0x60), `.rodata.str1.4` 0x42EC → **0x4278** (**−0x74**, the four
counted slots) and `.bss` 0x1C44 → **0x1C04** (−0x40).

## The dropped row, and the four addresses it moved

The object table above was built from `arm-none-eabi-objdump -h` through a small awk/python pipeline that took
field 1 as the section name and required exactly three fields. A Mach-O-style name **contains a space**, so
`__DATA, __data 00000060 ...` shifts every field by one and the parser's `except: continue` turned
`int('__data', 16)` into a silent skip rather than an error. The table said the object brings no `.data` at
all; it brings 0x60 of it. **One dropped row moved four addresses:**

| | 338 | 339 measured | predicted |
|---|---|---|---|
| `.data` | 0x8015C000 (0x19408) | **0x8015C000 (0x19468)** | address **right**, size 0x60 wrong |
| `.sysctl_set` | 0x80175408 (0x10C) | **0x80175468 (0x10C)** | +0x60 |
| `.init_array` | 0x80175514 (0x38) | **0x80175574 (0x3C)** | +0x60, size **+4 right**, fifteen entries |
| `.bss` | 0x80175580 (0x37CD8) | **0x801755C0 (0x37CD8)** | +0x40 |
| `__bss_end` | 0x801AD258 | **0x801AD298** | +0x40 |
| image | 1529164 | **1529264** | +0x60 |
| headroom | 1387944 | **1387880** | −0x40 |

`.data`'s placed total went 0x1195D → **0x119BD** (+0x60) with the fill **unmoved** at 0x7AAB;
`.sysctl_set` and `.init_array` followed by the same 0x60; and `.bss`'s start moved **+0x40 rather than
+0x60** because `align64` of `.init_array`'s new end 0x801755B0 is 0x801755C0 — the same round-up idiom that
has absorbed growth for five steps now.

This is the same defect class as the stub-name list's two record shapes (331): **a parser that drops a whole
record class cannot report that it did**, and the only thing that catches it is arithmetic between the model
and the build's own numbers. The check is cheap and should be routine: `objdump -h` and take the size from the
*end* of the line, or filter on the section's flags, rather than on a fixed field count.

## `.bss`: two halves that cancelled exactly

| `libkern_c++_OSData.o` | bytes | |
|---|---|---|
| one retired storage stand-in | **−0x40** | the full 64-byte slot, not the 4 bytes it held |
| the object's own `.bss` | **+0x18** | `OSData::gMetaClass` |
| **placed** | **−0x28** | 0x37BC0 → **0x37B98**, exact |
| fill | **+0x28** | 0x118 → **0x140** |
| **section** | **0** | 0x37CD8 in both builds |

The placed term is 331's rule as arithmetic for the third step running, and the fill moved by exactly its
magnitude in the opposite direction, so the section's **size is unmoved for two different layouts**. It is a
good reminder that `.bss`'s size is a worse progress indicator than its placed bytes, and the first time in
this walk that the two halves cancelled exactly.

`.init_array` is **0x3C — fifteen entries** ending 0x801755B0, `_GLOBAL__sub_I_OSData.cpp` at #14 (function at
0x80136F24) and `last_kernel_constructor` still last at #15 (0x80136F7C).

## The stop, computed from the caller

This is the first step whose stop is **not in the object being linked**. `OSData::withBytesNoCopy` is four
calls and a vtable dispatch, all real:

```
 480 <_ZN6OSData15withBytesNoCopyEPvj>:  push {r4, r5, r6, r7, fp, lr}
  +0x10  490  bl <_ZN8OSObjectnwEm>                          real since 326
  +0x24  4a4  bl <_ZN8OSObjectC2EPK11OSMetaClass>            real since 326
  +0x3C  4bc  bl <_ZNK11OSMetaClass19instanceConstructedEv>  real since 324
  +0x54  4d4  blx r3   [vptr+0x40 -> OSData::initWithBytesNoCopy]   this object
  +0x6C  4ec  blx r1   [vptr+0x14 -> OSObject::release]       the failure path, not taken
```

and `initWithBytesNoCopy` is `bl OSObject::init` (real), three stores (`bytes` at `[this+20]`, `length` at
`[this+8]`, `−1` at `[this+12]` — `capacity = EXTERNAL`) and a return. So the prediction was the **next** call
in `IOService::initialize`, and the tool agreed, before the device was touched:

```
$ ./tools/xnu_entry_callwalk.py --root _ZN9IOService10initializeEv

walk from _ZN9IOService10initializeEv:
  _ZN9IOService10initializeEv
    _ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_   STUB

first stub on the straight-line path: _ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_
```

Run from `_ZN6OSData15withBytesNoCopyEPvj` the same tool reports **no stub on the straight-line path**,
listing its two `blx` instructions rather than walking past them — which is the tool saying, correctly, that
the object 339 links is complete and that what stops the run is one frame up.

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_stub_caller_v=0x8012aa1c                   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000                   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_
```

`tools/host_resolve_entry_addr.sh 0x8012aa1c` → **`_ZN9IOService10initializeEv+0x544`**, `caller-4` =
`0x8012aa18: bl 8013ad24 <_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_>` — the symbol and
the offset both computed from the *caller's* disassembly before the build (`0xcfc − 0x7bc = 0x540`, return
address +0x544) and both matched by the run.

**What the one key measures.** The key moved one call forward in the same frame as 338's (+0x4D8 → +0x544),
and that forward move *is* the measurement: to reach it, `OSData::withBytesNoCopy` had to run to completion —
`OSObject::operator new` returning 28 bytes, the `OSObject` base constructor chaining through
`OSMetaClass::instanceConstructed`, the vtable dispatch landing on `OSData::initWithBytesNoCopy`, and that
function's `OSObject::init` returning true so the three stores happen and the wrap succeeds — and the returned
`OSData` had to be stored into `gIOConsoleUsersSeedValue` and survive. A failure anywhere in that path takes
`withBytesNoCopy`'s `bne` to the release-and-return-0 arm and the walk would have continued with `r4 = 0`;
instead the frame moved on. `abort_entries=0` throughout.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, `25` records of
`persistent_write_attempted=0x00000000` and `87` of `failure_mask=0x00000000` with **no non-zero reading of
either**, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301676 bytes, and the device back on Android on
its own (`MI 4LTE`, release 10).

## What this step measures

The frontier is `_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_`, defined by
`libkern/c++/OSOrderedSet.cpp` (`libkern_c++_OSOrderedSet.o`) — and the two instructions in front of the call in
the linked image say exactly what it is:

```
8012aa10:  mov  r0, #10
8012aa14:  mov  r1, #0
8012aa18:  bl   <_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_>

    gJobs = OSOrderedSet::withCapacity(10);        IOService.cpp:418 (declared at :183)
```

— the single-argument form with the **default** comparator (`r1 = 0`), so this step is not about the
match-ordering machinery: it is the ordered container `IOService` keeps its job and probe lists in. 340 is
where the walk stops building key symbols and starts building the **containers** IOKit keeps services in, and
because `OSSet` is `OSOrderedSet`'s base class, one link may resolve more than the one name —
`tools/entry_object_effect.py` will say so before the build. What is worth carrying forward is that after this
step the run will be inside the container-construction path, where the next stop is as likely to be
`OSDictionary`/`OSSet`/`OSArray` internals (all real since 330/334) as it is to be a new name.
