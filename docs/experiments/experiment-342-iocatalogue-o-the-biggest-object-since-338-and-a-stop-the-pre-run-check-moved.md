# Experiment 342 — `IOCatalogue.cpp` linked: the biggest object since 338, and a stop the pre-run check moved to a stub this step created

**Step:** link one object, `iokit/Kernel/IOCatalogue.cpp` (`iokit_Kernel_IOCatalogue.o`) — the only object in
the pool that defines 341's stop, `_ZN11IOCatalogue10initializeEv`. Nothing else changes.

**Prediction:** *9 resolved / 2 added — 839 → **832** undefined, 734 → **729** function, 105 → **103**
storage; `.text` +0x18DC to +0x19D2 in seven terms, ending 0x16AE to 0x17D4 below the 0x80160000 boundary so
`.data` stays put; `.bss` placed −0x54;* and **the stop at `_ZN5OSSet12withCapacityEj` on
`_ZN6OSKext10initializeEv+0xA8`.**

**Result:** all three counts exact, the seven `.text` terms closing to the byte (+0x19A7 placed), the `0x18`
stub-body rule proved a second time off `realstubs.o` — and **the stop was not the one predicted**. The run
stopped at `_Z13OSUnserializePKcPP8OSString` on `_ZN11IOCatalogue10initializeEv+0x1c`, a name *this step's own
link created*, inside the object being linked and one call in. The prediction had no chance of being right: the
check behind it asked "is any of the object's 109 `bl` targets a stub today" and not "is the target in the
image at all, counting the stubs this step creates".

## The object

| `iokit_Kernel_IOCatalogue.o` | |
|---|---|
| `.text` | **6576** (0x19B0), 60 definitions |
| `.text._ZN11IOCatalogue9MetaClassD0Ev` | 4 (COMDAT) |
| `.rodata` | **132** (0x84) |
| `.rodata.str1.1` | **246** (0xF6) |
| `.bss` | **44** (0x2C) |
| `.init_array` | 4 (`_GLOBAL__sub_I_IOCatalogue.cpp`) |
| definitions / references | 60 / 68 |

**9 resolved / 2 added** — six functions and `gIOCatalogue`, `gIOClassKey`, `gIOProbeScoreKey` out;
`_Z13OSUnserializePKcPP8OSString` (function) and `gIOKernelConfigTables` (storage, `D 4` in its defining
object, so a 0x40 `.bss` slot) in. 839/734/105 → **832/729/103**, and this time the prediction checked its own
three numbers against each other — 729 + 103 = 832 — because 341's had not.

## `.text`: seven terms, closing, and the 0x18 rule proved twice

| term | predicted | measured |
|---|---|---|
| this object's `.text` | +0x19B0 | **+0x19B0** |
| its COMDAT | +0x004 | **+0x004** |
| its `.rodata`, placed whole | +0x084 | **+0x084** |
| its string bytes, as placed | +0x000 .. +0x0F6 | **+0x0CB** (43 of 246 bytes deduped) |
| the six retired stub bodies | −0x090 | **−0x090** |
| the one created stub body | +0x018 | **+0x018** |
| the name slots: −0x104 + 0x020 | −0x0E4 | **−0x0E4** |

Sum **+0x19A7**, the measured placed move (0x15B518 → **0x15CEBF**), inside the predicted band 0x18DC..0x19D2.
`realstubs.o` confirms all three stub terms a second time and is where the 0x18 rule is *measured* rather than
assumed: `.text` 0x44D0 → **0x4458** (exactly −0x90 + 0x18), `.bss` 0x1B04 → **0x1A84** (exactly −0xC0 + 0x40),
and `.rodata.str1.4` 0x424C → **0x4167** where the model says 0x4168 — the same ≤1-byte tail artifact 338
recorded, on the same section, for the same reason.

## The size row, wrong for the third step running

The prediction printed `.text` as `0x8015E82C .. 0x8015E952`. It had added its **placed** band to the fill as
though the fill were a term to add to a **size**: 0x15C240 is already placed-plus-fill, so the band came out
0xBD8 high. Measured `0x8015DBE0`, inside the correct band 0x15DB5C..0x15DC4E.

| | 341 | 342 measured | 342 predicted |
|---|---|---|---|
| `.text` | 0x8015C240 (0x15C240) | **0x8015DBE0** (0x15DBE0) | 0x8015E82C .. 0x8015E952 ✗ |
| `.data` | 0x80160000 (0x19498) | **0x80160000** (0x19498, fill 0x7AAB) | **0x80160000** ✓ |
| `.sysctl_set` | 0x80179498 (0x10C) | **0x80179498** (0x10C) | **0x80179498** ✓ |
| `.init_array` | 0x801795A4 (0x44, 17) | **0x801795A4** (**0x48**, eighteen entries) | **0x801795A4** ✓ |
| `.bss` | 0x80179600 (0x37BD8) | **0x80179600** (size 0x37B98, placed **0x37A7C**, fill 0x11C) | placed 0x37A7C ✓ |
| `__bss_end` | 0x801B11D8 | **0x801B1198** | ~0x801B116C .. 0x801B119C ✓ |
| image | 1545704 | **1545708** | **1545708** ✓ |
| headroom | 1371688 | **1371752** | ~1371772 |

`.text` ends **0x2420 below the 0x80160000 boundary** (the prediction said 0x16AE..0x17D4 — the same conclusion
with the same 0xBD8 offset inside it). `.bss`'s placed term is exact at **−0x54** = −0xC0 (three retired 64-byte
slots) + 0x40 (the one created slot) + 0x2C (this object's own `.bss`), and its start is unmoved because
`align64(0x801795EC)` is still 0x80179600.

**The rule this makes explicit, and it is the third time it has cost a row: a section's size is
`size_old + placed_delta + fill_delta`, and the fill delta is read rather than added a second time.** 341's row
had the same shape.

## The run: the stop moved to a name this step created

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_Z13OSUnserializePKcPP8OSString
 xnu_entry_stub_caller_v=0x80137e08    = _ZN11IOCatalogue10initializeEv+0x1c
 xnu_entry_abort_entries=0x00000000
```

`caller-4` = `0x80137e04: bl 8013d2b0 <_Z13OSUnserializePKcPP8OSString>`, so the run stopped **one call into
the object this step linked** and never reached `OSKext::initialize` at all.

```
80137df4: movw r0, #0xbc0  /  movt r0, #0x801b   ->  r0 = 0x801B0BC0 = gIOKernelConfigTables
80137df8: add  r1, sp, #4                        ->  the `OSString **` out-parameter
80137e00: ldr  r0, [r0]                          ->  r0 = 0
80137e04: bl   <_Z13OSUnserializePKcPP8OSString>  <- the created stub, and the run's stop
```

`IOCatalogue.cpp:100` is `array = OSDynamicCast(OSArray, OSUnserialize(gIOKernelConfigTables, &errorString));`
and the source's `extern const char * gIOKernelConfigTables` **read zero** — with `abort_entries=0`, because a
4-byte pointer stand-in read *as a value* is not a dereference. `tools/standin_value_loads.py` had called this
site correctly before the run: `read-through`, `ldr r0, [r0]`, one site in the whole function.

**The defect is in the check, not in the model.** The pre-run check asked "is any of the object's 109 `bl`
targets a stub *today*" and answered zero — true, and useless, because a name the object references and the
image does not define becomes a stub *in this very link*. It was missing exactly the `added` column that
`tools/entry_object_effect.py` had already printed. This is the same class of instrument as 335's walker that
silently dropped five of its nine roots: **a check that reports only the names it looked for will confirm a
wrong prediction.**

## What this step masks, and what 343 must therefore do

`OSUnserialize` is a stub, so the NULL never reaches a real function. The moment it is real, `OSUnserialize(NULL,
&err)` dereferences its string argument inside its own body. And the value has a home: `iokit/KernelConfigTables.cpp:35`
defines `const char * gIOKernelConfigTables = "<plist ...>"`, whose object `iokit_KernelConfigTables.o` is 912
bytes — `.data` **4** (the pointer) plus `.rodata.str1.1` **0x82** (the table itself).

So 343 is **two objects**, and the second is needed only for its value: the first time in this walk that a
step's second object is not a callee's definition but a *stand-in's data*. Linking `libkern_c++_OSUnserialize.o`
alone would turn a stub stop into a `data abort` inside a real `OSUnserialize` — the walk's second value stop,
and the first one it could have prevented by reading its own ledger.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301643 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).
