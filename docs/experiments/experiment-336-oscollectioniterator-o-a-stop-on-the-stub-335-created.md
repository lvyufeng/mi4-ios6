# Experiment 336 — `OSCollectionIterator.cpp` linked: the smallest count change of the walk, and a stop on the stub 335 created

**Step:** link one object, `libkern/c++/OSCollectionIterator.cpp` (`libkern_c++_OSCollectionIterator.o`,
manifest:365) — the only object in the pool that defines 335's stop,
`_ZN20OSCollectionIterator14withCollectionEPK12OSCollection`. Nothing else changes.

**Prediction:** *1 resolved / 0 added — 928 → **927** undefined, 815 → **814** function, 113 → **113**
storage; `.text` +0x4AC to +0x4C1 plus a fill band, still 0x2B20 short of the 0x80150000 boundary, so
`.data` does not move; `.init_array` 0x2C → 0x30 with `_GLOBAL__sub_I_OSCollectionIterator.cpp` at #11;
`.bss` at the `align64` of `.init_array`'s end, **0x80169580**;* and **the stop inside the newly real
function, at `withCollection+0x24` on `_ZN10OSIteratorC2EPK11OSMetaClass`.**

**Result:** **all of it.** `927 symbol(s) undefined` / `814 function` / `113 storage`, every address and
section as predicted, and the run stops at `stub_hit=_ZN10OSIteratorC2EPK11OSMetaClass` with
`xnu_entry_stub_caller_v=0x80129954` = `_ZN20OSCollectionIterator14withCollectionEPK12OSCollection+0x24` —
the symbol predicted and the offset predicted, both confirmed independently before the device run.

## The object

| `libkern_c++_OSCollectionIterator.o` | |
|---|---|
| `.text` | **1124** (0x464), 9 functions |
| `.text._ZN20OSCollectionIterator9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **148** (0x94 — `_ZTV20OSCollectionIterator`, its `MetaClass` vtable, `gMetaClass`/`superClass`) |
| `.rodata.str1.1` | **21** (0x15) |
| `__DATA, __data` | **48** (0x30) |
| `.bss` | **24** (0x18 — `OSCollectionIterator::gMetaClass`) |
| `.init_array` | 4 (`_GLOBAL__sub_I_OSCollectionIterator.cpp`) |
| definitions / references | 27 / 34 |

**1 resolved, 0 added** — the single function stub `withCollection`, with all 34 references already
satisfied, including the two base-class constructors `OSIterator::OSIterator` and `OSObject::OSObject`.
This is the smallest count change the walk has made:

| | 335 | 336 | delta |
|---|---|---|---|
| undefined / function / storage | 928 / 815 / 113 | **927 / 814 / 113** | −1 / −1 / 0 |

It is also the first step whose kind split was predicted with the pool-aware classifier the 335 defect
produced, and the first where that prediction was right.

## `.text`: six terms, no residual

| term | bytes |
|---|---|
| this object's `.text` | **+0x464** |
| its COMDAT | **+0x004** |
| its `.rodata`, placed whole | **+0x094** |
| its string bytes, as placed | **+0x015** — all 21, the first step since 328 with nothing deduped |
| the one retired stub body | **−0x018** |
| its one name slot | **−0x03C** (`align4(58+1)`) |

Sum **+0x4BD**, which is the measured placed total 0x14C2C0 → **0x14C77D**, and with the fill going 0xD20 →
**0xD23** the section is **+0x4C0** (0x14CFE0 → **0x14D4A0**).

**The prediction's slot term said 0x38 and the answer is 0x3C: the name is 58 characters and the ledger said
55.** That is the whole of the difference between the predicted range 0x8014D48C..0x8014D4A1 and the measured
0x8014D4A0, and the range contained it only because four bytes happened to be smaller than the string
uncertainty the term was added to. The rule that catches this is 322's, and it needs no new idea: the term is
`align4(len+1)` over an exactly known string, so the length should be *counted* and not eyeballed.

`realstubs.o` confirms the two retired terms a second time — `.text` 19560 → **19536** (−0x18) and
`.rodata.str1.4` 19999 → **19939** (−0x3C) — and its `.bss` is **unmoved at 7428**, the section that moved
0x40 in 335 and moves nothing here, because this step resolves one *function* stub and touches no storage.

## The layout, unmoved from `.data` down

| | 335 | 336 | delta |
|---|---|---|---|
| `.text` | 0x8014CFE0 (0x14CFE0) | **0x8014D4A0 (0x14D4A0)** | +0x4C0 |
| `.data` | 0x80150000 (0x193D8) | **0x80150000 (0x19408)** | **+0, size +0x30** |
| `.sysctl_set` | 0x801693D8 (0x10C) | **0x80169408 (0x10C)** | +0x30, size 0 |
| `.init_array` | 0x801694E4 (0x2C) | **0x80169514 (0x30)** | +0x30, **+4** |
| `.bss` | 0x80169540 (0x37B18) | **0x80169580 (0x37B58)** | **+0x40, size +0x40** |
| `__bss_end` | 0x801A1058 | **0x801A10D8** | +0x80 |
| image | 1479952 | **1480004** | +0x34 |
| headroom | 1437608 | **1437480** | −0x80 |

`.text` now ends 0x8014D4A0, **0x2B60 below the 0x80150000 boundary**, so `.data` does not move — the first
step in four that leaves the data block's address alone. `.data`'s **+0x30** is this object's `__DATA, __data`
whole (placed 0x1192D → **0x1195D**) with the fill *unmoved* at **0x7AAB**, which is 334's shape rather than
335's (where the fill moved with the contribution). `.bss`'s **+0x40** is placed's +0x18 — the object's own
`gMetaClass`, 24 bytes — plus fill's **+0x28** (0x105 → **0x12D**), the seventh sign of the fill's move in
this walk.

`.init_array` is **0x30 — twelve entries**, and the order is again what the step runs on:

```
0x80169514  _GLOBAL__sub_I_OSKext.cpp        0x80124248  _GLOBAL__sub_I_IOCPU.cpp
0x80169518  _GLOBAL__sub_I_OSMetaClass.cpp   0x80124fe4  _GLOBAL__sub_I_OSArray.cpp
0x8016951c  _GLOBAL__sub_I_OSDictionary.cpp  0x80129704  _GLOBAL__sub_I_IORegistryEntry.cpp
0x80169520  _GLOBAL__sub_I_OSObject.cpp      0x80129c04  _GLOBAL__sub_I_OSCollectionIterator.cpp  <- #11, new
0x80169524  _GLOBAL__sub_I_OSCollection.cpp  0x80129c5c  last_kernel_constructor                 <- #12, still last
0x80169528  _GLOBAL__sub_I_OSSymbol.cpp
0x8016952c  _GLOBAL__sub_I_OSString.cpp
```

`.bss` starts at the `align64` of `.init_array`'s end, 0x80169544 → **0x80169580** — 332's idiom for the
fourth step running, and now the routine way the start of `.bss` is decided.

## The stop, checked twice before the device run

The prediction was written from the object, and then the closure claim was checked independently with the
project's own tool, which walks the transitive closure of **direct calls and tail branches** and reports the
indirect calls it cannot follow rather than walking past them:

```
$ ./tools/xnu_entry_callwalk.py --root _ZN20OSCollectionIterator14withCollectionEPK12OSCollection

walk from _ZN20OSCollectionIterator14withCollectionEPK12OSCollection:
  _ZN20OSCollectionIterator14withCollectionEPK12OSCollection
    _ZN10OSIteratorC2EPK11OSMetaClass   STUB

first stub on the straight-line path: _ZN10OSIteratorC2EPK11OSMetaClass
  the run should stop with stub_hit=_ZN10OSIteratorC2EPK11OSMetaClass
```

Its guarded list — what to read if the device disagrees — runs through `zalloc_internal`
(`OSObject::operator new` → `kern_os_malloc` → `zalloc`), each entry needing one or two guards to be taken.

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_why=0x8012f0ec
 xnu_entry_why_byte=0x00000061                       <- 'a', the epilogue's stub-hit text
 xnu_entry_stub_caller_v=0x80129954                   (also _w0=0x32313038 "8012", _w1=0x34353939 "9954")
 xnu_entry_stub_caller_digits=0x00000045
 xnu_entry_stub_caller=0x80129954                     (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000                   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN10OSIteratorC2EPK11OSMetaClass
```

`tools/host_resolve_entry_addr.sh 0x80129954` → **`_ZN20OSCollectionIterator14withCollectionEPK12OSCollection+0x24`**,
`caller-4` = `0x80129950: bl 8012d6e0 <_ZN10OSIteratorC2EPK11OSMetaClass>`. The symbol and the offset were
both predicted before the build, and both confirmed by an independent tool before the run — which is the
shape 335's failure bought.

**What the one key measures beyond the stop.** `withCollection`'s *first* call is `OSObject::operator new`
(`mov r0, #24`; `bl` at +0x0C), so reaching +0x24 means the allocator returned 24 bytes through the
`kern_os_malloc`/`zalloc` path whose guards the callwalk listed. The stop is one instruction after the
base-class constructor's `bl`: the walk has linked the object that **calls** `OSIterator::OSIterator` before
the object that **defines** it — the stub 335 created one step ago.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, `25` records of
`persistent_write_attempted=0x00000000` and `87` of `failure_mask=0x00000000` with **no non-zero reading of
either**, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301645 bytes, and the device back on Android on
its own (`MI 4LTE`, release 10).

## What this step measures

The frontier is `_ZN10OSIteratorC2EPK11OSMetaClass`, from `libkern/c++/OSIterator.cpp`
(`libkern_c++_OSIterator.o`, manifest:368) — and this is the **first step of the walk whose frontier is a stub
the walk itself created**, rather than one the image has carried since before it. The object is the smallest
kind there is: `.text` 188 bytes, `.rodata` 144, `.bss` 24, 16 definitions and 28 references, resolving
**exactly the three names 335 added** — `OSIterator::gMetaClass` (storage, 0x18), `OSIterator::OSIterator`
and `OSIterator::~OSIterator` — for **0 added**. So 337 is 927 → **924 undefined, 814 → 812 function,
113 → 112 storage**, and it should unblock `OSCollectionIterator`'s whole body in one move: `OSIterator` is an
abstract base class with no data members, so what arrives is a base-class constructor chain and a `MetaClass`
call.
