# Experiment 323 — `libkern/c++/OSRuntime.cpp`: the C++ runtime becomes real, and a miss that was the previous step's zero

**Step:** link **`libkern/c++/OSRuntime.cpp`** (`out/xnu_kernel_obj/libkern_c++_OSRuntime.o`, manifest:374) —
the object that defines `OSlibkernInit`, 322's stop, and the kernel's whole C++ runtime.

**Prediction:** *counts **4 resolved / 6 added** (831 undefined, 721 function stubs, storage 110); `.text`
**+0x8EC .. +0xA2C plus a band**; `.data` +0x64, `.bss` +1 byte; predicted stop
**`_ZN15OSMetaClassBase10initializeEv` at caller key `OSlibkernInit+0x8`**.*

**Result:** **every count is exact, the stop is the predicted name *and* the predicted key, and `.text`
closes with no residual in five terms — but the point estimate missed by 0x154, and one omission pays for
all of it: the prediction treated the stubs this step *creates* as nothing.** The run also gives this walk
its first offset conversion since 320.

## The object, and a baseline that reproduces 322

`libkern/c++/OSRuntime.cpp` is the kernel's C++ runtime, and it brings every section kind at once:

| | size |
|---|---|
| `.text` | **2476** (0x9AC), 15 functions |
| `.data` | **4** |
| `__DATA, __data` | **48** |
| `.rodata.str1.1` | **320** (0x140), eleven `.L.str*` blocks |
| `.bss` | **1** |
| references | **26** |

What it defines: the linker's `operator new`/`new[]`/`delete`/`delete[]` (`_Znwm`, `_Znam`, `_ZdlPv`,
`_ZdaPv`, over `kern_os_malloc`/`kern_os_free`/`kern_os_realloc`), the constructor initialiser
`OSRuntimeInitializeCPP` (0x310 bytes — the function 318 found walking `__mod_init_func` by *section name*),
`OSlibkernInit` (72 bytes) which calls it and `panic`s on failure, `OSRuntimeFinalizeCPP`,
`OSRuntimeUnloadCPP`, `OSRuntimeUnloadCPPForSegment`, `__cxa_pure_virtual`, plus the data
`kOSRuntimeLogSpec`, two 24-byte `VM_ALLOC_SITE_STATIC` sites and the one-byte static
`_ZL21gKernelCPPInitialized`.

The baseline was built in this session with an **empty stand-in object** in this slot and reproduced 322's
image to the byte (`829/719/110`, `.text` 0x13D600, `.data` 0x80140000/0x19210, `.sysctl_set`
0x80159210/0x10C, `.init_array` 0x8015931C/0x4, `.bss` 0x80159340/0x37858, `__bss_end` 0x80190B98, image
1413920, headroom 1504360).

|  | predicted | measured |
|---|---|---|
| undefined | 831 | **831** |
| function stubs | 721 | **721** |
| storage stubs | 110 | **110** |
| resolved / added | 4 / 6 | **4 / 6** |

The 4 resolved are the four of the object's 15 functions that are stubs in the image today —
`OSlibkernInit`, `OSRuntimeInitializeCPP`, `OSRuntimeFinalizeCPP`, `OSRuntimeUnloadCPPForSegment`
(`OSRuntimeUnloadCPP` is *not* among them: something in the pool already defines it). The 6 added are the
references nothing defines yet, all functions, so all function stubs:

```
_ZN11OSMetaClass10preModLoadEPKc        _ZN11OSMetaClass11postModLoadEPv
_ZN11OSMetaClass12checkModLoadEPv       _ZN11OSMetaClass14modHasInstanceEPKc
_ZN15OSMetaClassBase10initializeEv      _ZN8OSSymbol18checkForPageUnloadEPvS0_
```

No storage name resolves and no storage stand-in is created.

## `.text` closes with no residual in five terms

`.text` 0x13D600 → **0x13E180** is +0xB80:

```
  this object's .text                                  +0x9AC   (2476, exact)
  this object's .rodata.str1.1                         +0x131   (305 placed of the object's 320)
  the stub object's .text                              +0x030   (4 bodies retired, 6 created, 24 each)
  the stub object's name strings                       +0x080   (0x3C67 -> 0x3CE7; the model of 322
                                                                is exact again: created 0xE0 -
                                                                retired 0x60)
  .text-region alignment fill                          -0x00D   (0xD29 -> 0xD1C; 56 -> 55 fills)
                                                      -------
                                                       +0xB80   against a measured +0xB80
```

The region identity agrees: Σ(placed inputs) 0x13C8F6 → 0x13D483 = +0xB8D, Σ(fill) 0xD29 → 0xD1C = −0xD,
sum +0xB80. Exactly two inputs moved — `libkern_c++_OSRuntime.o` (+0xADD = its 0x9AC of text plus its
0x131 of placed strings) and `xnu_arm_entry_realstubs.o` (+0xB0 = 0x30 of bodies plus 0x80 of names) —
which is what a small closure looks like from the map's side.

**The string term, measured for the fourth time**: the object brings 320 bytes and the linker placed
**305**, dropping 15. That is the *smallest* dedup of the four measured so far — 319's was 100% dropped and
321's was 63%, and this one is 4.7%.

## The miss is one omission, and it was 322's zero

The prediction's `.text` summed −0x060 for the stub bodies and −0x060 for the names, i.e. **only the
retirements** — while the step also *creates* six function stubs, worth +0x90 of bodies and +0xE0 of name
slots. The 0x154 miss decomposes exactly:

| | bytes |
|---|---|
| six created stub **bodies** the body term did not count (6 × 24) | **+0x090** |
| six created **name slots** the name term did not count (0xE0) | **+0x0E0** |
| strings placed 0x131 against the object's own 0x140 | −0x00F |
| fill — inside the quoted band, and negative | −0x00D |
| | **+0x154** = 0xB80 − 0xA2C |

And the reason it could happen is a property of the *previous* step: **322 created no stubs at all** (6
resolved, 0 added), so the created half of both terms was zero there, and copying that shape forward
dropped it silently. 316 has the same shape one level down — a resolution that turned out to be *storage*
and so retired no body — and the tell is the same in both: **a term that is zero because of the step you
just measured is not a term that is zero.**

## The data block grew by its own arithmetic and did not move

`.text` ends at 0x8013E180, 0x1E80 below the 16 KB boundary, so `.data` stays at 0x80140000:

| | base (322) | measured (323) | delta |
|---|---|---|---|
| `.text` | 0x13D600 | **0x13E180** | +0xB80 |
| `.data` | 0x80140000 (0x19210) | 0x80140000 (**0x19240**) | +0x30 |
| `.sysctl_set` | 0x80159210 (0x10C) | **0x80159240** (0x10C) | +0x30 |
| `.init_array` | 0x8015931C (0x4) | **0x8015934C** (0x4) | +0x30 |
| `.bss` | 0x80159340 (0x37858) | **0x80159380** (0x37898) | +0x40 start, +0x40 size |
| `__bss_end` | 0x80190B98 | **0x80190C18** | +0x80 |
| image | 1413920 (0x1592C0) | **1413968 (0x1592D0)** | +0x30 |
| headroom | 1504360 | **1504232** | −0x80 |

Two places where the arithmetic is not the plain one, and both are rules this ledger already had:

* **`.data` grew 0x30 for 0x34 of input** — the object's 4-byte `.data` and its 0x30 `__DATA, __data` —
  because the section's fill fell by exactly 4 (0x7AAB / 9 fills → 0x7AA7 / 8 fills). **317's "a 4-byte
  insertion can be free"**, arriving in `.data` for the second time.
* **`.bss` grew 0x40 for a one-byte object** (`_ZL21gKernelCPPInitialized`, placed at 0x8018EF80), of which
  0x3F is new fill (0xFA / 38 fills → 0x139 / 39 fills): the byte did not fit the gap in front of the
  64-byte stand-in block, so the block moved one slot — **310/313's rule** — and the section's start moved
  with it (align64(0x80159350) = 0x80159380).

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN15OSMetaClassBase10initializeEv
 xnu_entry_stub_caller=0x8011d8e4   (also _a and _e)
```

`tools/host_resolve_entry_addr.sh 0x8011d8e4` → **`OSlibkernInit+0x8`**, `caller-4` = `0x8011d8e0: bl
8012190c <_ZN15OSMetaClassBase10initializeEv>` — the predicted name and the predicted key. It is also the
first key since 320 whose offset had to be **converted**: the `bl` is at object offset 0x844 and
`OSlibkernInit` starts at 0x840, so the section-relative offset is function offset 0x04 and the key is
+0x08 — 322's rule in the direction that needs arithmetic (321 needed none, its function being first in
its section).

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, `STAGE90_HW_WATCHDOG ARMED`, software dead-man armed,
no storage symbols in the payload), log **301646** bytes, one `stub_hit=` line, **no `exception:` line**,
and the log carries the stub's own instrumentation (`xnu_entry_stub_caller_v`, `_digits`, `_w0`, `_w1`)
above the decoded line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to Android
on its own (`ro.build.version.release` = 10).

## What it measures, and what it does not

Measured: **`OSlibkernInit`'s first frame** — `push {fp, lr}` and then the call into the stub. Nothing
after it in the same function is measured: not the `g_kernel_kmod_info + 0x94 = &_mh_execute_header` store,
not the call to `OSRuntimeInitializeCPP`, not the conditional `panic`, not the
`_ZL21gKernelCPPInitialized` store.

Not measured either: the rest of the C++ runtime this step just made real — the four
`operator new`/`delete` entry points, `OSRuntimeFinalizeCPP`, the two unload paths — and
`OSRuntimeInitializeCPP`'s scan for `__mod_init_func`/`__constructor` sections, which 318's finding says
the entry image does not describe, so it should find nothing. **324 is the step that will actually run it.**

## Next

**`libkern/c++/OSMetaClass.cpp`** (`libkern_c++_OSMetaClass.o`, manifest:370) — which defines five of the
six names this step just added as stubs, and is the largest object linked since 318:

| | size |
|---|---|
| `.text` | **6608**, plus a 4-byte COMDAT `.text._ZN15OSMetaClassMetaD0Ev` |
| `.rodata` | **180** |
| `.rodata.str1.1` | **939** |
| `.data` / `__DATA, __data` | **8** / **72** |
| `.bss` | **48** |
| `.init_array` | **4** — an entry nothing runs (318: the entry image describes no `__constructor`) |
| symbols | 63 `T`, 31 references |

Predicted **23 resolved / 3 added**: 23 function names resolved, and added
`debug_container_malloc_size` and `_ZN12OSOrderedSet9metaClassE` as storage plus
`_ZN12OSOrderedSet12withCapacityEjPFlPK15OSMetaClassBaseS2_PvES3_` as the one function — so 831 → **811**
undefined, 721 → **699** function stubs, 110 → **112** storage. `.text` predicted **+0x1550 .. +0x18FB plus
a band** (0x19D0 object + 0xB8 of `.rodata` + 4 of COMDAT + up to 0x3AB of strings + 0x44 of created names
− 0x228 of retired bodies − 0x354 of retired names).

**And the stop should not be where `StartIOKit` is heading.** `OSMetaClassBase::initialize`'s body is
`push {fp, lr}` and **three real `IOLockAlloc` calls** — real since 322 — so `OSlibkernInit` proceeds to
`OSRuntimeInitializeCPP` (real since this step), which calls the segment and section walkers and, only if it
finds a constructor section, `preModLoad`/`checkModLoad`/`postModLoad` (all real as of 324). Per 318 it
finds none, so the chain returns and `StartIOKit` advances one call: predicted stop **`devsw_init`, caller
key `0x8011B26C` = `StartIOKit+0xCC`** (`caller-4` = `0x8011b268: bl 8011df4c <devsw_init>`). The named
alternative: if the scan does find a section, the stop is inside that path instead — and 324's own log will
say which.
