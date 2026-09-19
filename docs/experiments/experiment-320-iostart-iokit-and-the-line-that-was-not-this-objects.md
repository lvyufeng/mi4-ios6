# Experiment 320 — `iokit/IOStartIOKit.cpp`, and the line that was not this object's

**Step:** link **`iokit/Kernel/IOStartIOKit.cpp`** (`out/xnu_kernel_obj/iokit_Kernel_IOStartIOKit.o`,
manifest:346) — the object that defines `StartIOKit`, 319's stop, and the step that enters IOKit.

**Prediction:** *counts **2 resolved / 21 added**, so 813 → **832** undefined, 708 → **724** function
stubs, 105 → **108** storage; `.text` **+0x7D2** from four named terms plus a fill band; every section
above `.bss` unmoved, the image staying 1397464; `__bss_end` moving +0xCC ± fill. Predicted stop:
**`IOLibInit`**, the first stub on `StartIOKit`'s straight line.*

**Result:** **the counts are exact to the unit, the stop is `IOLibInit`, `.text` closes with no residual,
and the section's `.bss` cost was absorbed by fill exactly as at 312.** The step is also the first one in
this walk where a mergeable contribution line belonging to a *different* object moved — and the closure
cannot be written without it.

## The object, and a baseline that reproduces 319

`iokit/Kernel/IOStartIOKit.cpp` is the largest closure in many steps: `.text` **876** (0x36C) for five
functions, `.bss` **12**, `.rodata.str1.1` **142** (0x8E), **3 `B` definitions** and **29 references**.

| function | object offset | length | linked |
|---|---|---|---|
| `IOKitInitializeTime` | 0x000 | 0x3C | 0x8011B0B0 |
| `iokit_post_constructor_init` | 0x03C | 0xB4 | 0x8011B0EC |
| **`StartIOKit`** | **0x0F0** | **0x184** | **0x8011B1A0** |
| `IORegistrySetOSBuildVersion` | 0x274 | 0x50 | 0x8011B324 |
| `IORecordProgressBackbuffer` | 0x2C4 | 0xA8 | 0x8011B374 |

The baseline was built in this session with an **empty stand-in object** in this slot, and it reproduced
319's image to the byte (`.text` 0x13B1A0, `.bss` 0x80155300 size 0x37598, image 0x1552D8, headroom
1521512).

|  | predicted | measured |
|---|---|---|
| undefined | 832 | **832** |
| function stubs | 724 | **724** |
| storage stubs | 108 | **108** |
| resolved / added | 2 / 21 | **2 / 21** |

`832` is `wc -l out/stage90/xnu_arm_entry_undef.txt`; `724 T` and `108 B` are
`nm -P out/stage90/xnu_arm_entry_realstubs.o`. The three storage stand-ins arrived sized from the pool as
predicted — `gCanSleepTimeout` (4, at 0x8018C500), `gIOKitDebug` (8), `gIOKitTrace` (8).

## `.text` closes — with a term that belongs to another object

`.text` 0x13B1A0 → **0x13B960** is +0x7C0:

```
  this object's .text                                  +0x36C   (876, exact)
  this object's .rodata.str1.1                         +0x08B   (139 placed - the object's own
                                                                section is 0x8E, see below)
  pexpert_arm_pe_init.o's .rodata.str1.1               -0x008   (446 -> 438, NOT this object's)
  the stub object's .text                              +0x180   (2 bodies retired, 18 created)
  the stub object's name strings                       +0x258   (0x3A1F -> 0x3C77, exact)
  .text-region alignment fill                          -0x007   (0xD2F -> 0xD28; 55 -> 56 fills)
                                                      -------
                                                       +0x7C0   against a measured +0x7C0
```

**The string term is 139, not 142, and the three missing bytes can be pointed at.** The object's own
`.rodata.str1.1` is

```
IORTC\0 IOKitBuildVersion\0 IOKitDiagnostics\0 io\0 iotrace\0 pmtimeout\0
IOProgressBackbuffer\0 OS Build Version\0 IODeviceTree:/chosen\0 IOProgressColorTheme\0
```

and the 139 bytes the linker placed at 0x80137188 are that string **with `io\0` removed**. The next input
starts at 0x80137213 = 0x80137188 + 0x8B, so the printed contribution is *consumed*, not merely printed.
The reference resolves to **0x80127FC7**, where the image holds `io\0/cpus\0running\0tim…`, loaded by
`movw r0, #0x7fc7 / movt r0, #0x8012` at 0x8011B1AC for the first `PE_parse_boot_argn`.

This is the **fourth sighting** of the rule 301, 312 and 314 established (an object's mergeable section
size is an upper bound, and so is the map's contribution line), and like 319 it is *shown* rather than
inferred — the placed bytes were read out of the image and differ from the object's by exactly one
three-byte string.

**The eight bytes are new.** `pexpert_arm_pe_init.o`'s `.rodata.str1.1` contribution line *fell* from 446
to 438. That object is not linked by this step and is not otherwise touched: its section shrank because
the string pool redistributed around the insertion. **So a mergeable contribution line is not a property
of the object that owns the section** — it is an allocation out of a pool, and an insertion anywhere in
the pool can move *any* line. The identity over the whole `.text` output section makes it visible and
exact:

```
  Sigma(placed inputs)   0x13A490 -> 0x13AC57   = +0x7C7
  Sigma(fill in .text)   0xD2F    -> 0xD28      = -0x007
                                                -------
                                                 +0x7C0   = the section's own delta
```

and the *only* contribution lines that changed are the five above.

That claim needed an instrument that works. The first version of this measurement **required the section
name and the input on the same line**, and the map's mergeable contributions are **two-line records** — a
section name on a line of its own, then one line per input:

```
 .rodata.str1.1
                0x0000000080137188       0x8b /mnt/.../iokit_Kernel_IOStartIOKit.o
                0x8e (size before relaxing)
```

so the parser dropped *every* mergeable input, reported the region's inputs as growing +0x4EC rather than
+0x7C7, and would have hidden the `pe_init` line entirely. That is a measurement defect of the 301/298
family, and it is why the closure above is quoted from the region sums rather than from a per-object
table.

## So the prediction missed by 0x12, and all three reasons are named

| | bytes |
|---|---|
| the string term was taken as the object's own section size (0x8E) instead of what the linker placed (0x8B) | **+3** |
| the pool took eight bytes back out of another object's line | **+8** |
| fill the point estimate did not carry | **+7** |
| | **+0x12** above the measured +0x7C0 |

The four named terms were each right to the byte. The whole miss is in the mergeable accounting — which
is where this walk's `.text` predictions have missed since 301, and the first time the *direction* is
explained rather than merely recorded. The band the prediction carried was for the fill term alone, so
the honest statement is that 0x12 is outside it.

## The predicted stop landed; the offset in the prediction was quoted against the wrong base

The relocation-stream offsets (0x118, 0x14C, 0x198, 0x1B0 …) are relative to the object's `.text`, and the
two functions before `StartIOKit` in that section are 0xF0 bytes (0x3C + 0xB4). So the `bl` that calls
`IOLibInit` is at **`StartIOKit+0xC0`**, not +0x1B0, and the run's caller key is **`StartIOKit+0xC4`**:

```
StartIOKit       0x8011B1A0
caller key       0x8011B264   = StartIOKit+0xC4
caller-4         0x8011B260   bl 8011BF38 <IOLibInit>   <- the instruction the prediction named
```

Its immediate successor is `bl 8011D378 <OSlibkernInit>`. The three `PE_parse_boot_argn` calls are
unconditional `bl`s with `cmp`/`strne` pairs after them, so the straight-line claim held: `IOLibInit` is
reached on every path.

## Every other section is unchanged — and `.bss` closes the way 312's did

| | base (319) | measured (320) | delta |
|---|---|---|---|
| `.text` | 0x13B1A0 | **0x13B960** | +0x7C0 |
| `.data` | 0x8013C000 (0x191C8) | 0x8013C000 (0x191C8) | **0** |
| `.sysctl_set` | 0x801551C8 (0x10C) | 0x801551C8 (0x10C) | **0** |
| `.init_array` | 0x801552D4 (0x4) | 0x801552D4 (0x4) | **0** |
| `.bss` | 0x80155300 (0x37598) | 0x80155300 (0x37658) | +0xC0 |
| `__bss_end` | 0x8018C898 | **0x8018C958** | +0xC0 |
| image | 1397464 (0x1552D8) | **1397464 (0x1552D8)** | **0** |
| headroom | 1521512 | **1521320** | −0xC0 |

`.text` ends 0x6A0 below the 16 KB-aligned `.data`, so the step crossed no boundary and cost nothing
outside `.text` — the second such step in a row, and 318's +0x14000 is still the other extreme.

`.bss` is the object's **12** bytes plus **three** 64-byte stand-in slots = 0xCC, and the section grew
0xC0 because the fill in front of the stand-ins fell by exactly 0xC (0x106 → 0xFA). The prediction said
"+0xCC plus or minus fill"; the fill took all of it.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=IOLibInit
 xnu_entry_stub_caller=0x8011b264
 xnu_entry_stub_caller_a=0x8011b264
 xnu_entry_stub_caller_e=0x8011b264
```

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, `STAGE90_HW_WATCHDOG ARMED`, software dead-man
armed, no storage symbols in the payload), log **301621** bytes — exactly one fewer than 319's, because
the payload records the stub's name verbatim and `IOLibInit` is one byte shorter than `StartIOKit`. One
`stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to Android
on its own (`ro.build.version.release` = 10).

## What it measures: IOKit's first function, and a store into a stand-in

`StartIOKit` is real for the first time and it is the first IOKit function this boot has ever run. Its
three `PE_parse_boot_argn` calls are real, and the third **stores** `pmtimeout`'s value into
`gCanSleepTimeout` at 0x8018C500 — a `.bss` stand-in that arrived in this same step. The instruction is
`strne r0, [r1]` with `r1 = 0x8018C500`; the slot exists, is 4 bytes, holds the value, and nothing reads
it. That is the stand-in hazard stated rather than discovered: the definition is present, and its only
consumer is a stub.

## What it does not measure

* **`IOLibInit`.** It is a 24-byte stub; `iokit/Kernel/IOLib.cpp` (manifest:322) is the next step.
* **The other three functions of this object.** `iokit_post_constructor_init`,
  `IORecordProgressBackbuffer` and `IORegistrySetOSBuildVersion` are defined now and referenced by
  nothing, which by 250/270's rule is not a count change — so nothing calls them yet.
* **The 18 new function stubs and 3 storage stand-ins**, beyond the fact that they exist.
* **Whether `OSKext::gMetaClass` is read by anything.** `OSKext::initialize` is a stub that stays a stub
  and `IOLibInit` is the next call in the body, so the answer is still no — the `.init_array` initialiser
  318 found linked and unreachable remains unreachable.
* **The pool's re-attribution rule itself.** That a mergeable line can move for another object is
  measured; *why* pe_init's line is the one that lost eight bytes is not, and this experiment does not
  claim a mechanism for it.

## Next

**`iokit/Kernel/IOLib.cpp`** (`iokit_Kernel_IOLib.o`, manifest:322) — the largest single object since
318: `.text` **6476**, `.bss` **384**, `.rodata.str1.1` **299**, a `__DATA,__data` of 72, a `.rodata` of
56, and **52 references**, with `IOCreateThread`, `IOFree`, `IOFreeAligned`, `IOFreeContiguous`,
`IOKernelAllocateWithPhysicalRestrict`, `IODelay` and `IOAlignmentToSize` among its definitions. It is
the object that makes IOKit's allocator and thread wrappers real, and it is where the frontier moves into
the IORegistry.
