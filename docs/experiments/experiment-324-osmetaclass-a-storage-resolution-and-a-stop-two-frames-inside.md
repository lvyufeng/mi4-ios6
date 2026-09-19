# Experiment 324 — `libkern/c++/OSMetaClass.cpp`: the metaclass machinery, a storage resolution the count missed, and a stop two frames inside the C++ runtime

**Step:** link **`libkern/c++/OSMetaClass.cpp`** (`out/xnu_kernel_obj/libkern_c++_OSMetaClass.o`,
manifest:370) — the object that defines five of the six names 323 added, and the largest object linked
since 318.

**Prediction:** *counts **23 resolved / 3 added** (831 undefined, 699 function stubs, 112 storage);
`.text` **+0x1550 .. +0x18FB plus a band**; `.data` +0x50, `.init_array` +0x4, `.bss` +0x30 plus two
stand-ins; predicted stop **`devsw_init` at caller key `0x8011B26C` = `StartIOKit+0xCC`**, with
`OSOrderedSet::withCapacity` and a `panic` named as the alternatives.*

**Result:** **the function counts are exact and the storage count is one low, `.text` closes in four
terms with no residual, and the stop is neither the prediction nor the alternative it named — it is two
frames deeper, inside `OSRuntimeInitializeCPP`'s mod-load path, where `postModLoad` builds an
`OSDictionary` of 40.** The point estimate for `.text` missed by 5 bytes while being wrong twice.

## The object, and a baseline that reproduces 323

`libkern/c++/OSMetaClass.cpp` is the metaclass machinery: `OSMetaClassBase` (`initialize` — 323's stop —
`safeMetaCast`, `metaCast`, the ctor/dtor) and `OSMetaClass` itself (`preModLoad`, `postModLoad`,
`checkModLoad`, `modHasInstance`, `applyToInstances*`, `allocClassWithName*`, `getMetaClassWithName`,
`removeClasses`, `printInstanceCounts`, `serializeClassDictionary`, …), the three static locks
`sAllClassesLock` / `sStalledClassesLock` / `sInstancesLock`, and `_GLOBAL__sub_I_OSMetaClass.cpp`.

| | size |
|---|---|
| `.text` | **6608** (0x19D0), 63 `T` |
| COMDAT `.text._ZN15OSMetaClassMetaD0Ev` | **4** |
| `.rodata` | **180** (0xB4) |
| `.rodata.str1.1` | **939** (0x3AB) |
| `.data` / `__DATA, __data` | **8** / **72** |
| `.bss` | **48** (0x30) |
| `.init_array` | **4** |
| `.group` | 12 |
| references | **31** |

The baseline was built in this session with an **empty stand-in object** in this slot and reproduced
323's image to the byte (`831/721/110`, `.text` 0x13E180, `.data` 0x80140000/0x19240, `.sysctl_set`
0x80159240/0x10C, `.init_array` 0x8015934C/0x4, `.bss` 0x80159380/0x37898, `__bss_end` 0x80190C18, image
1413968, headroom 1504232).

|  | predicted | measured |
|---|---|---|
| undefined | 811 | **810** |
| function stubs | 699 | **699** |
| storage stubs | 112 | **111** |
| resolved / added | 23 / 3 | **24 / 3** |

## The miss is a storage resolution the prediction never looked for

The prediction computed "resolved" by intersecting the object's **63 `T`s** with the stub list. The
missing one is `_ZN11OSMetaClass9metaClassE` — a **`data` name**, the static `OSMetaClass::metaClass`,
which the image carried as a 64-byte storage stand-in and this object now defines. An object resolves
stand-ins with its `B`/`D`/`R` definitions too:

```
24 retired = 23 functions + 1 storage (`OSMetaClass::metaClass`)
 3 added   =  1 function  + 2 storage (`_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_`,
                                      `_ZN12OSOrderedSet9metaClassE`, `debug_container_malloc_size`)
```

so 831 − 24 + 3 = **810**, 721 − 23 + 1 = **699**, 110 − 1 + 2 = **111**, and 699 + 111 = 810. This is
**316's rule in mirror image**: there a storage resolution was *counted as a body* and over-counted
`.text`; here it was left out of the resolution count and under-counted storage. The kind column matters
in both directions — which is why 325's prediction below reads every name by kind.

## `.text` closes with no residual in four terms

`.text` 0x13E180 → **0x13FA80** is +0x1900:

```
  this object's contributions                          +0x1E20   = 0x19D0 `.text` + 0x004 COMDAT
                                                                  `.text._ZN15OSMetaClassMetaD0Ev`
                                                                  + 0x0B4 `.rodata` + 0x398 of
                                                                  `.rodata.str1.1` placed (920 of the
                                                                  object's 939 — 19 bytes dropped, the
                                                                  fourth dedup measured)
  the stub object's .text                              -0x210   (23 bodies retired, 1 created, 24 each)
  the stub object's name strings                       -0x310   (0x354 of retired names − 0x44 of the
                                                                 one created)
  .text-region alignment fill                          +0x000   (0xD1C in both; 55 -> 56 fills)
                                                      -------
                                                       +0x1900   against a measured +0x1900
```

The region identity says the same: Σ(placed inputs) 0x13D483 → 0x13ED83 = +0x1900, Σ(fill) 0xD1C →
0xD1C = 0, with exactly two inputs moving in the map — this object (+0x1E20) and
`xnu_arm_entry_realstubs.o` (−0x520 = 0x210 of bodies + 0x310 of names).

**The point estimate came in 5 bytes low from two errors that cancel.** The prediction used the object's
own string size (939) where the linker placed 920, and counted 23 retired bodies where the step also
creates one:

| | bytes |
|---|---|
| 19 bytes of strings the pool dropped | **−0x13** |
| one created stub body the body term did not count | **+0x18** |
| | **+0x5** = 0x1900 − 0x18FB |

A small residual is not evidence of a right model: 322 missed by 4 bytes with a correct closure, this one
by 5 while wrong twice.

## The data block grew and did not move — the tightest margin yet

`.text` ends at 0x8013FA80, **0x580 below the 16 KB boundary** at 0x80140000, and the boundary held:

| | base (323) | measured (324) | delta |
|---|---|---|---|
| `.text` | 0x13E180 | **0x13FA80** | +0x1900 |
| `.data` | 0x80140000 (0x19240) | 0x80140000 (**0x19290**) | +0x50 |
| `.sysctl_set` | 0x80159240 (0x10C) | **0x80159290** (0x10C) | +0x50 |
| `.init_array` | 0x8015934C (0x4) | **0x8015939C** (**0x8**) | +0x50 start, +0x4 size |
| `.bss` | 0x80159380 (0x37898) | **0x801593C0** (0x378D8) | +0x40 start, +0x40 size |
| `__bss_end` | 0x80190C18 | **0x80190C98** | +0x80 |
| image | 1413968 (0x1592D0) | **1414052 (0x159324)** | +0x54 |
| headroom | 1504232 | **1504104** | −0x80 |

* **`.data` +0x50 is exactly the object's own data** — 8 (`.data`) + 72 (`__DATA, __data`) — moved in
  full with the section's fill untouched (0x7AA7 / 8 fills in both).
* **`.init_array` doubled to two entries.** This object brings `_GLOBAL__sub_I_OSMetaClass.cpp`, and per
  318 nothing in this image runs either entry: the table keeps its ELF name `.init_array`, and
  `sectionIsConstructor` accepts only `__mod_init_func` or `__constructor`.
* **`.bss` +0x40 closes on three moves**: the object's own 0x30, **two** new 64-byte stand-ins (+0x80) and
  **one** retired (`OSMetaClass::metaClass`, −0x40), with the section's fill **falling 0x30** (0x139 / 39
  fills → 0x109 / 40 fills). The section identity agrees exactly (Σplaced inputs +0x70, Σfill −0x30). So
  the retired stand-in did not hand its whole slot back to fill: the two new ones were placed partly
  inside the space it freed, and there is one more fill entry. The `.bss` *start* moved +0x40 for a reason
  with nothing to do with `.bss`: it is align64 of the preceding end, and `.data` +0x50 with
  `.init_array` +0x4 pushed that end from 0x80159350 to 0x801593A4, whose align64 is 0x801593C0.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN12OSDictionary12withCapacityEj
 xnu_entry_stub_caller=0x8011e468   (also _a and _e)
```

`tools/host_resolve_entry_addr.sh 0x8011e468` → **`_ZN11OSMetaClass11postModLoadEPv+0xd0`**, `caller-4`
= `0x8011e464: bl 801231c0 <_ZN12OSDictionary12withCapacityEj>`, and the instruction before the `bl` is
`mov r0, #40`. So the stop is `postModLoad`'s **first call**, and it is **not** the predicted `devsw_init`.
The prediction had named the alternative it took, but the wrong symbol inside it: what it named was the
step's own added `OSOrderedSet::withCapacity`, whereas `postModLoad` builds an `OSDictionary` first.

**The frame chain says more than the missing symbol.** `postModLoad` is one of the five names 323 created
as stubs and this step made real. In the linked image it sits at 0x8011E398 with exactly three callers —
`OSRuntimeFinalizeCPP+0xC0` (0x8011D50C) and `OSRuntimeInitializeCPP+0x23C` / `+0x2E0` (0x8011D808,
0x8011D8AC) — so the run reached it from the initialiser. That initialiser's body (0x8011D5CC, 0x310
bytes) is, in order:

```
  _ZN6OSKext24lookupKextWithIdentifierEPKc   on kernelAddress+12 - the kernel's own kmod, by identifier
  _ZN11OSMetaClass10preModLoadEPKc           +0x84
  loop over segments:
      _ZN11OSMetaClass12checkModLoadEPv      +0xC8      (its return value gates the loop)
      loop over sections:
          strncmp(sectname, r6, 15)  \
          strncmp(sectname, r8, 13)  /  sectionIsConstructor: __mod_init_func and __constructor
          -> only on a match, a `blx r0` loop that invokes the section's function pointers
  _ZN11OSMetaClass11postModLoadEPv          +0x23C or +0x2E0, the segment loop's exit paths
```

So this single stop proves three names 323 had created as stubs are now really entered in sequence —
`preModLoad`, `checkModLoad` and `postModLoad` — because any of them still being a stub would have stopped
the walk earlier under its own name. And it keeps 318's finding intact rather than contradicting it: the
scan *runs*, and matches nothing, because the entry image's table keeps its ELF name `.init_array` and
`sectionIsConstructor` accepts only `__mod_init_func` or `__constructor`. Nothing can have been
constructed, and `postModLoad` itself stops inside its first statement.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, `STAGE90_HW_WATCHDOG ARMED`, software dead-man armed,
`STAGE90_DEADMAN_ENABLE 1`, no storage symbols in the payload), log **301645** bytes, one `stub_hit=` line,
**no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed, `persistent_write_attempted=0x00000000`
×25, `failure_mask=0x00000000` ×87, `xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`,
and the device returned to Android on its own (`ro.build.version.release` = 10).

## What it measures, and what it does not

Measured: `OSMetaClassBase::initialize` ran (three real `IOLockAlloc` calls, one per static lock);
`OSlibkernInit` continued into `OSRuntimeInitializeCPP` (real since 323), which looked the kernel's own
kmod up by identifier, called `preModLoad` — real as of this step and no longer a stub: its body takes
`sAllClassesLock`, `kalloc_canblock`s a 20-byte and a 40-byte structure, `OSAddAtomic`s a counter and
`__bzero`s one of them — then walked the segments calling `checkModLoad` (44 bytes, not the near-trivial
body the prediction assumed: it reads the structure `preModLoad` installed at 0x8018EFE8, compares it with
the segment, `clz`/`lsr` to a bool), compared each section name, matched nothing, and reached
`postModLoad`.

Not measured: whether `postModLoad` would have found anything (it stops at its first call), the 40
dictionary entries it asked for, the 40 of its 63 definitions nothing references yet, and
`OSMetaClass::applyToInstances*` — the only places this walk's new `OSOrderedSet` stub could have been hit
instead.

## Next

**`libkern/c++/OSDictionary.cpp`** (`libkern_c++_OSDictionary.o`, manifest:367) — which defines the current
stop:

| | size |
|---|---|
| `.text` | **5640** (0x1608) |
| COMDAT `.text._ZN12OSDictionary9MetaClassD0Ev` | **4** |
| `.rodata` | **240** (0xF0) |
| `.rodata.str1.1` | **53** (0x35) |
| `.bss` | **24** (0x18) |
| `__DATA, __data` | **48** (0x30 — the two 24-byte `VM_ALLOC_SITE_STATIC`s) |
| `.init_array` | **4** |
| plain `.data` | none |
| symbols | 64 definitions, 47 references |

Predicted **3 resolved / 9 added**, read by kind on both sides: resolved are two **function** stubs,
`OSDictionary::withCapacity` (the stop) and `OSDictionary::withDictionary`, plus
`_ZN12OSDictionary9metaClassE`, an `R` **storage** stand-in; added are seven **function** names —
`OSCollection::{setOptions, haveUpdated, copyCollection, init}`, `OSCollection::OSCollection`,
`OSCollection::~OSCollection` and `_ZN8OSSymbol7bsearchEPKvS1_jm` (defined by
`libkern_c++_OSCollection.o` and `libkern_c++_OSSymbol.o`) — and two **storage** stand-ins,
`OSCollection::gMetaClass` (`B`) and `OSCollection::metaClass` (`R`). That is 810 → **816** undefined,
699 → **704** function stubs, 111 → **112** storage.

`.text` predicted **+0x1851 plus the fill band**: 0x1608 this object + 0x004 COMDAT + 0x0F0 `.rodata` +
0x000..0x035 its strings + 0x0F4 the seven created names + 0x0A8 the seven created bodies − 0x04C the two
retired names − 0x030 the two retired bodies. **And this step crosses the 16 KB boundary — the first
crossing since 321.** `.text` ends today 0x580 below 0x80140000 and +0x1851 lands at 0x801412D1, so
`.data` should step a whole 0x4000: predicted `.data` start **0x80144000**, size 0x192C0, `.init_array`
**0x8**, image **1430484** (0x15D3D4), headroom down 0x4040. `.bss` predicted +0x58 before fill (0x18
object, +0x80 two new stand-ins, −0x40 one retired), with fill free to absorb or to push the block a slot
(310/313).

**And the stop should be `_ZN8OSObjectnwEm` — `OSObject::operator new` — at the caller key
`OSDictionary::withCapacity+0x10`.** 324 stopped at the *stub* for `withCapacity`; this step makes its body
real, and the body opens `push {r4, r5, r6, lr}`, `mov r5, r0`, `mov r0, #32`, `bl _ZN8OSObjectnwEm` at
function+0x0C. That name is a stub in the image today and **stays** a stub — OSDictionary.o is a caller,
not a definition — so it is the first stub the newly real body reaches. The named alternatives, in order:
next in the body comes `bl _ZN12OSCollectionC2EPK11OSMetaClass`, **one of the seven function stubs this
step creates**, then `bl _ZNK11OSMetaClass19instanceConstructedEv` (real since 324), then an **indirect**
`blx r2` through the vtable at +0x64, which is the real `OSDictionary::initWithCapacity` — and *its* first
instruction after the prologue is `bl _ZN12OSCollection4initEv`, another name this step creates.
