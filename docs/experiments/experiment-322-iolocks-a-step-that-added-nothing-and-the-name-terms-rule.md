# Experiment 322 — `iokit/IOLocks.cpp`: a step that added nothing, and the name term's rule

**Step:** link **`iokit/Kernel/IOLocks.cpp`** (`out/xnu_kernel_obj/iokit_Kernel_IOLocks.o`, manifest:323) —
the object that defines `IOLockAlloc`, 321's stop, and IOKit's whole lock family.

**Prediction:** *counts **6 resolved / 0 added** (835 → **829** undefined, 725 → **719** function stubs,
storage unchanged at 110); `.text` **+0x224 plus a band**; every other section, the image (1413920) and the
headroom (1504360) unchanged to the byte; predicted stop **`OSlibkernInit`, caller key `0x8011B268` =
`StartIOKit+0xC8`**.*

**Result:** **every column is exact, the stop is the predicted name *and* the predicted key, and `.text`
closes in four terms with no residual — the closest `.text` point estimate in this walk, 4 bytes.** Every
other section, the image and the headroom are unchanged to the byte. And the frontier moved **up** a frame
for the first time: 320 entered `StartIOKit`, 321 ran the body of the function it called, and this step
makes that function's own callee real, so `IOLibInit` returns and `StartIOKit` advances to its next
statement.

## The object, and a baseline that reproduces 321

`iokit/Kernel/IOLocks.cpp` is the opposite shape to 321's object, and in the one way that matters here it
is the first of its kind in this walk: it brings **no storage of any kind**.

| | size |
|---|---|
| `.text` | **820** (0x334), 25 functions |
| `.comment` | 40 |
| `.bss`, `.data`, `.rodata`, `.rodata.str1.1` | **none** — the object's section list is `.text`, `.comment`, `.note.GNU-stack`, `.ARM.attributes` |
| symbols | 25 `T`, 1 `t`, **19 undefined** |
| references | 19 |

It defines the whole `IOLock` family: `IOLockInitWithState`, `IOLockAlloc`, `IOLockFree`,
`IOLockGetMachLock`, `IOLockSleep`, `IOLockSleepDeadline`, `IOLockWakeup`, the ten `IORecursiveLock*`
entry points, the three `IORWLock*` ones and the three `IOSimpleLock*` ones.

The baseline was built in this session with an **empty stand-in object** in this slot, and it reproduced
321's image to the byte (`.text` 0x13D3E0, `.data` 0x80140000/0x19210, `.sysctl_set` 0x80159210/0x10C,
`.init_array` 0x8015931C/0x4, `.bss` 0x80159340/0x37858, `__bss_end` 0x80190B98, image 1413920, headroom
1504360).

|  | predicted | measured |
|---|---|---|
| undefined | 829 | **829** |
| function stubs | 719 | **719** |
| storage stubs | 110 | **110** |
| resolved / added | 6 / 0 | **6 / 0** |

Resolved are the six of the 25 that are currently stubs: `IOLockAlloc`, and `IORecursiveLockAlloc`,
`IORecursiveLockLock`, `IORecursiveLockSleep`, `IORecursiveLockUnlock`, `IORecursiveLockWakeup`. The other
19 are definitions nothing references yet — 250/270's rule. **Added is nothing**, because all 19 references
are already defined: `current_thread`, `IOMalloc` and `IOFree` (real as of 321), `IOLockGroup` (also 321's,
and the reason this step can add nothing at all), the eleven `lck_mtx_*`/`lck_rw_*`/`lck_spin_*` entry
points, and `thread_wakeup_prim`. It is the second step in this walk with an added count of zero, after
314's eleven-to-one.

## `.text` closes with no residual

`.text` 0x13D3E0 → **0x13D600** is +0x220:

```
  this object's .text                                  +0x334   (820, exact)
  the stub object's .text                              -0x090   (6 bodies retired, none created)
  the stub object's name strings                       -0x080   (0x3CF7 -> 0x3C77: exactly the
                                                                align4(len+1) sum of the six retired
                                                                names, 12+24+20+24+24+24)
  .text-region alignment fill                          -0x004   (0xD2D -> 0xD29; 56 fills in both)
                                                      -------
                                                       +0x220   against a measured +0x220
```

The point estimate said +0x224 + a band and came in 4 bytes high — the discipline of writing the band
rather than a number is what makes that a band question and not a mechanism question.

## The name term: the rule, and 321's 0x58 resolved and withdrawn

The row above is exact, and 322 is where it becomes a rule rather than a coincidence. The name pool holds
**one padded slot per *function* stub and nothing else**, so

```
name term = Σ align4(len+1) over the added function-stub names
          − Σ align4(len+1) over the retired function-stub names
```

Measured directly on the three images the device ran, taking the pool's extent as its first name to the
next input's start:

| image | function stubs | pool extent | delta |
|---|---|---|---|
| 320 (321's baseline) | 724 | 0x3C87 | |
| 321 | 725 | **0x3CF7** | **+0x70** |
| 322 | 719 | **0x3C77** | **−0x80** |

Both deltas are byte-exact under that model:

* **321's +0x70** = 0x98 − 0x28. **0x98** is the five names it added — `IOMapPages` 12, `IOUnmapPages` 16,
  `getPhysicalAddress` 48, `inTaskWithPhysicalMask` 64, `proc_name` 12. **0x28** is the four it resolved —
  `IOFree` and `IOSleep` 8 each, `IOLibInit` and `IOMalloc` 12 each.
* **322's −0x80** is the six names above and nothing else.

And the pool is a table, not a compression: `align4(len+1)` over *all* of an image's function names
reproduces that pool's **span** (first name to the last name's NUL) to within one byte — 0x3CE8 against
0x3CE7 at 321, 0x3C78 against 0x3C77 at 320. In the 322 image all 719 function names are present as whole
NUL-terminated strings in one dense run, **0x80138FD0 … 0x8013CC37**, each preceded by a NUL, i.e. each in
its own slot.

The pool holds **no storage name at all**. Of this image's 110 storage stand-ins, exactly two match as
whole strings anywhere in the binary, and both are coincidences:

| storage name | where it matches | what it actually is |
|---|---|---|
| `mountroot` | 0x8013B654, inside the pool range | the tail of the pool's own `vfs_mountroot\0` |
| `systemdomain` | 0x80132AED | inside another object's kprintf format, `"dp == systemdomain"` |

So 321's 0x58 is **not** tail merging, and the reading that stood in 321's record — that its seven names
"shared tails with the pool", inferred from 832 names summing 0x4458 by `align4(len+1)` against a pool of
0x3C77 — is withdrawn. The 832 names include the 108 storage stand-ins, whose own `align4` sum is
**0x810**, which is the whole of that gap.

The 0x58 is exactly 0x30 + 0x28, and both terms are the ledger's own:

* **0x30** = `debug_iomalloc_size` (20) + `debug_iomallocpageable_size` (28). Both are *storage* stand-ins,
  so neither name enters the pool — and neither is present anywhere in the image.
* **0x28** = the four resolved names, already subtracted in the prediction's own parenthetical and left out
  of its sum. That is the arithmetic slip this ledger has now recorded three times (307, 317, 321).

One number of 321's closure table is corrected with it: the base of its name term was written as **0x3C77**,
which is 320's pool *span*; 320's pool *extent*, the definition its end 0x3CF7 uses, is **0x3C87**. Same
value, two definitions — the defect class this project has recorded before — and the +0x070 delta was right
either way.

## Nothing else moved, to the byte

This is the first step in four where the object brings no storage, so `.bss` had nothing to say and
`.data` had nothing to say:

| | base (321) | measured (322) | delta |
|---|---|---|---|
| `.text` | 0x13D3E0 | **0x13D600** | +0x220 |
| `.data` | 0x80140000 (0x19210) | 0x80140000 (0x19210) | 0 |
| `.sysctl_set` | 0x80159210 (0x10C) | 0x80159210 (0x10C) | 0 |
| `.init_array` | 0x8015931C (0x4) | 0x8015931C (0x4) | 0 |
| `.bss` | 0x80159340 (0x37858) | 0x80159340 (0x37858) | 0 |
| `__bss_end` | 0x80190B98 | **0x80190B98** | 0 |
| image | 1413920 (0x1592C0) | **1413920 (0x1592C0)** | 0 |
| headroom | 1504360 | **1504360** | 0 |

`.text` now ends at 0x8013D600, 0x2A00 below the 16 KB boundary at 0x80140000 — so the step had 0x2A00 of
room and used 0x220 of it. That is 319's case, and the third step in four that cost only `.text` (319
nothing at all, 321 a boundary for text that fitted under it, this one nothing again).

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=OSlibkernInit
 xnu_entry_stub_caller=0x8011b268   (also _a and _e)
```

`tools/host_resolve_entry_addr.sh 0x8011b268` → `StartIOKit+0xc8`, `caller-4` = `0x8011b264: bl
8011d378 <OSlibkernInit>` — the predicted name and the predicted key.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, `STAGE90_HW_WATCHDOG ARMED`, software dead-man armed,
no storage symbols in the payload), log **301625** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to Android
on its own (`ro.build.version.release` = 10).

## What it measures: four steps that read as one sentence

320 entered `StartIOKit` at its first stub; 321 ran `IOLibInit`'s whole body up to *its* one stub; 322 makes
that stub real, so `IOLibInit` runs off its epilogue — `strb r8, [r5]` sets its own `libInitialized`, then
`pop {r4, r5, r6, r7, r8, pc}` — and **returns**. The frontier therefore moved for the first time in this
walk *up* a frame, back into the caller's own next statement rather than into a callee.

`IOLockAlloc` is what makes that visible: it is **twelve bytes** — load `IOLockGroup`, `ldr`, then
**`b lck_mtx_alloc_init`**, a tail branch to a function that has been real since 312 — so the stub at
0x8011D86C was entered and never returned to, and the `caller-4` at the *next* stop is the caller's
caller's instruction. The key moved from `IOLibInit+0x10C` to `StartIOKit+0xC8` in one step, and the two
offsets it moved across are a whole function's return.

`IOLocks.cpp`'s 25 functions are all real now, 19 of them unreferenced, and IOKit's lock family is complete
— `IOLock`, `IORecursiveLock`, `IORWLock` and `IOSimpleLock` — over the `lck_*` primitives that were
already real.

## What it does not measure

* **`OSlibkernInit`.** It is defined by `libkern/c++/OSRuntime.cpp`, the next step.
* **The 19 unreferenced `IOLock*` entry points** — defined, linked, unreached.
* **Whether anything ever takes one of these locks.** `IOLockAlloc` was called twice by `IOLibInit` and
  both results were stored in the two page allocators, so the locks exist and nothing contends for them.
* **The tail-branch reading of the key.** `IOLockAlloc`'s `b` is the reason the key jumped a frame; for a
  real function that returns, the key would have named the *next* call inside `IOLibInit` instead.

## Next

**`libkern/c++/OSRuntime.cpp`** (`libkern_c++_OSRuntime.o`, manifest:374) — the object that defines
`OSlibkernInit` and the C++ runtime initialiser: `.text` **2476**, `.data` **4**, `__DATA, __data` **48**,
`.rodata.str1.1` **320**, `.bss` **1**, 15 `T` and 26 references. Predicted **4 resolved / 6 added** — the
resolved being the four of its 15 functions that are stubs in the image today (`OSlibkernInit`,
`OSRuntimeInitializeCPP`, `OSRuntimeFinalizeCPP`, `OSRuntimeUnloadCPPForSegment`; `OSRuntimeUnloadCPP` is
*not* among them, something in the pool already defines it), and the added six being the references nothing
defines yet, all functions and so all function stubs:

```
_ZN11OSMetaClass10preModLoadEPKc        _ZN11OSMetaClass11postModLoadEPv
_ZN11OSMetaClass12checkModLoadEPv       _ZN11OSMetaClass14modHasInstanceEPKc
_ZN15OSMetaClassBase10initializeEv      _ZN8OSSymbol18checkForPageUnloadEPvS0_
```

so 829 → **831** undefined, 719 → **721** function stubs, storage 110. `.text` should be **+0x8EC plus up
to 0x140 plus a band**: 0x9AC of object text, minus 0x60 for the four retired stub bodies (24 bytes each,
the caller-key stub shape 322 measured — 6 of them were its own 0x90), minus 0x60 for the four retired
names (`OSlibkernInit` 16, `OSRuntimeInitializeCPP` and `OSRuntimeFinalizeCPP` 24,
`OSRuntimeUnloadCPPForSegment` 32), plus whatever part of the object's 320-byte `.rodata.str1.1` the pool
keeps — which 301/321's rule says can be as little as nothing.

**And the stop should be the first call in `OSlibkernInit`'s own body**: it calls
`OSMetaClassBase::initialize()` at its offset 0x04 — one of the six names this step adds as a stub — before
`OSRuntimeInitializeCPP` (real from this step) and a conditional `panic`. Predicted stop
**`_ZN15OSMetaClassBase10initializeEv`, caller key `OSlibkernInit+0x8`**.
