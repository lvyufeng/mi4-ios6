# Experiment 328 — `libkern/c++/OSSymbol.cpp`: a real definition holding zero, and the first stop that is not a stub

**Step:** link **`libkern/c++/OSSymbol.cpp`** (`out/xnu_kernel_obj/libkern_c++_OSSymbol.o`,
manifest:380) — the object that defines the current stop.

**Prediction:** *counts **6 resolved / 14 added** read by kind (801/692/109); `.text` **+0x1300 .. +0x1325
plus a fill band**; `.data` +0x90, `.init_array` a sixth entry, `.bss` +0x1C before fill; predicted stop
**`_ZN8OSStringC2EPK11OSMetaClass` at the caller key `_ZN8OSSymbol17withCStringNoCopyEPKc+0x50`**.*

**Result:** **every count and every address exact, and the stop is not a stub at all.** The run reports no
`stub_hit=` line: the walk ran past the three stubs that had stopped 325, 326 and 327, entered
`withCStringNoCopy` at 327's own caller key, and took a **`data abort` at `withCStringNoCopy+0x14`** —
`ldr r0, [r0, #16]` with `r0 = 0`, the faulting address 0x10. The null is **`_ZL4pool`**, this object's own
`static OSSymbolPool *`, and its only writer is a function whose only reachable caller is `.init_array`
entry #6 — a table nothing runs. **The frontier has left the undefined-symbol list: it is now a value the
image never writes.**

## The object, and a baseline that reproduces 327

`libkern/c++/OSSymbol.cpp` is `OSSymbol` (the interned-symbol class) and `OSSymbolPool` (its hash table and
lock): the two `MetaClass`es and their stand-ins, the constructors/destructors, `withCString`,
`withCStringNoCopy` and `withString` — all three going through the object's own pool under a lock —
`bsearch`, `checkForPageUnload`, `serialize`, and the pool's `init`/`insertSymbol`/`findSymbol`/
`removeSymbol`/`reconstructSymbols`.

| | size |
|---|---|
| `.text` | **0x10A4**, 60 definitions |
| COMDAT `.text._ZN8OSSymbol9MetaClassD0Ev` | **4** |
| `__DATA, __data` | **0x90** |
| `.rodata` | **0xC0** (192) |
| `.rodata.str1.1` | **0x25** (37) |
| `.bss` | **0x1C** |
| `.init_array` | **4** |
| references | **54** |

The baseline was built in this session with an **empty stand-in object** in this slot and reproduced 327's
image to the byte (`793/684/109`, `.text` 0x1416A0, `.data` 0x80144000/0x192D8, `.sysctl_set`
0x8015D2D8/0x10C, `.init_array` 0x8015D3E4/0x14, `.bss` 0x8015D400/0x37898, `__bss_end` 0x80194C98, image
1430520, headroom 1487720).

|  | predicted | measured |
|---|---|---|
| undefined | 801 | **801** |
| function stubs | 692 | **692** |
| storage stubs | 109 | **109** |
| resolved / added | 6 / 14 | **6 / 14** |

Six resolved: **five function** stubs (`withString`, `withCString`, `withCStringNoCopy` — the current stop
— `checkForPageUnload`, and `bsearch`, which 325 created) and **one storage** stand-in
(`OSSymbol::metaClass`). Fourteen added: **thirteen function** names and **one storage** stand-in, every
one of them `OSString`'s, because `libkern/c++/OSString.cpp` is not linked yet.

## `.text` closes in three terms, and the string pool dropped 9 bytes

`.text` 0x1416A0 → **0x1429C0** is +0x1320:

```
  this object's contributions                          +0x1184   = 0x10A4 `.text` + 0x004 COMDAT
                                                                  `.text._ZN8OSSymbol9MetaClassD0Ev`
                                                                  + 0x0C0 `.rodata` + **0x01C of 0x025** —
                                                                  the pool dropped 9 of the object's 37
                                                                  string bytes, so the three steps of 100%
                                                                  placement end with a small drop, and the
                                                                  predicted range's top (+0x1325) is 9
                                                                  bytes high by exactly that
  the stub object's .text + names                      +0x198   (13 bodies 0x138 + 13 names 0x18C
                                                                  created against 5 bodies 0x78 and 5 names
                                                                  0xB4 retired — the created half larger
                                                                  than the retired one for the first time
                                                                  since 323, and counted on both sides)
  .text-region alignment fill                          +0x004   (0xD2D -> 0xD31; 59 -> 60 fills)
                                                      -------
                                                       +0x1320   against a measured +0x1320
```

The region identity agrees exactly: Σ(placed inputs) +0x131C, Σ(fill) +0x4, sum +0x1320. The object's own
pieces are placed at 0x80120FCC (`.text`), 0x80122070 (COMDAT), 0x8013E504 (`.rodata`) and 0x8013E5C4 (the
0x1C of strings). The dedup measurements now read **100% dropped** (319), **63%** (321), **4.7%** (323),
**2.0%** (324), **0%** three times (325, 326, 327) and this step's small **24%** — the term is real, small,
and not predictable from the object.

## Everything after `.text` moved exactly as the object says

| | base (327) | measured (328) | delta | predicted |
|---|---|---|---|---|
| `.text` | 0x1416A0 | **0x1429C0** | +0x1320 | +0x1300 .. +0x1325 + band |
| `.data` | 0x80144000 (0x192D8) | **0x80144000** (**0x19368**) | +0x90 | +0x90 — exact, fill unmoved (0x7AA7 / 8 fills in both, the third step running) |
| `.sysctl_set` | 0x8015D2D8 (0x10C) | **0x8015D368** (0x10C) | +0x90 | follows `.data` |
| `.init_array` | 0x8015D3E4 (0x14) | **0x8015D474** (**0x18**) | +0x90 start, +0x4 size | a sixth entry nothing runs |
| `.bss` | 0x8015D400 (0x37898) | **0x8015D4C0** (**0x378D8**) | +0xC0 start, +0x40 size | +0x1C before fill |
| `__bss_end` | 0x80194C98 | **0x80194D98** | +0x100 | |
| image | 1430520 (0x15D3F8) | **1430668 (0x15D48C)** | +0x94 | ~1430652 |
| headroom | 1487720 | **1487464** | −0x100, follows `__bss_end` | |

The arithmetic is checkable on its own: the image is `align16K(0x1429C0) + 0x19368 + 0x10C + 0x18` =
0x144000 + 0x19368 + 0x10C + 0x18 = **0x15D48C**, and `__bss_end` = 0x8015D4C0 + 0x378D8 = **0x80194D98**.
`.text` ends **0x1640 below the 16 KB boundary** — the second-tightest margin of the walk after 324's
0x580 — and it holds, so only the object's own data moved.

`.bss` grew **0x40** and closes on the two moves the object makes: Σ(placed inputs) **+0x1C** (its own
0x1C, one 64-byte stand-in retired and one created, netting zero) with the section's fill **rising** a
further **0x24** (0xFD / 39 fills → 0x121 / 40 fills). Its *start* moved +0xC0 for a reason with nothing to
do with `.bss`: it is align64 of the preceding end, and `.data` +0x90 with `.init_array` +0x4 pushed that
end from 0x8015D3F8 to 0x8015D48C. **Five steps give five signs of the fill's move** — 324 −0x30 (slot
re-used), 325 +0x28 (inputs pushed), 326 −0x1C and 327 −0x18 (slot handed back), 328 **+0x24** (pushed
again) — and the rule is unchanged: the inputs are predicted, the fill's sign and size are read.

## The run: not a stub, and not a symbol

```
MI4IOS6_STAGE90_XNU real XNU entry: exception: data abort
 xnu_entry_abort_entries=0x00000001
 xnu_entry_abort_first_pc=0x80121c78      _ZN8OSSymbol17withCStringNoCopyEPKc+0x14
 xnu_entry_abort_first_insn=0xe5900010    ldr r0, [r0, #16]
 xnu_entry_abort_first_dfar=0x00000010    0 + 16: the faulting address
 xnu_entry_abort_first_dfsr=0x00000005    a translation fault, not a permission fault
 xnu_entry_abort_first_lr=0x80121c80      the *callee's* own return address, not a caller's
 xnu_entry_why=0x80126ce4                 "exception: data abort"
 xnu_entry_stub_caller_digits=0x00000000  no stub was entered at all
```

**The caller-key idiom has no `bl` to report here, and what the record leaves instead is what names the
stop.** `digits = 0` says no stub was entered, and the two words that follow it are read at `g_kv_buf[0]`
(`entry_stubs.c:1062-1067`) — the head of the key/value buffer — so they come out as `0x756e7820` /
`0x746e655f`, i.e. ` xnu_ent`, this instrument's own first record, and not a caller. What names the stop is
`abort_first_pc` together with `abort_first_insn` and the disassembly of the function they land in:

```
80121c64 <_ZN8OSSymbol17withCStringNoCopyEPKc>:
 80121c64  push {r4, r5, r6, r7, fp, lr}
 80121c68  movw r7, #0x3158 ; movt r7, #0x8019      r7 = &_ZL4pool = 0x80193158
 80121c6c  mov  r5, r0
 80121c74  ldr  r0, [r7]                            r0 = pool = 0
 80121c78  ldr  r0, [r0, #16]                       **the abort**: pool->lock, at 0x10
 80121c7c  bl   lck_mtx_lock                        (real, 0x80013f34)
 80121c80  ldr  r0, [r7]                            lr on entry to the fault, hence the record
 80121c88  bl   _ZNK12OSSymbolPool10findSymbolEPKc  (real, in this object)
 80121c9c  bl   _ZN8OSObjectnwEm                    (real since 326)
 80121cb0  bl   _ZN8OSStringC2EPK11OSMetaClass      (a stub — the predicted stop, at +0x4C)
```

So the prediction was right about the call **and** right about the key: the `bl` is at +0x4C and its key is
+0x50. The run never reached it — it faulted one instruction into the body's second statement, 0x38 before
that `bl`. The prediction read `lck_mtx_lock(pool->lock)` as an ordinary call: it asked whether
`lck_mtx_lock` was defined (it is) and never asked what `pool` would *hold*.

## Why the pool is zero, and why no link can fix it

`_ZL4pool` is a four-byte `b` symbol at **0x80193158** in `.bss`. The image writes it in exactly one
place: `str r4, [r6]` at **`OSSymbol::initialize()+0x88`** (0x80121AF8), where `r6 = 0x80193158`. That
function is present, correct and correctly sized — and in the whole image it has exactly two callers:

| caller | site | who calls *it* |
|---|---|---|
| `OSSymbol::MetaClass::MetaClass()+0x30` | 0x80121A64 | **nothing** — zero `bl` sites in the image |
| `_GLOBAL__sub_I_OSSymbol.cpp+0x38` | 0x80122050 | `OSRuntimeInitializeCPP`'s constructor scan — which finds nothing to call |

`_GLOBAL__sub_I_OSSymbol.cpp` is at **0x80122018**, and it is **entry #6 of `.init_array`**:

| # | entry | initializer | what it does |
|---|---|---|---|
| 1 | 0x8011AEF4 | `_GLOBAL__sub_I_OSKext.cpp` | `OSMetaClass::OSMetaClass("OSKext", …)`, vtable, `b __cxa_atexit` |
| 2 | 0x8011F3C8 | `_GLOBAL__sub_I_OSMetaClass.cpp` | same shape |
| 3 | 0x801209D0 | `_GLOBAL__sub_I_OSDictionary.cpp` | same shape |
| 4 | 0x80120DE0 | `_GLOBAL__sub_I_OSObject.cpp` | same shape |
| 5 | 0x80120F74 | `_GLOBAL__sub_I_OSCollection.cpp` | same shape |
| 6 | 0x80122018 | `_GLOBAL__sub_I_OSSymbol.cpp` | `…`, then `bl OSSymbol::initialize` at +0x38 |

Nothing in this image runs any of them, and 318 said why: XNU's own `OSRuntimeInitializeCPP`
(`libkern/c++/OSRuntime.cpp:403`) finds constructors by **section name** — `sectionIsConstructor`
(`:246`) accepts `__mod_init_func` or `__constructor` — and `entry_macho.s` describes the `__DATA`
segment with one section, an empty `__const`. That scan really runs (324 watched its
`strncmp(sectname, …, 15)` / `13` pair execute and match nothing); it simply has no section to match.

So this step went in with a name that is *defined* and came out with a value that is *zero because the
initializer that writes it is unreachable*. That is the third kind of stop in its sharpest form: not a
missing symbol and not a stand-in, but a real definition holding zero between its linker-assigned address
and its first assignment.

**And what the walk proves by not stopping earlier is one step stronger than 326 and 327 proved.** The
three stubs that ended the last three runs are all real now: `withCapacity` ran its whole body,
`OSCollection::OSCollection` returned, `postModLoad` made it to its fifth call, and the entry into
`withCStringNoCopy` *is* that fifth call — 327's key `_ZN11OSMetaClass11postModLoadEPv+0x108`. The
frontier is 20 bytes into the function 327's run stopped at the door of.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, `STAGE90_HW_WATCHDOG ARMED`, software dead-man armed,
no storage symbols in the payload), log **301505** bytes, **no `stub_hit=` line**, one
`exception: data abort` line, `xnu_entry_failures=0x00000000`, `xnu_entry_image_bytes=0x0015d48c`.

**Safety:** this is the walk's first fault rather than a stop, and the nets held — the hardware watchdog is
the only net across the jump, and it did not have to fire: the device returned to Android on its own
(`ro.build.version.release` = 10). Nothing was flashed (`fastboot boot` only),
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`disarm_hw_watchdog_en=0x00000001`.

## What it measures, and what it does not

Measured: **the frontier has left the undefined list.** Every stub the walk has stopped at since 324 is
real, nothing on the walked path is undefined any more, and the boot is stopped by a value —
a pointer in `.bss` that the image never writes. Not measured: what `withCStringNoCopy` would do with a
live pool (`findSymbol`, `insertSymbol`, `initWithCStringNoCopy`, the `OSString` constructor, the `blx`
through the vtable at +0xA4); what the six constructors would do; and whether
`OSMetaClass::OSMetaClass("OSKext", &OSObject::gMetaClass, 96)` — real since 324 — would itself run clean.

## Next

**`__mod_init_func` — the step that makes the initializer reachable, and why it is not an object.** No XNU
object can fix this stop: `pool`'s writer is a constructor, the constructor is in the image and is
correct, and what is missing is its caller. So the step is a *header* change: describe `.init_array` —
`addr = __entry_init_array`, `size = __entry_init_array_size`, both already exported by `entry.ld` — as a
**`__mod_init_func`** section of the `__DATA` segment (`nsects` 1 → 2, `cmdsize` 124 → 192, `sizeofcmds`
236 → 304, the header 68 bytes larger inside `.rodata.macho`, which shifts `.text`'s end +0x44), and
extend `tools/host_entry_macho_check.sh` — which already walks `nsects`, validates `cmdsize` and
`sizeofcmds` and asserts the `__const` section by name — with a second section assertion.

Then XNU's own scan calls the six entries in table order and **the predicted stop is the first stub inside
the first of them: `__cxa_atexit`** (already written down in `out/stage90/xnu_arm_entry_undef.txt`, line 54,
a function stub; `__dso_handle` is line 70). `_GLOBAL__sub_I_OSKext.cpp` is `OSMetaClass::OSMetaClass
("OSKext", &OSObject::gMetaClass, 0x60)` at +0x24 (real, 0x8011DEE4, and all eight of its own callees are
defined), the vtable store, then **`b __cxa_atexit` at +0x50 — a tail branch, not a `bl`**. So the stop
should be `stub_hit=__cxa_atexit` with `xnu_entry_stub_caller=0x8011d750` = `OSRuntimeInitializeCPP+0x184`,
whose `caller-4` is the `blx r0` at 0x8011d74c — **the constructor call in XNU's own scan loop**, which is
what the key would name and what makes this step's mechanism visible in the log.

And the step after that cannot be an object either: `__cxa_atexit` is defined **nowhere in XNU**
(`grep -rn __cxa_atexit external/xnu-upstream/` returns nothing, and neither does `__dso_handle`). A kernel
has no `atexit`, which is why Apple's own kernel objects do not call it — the call is in these objects
because they were compiled by `arm-none-eabi-gcc` without `-fno-use-cxa-atexit` (Apple's `-fapple-kext`
implies it). The header makes the constructors *reachable*; that flag makes them *runnable*.
