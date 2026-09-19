# Experiment 343 — `OSUnserialize.cpp` **and** `KernelConfigTables.cpp`: the first stop *inside* the object the step links, and a second object that is only a value

**Step:** link **two** objects — `libkern/c++/OSUnserialize.cpp` (`libkern_c++_OSUnserialize.o`), the object
that defines 342's stop `_Z13OSUnserializePKcPP8OSString`, and `iokit/KernelConfigTables.cpp`
(`iokit_KernelConfigTables.o`, 912 bytes), which defines `gIOKernelConfigTables` — the string that 342's run
found as a **NULL**, because the only thing in the image holding it was a zeroed `.bss` stand-in. Nothing else
changes.

**Prediction:** *1 resolved (`OSUnserialize`) + 1 resolved stand-in (`gIOKernelConfigTables`) + 1 added
(`_ZN5OSSet9withArrayEPK7OSArrayj`) — 832 → **831** undefined, 729 → **729** function, 103 → **102** storage;
`.text` +0x11D3 to +0x12B1 in seven terms; `.data` +4; `.bss` placed **+0x68**;* and **the stop at
`_ZN8OSNumber10withNumberEyj` on `_Z18OSUnserializeparsev+0xAAC`.**

**Result:** all three counts exact; the stop landed **name and key**; `.text` closed to the byte at the **top**
of the predicted band (no dedupe of either `.rodata.str1.1` input); and the one row that was wrong — `.data` —
was wrong for a reason worth more than the row: the 4 new bytes were **absorbed whole by alignment padding**, so
`.data`'s size did not move at all. This is the first step of the walk whose stop is a call made from *inside*
the object being linked, and the first whose second object is not a callee's definition but a *stand-in's data*.

## The two objects

| object | `.text` | `.rodata` | `.rodata.str1.1` | `.data` | `.bss` | defs / refs |
|---|---|---|---|---|---|---|
| `libkern_c++_OSUnserialize.o` | **3968** (0xF80) | **595** (0x253) | **92** (0x5C) | — | **168** (0xA8) | 27 / 18 |
| `iokit_KernelConfigTables.o` | — | — | **130** (0x82) | **4** | — | 1 / 0 |

`libkern_c++_OSUnserialize.o` is a bison parser: `_Z18OSUnserializeparsev` is 0xEA4 bytes (the object's whole
`.text` is 0xF80, and the remaining 0xDC is `_Z13OSUnserializePKcPP8OSString`), nine `yacc` tables and five
`.L.str` blocks sit in its `.rodata`, and its `.bss` is the eleven words the parser keeps state in —
`parseBuffer`, `parseBufferIndex`, `tags`, `parsedObject`, `lineNumber`, the `lock`, the 128-byte
`yyerror_message`, `oo`, and the lexer's `char`, `lval`, `nerrs`. It has no COMDAT, no `.init_array` and no
`.data`. `iokit_KernelConfigTables.o` is one symbol and one string — the 0x81 bytes at
`iokit/KernelConfigTables.cpp:35`:

```
(   {     'IOClass'         = IOPanicPlatform;     'IOProviderClass' = IOPlatformExpertDevice;
          'IOProbeScore'    = 0:32;   })
```

**1 resolved / 1 added, plus one stand-in resolved:** `_Z13OSUnserializePKcPP8OSString` (`T` against a
`func T` stand-in), `gIOKernelConfigTables` (`D 4` against a `data D 0x4` stand-in, so its 0x40 `.bss` slot
comes back), and `_ZN5OSSet9withArrayEPK7OSArrayj` arrives as a new function stub — **created by this very
link**, because the object references it and nothing in the image defines it. That last name is exactly the
trap 342's run exposed: a pre-run check that asks only "is this target a stub *today*" answers *zero* for it and
confirms a prediction that is already wrong. So the check was done the other way round here — **all 39 `bl`
targets of the object's two functions classified against the stub list plus this step's `added` column**, which
returns exactly two stubs: `+0xAA8` and `+0xC7C`.

## `.text`: seven terms, closing at the top of the band

| term | predicted | measured |
|---|---|---|
| `OSUnserialize`'s `.text` | +0xF80 | **+0xF80** (input at 0x801396B8, not deduped) |
| its `.rodata`, placed whole | +0x253 | **+0x253** (input at 0x80159EF4) |
| both objects' string bytes, as placed | +0x000 .. +0x0DE | **+0x0DE** (0x5C at 0x8015A147, 0x82 at 0x8015A1A3 — contiguous) |
| the one retired stub body | −0x018 | **−0x018** |
| the one created stub body | +0x018 | **+0x018** |
| the name slots: −0x020 + 0x020 | +0x000 | **+0x000** (both names are 31 characters) |

Sum **+0x12B1**, the measured placed move (0x15CEBF → **0x15E170**), with the fill 0xD21 → **0xD30** (+0xF), so
`.text` is **+0x12C0** (0x15DBE0 → **0x15EEA0**). The prediction's band was 0x11D3..0x12B1 and it landed exactly
on the top, because the low end allowed for deduplication of `.rodata.str1.1` and **there was none** — both
inputs were placed whole, which the map shows to the byte.

`realstubs.o` measures the three stub terms a second time, and this is the first step where they *cancel*: its
`.text` is **0x4458, unchanged** (one body out, one body in), its `.rodata.str1.4` is **0x4167, unchanged**
(two 31-character names, `align4(32)` each — still the ≤1-byte tail artifact below the model's 0x4168 that 338
and 342 both recorded), and its `.bss` went 0x1A84 → **0x1A44**, exactly **−0x40**: one 64-byte stand-in slot
given back and none created.

## The row that was wrong, and the rule it makes clearer

The prediction said `.data`: 0x19498 + 4. Measured: **0x19498, unchanged**. `.sysctl_set` (0x80179498),
`.init_array` (0x801795A4), the image size and `__bss_end`'s upper bound consequently did not move either, and
the image and `__bss_end` rows came in 4 and 0x18 low.

The symbol table says why, and it is not a rounding rule:

```
80178298  _ZL17gIORegistryLastID     0x8
801782a0  gIOKernelConfigTables      0x4    <- the new input lands here
801782a4  const_boot_args            0x140
801783e4  BootArgs                   0x4
801783e8  cpu_data_alloc.site        0x18   <- 8-byte aligned, and it did NOT move
```

`cpu_data_alloc.site` is the next input needing 8-byte alignment, and everything from it to the end of `.data`
is byte-for-byte where 342 left it (`_ZZ36IOKernelAllocateWithPhysicalRestrictE4site` at 0x80179210 and
`_ZZN12OSOrderedSet14ensureCapacityEjE4site` at 0x80179480, both unchanged). So the four new bytes were
absorbed by the **four bytes of alignment padding that stood between `BootArgs` and `cpu_data_alloc.site`**:
placed +4, fill −4, size unmoved. 340's `.data` +0x30 was *not* absorbed, so this is the same rule seen from the
other side — **a section's size is `size_old + placed_delta + fill_delta`, and the fill delta is a measurement,
not a rounding**: it can be zero, it can be +0x30, and here it is −0x4.

| | 342 | 343 measured | 343 predicted |
|---|---|---|---|
| `.text` | 0x8015DBE0 (0x15DBE0) | **0x8015EEA0** (0x15EEA0) | 0x8015EDB3..0x8015EE91 (placed) — right shape, low: no dedupe |
| `.data` | 0x80160000 (0x19498) | **0x80160000** (0x19498, placed +4, **fill −4**) | 0x80160000 (**0x1949C**) ✗ |
| `.sysctl_set` | 0x80179498 (0x10C) | **0x80179498** (0x10C, unmoved) | 0x8017949C ✗ |
| `.init_array` | 0x801795A4 (0x48, 18) | **0x801795A4** (0x48, **eighteen** entries) | 0x801795A8 ✗ |
| `.bss` | 0x80179600 (size 0x37B98, placed 0x37A7C, fill 0x11C) | **0x80179600** (size **0x37C18**, placed **0x37AE4**, fill **0x134**) | placed 0x37AE4 ✓ |
| `__bss_end` | 0x801B1198 | **0x801B1218** | ~0x801B1200 (the `.bss` fill grew +0x18) |
| image | 1545708 | **1545708** (unmoved) | 1545712 ✗ |
| headroom | 1371688 | **1371624** | ~1371648 |

`.bss`'s placed term is exact at **+0x68** — −0x40 (the retired 64-byte slot) + 0xA8 (this object's own `.bss`)
— and a *function* stub takes no `.bss` slot, so the created name costs nothing there: the one place in this
step where its two directions do not cancel. `.bss`'s start stays at 0x80179600 because `align64(0x801795F0)` is
still 0x80179600. The image row is unmoved for a reason worth keeping: `bin_size` is `.init_array`'s end minus
`ENTRY_BASE`, and `.init_array` did not move, so the step's 4 new `.data` bytes are inside a section the copied
image already covered.

The `headroom` row's constant was corrected in the prediction before the run: `ENTRY_DATA_LIMIT` is
`align_up(ENTRY_ARGS_OFFSET + ARGS_BYTES + 0x100000, 0x100000)` and the **megabyte** granularity is why it is
0x300000 in both 342 and 343, rather than the 0x2FFFC0 the row was first written with.

## The run: the stop is a call inside the object the step linked

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000006c   xnu_entry_kv_in_dram=0x00000090   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x8013f2d0          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8013a164   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN8OSNumber10withNumberEyj
```

`tools/host_resolve_entry_addr.sh 0x8013a164` → **`_Z18OSUnserializeparsev+0xaac`**, `caller-4` =
`0x8013a160: bl 8013e5f0 <_ZN8OSNumber10withNumberEyj>` — the name and the key both as written, and the key is
a return address **inside** `_Z18OSUnserializeparsev`, i.e. inside the object this step linked.
`xnu_entry_why=0x8013f2d0` disassembles to `.word 0x79732061` = `"a sy"`: the tail of the message pool, which is
where a stub stop's `why` always points.

**`abort_entries=0` is what makes this stop a measurement of the second object too**, and four things had to
hold at once — none of them a call:

1. `gIOKernelConfigTables`'s `.data` word received its link-time relocation; in the image it reads
   **0x8015a1a3**, which the map confirms is exactly where `iokit_KernelConfigTables.o`'s `.rodata.str1.1`
   input was placed. That is 342's NULL, replaced by a pointer;
2. the string was placed, whole and undeduped (0x5C at 0x8015A147, 0x82 at 0x8015A1A3, contiguous);
3. `OSUnserialize` ran — `lck_mtx_alloc_init`, `lck_mtx_lock`, `OSDictionary::withCapacity` and its own lock
   all returned;
4. `OSString::withCString` returned **five** times — `'IOClass'`, `IOPanicPlatform`, `'IOProviderClass'`,
   `IOPlatformExpertDevice`, `'IOProbeScore'`.

Only then did `0:32` reduce as **`offset: NUMBER ':' NUMBER`** → `buildOSOffset` → `OSNumber::withNumber`, the
stub at `+0xAA8`. That is the part of the prediction that rests on the plist's *content* rather than on the
parser's skeleton, and the run is what confirms the plist was actually read. Had the string failed to place, the
site would still have read zero and the fault would have been `abort_entries` non-zero *inside* a real
`OSUnserialize` — the falsifier the prediction named for the second object, and it did not fire.

The other candidate was never reachable: the string contains no `[`, so `object: set`'s action is never taken
and `OSSet::withArray` — the name this link *created* — is never called. It is a link-time stub, not a run-time
stop. `OSDictionary::withCapacity` (at `+0x938`) and `OSArray::withCapacity` (at `+0x9B8`) run after the stop,
and both are real.

## One promise the build could not keep, and it is the useful kind

The plan was to re-run `tools/standin_value_loads.py _ZN11IOCatalogue10initializeEv` and see the *same* site
still classified `read-through` — now of a real pointer. That command now reports **`0 storage stand-ins in the
image, 0 sites reference one`**, because the tool's stand-in set is `realstubs.o`'s `B` list and
`gIOKernelConfigTables` is no longer in it. The site the tool was written to guard has been *retired by the
cure*. The verdict cannot be re-read after the stand-in is defined; the evidence for the fix is the word's
content (0x8015a1a3, the string's own address). **A tool whose subject is a stand-in goes silent when the
stand-in is defined, and silence there is success** — worth writing down because the same silence is
indistinguishable from the defect the tool exists to find.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301639 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 344 — `libkern/c++/OSNumber.cpp`

`libkern_c++_OSNumber.o` (6488 bytes) defines this step's stop and one more name: **2 resolved / 0 added** —
`_ZN8OSNumber10withNumberEyj` and `_ZN8OSNumber9metaClassE` (an `R 0x4` whose stand-in is `data R 0x4`) — for
831 → **829**, **729 → 728 function, 102 → 101 storage**. It brings `.text` 0x504, a `.group`/COMDAT pair,
`.bss` 0x18, `.rodata` 0xB0, `.rodata.str1.1` 0x34, and an **`.init_array` of its own**
(`_GLOBAL__sub_I_OSNumber.cpp`), which would be the nineteenth entry — whether `.init_array`'s size moves is the
fill's question, not the count's (332's rule).

What makes 344 a different kind of step is that this stop is the *last* thing between the plist and a complete
parse. With `OSNumber::withNumber` real, every reduction in the string has a real builder: `;` → `pair`, `}` →
`buildOSDictionary`, `)` → `buildOSArray`, `parse` returns, `OSUnserialize` releases `tags` and unlocks, and
`IOCatalogue::initialize` resumes. And `tools/first_stub_call.py _ZN11IOCatalogue10initializeEv --from 0x1C`
already answers **"no stub call anywhere on the straight line from +0x1C"** — its direct calls there are
`OSMetaClassBase::safeMetaCast`, `IOLog`, three `OSSymbol::withCStringNoCopy` and `OSObject::operator new`, all
real. So `IOCatalogue::initialize` should run to completion, `iokit_post_constructor_init` advances to
`_ZN6OSKext10initializeEv` at `+0x14`, and **342's deferred prediction is still waiting there**: 332 read
`OSKext::initialize`'s own stop as `_ZN5OSSet12withCapacityEj` at `+0xA4`, key `+0xA8`, and record 773 of
`xnu_arm_entry_stubnames.txt` says that name is still a function stub. The two ways it could be something else
are both named in advance: a stub among `OSNumber::withNumber`'s own 32 references (the tool's `0 added` is the
*link* answer, not the *run* answer — 343's lesson applied to itself), or a `blx` dispatch in the dict/array
build that lands in a still-stubbed `OSDictionary`/`OSArray` method.
