# Experiment 346 — `OSKextVersion.c`: the 16 KB boundary crossed as predicted, and a "constant" withdrawn

**Step:** link one object, `libkern/OSKextVersion.c` (`libkern_OSKextVersion.o`, 9476 bytes) — the object that
defines 345's stop `OSKextParseVersionString`. Nothing else changes.

**Prediction:** *2 resolved / 0 added — 825 → **823** undefined, 725 → **723** function, 100 → **100** storage;
`.text` +0xFF0 to +0x1010;* **`.data` steps a whole 0x4000 to 0x80164000`** (345's ledger wrote the crossing
condition the step before), and the stop moves out of `OSKext::initialize` to
**`_ZN12IOUserClient10initializeEv` on `iokit_post_constructor_init+0x20`**.

**Result:** all three counts exact, `.data`, `.sysctl_set`, `.bss` and `__bss_end` all as predicted —
`__bss_end` and the headroom **exact to the byte for the first time in the walk** — and the stop landed **name
and key** at `iokit_post_constructor_init+0x20`, one call past 345's. Three rows were wrong and one of them
**withdrew a claim 345 had just written**: the `≤1-byte` tail artifact on `realstubs.o`'s `.rodata.str1.4` is
not a constant.

## The object

| `libkern_OSKextVersion.o` | |
|---|---|
| `.text` | **4120** (0x1018) — three functions, three `.L.str`, one switch table |
| `.rodata` | **36** (0x24 — `.Lswitch.table.OSKextVersionGetString`) |
| `.rodata.str1.1` | **32** (0x20) |
| `.bss`, `.data`, COMDAT, `.init_array` | **none of them** |
| definitions / references | 7 / 7, **all seven references already satisfied** |

**2 resolved / 0 added** — `OSKextParseVersionString` (345's stop) and `OSKextVersionGetString` — for
**825/725/100 → 823/723/100**, all three exact and 723 + 100 = 823. Both are functions; the object defines no
storage at all, which is why the `.bss` placed term is **+0x000** for the first time in the walk.

The object is worth reading as a **reverse** check rather than a forward one: this step retires a *callee*,
so what matters is what can stop a run *inside* it. Its call sites are seven functions and every one was
checked stub-free **one level deep** against the 345 image before the device was touched:

```
OSKextParseVersionString: snprintf, __aeabi_ldivmod x3, strlen, strlcpy, strlcat, bzero,
                          __OSKextVersionStageForString (in this object, 0 call sites)
snprintf        -> __doprnt        real, and __doprnt's own line has no stub call
strlcat         -> strlen          real, 0 direct calls
strlcpy         -> strlen, memcpy  real, memcpy 0 direct calls
__aeabi_ldivmod -> __udivmoddi4    real, whose own line has no stub call
```

`__OSKextVersionStageForString` is the object's third function (0xC20) and makes **no calls at all**, so the
whole of this step's new code is stub-free by construction — which is what makes the *next* stop predictable
rather than the current one.

## `.text`: six terms, and the first placed term to leave its band

| term | predicted | measured |
|---|---|---|
| this object's `.text` | +0x1018 | **+0x1018** (input at 0x8013B688 — 345's `last_kernel_constructor`) |
| its `.rodata`, placed whole | +0x024 | **+0x024** (at 0x8015C3E0) |
| its string bytes, as placed | +0x000 .. +0x020 | **+0x014**, **relaxed** from 0x20 |
| the two retired stub bodies | −0x030 | **−0x030** |
| the two name slots (24 and 23 characters → `align4(25)` + `align4(24)`) | −0x034 | **−0x034** |

Sum **+0xFEC** — and this is the first placed term of the walk to land **outside** its predicted band, by
**4 bytes on the low side** (0xFF0..0x1010). The band's low end was "every string byte deduped away" = 0x000,
and the outcome was neither end of it: `.rodata.str1.1` was **relaxed** from 0x20 to 0x14. The map names it in
that form —

```
 .rodata.str1.1
                0x000000008015c3ca       0x14 /…/libkern_OSKextVersion.o
                                         0x20 (size before relaxing)
 *fill*         0x000000008015c3de        0x2
```

— and it is the same suffix-merging the linker did to 345's ten bytes (`"set"` inside `"OSSet"`), but acting
**inside this object's own input** rather than against a neighbour's. **A string term has three outcomes, not
two: placed whole, deduped to zero against a *previous* input, or partly relaxed against its *own*
duplicates** — and only the first two were in the model. The 0x2 fill immediately after it is the leftover.

The fill went 0xD36 → **0xD2A** (−0xC) across **65** rows where 345 had 66. One row left: 345's new 2-byte
entry at 0x8015B3E2 — the one its own string bytes created — is gone again. `.text` is **+0xFE0**
(0x15FFE0 → **0x160FC0**), the top of the predicted band 0x80160F80..0x80161020.

## The crossing, and the first `.bss` row that needed no fill

| | 345 | 346 measured | 346 predicted |
|---|---|---|---|
| `.text` | 0x8015FFE0 (0x15FFE0) | **0x80160FC0** (0x160FC0) — **crossed** | 0x80160F80..0x80161020 ✓ |
| `.data` | 0x80160000 (0x19498) | **0x80164000** (0x19498, size unmoved) | **0x80164000** ✓ |
| `.sysctl_set` | 0x80179498 (0x10C) | **0x8017D498** (0x10C) | **0x8017D498** ✓ |
| `.init_array` | 0x801795A4 (0x50, 20) | **0x8017D5A4** (**0x50**, twenty, unmoved) | 0x8017D5A4 (0x54, 21) ✗ |
| `.bss` | 0x80179600 (size 0x37BD8, placed 0x37A94) | **0x8017D600** (size **0x37BD8**, placed **+0x000**) | **0x8017D600** ✓ |
| `__bss_end` | 0x801B11D8 | **0x801B51D8** | **0x801B51D8** ✓ |
| image | 1545716 | **1562100** | 1562104 ✗ |
| headroom | 1371688 | **1355304** | **~1355304** ✓ |

The crossing was not a possibility to hedge — 345 left 0x20 of margin and this object's placed term is 0xFEC,
127 times that — and it moved four sections a whole 0x4000 at once. `.data` is at the next 16 KB multiple at
or above the text end, and every text end in the predicted band rounds to the same 0x80164000, which is why
that row was exact while the text row was a band.

**`.bss`'s placed term is +0x000** — no storage name resolves, none is created, and the object brings no
`.bss` — and because `.init_array` does not grow either, `__bss_end` is **exactly** 0x801B11D8 + 0x4000 with
**no fill term consulted**. That is 344's and 345's fill lesson as a contrapositive, and the first time a
`.bss` row in this ledger could be stated without reading a fill: **a `.bss` whose content does not change has
a fill that does not change**. The headroom is exact with it.

**The one wrong row is the same mistake twice, and the prediction's own table refutes it.** The `.init_array`
row predicted a twenty-first entry (`_GLOBAL__sub_I_OSKextVersion.cpp`) and 4 bytes, when the object table two
paragraphs above says `libkern_OSKextVersion.o` has **no `.init_array`** — it is C, not C++. The `.init_array`
row and the image size are then both 4 high, and 1562104 is exactly 1562100 + 4. The prediction contradicted
its own table, which is the cheapest class of defect to prevent and the reason the table is worth writing
before the rows are.

## One claim withdrawn: the `.rodata.str1.4` tail artifact is not a constant

`realstubs.o` moves on two of its three sections and not the third: its `.text` 0x43F8 → **0x43C8**
(−0x30 exactly, the two bodies), its `.bss` **0x19C4 unchanged** (no storage resolved, none created), and its
`.rodata.str1.4` 0x40E7 → **0x40B3** — which is `0x40E7 − 0x1C − 0x18` **to the byte**. That is the model, with
no artifact.

345's ledger had written, one step earlier, that the `≤1-byte` tail artifact this section had shown in 338,
342, 343, 344 and 345 was "a constant of this section: measured = model − 1". **346 is the control that
falsifies it.** Five samples of a rounding that sometimes does not happen are a distribution, not a constant,
and four of those five shared a construction (a retired name of the same shape) that did not make them
independent. The claim is corrected in 345's block and its document rather than deleted, because the
interesting part is not that the number was wrong but that a rule was promoted from five observations one step
before anything could test it — which is exactly the error 345's fill *model* made one row above it.

## The run: out of `OSKext::initialize` for good

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000070   xnu_entry_kv_in_dram=0x00000094   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x801412a8          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8011b10c   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN12IOUserClient10initializeEv
```

`tools/host_resolve_entry_addr.sh 0x8011b10c` → **`iokit_post_constructor_init+0x20`**, `caller-4` =
`0x8011b108: bl 80140358 <_ZN12IOUserClient10initializeEv>` — **341's reading of the constructor's line,
five steps later.** `digits=0x43` = 67 with `w0=0x31313038` (`"8011"`) and `w1=0x63303162` (`"b10c"`) renders
the key as its own ASCII digits.

**`abort_entries=0` is what makes the two retirements a measurement.** Between 345's stop and this one the run
had to:

* run **`OSKextParseVersionString` in full** — including `snprintf` → `__doprnt`, `strlcpy`, `strlcat`,
  `bzero`, `strlen` and three `__aeabi_ldivmod`;
* run the **0x264 bytes of `OSKext::initialize` past +0x224** — `strlcpy`, `OSDictionary::withCapacity`, two
  `OSString::withCStringNoCopy`, `IORegistryEntry::getRegistryRoot`, two `OSNumber::withNumber` and
  `OSKextLog` — plus **ten `blx` vtable dispatches**, all of which returned;
* return out of `OSKext::initialize` and let `iokit_post_constructor_init` advance **one call**.

So both falsifiers the prediction named — a stop inside `OSKextParseVersionString` (a stub deeper than one
level) and a stop naming one of the ten `blx` targets — did not fire, and the frontier has left
`OSKext::initialize` for good: **331's target function has been entered and exited.**

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301643 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 347 — `iokit/Kernel/IOUserClient.cpp`, and a stop that does not move to a created name

`iokit_Kernel_IOUserClient.o` (**88856 bytes**) is the only object defining 346's stop and the largest step
since 342: **7 resolved / 21 added** — six `IOUserClient` methods, `iokit_task_terminate` and the `R 0x4`
stand-in `_ZN12IOUserClient9metaClassE` out; **nineteen functions and two storage stand-ins in** — for
823 + 21 − 7 = **837** undefined, **723 → 736 function, 100 → 101 storage**. It brings `.text` **35036**,
`.rodata` 1720, `.rodata.str1.1` 1515, `.bss` 160 and an `.init_array` of its own, and it **crosses the 16 KB
boundary again** — 0x88DC against 0x3020 of margin, so `.data` steps to at least 0x80168000.

What makes it worth writing down before it runs: classifying every `bl` target in the object against the **stub
list plus its own `added` column** — 342's and 343's rule, and here it has to be applied to both populations
because 21 names are created — gives **71** sites, and **zero of them are in `IOUserClient::initialize`**. So
`IOUserClient::initialize` should run to completion and the constructor advances **one** call to the stub at
+0x24, `_ZN18IOMemoryDescriptor10initializeEv`, key **0x8011b110**. That would make 347 the first step of the
walk whose stop does **not** move to a name the step creates even though it creates twenty-one of them.
