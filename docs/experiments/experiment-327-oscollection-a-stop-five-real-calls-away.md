# Experiment 327 — `libkern/c++/OSCollection.cpp`: a stop five real calls away, and three steps of `.bss` fill

**Step:** link **`libkern/c++/OSCollection.cpp`** (`out/xnu_kernel_obj/libkern_c++_OSCollection.o`,
manifest:364) — the object that defines **all seven** of the names 325 created.

**Prediction:** *counts **8 resolved / 1 added** (793/684/109); `.text` **+0x114 .. +0x17F plus a fill
band**; `.data` unmoved, `.init_array` a fifth entry, `.bss` −0x68 before fill; predicted stop
**`_ZN8OSSymbol17withCStringNoCopyEPKc` at the caller key `_ZN11OSMetaClass11postModLoadEPv+0x108`** —
the first stop in the walk that is *not* the name the step defines.*

**Result:** **all three counts exact, `.text` closing in three terms with the inputs exactly at the top of
the predicted range for the second step running, `.data` unmoved to the byte, `.bss` −0x80 (the fill gave
back a further 0x18), and the predicted name at the predicted key — reached through five calls, four of
which the previous two steps had made real.**

## The object, and a baseline that reproduces 326

`libkern/c++/OSCollection.cpp` is `OSCollection`, the base class of `OSDictionary`/`OSArray`/`OSSet`: its
`MetaClass`, the constructor (no more than `OSObject::OSObject` plus a vtable store), the destructor,
`init` (no more than `OSObject::init` plus a flags store), `setOptions` (a leaf that masks a flag word and
returns the old flags), `haveUpdated`, `copyCollection`, and the two `gMetaClass`/`metaClass` stand-ins.

| | size |
|---|---|
| `.text` | **0x194**, 22 definitions |
| COMDAT `.text._ZN12OSCollection9MetaClassD0Ev` | **4** |
| `.bss` | **0x18** |
| `.rodata` | **0xB0** (176) |
| `.rodata.str1.1` | **0x6B** (107) |
| `.init_array` | **4** |
| `.data` / `__DATA, __data` | **none at all** |
| references | **31** |

The baseline was built in this session with an **empty stand-in object** in this slot and reproduced 326's
image to the byte (`800/689/111`, `.text` 0x141520, `.data` 0x80144000/0x192D8, `.sysctl_set`
0x8015D2D8/0x10C, `.init_array` 0x8015D3E4/0x10, `.bss` 0x8015D400/0x37918, `__bss_end` 0x80194D18, image
1430516, headroom 1487592).

|  | predicted | measured |
|---|---|---|
| undefined | 793 | **793** |
| function stubs | 684 | **684** |
| storage stubs | 109 | **109** |
| resolved / added | 8 / 1 | **8 / 1** |

Six **function** stubs resolved (the ctor 326's run died in, `init`, `setOptions`, `haveUpdated`,
`copyCollection`, the dtor), two **storage** stand-ins resolved (`OSCollection::gMetaClass`,
`OSCollection::metaClass`), one **function** added: `OSReportWithBacktrace`, which is referenced only from
`haveUpdated`'s "this must be overridden" path (`gIOKitDebug`, then `panic` and the backtrace) and which
nothing on the walk's path reaches.

## `.text` closes in three terms, at the top of the range again

`.text` 0x141520 → **0x1416A0** is +0x180:

```
  this object's contributions                          +0x2B3   = 0x194 `.text` + 0x004 COMDAT
                                                                  `.text._ZN12OSCollection9MetaClassD0Ev`
                                                                  + 0x0B0 `.rodata` + **0x06B of 0x06B** —
                                                                  all 107 string bytes placed: the **third**
                                                                  dedup in a row measured at zero
  the stub object's .text + names                      -0x134   (6 bodies 0x90 + 6 names 0xD4 retired,
                                                                 less 1 body 0x18 + 1 name 0x18 created —
                                                                 the created half counted this time, and it is
                                                                 not zero)
  .text-region alignment fill                          +0x001   (0xD2C -> 0xD2D; 58 -> 59 fills)
                                                      -------
                                                       +0x180   against a measured +0x180
```

The region identity agrees: Σ(placed inputs) 0x140813 → 0x140992 = **+0x17F**, Σ(fill) +0x1, sum +0x180.
The predicted range `0x114 .. 0x17F + band` had its **top equal to the inputs term for the second step
running** — the range was only ever the uncertainty in how many of the object's 107 string bytes the pool
would place, and the pool placed all of them.

## `.data` did not move at all

| | base (326) | measured (327) | delta | predicted |
|---|---|---|---|---|
| `.text` | 0x141520 | **0x1416A0** | +0x180 | +0x114 .. +0x17F + band |
| `.data` | 0x80144000 (0x192D8) | **0x80144000** (0x192D8) | **0** | 0 — the object has no data section at all |
| `.sysctl_set` | 0x8015D2D8 (0x10C) | **0x8015D2D8** (0x10C) | 0 | 0 |
| `.init_array` | 0x8015D3E4 (0x10) | **0x8015D3E4** (**0x14**) | +0x4 | fifth entry |
| `.bss` | 0x8015D400 (0x37918) | **0x8015D400** (**0x37898**) | **−0x80** | −0x68 before fill |
| `__bss_end` | 0x80194D18 | **0x80194C98** | −0x80 | |
| image | 1430516 (0x15D3F4) | **1430520 (0x15D3F8)** | +0x4 | +0x4 — exact |
| headroom | 1487592 | **1487720** | +0x80 (follows `__bss_end`) | |

The boundary stayed out of play with 0x2961 of slack, and this is the first step in the walk whose object
has **neither** a `.data` nor a `__DATA, __data` section — so the only thing that moved after `.text` is
`.init_array`, by four bytes.

`.bss` again shrank more than predicted, by the same mechanism as 326: Σ(placed inputs) −0x68 (the
object's 0x18 and **two** retired 64-byte stand-ins) with the fill **falling** a further 0x18 (0x115 →
0xFD, 40 → 39 fills) for a −0x80 section. Three consecutive steps now give three signs of the fill's move:

| step | `.bss` inputs | fill | section |
|---|---|---|---|
| 324 | +0x30 | **−0x30** (slot re-used) | +0x40 |
| 325 | +0x58 | **+0x28** (inputs pushed) | +0x80 |
| 326 | −0x24 | **−0x1C** (slot handed back) | −0x40 |
| 327 | −0x68 | **−0x18** (slot handed back) | −0x80 |

The prediction can name the inputs; the fill is read.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN8OSSymbol17withCStringNoCopyEPKc
 xnu_entry_stub_caller=0x8011e4a0   (also _a and _e)
```

`tools/host_resolve_entry_addr.sh 0x8011e4a0` → **`_ZN11OSMetaClass11postModLoadEPv+0x108`**, `caller-4` =
`0x8011e49c: bl 80124e90 <_ZN8OSSymbol17withCStringNoCopyEPKc>` — the predicted name and the predicted
key, and **the first stop in the walk that is not the name the step defines**. The chain that reaches it is
the five-step argument the prediction wrote down, and the run's arrival at this call is evidence for every
link of it:

1. `OSCollection::OSCollection(OSMetaClass*)` returned — its body is `bl _ZN8OSObjectC2EPK11OSMetaClass`
   (real since 326) plus a vtable store.
2. `withCapacity` stored the vtable and called the real `_ZNK11OSMetaClass19instanceConstructedEv` (real
   since 324).
3. The vtable call at +0x64 reached the real `OSDictionary::initWithCapacity`, whose first instruction
   after its prologue is `bl _ZN12OSCollection4initEv` — real **this** step, and itself
   `bl _ZN8OSObject4initEv` (real since 326) plus a store; the rest of its body is `kalloc_canblock`,
   `bzero` and `OSAddAtomic`, all real.
4. The real `OSDictionary::setOptions(2, 2, 0)` — called through the vtable by `postModLoad` — returned
   **at its first branch**: its first call is `OSCollection::setOptions` (real this step, a leaf), and its
   propagation loop is guarded by `(oldflags ^ 2) & 2` **and the count at `+0x10`**, which
   `initWithCapacity` left at 0. **That the walk reached the symbol call at all is the run's evidence that
   the count was 0** — had it been non-zero, the loop's `OSMetaClassBase::safeMetaCast` and a `blx` per
   member would have stopped the walk first.
5. `postModLoad` then loaded `kmodInfo[0]` and called `OSSymbol::withCStringNoCopy`, a stub in today's
   image (0x80124E90, the 24-byte caller-key shape) because nothing links `libkern/c++/OSSymbol.cpp` yet.

So **one stop measures a chain of five calls**, four of them made real by the two preceding steps, and the
fifth — `setOptions`'s early return — a *value* the run confirms indirectly.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, `STAGE90_HW_WATCHDOG ARMED`, software dead-man armed,
no storage symbols in the payload), log **301647** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed, `persistent_write_attempted=0x00000000`
×25, `failure_mask=0x00000000` ×87, `xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`,
and the device returned to Android on its own (`ro.build.version.release` = 10).

## What it measures, and what it does not

Measured: `postModLoad`'s fifth call and no more. Not measured: what `OSSymbol::withCStringNoCopy` would
do, the rest of `OSCollection`'s 22 definitions, `haveUpdated`'s `panic`/backtrace path, and the
`.init_array` table — **five entries now, and still nothing runs them** (318).

## Next

**`libkern/c++/OSSymbol.cpp`** (`libkern_c++_OSSymbol.o`, manifest:380), which defines the current stop and
also the `bsearch` that 325 created. Its shape is read the same way — by kind, with the *created* half of
both stub-object terms written down — and the prediction after it is the next call in `postModLoad`: the
body continues past this `bl` with `mov r5, r0`, a load of `kmodInfo[0] + 0xc`, a `beq` to +0x2D4, and
`bl _ZN6OSKext24lookupKextWithIdentifierEP8OSString` — the `OSString` overload of the lookup, whose
argument is the symbol this step makes real. So the symbol should be built, used to look a kext up, and the
stop should be the first stub on that path.
