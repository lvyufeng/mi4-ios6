# Experiment 340 — `OSOrderedSet.cpp` linked: the first abort in eight steps, and a zero the walk had written down in advance

**Step:** link one object, `libkern/c++/OSOrderedSet.cpp` (`libkern_c++_OSOrderedSet.o`) — the only object in
the pool that defines 339's stop, `_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_`. Nothing
else changes.

**Prediction:** *2 resolved / 0 added — 844 → **842** undefined, 735 → **734** function, 109 → **108**
storage; `.text` +0xA74 to +0xB0D in six terms, ending between 0x8015BE60 and 0x8015BE6D, inside 0x193 of the
0x8015C000 boundary; `.data` +0x30; `.bss` placed −0x28;* and **the stop at
`_ZN11IOCatalogue10initializeEv` on `iokit_post_constructor_init+0x18` — two frames above the object being
linked.**

**Result:** the counts exact, all six `.text` terms closing on +0xB00 (placed +0xB11, fill −0x11), `.text`
ending at the bottom of the predicted range — **and the run did not stop on a stub at all.** It reports
`exception: data abort`, `DFAR=0`, at `IOService::initialize+0x5C8`: the load of `kOSBooleanTrue`, a 4-byte
storage stand-in whose value is zero, dereferenced by the `setProperty(...)` call seven source lines below
339's stop. The first abort in eight steps (every run from 332 to 339 ended on a `stub_hit`), and **the
hazard 332 had already written down as a prediction** — same declaration, same `dfar = 0`, at a different one
of the **twelve sites in this image that read `kOSBooleanTrue`**.

## The object

| `libkern_c++_OSOrderedSet.o` | |
|---|---|
| `.text` | **2676** (0xA74), 30 functions |
| `.text._ZN12OSOrderedSet9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **232** (0xE8) |
| `.rodata.str1.1` | **13** (0x0D) |
| `__DATA, __data` | **48** (0x30) |
| `.bss` | **24** (0x18 — `OSOrderedSet::gMetaClass`) |
| `.init_array` | 4 (`_GLOBAL__sub_I_OSOrderedSet.cpp`) |
| definitions / references | 49 / 42 |

**2 resolved, 0 added** — `withCapacity` and `OSOrderedSet::metaClass`, and all 42 references already
satisfied. What made this step's stop prediction possible is that **every one of those 42 references is a real
symbol rather than a stub**: `withCapacity` calls only `OSObject::operator new` (real since 326),
`OSCollection::OSCollection(metaClass)` (326) and `OSMetaClass::instanceConstructed` (324); its
`initWithCapacity` calls `OSCollection::init`, `kalloc_canblock`, `bzero` and `OSAddAtomic`; and its
constructor calls the real `OSMetaClass::OSMetaClass(char const *, OSMetaClass const *, unsigned int)` and
`__cxa_atexit`. The whole 42 were checked against `xnu_arm_entry_stubnames.txt` by name, because
`tools/entry_object_effect.py`'s "already satisfied" is about the *image*, and a name the image provides as a
stub is in the image too.

| | 339 | 340 | delta |
|---|---|---|---|
| undefined / function / storage | 844 / 735 / 109 | **842 / 734 / 108** | −2 / −1 / −1 |

The call site is what the two instructions in front of it say:

    8012aa0c: mov r0, #10
    8012aa10: mov r1, #0
    8012aa14: bl  <_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_>

    gJobs = OSOrderedSet::withCapacity(10);       IOService.cpp:418 (declared :183)

— the single-argument form with the *default* comparator, so this step is not about the match-ordering
machinery: it is the ordered container IOKit keeps its job and probe lists in.

## `.text`: six terms, all closing

| term | predicted | measured |
|---|---|---|
| this object's `.text` | +0xA74 | **+0xA74** |
| its COMDAT | +0x004 | **+0x004** |
| its `.rodata`, placed whole | +0x0E8 | **+0x0E8** |
| its string bytes, as placed | +0x000 .. +0x00D | **+0x00D** — all 13 placed, no dedup |
| the one retired stub body | −0x018 | **−0x018** |
| its one name slot | −0x044 | **−0x044** |

Sum **+0xB11**, the measured placed move (0x15A628 → **0x15B139**), with the fill 0xD38 → **0xD27** (−0x11),
so `.text` is **+0xB00** (0x15B360 → **0x15BE60**) — the *bottom* of the predicted range 0x8015BE60 ..
0x8015BE6D, i.e. every one of the 13 string bytes placed with none deduped, the first full string term of the
walk. `realstubs.o` confirms all three retired terms by section, to the byte: `.text` 0x44E8 → **0x44D0**
(−0x18), `.rodata.str1.4` 0x4278 → **0x4234** (**−0x44**, the counted `align4(64+1)` slot for the longest stub
name the walk has retired — `withCapacity`'s mangled name) and `.bss` 0x1C04 → **0x1BC4** (−0x40).

## The layout, and one derived row with a sign error

| | 339 | 340 measured | 340 predicted |
|---|---|---|---|
| `.text` | 0x8015B360 (0x15B360) | **0x8015BE60** (0x15BE60) | 0x8015BE60 .. 0x8015BE6D ✓ |
| `.data` | 0x8015C000 (0x19468) | **0x8015C000 (0x19498)** | +0x30 ✓ |
| `.sysctl_set` | 0x80175468 (0x10C) | **0x80175498** (0x10C) | 0x80175438 ✗ |
| `.init_array` | 0x80175574 (0x3C) | **0x801755A4** (**0x40**, 16) | 0x80175544 ✗ |
| `.bss` | 0x801755C0 (0x37CD8) | **0x80175600** (**0x37C98**) | 0x801755C0 ✗ |
| `__bss_end` | 0x801AD298 | **0x801AD298** | ~0x801AD270 ✗ |
| image | 1529264 | **1529316** | 1529220 ✗ |
| headroom | 1387880 | **1387880** | ~1387920 ✗ |

**Every wrong row is wrong in the same direction, and the error is a sign.** `.data` grew by 0x30, so every
section the linker places *after* it starts 0x30 **later** — `.sysctl_set` and `.init_array` at +0x30, `.bss`
at +0x40 — and the table moved them 0x30 and 0x40 **earlier**. This is 338's defect class (a derived row
written as a number rather than as an expression) with the delta's sign reversed this time. The picture in the
prediction was right and the arithmetic under it was applied backwards:

    .init_array ends   0x801755A4 + 0x40 = 0x801755E4
    align64(0x801755E4)                 = 0x80175600      <- where .bss starts
    align64(0x801755B0)                 = 0x801755C0      <- where 339's .bss started

so the one-line argument — "the round-up absorbs the 0x30" — was the wrong case: the round-up absorbed
*nothing* here, because 339's `.bss` start came from an `.init_array` end 0x10 below a 64-byte boundary and
340's came from one 0x1C below it. Two builds, the same idiom, different absorbency.

`__bss_end` and the headroom are **unchanged at 0x801AD298 and 1387880** — `.bss`'s start moved +0x40 and its
size moved −0x40 — which is why the image's own byte count moved +0x34 (its end is `__entry_data_fileoff` plus
the file-backed data, and only `.text` and `.data` are in it).

`.init_array` is **0x40 — sixteen entries**, `.sysctl_set` unmoved in size at 0x10C, and
`_GLOBAL__sub_I_OSOrderedSet.cpp` is the new entry with `last_kernel_constructor` still last.

## `.bss`: the two halves do not cancel this time

Placed **0x37B98 → 0x37B70 = −0x28**, the fifth step running on 331's rule as arithmetic: one retired
64-byte-aligned stand-in gives back its full 0x40 slot and `OSOrderedSet::gMetaClass` (0x18) arrives in its
place. The **fill**, though, went 0x140 → **0x128** (−0x18) instead of moving by +0x28 as it did at 339, so the
size moved the whole 0x40: 0x37CD8 → **0x37C98**. The filled bytes inside `.bss` are the alignment gaps
*between* aligned symbols, and retiring a 64-byte-aligned stand-in changes those gaps — the stand-in's slot is
what carried the 64-byte alignment for its neighbourhood. 339's conclusion survives, restated: `.bss`'s
*placed* bytes are the ledger, and its size moves for reasons that are not the step's — one step it cancels the
placed term exactly, the next it doubles it.

## The run: the walk leaves the stub idiom again, eight steps later

**The predicted stop did not happen.** The run reports an exception, not a `stub_hit`:

```
MI4IOS6_STAGE90_XNU real XNU entry: exception: data abort
 xnu_entry_why=0x8013ca58
 xnu_entry_why_byte=0x00000065                    'e' - the first byte of "exception: ..."
 xnu_entry_stub_caller_v=0x00000000               no stub was entered at all
 xnu_entry_abort_entries=0x00000001
 xnu_entry_abort_first_dfar=0x00000000            <- the faulting address is *zero*
 xnu_entry_abort_first_pc=0x8012aaa0
 xnu_entry_abort_first_dfsr=0x00000005            section translation fault
 xnu_entry_abort_first_lr=0x8012aaa8
 xnu_entry_abort_first_insn=0xe5952000
 xnu_entry_abort_first_sp=0x801765e0
 xnu_entry_abort_first_spsr=0x60000093
 xnu_entry_abort_first_end_kern=0x801ae000
 xnu_entry_abort_first_prelink_b=0x801ad298
```

`xnu_entry_why_byte` is the first byte of the `why` string, which makes it a one-character telegraph of the
stop's *kind*: `'s'` on every stub stop of this walk, and `'e'` here.

`e5952000` is `ldr r2, [r5]`, and the instructions that load `r5` sit 0x1C bytes above it:

```
8012aa7c: movw r1, #51520        ; movt r1, #32794   ->  r1 = 0x801AC940
8012aa84: ldr  r5, [r1]                             ->  r5 = *(0x801AC940) = 0   <- the zero
8012aa94: bl   801253c0 <_ZN15IORegistryEntry15getRegistryRootEv>
8012aa98: ldr  r3, [r0]                              ; the root's vptr
8012aaa0: ldr  r2, [r5]                              ; FAULT, DFAR = 0
8012aaa4: ldr  r3, [r3, #88]                         ; vtable slot 0x58 = setProperty
8012aaa8: blx  r3                                    ; <- the saved lr
```

`0x801AC940` is **`kOSBooleanTrue`**, and `nm` reports it `B`: it is one of the 108 storage stand-ins,
`data kOSBooleanTrue R 0x4` in `xnu_arm_entry_stubnames.txt`, one 0x40-byte slot with `kOSBooleanFalse` next
at 0x801AC980. Why *two* loads for one name is the whole finding — `OSBoolean.cpp` declares it as a reference:

```c
static OSBoolean * gOSBooleanTrue  = 0;                // the pointer
OSBoolean * const & kOSBooleanTrue = gOSBooleanTrue;   // a reference to that pointer
```

so the symbol is the **reference variable**: the first load fetches its referent (the word holding the address
of `gOSBooleanTrue`), the second fetches the `OSBoolean *` itself. **A stand-in gives that variable its right
address and its right 4 bytes and no value whatever**, so the first load returns 0 and the second — the load
that dereferences the boolean — faults at address 0. The source is `IOService.cpp:427`, the line after the
`thread_call_allocate` whose result is stored 0xC bytes above the fault:

```c
gIOConsoleLockCallout = thread_call_allocate(&IOService::consoleLockTimer, NULL);
IORegistryEntry::getRegistryRoot()->setProperty(gIOConsoleLockedKey, kOSBooleanTrue);
```

## What this step measures

**The walk has left the stub idiom again, and this time on a zero it had already named.** Every run from 332
to 339 ended on a `stub_hit`, i.e. on the first frontier kind — a name in the undefined list — and each step
was "find the object that defines it, link it". Two earlier runs left that idiom the other way: 328's
`OSSymbol::withCStringNoCopy+0x14` (`dfar=0x10`, a real definition whose writer has no caller that runs) and
331's `IORecursiveLockLock+0x8` (`dfar=0xC`, `sKextLock` a zeroed stand-in). This stop is the **second kind —
an invented zero — and it is the first of them that was written down before it happened**: 332 read the
declaration out of `OSKext::initialize`'s body and recorded it in the ledger,

> at `+0x2C4` the code does `ldr r2, [r1]` where `r1` was loaded from `kOSBooleanTrue` … the C declaration is
> `extern OSBoolean * const & kOSBooleanTrue`, **a reference** … so a zeroed stand-in is dereferenced twice
> and the fault is `dfar = 0x0` at `OSKext::initialize+0x2C4` with `insn = 0xe5912000`.

and 340 measured it — at a different site. **Twelve places in this image load `kOSBooleanTrue`** (found by
scanning the disassembly for the `movw #0xc940` / `movt #0x801a` pair, the `nm`-confirmed address): five in
`OSKext` methods, one in `OSKext::initialize` at +0x2C4 — 332's site — five more in `IOService`
(`initialize`, two in `updateConsoleUsers`, `setAuthorizationID`) and one in `_OSKextConsiderUnloads`.
`iokit_post_constructor_init` reaches the four `*::initialize` stubs *before* `OSKext::initialize`, so the
site that fires first is `IOService::initialize+0x5C8` (`insn = 0xe5952000`, one register number different
from 332's `0xe5912000` because the compiler picked `r5` here and `r1` there). 332's written site is still
ahead, and it now has a measured precedent behind it rather than a deduction — and so are the other ten: on
this evidence the same zero stops the boot at **each** of the twelve until `OSBoolean.cpp` is linked, which
is the strongest argument this walk has had for a single next object.

The prediction's falsifier did its job and was wrong in the informative way. It said "any `abort_entries=`
non-zero means one of the four `blx` vtable dispatches in that range faulted instead of stopping on a stub".
Something faulted instead of stopping on a stub, exactly as its premise said — but it was not a `blx`, it was
a **global load**, the one instruction class the model treats as free because its symbol is in the stand-in
list. That is the blind spot of the two straight-line tools stated plainly: they answer "is this name in the
image", and a zero-valued stand-in *is* in the image.

**And the fault is 0x84 bytes before the end of a function the walk had already entered twice**, which is the
second thing this run buys: the key at 339 (`+0x544`) and this fault (`+0x5C8`) are the same frame, so
everything between them — `IOService::initialize`'s job-list, busy-lock, console-users-lock, semaphore and
`thread_call_allocate` steps — **ran and returned**. Only the store of `kOSBooleanTrue` into the registry
failed, and it failed on the *value*, not on the API.

**Safety, as every run.** Non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301505 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10). A data abort is an instrument outcome, not a hang: it takes the same exit the stub
stops take.

## Frontier: 341 — `libkern/c++/OSBoolean.cpp`

The object that defines the zero. It defines `kOSBooleanTrue` and `kOSBooleanFalse` (both `R 0x4` stand-ins
today) and `OSBoolean::metaClass` (a `data R 0x4` stand-in), and — the point — its constructor
`_GLOBAL__sub_I_OSBoolean.cpp` runs `OSBoolean::initialize()`, which allocates the two instances and stores
them into `gOSBooleanTrue` and `gOSBooleanFalse`. **That store is what makes the reference non-zero and the
second load legal**, and it is the first time in this walk that the fix is not "resolve the symbol" but
"initialize the value".

Measured by `tools/obj_sections.py` (the tool written for 339's dropped row): `.text` **0x30C**,
`COMDAT` 0x004, `.bss` **0x20** (the two statics `gOSBooleanTrue`/`gOSBooleanFalse`), `.rodata` **0x9C**,
`.rodata.str1.1` **0x1B**, `.init_array` **0x4**. `tools/entry_object_effect.py` reads 36 definitions and 29
references: **4 resolved** (1 function — `OSBoolean::withBoolean`, a stub since 335 — and 3 storage, all three
stand-ins) and **1 added** (`_ZN11OSSerialize15binarySerializeEPK15OSMetaClassBase`, verified absent from
`xnu_arm_entry_stubnames.txt` rather than assumed), so 842 → **841** undefined, 734 → **734** function,
108 → **105** storage.

Three stand-ins retired and one object's `.bss` arriving means the `.bss` placed term is **−0xC0 + 0x20 =
−0xA0** with the `gOSBoolean*` statics placed inside it; whether the fill cancels or doubles that, the last two
steps say, is not something the model decides — it is read off the build.
