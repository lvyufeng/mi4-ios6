# Experiment 318 — `c++/OSKext.cpp`, a build that refused the step, and a `B` symbol that is not a stand-in

**Step:** link **`libkern/c++/OSKext.cpp`** (`out/xnu_kernel_obj/libkern_c++_OSKext.o`, manifest:369) — the
object that defines `_ZN6OSKext14kextForAddressEPKv`, 317's stop, and all twelve other names 317 created.

**Prediction:** *counts 18 resolved / 104 added; `.text` grows by the object's 74436 bytes; and the stop is
`StartIOKit` at caller key `0x80004A20`, because with `kextForAddress` real its body reads
`vm_kernel_stext`/`vm_kernel_etext` — both zero, because both are storage stand-ins — and returns NULL
without a call.*

**Result:** **the build refused the step before it could be run, every count is exact, every section closes —
and the stop's stated premise is wrong in a way worth its own section.** Measured
`stub_hit=_os_trace_addr_in_text_segment` at `xnu_entry_stub_caller=0x800962B0` = `_os_log_to_log_internal+0x44`.
`vm_kernel_stext` and `vm_kernel_etext` are **not zero**: they are real definitions in
`osfmk/arm/arm_vm_init.c`, written by `arm_vm_init` from this image's own Mach-O header, and this boot has
already run it.

## The build refused the step, and that is half the result

`entry.ld` matched none of `libkern_c++_OSKext.o`'s sections for `.init_array`, so the linker made it a fifth,
orphan output section and `verify_sections` failed the build by name:

```
FAIL: the allocated output sections are '.bss .data .init_array .sysctl_set .text ' and this script
names exactly '.bss .data .sysctl_set .text ' - a section the linker placed on its own has appeared,
and whatever it is will be inside the window the payload copies and zeroes
```

That check was written after 297 and 298 for exactly this case, and this is the first step it has refused
since 291 — where the build refusing the step *was* the result. `entry.ld` now names `.init_array` and places
it between `.sysctl_set` and `.bss`, for three stated reasons: it is `WA`, so a writable input inside `.text`
would put a writable section in the region `entry_macho.s` describes as read-only `__TEXT` (298's whole
point); it sits with `.sysctl_set` because both are four-byte pointer tables the linker lays out and readers
find by address; and it keeps its **name** rather than being merged into `.data` by a wildcard, so the table
stays visible in the map. The build's expected list is now five names, and a sixth is again a failure.

## What is in the `.init_array`, and what it means that nothing calls it

One entry: an `R_ARM_TARGET1` relocation to `_GLOBAL__sub_I_OSKext.cpp`. That function is not a no-op:

```
_ZN6OSKext10gMetaClassE   OSMetaClass::OSMetaClass("OSKext", &OSObject::gMetaClass, 96)
                          OSKext::gMetaClass.vtable = &_ZTVN6OSKext9MetaClassE + 8
                          __cxa_atexit(OSMetaClass::~OSMetaClass, &OSKext::gMetaClass, __dso_handle)
```

**Nothing in this image runs it.** There is no C runtime here, and XNU's own initializer walk —
`OSRuntimeInitializeCPP` (`libkern/c++/OSRuntime.cpp:364`) — finds constructors by *section name*:
`sectionIsConstructor` (`OSRuntime.cpp:208`) accepts `__mod_init_func` or `__constructor`, and
`entry_macho.s` describes no section with either name. The consequence is stated where it can be found rather
than discovered later: **`OSKext::gMetaClass` is 96 bytes of this object's `.bss` and stays zero**, because
the initializer that would fill it is linked, correct, and unreachable. That is the stand-in hazard in its
sharpest form — the definition is present and the right size, and zero because the *initializer* lives
somewhere nothing calls.

## Counts, exact

The baseline was built in this session with an **empty stand-in object** in this slot, reproducing 317
exactly (728 / 638 / 90, `.text` 0x1243E0, `.data` 0x80128000 size 0x18F90, `.bss` 0x801410C0 size 0x37018,
image 0x141098, headroom 1605416).

|  | predicted | measured |
|---|---|---|
| undefined | 814 | **814** |
| function stubs | 709 | **709** |
| storage stubs | 105 | **105** |
| resolved / added | 18 / 104 | **18 / 104** |

**Resolved 18** — 15 functions and 3 storage: the 13 names 317 created (`OSKextLog` and the twelve
`_ZN6OSKext*`), plus five that were already stubs (`gLoadedKextSummaries` `B 0x4`,
`gLoadedKextSummariesTimestamp` `B 0x8`, `kmod` `B 0x4`, `OSKextGetAllocationSiteForCaller`,
`OSKextGetKmodIDForSite`).

**Added 104** — **86 functions** and **18 storage stand-ins**:

```
82 func T     with a defining object in the pool
 4 func -     with no defining object anywhere: __cxa_atexit, osrelease,
              __llvm_profile_get_size_for_buffer_internal, __llvm_profile_write_buffer_internal
              (by the 304 rule these still arrive as *function* stubs)
 7 data B     gIOCatalogue, gIOClassKey, gIOMatchCategoryKey, gIOResourceMatchKey,
              gIOUserClientClassKey, sysctl__debug_children, _ZN8OSObject10gMetaClassE
11 data R     the nine other metaClassE variables, kOSBooleanFalse, kOSBooleanTrue
```

**104 new stubs in one step is the largest single jump in this walk**, in both directions at once.

## Every section closes

| | base (317) | measured (318) | delta |
|---|---|---|---|
| `.text` | 0x1243E0 | **0x13B060** | +0x16C80 |
| `.data` | 0x80128000 (0x18F90) | **0x8013C000** (**0x191C8**) | +0x14000 start, +0x238 size |
| `.sysctl_set` | 0x80140F90 (0x108) | **0x801551C8** (**0x10C**) | +0x4 |
| `.init_array` | — | **0x801552D4** (**0x4**) | new |
| `.bss` | 0x801410C0 (0x37018) | **0x80155300** (**0x37598**) | +0x14240 start, +0x580 size |
| image | 0x141098 | **0x1552D8** | +0x14240 |
| `__bss_end` | 0x801780D8 | **0x8018C898** | +0x147C0 |
| headroom | 1605416 | **1521512** | −0x147C0 |

`.text` +0x16C80, and the terms sum to 0x16CA3 — a **residual of 0x23, in the over-counting direction**:

```
  this object's .text + COMDAT                          +0x122C8   (74436 + 4, exact)
  this object's .rodata                                 +0x1A8     (424, exact - not mergeable)
  this object's .rodata.str1.1, linked                  +0x3A22    (the map prints 0x3af4 beside it as
                                                                    "size before relaxing", so 0xD2 was
                                                                    relaxed inside the object)
  the stub object's .text                               +0x6A8     (-15 + 86 = +71 function bodies at
                                                                    0x18 each - 316's rule again: the
                                                                    retired count is the *function*
                                                                    resolutions, not the 18)
  the stub object's name strings                        +0x76E     (0x32D1 -> 0x3A3F)
  .text-region alignment fill                           −0x005     (0xD20 -> 0xD1B; 56 fills -> 54)
                                                       -------
                                                        +0x16CA3   against a measured +0x16C80
```

The only term that can over-count is a mergeable contribution (314's rule). A scan of the object's 439 string
constants finds **exactly 35 already present in the base image** — the same number as the residual, which is
suggestive and which is *not* claimed as the mechanism: deduplicating one 51-byte string alone would save
more than 35 bytes, so the coincidence is recorded rather than used.

`.data`'s +0x238 is exactly its content and its fill: 0x15B (the object's `.data`) + 0xD8 (its
`__DATA,__data`) = **0x233**, plus **+0x5** of fill (7 fills 0x7AA6 → 9 fills 0x7AAB). `.sysctl_set`'s +0x4 is
the object's `__DATA,__sysctl_set`. **`.bss`'s +0x580 is 0x1DD + 0x3C0 − 0x1D**: the object's 477 bytes, plus
the stand-in block growing 0x1744 → **0x1B04** (18 storage stand-ins arrive, 3 retire, and each takes a whole
64-byte slot because they all carry `aligned(64)`: 15 × 0x40 = 0x3C0 exactly), minus 0x1D of fill
(0x123 → 0x106).

And `.text`'s end crossed four more 16 KB boundaries, so `.data`'s start moves **+0x14000** — the largest
single step of 304's mechanism so far, and the reason the image grew 0x14240 while `.text` grew only 0x16C80
in total content terms.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_os_trace_addr_in_text_segment
 xnu_entry_stub_caller=0x800962b0
 xnu_entry_stub_caller_a=0x800962b0
 xnu_entry_stub_caller_e=0x800962b0
```

`tools/host_resolve_entry_addr.sh 0x800962b0` → `_os_log_to_log_internal+0x44`, the `bl` at 0x800962AC.
Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the payload), log
**301642** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to Android on
its own (`ro.build.version.release` = 10).

## A `B` symbol in the linked image is not automatically a stand-in

The prediction reasoned: *`vm_kernel_stext` and `vm_kernel_etext` are `B` in the linked image, they are absent
from the stub set, therefore both are zero, therefore `kextForAddress`'s first test is false and its second
(`if (!sKextSummariesLock)`) is true, and it returns NULL without a call.*

The first half is true and the conclusion is false. **Absent from the stub set means a real defining object**,
and that object is `osfmk_arm_arm_vm_init.o`, which is in `LINK_OBJS` and has already run:

```
arm_vm_init.c:406   segTEXTB = (vm_offset_t) getsegdatafromheader(&_mh_execute_header, "__TEXT",
                                                                    &segSizeTEXT);
arm_vm_init.c:496   vm_kernel_stext = segTEXTB;
arm_vm_init.c:497   vm_kernel_etext = segTEXTB + segSizeTEXT;
```

`segTEXTB` and `segSizeTEXT` are themselves `.bss` in the same object (`osfmk_arm_arm_vm_init.o`, 0x8015B910,
0xD8) and are computed at boot from this image's own Mach-O header. So `vm_kernel_stext` = the image's
`__TEXT` base and `vm_kernel_etext` = base + size, `format` is a string constant inside `__TEXT`, and
`kextForAddress` takes its **early return** — `return (void *)&_mh_execute_header` at 0x8011A36C — not the
NULL path. `dso` is therefore non-NULL and `_os_log_to_log_internal` proceeds. Source and disassembly agree
line for line:

```
80096284  cmp r6,#0 ; bne 800962a4                      if (dso) skip
80096294  bl OSKextKextForAddress                       dso = OSKextKextForAddress(format)  <- 316's stop
8009629c  cmp r0,#0 ; beq 800962bc                      if (!dso) return
800962ac  bl _os_trace_addr_in_text_segment             if (!_os_trace_addr_in_text_segment(dso, format))
                                                                                            <- 318's stop
800962b0  cmp r5,#0 / cmpne r0,#0 / bne 800962c4        if (addr && in_text) continue
800962c8  bl OSKextKextForAddress                       dso_addr = OSKextKextForAddress(addr)
800962cc  cmp r6,r0 ; bne 800962bc                      if (dso != dso_addr) return
```

305's lesson — *a predicate that is true of every function on a path is not a predicate about the path* — has
a value-shaped twin: **"this symbol is undefined/stubbed" is a statement about the link, not about the
value**, and a real defining object can write the name before the frontier reaches it. The stub set answers
"does this image define the name", and the answer to "what is in it" is a different question with a different
instrument.

## What it measures

`libkern/c++/OSKext.cpp` ran — `kextForAddress` returned `&_mh_execute_header`, which is a *correct* answer
for a `format` string inside the kernel text, and is the first time this walk has executed real XNU code whose
result depends on values written by earlier initialization rather than on symbol presence. The object's 104
new stubs are linked and unreached, and `_GLOBAL__sub_I_OSKext.cpp` is linked and unreached.

## What it does not measure

* **Whether `gMetaClass` matters yet.** It is zero, and nothing has read it.
* **Any of the 104 names this step created**, and none of the 18 it resolved beyond `kextForAddress`.
* **Whether `.init_array`'s placement is the right one for a later step.** If something ever runs the
  initializer, the section's name and address are where that step will find them; if XNU's runtime is ever
  taught to walk it, `entry_macho.s` needs the section described under `__mod_init_func`.
* **The `0x23` residual in `.text`.** Stated, with the term it must be in, and not attributed.

## Next

**`libkern/os/internal.c`** (`libkern_os_internal.o`, manifest:402) — the object that defines
`_os_trace_addr_in_text_segment`, and the smallest step in a long time: `.text` **356**, `.rodata.str1.1`
**7**, **1 definition**, **1 reference** (`strncmp`, already real). Predicted **1 resolved / 0 added**:
814 → **813** undefined, 709 → **708** function stubs, 105 → **105** storage.

Its body walks the `dso`'s Mach-O load commands looking for `LC_SEGMENT` with segname `__TEXT`, returning
whether `addr` lies in `vmaddr .. vmaddr + vmsize`. With `dso` = `&_mh_execute_header` and `addr` = `format`,
a string constant inside `__TEXT`, it should return **true**; then the second `OSKextKextForAddress(addr)`
returns the same `&_mh_execute_header`, the equality test passes, and the function runs on into its real body —
two `memset`s, a `va_copy`, `__doprnt` and the buffer bookkeeping.

Predicted stop: **`StartIOKit`, caller key `0x80004A20`**. After this step,
`tools/stub_calls_in_function.py _os_log_to_log_internal` reports **one** stub call in the function, and it is
this one; the function's other 37 `bl`s are real. `tools/xnu_entry_callwalk.py --root _os_log_to_log_internal`
agrees — no stub on the straight-line path — and, this time, names its own gap explicitly: the indirect calls
it cannot follow, nearly all of them inside the real `__doprnt`. So the named alternative is **`__doprnt`'s
indirect dispatch**, the one place this walk is still blind, and the second alternative is that `addr` is
NULL — which short-circuits to the same early return and the same outcome.
