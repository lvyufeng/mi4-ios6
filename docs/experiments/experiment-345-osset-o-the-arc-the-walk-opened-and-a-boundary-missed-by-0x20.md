# Experiment 345 — `OSSet.cpp`: the walk closes an arc it opened itself, and the boundary it stopped 0x20 short of

**Step:** link one object, `libkern/c++/OSSet.cpp` (`libkern_c++_OSSet.o`, 9248 bytes) — the object that
defines 344's stop `_ZN5OSSet12withCapacityEj`, and the object that defines the name **343's link invented**.
Nothing else changes.

**Prediction:** *4 resolved / 0 added — 829 → **825** undefined, 728 → **725** function, 101 → **100** storage;
`.text` +0xB7C to +0xB86 in six terms;* and **`_text` ends at exactly `0x8015FFE0`, 0x20 below the
`0x80160000` boundary, so `.data` stays put** — with the crossing condition written down as a number
(`fill_delta + string_bytes_placed > 0x34`). Stop predicted at **`OSKextParseVersionString` on
`_ZN6OSKext10initializeEv+0x224`**.

**Result:** all three counts exact, `.text` ended at **0x8015FFE0 to the byte** — the single-address
prediction — the stop landed **name and key**, and `abort_entries=0`. Two rows were wrong and both are
informative: the `.text` fill went **−0x2**, not the −0x30 the fill *model* said, so the prediction was right
because it was stated as an **end address under a 32-byte `ALIGN`** rather than as a size; and
`__bss_end`/`headroom` did not move at all, because the `.bss` fill grew by exactly what the placed content
shrank by — 343's `.data` rule, now measured on `.bss` and in the opposite direction.

## The object

| `libkern_c++_OSSet.o` | |
|---|---|
| `.text` | **2884** (0xB44), 50 definitions |
| `.text._ZN5OSSet9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **224** (0xE0) |
| `.rodata.str1.1` | **10** (0xA — one string, and see below) |
| `.bss` | **24** (0x18 — `OSSet::gMetaClass`) |
| `.init_array` | 4 (`_GLOBAL__sub_I_OSSet.cpp`) |
| definitions / references | 50 / 36 |

**4 resolved / 0 added**, and the arc is the story:

| resolved | object | stand-in was |
|---|---|---|
| `_ZN5OSSet12withCapacityEj` | `T` | `func T` — this step's stop, and the one 342 predicted |
| `_ZN5OSSet9withArrayEPK7OSArrayj` | `T` | `func T` — **the name 343's link created** |
| `_ZN5OSSet11withObjectsEPPK8OSObjectjj` | `T` | `func T` |
| `_ZN5OSSet9metaClassE` | `R` 4 | `data R 0x4` |

343's link *created* `OSSet::withArray` as a stub, because the object it linked referenced a name nothing in
the image defined; 344 never called it, because the plist has no `[`; 345 gives it a body. **That is the first
stub in this walk's history that the walk invented, deferred, and then defined** — 343 → 344 → 345 — and it is
the concrete form of 342's lesson ("a check that asks only *is this a stub today* cannot see a name the link
is about to create"): the name that trap hid is now a definition two steps later.

Counts **829 → 825**, **728 → 725 function, 101 → 100 storage**, and 725 + 100 = 825.

## `added = 0` is what makes the 342/343 check completable rather than arguable

342's pre-run check answered "is any of this object's `bl` targets a stub *today*" and returned zero while the
run stopped on a name the same link created. 343's fix was to classify every call site against the stub list
**plus** the `added` column. Here the added column is empty — so the two questions collapse, and the check can
be made over the whole object rather than over its `.text`:

```
$ objdump -dr --section=.text libkern_c++_OSSet.o   vs the 829 stub names
bl sites naming a CURRENT stub: 0        (of the object's 50 defined functions)
$ objdump -r libkern_c++_OSSet.o | awk '{print $NF}' | grep -Fx -f <the 829 stub names>
relocations naming a CURRENT stub: (none, in any section)
```

The second of those is the one no earlier step had reason to make. This object ends `withCapacity` with a
`blx r2` and calls through vtables in `initWithCapacity`, so a `bl`-only check cannot see all of its control
flow — vtable slots are `R_ARM_ABS32` relocations, not instructions. Checked over every section, the `blx r2`
at `withCapacity+0x4C` reaches `OSSet::initWithCapacity` (in this object, offset 0xE8), whose two calls are
`OSCollection::init` and `OSArray::withCapacity`, both real in the 344 image; `withCapacity`'s three direct
calls are `OSObject::operator new`, the `OSCollection` base constructor and
`OSMetaClass::instanceConstructed`, all real. So every edge of the object's call graph is real **by
construction**, which is the strongest thing that can be said about a step before it runs — and the run's
`abort_entries=0` is the hardware's agreement.

## `.text`: six terms, and the fill model was wrong by 0x2E

| term | predicted | measured |
|---|---|---|
| this object's `.text` | +0xB44 | **+0xB44** (input at 0x8013AB40 — 344's `last_kernel_constructor` address) |
| its COMDAT | +0x004 | **+0x004** |
| its `.rodata`, placed whole | +0x0E0 | **+0x0E0** (input at 0x8015B2FC) |
| its string bytes, as placed | +0x000 .. +0x00A | **+0x006** |
| the three retired stub bodies | −0x048 | **−0x048** |
| the three name slots (25, 31, 37 chars → 0x1C + 0x20 + 0x28) | −0x064 | **−0x064** |

Sum **+0xB82**; the fill went 0xD38 → **0xD36** (−0x2), so the section's size moved **+0xB80**
(0x15F460 → **0x15FFE0**) and it ends **0x20 below the 0x80160000 boundary** — inside the window.

**The string row is the one new measurement.** The object's `.rodata.str1.1` is ten bytes and holds *one*
`.L.str` pair: `"OSSet\0set\0"`. Six bytes — `"OSSet\0"` — were placed at 0x8015B3DC and four were **deduped
into them**: `"set"` is a suffix of `"OSSet"`, so the linker pointed the second string four bytes into the
first. This is the first **partial** dedupe of a `.rodata.str1.1` input this walk has measured: 343 and 344
each placed theirs whole (0x5C/0x82 and 0x34), 342 deduped 43 of 246, and here one string of a *single* input
deduped into its own sibling. The pool is suffix-merged, which is a property of the *section* and not of the
step — and it is why the string row stays a band.

`realstubs.o` moves on all three sections, and by more than in any step since 342:

| | 344 | 345 measured | model |
|---|---|---|---|
| `.text` | 0x4440 | **0x43F8** | −0x48 exactly (three bodies) |
| `.rodata.str1.4` | 0x414B | **0x40E7** | 0x40E8 — the ≤1-byte tail artifact, for the **fifth** time (**withdrawn by 346**: it measured the model exactly, so the rule is "model or model − 1") |
| `.bss` | 0x1A04 | **0x19C4** | −0x40 exactly (one stand-in slot) |

The `.rodata.str1.4` row is the first time that artifact was **predicted** rather than noticed: 338, 342, 343
and 344 each recorded "measured = model − 1" on this section after the fact, so 345 wrote it down before the
run as a constant. It was — **and 346 showed the constant does not exist.** 346's `.rodata.str1.4` went
0x40E7 → **0x40B3**, which is `0x40E7 − 0x1C − 0x18` to the byte: the model, with no artifact at all. Five
samples of a ≤1-byte rounding that sometimes does not happen is a distribution, not a constant, and four of
those five shared a construction (one retired name of the same shape) that did not make them independent.
The claim was written one step before the control existed to falsify it, which is the same error this step's
fill *model* made one row above.

**The fill row is where the model failed, and the failure is the useful part.** Applying the 344 map's own
`*fill*` rows to a uniform shift — the model 343's `.data` row and 344's `.bss` row were both argued from —
gives **−0x30** for a placed term of 0xB82. Measured: **−0x2**. The model was wrong by 0x2E, and it was wrong
for a structural reason a map cannot show: the fills are not a property of addresses, they are the result of a
*sequential* allocation in which removing three stub bodies and three name slots from inside the generated stub
object changes the alignment of everything after them, and the map has no row for a fill that is zero today
and nonzero tomorrow.

**What saved the prediction is that it was stated as an end address rather than a size.** `.text` closes with
`. = ALIGN(32)` (`entry.ld:69`), so its end is always a multiple of 32; the last input ends at 0x8015F450
(≡ 0x10 mod 32) and the 345 end is `align32(0x8015F450 + Q)` for the total shift Q — which is the *same
number*, 0x8015FFE0, for every Q in **(0xB70, 0xB90]**. The placed term alone gives Q = 0xB7C..0xB86, inside
that window with about 0xE to spare, and the measured Q was 0xB80. Had the fill model's −0x30 been trusted
instead, Q would have been 0xB52 and the prediction would have been 0x8015FFC0 — **0x20 low**.

So the rule from 343 ("the fill delta is a measurement, not a rounding") has a second half, and this step
supplies it: **a fill *model* is a hypothesis, and a 32-byte quantiser at the end of a section can absorb a
model error of up to 15 bytes in an address prediction that it would convert into a miss in a size
prediction.** Two of this step's 66 fill rows moved, and the count went *up* by one: a new 2-byte fill appears
at 0x8015B3E2, immediately after the object's six placed string bytes, because the six bytes left the string
pool 2 bytes out of four-byte alignment; elsewhere one 8-byte fill became a 4-byte fill. −4 and +2 is the
whole −2.

## The boundary, quantified before the run

`.text`'s end is 32-aligned and `.data` starts at the next **16 KB** boundary after it (there is no `ALIGN` in
`entry.ld` for this — `data.o`'s `.data` is a 0xC000-byte, 0x4000-aligned blob, so the `.data` output
section's own start alignment is 16 KB and `__entry_data_start = .` lands on 0x80160000). The margin was
**0xBA0** and the placed term is 0xB7C..0xB86, so this is the first step of the walk where the placed content
alone came within 0x24 of the line — 343 ate 0x12B1 of a 0x1710 margin and 344 ate 0x5B8 of 0xBA0.

The crossing condition was written down as an inequality and it did not fire:

```
.text crosses 0x80160000  <=>  Q > 0xBB0  <=>  fill_delta + string_bytes_placed > 0x34
```

The fill's history on this section was +0xF (342 → 343) and +0x8 (343 → 344), so the crossing needed a fill
delta more than three times anything measured since 342. Measured: **−0x2**, and the section closed 0x20 below
the line. The next step's margin will be **0x20**, which means the crossing question is not if but when.

| | 344 | 345 measured | 345 predicted |
|---|---|---|---|
| `.text` | 0x8015F460 (0x15F460) | **0x8015FFE0** (0x15FFE0) | **0x8015FFE0** ✓ (single address) |
| `.data` | 0x80160000 (0x19498) | **0x80160000** (0x19498, unmoved) | **0x80160000** ✓ (no `.data` in the object) |
| `.sysctl_set` | 0x80179498 (0x10C) | **0x80179498** (0x10C) | **0x80179498** ✓ |
| `.init_array` | 0x801795A4 (0x4C, 19) | **0x801795A4** (**0x50**, twenty entries) | **0x801795A4** (0x50, 20) ✓ |
| `.bss` | 0x80179600 (size 0x37BD8, placed 0x37ABC, fill 0x11C) | **0x80179600** (size **0x37BD8**, placed **0x37A94**, fill **0x144**) | placed 0x37A94 ✓, fill assumed 0x11C..0x134 ✗ |
| `__bss_end` | 0x801B11D8 | **0x801B11D8** (unmoved) | 0x801B11B0 .. 0x801B11C8 ✗ |
| image | 1545712 | **1545716** | **1545716** ✓ |
| headroom | 1371688 | **1371688** (unmoved) | 1371704 .. 1371728 ✗ |

`.bss`'s placed term is exact at **−0x28** (−0x40 for the `OSSet::metaClass` stand-in's slot, +0x18 for this
object's own `.bss`), and `.init_array`'s twentieth entry costs `.bss` nothing — its end is 0x801795F4 and
`align64` of it is still 0x80179600, so `.bss`'s start is unmoved for the **sixth** step running.

## The row that did not move at all

`__bss_end` did not move, and this time it is not a coincidence of two 0x18s cancelling (344's reason). The
`.bss` **fill grew by +0x28, exactly the magnitude of the placed delta**, so the section's size is unchanged
at 0x37BD8. One fill row did it, and the map shows it directly:

```
 .bss  0x801AF794 0x18  libkern_c++_OSNumber.o      (OSNumber::gMetaClass)
 .bss  0x801AF7AC 0x18  libkern_c++_OSSet.o         (OSSet::gMetaClass)
 .bss  0x801AF7C4 0x0   entry_last_kernel_constructor.o / entry_arm_rtabi.o / entry_macho.o
 *fill* 0x801AF7C4 0x3C                              <- 344's row here was 0x14
```

0x801AF7C4 + 0x3C = 0x801AF800 is a 64-byte boundary, and **every named `.bss` symbol from there to the end
kept its 344 address** — checked for thirty of them, from `allproc` at 0x801B11C0 back through `pidhash`,
`hz`, `gIOKitDebug`, `KERNEL_AUDIT_TOKEN`. So the whole of this step's `.bss` movement is one alignment fill
growing from 0x14 to 0x3C, and the image's `last_kernel_symbol` (which is `__bss_end`, `entry.ld:279`) and the
headroom are unmoved.

This is 343's `.data` row seen from the other side. There, four new bytes were absorbed by four bytes of
padding: placed +4, fill −4, size unmoved. Here, 0x28 bytes were *removed* and the fill grew 0x28: placed
−0x28, fill **+0x28**, size unmoved. The two brackets now close the rule: **a fill can absorb a placed delta
of either sign, and the only way to know which is to read it.** 344 got this row wrong by carrying 343's fill
forward; 345 got it wrong by assuming a fill in the same range at all.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000069   xnu_entry_kv_in_dram=0x0000008d   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x801402c0          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x80109520   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=OSKextParseVersionString
```

`tools/host_resolve_entry_addr.sh 0x80109520` → **`_ZN6OSKext10initializeEv+0x224`**, `caller-4` =
`0x8010951c: bl 8013d4c8 <OSKextParseVersionString>` — the name and the key both as written, and the key is a
return address inside `OSKext::initialize`, four instructions past the second `OSSet::withCapacity` call at
+0xB8 that the previous run stopped 0x20 bytes short of. `xnu_entry_why=0x801402c0` disassembles to
`61 20 73 79` = `"a sy"`, the tail of the message pool, which is where a stub stop's `why` always points.

**`abort_entries=0` is what makes this stop a measurement of the object.** Between 344's stop and this one the
run had to:

* return from the **first** `withCapacity` — three direct calls all real, then a vtable `blx` into
  `initWithCapacity`, which calls `OSCollection::init` and `OSArray::withCapacity`, and return an object;
* run the 0x14 bytes to the **second** `withCapacity` call at +0xB8 — this is the one the previous step's
  falsifier named ("a stop at +0xBC would mean the first call's return value gated on something") and it did
  not fire, so `OSKext::initialize` really does make two `OSSet::withCapacity` calls unconditionally;
* run +0xCC `OSArray::withCapacity`, +0xEC `PE_parse_boot_argn`, +0x128 `OSKextLog`, +0x13C
  `PE_parse_boot_argn`, +0x168 `OSKextLog`, +0x180 `PE_parse_boot_argn` — all read as real before the run.

Then `+0x220` is the stub. So the step bought the rest of `OSKext::initialize`'s container setup, and the
falsifiers it named — a stop *inside* `OSSet`, or at `+0xBC` — did not fire.

The instrument's newer columns agree with the older ones: `xnu_entry_stub_caller_digits=0x0000003c`,
`_w0=0x30313038` (`"8010"`) and `_w1=0x30323539` (`"9520"`) are the key rendered as its own ASCII digits, and
`_v`/`_a`/`_e` are all 0x80109520.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301636 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 346 — and the margin is 0x20

The next stub on the line is `+0x224`. `OSKextParseVersionString` is a stub body in `realstubs.o` (`T` at
offset 0x1DE8) and in the image at 0x8013d4c8, and the object that defines it is the next step's question, not
this one's.

What 346 has that no earlier step had is a **known, small margin**: `.text` now ends at 0x8015FFE0, so the
next step crosses the 0x80160000 line if its net `.text` growth exceeds 0x20 — and `.data`, `.sysctl_set`,
`.init_array` and `.bss` all step a whole 0x4000 when it does. The one structural fact that helps is that a
function stub costs `.text` and *not* `.bss` (345's `.bss` row moved for the storage stand-in alone), so a step
that retires only functions and brings a small object can still fit; a step that brings a `.rodata` of any
size will not. The crossing is a layout event, not a safety one — 341 crossed by construction and ran clean —
but it is worth naming as the shape of the next few steps rather than discovering it from a `.data` address.
