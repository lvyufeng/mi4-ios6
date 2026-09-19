# Experiment 352 — `IOKitDebug.cpp`: the constructor returns, and `__DATA` moves

**Step:** link one object, `iokit/Kernel/IOKitDebug.cpp` (`iokit_Kernel_IOKitDebug.o`, 27044 bytes) — the only
object in the pool defining 351's stop `_ZN16IOKitDiagnostics11diagnosticsEv`. Nothing else changes.

**Prediction:** *7 resolved / 0 added — 857 → **850** undefined, 747 → **746** function, 110 → **104** storage
(six storage stand-ins retired, the largest storage retirement of the walk);* **`.data` stepping a whole 0x4000
to 0x80188000`**; and **the stop at `devsw_init` on `StartIOKit+0xCC`, key `0x8011b26c`** — which is also the
step that ends `iokit_post_constructor_init`, because the diagnostics call it retires is that function's last
`bl`.

**Result:** all three counts exact, `.data` moved exactly as predicted, every derived row exact except the
`.bss` fill, **the stop name and key exactly as predicted** — and **the fill was zero**, the first time since
346, with the text closure's every term read from the map. All three falsifiers did not fire, including the two
this walk's tooling cannot see.

## The object

| `iokit_Kernel_IOKitDebug.o` | |
|---|---|
| `.text` | **2148** (0x864), 9 definitions |
| `.group` / COMDAT | 0x18 / **0x8** (two 4-byte `.text.*`) |
| `.bss` | **80** (0x50) |
| `.rodata` | **1116** (0x45C) |
| `.rodata.str1.1` | **378** (0x17A) |
| `.data` | **96** (0x60) |
| `__DATA,__sysctl_set` | **8** |
| `.init_array` | 4 |
| definitions / references | **73 / 258**, all 258 already satisfied |

**7 resolved / 0 added** — one function, `_ZN16IOKitDiagnostics11diagnosticsEv`, plus **six storage
stand-ins** (`gIOKitDebug`, `gIOKitTrace`, `sysctl__debug_iokit`, `debug_iomalloc_size`,
`debug_iomallocpageable_size`, `debug_container_malloc_size`) — for 857 + 0 − 7 = **850**,
**747 → 746 function, 110 → 104 storage**, 746 + 104 = 850.

**The six storage stand-ins are all `.bss`, and that is measured rather than assumed.** Read in the 351 image:
`sysctl__debug_iokit` 0x801D4700, `gIOKitTrace` 0x801D54C0, `gIOKitDebug` 0x801D5500, `debug_iomalloc_size`
0x801D57C0, `debug_iomallocpageable_size` 0x801D5800, `debug_container_malloc_size` 0x801D5840 — each a
0x40-byte 64-byte-aligned slot, so the retirement gives back **0x180** and not the 6 × 0x30 a reading of the
object's own sizes would suggest. The most interesting of the six is `sysctl__debug_iokit`: the object defines
it as **0x30 bytes of `.data`**, and the stand-in standing in for it is 0x40 bytes of `.bss` — the section a
name leaves is not the section it enters.

## The stop: the constructor's last call, and the unwind that follows it

```
stub_hit=devsw_init
xnu_entry_stub_caller=0x8011b26c   (also _a and _e, all three agreeing)
caller-4 = 0x8011b268: bl 8015c818 <devsw_init>
abort_entries=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8011b26c` → **`StartIOKit+0xcc`** — `StartIOKit` being the step's real
subject, because everything between the constructor and this call had to run for the stop to be here:

```
iokit_post_constructor_init  returns - via the tail-called `OSObject::release()` on the
                             diagnostics object, and only because `OSObject::init()` is
                             `mov r0, #1; bx lr` and so the release path is not taken
  -> OSRuntimeInitializeCPP+0x184    `subs r9, r9, #1` reads zero: the table's last entry
  -> its tail                        checkModLoad (0x30), nextsegfromheader, firstsegfromheader
  -> postModLoad                     0x474 bytes, EIGHT `blx` dispatches
  -> OSRuntimeFinalizeCPP            0x180 bytes, three `blx`
  -> OSlibkernInit+0x1C              `cmp r0, #0; beq` - nonzero would `bl panic` (0x8002DEB4)
  -> StartIOKit+0xC4's return        `bl OSlibkernInit` at 0x8011b264 returns to 0x8011b268
8011b268: bl devsw_init               STUB  <- the stop
```

**`abort_entries=0` is what makes the unwind a measurement**, and it is the largest region this walk has ever
crossed in one step: the constructor *returned for the first time in the walk's history*, and the C++ runtime's
whole tail — the segment loop, the metaclass walk and the `OSlibkernInit` gate that would have panicked — ran
without a fault and without a panic.

## The layout: `.data` crosses, and the fill is zero

| | 351 | 352 measured | 352 predicted |
|---|---|---|---|
| `.text` | 0x80183B80 (0x183B80) | **0x80184960** (0x184960) | 0x80184820..0x801849A0 ✓ (0x30 below the top) |
| `.data` | 0x80184000 (0x197E0) | **0x80188000** (**0x19840**) | **0x80188000**, 0x19840 ✓ |
| `.sysctl_set` | 0x8019D7E0 (0x138) | **0x801A1840** (**0x140**) | **0x801A1840**, 0x140 ✓ |
| `.init_array` | 0x8019D918 (0x60, 24) | **0x801A1980** (**0x64**, twenty-five) | **0x801A1980**, 0x64, 25 ✓ |
| `.bss` | 0x8019D980 (0x38198) | **0x801A1A00** (0x38058) | **0x801A1A00**, 0x38068 (+ fill) |
| `__bss_end` | 0x801D5B18 | **0x801D9A58** | ~0x801D9A68 (0x10 high) |
| image | 1694072 | **1710564** | **1710564** ✓ |
| headroom | 1221864 | **1205672** | ~1205656 (0x10 low) |

**The move was predicted from the boundary, not from the fill.** `.text` closed **+0xDE0** with the object's
`.rodata.str1.1` placed at **0x158 of its 0x17A** (read from this map at 0x8017F3C9), so every term is measured
— **+0x864** object text, **+0x008** COMDAT, **+0x45C** `.rodata`, **+0x158** string bytes, **−0x018** one
retired body, **−0x028** one retired name slot — and their sum is **+0xDE0 with a fill of zero**, the first zero
fill since 346. And the crossing needed no fill at all: the object's own `.text` (0x864) is larger than the
0x480 of room left below 0x80184000, so `.data` had to step whether the fill was 0, −0x4 or +0x100.

`.data`'s size is exactly 0x197E0 + 0x60 and `.sysctl_set`'s exactly 0x138 + 8, both with no fill, so the three
rows below them are arithmetic: `.init_array`'s start is `.sysctl_set`'s end, its size is 0x60 + 4, its end is
0x801A19E4, `.bss`'s start is `align64` of that, and the image is `.init_array`'s end minus `ENTRY_BASE` —
**1710564 exact**, which the build says in the other direction too ("the copied image ends at 0x801a19e4").

**The one row that missed is the fill again, for the seventh step running**: `.bss`'s placed term is
0x38198 − 0x180 + 0x50 = 0x38068 and the section came in at **0x38058**, a fill of **−0x10**, so `__bss_end`
and the headroom are 0x10 out. Every term of that row is measured; the alignment behind them is not, and it has
been the wrong half of this ledger for seven steps.

## The name slot, pinned: `align4(len + 1)`, and one ledger row corrected

The prediction's name-slot row was written `align4(36)` = 0x24 — the convention this ledger has been using
since 346 — and the correction to **`align4(36 + 1)` = 0x28** was made after this step's build and before its
run. The two agree for every name whose length is 2 mod 4, which is why 351's `align4(41)` = 0x2C came out right
by coincidence while this row would have been 4 bytes out.

What pins it is an identity on the image this step built:

> **the sum of `align4(len + 1)` over the 746 function stubs is exactly 0x44D0**, the measured size of
> `realstubs.o`'s `.rodata.str1.4`, with no fill and no tail.

and 0x44D0 + 0x28 = 0x44F8 is the same sum over 747 — the 746 plus `_ZN16IOKitDiagnostics11diagnosticsEv`.
Every earlier step read that section's size as a *difference* between two totals, and a difference cannot
distinguish "the model is right" from "two errors cancelled". This is the first step where the number was
derivable without a predecessor.

## `realstubs.o`: three amounts, one of them a closed form

| | 352 measured | model |
|---|---|---|
| `.text` | **0x45F0** | 746 × 0x18 = 747 × 0x18 − 0x18 (one retired body) ✓ |
| `.rodata.str1.4` | **0x44D0** | the sum of `align4(len + 1)` over 746 names ✓ |
| `.bss` | **0x1AC4** | 104 × 0x40 + 0xC4 — six retired slots, the 0xC4 being the reserved-slot advance this section has carried since 344 ✓ |

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000005b   xnu_entry_kv_in_dram=0x0000007f   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x801619f4          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller=0x8011b26c   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=devsw_init
```

**Every falsifier did not fire, including the two this walk has never had a way to see**: the eight `blx`
dispatches inside `postModLoad` (0x474 bytes of the blind spot), the real 0x48-byte `panic` at 0x8002DEB4 that
`OSlibkernInit` calls on a nonzero return — which would have been the walk's *kind 6* rather than a stub — and
a stop inside `OSRuntimeFinalizeCPP`.

One incidental reading the step did not predict: `xnu_entry_kv_written` **fell** to 0x5B from 351's 0x75, while
`kv_in_dram` fell 0x99 → 0x7F. Both are counts of writes the instrument makes on the way out, and neither has
been monotone across the walk; they are read here only to say the instrument's own state is unchanged in kind.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301622 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## One claim corrected: "the pool" is a mangled-name fact

The 351 frontier said, of `StartIOKit`'s call list, that `IOPlatformExpertDevice`'s constructor "is defined
**nowhere in the 695-object pool**". It is defined there — `iokit_Kernel_IOPlatformExpert.o`, `T 0x38` at 0x2764
— and the 352 image carries the symbol as a stub (`T 0x18` at 0x80160B34) only because that object is not
linked yet. The sweep that produced the claim looked for the **C name**:
`nm --defined-only *.o | grep -w IOPlatformExpertDevice` matches nothing, because the pool holds
`_ZN22IOPlatformExpertDeviceC1Ev`. So it is 348's rule one turn further out — a body is an object fact, a stub
is an image fact, and **a definition is a mangled-name fact**. The frontier tool never had the bug (it compares
the image's undefined list against the pool's defined list, both mangled); the ad-hoc sweep around it did. The
correction is recorded in place in the 351 block and its document rather than deleted.

## Frontier: 353 — `iokit/Kernel/IOInterruptAccounting.cpp`

`iokit_Kernel_IOInterruptAccounting.o` is the only pool definer of this step's stop `_Z23interruptAccountingInitv`
(looked for under its mangled name, per the correction above): **4 resolved / 2 added** — three functions
(`_Z23interruptAccountingInitv`, the two `interruptAccountingData*Channels` entry points) and the `D 0x4`
stand-in `gInterruptAccountingStatisticBitmask` out; `IOSimpleReporter::getValue` and `::setValue` in, both
defined in the pool by `iokit_Kernel_IOSimpleReporter.o` and neither linked — for 850 → **848** undefined,
**746 → 745 function, 104 → 103 storage**. The object is small: `.text` 0x244 of nine definitions, `.data` 4,
`.bss` 0x10, `.rodata.str1.1` 0x15, and **no `.rodata`, no COMDAT and no `.init_array`**.

**Predicted stop: `interruptAccountingInit` at `StartIOKit+0x104`, key `0x8011b2a4`.** `StartIOKit`'s list of
calls past `devsw_init`, read in the 352 image:

```
8011b268: bl devsw_init                            0x18 STUB  <- 352's stop, retired here
8011b274: bl _ZN8OSSymbol17withCStringNoCopyEPKc   0xC8 real
8011b290: bl _ZN5OSSet11withObjectsEPPK8OSObjectjj 0x84 real
8011b2a0: bl interruptAccountingInit               0x18 STUB  <- 353's stop, key 0x8011b2a4
```

and the two real calls in between have been swept **two levels deep** — `OSSymbol::withCStringNoCopy` (one
`blx`) reaches `OSSymbolPool::insertSymbol` (0x280), `OSObject::operator new`, `OSString::initWithCStringNoCopy`,
`OSString::free`, `OSString::OSString(OSMetaClass const*)`, `OSMetaClass::instanceConstructed`,
`OSSymbolPool::findSymbol` and the two `lck_mtx_*` calls; `OSSet::withObjects` (two `blx`) reaches
`OSCollection::OSCollection(OSMetaClass const*)` — and **not one of them, at either level, names a stub**.
`interruptAccountingInit`'s own body, read from its object as 348's rule requires, is **0x60 bytes**:
`PE_parse_boot_argn` (8 bytes, real), `IOLockAlloc` (0x14, real, whose one call `lck_mtx_alloc_init` is real),
and then four stores. So the only code the step adds to the path is two calls, and both are real.
