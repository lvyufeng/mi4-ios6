# Experiment 447 — the non-terminal wrapper names the site, and the count says whether the flag's writer ever ran

**Status: measured, and every predicted row came in exact — the site is `commpage_populate+0x70`, the
first call of `ml_get_max_cpus` is the one that blocked, and the flag's writer never ran. What the run
adds over the prediction is the frontier's *direction*: the walk is some thirty frames behind the last
two hardware runs, in `vm_shared_region_init` and not in `bsd_init`.**

## Why: 446 named the frame and not the site

446 resolved the run's block to `ml_get_max_cpus+0x3c` — the function's own `thread_block` — and named
the condition (`max_cpus_initialized != MAX_CPUS_SET`). It could not name the site, because the terminal
wrapper reports the call *inside* the function it wrapped. The image has six `bl ml_get_max_cpus` sites
and two `bl ml_init_max_cpus` sites, all resolved:

| `bl ml_get_max_cpus` (`0x800081e8`) | site |
| --- | --- |
| `vm_page_init_local_q` | `0x80019fb8` |
| `waitq_alloc_prepost_reservation` | `0x800aad4c` |
| `commpage_populate` | `0x800e7594` |
| `mcache_init` | `0x801d10cc` |
| `mbinit` | `0x801d4384` |
| `sysctl_mib_init` | `0x8022c64c` |

| `bl ml_init_max_cpus` (`0x8000817c`) | site |
| --- | --- |
| `IOCPUInterruptController::initCPUInterruptController(int,int)+0x94` | `0x801246b8` |
| `MSM8974PlatformExpert::start(IOService*)+0x28` | `0x80266f4c` — this project's 405 probe |

## The change, and why these two are *not* terminal

`ml_get_max_cpus` **returns** once its flag is set, so it can be wrapped the way `vm_page_wait` is and
not the way `thread_block` is: the wrapper records its caller into a `.bss` slot and calls through, and
the block that ends the run still happens inside the real function. That is what makes the site it
records the site of the *block* rather than of a return — and it forces the one design decision in this
step: **the first caller is the reading, not the last.** The call that blocks is the first call to find
the flag unset; a call that found it set would not have blocked, and keeping the last would name the one
site that cannot be the one the run ended in. `entry_note_maxcpus` keeps the first site and a count;
`entry_note_initmax_cpus` keeps the first caller of the writer and the number it announced.

Five `.bss` slots (`g_maxcpus_caller`, `g_maxcpus_count`, `g_initmax_cpus_caller`,
`g_initmax_cpus_count`, `g_initmax_cpus_arg`, at `0x804f8ae8 .. 0x804f8af8` — inside `.bss`, inside the
window), five keys printed **outside** the dump beside 446's, and two `--wrap`s added to
`build_entry.sh`'s `TRACE_LDFLAGS` (five → seven).

## The prediction, written before the run

Build: `STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1 ./build_entry.sh` then
`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh`. `.text` **5001536 → 5001888** (+0x160),
entry bin **5208596** unchanged, `.bss` unmoved at `0x804f7a40 .. 0x80548a18`, layout unchanged
(`args +5545984, topOfKernelData +7340032, tree +7208960, window 0x01000000`). The routing is
mechanical and already checked in the image: **6** `bl __wrap_ml_get_max_cpus` and **2**
`bl __wrap_ml_init_max_cpus`, at exactly the eight sites above.

| key | predicted | why |
| --- | --- | --- |
| `xnu_entry_maxcpus_caller` | **`commpage_populate+0x70`** (`0x800e7598`) | 403's checkpoint measured the *first* invocation of `ml_get_max_cpus` on this boot at that site, and the ladder between `kernel_bootstrap` and `bsd_init` has not changed since; the first call is the one that blocks |
| `xnu_entry_maxcpus_count` | **`1`** | the recorded call is the blocking one, so nothing after it runs |
| `xnu_entry_initmax_cpus_count` | **`0`** | the writer's only runnable caller is this project's `MSM8974PlatformExpert::start+0x28`; the block *is* the statement that it did not run — and a non-zero count is the falsifier of the whole chain, because the flag is monotonic and a set flag cannot block a later call |
| `xnu_entry_initmax_cpus_caller` | **`0`** | follows from the row above; if it is not 0 the value identifies which of the two writers ran |

**The branch, and the alternatives are named rather than hidden.** The prediction is that the block is
the *first* `ml_get_max_cpus` call, from `commpage_populate`, and that nothing ever set the flag. The
two readings that would falsify it are both worth more than the prediction:

  * **a later site** (`mcache_init`, `mbinit`, `sysctl_mib_init` — all inside `bsd_init`) — which would
    mean the boot crossed `commpage_populate` *and* 439's and 440's whole frontier without setting the
    flag, i.e. that `ml_get_max_cpus` returned there with the flag unset, which the source does not
    allow; the only way it can happen is a second definition of `max_cpus_initialized`, which is the
    defect class this project has paid for twenty-four times and which the two functions' shared
    `0x804fbe88` was checked against in 446;
  * **`initmax_cpus_count != 0`** — the probe ran and the run still blocked, which no reading of the
    source survives, so it would mean the instrument is wrong rather than the boot.

Falsifiers of the instrument itself, named in advance: the keys absent from the report (the slots are
`#ifdef STAGE90_ENTRY_TRACE` and this build passes the define to `entry_stubs.c`'s own compile, so
absence means the build wiring is wrong); a count of 0 with a `thread_block` report, which would mean
the wrapper never ran on the path that blocked; and a `caller` of 0 with a non-zero count, which would
mean `__builtin_return_address(0)` was not the caller — it is `lr`, so the `bl` is at `caller - 4`.

## The measurement: every row as predicted, and the frontier is *behind* `bsd_init`

`.text` **5001536 → 5001888** (+0x160), entry bin **5208596** bytes unchanged, `.bss` unmoved at
`0x804f7a40 .. 0x80548a18`, layout unchanged. Payload `out/stage90/stage90-qcdt.img` **8228864** bytes,
sha256 `05651c4ce59bd6f69f1edb414e22e0d67d41c1a86218c196c4ed55a3b35a6ef0`. The routing was checked in the
image before the device was touched: **6** `bl __wrap_ml_get_max_cpus` and **2** `bl __wrap_ml_init_max_cpus`,
at exactly the eight sites named above. The gate passed, the run wrote **310209 bytes**, the device
returned on its own, 25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, no
`exception:`, no `panic:`, no `stub_hit=`.

```
 xnu_entry_block_caller=0x800082b4        <- ml_get_max_cpus+0x3c (the function moved 0x800081e8 -> 0x80008278)
 xnu_entry_block_continuation=0x00000000
 xnu_entry_block_kv_len=0x00001fea
 xnu_entry_vmwait_caller=0x00000000
 xnu_entry_vmwait_count=0x00000000
 xnu_entry_maxcpus_caller=0x800e7598      <- commpage_populate+0x70 (commpage_populate 0x800e7528 + 0x70)
 xnu_entry_maxcpus_count=0x00000001
 xnu_entry_initmax_cpus_caller=0x00000000
 xnu_entry_initmax_cpus_count=0x00000000
 xnu_entry_initmax_cpus_arg=0x00000000
```

| key | predicted | measured | |
| --- | --- | --- | --- |
| `xnu_entry_maxcpus_caller` | `commpage_populate+0x70` | `0x800e7598` = `commpage_populate+0x70` | ✓ |
| `xnu_entry_maxcpus_count` | `1` | `0x00000001` | ✓ |
| `xnu_entry_initmax_cpus_count` | `0` | `0x00000000` | ✓ |
| `xnu_entry_initmax_cpus_caller` | `0` | `0x00000000` | ✓ |

### What the two numbers decide

**The site is the one 403 first measured and 405 cured**, and the count is what makes that a statement
about the block rather than a statistic about the boot: `maxcpus_count` is 1 and the run ends in the
`thread_block` of that same call, so **the first call of `ml_get_max_cpus` on this boot is the call that
blocked**. Had the first call returned — the flag set by anything at all — the count at the block would
have been 2 or more, and the identity of the first caller would have said nothing.

**`initmax_cpus_count = 0` is the writer measured directly.** 446 inferred that the flag's writer had not
run, from the flag's own value at the reader; 447 puts the counter on the writer, and the number is the
same answer from the other side: `MSM8974PlatformExpert::start+0x28` — this project's experiment-405
probe — did not execute its third statement on this boot, and `IOCPUInterruptController`'s path, Apple's
own, is the one that cannot run here.

**And the frontier is *behind* `bsd_init`, which is new.** 439's stop was `stub_hit=bpf_init` at
`bsd_init+0x870`; 440's was the device-tree panic at `bsd_init+0x880`. This run stops in
`vm_shared_region_init`'s `commpage_populate`, which `kernel_bootstrap` calls *before* `bsd_init`
(`osfmk/kern/startup.c:576`). The walk is therefore not at a site it had merely passed through — it is
some thirty frames **earlier** than the last two hardware runs, and 445's reading of its own run ("the
boot passed `bpf_init` and reached a block after at least 16269 further allocation records") is refuted
by measurement rather than by argument. The record count and the frame are both real; the count does not
mean what 445 thought, because the name it leaned on had changed kind.

### The regression, narrowed to one function's first statements

Since `PE_init_iokit` has no early `return` — the only occurrence of `StartIOKit` in it is its last
statement (`pexpert/arm/pe_init.c:279`) — and `commpage_populate` is *after* it in the ladder, this run
**did** return through `StartIOKit`. And the config table keeps Apple's fallback behind this project's
personality on purpose (`stage90_platform_config_tables.c`: "with this class absent or its name wrong,
the machine must still behave exactly as experiment 362 measured it, and panic with the platform name
rather than boot silently with no platform expert at all"). So a matching attempt that failed would have
**panicked**, and there is no panic. The three readings that survive are all inside `StartIOKit`'s first
six statements (`iokit/Kernel/IOStartIOKit.cpp:120-170`):

  * `new IOPlatformExpertDevice` returned NULL — the class is instantiated **by name**
    (`OSMetaClass::allocClassWithName`, `IOService.cpp:3296-3301`), so a metaclass that never registered
    is a name that matches nothing, and nothing else in this boot reports it;
  * `initWithArgs` returned false — its device-tree walk is `IODeviceTreeAlloc`/`MakeReferenceTable`, the
    function whose `next_prop` overflow panicked at 440, and a walk that now returns an error rather than
    tripping the check skips the whole `if` body, `registerService` included;
  * `registerService` ran and the match failed with the fallback somehow out of the catalogue — the one
    reading the table's own comment is written to exclude.

All three are host-side questions before they are hardware ones — the image's constructor list and the
metaclass registry for the first, the tree's bytes at `0x806e0000` read with 443's replay for the second
— and that is where 448 starts.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the
hardware watchdog across the jump.
