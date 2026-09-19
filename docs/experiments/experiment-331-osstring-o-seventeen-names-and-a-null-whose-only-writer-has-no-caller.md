# Experiment 331 — `OSString.cpp` linked: seventeen names resolved, and a stop that is a NULL whose only writer has no caller

**Step:** link one object, `libkern/c++/OSString.cpp` (`libkern_c++_OSString.o`, manifest:381) — the object
the previous stop named. Nothing else changes.

**Prediction:** *`OSString::OSString(OSMetaClass const *)`, the stub 330 stopped in, becomes real, and the
walk runs on through `OSSymbol::withCStringNoCopy` and `OSMetaClass::postModLoad`; the counts go 799 → 784
undefined / 690 → 675 function / 109 storage (**15 resolved, 0 added**); `.text`'s placed inputs move by
+0x628..+0x648 (the object's `+0x8B0` text, `+0x04` COMDAT, `+0xAC` rodata, `+0x20`-or-less string bytes,
against `−0x168` retired stub bodies and `−0x1D0` retired name slots); `.init_array` grows for the first
time since 318, **0x18 → 0x1C**; `.bss`'s start does not move; and the stop is read off the new image's
bodies rather than guessed.*

**Result:** **the stop is where it was predicted, to the byte — and it is `exception: data abort` with
`dfar = 0x0000000C` and `pc_abt = 0x8011CEBC` = `IORecursiveLockLock+0x8`, with no `stub_hit=` line at
all.** The counts landed at 17 resolved rather than 15 because the measuring tool had a defect of its own;
everything else above the boundary is the prediction's, and `.text`'s five terms close on +0x640 with the
placed half inside the predicted band.

## The object, and one paragraph of it that the build refuted

`OSString` is the second libkern C++ class object in the walk, and it is the first one whose vtable the 330
step's flag measurement had already named: `-fapple-kext` would have needed `_ZTV8OSString` from an object
this image did not link, and this is that object.

| `libkern_c++_OSString.o` | |
|---|---|
| `.text` | **2224** (0x8B0), 31 functions |
| `.text._ZN8OSString9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **172** (the two 4-byte `metaClass`/`superClass` pointers, `_ZTV8OSString` 0x68, `_ZTVN8OSString9MetaClassE` 0x3C) |
| `.rodata.str1.1` | **32** (five `.L.str*` blocks) |
| `__DATA,__data` | **48** (two 24-byte `VM_ALLOC_SITE_STATIC` sites) |
| `.bss` | **24** (`OSString::gMetaClass`) |
| `.init_array` | 4 |
| definitions / references | 43 / 38 |

The first draft of this table said "**no `.data` and no `__DATA,__data`**", because `size -A` on the object
did not print one. It does not print one because the section is named `__DATA, __data` and the draft was
read off the same command rather than counted against the **section's own delta after the build** — where a
missing section shows up as an unexplained term and not as an absent line. `.data` grew by exactly 0x30 and
the two `site` variables are exactly 0x30.

## The counts, and a tool that was wrong about them

**Predicted 15 resolved / 0 added; measured 17 / 0.** The two extra names are `OSString::gMetaClass`
(24 bytes, `.bss`) and `OSString::metaClass` (4 bytes, `.rodata`), both of which the image was carrying as
**storage stand-ins** — so the prediction's arithmetic was right about the *functions* and blind to the
*data*, and the tool is why.

`what_does_it_add.py` reads the generator's own `xnu_arm_entry_stubnames.txt` and admits records by field
count. That file has **two record shapes**: 675 lines of `func NAME T` and 107 lines of
`data NAME TYPE SIZE`, and the parser's `if len(f) == 3` silently dropped every `data` line. This is the
third time in this walk that a hand-written parser of a generated list has been the defect (320's map
parser dropped every two-line mergeable record). The counts are what caught it: a `data` record that
disappears from the model makes a *resolved* name disappear with it.

```
   675 func  (3 fields)
   107 data  (4 fields)     <- the shape the parser did not admit
   782 total
```

**Once the counts disagreed, the true split is measurable directly**: 799 → 782 is −17, function 690 → 675
is −15, storage 109 → 107 is −2, and the retired stand-ins are visible by name in the pre-build stub list.

## Layout: five measured terms and a fill term, closing on +0x640

| | 330 | 331 | delta |
|---|---|---|---|
| undefined / function / storage | 799 / 690 / 109 | **782 / 675 / 107** | −17 / −15 / −2 |
| `.text` | 0x1429A0 | **0x142FE0** | **+0x640** |
| `.data` | 0x80144000 (0x19368) | **0x80144000 (0x19398)** | 0, size +0x30 |
| `.sysctl_set` | 0x8015D368 (0x10C) | **0x8015D398 (0x10C)** | +0x30, size 0 |
| `.init_array` | 0x8015D474 (0x18) | **0x8015D4A4 (0x1C)** | +0x30, size **+4** |
| `.bss` | 0x8015D4C0 (0x378D8) | **0x8015D4C0 (0x37858)** | start 0, size **−0x80** |
| `__bss_end` | 0x80194D98 | **0x80194D18** | −0x80 |
| image | 1430668 (0x15D48C) | **1430720 (0x15D4C0)** | +0x34 |
| headroom | 1487464 | **1487592** | +0x80 |

`.text`'s growth is the map's own **placed-input total**, not arithmetic on section sizes — 0x141C8B →
0x1422BC is +0x631, and the fill is +0x00F on top (0xD15/57 → 0xD24/59 records), so **+0x631 + 0x00F =
0x640**, with no residual:

| term | bytes |
|---|---|
| this object's `.text` | **+0x8B0** |
| its COMDAT `.text._ZN8OSString9MetaClassD0Ev` | **+0x004** |
| its `.rodata`, placed whole | **+0x0AC** |
| its string bytes, as placed | **+0x009** of 0x20 — 23 bytes deduped, the smallest placement of the walk after 319's 0% |
| the fifteen retired stub bodies | **−0x168** (`realstubs.o` `.text` 16560 → 16200) |
| their fifteen name slots in the merged pool | **−0x1D0** (`realstubs.o` `.rodata.str1.4` 14575 → 14111) |
| `.text` fill | **+0x00F** |

The `+0x631` placed half sits inside the predicted `0x628..0x648`, and the fill is the term a band over the
inputs cannot contain.

## Above the boundary: `.data` does not move, and `.bss` shrinks by exactly two slots

`.text` ends at 0x80142FE0, **0x1020** below the 16 KB boundary at 0x80144000, so `.data` stays where it is
and grows by exactly this object's own data: 0x19368 → **0x19398**, the two 24-byte sites placed at
0x8015D368 and 0x8015D380, with `.data`'s fill unchanged at 0x7AA3. `.sysctl_set` follows at
0x8015D398 (size unmoved). `.init_array` grows **0x18 → 0x1C** — the first growth since 318 — and its
seventh entry ends at exactly 0x8015D4C0 = `align64(0x8015D4C0)`, which is why **`.bss`'s start does not
move**.

**`.bss` shrinks by 0x80, and all three numbers are measured:**

- **−0x80 placed slots.** `realstubs.o`'s own `.bss` goes 7172 → 7044. The two storage stand-ins the
  generator sized from `nm -S` — `OSString::gMetaClass` (0x18) and `OSString::metaClass` (0x4) — each
  occupied a **0x40-byte 64-aligned slot**, so retiring both frees 0x80 and not the 0x1C they hold.
- **+0x18 free.** The arriving definition costs nothing: the real `gMetaClass` is placed at **0x8019315C**,
  in the gap between `_ZL4pool` (ends 0x8019315C) and `zombproc` (0x80193180) that was *fill* in 330.
- **−0x18 fill.** `.bss`'s fill falls 0x121 → 0x109, exactly the 0x18 the definition takes.

`−0x68` placed plus `−0x18` fill is the `−0x80`, and `__bss_end` follows it down to 0x80194D18. One name
leaves `.bss` altogether: `OSString::metaClass` is `R` in the object, so it is real read-only data at
**0x8013ED00**, the first byte of this object's `.rodata`.

## The image, symbol for symbol

330's image rebuilds byte-for-byte from the same sources in this session — `799/690/109`, `.text`
0x1429A0, `.data` 0x80144000/0x19368, `.init_array` 0x8015D474/0x18, `.bss` 0x8015D4C0/0x378D8,
`__bss_end` 0x80194D98, image 1430668, headroom 1487464 — so the two symbol tables compare directly. Of
7798 symbols **none is retired and 21 are added**: the object's whole public surface, including
`_ZTV8OSString` and `_ZTVN8OSString9MetaClassE` (which the 330 block measured as what `-fapple-kext` would
have added as storage stand-ins from an unlinked object), the two `site` variables,
`_GLOBAL__sub_I_OSString.cpp`, and the `C1`/`C2` alias pairs. The 17 resolved names move:

| symbol | 330 | 331 |
|---|---|---|
| `OSString::OSString(OSMetaClass const *)` | 0x80126058 (a stub) | **0x80122074** |
| `OSString::withCStringNoCopy` | 0x80125FF8 (a stub) | **0x801224A4** |
| `OSString::serialize` | 0x80126160 (a stub) | **0x801227C8** |
| `OSString::gMetaClass` | 0x80193280 (a 0x40 slot in `realstubs.o`) | **0x8019315C** (real, in 330's fill) |
| `OSString::metaClass` | 0x80193240 (a 0x40 slot in `realstubs.o`) | **0x8013ED00** (`R`) |

Nothing below 0x8019315C moves.

## The stop: a NULL that only an uncalled function would have filled

331's stop is not a stub, and the walker says so before the run does:
`tools/xnu_entry_callwalk.py` reports *no stub on the straight-line path* from `postModLoad`,
`OSSymbol::withCStringNoCopy`, `OSKext::lookupKextWithIdentifier`,
`OSString::initWithCStringNoCopy`, `OSSymbolPool::insertSymbol`, `OSlibkernInit`, `panic` and
`DebuggerTrapWithState`. The only two stubs near the path are one guard away and **not taken**:

- `OSKextVLog+0x214 → OSNumber::withNumber` needs `logForUser`, i.e. `sUserSpaceLogSpecArray` and
  `sUserSpaceLogMessageArray`, which only `OSKext::setUserSpaceLogging` creates;
- `panic_trap_to_debugger+0x7c → PEHaltRestart` needs `CPUDEBUGGERCOUNT > NESTEDDEBUGGERENTRYMAX`, i.e. a
  **sixth** nested panic. (So the panic path, which the walk does reach in principle, is not the stop:
  `ml_wants_panic_trap_to_debugger()` returns FALSE and `CPUDEBUGGERCOUNT` is 1, so the first panic would
  have ended in the `udf` inside `DebuggerTrapWithState` instead.)

The path after 330's stop, in execution order, read off `OSMetaClass.cpp:589` and the built bodies:

```
withCStringNoCopy returns the new OSSymbol (its five remaining calls are all real) ...
:626  if (!sStalled->count) break;      count is 7 - the .init_array table's seven entries are the
                                        seven _GLOBAL__sub_I_*.cpp functions, and each contains exactly
                                        one bl OSMetaClass::OSMetaClass(char const*, OSMetaClass const*,
                                        unsigned int), measured by disassembling all seven
:630  myKext = OSKext::lookupKextWithIdentifier(myKextName)   -> NULL, there is no kext registry
:631  if (!myKext) { result = kOSMetaClassNoKext; OSKextLog(...); break; }

and it stops one call before that log, inside the lookup:
 8010d87c <_ZN6OSKext24lookupKextWithIdentifierEP8OSString>:      (the const char * overload is
  880  ldr r1, [r0]          ; vptr                                 inlined into this one)
  884  ldr r1, [r1, #0x50]   ; OSString::getCStringNoCopy           [real]
  888  blx r1
  898  ldr r0, [r5]         ; r5 = 0x80192D90 = sKextLock, .bss, ZERO
  89c  bl IORecursiveLockLock                          <- lr = 0x8010D8A0 = +0x24
 <IORecursiveLockLock>: e92d4830 push {r4, r5, fp, lr}
                        e1a04000 mov  r4, r0
                        e590500c ldr  r5, [r0, #12]    <- r0 = 0: the abort
```

**`sKextLock` is NULL because nothing in this image ever calls `OSKext::initialize()`**, and that is a
property of the link: the only `bl _ZN6OSKext10initializeEv` in the whole image is inside
`iokit_post_constructor_init+0x18`, whose only caller is `libsa/lastkernelconstructor.c`'s constructor —
an object this image does not link, so `iokit_post_constructor_init` has no caller either. Everything
`OSKext::initialize` initialises is `.bss`-zero: `sKextLock` 0x80192D90, `sKextLoggingLock` 0x80192D9C,
`sKextsByID` 0x80192DA0. The two locks that *are* live, `sStalledClassesLock` 0x801930CC and
`sAllClassesLock` 0x801930C4, are `IOLibInit`'s — which is why the walk has been able to run through
`preModLoad` at all.

It is the same **shape** as 328's stop with a different writer: 328's `_ZL4pool` had a writer in the same
object whose initialiser nothing ran; this one's writer is in the image and *its* caller is not.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: exception: data abort
 xnu_entry_kv_written=0x00000000     <- no stub_hit exists, and none was recorded
 xnu_entry_abort_entries=0x00000001  <- the first abort: nothing faulted earlier on the path
 xnu_entry_abort_first_dfar=0x0000000c
 xnu_entry_abort_first_pc=0x8011cebc
 xnu_entry_abort_first_dfsr=0x00000005
 xnu_entry_abort_first_lr=0x8011cec4
 xnu_entry_abort_first_insn=0xe590500c
```

`0x8011CEBC` resolves against the image the device ran to **`IORecursiveLockLock+0x8`**, and the word
there is `e590500c` = `ldr r5, [r0, #12]` with `r0 = 0` — the `sKextLock` load at
`_ZN6OSKext24lookupKextWithIdentifierEP8OSString+0x20`. **Every field is the prediction's**: `dfar` 0xC,
`dfsr` 5 (translation fault, read), the instruction, the function. The one thing the log cannot show is
the frame above, and the disassembly is where that is settled — the `bl` is at 0x8010D89C and `lr` is
**0x8010D8A0** = `lookupKextWithIdentifier(OSString *)+0x24`.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img` (4346 KB), nothing flashed,
`25` records of `persistent_write_attempted=0x00000000` and `87` of `failure_mask=0x00000000` with **no
non-zero reading of either**, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301505 bytes, and the
device back on Android on its own (`MI 4LTE`, release 10).

## What this step measures

The frontier has left the stub list, and the sentence that says so is `xnu_entry_kv_written=0`. What stops
the boot is no longer a missing symbol, an invented zero or a string this build supplies: it is a **static
pointer whose only writer is in the image and whose only caller is not**.

So the next step is not another object. It is a **call**: either link the constructor object that makes
`OSKext::initialize()` run at the end of the constructor table, or perform the same initialisations
deliberately. Which of the two depends on what `OSKext::initialize()` itself needs, and that is the next
step's measurement.

```
    # 332: `libsa/lastkernelconstructor.c` - the object whose constructor is the only caller of
    #      `iokit_post_constructor_init`, and therefore the only way `OSKext::initialize()` runs
```
