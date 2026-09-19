# Experiment 335 — `IORegistryEntry.cpp` linked: the registry API in one step, the 16 KB boundary crossed again, and a stop the prediction called unreachable

**Step:** link one object, `iokit/Kernel/IORegistryEntry.cpp` (`iokit_Kernel_IORegistryEntry.o`,
manifest:339) — the only object in the pool that defines 334's stop, `_ZN15IORegistryEntry10initializeEv`.
Nothing else changes.

**Prediction:** *67 resolved / 5 added — 990 → **928** undefined, 878 → **816** function, 112 → **112**
storage; `.text` +0x3664 plus up to 0xDF of strings plus a fill band, ending between 0x8014CF44 and
0x8014D043, which is past the 0x8014C000 boundary, so `.data` steps to **0x80150000**; `.init_array` 0x28 →
0x2C with `_GLOBAL__sub_I_IORegistryEntry.cpp` at #10; `.bss`'s start at the `align64` of `.init_array`'s
end, **0x80169540**;* and **the stop outside the object, three calls into its caller at
`iokit_post_constructor_init+0x14` on `_ZN9IOService10initializeEv`.**

**Result:** the counts, the addresses and every section came out as predicted — **928 undefined**, `.data` at
**0x80150000**, `.bss` at **0x80169540** — and **the stop did not**. The run stopped at
`stub_hit=_ZN20OSCollectionIterator14withCollectionEPK12OSCollection` with
`xnu_entry_stub_caller_v=0x80128680` = `_ZNK15IORegistryEntry7inPlaneEPK15IORegistryPlane+0x4c`, a name the
ledger had listed as still-stubbed but *declared unreachable* from `initialize`.

## The object, and two corrections written before the run

| `iokit_Kernel_IORegistryEntry.o` | |
|---|---|
| `.text` | **18348** (0x47AC), 95 functions |
| three COMDAT `.text._ZN*9MetaClassD0Ev` | 4 each (0x00C) |
| `.rodata` | **696** (0x2B8 — three vtables, three `metaClass`/`superClass` pairs, the plane tables) |
| `.rodata.str1.1` | **223** (0xDF) |
| `.data` | **8** |
| `.bss` | **124** (0x7C — `gRegistryRoot`, `gIORegistryPlanes`, the lock globals, ...) |
| `.init_array` | 4 (`_GLOBAL__sub_I_IORegistryEntry.cpp`) |
| definitions / references | 175 / 75 |

Two corrections were made to the ledger **before** the device run, which is the order the ledger exists for:

- **334's closing line was wrong about this step.** It said this would be "the first step whose `.text`
  should *shrink* — 67 retired bodies and name slots against 5 created". The 67 retirements are worth
  0x648 + 0xEBC = **0x1504**, and this object's own `.text` is **0x47AC** — nearly three and a half times
  as much. `.text` grows, and it grew by 0x3700.
- **The build refuted one term of the kind split.** It printed `928 symbol(s) undefined` exactly, then
  `stubs: 815 function(s), 113 storage` where the prediction said 816 / 112. One of the five added names is
  **storage**, and the generated list says which:

  ```
  data _ZN10OSIterator10gMetaClassE B 0x18
  ```

  **The cause is which population each side looked in.** The generator classifies an undefined name by
  looking for a definition with `nm -S` over the **whole 695-object pool**, and `libkern_c++_OSIterator.o` —
  compiled, in the pool, not in this link — defines that name `B 0x18`: an `OSMetaClass` object, 24 bytes.
  The prediction used a tool that classified added names off the object *being linked*, where the name is
  undefined and `nm` says nothing about its kind. Every added name came out "function" — right four times
  out of five, and right every time before, because this is the first step whose object references a name
  that some *other* unlinked object defines as storage. `tools/entry_object_effect.py` was fixed rather than
  annotated: it now scans the pool once (25176 defined names, 0.5 s) and classifies exactly as the
  generator does. Verified against this step's own refutation — with `--against /dev/null`, so that all 75
  references are "added", it reports `_ZN10OSIterator10gMetaClassE becomes a storage stub (pool B 18)`.

A one-name kind error is worth more than a count: a created storage stand-in costs a full **0x40-byte
64-aligned `.bss` slot**, so it moved `.text` by 0x38 and `.bss` by 0x40.

## The counts

| | 334 | 335 | delta |
|---|---|---|---|
| undefined / function / storage | 990 / 878 / 112 | **928 / 815 / 113** | −62 / −63 / +1 |

The 67 resolved are the whole registry-entry API as the rest of the kernel sees it — `initialize`, `init`,
both `setName`s, all seven `setProperty`s, both `copyProperty`/`getProperty` families, the
attach/detach pairs, `inPlane`, `hasAlias`, `getPath`, `getLocation`, `compareNames`,
`getRegistryEntryID`, `setProperties`, `removeProperty` and the three `MetaClass` destructors. The 5 added
are `_ZN10OSIterator10gMetaClassE` (storage, 0x18), `_ZN10OSIteratorC2EPK11OSMetaClass`,
`_ZN10OSIteratorD2Ev`, `_ZN9OSBoolean11withBooleanEb` and `strtouq`.

## `.text`: eight terms, no residual

| term | bytes |
|---|---|
| this object's `.text` | **+0x47AC** |
| its three COMDATs | **+0x00C** |
| its `.rodata`, placed whole | **+0x2B8** |
| its string bytes, as placed | **+0x0CC** of 0x0DF (19 deduped) |
| the 67 retired stub bodies | **−0x648** |
| their 67 name slots | **−0xEBC** |
| the 4 created stub bodies | **+0x060** |
| their 4 name slots | **+0x060** |

Sum **+0x36F8**, which is the measured placed total 0x148BC8 → **0x14C2C0**, and the fill went 0xD18 →
**0xD20** so `.text` itself is **+0x3700** (0x1498E0 → **0x14CFE0**). The prediction's range for that end was
0x8014CF44..0x8014D043 and the answer is inside it.

`realstubs.o` measures the four stub terms a second time, and it is where the kind defect shows in a section
rather than in a count:

| `realstubs.o` | 334 | 335 | delta |
|---|---|---|---|
| `.text` | 21072 | **19560** | **−0x5E8** = 0x18 × (67 − 4) |
| `.rodata.str1.4` | 23675 | **19999** | **−0xE5C** = −0xEBC + 0x60 |
| `.bss` | 7364 | **7428** | **+0x40** — one created storage stand-in |

## The 16 KB boundary, crossed for the second time in three steps

| | 334 | 335 | delta |
|---|---|---|---|
| `.text` | 0x801498E0 (0x1498E0) | **0x8014CFE0 (0x14CFE0)** | +0x3700 |
| `.data` | 0x8014C000 (0x193C8) | **0x80150000 (0x193D8)** | **+0x4000, size +0x10** |
| `.sysctl_set` | 0x801653C8 (0x10C) | **0x801693D8 (0x10C)** | +0x4010, size 0 |
| `.init_array` | 0x801654D4 (0x28) | **0x801694E4 (0x2C)** | +0x4010, **+4** |
| `.bss` | 0x80165500 (0x37A58) | **0x80169540 (0x37B18)** | **+0x4040, size +0xC0** |
| `__bss_end` | 0x8019CF58 | **0x801A1058** | +0x4100 |
| image | 1463548 | **1479952** | +0x4104 |
| headroom | 1454248 | **1437608** | −0x4100 |

`.text` ends past 0x8014C000, so `.data` steps a whole 16 KB and every section above it follows — the
boundary rule 333 and 334 both measured, now from the growth side rather than the retirement side.

`.data`'s **+0x10** is both halves moving: the object's own `.data` placed 0x8 *and* the fill 0x8, 0x7AA3 →
**0x7AAB**. It is the only place in this walk where a `.data` contribution did not fit inside the fill
already there.

`.bss`'s **+0xC0** is three measured parts: **+0x7C** the object's own `.bss` (the map prints it as one line
at 0x8019F2C0), **+0x40** the created storage stand-in taking a full 64-aligned slot for its 24 bytes —
331's rule a third time — and **+0x04** of fill, 0x101 → **0x105**. And `.bss`'s *start* moved by 332's
idiom rather than by the boundary: `.init_array` ends at 0x80169508 and `align64` of that is **0x80169540**.

`.init_array` is **0x2C — eleven entries**, with the order the step depends on intact:

```
0x801694e4  _GLOBAL__sub_I_OSKext.cpp        0x80124248  _GLOBAL__sub_I_IOCPU.cpp
0x801694e8  _GLOBAL__sub_I_OSMetaClass.cpp   0x80124fe4  _GLOBAL__sub_I_OSArray.cpp
0x801694ec  _GLOBAL__sub_I_OSDictionary.cpp  0x80129704  _GLOBAL__sub_I_IORegistryEntry.cpp  <- #10, new
0x801694f0  _GLOBAL__sub_I_OSObject.cpp      0x801297f4  last_kernel_constructor            <- #11, still last
0x801694f4  _GLOBAL__sub_I_OSCollection.cpp
0x801694f8  _GLOBAL__sub_I_OSSymbol.cpp
0x801694fc  _GLOBAL__sub_I_OSString.cpp
```

## The stop the prediction called unreachable

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_why=0x8012ec9c
 xnu_entry_why_byte=0x00000061                       <- 'a', the epilogue's stub-hit text
 xnu_entry_stub_caller_v=0x80128680                   (also _w0=0x32313038 "8012", _w1=0x30383638 "8680")
 xnu_entry_stub_caller_digits=0x0000005e
 xnu_entry_stub_caller=0x80128680                     (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000                   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN20OSCollectionIterator14withCollectionEPK12OSCollection
```

`tools/host_resolve_entry_addr.sh 0x80128680` → **`_ZNK15IORegistryEntry7inPlaneEPK15IORegistryPlane+0x4c`**,
`caller-4` = `0x8012867c: bl 8012d584 <_ZN20OSCollectionIterator14withCollectionEPK12OSCollection>`.

The predicted key was `iokit_post_constructor_init+0x14` on `IOService::initialize`. The chain the run
measures is:

```
iokit_post_constructor_init
  -> IORegistryEntry::initialize                                  real since this step
     -> setProperty("IORegistryPlanes", gIORegistryPlanes)        vtable slot 0x60
        -> setProperty(const OSSymbol*, OSObject*)                vtable slot 0x58
           -> inPlane(NULL)                                       vtable slot 0xd0
              -> OSCollectionIterator::withCollection(...)        STUB
```

Every step is in the source, in order. `setProperty`'s body is

```c
OSCollection *coll = OSDynamicCast(OSCollection, anObject);
bool makeImmutable = (coll && inPlane());        // IORegistryEntry.cpp:542-552
```

and `gIORegistryPlanes` **is** an `OSCollection` — it is the `OSDictionary::withCapacity(1)` that the same
function made eleven calls earlier — so the cast succeeds and `inPlane()` is evaluated, with `plane`
defaulted to NULL. With a NULL plane, `inPlane`'s first statement is
`OSCollectionIterator::withCollection( registryTable())` (`:1595`), unconditionally.

**What the prediction's closure walk got wrong is one line of its own output.** The walk followed
`R_ARM_CALL` relocations only, so vtable-mediated edges were invisible to it; the patch was to pass the
vtable targets in as extra roots, and that resolver returned `None` for **all five** of them (its `.rodata`
relocation parse was broken), so the root list came out four long where it should have been nine. The script
printed that root list and the prediction was written anyway. The four functions it did walk really do make
no stub call — and the two it never entered, `setProperty(const OSSymbol*, OSObject*)` and
`setName(const OSSymbol*, const IORegistryPlane*)`, are exactly where the stub was.

The name the run stopped on was **on the ledger's own list of the ten functions of this object that still
call a stub**, under a sentence declaring them all unreachable from `initialize`. The list was right and the
reachability claim was not. The lesson is the one this project keeps relearning: **a walker that can
silently drop edges cannot be made safe by printing what it kept** — count the roots against the number you
asked for, the same way the stub-name reader counts records against lines. A closure claim has to be an
enumeration of the *edges followed*, not of the nodes reached.

**What the one key measures beyond the stop.** Reaching `inPlane+0x4c` means `initialize` got past its
`lck_*` block, its `new IORegistryEntry` (through `OSObject::operator new`), the
`OSMetaClass::instanceConstructed` call, `IORecursiveLockAlloc`, `OSDictionary::withCapacity(1)`, the vtable
call into `init(NULL)` **and `init`'s whole body** (`OSObject::init`, `IOMalloc`, `bzero`,
`IORecursiveLockAlloc`, two `OSDictionary::withCapacity`, `safeMetaCast`, `OSSymbol::withString`), the
`++gIORegistryLastID` store, four `OSSymbol::withCStringNoCopy`, the `setName("Root")` chain, and then
`setProperty`'s own `safeMetaCast(anObject, OSCollection::gMetaClass)` — **a successful dynamic cast in real
XNU C++ at kernel level, the first this walk has run**. `abort_entries=0` throughout.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img` (4394 KB), nothing flashed, `25`
records of `persistent_write_attempted=0x00000000` and `87` of `failure_mask=0x00000000` with **no non-zero
reading of either**, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301670 bytes, and the device back on
Android on its own (`MI 4LTE`, release 10).

## What this step measures

The frontier is `_ZN20OSCollectionIterator14withCollectionEPK12OSCollection`, from
`libkern/c++/OSCollectionIterator.cpp` (`libkern_c++_OSCollectionIterator.o`, manifest:365): 27 definitions,
`.text` 1124, `.rodata` 148, `.bss` 24, one `__DATA, __data` of 48 and its own `.init_array` entry. The same
name is reached by five other `IORegistryEntry` methods — `getChildIterator`, `getParentIterator`,
`hasAlias`, `getNextObjectFlat`, `compareNames` and `attachToParent` — so this one object unblocks the whole
iteration half of the registry API, and it is also the iterator type every `OSCollection`-walking caller in
IOKit will use.
