# Experiment 319 — `os/internal.c`, and the frame chain that unwound

**Step:** link **`libkern/os/internal.c`** (`out/xnu_kernel_obj/libkern_os_internal.o`, manifest:402) — the
object that defines `_os_trace_addr_in_text_segment`, 318's stop.

**Prediction:** *counts **1 resolved / 0 added**, so 814 → **813** undefined, 709 → **708** function stubs,
105 → 105 storage; the predicate returns **true** for a `format` string inside `__TEXT`, the second
`OSKextKextForAddress(addr)` returns the same header, the equality test passes, the function's real body
runs, the whole logging frame chain returns, and `PE_init_iokit` runs on to its one remaining stub call.
Predicted stop: **`StartIOKit`, caller key `0x80004A20`**.*

**Result:** **all of it landed, and the stop is `stub_hit=StartIOKit` at `xnu_entry_stub_caller=0x80004A20`
= `PE_init_iokit+0x34c`.** Every count is exact, `.text` closes with **no residual**, every other section is
unchanged to the byte, and the run is back on the bootstrap thread's own straight line for the first time
since 316.

## The object, and a baseline that reproduces 318

`libkern/os/internal.c` is the smallest step in a long time: `.text` **356** (0x164) for one function,
`.rodata.str1.1` **7** (the `"__TEXT"` literal), **1 definition** (`_os_trace_addr_in_text_segment`) and
**1 reference** (`strncmp`, real long ago). Its body switches on `mhp->magic` (`MH_MAGIC` 0xfeedface /
`MH_MAGIC_64` 0xfeedfacf) and walks the load commands looking for `LC_SEGMENT` with segname `__TEXT`,
returning whether `addr` lies in `vmaddr .. vmaddr + vmsize`.

The baseline was built in this session with an **empty stand-in object** in this slot, reproducing 318
exactly (709 function / 105 storage / 814 undefined, `.text` 0x13B060, `.data` 0x8013C000 size 0x191C8,
`.bss` 0x80155300 size 0x37598, image 0x1552D8, headroom 1521512).

|  | predicted | measured |
|---|---|---|
| undefined | 813 | **813** |
| function stubs | 708 | **708** |
| storage stubs | 105 | **105** |
| resolved / added | 1 / 0 | **1 / 0** |

## `.text` closes with no residual, because the string term is zero

`.text` 0x13B060 → **0x13B1A0** is +0x140:

```
  this object's .text                                  +0x164   (356, exact)
  this object's .rodata.str1.1                         +0x000   the map prints 0x7 - see below
  the stub object's .text                              -0x018   (709 -> 708 bodies)
  the stub object's name strings                       -0x020   (0x3A3F -> 0x3A1F: the 30-byte name
                                                                 `_os_trace_addr_in_text_segment`
                                                                 padded to 32)
  .text-region alignment fill                          +0x014   (0xD1B -> 0xD2F; 54 -> 55 fills)
                                                       -------
                                                        +0x140   against a measured +0x140
```

**The map prints `0x7` for a contribution of nothing**, and this time the duplicate can be *pointed at*
rather than inferred:

```
 .rodata.str1.1
                0x0000000080136ca8        0x7 /mnt/.../libkern_os_internal.o
 .rodata.macho  0x0000000080136ca8      0x108 /mnt/.../xnu_arm_entry_macho.o
                0x0000000080136ca8                _mh_execute_header
```

The next input starts at the **same address**, so the seven bytes were dropped. The built code loads the
surviving `"__TEXT"` from **0x80128CFF** — `movw r6, #0x8cff / movt r6, #0x8012` — where the image holds
`__TEXT\0__DATA\0__LAST…`, and the whole image contains exactly **two** occurrences of `__TEXT\0`: that one
and the Mach-O header's own `segname` field. This is the **third sighting** of the rule 301, 312 and 314
established (an object's mergeable section size is an upper bound; so is the map's contribution line), and
the first where the deduplicated string can be named.

## Every other section is unchanged — and that is 304's mechanism read the other way

| | base (318) | measured (319) | delta |
|---|---|---|---|
| `.text` | 0x13B060 | **0x13B1A0** | +0x140 |
| `.data` | 0x8013C000 (0x191C8) | 0x8013C000 (0x191C8) | **0** |
| `.sysctl_set` | 0x801551C8 (0x10C) | 0x801551C8 (0x10C) | **0** |
| `.init_array` | 0x801552D4 (0x4) | 0x801552D4 (0x4) | **0** |
| `.bss` | 0x80155300 (0x37598) | 0x80155300 (0x37598) | **0** |
| image | 1397464 (0x1552D8) | **1397464 (0x1552D8)** | **0** |
| headroom | 1521512 | **1521512** | **0** |

`.text` now ends at 0x8013B1A0 and the 16 KB-aligned `.data` begins at 0x8013C000, so the 0x140 grew into
**0xE60 bytes of slack** and moved nothing. 318's step crossed four 16 KB boundaries at once and moved the
whole data block +0x14000; this one crossed none and moved nothing. Both are the same mechanism, and
together they are the reason a step's *image* cost cannot be read off its `.text` cost in either direction.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=StartIOKit
 xnu_entry_stub_caller=0x80004a20
 xnu_entry_stub_caller_a=0x80004a20
 xnu_entry_stub_caller_e=0x80004a20
```

`tools/host_resolve_entry_addr.sh 0x80004a20` → `PE_init_iokit+0x34c`, `caller-4` =
`80004a1c: bl 8011dccc <StartIOKit>` — the call 316 predicted and did not reach, and 317 predicted and did
not reach either. Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in
the payload), log **301622** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to Android on
its own (`ro.build.version.release` = 10).

## What it measures: four functions closed at once, and the straight line is back

`_os_log_to_log_internal` is 0xA6C bytes and its whole real body ran. Read off the linked image, the only
calls it makes are `OSKextKextForAddress` (real since 317), `_os_trace_addr_in_text_segment` (this step),
`memset`, `hw_atomic_add`, `strchr`, `memcpy`, `__aeabi_memclr8`, `__bzero`, `strlen`, `_encode_data`, with
`_os_log_encode`/`_os_log_actual` inlined — including a real `_firehose_trace` write (257's ported
implementation) — and `atm_get_diagnostic_config`, `mach_continuous_time`,
`mach_continuous_approximate_time`, `current_thread`, `thread_tid`. **Every one is real**, and the two gates
that would have opened `_firehose_trace` → `oslog_streamwakeup` → `selwakeup`/`wakeup` (two stubs) —
`oslog_stream_open` and `oslog_is_safe()` — are both closed this early in the boot.

Then `printf` and `vprintf_internal` returned, and **`PE_init_iokit` ran the rest of its own body**:
`pe_prepare_images`, the `/chosen/memory-map` lookups, `PE_get_default("progress-dy")`,
`vc_progress_initialize`, `kdebug_debugid_enabled`, and the four `/chosen/iBoot` lookups if its last
conditional sent it that way. `StartIOKit` is the **first stop of this entire walk that is inside
`PE_init_iokit`'s own body** rather than below a call it made.

**One consequence worth recording, because it explains something that had been odd for a hundred
experiments: no XNU `printf` text appears in any log.** This image's `printf` is Apple's os_log shim, so
`printf("iBoot version: %s\n", firmware_version)` — `PE_init_iokit`'s second statement — becomes a
tracepoint rather than a console write and never reaches `PE_putc`. The `printf` diversion 316 found is not
an accident of this image's log settings; it is what `printf` *is* here.

## What it does not measure

* **`StartIOKit`.** It is 24 bytes of stub; the object that defines it is the next step.
* **Which side of `PE_init_iokit`'s last conditional was taken.** Both sides reach `bl StartIOKit` at
  0x80004A1C, and the nonzero side calls `kernel_debug` first, whose `kernel_debug_internal` calls
  `current_proc` (a stub) at 0x8003C080/0x8003C0D0 behind `kdebug_flags` bits 4 and 6. The prediction named
  this as its alternative and it remains unmeasured — one branch from the stop, not a hypothesis about a
  missing call.
* **The closure `StartIOKit` opens.** 29 references, nearly all libkern C++ and IOKit; none of them is
  reached yet.
* **Anything about the 104 names 318 added.** Still linked and unreached.

## Next

**`iokit/Kernel/IOStartIOKit.cpp`** (`iokit_Kernel_IOStartIOKit.o`, manifest:346) — the ordinary shape, and
the largest closure in many steps: `.text` **876**, `.bss` **12**, `.rodata.str1.1` **142**, **5 functions**
(`StartIOKit`, `IOKitInitializeTime`, `iokit_post_constructor_init`, `IORecordProgressBackbuffer`,
`IORegistrySetOSBuildVersion`), **3 storage** (`gIOProgressBackbufferKey`, `gIORemoveOnReadProperties`,
`record_startup_extensions_function`, all `B`) and **29 references** — `OSObject::operator new`,
`OSString::withCString`, `OSSymbol::withCStringNoCopy`, `OSSet::withObjects`, `OSKext::initialize`,
`IOCatalogue::initialize`, `IORegistryEntry::initialize`/`getRegistryRoot`/`fromPath`,
`IOService::initialize`/`waitForService`/`resourceMatching`, `IOUserClient::initialize`,
`IOMemoryDescriptor::initialize`, `IOPlatformExpertDevice`'s constructor, `IOLibInit`, `OSlibkernInit`,
`IOCPUInitialize`, `interruptAccountingInit`, `clock_initialize_calendar`, `devsw_init`, `version`,
`gIOKitDebug`, `gIOKitTrace`, `gCanSleepTimeout`, `PE_parse_boot_argn`.

**320 is the step that enters IOKit**, and its added count is where this walk's numbers move again for the
first time in a while.
