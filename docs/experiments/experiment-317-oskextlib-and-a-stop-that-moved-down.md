# Experiment 317 — `OSKextLib.cpp`, and a stop that moved *down* instead of forward

**Step:** link **`libkern/OSKextLib.cpp`** (`out/xnu_kernel_obj/libkern_OSKextLib.o`, manifest:360) — the
object that defines `OSKextKextForAddress`, the name 316 stopped on.

**Prediction:** *the frame chain that diverted 316 has no stub left on it once this lands, so `printf`
returns into `PE_init_iokit` and that function runs to its one stub call. Predicted stop: `StartIOKit`,
caller key `0x80004A20`.* Plus counts: **4 resolved / 13 added**, 719 → 728 undefined and 629 → 638
function stubs.

**Result:** **the counts land exactly; the stop does not move forward at all — it moves one frame down.**
Measured `stub_hit=_ZN6OSKext14kextForAddressEPKv` at `xnu_entry_stub_caller=0x80096298` — **the same caller
key 316 reported** — because the function this step made real is a one-instruction tail branch into a name
this step itself created.

## The object, and a small one at that

`libkern_OSKextLib.o` is `.text` **1424** / `.rodata.str1.1` **474** / `.data` 4 (`gOSKextUnresolved`), with
**17 definitions** and **25 references**. The name invites the wrong file: `libkern/c++/OSKext.cpp`
(manifest:369) defines the C++ methods; the C shim `_os_log_to_log_internal` calls lives here.

Twelve of the 25 references are already defined in the image and none is currently undefined:

```
g_kext_map        kernel_map        kmem_free         panic
paniclog_append_noflush             printf            segPRELINKTEXTB
segSizePRELINKTEXT                  vm_deallocate     vm_map_copy_discard
vm_map_copyin     vm_map_copyout
```

The baseline was built in this session with an **empty stand-in object** in this slot, reproducing 316
exactly (719 / 629 / 90, `.text` 0x123A00, `.data` 0x80124000 size 0x18F90, `.sysctl_set` 0x8013CF90 size
0x108, `.bss` 0x8013D0C0 size 0x37018, image 0x13D098, headroom 1621800).

|  | predicted | measured |
|---|---|---|
| undefined | 728 | **728** |
| function stubs | 638 | **638** |
| storage stubs | 90 | **90** |
| resolved / added | 4 / 13 | **4 / 13** |

**Resolved 4**, all functions: `OSKextKextForAddress` (this stop), `kext_request`, `kext_dump_panic_lists`,
`OSKextRemoveKextBootstrap`. **Added 13**, all functions, every one defined by `libkern_c++_OSKext.o`:
`OSKextLog` and the twelve `_ZN6OSKext*` methods (`kextForAddress`, `loadKextWithIdentifier`,
`lookupKextWithIdentifier`, `lookupKextWithLoadTag`, `removeKextWithLoadTag`, `cancelRequest`,
`handleRequest`, `requestResource`, `considerUnloads`, `printKextPanicLists`, `printKextsInBacktrace`,
`removeKextBootstrap`).

The first draft of the prediction wrote **642** function stubs by adding the 13 without subtracting the 4 —
which is 307's count defect in miniature. The counter has to be rebuilt from resolved and added
(629 − 4 + 13), never nudged by the added count alone.

## `.text` closes to 2 bytes, and the terms are all checkable

`.text` 0x123A00 → **0x1243E0** is +0x9E0, against a predicted +0x9A0 ± 0x40 — the top edge of the band:

```
  this object's .text                                  +0x590   (1424, exact)
  this object's .rodata.str1.1, linked                 +0x1C6   (the map's contribution line; the
                                                                 object's own section is 0x1DA, so
                                                                 20 bytes were relaxed away)
  the stub object's .text                              +0x0D8   (4 bodies out, 13 in: 9 x 0x18)
  the stub object's name strings                       +0x1A1   (predicted 0x1A4 - the estimate
                                                                 assumed 4-aligned names)
  .text-region alignment fill                          +0x013   (0xD0D -> 0xD20; 54 -> 56 fills)
                                                       -------
                                                        +0x9E2   against a measured +0x9E0
```

Two of the five terms are exact, one is 3 bytes high for a stated reason (name strings are not 4-aligned),
and the 0x1C6 is a *contribution line* — which 314 established is an upper bound for a mergeable input even
when it is nonzero. The residual is 2 bytes and is left as a residual rather than attributed.

Four bodies retire and thirteen are created, so both the body term and the name term are positive for the
first time in several steps — a net +9 bodies and +9 names — and the names are long C++ mangled ones:
0x200 of padded string against 0x5C retired.

## `.data` gains 4 bytes and the section does not grow — 312's rule, in `.data`

`gOSKextUnresolved` lands at **0x80140128**, in exactly the slot the empty stand-in occupied, immediately
before `osfmk_arm_arm_init.o`'s `const_boot_args`:

```
 base     .data  0x8013c128  0x0  /tmp/empty_entry.o
          .data  0x8013c128  0x144 osfmk_arm_arm_init.o   (const_boot_args … BootArgs)
          *fill* 0x8013c26c  0x4
          __DATA, __data
                 0x8013c270  0x30  osfmk_arm_cpu.o

 real     .data  0x80140128  0x4  libkern_OSKextLib.o   (gOSKextUnresolved)
          .data  0x8014012c  0x144 osfmk_arm_arm_init.o
          (no fill)
          __DATA, __data
                 0x80140270  0x30  osfmk_arm_cpu.o
```

The `__DATA,__const` block moves +4 relative to the section and the 4-byte fill at its end disappears, so
everything from `osfmk_arm_cpu.o` onward keeps its offset and `.data` stays **0x18F90**. The section's fill
total falls by exactly 4 (8 fills / 0x7AAA → 7 fills / 0x7AA6). **The fill absorbed it** — 312's `.bss` case
arriving in `.data`.

## The 16 KB step, and a `.bss` with nothing to say

`.text`'s end crossed another 16 KB boundary, so the whole data block moves a page-group — 304's mechanism,
last seen at 314:

| | base (316) | measured (317) | delta |
|---|---|---|---|
| `.text` | 0x123A00 | **0x1243E0** | +0x9E0 |
| `.data` | 0x80124000 (0x18F90) | **0x80128000** (**0x18F90**) | +0x4000 start, **size 0** |
| `.sysctl_set` | 0x8013CF90 (0x108) | **0x80140F90** (**0x108**) | +0x4000, size 0 |
| `.bss` | 0x8013D0C0 (0x37018) | **0x801410C0** (**0x37018**) | +0x4000, **size 0** |
| image | 0x13D098 | **0x141098** | +0x4000 |
| `__bss_end` | 0x801740D8 | **0x801780D8** | +0x4000 |
| headroom | 1621800 | **1605416** | −0x4000 |

**`.bss` is the first step in eight where the alignment rule has nothing to say.** This object has no
`.bss`, retires no storage stand-in and creates none, so the section is unchanged to the byte and its start
moves only because `.data` did. Predicted, and it is the only section prediction in this step that needed no
arithmetic at all.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN6OSKext14kextForAddressEPKv
 xnu_entry_stub_caller=0x80096298
 xnu_entry_stub_caller_a=0x80096298
 xnu_entry_stub_caller_e=0x80096298
```

`tools/host_resolve_entry_addr.sh 0x80096298` → `_os_log_to_log_internal+0x2c`, the `bl` at 0x80096294 —
**the same key 316 reported**. Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no
storage symbols in the payload), log **301642** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to Android on
its own (`ro.build.version.release` = 10).

## Why the stop is at the same caller key, one frame down

`OSKextKextForAddress` is **one instruction**:

```
80108c7c <OSKextKextForAddress>:  b 8010c7b8 <_ZN6OSKext14kextForAddressEPKv>
80108c80 <OSKextLoadedKextSummariesUpdated>:  bx lr     <- this object's empty definition
```

The `b` does not set `lr`, so when `kextForAddress`'s newly created stub fires, the register it reports is
still 0x80096298 — the return address of the `bl` that `_os_log_to_log_internal` made:

```
80096290  mov r0, r9                       ; the address os_log was asked to describe
80096294  bl OSKextKextForAddress          ; lr = 0x80096298
            -> b _ZN6OSKext14kextForAddressEPKv     <- 317 made this a stub
```

This is 315's trampoline idiom — *a `b` preserves `lr`, so the stub names the caller one level up* — in a new
shape, and the shape is the step's real lesson: **resolving a name can move the frontier one frame *down*
into the function the step just linked instead of forward along the boot's own call chain.** The run is not
in a different place; it is one frame deeper in the same place, which is why the caller key is identical by
construction rather than by coincidence. 284 met the neighbouring case (the frontier resuming *inside* the
function the step links); this is the case where the frontier drops *below* it.

## What it measures

`libkern/OSKextLib.cpp` ran — all 1424 bytes of it, entered from the boot's own logging path — and did what
its source says: read the address, tail-branch to the C++ lookup. `OSKextLoadedKextSummariesUpdated` and the
rest of the object's 17 definitions are linked and unreached.

## What it does not measure

* **Anything about the C++ lookup.** `_ZN6OSKext14kextForAddressEPKv` is a stub; the object that defines it
  is the next step.
* **Any of the 13 names this step created.** They are linked as reporting stubs and reached by nothing.
* **Whether `gOSKextUnresolved`'s value is right.** It is 4 bytes of zeroed `.data` under a name that reads
  as a flag; nothing has written it and nothing has read it.
* **Whether the 12 already-defined references point where they should.** `segPRELINKTEXTB` and
  `segSizePRELINKTEXT` in particular are names a kext loader reads, and this run never called anything that
  reads them.

## Next

**`libkern/c++/OSKext.cpp`** (`libkern_c++_OSKext.o`, manifest:369) — now not optional, because it defines
`_ZN6OSKext14kextForAddressEPKv` *and* all twelve other names this step created. Recomputed against *this*
image rather than against 316's: **18 resolved / 104 added**, so 728 → **814** undefined, 638 → **709**
function stubs, 90 → **105** storage.

The 18 resolved are the 13 this step added, plus five that were already stubs: `gLoadedKextSummaries`,
`gLoadedKextSummariesTimestamp` and `kmod` (all `B`), and `OSKextGetAllocationSiteForCaller`,
`OSKextGetKmodIDForSite`. Of the 104 added, **86 are functions** — 82 with a defining object in the pool and
4 with none (`__cxa_atexit`, `osrelease`, the two `__llvm_profile_*`), which by the 304 rule still arrive as
function stubs — and **18 are storage stand-ins** (7 `B`, 11 `R`, the nine other `metaClassE` variables,
`kOSBooleanFalse`/`kOSBooleanTrue`, `gIOCatalogue`, the `gIO*Key` strings and `sysctl__debug_children`).

The object also brings an `.init_array` that this image's `entry.ld` does not name, which would become an
**orphan output section** — the build's own layout report ("the allocated output sections are exactly `.bss`
`.data` `.sysctl_set` `.text`") is written to catch that. And 104 new stubs is the largest single jump in
this walk in both directions at once.

Predicted stop: **`StartIOKit`, caller key `0x80004A20`** — the prediction 316 and 317 both made and neither
reached, and this time it is backed by a walk of the chain rather than by a stub list:

* With `kextForAddress` real, its body reads `vm_kernel_stext` and `vm_kernel_etext` — **both zero in this
  image, because both are storage stand-ins** — so the test `addr >= stext && addr < etext` is false, and the
  next test `if (!sKextSummariesLock)` is true, because that static is four zero bytes of this object's
  `.bss`. It **returns NULL without making a single call.**
* `_os_log_to_log_internal` then takes its `cmp r0,#0; beq` straight to the return — the second
  `OSKextKextForAddress` call at 0x800962c8 is not reached — and the stack unwinds through `os_log_with_args`,
  `vprintf_internal` and `printf`, none of which has a stub call.
* **Every path of `PE_init_iokit` ends at the same call.** The guard at 0x800048e4 tests `kdebug_enable` — a
  real 0x88-byte object in `bsd_kern_kdebug.o`, not a stand-in — and `beq 80004A0C` sends a zero straight to
  the `bl StartIOKit`; the nonzero path does its four DT-lookup pairs, calls `bl kernel_debug`, and then
  `b 80004A0C`. The function's own extent is 0x378, so 0x80004A28 is inside it and not a neighbour. Four
  branches, one destination.

The named alternative is that some stub below `kextForAddress` fires anyway — which would mean the storage
stand-ins are not zero, and that would be a fact about the boot worth knowing.
