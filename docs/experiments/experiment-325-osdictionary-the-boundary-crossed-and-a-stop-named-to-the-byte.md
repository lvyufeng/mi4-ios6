# Experiment 325 — `libkern/c++/OSDictionary.cpp`: the 16 KB boundary crossed, an allocator stop named to the byte, and a correction carried back into three rows

**Step:** link **`libkern/c++/OSDictionary.cpp`** (`out/xnu_kernel_obj/libkern_c++_OSDictionary.o`,
manifest:367) — the object that defines 324's stop.

**Prediction:** *counts **3 resolved / 9 added** read by kind (816/704/112); `.text` **+0x1851 plus the
fill band**; **the first 16 KB boundary crossing since 321**, with `.data` at **0x80144000** of size
0x192C0 and image 1430484; `.bss` +0x58 before fill; predicted stop **`_ZN8OSObjectnwEm` at the caller key
`OSDictionary::withCapacity+0x10`**.*

**Result:** **every count is exact, the boundary crossing happened exactly where predicted with `.data`
at 0x80144000/0x192C0, and the stop is the predicted symbol in the predicted function at the predicted
offset — the sharpest hit of the walk so far.** The `.text` point estimate was 0xF low because the fill
band is a band, and three addresses in the prediction's own table were my own addition slip.

## The object, and a baseline that reproduces 324

`libkern/c++/OSDictionary.cpp` is the dictionary: `OSDictionary` and its `OSDictionary::MetaClass`,
`withCapacity`/`withObjects`/`withDictionary`, `initWithObjects`/`initWithDictionary`/`initWithCapacity`,
`setObject`/`getObject`/`removeObject`/`merge`/`flushCollection`/`ensureCapacity`/`setOptions`/
`copyCollection`/`copyKeys`, the iterator surface and `serialize`.

| | size |
|---|---|
| `.text` | **5640** (0x1608), 47 `T` |
| COMDAT `.text._ZN12OSDictionary9MetaClassD0Ev` | **4** |
| `.rodata` | **240** (0xF0) |
| `.rodata.str1.1` | **53** (0x35) |
| `.bss` | **24** (0x18) |
| `__DATA, __data` | **48** (0x30 — the two 24-byte `VM_ALLOC_SITE_STATIC` sites) |
| `.init_array` | **4** |
| plain `.data` | none |
| symbols | 64 definitions, 47 references |

The baseline was built in this session with an **empty stand-in object** in this slot and reproduced 324's
image to the byte (`810/699/111`, `.text` 0x13FA80, `.data` 0x80140000/0x19290, `.sysctl_set`
0x80159290/0x10C, `.init_array` 0x8015939C/0x8, `.bss` 0x801593C0/0x378D8, `__bss_end` 0x80190C98, image
1414052, headroom 1504104).

|  | predicted | measured |
|---|---|---|
| undefined | 816 | **816** |
| function stubs | 704 | **704** |
| storage stubs | 112 | **112** |
| resolved / added | 3 / 9 | **3 / 9** |

The kinds are right on both sides — resolved are two **function** stubs (`withCapacity`, the stop, and
`withDictionary`) plus the **storage** stand-in `OSDictionary::metaClass`; added are seven **function**
names (the six `OSCollection` ones and `OSSymbol::bsearch`) and two **storage** stand-ins
(`OSCollection::gMetaClass` `B`, `OSCollection::metaClass` `R`). Reading every name by kind, which is what
324's miss taught, is what makes all three columns exact.

## `.text` closes in three terms, and the strings were not deduplicated at all

`.text` 0x13FA80 → **0x1412E0** is +0x1860:

```
  this object's contributions                          +0x1731   = 0x1608 `.text` + 0x004 COMDAT
                                                                  `.text._ZN12OSDictionary9MetaClassD0Ev`
                                                                  + 0x0F0 `.rodata` + **0x035 of 0x035** —
                                                                  every one of the object's **53** string
                                                                  bytes placed, the first dedup of the five
                                                                  measured at **zero**
  the stub object's .text + names                      +0x120   (7 bodies created, 2 retired = +0x78;
                                                                 7 names created 0xF4, 2 retired 0x4C = +0xA8)
  .text-region alignment fill                          +0x00F   (0xD1C -> 0xD2B; 56 -> 57 fills)
                                                      -------
                                                       +0x1860   against a measured +0x1860
```

The region identity agrees exactly: Σ(placed inputs) 0x13ED83 → 0x1405D4 = **+0x1851**, Σ(fill) 0xD1C →
0xD2B = **+0xF**. So the prediction's `+0x1851` was the *inputs* term to the byte, and the fill — predicted
as a band, not derived — supplied the other 0xF.

The dedup measurements now read: **100%** dropped (319), **63%** (321), **4.7%** (323), **2.0%** (324),
and **0%** here. The five points bound the term rather than determining it, which is why the walk carries
it as a *range* whose width is the whole prediction's uncertainty.

## The 16 KB boundary was crossed — the first crossing since 321

| | base (324) | measured (325) | delta | predicted |
|---|---|---|---|---|
| `.text` | 0x13FA80 | **0x1412E0** | +0x1860 | +0x1851 ± band |
| `.data` | 0x80140000 (0x19290) | **0x80144000** (**0x192C0**) | +0x4000 start, +0x30 size | 0x80144000 / 0x192C0 — exact |
| `.sysctl_set` | 0x80159290 (0x10C) | **0x8015D2C0** (0x10C) | +0x4000 | 0x8015D290 |
| `.init_array` | 0x8015939C (0x8) | **0x8015D3CC** (**0xC**) | +0x4000 start, +0x4 size | 0x8015D39C |
| `.bss` | 0x801593C0 (0x378D8) | **0x8015D400** (**0x37958**) | +0x4000 start, +0x80 size | +0x58 before fill |
| `__bss_end` | 0x80190C98 | **0x80194D58** | +0x40C0 | |
| image | 1414052 (0x1593A4) | **1430488 (0x15D3D8)** | +0x4034 | 1430484 |
| headroom | 1504104 | **1487528** | −0x40C0 (it follows `__bss_end` exactly) | 1487632 |

The text end was predicted at 0x801412D1 and came out 0x801412E0: **the 0xF of fill is the whole
difference**, and the crossing margin was 0x580 against a +0x1860 step. **Every address I derived by
*adding* 0x192C0 to 0x80144000 in the prediction's table was 0x30 low, and the image 4 low with it** —
three numbers, one addition, done once and copied three times. `.data`'s start and size, which were
predicted directly rather than derived, are exact.

`.bss` grew **0x80**, not the 0x58 predicted before fill: Σ(placed inputs) +0x58 (the object's 0x18, and
the stub object's +0x40 — two new 64-byte stand-ins 0x80 less one retired 0x40, `OSDictionary::metaClass`)
and then the section's fill **rose** 0x28 (0x109 / 40 fills → 0x131 / 40 fills) instead of absorbing it, so
the stand-ins were pushed rather than tucked in. 324's fill fell; 325's rose. The *sign* of the fill's move
is read, never predicted.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN8OSObjectnwEm
 xnu_entry_stub_caller=0x8011f8d8   (also _a and _e)
```

`tools/host_resolve_entry_addr.sh 0x8011f8d8` → **`_ZN12OSDictionary12withCapacityEj+0x10`**, `caller-4` =
`0x8011f8d4: bl 80124ab4 <_ZN8OSObjectnwEm>`, and the disassembly of the newly real body is
`push {r4, r5, r6, lr}`, `mov r5, r0`, `mov r0, #32`, `bl <_ZN8OSObjectnwEm>` — exactly as the prediction
read it out of the object. **The predicted symbol, the predicted caller function and the predicted offset
within it are all three right**, which makes this the sharpest hit of the walk (321 and 323 named a name
and a key but not the offset separately; 324 named neither).

What it measures: `postModLoad` calls `withCapacity(40)` through this step's new real body, and the body's
first act is to allocate 32 bytes with `OSObject::operator new` — a name nothing in this image defines,
because `libkern/c++/OSObject.cpp` is not linked.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, `STAGE90_HW_WATCHDOG ARMED`, software dead-man armed,
no storage symbols in the payload), log **301628** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed, `persistent_write_attempted=0x00000000`
×25, `failure_mask=0x00000000` ×87, `xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`,
and the device returned to Android on its own (`ro.build.version.release` = 10).

## What it measures, and what it does not

Measured: `OSDictionary::withCapacity`'s first frame and no more. Not measured: `OSCollection`'s
constructor (the body's next `bl`, a name this step creates), `OSMetaClass::instanceConstructed`, the
vtable call into the real `initWithCapacity` — whose own first instruction is
`bl _ZN12OSCollection4initEv`, another name this step creates — and the 63 other definitions of this
object, referenced by nothing yet.

## A correction carried back into the 321, 322 and 323 rows

Those three rows restated their measured image size in hex — `1413920 (0x1592C0)` and
`1413968 (0x1592D0)` — and both restatements are wrong. The image is
`align16K(text_end) + .data + 0x10C + .init_array`, so 322's is 0x140000 + 0x19210 + 0x10C + 0x4 =
**0x159320** and 323's 0x140000 + 0x19240 + 0x10C + 0x4 = **0x159350**, which are 1413920 and 1413968
exactly. The decimals and the headrooms in those rows were self-consistent (`headroom = 0x80300000 −
__bss_end` checks at both); only the hex restatement was wrong, by 0x60 and 0x80, and four documents
carried it. A number restated in another unit is a new number and has to be recomputed.

## Next

**`libkern/c++/OSObject.cpp`** (`libkern_c++_OSObject.o`, manifest:372), which defines the current stop's
name. `_ZN8OSObjectnwEm` is 24 bytes at 0x80124AB4 and is the *caller-key stub shape* (`push {lr}`,
`mov r1, lr`, `pop {lr}`, `b entry_stub_hit`) — still a stub, which is why the name is still in the
undefined list. Its real body is `kalloc_canblock(size, VM_KERN_MEMORY_LIBKERN)` + `assert` +
`bzero(mem, size)` + `OSIVAR_ACCUMSIZE`, and **both `kalloc_canblock` and `bzero` are already real in this
image** (324's `preModLoad` called the first). So the step that makes `OSObject.cpp` real should **not**
stop inside `operator new` at all: it should return and the frontier should resume in `withCapacity`'s own
body, whose next `bl` is `_ZN12OSCollectionC2EPK11OSMetaClass` — a stub **this** step created, and the name
to predict — with `OSMetaClass::instanceConstructed` (real since 324) after it and then the vtable call
into `initWithCapacity`, whose own first instruction is `bl _ZN12OSCollection4initEv`, also created here.

The object: `.text` **0x408** plus a 4-byte COMDAT `.text._ZN8OSObject9MetaClassD0Ev`, `.bss` **0x1C**,
`.rodata` **0x84**, `.rodata.str1.1` **0xA3** (163), `__DATA, __data` **0x18**, `.init_array` **4** — a
**third** entry nothing runs (318) — **40 definitions** and **27 references**.
