# Experiment 338 — `IOService.cpp` linked: the largest object of the walk, `.data` three boundaries away, and a stop 0x4D4 bytes into the biggest initializer in IOKit

**Step:** link one object, `iokit/Kernel/IOService.cpp` (`iokit_Kernel_IOService.o`, manifest:339) — the only
object in the pool that defines 337's stop, `_ZN9IOService10initializeEv`. Nothing else changes.

**Prediction:** *102 resolved / 27 added — 924 → **849** undefined, 812 → **739** function, 112 → **110**
storage; `.text` +0xBE94 to +0xCF68 plus a fill band, ending between 0x80159434 and 0x8015A538, so `.data`
steps **three** 16 KB boundaries to **0x8015C000**; `.init_array` 0x34 → 0x38 with
`_GLOBAL__sub_I_IOService.cpp` at #13; `.bss` 0x80175580 with its placed bytes exactly **+0x1BD**;* and **the
stop at `_ZN6OSData15withBytesNoCopyEPvj` on `_ZN9IOService10initializeEv+0x4D8`, 0x4D4 bytes into the
function after a straight line of 51 calls made of only three symbols.**

**Result:** the counts exact, every one of the eight `.text` terms closed, `.data` at **0x8015C000**, `.bss`
placed exactly **+0x1BD**, the `.init_array` fourteen entries as predicted, and the run stops at
`stub_hit=_ZN6OSData15withBytesNoCopyEPvj` with `xnu_entry_stub_caller_v=0x8012a9b0` =
`_ZN9IOService10initializeEv+0x4d8` — symbol and offset both as predicted, and both confirmed before the run
by the closure tool. Two *derived* rows were wrong: the image and the headroom, both read off the wrong
expression.

## The object

| `iokit_Kernel_IOService.o` | |
|---|---|
| `.text` | **49996** (0xC34C), 176 functions |
| nine COMDAT `MetaClassD0Ev` | 4 each (0x024) |
| `.rodata` | **3260** (0xCBC) |
| `.rodata.str1.1` | **4308** (0x10D4) |
| `.bss` | **573** (0x23D) |
| `.init_array` | 4 (`_GLOBAL__sub_I_IOService.cpp`) |
| definitions / references | 553 / 278 |

It carries the whole `IOService` class family — `IOService`, `IOResources`, `IONotifier`, `_IOServiceNotifier`,
`_IOServiceNullNotifier`, `_IOServiceInterestNotifier`, `_IOServiceJob`, `_IOConfigThread`,
`_IOOpenServiceIterator` and the matching and power-state machinery — and it has **no `__DATA, __data` and no
`.data` of any kind**, only `.bss`. It is the largest single input this walk has linked, by a factor of nearly
four over 335's registry entry.

**102 resolved / 27 added** — 94 function and 8 storage retired, 21 function and 6 storage created:

| | 337 | 338 | delta |
|---|---|---|---|
| undefined / function / storage | 924 / 812 / 112 | **849 / 739 / 110** | −75 / −73 / −2 |

The six created storage names are all four-byte keys the pool defines outside this link:
`_ZN12IOUserClient9metaClassE`, `_ZN5OSSet9metaClassE`, `_ZN9IOCommand9metaClassE`, `gIODTPlane`,
`gIOProbeScoreKey` and `gInterruptAccountingStatisticBitmask`.

## `.text`: eight terms, no residual, on the largest object of the walk

| term | predicted | measured |
|---|---|---|
| this object's `.text` | +0xC34C | **+0xC34C** |
| its nine COMDATs | +0x024 | **+0x024** |
| its `.rodata`, placed whole | +0xCBC | **+0xCBC** |
| its string bytes, as placed | +0x000 .. +0x10D4 | **+0xFE8** of 0x10D4 (236 bytes deduped) |
| the 94 retired stub bodies | −0x8D0 | **−0x8D0** |
| their 94 name slots | −0xEEC | **−0xEEC** |
| the 21 created stub bodies | +0x1F8 | **+0x1F8** |
| their 21 name slots | +0x42C | **+0x42C** |

Sum **+0xCE84**, exactly the measured placed move (0x14C870 → **0x1596F4**), with the fill going 0xD30 →
**0xD2C** (−0x4) so `.text` is **+0xCE80** (0x14D5A0 → **0x15A420**) — inside the predicted range, 0x118 below
its top. Four of the eight terms are counts of stand-ins rather than of files, and the created half is finally
large enough to matter: 21 bodies, 21 slots and six 64-byte `.bss` slots.

`realstubs.o` measures the terms a second time, by section:

| `xnu_arm_entry_realstubs.o` | 337 | 338 | delta |
|---|---|---|---|
| `.text` | 19488 (0x4C20) | **17736 (0x4548)** | **−0x6D8** = 0x18 × (94 − 21) |
| `.rodata.str1.4` | 19883 (0x4DAB) | **17132 (0x42EC)** | **−0xABF** |
| `.bss` | 7364 (0x1CC4) | **7236 (0x1C44)** | **−0x80** = (8 − 6) slots × 0x40 |

**The string section is one byte off the model, and the byte is a tail artifact — the check that shows it is
the one this walk has used since 322.** Summing `align4(len+1)` over each build's *function* stub names gives
**19884** for 337's 812 names and **17132** for 338's 739: the model reproduces each absolute size to within a
byte (19884 against a measured 19883 on the 337 side, exact on the 338 side), the two name sets differ by
exactly **0xAC0**, and the sections differ by 0xABF because the one unpadded byte sits on the 337 end. It is how
the mergeable string section pads its last string, not the name arithmetic: compute the sum over the *names*,
not the difference over the *sections*.

## `.data` three boundaries away, and the margin that decided it

| | 337 | 338 | delta |
|---|---|---|---|
| `.text` | 0x8014D5A0 (0x14D5A0) | **0x8015A420 (0x15A420)** | +0xCE80 |
| `.data` | 0x80150000 (0x19408) | **0x8015C000 (0x19408)** | **+0xC000, size and fill +0** |
| `.sysctl_set` | 0x80169408 (0x10C) | **0x80175408 (0x10C)** | +0xC000, size 0 |
| `.init_array` | 0x80169514 (0x34) | **0x80175514 (0x38)** | +0xC000, **+4** |
| `.bss` | 0x80169580 (0x37B18) | **0x80175580 (0x37CD8)** | +0xC000, size +0x1C0 |
| `__bss_end` | 0x801A1098 | **0x801AD258** | +0xC1C0 |
| image | 1480004 | **1529164** | +0xC0A0 |
| headroom | 1437480 | **1387944** | −0xC1C0 |

The rule the last four steps measured is that `.data` lands on `round_up(__entry_text_end, 0x4000)` — the
linker's page size for this target — and the new `.text` end at 0x8015A420 is inside the
0x80158000..0x8015C000 window, so `.data` jumped **three** boundaries at once. This is the first step where
that was a *choice between two boundaries* rather than simply the next one up, which is why the prediction
stated the margin as well as the answer: `.text` would have had to grow another **0x5BE0** past where it
landed before 0x80160000 became the answer, and it did not.

`.data`'s own size and fill did not move at all (0x19408 and 0x7AAB in both builds) — the object brings no
`.data`, so the whole of the +0xC000 is the address.

**Two prediction rows were wrong, and they are the same defect 337 recorded in a smaller key.** `image` was
stated as ~1529268 and is **1529164**: the image is `__entry_init_array_end - ENTRY_BASE` and the prediction
used `.bss`'s start + 0x34 instead, 0x68 too high. `__bss_end` was stated as ~0x801AD140, which is `.bss`'s
start + its *placed* bytes with the fill left out; the measured 0x801AD258 is that plus 0x118 of fill, and the
headroom followed it to 1387944 where the prediction said ~1388544. Neither was a term in an identity; both
were derived numbers read off a different expression than the one the build uses. 337's rule — *the range a
prediction prints should be the sum of the rows above it* — is the same rule stated about a sum; this is its
form about a **derivation**, and the fix is the same: name the expression, not the number.

## `.bss`: placed exact for the second step running

| `iokit_Kernel_IOService.o` | bytes | |
|---|---|---|
| eight retired storage stand-ins | **−0x200** | 8 × a full 0x40-byte 64-aligned slot |
| the object's own `.bss` | **+0x23D** | 573 bytes, one map line |
| six created storage stand-ins | **+0x180** | 6 × 0x40 |
| **placed** | **+0x1BD** | 0x37A03 → **0x37BC0**, exact |
| fill | **+0x3** | 0x115 → **0x118** |
| **section** | **+0x1C0** | 0x37B18 → **0x37CD8** |

All three parts were known before the build, and the whole term is a count of slots rather than a statement
about direction — 331's rule used as arithmetic for the second step running. `realstubs.o`'s `.bss` is the same
statement at object scope: **−0x80** for the two net slots.

`.init_array` is **0x38 — fourteen entries**, with `_GLOBAL__sub_I_IOService.cpp` at #13 and
`last_kernel_constructor` still last at #14:

```
0x80175514  _GLOBAL__sub_I_OSKext.cpp        0x80175530  _GLOBAL__sub_I_IOCPU.cpp
0x80175518  _GLOBAL__sub_I_OSMetaClass.cpp   0x80175534  _GLOBAL__sub_I_OSArray.cpp
0x8017551c  _GLOBAL__sub_I_OSDictionary.cpp  0x80175538  _GLOBAL__sub_I_IORegistryEntry.cpp
0x80175520  _GLOBAL__sub_I_OSObject.cpp      0x8017553c  _GLOBAL__sub_I_OSCollectionIterator.cpp
0x80175524  _GLOBAL__sub_I_OSCollection.cpp  0x80175540  _GLOBAL__sub_I_OSIterator.cpp
0x80175528  _GLOBAL__sub_I_OSSymbol.cpp      0x80175544  _GLOBAL__sub_I_IOService.cpp   <- #13, new
0x8017552c  _GLOBAL__sub_I_OSString.cpp      0x80175548  last_kernel_constructor       <- #14, still last
```

`.bss`'s start is `align64` of `.init_array`'s end 0x8017554C → **0x80175580** — 332's idiom for the fourth step
running.

## The stop, and the longest straight line this walk has bet on

The prediction came from reading the object's own disassembly rather than from a tool: `IOService::initialize`
opens with a straight line of **51 calls made of only three symbols** — `IORegistryEntry::makePlane` twice,
**48** × `OSSymbol::withCStringNoCopy`, one `OSDictionary::withCapacity` — and its **52nd** call, 0x4D4 bytes
into the function, is the first this image cannot provide:

```
 7bc <_ZN9IOService10initializeEv>:  push {r4, r5, fp, lr}
  +0x10   7cc  bl <_ZN15IORegistryEntry9makePlaneEPKc>        real since 335
  +0x28   7e4  bl <_ZN15IORegistryEntry9makePlaneEPKc>        real since 335
  +0x40 .. +0x310   31 x bl <_ZN8OSSymbol17withCStringNoCopyEPKc>   real since 331
  +0x324  ae0  bl <_ZN12OSDictionary12withCapacityEj>        real since 330
  +0x33C .. +0x4B8   17 x bl <_ZN8OSSymbol17withCStringNoCopyEPKc>   real since 331
  +0x4D4  c90  bl <_ZN6OSData15withBytesNoCopyEPvj>          STUB   <- return address +0x4D8
```

and the tool agreed, before the device was touched:

```
$ ./tools/xnu_entry_callwalk.py --root _ZN9IOService10initializeEv

walk from _ZN9IOService10initializeEv:
  _ZN9IOService10initializeEv
    _ZN6OSData15withBytesNoCopyEPvj   STUB

first stub on the straight-line path: _ZN6OSData15withBytesNoCopyEPvj
```

Run from `iokit_post_constructor_init` it gives the same stub two frames below its root — the first time a
pre-run walk has resolved a stop two frames down.

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_stub_caller_v=0x8012a9b0                   (also _w0=0x32313038 "8012", _w1=0x30623961 "a9b0")
 xnu_entry_stub_caller_digits=0x00000043
 xnu_entry_stub_caller=0x8012a9b0                     (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000                   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN6OSData15withBytesNoCopyEPvj
```

`tools/host_resolve_entry_addr.sh 0x8012a9b0` → **`_ZN9IOService10initializeEv+0x4d8`**, and in the linked image
`0x8012a9ac: bl 8013a0a4 <_ZN6OSData15withBytesNoCopyEPvj>`.

**What the one key measures.** It is the longest straight-line run this ledger has ever bet on: reaching
`+0x4D8` measures that both `IORegistryEntry::makePlane` calls (two `IORegistryPlane` objects built and linked
into the registry root), all **48** `OSSymbol::withCStringNoCopy` calls (each running the symbol pool's
`findSymbol` with its `strncmp` and lock, and `OSString::initWithCString` plus `insertSymbol` on a miss) and the
`OSDictionary::withCapacity` all returned — 52 calls and their transitive bodies — with `abort_entries=0`
saying nothing faulted anywhere in them. It also measures 0x4D4 bytes of a 176-function object executing, and
the globals it built are the keys the rest of IOKit matches on: `gIOServicePlane`, `gIOPowerPlane`,
`gIOProviderClassKey`, `gIONameMatchKey`, `gIONameMatchedKey`, `gIOPropertyMatchKey`,
`gIOPropertyExistsMatchKey`.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img` (4442 KB), nothing flashed, `25`
records of `persistent_write_attempted=0x00000000` and `87` of `failure_mask=0x00000000` with **no non-zero
reading of either**, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301643 bytes, and the device back on
Android on its own (`MI 4LTE`, release 10).

## What this step measures

The frontier is `OSData::withBytesNoCopy`, from `libkern/c++/OSData.cpp` (`libkern_c++_OSData.o`) — a much
smaller object than this one. What makes it worth its own step is the call rather than the object: the
disassembly puts `movw r0, <_ZL19gIOConsoleUsersSeed>` / `movt r0, #0x801a` / `mov r1, #4` in front of it, so it
is

```c
gIOConsoleUsersSeedValue = OSData::withBytesNoCopy(&gIOConsoleUsersSeed, sizeof(gIOConsoleUsersSeed));
```

(`IOService.cpp:387`, with the static declared at `:199`) — an `OSData` built over a **four-byte file-static
inside this image's own `.bss`**, at 0x801a…. Three things about that are checkable and each is worth having:
the static is this object's own, so its address is decided by the `.bss` arithmetic this ledger now predicts
exactly; `withBytesNoCopy` **shares** the buffer rather than copying it (`OSData.h:303`, `OSData.cpp:161`), so
the image and the `OSData` alias one address; and nothing frees it — the two-argument form supplies no dealloc
function, and `OSData::free()` runs one only when `capacity == EXTERNAL` *and* a function was given
(`OSData.cpp:200-213`). That third claim is the kind this project has learned to check in the source rather
than assume, and it is why the next step is about what `OSData` does with a buffer it does not own.
