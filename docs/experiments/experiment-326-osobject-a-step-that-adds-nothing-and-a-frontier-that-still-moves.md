# Experiment 326 — `libkern/c++/OSObject.cpp`: a step that adds nothing, an allocator that returns, and a frontier that moves anyway

**Step:** link **`libkern/c++/OSObject.cpp`** (`out/xnu_kernel_obj/libkern_c++_OSObject.o`, manifest:372) —
the object that defines 325's stop.

**Prediction:** *counts **16 resolved / 0 added** read by kind (800/689/111); `.text` **+0x19C .. +0x23F
plus a fill band**; `.data` unmoved in start with the object's own 0x18, `.init_array` a fourth entry, and
`.bss` −0x24 before fill; predicted stop **`_ZN12OSCollectionC2EPK11OSMetaClass` at the caller key
`OSDictionary::withCapacity+0x24`**.*

**Result:** **all three counts exact, `.text` closing in three terms with the inputs exactly at the
prediction's top, `.data`'s start unmoved, `.bss` shrinking 0x40 instead of 0x24 (the fill gave back
0x1C), and the stop the predicted name at the predicted key.** The step proves two things a name alone
would not: the allocator that stopped 325 ran its whole real body and returned, and **a step that adds no
names at all can still move the frontier** — because the frontier was sitting on a name *it* makes real.

## The object, and a baseline that reproduces 325

`libkern/c++/OSObject.cpp` is `OSObject` itself: the two allocators (`operator new`/`operator delete` and
their sized forms), `OSObject::MetaClass` and `gMetaClass`, `init`/`free`, the ctor/dtor, the
retain/release family (`retain`, `release`, `release(int)`, `taggedRetain`, `taggedRelease` ×2,
`getRetainCount`, `serialize`) and the C `osobject_retain` shim.

| | size |
|---|---|
| `.text` | **0x408**, 40 definitions |
| COMDAT `.text._ZN8OSObject9MetaClassD0Ev` | **4** |
| `.bss` | **0x1C** |
| `.rodata` | **0x84** |
| `.rodata.str1.1` | **0xA3** (163) |
| `__DATA, __data` | **0x18** |
| `.init_array` | **4** |
| plain `.data` | none |
| references | **27 — every one of which this image already defines** |

The baseline was built in this session with an **empty stand-in object** in this slot and reproduced 325's
image to the byte (`816/704/112`, `.text` 0x1412E0, `.data` 0x80144000/0x192C0, `.sysctl_set`
0x8015D2C0/0x10C, `.init_array` 0x8015D3CC/0xC, `.bss` 0x8015D400/0x37958, `__bss_end` 0x80194D58, image
1430488, headroom 1487528).

|  | predicted | measured |
|---|---|---|
| undefined | 800 | **800** |
| function stubs | 689 | **689** |
| storage stubs | 111 | **111** |
| resolved / added | 16 / 0 | **16 / 0** |

Sixteen resolved: **fifteen function** stubs (`operator new`/`delete` and the sized forms, `init`, `free`,
the ctor, the dtor, five retain/release entry points, `serialize`, `osobject_retain`) and **one storage**
stand-in, `_ZN8OSObject10gMetaClassE`. **Zero added — the second step in the walk with an added count of
zero** (322 was the first), and the difference from 322 is that the zero is written down *as a term* here
rather than being a shape copied forward with no reason attached, which is what cost 323 0x154.

## `.text` closes in three terms, one byte into the band

`.text` 0x1412E0 → **0x141520** is +0x240:

```
  this object's contributions                          +0x533   = 0x408 `.text` + 0x004 COMDAT
                                                                  `.text._ZN8OSObject9MetaClassD0Ev`
                                                                  + 0x084 `.rodata` + **0x0A3 of 0x0A3**
                                                                  — all 163 string bytes placed, the
                                                                  **second** dedup in a row measured at zero
  the stub object's .text + names                      -0x2F4   (15 bodies 0x168 + 15 names 0x18C,
                                                                 exactly; the created halves are +0x000
                                                                 because nothing is added)
  .text-region alignment fill                          +0x001   (0xD2B -> 0xD2C; 57 -> 58 fills)
                                                      -------
                                                       +0x240   against a measured +0x240
```

The region identity agrees: Σ(placed inputs) 0x1405D4 → 0x140813 = **+0x23F**, Σ(fill) 0xD2B → 0xD2C =
**+0x1**. So the predicted range `0x19C .. 0x23F + band` had its top *equal to the inputs term*: the range
was only ever about how many of the object's 163 string bytes the pool would place, and this step placed
every one of them, with the band supplying one byte.

## Nothing after `.text` moved except by the object's own data

| | base (325) | measured (326) | delta | predicted |
|---|---|---|---|---|
| `.text` | 0x1412E0 | **0x141520** | +0x240 | +0x19C .. +0x23F + band |
| `.data` | 0x80144000 (0x192C0) | **0x80144000** (**0x192D8**) | +0x18 | +0x18 — exact, fill unmoved (0x7AA7 / 8 fills in both) |
| `.sysctl_set` | 0x8015D2C0 (0x10C) | **0x8015D2D8** (0x10C) | +0x18 | follows `.data` |
| `.init_array` | 0x8015D3CC (0xC) | **0x8015D3E4** (**0x10**) | +0x18 start, +0x4 size | fourth entry |
| `.bss` | 0x8015D400 (0x37958) | **0x8015D400** (**0x37918**) | **−0x40** | −0x24 before fill |
| `__bss_end` | 0x80194D58 | **0x80194D18** | −0x40 | |
| image | 1430488 (0x15D3D8) | **1430516 (0x15D3F4)** | +0x1C | +0x1C — exact |
| headroom | 1487528 | **1487592** | +0x40 (follows `__bss_end`) | |

`.text` now has **0x2AE0 of slack** below the 16 KB boundary, so the crossing 325 made is not in play for
many steps at this rate.

`.bss` is the one term that beat the prediction, and it beat it by growing *smaller*: Σ(placed inputs)
−0x24 (the object's 0x1C and the retired 64-byte stand-in `OSObject::gMetaClass`) and the fill **fell
0x1C** as well (0x131 → 0x115, 40 fills in both), for a −0x40 section. So this retired stand-in *did* hand
its whole slot back, and 0x1C of it came out of the fill — the mirror of 324, where the retired slot was
partly re-used. 324, 325 and 326 together give the `.bss` rule its full shape: **inputs, plus a fill move
whose sign and size are read from the map, and the section lands on whatever those two say.**

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN12OSCollectionC2EPK11OSMetaClass
 xnu_entry_stub_caller=0x8011f8ec   (also _a and _e)
```

`tools/host_resolve_entry_addr.sh 0x8011f8ec` → **`_ZN12OSDictionary12withCapacityEj+0x24`**, `caller-4` =
`0x8011f8e8: bl 8012482c <_ZN12OSCollectionC2EPK11OSMetaClass>` — the predicted name *and* the predicted
key, read off `withCapacity`'s disassembly before the step was built.

Two things this proves that a name alone would not:

* **`OSObject::operator new`, which stopped 325, ran its whole real body and returned** — `kalloc_canblock`,
  `bzero`, `OSAddAtomic`, all real, exactly as the prediction argued from the object rather than from hope.
* **A step with zero added names can still move the frontier**, because the frontier was sitting on a name
  *it* makes real: the walk resumed inside `withCapacity` at its next call, which is a stub the previous
  step created.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, `STAGE90_HW_WATCHDOG ARMED`, software dead-man armed,
no storage symbols in the payload), log **301647** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed, `persistent_write_attempted=0x00000000`
×25, `failure_mask=0x00000000` ×87, `xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`,
and the device returned to Android on its own (`ro.build.version.release` = 10).

## What it measures, and what it does not

Measured: `withCapacity`'s third call and no more. `OSCollection::OSCollection` is entered with `r1` set to
a metaclass pointer; what happens inside it is unknown to this image. Not measured:
`OSMetaClass::instanceConstructed` (real since 324), the vtable call into the real `initWithCapacity`
whose own first instruction is `bl _ZN12OSCollection4initEv`, the rest of `OSObject`'s 40 definitions, and
whether `operator new`'s `assert` would ever fire.

## Next

**`libkern/c++/OSCollection.cpp`** (`libkern_c++_OSCollection.o`, manifest:364), which defines **every one**
of the seven function names 325 created — the step that turns the whole `OSCollection` surface real at
once: the constructor the current stop is inside, `init`, `haveUpdated`, `setOptions`, `copyCollection`,
the destructor, and the two `gMetaClass`/`metaClass` stand-ins. Its own first frame is the prediction to
make: `OSCollection::OSCollection(OSMetaClass*)` is a short body (store the metaclass, call
`OSMetaClass::instanceConstructed`, store the vtable), so the stop should move to the **first stub inside
`initWithCapacity`** — `bl _ZN12OSCollection4initEv` — if the constructor returns, or to whatever the
constructor's own body reaches first.
