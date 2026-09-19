# Experiment 364 — `IOMapper.cpp`: the stop 363 made reachable, and the walk four statements deeper into the platform expert

**Step:** link one object, `iokit/Kernel/IOMapper.cpp` (`iokit_Kernel_IOMapper.o`) — the pool definer of 363's
stop `IOMapper::setMapperRequired(bool)`. It is inserted into the entry link between
`osfmk_kern_sched_average.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else changes.

**Prediction:** *3 resolved (2 function, 1 storage) / 0 added — 786 → **783** undefined, 682 → **680** function,
104 → **103** storage;* `.text` ending **0x801A4D60**; `.data` **0x801A8000** (0x19A60) — the first `.data` move
in five steps, and it is a whole `ALIGN(0x4000)`; `.sysctl_set` **0x801C1A60** (0x150); `.init_array`
**0x801C1BB0** (**0x80**, thirty-two); `.bss` **0x801C1C40** (**0x390D8**); `__bss_end` **0x801FAD18**; image
**1842224**; headroom **1069800**; `args` **+2080768**; `topOfKernelData` **+3145728**; and the stop at
**`_ZN16IORangeAllocator9withRangeEmmmm`** at key **`0x8015D98C`**.

**Result:** all three counts exact, the layout exact everywhere the arithmetic was load-bearing, and **the stop
name and key exactly as predicted**, with no falsifier firing. `IOMapper::setMapperRequired` ran and *returned*,
so Apple's `IOPlatformExpert::start` — which the step before last only just entered — is now four statements
further along, at the physical-range allocator. The two misses are one cause: this is the first step in a long
while that changes the size of the **stand-in object** itself, and the layout model reads its input sizes from a
map.

## Why this object, and why it leaves immediately

363 stopped at `setMapperRequired` from `IOPlatformExpert::start` at key `0x8015D8FC`. `entry_object_effect.py`
against the 363 image gives the whole step:

```
resolved (3: 2 function, 1 storage)
    _ZN8IOMapper17setMapperRequiredEb        object T, stand-in was func T
    _ZN8IOMapper19waitForSystemMapperEv      object T, stand-in was func T
    _ZN8IOMapper7gSystemE                    object D, stand-in was data D 0x4
added (0: 0 function, 0 storage)
of the 257 references, 257 are already satisfied
```

`waitForSystemMapper` coming real alongside it is the useful half: `IOMapper::get()` is
`if ((uintptr_t) gSystem & kWaitMask) waitForSystemMapper();` (`IOMapper.h:95`), `kWaitMask` is 3,
`setMapperRequired(false)` writes 0 and `(true)` writes 2 — so a zero `gSystem` never enters that loop, and the
second stop this object could have caused cannot happen.

The object's own body, disassembled from the pool object, is eleven instructions and every callee is already real:

```
16c: push {r4,r5,fp,lr}; cmp r0,#0; beq 18c
178: movw/movt r0, #IOMapper::gSystem ; mov r1,#2 ; str r1,[r0] ; pop {...pc}
18c: ldr r0,[r5]                  ; fWaitLock
198: bl lck_mtx_lock
1a0: ... gSystem = 0 ...
1b0: bl lck_mtx_unlock
1c4: b  IOLockWakeup              ; tail call
```

**A name that appears in no symbol list at all, and why.** `IOLockLock` and `IOLockUnlock` are declared in
`iokit/IOKit/IOLocks.h:99-126` behind `#ifdef IOLOCKS_INLINE`; when it is defined they are **macros** to
`lck_mtx_lock`/`lck_mtx_unlock`, which is the case for this build. So the names exist nowhere — not defined, not
undefined, not in the stub list — and a `grep` for them is a false negative rather than a missing stub. What has
to be checked instead is `lck_mtx_lock`, `lck_mtx_unlock` and `IOLockWakeup`, and all three are real.

`hasMapper` is false: the boot args carry no `dart` (so `PE_parse_boot_argn` at 0x8015D8A8 returns false and
`removeProperty` is skipped), and the root nub has no `IOPlatformMapperPresent` property. `sMapperLock`'s
`fWaitLock` is a live lock because the object's own static constructor `_GLOBAL__sub_I_IOMapper.cpp` is an
`.init_array` entry that runs at image start and calls `IOLockAlloc` — real. That is what rules out a data abort
from `lck_mtx_lock(NULL)`.

## What the stop means

```
 xnu_entry_why=0x8017f548      xnu_entry_why_byte=0x00000061      xnu_entry_kv_dropped=0x00000000
 xnu_entry_stub_caller_v=0x8015d98c     xnu_entry_abort_entries=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN16IORangeAllocator9withRangeEmmmm
```

`xnu_entry_stub_caller_v` is `caller-4` = **0x8015D988**, the `bl` at `IOPlatformExpert::start + 0x114` — the
same address in both images, because the object is linked after `IOPlatformExpert.o` and this caller is in an
object the step does not touch. `why` is the message string again (`why_byte` 0x61 is its `a`), so this is the
stub-hit path, and there is no panic.

Everything between the two stops is now **real and returning**, in source order
(`iokit/Kernel/IOPlatformExpert.cpp:137-143`):

```
8015d900  bl  OSDictionary::withCapacity(1)                                        real
8015d910  bl  IOLockAlloc                                                          real
8015d92c  bl  OSData::withBytesNoCopy(&bus_clock_rate_hz, 4)                       real
8015d94c  blx r3   provider->setProperty("clock-frequency", busFrequency)          virtual, real
8015d95c  blx r1   busFrequency->release()  = _ZNK8OSObject7releaseEv              virtual, real
8015d968  bl  OSSymbol::withCStringNoCopy("IOPlatformInterruptController")         real
8015d988  bl  IORangeAllocator::withRange(0xffffffff, 1, 16, ...)                  STUB
```

The `.rodata`-looking detail worth keeping: `release()` mangles to `_ZNK8OSObject7releaseEv` (it is `const`) and
sits at vtable slot +0x14 = 0x80120C24. A search for `_ZN8OSObject7releaseEv` finds nothing and would suggest the
virtual call is a stub; the const is the whole difference.

So the frontier is now the **physical range allocator** `IOPlatformExpert::start` builds before
`configure(provider)`, and the step after this one is the pool object defining
`IORangeAllocator::withRange`.

## What the layout did

| | 363 measured | 364 predicted | 364 measured |
|---|---|---|---|
| counts | 786 / 682 / 104 | **783 / 680 / 103** | **783 / 680 / 103** |
| `.text` end | 0x801A3D80 | **0x801A4D60** | **0x801A4CE0** (−0x80) |
| `.data` | 0x801A4000 (0x19A58) | **0x801A8000 (0x19A60)** | **0x801A8000 (0x19A58)** |
| object's `.data` word | — | **0x801C0830** | **0x801C0830** |
| `.sysctl_set` | 0x801BDA58 (0x150) | **0x801C1A60** | **0x801C1A58** (0x150) |
| `.init_array` | 0x801BDBA8 (0x7C) | **0x801C1BB0 (0x80)** | **0x801C1BA8** (0x80, thirty-two) |
| object's entry | — | **0x801C1C24** | **0x801C1C1C**, sentinel still last at 0x801C1C24 |
| its end | 0x801BDC24 | **0x801C1C30** | **0x801C1C28** |
| `.bss` | 0x801BDC40 (0x39118) | **0x801C1C40 (0x390D8)** | **0x801C1C40** (0x390D8) |
| object's `.bss` | — | **0x801F8540 (0x1C)** | **0x801F8540** (0x1C), expert to 0x801F855C |
| `realstubs.o` `.bss` | 0x801F4580 (0x27C4) | 0x801F8580 (0x2784) | **0x801F8580** (0x2784) |
| `__bss_end` | 0x801F6D58 | **0x801FAD18** | **0x801FAD18** |
| image | 1825828 | **1842224** | **1842216** |
| headroom | 1086120 | **1069800** | **1069800** |
| `args` | +2064384 | **+2080768** | **+2080768** |
| `topOfKernelData` | +3145728 | **+3145728** | **+3145728** |

And the new object's own pieces — every `.text`-run row exact, every `.rodata`-run row 0x38–0x48 low:

| | predicted | measured |
|---|---|---|
| its `.text` (0xBD0) | 0x8017A008 | **0x8017A008** |
| `.text.<IOMapperLock>D2Ev` (0x18) | 0x8017ABD8 | **0x8017ABD8** |
| `.text.<IOMapper::MetaClass>D0Ev` (0x4) | 0x8017ABF0 | **0x8017ABF0** |
| platform expert `.text` (0x150) | 0x8017ABF4 | **0x8017ABF4** |
| `realstubs.o` `.text` (0x3FC0) | 0x8017ADA0 | **0x8017ADA0** |
| its `.rodata` (0x3A4) | 0x8019FF20 | **0x8019FEE8** |
| its `.rodata.str1.1` | 0x801A02C4 (0x55) | **0x801A028C (0x45)** |
| platform expert `.rodata` (0x440) | 0x801A031C | **0x801A02D4** |
| platform expert `.rodata.str1.1` (0x16) | 0x801A075C | **0x801A0714** |
| `realstubs.o` `.rodata.str1.4` | 0x801A08C0 (0x3AE4) | **0x801A0878 (0x3A9C)** |

Three things in there are rules rather than arithmetic:

* **`.data` moved a whole `ALIGN(0x4000)`, and it was predicted from a rule, not from the model.** 363 left only
  0x280 of room under `.data` at 0x801A4000; this step needs ~0xFD0; `align_up(text_end, 0x4000)` therefore lands
  on the next bucket **whatever** the text end does within ±0x80. The model's text end was 0x80 high and the
  bucket was right anyway. The row worth trusting was the one derived from "how much room is left", not the one
  derived from a shift.
* **`.data`'s *size* did not change at all** — 0x19A58 before and after — because the object's 4 bytes were
  swallowed by `.data`'s closing `ALIGN(8)` (the last input's end moved 0x801C1A54 → 0x801C1A58 and both round to
  the same place). That is experiment 353's rule ("an alignment was the whole answer"), and it is why
  `.sysctl_set`, `.init_array` and the image all came out 0x8 low rather than 0x4.
* **The `.bss` pad narrowed 0x28 → 0xC**, measured as `*fill* 0x801f8574 0xc`. The pad rule's **twelfth**
  confirmation, `(0 - 0x34) mod 64`, and the first step where the pad neither grows, shrinks to zero, nor stays
  put. `realstubs.o`'s `.bss` address is unchanged in the new frame (0x801F8580) while its size shrinks 0x27C4 →
  **0x2784** — one retired storage stand-in is exactly one 0x40-byte slot.

## The two misses, and they are one cause

**This step changes the size of the stand-in object, and `tools/predict_layout.py` reads input sizes from a
map.** Retiring two function stubs takes `realstubs.o`'s `.text` from 0x3FF0 to **0x3FC0** (= 680 × 0x18, the
closed form holding at the new count) and its `.rodata.str1.4` from 0x3AE4 to **0x3A9C** (two fewer
`align4(len+1)` name slots). Both live inside the `.text` *output* section, so the text end moves by
−0x30 − 0x48 = −0x78 and lands at 0x801A4CE0. And `.rodata.str1.1` is `SHF_MERGE`, so the object's own 0x55 of
strings deduplicated to **0x45** against literals already in the run, which moves everything after it — the
0x38–0x48 of `.rodata`-run error.

That is a **written caveat on the tool now**, and it is sharper than a fill term: the model is exact only for a
step that does **not** change the stub set, which every step from 302 to 363 happened to be and this one is not.
The practical rule that comes out of it: for a step that retires storage or functions, predict the *lands-in-a-
bucket* rows (`.data`'s start, `__bss_end` from the pad rule, the counts) and treat the model's raw ends as
±0x80.

## How this step was read

Both times the question "which object defines this name" was answered by a tool rather than by reading: the
effect tool split the 3/0, and the walk tool named the second `STUB` on the path behind the first — 363's own
falsifier (a), one step nearer. The four "a stub N guards further" annotations the walk printed for
`lck_mtx_lock_spin_always`, `thread_wakeup_prim`, `zalloc_internal`, `kfree` and `IOMalloc` all turned out to be
**real**, which is the standing caveat on that annotation and was checked by hand here.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of either.
`xnu_entry_checks=5` / `xnu_entry_failures=0`. Log 301648 bytes, last line `No errors detected`. The device came
back to Android on its own (`MI 4LTE`, release 10).
