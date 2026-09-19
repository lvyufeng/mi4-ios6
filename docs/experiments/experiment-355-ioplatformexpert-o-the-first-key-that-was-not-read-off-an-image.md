# Experiment 355 — `IOPlatformExpert.cpp`: the first key that was not read off an image

**Step:** link one object, `iokit/Kernel/IOPlatformExpert.cpp` (`iokit_Kernel_IOPlatformExpert.o`) — the only
pool definer of 354's stop `_ZN22IOPlatformExpertDeviceC1Ev`. Nothing else changes.

**Prediction:** *8 resolved / 16 added — 856 → **864** undefined, 748 → **753** function, 108 → **111**
storage;* `.text` a band at +0x4394..+0x46A7 both of whose ends cross a 0x4000 boundary, so **`.data` steps to
`0x8018C000`** with every row below it; `.init_array` 0x68 (twenty-six); and **the stop at
`_Z17IODeviceTreeAllocPv` at `initWithArgs+0x18`, key `0x8015FF40`** — a key derived from the object's
placement, not read off the previous image.

**Result:** all three counts exact, every derived row exact, the image exact, and **the stop's name and key
exact to the byte**: `stub_hit=_Z17IODeviceTreeAllocPv` at `xnu_entry_stub_caller_v=0x8015ff40`,
`abort_entries=0`. The step's own map places the object's `.text` at **0x8015D788**, exactly where the
placement argument said it would be. The one row that missed is `.bss`'s size, by 0x2C, and this time the cause
is the fill — the object's 0x94 was not absorbed and the `*fill*` in front of `realstubs.o`'s `.bss` grew
0x10 → 0x3C. The run is also the measurement that `PE_state.deviceTreeHead != 0`, because the p1 == 0 branch
would have stopped somewhere else.

## The object

| `iokit_Kernel_IOPlatformExpert.o` | |
|---|---|
| `.text` | **11892** (0x2E74), 228 definitions |
| `.text.*` COMDAT | **0x14** (five 4-byte pieces) + five `.group` sections |
| `.bss` | **148** (0x94) |
| `.rodata` | **5028** (0x13A4) |
| `.rodata.str1.1` | **787** (0x313) |
| `.rodata.cst16` | **16** (0x10) |
| `.init_array` | 4 |
| `.data`, `__DATA,__data`, `__DATA,__sysctl_set` | **none of them** |
| definitions / references | 228 / 308, of which 292 already satisfied |

**8 resolved / 16 added** — `PEGetUTCTimeOfDay`, `PEHaltRestart`, `PEReadNVRAMProperty`,
`PERemoveNVRAMProperty`, `PESetUTCTimeOfDay` and the constructor out (functions),
`_ZN16IOPlatformDevice9metaClassE` (`R 4`) and `gPlatformInterruptControllerName` (`B 4`) out (storage);
**eleven functions in** (`SHA1Init`, `SHA1Update`, `SHA1Final`, `_Z17IODeviceTreeAllocPv`,
`_Z18IODTCompareNubNamePK15IORegistryEntryP8OSStringPS3_`,
`_Z21IODTResolveAddressingP15IORegistryEntryPKcP14IODeviceMemory`,
`_Z23IODTFindMatchingEntriesP15IORegistryEntrymPKc`, `_ZN10IOWorkLoop8workLoopEv`,
`_ZN16IORangeAllocator9withRangeEmmmm`, `_ZN8IOMapper17setMapperRequiredEb`, `_ZN9IODTNVRAMC1Ev`) and **five
storage stand-ins** (`_ZN16IORangeAllocator9metaClassE`, `_ZN21IOInterruptController9metaClassE`,
`_ZN9IODTNVRAM9metaClassE`, `gIODTCompatibleKey`, `gIODTModelKey`) — for 856 → **864** undefined,
**748 → 753 function, 108 → 111 storage**.

**All five created stand-ins are 4 bytes**, so this is a step where the 0x40 model and `align64(size)` agree
and 354's defect is not retested. That is worth stating rather than assuming: 354's correction makes the cost
of five stand-ins `0x140` here only because their sizes are 4.

## The stop: a key derived, not read

```
stub_hit=_Z17IODeviceTreeAllocPv
xnu_entry_stub_caller_v=0x8015ff40   (also _a and _e, all three agreeing)
caller-4 = 0x8015FF3C: bl 80164298 <_Z17IODeviceTreeAllocPv>   0x18 STUB
abort_entries=0x00000000
```

The prediction of `0x8015FF40` was **arithmetic, not a reading**: the object's `.text` was predicted at
0x8015D788 (immediately after `bsd_kern_bsd_stubs.o`'s 0x980 at 0x8015CE08 ends, where the next input,
`xnu_arm_entry_last_kernel_constructor.o`, is a 0x0 `.text` at that very address), `initWithArgs` is at object
+0x27A0, and the `bl` at object +0x14 makes the recorded caller object +0x18. The step's own map then shows the
object's `.text` at **0x8015D788**, and the linker's symbols at `T 0x84` — `initWithArgs` — at **0x8015FF28**.
The three parts of that argument (the previous step's last `.text` input, this object's section offset, and the
place inside the body) are all facts, and the composite is the first key of the walk that the previous image
could not have supplied.

What the run also measured, through `abort_entries=0`:

```
ctor   T 0x38 at 0x8015FEEC   bl IOService::IOService(OSMetaClass const*), str vptr,
                              bl OSMetaClass::instanceConstructed   - all real, all ran
StartIOKit+0x114              ldr r0, [r0]; ldr r5, [r0, #0x340]; blx r5
                              slot 0x340 = _ZTV22IOPlatformExpertDevice (`.rodata` + 0x86C, size 0x34C)
                              + 8 + 0x340 = `.rodata` + 0xBB4 = initWithArgs
initWithArgs+0x18             bl _Z17IODeviceTreeAllocPv            <- the stop
```

and the named falsifier did not fire: p1 is non-zero, so **`PE_state.deviceTreeHead != 0`** — the payload's
`deviceTreeP` reached `PE_state` through `PE_init_platform(FALSE, ...)`. Had it been zero, the same call site
would have gone to the `IOService::init(NULL)` branch and the stop would have been `IOWorkLoop::workLoop`.

## The layout: everything, but the `.bss` fill

| | 354 | 355 measured | 355 predicted |
|---|---|---|---|
| `.text` | 0x801855A0 (0x1855A0) | **0x80189BA0** (0x189BA0) | 0x80189940..0x80189C60 ✓ |
| `.data` | 0x80188000 (0x198B8) | **0x8018C000** (0x198B8, **unmoved**) | **0x8018C000**, 0x198B8 ✓ |
| `.sysctl_set` | 0x801A18B8 (0x140) | **0x801A58B8** (0x140) | **0x801A58B8** ✓ |
| `.init_array` | 0x801A19F8 (0x64, 25) | **0x801A59F8** (**0x68**, twenty-six) | **0x801A59F8**, 0x68, 26 ✓ |
| `.bss` | 0x801A1A80 (0x38E98) | **0x801A5A80** (**exact**), size **0x39018** | 0x801A5A80 ✓, 0x38F58..0x38FEC (+0x2C) |
| `__bss_end` | 0x801DA918 | **0x801DEA98** | 0x801DE9D8..0x801DEA6C (+0x2C) |
| image | 1710684 | **1727072** | **1727072** ✓ |
| headroom | 1201896 | **1185128** | 1185320..1185172 (−0x2C) |

**`.text` closed `+0x4600`** (0x1855A0 → 0x189BA0) against a placed sum of **+0x45E8**, so the fill is
**+0x18**:

| term | bytes |
|---|---|
| this object's `.text` (0x8015D788, ending 0x801605FC) | **+0x2E74** |
| its five `.text.*` COMDAT pieces | **+0x014** |
| its `.rodata` (0x8018353C) | **+0x13A4** |
| its string bytes, **placed 0x254 of 0x313** (read from this map at 0x801848E0) | **+0x254** |
| its `.rodata.cst16` | **+0x010** |
| the six retired stub bodies | **−0x090** |
| the six retired name slots (0x14 + 0x10 + 0x14 + 0x18 + 0x14 + 0x20) | **−0x084** |
| the eleven created stub bodies | **+0x108** |
| the eleven created name slots (0xC + 0xC + 0xC + 0x18 + 0x38 + 0x40 + 0x34 + 0x1C + 0x28 + 0x24 + 0x14) | **+0x164** |

`realstubs.o`'s two sections are exact against the closed forms and are what makes the last four rows
checkable: `.text` **0x4698** = 753 × 0x18 = 0x4620 − 0x90 + 0x108 ✓, `.rodata.str1.4` **0x453C** =
0x445C − 0x84 + 0x164 ✓. And the crossing was free: the band's low end 0x80189940 and its high end 0x80189C60
are on **opposite sides of 0x80188000 and the same side of 0x8018C000**, so `ALIGN(0x4000)` sent both to
**0x8018C000** and the whole `__DATA` segment plus `.bss`'s start moved by one flat 0x4000 — which is why five
of the eight rows above are exact and not banded.

**The `.bss` row missed by 0x2C, and the culprit is the fill.** The row is `0x38E98 + 0x180`, and both
halves are measured: `+0xC0` is the stand-in net (five created slots at 0x40 = 0x140, two retired at 0x40 =
0x80) and shows up as `realstubs.o`'s `.bss` going **0x28C4 → 0x2984**; the other `+0xC0` is the object's own
0x94 of `.bss` placed at 0x801DC030 (`IOPlatformExpert::gMetaClass` there, `gPlatformInterruptControllerName`
at 0x801DC048 — the retired stand-in's name, now a real definition) **plus a `*fill*` that grew from 0x10 to
0x3C**. The object's bytes end at 0x801DC0C4 and the 64-byte-aligned slot at 0x801DC100 needs 0x3C more, so
nothing was absorbed: the section grew by 0x94 + 0x2C where the band's top assumed it grew by 0x94.

That is the fill row again, in its mildest form, and it is the row 353's `*fill*` was the answer to and 354's
model error was mistaken for. The three cases side by side make the rule the ledger keeps re-learning: the
object's bytes are absorbed when the alignment gap is at least as large as they are (353: 0x10 into 0x30 →
0x20 left; 354: 0x20 into 0x30 → 0x10 left), and they are not when the gap is smaller (355: 0x94 into 0x30 →
the gap *grows* to 0x3C). The fill is a function of the running total, and it is measured, never modelled.

## The tool defect this step found, and fixed

`tools/entry_object_effect.py` printed its first pair under the word "undefined" from `len(image)` — the
image's *symbol* count, 10121 for this image — where the quantity meant is **the number of names the link needs
and the image does not provide**: this step's **864**, which is the build's own `N symbol(s) undefined` line
and, identically, the stub list's function-plus-storage record count. The deltas were right, and every block's
base was right, because each takes the base from the build's line and only the delta from the tool — so nothing
in the ledger moves. The tool now prints `864 -> 855 undefined` for 356's object and, run against the object
this step linked, `864 -> 864 undefined, 753 -> 753 function, 111 -> 111 storage`, which is what an object with
no effect on the current image should print.

It is the same class as 352's pool sweep and 354's stand-in caveat: **a number read off a tool's sentence
rather than off the thing that defines it.** Here the ledger was spared by an accident of method (the base came
from somewhere else), which is worth knowing: a wrong label can survive arbitrarily many steps when nothing
depends on it.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000068   xnu_entry_kv_in_dram=0x0000008c   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x801654e8          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8015ff40   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_Z17IODeviceTreeAllocPv
```

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of
either, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301635 bytes, and the device back on Android on its
own (`MI 4LTE`, release 10).

## Frontier: 356 — `iokit/Kernel/IODeviceTreeSupport.cpp`

`iokit_Kernel_IODeviceTreeSupport.o` is the only pool definer of this step's stop `_Z17IODeviceTreeAllocPv`:
**11 resolved / 2 added** — seven functions and four storage stand-ins out (`gIODTCompatibleKey`,
`gIODTModelKey`, `gIODTPlane`, `gIODTTargetTypeKey`), `_ZN14IODeviceMemory12withSubRangeEPS_mm` (pool `T 8`) and
`_ZN14IODeviceMemory9withRangeEmm` (pool `T 30`) in — for 864 → **855** undefined,
**753 → 748 function, 111 → 107 storage**. The object: `.text` **0x2920** of 82 definitions, `.bss` 0x58,
`.rodata.str1.1` **0x307**, and no `.rodata`, no `.init_array`, no COMDAT and no `.data` — so its closure has
four terms and the only `__DATA` motion is the four storage retirements, each freeing a 0x40 slot.

**Predicted stop: `_ZN10IOWorkLoop8workLoopEv` at `initWithArgs+0x6C`, key `0x8015FF94`** — the stub 355
created, at the far end of the call this step retires. `IODeviceTreeAlloc`'s body was read from its object as
348's rule requires: **0x694 bytes, 41 direct calls**, and the walk of its body against this image puts **79
direct external targets on the path — the function, its two local callees, and the two locals those reach —
with not one of them a stub or absent**: the smallest is `_ZN15IORegistryEntry15getRegistryRootEv` at `T 16`
and the largest `_ZN15IORegistryEntry8fromPathEPKcPK15IORegistryPlanePcPiPS_` at `T 676`, each verified with
`nm -S -P` over the image, which prints the defined name first and its size last. The object's three other
locals are off the path. So the function returns, `initWithArgs` calls the real
`IOService::init(entry, gIODTPlane)`, and the success tail's `bl IOWorkLoop::workLoop` is the stop.

**The claim cost two attempts, and the first was a parser.** The walk's first pass read `nm -S` on the image
and admitted records with four fields (`value size type name`), which is what a *sized* symbol prints. A
defined symbol the entry shim supplies without a size — `strncmp` at `T 0x80006cc0`, `strlen`, `memcpy` —
prints three, and the parser dropped it, so `strncmp`, which `MakeReferenceTable` really does call at object
+0x1EC, came back "not in the image". The verdict "every target is real" was the right one for the wrong
reason: **a record that cannot be read has to be counted as refused, not as absent**, and `nm -S -P` removes
the ambiguity because the name comes first and the size last, so a missing size cannot be mistaken for a
missing symbol. This is the record-shape defect `tools/entry_object_effect.py` was built to not have, and three
steps later it is still the cheapest way for this walk to write a false sentence.

Three named falsifiers: (a) one of the **29 `blx` dispatches** inside `IODeviceTreeAlloc` — the class of call
this walk's tooling has never been able to see, and 29 in one function is the largest count it has faced;
(b) the real `panic` at 0x8002DEB4, which `IODeviceTreeAlloc` calls at object +0x4F0 on a failed DT lookup
(kind 6, or kind 3 if the missing thing is a property of the simulated tree); (c) `IOService::init` returning
zero, which sends `initWithArgs` to its failure branch and `StartIOKit` out through its `pop` — a stop in a
different function, and the first stop of the walk outside the IOKit startup chain.
