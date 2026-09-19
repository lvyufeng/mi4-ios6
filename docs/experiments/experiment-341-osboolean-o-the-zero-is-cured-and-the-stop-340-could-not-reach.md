# Experiment 341 — `OSBoolean.cpp` linked: the zero is cured, and the stop is the prediction 340 wrote and could not reach

**Step:** link one object, `libkern/c++/OSBoolean.cpp` (`libkern_c++_OSBoolean.o`) — the only object in the
pool that defines 340's zero, `kOSBooleanTrue`, and the only one that writes it. Nothing else changes.

**Prediction:** *4 resolved / 1 added — 842 → **841** undefined, 734 → **734** function, 108 → **105**
storage; `.text` +0x3BF to +0x3DA in six terms, ending past 0x8015C000 so `.data` steps a whole 16 KB to
0x80160000; `.bss` placed −0xA0;* and **the stop at `_ZN11IOCatalogue10initializeEv` on
`iokit_post_constructor_init+0x18` — the call 340's abort happened one step before.**

**Result:** the zero is cured and the stop landed exactly — `stub_hit=_ZN11IOCatalogue10initializeEv` at
`xnu_entry_stub_caller_v=0x8011b104` = `iokit_post_constructor_init+0x18`, with **`abort_entries=0`**, i.e.
`IOService::initialize` ran the 0x12C bytes past 340's fault and returned. The three kind columns are exact
and the prediction's own three numbers did not add up (it printed `841 / 734 / 105`, and 734 + 105 = 839); the
six-term table was 0x20 short because the retired body was written as −0x020 where every stub body in this
project is exactly **0x18**, and the created one was left out at +0x018; and the run corrected this ledger's
claim about the `why_byte` telegraph.

## The object

| `libkern_c++_OSBoolean.o` | |
|---|---|
| `.text` | **780** (0x30C), 8 functions |
| `.text._ZN9OSBoolean9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **156** (0x9C — the two `R` reference words, `metaClass`, two vtables) |
| `.rodata.str1.1` | **27** (0x1B) |
| `.bss` | **32** (0x20 — `OSBoolean::gMetaClass` 0x18, `gOSBooleanTrue` 4, `gOSBooleanFalse` 4) |
| `.init_array` | 4 (`_GLOBAL__sub_I_OSBoolean.cpp`) |
| definitions / references | 36 / 29 |

**4 resolved / 1 added** — `OSBoolean::withBoolean` (a stub since 335) and the three storage stand-ins out,
`_ZN11OSSerialize15binarySerializeEPK15OSMetaClassBase` in (checked absent from
`xnu_arm_entry_stubnames.txt` rather than assumed) — so the function column does not move: one body in, one
body out.

## Why the zero is cured, and it is cured twice

`OSBoolean.cpp` declares

```c
static OSBoolean * gOSBooleanTrue  = 0;               // a .bss word
OSBoolean * const & kOSBooleanTrue = gOSBooleanTrue;  // a reference to that pointer
```

so `kOSBooleanTrue` is **read-only data holding an address** — `R 0x4` in the object, with a relocation
against `gOSBooleanTrue`. Two levels of the double dereference, each fixed by a different mechanism:

| level | what it was in 340 | what fixes it |
|---|---|---|
| `ldr r5, [kOSBooleanTrue]` | a 4-byte zeroed `.bss` stand-in → `r5 = 0` | the object defines it in a read-only section, so the link **relocates** it to `&gOSBooleanTrue` |
| `ldr r2, [r5]` | never reached | `_GLOBAL__sub_I_OSBoolean.cpp` calls `OSBoolean::initialize()` (its `bl` is at object offset 0x70), which allocates the two instances and stores them into `gOSBooleanTrue`/`gOSBooleanFalse` |

and the constructor runs because the linker places it in `.init_array` **before**
`last_kernel_constructor` — and `iokit_post_constructor_init` is that last entry, so XNU's own scan (given an
entry point at 329) has run every `_GLOBAL__sub_I_*` before the call that faulted. This is the first step of
the walk whose fix is not "resolve the symbol" but "initialize the value", and the first whose two failure
modes are *both* about data rather than about a call.

## `.text`: six terms, one of them left out

| term | predicted | measured |
|---|---|---|
| this object's `.text` | +0x30C | **+0x30C** |
| its COMDAT | +0x004 | **+0x004** |
| its `.rodata`, placed whole | +0x09C | **+0x09C** |
| its string bytes, as placed | +0x000 .. +0x01B | **+0x01B** (all 27 placed) |
| one retired body (`withBoolean`) | −0x020 | **−0x018** |
| **one created body (`binarySerialize`)** | **left out of the table** | **+0x018** |
| the name slots: −0x020 + 0x038 | +0x018 | **+0x018** |

Sum **+0x3DF**, the measured placed move (0x15B139 → **0x15B518**), with the fill 0xD27 → **0xD28** (+0x1), so
`.text` is **+0x3E0** (0x15BE60 → **0x15C240**). The table was 0x20 short for two reasons at once, and both are
about one term's size: the retirement was written as −0x020 where it is **−0x018**, and the creation was left
out entirely where it is **+0x018** — so the prose's "one body in, one body out" was right and the rows under
it did not say so. **Every stub body in this project is exactly 0x18**, measured rather than assumed:
`arm-none-eabi-nm -S out/stage90/xnu_arm_entry_realstubs.o` reports the same size for all **734** of them, so a
body term is ±0x18 and any other number in that row is a defect. `realstubs.o` measures the pair directly: its
`.text` is **0x44D0, unchanged from 340**. Its other two sections confirm the terms that *were* written: `.rodata.str1.4` 0x4234 → **0x424C**
(**+0x18** = −0x20 for `align4(28+1)` + 0x38 for `align4(53+1)`) and `.bss` 0x1BC4 → **0x1B04** (**−0xC0**,
three stand-in slots given back, none created).

## The layout, and a boundary that was crossed by construction

| | 340 | 341 measured | 341 predicted |
|---|---|---|---|
| `.text` | 0x8015BE60 (0x15BE60) | **0x8015C240** (0x15C240) | placed band right, size band mis-derived |
| `.data` | 0x8015C000 (0x19498) | **0x80160000** (0x19498, fill 0x7AAB) | **0x80160000** ✓ |
| `.sysctl_set` | 0x80175498 (0x10C) | **0x80179498** (0x10C) | **0x80179498** ✓ |
| `.init_array` | 0x801755A4 (0x40, 16) | **0x801795A4** (**0x44**, seventeen entries) | **0x801795A4** ✓ |
| `.bss` | 0x80175600 (0x37C98) | **0x80179600** (size 0x37BD8, placed **0x37AD0**, fill 0x108) | 0x80179600, placed 0x37AD0 ✓ |
| `__bss_end` | 0x801AD298 | **0x801B11D8** | ~0x801B11E0 .. 0x801B1210 (8 low) |
| image | 1529316 | **1545704** | **1545704** ✓ |
| headroom | 1387880 | **1371688** | ~1371656 |

**The 16 KB boundary moved for the third time in the walk** (335, 338, 341), and this is the first crossing
that was *structural* rather than measured-and-hopeful: `.text` ended 0x240 past 0x8015C000, so `.data` could
not have stayed put. Because this object brings no `.data` at all, the whole 0x4000 step is the boundary, and
`.sysctl_set`, `.init_array` and `.bss` follow it by the same 0x4000.

`__bss_end` and the headroom are the shortest-lived rows in the walk so far — 0x801AD298 sat there for two
steps (339, 340) because `.bss`'s start moved +0x40 while its size moved −0x40 — and one boundary crossing
retires both.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000006f
 xnu_entry_why=0x8013ca14   xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8011b104                           (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000                           <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN11IOCatalogue10initializeEv
```

`tools/host_resolve_entry_addr.sh 0x8011b104` → **`iokit_post_constructor_init+0x18`**, `caller-4` =
`0x8011b100: bl 8013b9bc <_ZN11IOCatalogue10initializeEv>` — the name **and** the key both as predicted, and
the same call 340's abort had pre-empted one instruction earlier.

**`abort_entries=0` is the measurement of the cure**, and it requires all three of: the `.rodata` word
received its link-time relocation; `_GLOBAL__sub_I_OSBoolean.cpp` ran under XNU's scan and
`OSBoolean::initialize()` stored the two instances; and `setProperty(gIOConsoleLockedKey, kOSBooleanTrue)`
took the object and returned. Then `IOService::initialize` ran its remaining 0x12C bytes — `operator new`, the
`IORegistryEntry` base constructor, `instanceConstructed`, `IOLockAlloc`, two `OSArray::withCapacity` — and
returned, and the constructor advanced one call. **The key at +0x18 is a forward move in the same frame for
the third time in the walk** (337, 339, 341).

**One correction the run makes to 340's write-up.** The `why_byte` telegraph is `'e'` for an abort and
**`'a'`** for a stub stop — the first byte of *"a symbol this image does not provide was called"* — not the
`'s'` the 340 block guessed from the wording of the `stub_hit=` line. The instrument reports the first byte of
the message, and the message begins with the article.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301642 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 342 — `iokit/Kernel/IOCatalogue.cpp`

`iokit_Kernel_IOCatalogue.o` (16428 bytes, in the pool) defines this step's stop. What makes it more than a
name is what follows it in the constructor's line, which the linked image now shows in one screen:

```
8011b100: bl <_ZN11IOCatalogue10initializeEv>          <- 341's stop
8011b104: bl <_ZN6OSKext10initializeEv>                 real — 331 stopped inside it, 332 cured the sKextLock NULL
8011b108: bl <_ZN12IOUserClient10initializeEv>          STUB
8011b10c: bl <_ZN18IOMemoryDescriptor10initializeEv>    STUB
8011b110: bl <_ZN12IORootParent10initializeEv>          STUB
```

So the next stub *after* `IOCatalogue::initialize` is `_ZN12IOUserClient10initializeEv` at
`iokit_post_constructor_init+0x20` — but only if `IOCatalogue::initialize` returns **and** `OSKext::initialize`
(real since 331/332) runs to completion, and 332 already read that function's own stop out of its body: four
`OSArray::withCapacity` (real since 334) and **two `OSSet::withCapacity`** at +0x68..+0xCC. Whether `OSSet` is
still a stub is the question to settle against `xnu_arm_entry_stubnames.txt` before predicting, because if it
is, the run stops *inside* `OSKext::initialize` rather than at +0x20 — a different prediction with a different
object. This is the first step of the walk whose prediction has to be settled by reading a function that sits
*between* the object being linked and the next stub; the key at +0x18 buys `IOCatalogue::initialize`'s
completion and nothing beyond it.
