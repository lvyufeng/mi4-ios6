# Experiment 344 — `OSNumber.cpp`: the plist parses end to end, and a stop four frames down that 342 predicted

**Step:** link one object, `libkern/c++/OSNumber.cpp` (`libkern_c++_OSNumber.o`, 6488 bytes) — the object that
defines 343's stop `_ZN8OSNumber10withNumberEyj`. Nothing else changes.

**Prediction:** *2 resolved / 0 added — 831 → **829** undefined, 729 → **728** function, 102 → **101** storage;
`.text` +0x584 to +0x5B8 in six terms; `.data` unmoved; `.bss` placed −0x28;* and **the stop at
`_ZN5OSSet12withCapacityEj` on `_ZN6OSKext10initializeEv+0xA8`** — 342's prediction, deferred twice.

**Result:** all three counts exact; the stop landed **name and key**, so it is the longest chain the walk has
predicted through (four frames); and the plist 342's abort never got to has now been **parsed end to end and
built into a container**. `realstubs.o` moves on both of its sections for the first time since 341, the `.bss`
fill goes back to 342's value rather than staying where 343 left it, and the `.rodata.str1.4` tail artifact
appears for the fourth time — now as an expected row.

## The object

| `libkern_c++_OSNumber.o` | |
|---|---|
| `.text` | **1284** (0x504), 28 functions |
| `.text._ZN8OSNumber9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **176** (0xB0 — `_ZTV8OSNumber` 0x6C, `_ZTVN8OSNumber9MetaClassE` 0x3C, `_ZN8OSNumber9metaClassE`, `_ZN8OSNumber10superClassE`) |
| `.rodata.str1.1` | **52** (0x34, four `.L.str` blocks) |
| `.bss` | **24** (0x18 — `OSNumber::gMetaClass`) |
| `.init_array` | 4 (`_GLOBAL__sub_I_OSNumber.cpp`) |
| definitions / references | 39 / 32 |

**2 resolved / 0 added**: `_ZN8OSNumber10withNumberEyj` (`T` against a `func T` stand-in) and
`_ZN8OSNumber9metaClassE` (`R 4` against a `data R 0x4` stand-in, so its 0x40 `.bss` slot comes back). Nothing
is added, so the function column moves for the first time in four steps — 343, 342 and 341 each retired one
stub and created one.

The object's two halves are joined by one line of source: `_ZN8OSNumber9metaClassE` is a read-only word the
object defines, carrying a relocation against `OSNumber::gMetaClass` in its own `.bss`, and
`_GLOBAL__sub_I_OSNumber.cpp` is the constructor that fills that `.bss` word in. That is 341's shape (an `R`
reference word plus a `.init_array` constructor), and 341 is where the shape was measured to work under XNU's
own `.init_array` scan.

## `.text`: six terms, closing at the top of the band again

| term | predicted | measured |
|---|---|---|
| this object's `.text` | +0x504 | **+0x504** (input at 0x8013A638, whole) |
| its COMDAT | +0x004 | **+0x004** |
| its `.rodata`, placed whole | +0x0B0 | **+0x0B0** (input at 0x8015A718) |
| its string bytes, as placed | +0x000 .. +0x034 | **+0x034** (all 52 bytes at 0x8015A7C8) |
| the one retired stub body | −0x018 | **−0x018** |
| its one name slot (27 chars, `align4(28)`) | −0x01C | **−0x01C** |

Sum **+0x5B8**, the measured placed move (0x15E170 → **0x15F728**) — the **top** of the predicted band
0x584..0x5B8 for the second step running, and for the same reason both times: the band's low end allows for
dedupe of `.rodata.str1.1` and all 0x34 bytes were placed. The fill went 0xD30 → **0xD38** (+0x8), so `.text`
is **+0x5C0** (0x15EEA0 → **0x15F460**), and `.data` stays at 0x80160000 with 0xBA0 below it to spare — this
object brings no `.data` of its own to move it.

`realstubs.o` moves on **both** of its sections for the first time since 341, because this is the first step
since then with no created stub: `.text` 0x4458 → **0x4440** (−0x18 exactly) and `.rodata.str1.4`
0x4167 → **0x414B** where the model says 0x414C — the **fourth** appearance of the ≤1-byte tail artifact that
338, 342 and 343 each recorded on that section, and by now an expected row rather than a surprise. Its `.bss`
went 0x1A44 → **0x1A04**, exactly −0x40.

| | 343 | 344 measured | 344 predicted |
|---|---|---|---|
| `.text` | 0x8015EEA0 (0x15EEA0) | **0x8015F460** (0x15F460) | 0x8015F424..0x8015F458 (placed) — right band, top again |
| `.data` | 0x80160000 (0x19498) | **0x80160000** (0x19498, unmoved) | **0x80160000** ✓ |
| `.sysctl_set` | 0x80179498 (0x10C) | **0x80179498** (0x10C) | **0x80179498** ✓ |
| `.init_array` | 0x801795A4 (0x48, 18) | **0x801795A4** (**0x4C**, nineteen entries) | **0x801795A4** (0x4C, 19) ✓ |
| `.bss` | 0x80179600 (size 0x37C18, placed 0x37AE4, fill 0x134) | **0x80179600** (size **0x37BD8**, placed **0x37ABC**, fill **0x11C**) | placed 0x37ABC ✓ |
| `__bss_end` | 0x801B1218 | **0x801B11D8** | ~0x801B11F0 (0x18 high) |
| image | 1545708 | **1545712** | **1545712** ✓ |
| headroom | 1371624 | **1371688** | ~1371664 |

`.bss`'s placed term is exact at **−0x28** (−0x40 the stand-in's slot, +0x18 this object's own `.bss`), and its
start is unmoved for the fifth step running: `.init_array`'s end is 0x801795F0 and `align64` of it is still
0x80179600, so the nineteenth entry cost `.bss` nothing. `last_kernel_constructor` is still the nineteenth and
last entry, at 0x8013AB40 = its 343 address + 0x508 — the same 0x508 (0x504 plus the 4-byte COMDAT) that the
section's content grew by before the fill.

**The one row that missed is a sign**, and the reason is worth its own line: `__bss_end` and `headroom` were
predicted 0x18 the wrong way because the `.bss` fill was carried forward from 343 (0x134). 344's fill is
**0x11C** — exactly 342's value. The `.bss` fill is not a running total and not monotone: it went
0x11C → 0x134 → 0x11C across three steps. That is 343's `.data` lesson from the other side, and it is why the
fill is a row *read* rather than added.

## The run: a four-frame chain, and the plist finally parsed

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000006a   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x8013f7c0          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x801093a4   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN5OSSet12withCapacityEj
```

`tools/host_resolve_entry_addr.sh 0x801093a4` → **`_ZN6OSKext10initializeEv+0xa8`**, `caller-4` =
`0x801093a0: bl 8013eac8 <_ZN5OSSet12withCapacityEj>`. That is **342's prediction**, made two steps ago out of
`OSKext::initialize`'s own body — deferred first by 342's early stop and then by 343's. Four frames separate
this key from this step's stop (`parse` → `OSUnserialize` → `IOCatalogue::initialize` →
`iokit_post_constructor_init` → `OSKext::initialize`), and every link of the chain was read out of the
instrument before the device was touched.

**`abort_entries=0` is what makes the key a measurement of the whole chain.** Between 343's stop and this one
the run had to:

* return from `OSNumber::withNumber` — three direct calls, all real (`OSObject::operator new`, the `OSObject`
  base constructor, `OSMetaClass::instanceConstructed`), plus a `blx` at +0x60 into
  `OSNumber::init(unsigned long long, unsigned int)`, which this object itself defines and whose body makes one
  real call;
* reduce the last `;` into a `pair`, and build the dictionary at `}` — `OSDictionary::withCapacity` and three
  `d->setObject((OSString *)key, value)` calls, each of which goes `OSDictionary::setObject` →
  `OSSymbol::withString` → `OSMetaClassBase::safeMetaCast`, all read as real in the 343 image;
* build the array at `)` — `OSArray::withCapacity` and one `setObject(unsigned int, ...)` with its single real
  `OSCollection::haveUpdated`;
* release six objects and `tags`, unlock in `OSUnserialize`, and run `IOCatalogue::initialize`'s remaining
  0x118 bytes — whose ten direct calls are all real — to completion;
* then `iokit_post_constructor_init` advanced one call and `OSKext::initialize` ran its first 0xA4 bytes.

So the plist that 342's abort never reached has now been **parsed end to end and built into a container**: the
first step of the walk where a whole data structure, and not just a call, crosses from stubbed to real code.
`parse` had no other reachable stub — the only one left in its 33 call sites is
`+0xC7C _ZN5OSSet9withArrayEPK7OSArrayj`, and the string has no `[`.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301637 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 345 — `libkern/c++/OSSet.cpp`

`libkern_c++_OSSet.o` (9248 bytes) defines this step's stop and three more names: **4 resolved / 0 added** —
`_ZN5OSSet12withCapacityEj`, `_ZN5OSSet9withArrayEPK7OSArrayj` (**the name 343's link created**, so
343 → 344 → 345 closes a stub the walk itself invented), `_ZN5OSSet11withObjectsEPPK8OSObjectjj` and the
storage stand-in `_ZN5OSSet9metaClassE` (`R 0x4`) — for 829 → **825**, **728 → 725 function, 101 → 100
storage**. It brings `.text` 0xB44, a `.group`/COMDAT pair, `.bss` 0x18, `.rodata` 0xE0, `.rodata.str1.1` 0xA
and an `.init_array` of its own.

**The predicted stop is `OSKextParseVersionString` at `_ZN6OSKext10initializeEv+0x224`**, read off the 344
image rather than argued. `OSKext::initialize` has **two** calls to `_ZN5OSSet12withCapacityEj` — this run
stopped on the first, at +0xA4; the second is at +0xB8 — and past them
`first_stub_call.py _ZN6OSKext10initializeEv --from 0xBC` gives `+0x220 OSKextParseVersionString`, with
everything between real (+0xC8 `OSArray::withCapacity`, +0xEC/+0x13C/+0x180 `PE_parse_boot_argn`,
+0x128/+0x168 `OSKextLog`). So 345 buys the rest of `OSKext::initialize`'s container setup and the first 0x224
bytes of the function. The falsifier is a stop *inside* `OSSet` — one of its 36 references being a stub the link
did not resolve — or a stop at `+0xBC`, which would mean the second `OSSet::withCapacity` call was not reached
and the first one's return value gated on something.
