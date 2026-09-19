# Experiment 337 — `OSIterator.cpp` linked: the stub 335 created, the frame that unwound, and a `.bss` term that is exact for the first time

**Step:** link one object, `libkern/c++/OSIterator.cpp` (`libkern_c++_OSIterator.o`, manifest:368) — the only
object in the pool that defines 336's stop, `_ZN10OSIteratorC2EPK11OSMetaClass`. Nothing else changes.

**Prediction:** *3 resolved / 0 added — 927 → **924** undefined, 814 → **812** function, 113 → **112**
storage; `.text` +0xC8 to +0xD3 plus a fill band, ending near 0x8014D568..0x8014D573, still short of the
0x80150000 boundary, so `.data` does not move; `.init_array` 0x30 → 0x34 with
`_GLOBAL__sub_I_OSIterator.cpp` at #12; `.bss`'s start unmoved, placed −0x28 with the fill read;* and **the
stop outside the object at `iokit_post_constructor_init+0x14` on `_ZN9IOService10initializeEv` — the
prediction 335 made one step too early.**

**Result:** the counts exact, `.data`, `.sysctl_set`, `.init_array` and `.bss`'s address all unmoved as
predicted, `.bss`'s placed term exactly −0x28 as predicted, and the run stops at
`stub_hit=_ZN9IOService10initializeEv` with `xnu_entry_stub_caller_v=0x8011b100` =
`iokit_post_constructor_init+0x14` — symbol, function and offset all as predicted, and for the first time
confirmed by the closure tool **before** the run. Two things did not land: the printed `.text` range was a
mis-sum of its own table (0x20 low), and the `.bss` fill went −0x18 where the prediction had left it open.

## The object

| `libkern_c++_OSIterator.o` | |
|---|---|
| `.text` | **188** (0x0BC), 5 functions |
| `.text._ZN10OSIterator9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **144** (0x90 — `_ZTV10OSIterator`, its `MetaClass` vtable, `gMetaClass`/`superClass`) |
| `.rodata.str1.1` | **11** (0x0B) |
| `.bss` | **24** (0x18 — `OSIterator::gMetaClass`) |
| `.init_array` | 4 (`_GLOBAL__sub_I_OSIterator.cpp`) |
| definitions / references | 16 / 28 |

**3 resolved, 0 added** — exactly the three names 335 added, two functions and one storage stand-in, with
all 28 references already satisfied:

| resolved | object | stand-in was |
|---|---|---|
| `_ZN10OSIterator10gMetaClassE` | `B`, 0x18 | `data B 0x18` |
| `_ZN10OSIteratorC2EPK11OSMetaClass` (33 chars) | `T` | `func T` |
| `_ZN10OSIteratorD2Ev` (19) | `T` | `func T` |

| | 336 | 337 | delta |
|---|---|---|---|
| undefined / function / storage | 927 / 814 / 113 | **924 / 812 / 112** | −3 / −2 / −1 |

This is the first step of the walk whose frontier is a stub **the walk itself created**, one run earlier,
and the first whose kind split was predicted with the pool-aware classifier 335's defect produced — right
both times it has been used.

## `.text`: six terms, and a printed range that was 0x20 low

| term | predicted | measured |
|---|---|---|
| this object's `.text` | +0x0BC | **+0x0BC** |
| its COMDAT | +0x004 | **+0x004** |
| its `.rodata`, placed whole | +0x090 | **+0x090** |
| its string bytes, as placed | +0x000 .. +0x00B | **+0x00B** — all 11, nothing deduped |
| the two retired stub bodies | −0x030 | **−0x030** |
| their two name slots | −0x038 | **−0x038** |

Sum **+0xF3** — which is the measured placed move (0x14C77D → **0x14C870**) — and the fill went 0xD23 →
**0xD30** (+0xD), so `.text` itself is **+0x100** (0x14D4A0 → **0x14D5A0**).

**The prediction printed "`+0xC8` to `+0xD3` … ends near 0x8014D568..0x8014D573", and its own six rows sum
to `+0xE8`..`+0xF3`.** The 0x20 was lost in adding the table, before the build, and the measured end
0x8014D5A0 landed 0x2D above the top of the printed range. This is 336's defect in a different column: there
a name length was eyeballed where it should have been counted; here the sum of the table's own terms was not
taken at all, and a prediction written as a *range* is exactly the shape a 0x20 slip hides in. 336's rule —
"the term is `align4(len+1)` over an exactly known string, so the length should be counted and not
eyeballed" — generalizes to **the range a prediction is printed as should be the sum of the rows above it,
and the rows should be added rather than estimated.**

`realstubs.o` measures the three stub terms a second time, and reading it needed the same care: its
`arm-none-eabi-size` `text` column reads **39371**, which is not its `.text` but `.text` +
`.rodata.str1.4` (0x4C20 + 0x4DAB) — the "a tool that reports a *total* will be read as a component" trap,
from the other side.

| `xnu_arm_entry_realstubs.o` | 336 | 337 | delta |
|---|---|---|---|
| `.text` | 19536 | **19488 (0x4C20)** | **−0x30** = 0x18 × 2 retired bodies |
| `.rodata.str1.4` | 19939 | **19883 (0x4DAB)** | **−0x38** = 0x24 + 0x14, the two slots |
| `.bss` | 7428 | **7364 (0x1CC4)** | **−0x40** = one retired 64-byte stand-in slot |

and the two slot numbers are the two name lengths **counted**: `align4(33+1)` = **0x24** for
`_ZN10OSIteratorC2EPK11OSMetaClass` and `align4(19+1)` = **0x14** for `_ZN10OSIteratorD2Ev`.

## The layout, unmoved from `.data` down

| | 336 | 337 | delta |
|---|---|---|---|
| `.text` | 0x8014D4A0 (0x14D4A0) | **0x8014D5A0 (0x14D5A0)** | +0x100 |
| `.data` | 0x80150000 (0x19408) | **0x80150000 (0x19408)** | **+0, unmoved** |
| `.sysctl_set` | 0x80169408 (0x10C) | **0x80169408 (0x10C)** | +0, size 0 |
| `.init_array` | 0x80169514 (0x30) | **0x80169514 (0x34)** | +0, **+4** |
| `.bss` | 0x80169580 (0x37B58) | **0x80169580 (0x37B18)** | **+0, size −0x40** |
| `__bss_end` | 0x801A10D8 | **0x801A1098** | −0x40 |
| image | 1480004 | **1480008** | +4 |
| headroom | 1437480 | **1437544** | +0x40 |

`.text` ends 0x8014D5A0, **0x2A60 below the 0x80150000 boundary**, so `.data` does not move — the second step
running that leaves the data block's address alone, and this object brings no `.data` at all: its only
writable section is `.bss`.

## `.bss`: the first exact storage term

| `libkern_c++_OSIterator.o` | placed | fill | section |
|---|---|---|---|
| 336 | 0x37A2B | 0x12D | 0x37B58 |
| 337 | **0x37A03** | **0x115** | **0x37B18** |
| delta | **−0x28** | **−0x18** | **−0x40** |

**−0x28 placed** is the whole term and it is exact because both halves are known: the retired storage
stand-in gives back its full **0x40-byte 64-aligned slot** for the 0x18 it held (331's rule, fifth time),
and `OSIterator::gMetaClass` — the same 0x18, at **0x8019F394** — arrives in its place. 0x40 out, 0x18 in,
and the prediction that said "not move" was written against the new slot and read the fill out loud. Every
previous storage step has been about the *direction* of a 0x40 quantum; this is the first where the number
is arithmetic on two known sizes.

`.bss`'s *start* stayed put the way 336's did: `.init_array` grew to 0x34, ending 0x80169548, whose `align64`
is still **0x80169580** — 332's idiom for the third step running.

`.init_array` is **0x34 — thirteen entries**, and the order is again what the step runs on:

```
0x80169514  _GLOBAL__sub_I_OSKext.cpp        0x80124248  _GLOBAL__sub_I_IOCPU.cpp
0x80169518  _GLOBAL__sub_I_OSMetaClass.cpp   0x80124fe4  _GLOBAL__sub_I_OSArray.cpp
0x8016951c  _GLOBAL__sub_I_OSDictionary.cpp  0x80129704  _GLOBAL__sub_I_IORegistryEntry.cpp
0x80169520  _GLOBAL__sub_I_OSObject.cpp      0x80129c04  _GLOBAL__sub_I_OSCollectionIterator.cpp
0x80169524  _GLOBAL__sub_I_OSCollection.cpp  0x80129c5c  _GLOBAL__sub_I_OSIterator.cpp  <- #12, new
0x80169528  _GLOBAL__sub_I_OSSymbol.cpp      0x80129cb4  last_kernel_constructor        <- #13, still last
0x8016952c  _GLOBAL__sub_I_OSString.cpp
```

## The stop, confirmed by the tool that failed in 335

The prediction was written from the object, and then checked with `xnu_entry_callwalk.py` against the built
image — from `iokit_post_constructor_init`, the root 335's broken walk had been supposed to cover:

```
$ ./tools/xnu_entry_callwalk.py --root iokit_post_constructor_init

walk from iokit_post_constructor_init:
  iokit_post_constructor_init
    _ZN9IOService10initializeEv   STUB

first stub on the straight-line path: _ZN9IOService10initializeEv
  the run should stop with stub_hit=_ZN9IOService10initializeEv
```

and from `inPlane` — the frame 335's run actually died in — the same tool now reports **no stub on the
straight-line path at all**: the six indirect calls in its body are *listed* rather than walked past, and
the one edge to `withCollection` is now a real function. The `bl` the prediction named is
`0x8011b0fc: bl 8012dc74 <_ZN9IOService10initializeEv>`, the third of the six calls in
`iokit_post_constructor_init`'s straight line, so the return address is +0x14.

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_why=0x8012f17c
 xnu_entry_why_byte=0x00000061                       <- 'a', the epilogue's stub-hit text
 xnu_entry_stub_caller_v=0x8011b100                   (also _w0=0x31313038 "8011", _w1=0x30303162 "b100")
 xnu_entry_stub_caller_digits=0x0000003f
 xnu_entry_stub_caller=0x8011b100                     (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000                   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN9IOService10initializeEv
```

`tools/host_resolve_entry_addr.sh 0x8011b100` → **`iokit_post_constructor_init+0x14`**, `caller-4` =
`0x8011b0fc: bl 8012dc74 <_ZN9IOService10initializeEv>`. Symbol, function and offset were all predicted
before the build and all confirmed by an independent tool before the run.

**What the one key measures beyond the stop.** The key is two calls forward in the same frame as 334's
(`+0x0C` → `+0x14`), so reaching it measures that `IORegistryEntry::initialize` **returned** — and its
return is what 335 and 336 were both about. To get out of it the run had to take `setProperty`'s `inPlane()`
path, build the iterator via `OSCollectionIterator::withCollection`, run `OSIterator::OSIterator` (the stub
of the previous step, real now: `OSObject::OSObject` and then the vtable store — five instructions), walk the
registry table through `getNextObject`, and store the dictionary. A fault anywhere along that chain would
have been a `data abort` with `abort_entries=1`; instead the frames unwound and the next call was taken. So
this step measured the *completion* of the registry chain, not one more symbol — which is a different kind
of result from the counts and the section arithmetic.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img` (4394 KB), nothing flashed, `25`
records of `persistent_write_attempted=0x00000000` and `87` of `failure_mask=0x00000000` with **no non-zero
reading of either**, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301639 bytes, and the device back on
Android on its own (`MI 4LTE`, release 10).

## What this step measures

The frontier is `_ZN9IOService10initializeEv`, defined by `iokit/Kernel/IOService.cpp`
(`iokit_Kernel_IOService.o`, manifest:339) — and it is by far the largest object this walk has been asked to
link: **553 definitions against 278 references**, `.text` **0xC34C** (50000 bytes), nine COMDAT
`MetaClassD0Ev` entries, and the whole IOService / IOResources / IONotifier / `_IOServiceNotifier` /
`_IOServiceJob` / `_IOConfigThread` / `_IOOpenServiceIterator` class family inside it.
`tools/entry_object_effect.py` gives it **102 resolved (94 function, 8 storage) and 27 added (21 function,
6 storage)** — so 338 is 924 → **849 undefined, 812 → 739 function, 112 → 110 storage**, with the six
created storage stand-ins each costing a full 0x40 `.bss` slot. Its `.text` alone is 0xC34C, which would
take the section from 0x8014D5A0 to about **0x80159900** — past the 0x80150000, 0x80154000 and 0x80158000
boundaries at once — so `.data` and everything above it is expected to step to the next 16 KB boundary above
the new end: **0x8015C000** if the fill and strings fit, **0x80160000** if they do not. That is the first
step of the walk whose `.data` move is not a single boundary but a choice between two, and the arithmetic
that decides it is the same six-term `.text` table this step closed.
