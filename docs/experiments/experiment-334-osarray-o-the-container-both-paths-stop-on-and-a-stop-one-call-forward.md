# Experiment 334 — `OSArray.cpp` linked: the container both paths stop on, and a stop one call forward in the same frame

**Step:** link one object, `libkern/c++/OSArray.cpp` (`libkern_c++_OSArray.o`, manifest:362) — the only
object in the pool that defines 333's stop, `_ZN7OSArray12withCapacityEj`. Nothing else changes.

**Prediction:** *3 resolved / 0 added — 993 → **990** undefined, 880 → **878** function, 113 → **112**
storage; `.text` near 0x149B00, still below the 0x14C000 boundary 333 crossed, so `.data` does not move;
`.init_array` 0x24 → 0x28 with `_GLOBAL__sub_I_OSArray.cpp` as entry #9 and `last_kernel_constructor` still
#10; `.bss`'s start unmoved, its size −0x28 placed plus fill;* and **the stop outside the object it links**,
at `iokit_post_constructor_init+0x0C` on `_ZN15IORegistryEntry10initializeEv`.*

**Result:** **all of it.** The counts are exact to the name, every `.text` and `.bss` term closes with no
residual, `.bss`'s start did not move, and the run stops at
`stub_hit=_ZN15IORegistryEntry10initializeEv` with `xnu_entry_stub_caller_v=0x8011b0f8` =
`iokit_post_constructor_init+0xc`.

## The object

| `libkern_c++_OSArray.o` | |
|---|---|
| `.text` | **3400** (0xD48), 19 functions |
| `.text._ZN7OSArray9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **224** (0xE0 — `_ZTV7OSArray`, `_ZTVN7OSArray9MetaClassE`, `_ZN7OSArray9metaClassE`, `_ZN7OSArray10superClassE`) |
| `.rodata.str1.1` | **14** (0xE) |
| `__DATA, __data` | **48** (0x30 — the two 24-byte `VM_ALLOC_SITE_STATIC` sites) |
| `.bss` | **24** (0x18 — `OSArray::gMetaClass`) |
| `.init_array` | 4 (`_GLOBAL__sub_I_OSArray.cpp`) |
| definitions / references | 51 / 41 |

The object is the `OSArray` container implementation: `withCapacity`/`withArray`/`withObjects` as the
constructors, `initWithCapacity`/`initWithArray`/`initWithObjects` behind them, the `OSCollection`
overrides (`getCount`, `getCapacity`, `ensureCapacity`, `setCapacityIncrement`, `setObject`, `removeObject`,
`replaceObject`, `flushCollection`, `merge`), the two `OSIterator` protocol methods (`initIterator`,
`iteratorSize`, `getNextObjectForIterator`), the `OSMetaClass` boilerplate, and the class constructors.

## The counts, and why the created half is zero

**Predicted 3 resolved / 0 added; measured 3 / 0.** The three are two function stubs and one storage
stand-in:

| resolved | object | stand-in was |
|---|---|---|
| `_ZN7OSArray12withCapacityEj` (26 chars) | `T` | `func T` |
| `_ZN7OSArray9withArrayEPKS_j` (26) | `T` | `func T` |
| `_ZN7OSArray9metaClassE` | `R`, 4 bytes | `data R 4` |

| | 333 | 334 | delta |
|---|---|---|---|
| undefined / function / storage | 993 / 880 / 113 | **990 / 878 / 112** | −3 / −2 / −1 |

Every one of the object's 41 references is already satisfied — `OSObject::operator new` (326),
`OSCollection::OSCollection`/`init`/`haveUpdated` (326/327), `OSMetaClass::instanceConstructed` (324),
`OSSymbol::withCStringNoCopy`/`OSString::withCStringNoCopy` (330/331), `kalloc_canblock`, `bzero`,
`OSAddAtomic`, `debug_container_malloc_size`, `__cxa_atexit`. This is the first step of the walk with a
**zero created half**, which is 250/270's rule read from the other side: an object whose whole reference set
is already in the image costs nothing to link but what it retires.

## `.text`: seven terms, no residual

| term | bytes |
|---|---|
| this object's `.text` | **+0xD48** |
| its COMDAT `.text._ZN7OSArray9MetaClassD0Ev` | **+0x004** |
| its `.rodata`, placed whole | **+0x0E0** |
| its string bytes, as placed | **+0x008** of 0x0E (6 bytes deduped) |
| the two retired stub bodies | **−0x030** |
| their two name slots | **−0x038** |
| `.text` fill | **−0x00C** |

Sum **+0xDC0**, which is the measured `.text` delta (0x148B20 → **0x1498E0**): the map's placed total moved
0x147DFC → **0x148BC8** (+0xDCC) with the fill falling 0xD24 → **0xD18**.

The prediction's string row was written as "**+0x0E or less**" and the answer is 8 of 14 — the object's two
literals are already in the pool, one as a substring of an existing string. The two stub terms are measured
a second time, independently, because `realstubs.o` is a separate instrument for the same quantity: its
`.text` goes 21120 → **21072** (−0x30, exactly two 0x18 bodies) and its `.rodata.str1.4` goes 23731 →
**23675** (−0x38 = 28 + 28, both names being 26 characters, so `align4(26+1)` twice — 322's rule
re-measured a fourth time).

## The 16 KB boundary, not crossed this time

| | 333 | 334 | delta |
|---|---|---|---|
| `.text` | 0x80148B20 (0x148B20) | **0x801498E0 (0x1498E0)** | +0xDC0 |
| `.data` | 0x8014C000 (0x19398) | **0x8014C000 (0x193C8)** | **+0x30, address unmoved** |
| `.sysctl_set` | 0x80165398 (0x10C) | **0x801653C8 (0x10C)** | +0x30, size 0 |
| `.init_array` | 0x801654A4 (0x24) | **0x801654D4 (0x28)** | +0x30, **+4** |
| `.bss` | 0x80165500 (0x37A98) | **0x80165500 (0x37A58)** | **+0, size −0x40** |
| `__bss_end` | 0x8019CF98 | **0x8019CF58** | −0x40 |
| image | 1463496 | **1463548** | +0x34 |
| headroom | 1454184 | **1454248** | +0x40 |

`.text` now ends 0x801498E0, **0x2720 below the 0x8014C000 boundary** — about ten kilobytes of walk still
fit before `.data` moves again. `.data`'s own arithmetic is the object's one `__DATA, __data` section and
nothing else: +0x30, fill unmoved at 0x7AA3, placed total 0x118F5 → 0x11925.

**`.bss`'s start did not move, and the reason is the alignment in front of it.** `.init_array` grew to 0x28
so its end is now 0x801654FC, which rounds up to the 0x80165500 that `.bss` already started at; in 333 that
same end was 0x801654C8 and the same round-up was absorbing a 0x38 gap. So the +0x30 of `.data` growth
pushed through `.sysctl_set` and `.init_array` and then vanished into alignment — which is why the `+0` in
that column is a measurement of the layout rather than a coincidence of small numbers. This is 312's rule
("growth may be absorbed by the fill in front of the next aligned input") doing the absorbing in the other
direction from the way it did in 331.

The headroom *rose* by 0x40, which is the same 312-shaped trap on the other side of the layout: it is the
room **below** `topOfKernelData`, so a shorter image has more of it. Experiment 332 predicted this term with
the wrong sign and the ledger carries that.

`.init_array` is **0x28 — ten entries**, resolved against the image the build just made:

```
0x801654d4  _GLOBAL__sub_I_OSKext.cpp        0x801228d0  _GLOBAL__sub_I_OSString.cpp
0x801654d8  _GLOBAL__sub_I_OSMetaClass.cpp   0x80124248  _GLOBAL__sub_I_IOCPU.cpp
0x801654dc  _GLOBAL__sub_I_OSDictionary.cpp  0x80124fe4  _GLOBAL__sub_I_OSArray.cpp   <- #9, new
0x801654e0  _GLOBAL__sub_I_OSObject.cpp      0x8012503c  last_kernel_constructor     <- #10, still last
0x801654e4  _GLOBAL__sub_I_OSCollection.cpp
0x801654e8  _GLOBAL__sub_I_OSSymbol.cpp
```

The *order* is what the step depends on: `OSArray.o` is appended before the last kernel constructor, so
`_GLOBAL__sub_I_OSArray.cpp` — which runs `OSMetaClass::OSMetaClass("OSArray", &OSCollection::gMetaClass,
32)` and then `__cxa_atexit` — runs before the call it enables, and `last_kernel_constructor` is still last,
which is the only thing its name is about.

## `.bss`: −0x40 in two measured parts

- **−0x28 placed.** The retired storage stand-in frees its **0x40-byte, 64-byte-aligned slot** — 331's rule,
  and not the 4 bytes the name holds — while the object's own `.bss` is added: `_ZN7OSArray10gMetaClassE`,
  0x18 bytes, at **0x8019B268**. So placed goes 0x3797F → **0x37957**.
- **−0x18 fill.** 0x119 → **0x101**.

−0x28 and −0x18 make the section's **−0x40**. A storage stand-in is worth five times its own size in `.bss`
and its retirement is worth five times its size back; nothing in this step created one, so the section
shrank.

## The stop, read off the object before the build

`withCapacity` is a two-line function, and everything it reaches is real:

```
_Z15IOCPUInitializev:
 +0x18  bl _ZN7OSArray12withCapacityEj        STUB (333)   <- return address +0x1C, 333's stop
```

whose body calls `OSObject::operator new` → `OSCollection::OSCollection` → `OSMetaClass::instanceConstructed`
→ the vtable call at +0x64 into `OSArray::initWithCapacity` (this object), whose four calls
(`OSCollection::init`, `kalloc_canblock`, `bzero`, `OSAddAtomic`) are all real and whose only data reference
is `&debug_container_malloc_size`, taken as an *address* for `OSAddAtomic`. So `withCapacity` returns, and so
does the rest of `IOCPUInitialize` — whose four distinct callees are `IOLockAlloc`,
`OSArray::withCapacity`, `OSSymbol::withCStringNoCopy` and `OSString::withCStringNoCopy`, the last three
already real — and the walk returns to `iokit_post_constructor_init` and stops on its **second** call:

```
8011b0ec <iokit_post_constructor_init>:  push {r4, r5, fp, lr}
 +0x04  8011b0f0  bl 80122c3c <_Z15IOCPUInitializev>               real since 333
 +0x08  8011b0f4  bl 80128d24 <_ZN15IORegistryEntry10initializeEv>  STUB   <- return address +0x0C
 +0x0C  8011b0f8  mov r4, r0
```

so:

```
stub_hit=_ZN15IORegistryEntry10initializeEv   xnu_entry_stub_caller = iokit_post_constructor_init+0x0C
```

The key is a **frame up** from the previous step's key (+0x08 → +0x0C, one call forward in the same
function), which is what 318's rule reads as progress rather than as a new frame — and the reason it could
move is that the whole of `IOCPUInitialize` returned.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_why=0x8012aacc
 xnu_entry_why_byte=0x00000061                       <- 'a', the epilogue's stub-hit text
 xnu_entry_kv_written=0x00000073
 xnu_entry_stub_caller_v=0x8011b0f8                   (also _w0=0x31313038 "8011", _w1=0x38663062 "b0f8")
 xnu_entry_stub_caller_digits=0x00000046
 xnu_entry_stub_caller=0x8011b0f8                     (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000                   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN15IORegistryEntry10initializeEv
```

`tools/host_resolve_entry_addr.sh 0x8011b0f8` → **`iokit_post_constructor_init+0xc`**, `caller-4` =
`0x8011b0f4: bl 80128d24 <_ZN15IORegistryEntry10initializeEv>` — the call predicted, in the function
predicted, at the offset predicted, on the symbol predicted.

**What the one key measures beyond the stop.** It is one call forward in the same frame as 333's key, and
that is a stronger statement than 333's was: 333 measured that the walk *reached* `withCapacity`'s call;
this run measures that `withCapacity` **returned**, that everything `OSArray::initWithCapacity` reaches
returned with it (the vtable call at +0x64, `OSCollection::init`, `kalloc_canblock`, `bzero`, `OSAddAtomic`),
that `IOCPUInitialize`'s remaining calls and the `_ZL11gIOCPUsLock` store between them all executed, and
that `.init_array` entry #9 ran. A single fault anywhere along that chain would have been a `data abort`
with `abort_entries=1` instead of a stub report.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img` (4378 KB), nothing flashed, `25`
records of `persistent_write_attempted=0x00000000` and `87` of `failure_mask=0x00000000` with **no non-zero
reading of either**, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301646 bytes, and the device back on
Android on its own (`MI 4LTE`, release 10).

The image's own table gains the object's 51 definitions and loses the three stand-in symbols it replaced
(`8170` `nm` records over `8131` distinct names — eleven assembler-local `L_*` labels repeat across
objects). The names that were stand-ins do not leave; they move:
`_ZN7OSArray12withCapacityEj` 0x801284E8 (a stub) → **0x8012456C**,
`_ZN7OSArray9metaClassE` → **0x80143078** (a real `R` in the read-only region rather than a zeroed `.bss`
slot — a **retired** stand-in, so the object's `R` replaces four bytes that were zero and stays four bytes,
unlike a function name whose body and slot retire).

## What this step measures

The frontier is `IORegistryEntry::initialize`, the **second** call of `iokit_post_constructor_init`, and so
the second name `iokit/Kernel/IOStartIOKit.cpp` obliges. `iokit_Kernel_IORegistryEntry.o` (manifest:297) is
the only object in the pool that defines it — and it is the largest single step this walk has taken:

```
    # 335: `iokit/Kernel/IORegistryEntry.cpp` - 175 definitions against 75 references, resolving 67 stubs
    #      at once (the registry-entry API as seen from outside) and adding only 5
```

67 resolved and 5 added put 335 at **990 → 928 undefined, 878 → 816 function, 112 → 112 storage**, so it is
also the first step of the walk whose `.text` should *shrink* — 67 retired bodies and name slots against 5
created — which makes it the test of whether the seven-term arithmetic the last four steps closed with still
closes when its terms have the opposite sign.
