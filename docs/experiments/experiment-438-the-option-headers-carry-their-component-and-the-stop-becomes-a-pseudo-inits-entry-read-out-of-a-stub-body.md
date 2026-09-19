# Experiment 438 — the option headers carry their **component**, two stubs retire at once, and the stop becomes an instruction prefetch abort at an address that *is* an instruction

**Step:** the option headers stop being one file. `tools/gen_option_headers.py` now writes
`out/xnu_options/<CONFIG>/<component>/meta_features.h` beside the flat one, `build_xnu_arm_kernel.sh`
puts a component's own directory **ahead of** the flat header for every manifest file that belongs to
that component, and `tools/check_option_headers.py` — new, run by the build — makes both halves of
that structural.

## Why this step exists: `bsd_init`'s last stub was not a missing symbol

437's run ended at `stub_hit=kmstartup`, caller key `0x8003B238` = `bsd_init + 0x848`, the return
address of the call at `+0x844`. The walk said `bsd_init` makes 232 calls and that is the only one
still a stub, so the goal's minimum bar looked like one function away, and 437's own write-up named
it as *a compile error*: `bsd/kern/subr_prof.c` uses the macro `STATIC`, which no header it includes
defines. That is true of `subr_prof.c` — and it is **not** why the file did not compile.

The measured cause is two files away and has nothing to do with `STATIC`:

    bsd/kern/bsd_init.c:852           #ifdef GPROF
                                      kmstartup();
                                      #endif
    libkern/conf/files:5              OPTIONS/gprof          optional gprof

`gprof` is declared by **libkern**. `tools/gen_option_headers.py` turned that line into
`gprof.h` — one line, `#define GPROF 0` — and then listed it in the *single* `meta_features.h` every
translation unit force-includes. So every BSD file saw `GPROF`. And `#define GPROF 0` is not "off" to
`#ifdef`; it is **defined**. A BSD file called a profiler entry point, and the only thing that defines
it is `bsd/kern/subr_prof.c`, whose body is inside the same `#ifdef` and which does not compile. The
two facts are one fact: **the body must be dead in every configuration that ships, so the call must be
dead too** — and it was not, because this build had aggregated all eight components' options into one
header.

## Why the aggregation was wrong, in Apple's own build

Not a judgement call, and not a fudge invented here:

- `SETUP/config/mkheaders.c:71-79` — `headers()` walks **the file table it is given**. Each component
  runs the config tool over **its own** `conf/files`, from its own object directory
  (`makedefs/MakeInc.dir`), so the OPTIONS headers land in the object directory of the `conf/files`
  that declared them.
- `makedefs/MakeInc.def:466` — `INCFLAGS_LOCAL = -I.`, and that `.` is the component's own object
  directory, with `-I$(OBJROOT)/EXPORT_HDRS/$(COMPONENT)` beside it.
- `main.c:206-216` — `path()` is `object_directory/build_directory/file`, a flat directory, and the
  include sites agree (`osfmk/ipc/ipc_hash.h:128` says `#include <mach_ipc_debug.h>`).
- Each of the seven kernel components' `conf/Makefile.template` has `CFLAGS+= -include
  meta_features.h` and `SFLAGS+= -include meta_features.h` — the one the `-I.` above resolves to.

So **which options a translation unit sees is a property of the component, not of the kernel.** The
comment in `scan_options()` asserted the opposite — *"first writer wins, and the value is the same
whichever component declared it"* — and the **value** is the same; the **membership** is not. That
comment is now corrected rather than left to be believed, and the correction is
`mi4-a-claim-in-a-comment-is-not-a-check` with a second instance.

## Membership alone is not a rule either: the two cross-component reads, measured

Narrowing the slices is *not* safe on its own, and the check is what says so. Scanning every manifest
source and every component header for `#ifdef`/`#ifndef`/`defined()` reads of an option macro the
reading component does not declare gives **exactly two** in the whole tree:

| macro | declared by | read by | decision | why |
|---|---|---|---|---|
| `GPROF` | libkern | `bsd/kern/{bsd_init,kern_clock,subr_prof,subr_xxx}.c`, `bsd/sys/gmon.h` | **not shared** | every one of those blocks is dead when the option is off; `nm` over all 695 objects finds no reference to `mcount`, `_gmonparam` or `cfreemem`; and it is the only reading under which Apple's own `subr_prof.c` compiles |
| `CONFIG_MACF` | bsd **and** security | osfmk | **shared** | `osfmk/kern/task.h:241` guards a `struct task` field with `#ifdef CONFIG_MACF` while `osfmk/kern/task.c` guards the same parameter with `#if CONFIG_MACF` — with the macro invisible to osfmk, `struct task` would have **two layouts in one kernel**, an ABI mismatch no compiler can see |

`MACH_ASSERT` is the reason the check keys on *declarers* and not on *whichever component declared it
first*: it is declared by osfmk, bsd **and** iokit, so a bsd file reading it is reading its own. The
first version of the check used a macro → component map and flagged
`bsd/kern/kern_credential.c` for a read that is entirely its own.

## What the step is, structurally

- `tools/gen_option_headers.py` — `scan_options_by_component()`; each
  `<component>/meta_features.h` written with that component's own headers first, then the shared ones
  (marked `/* shared: read by a component that does not declare it */`); the flat header kept and
  still the union, for the four out-of-manifest translation units, which belong to no component.
- `tools/check_option_headers.py` — both halves, no compiler needed, failing in a second instead of
  after a 695-file build:
  **membership** (each slice equals `conf/files`' order plus the shared headers, and the flat file is
  the union) and **reads** (every cross-component read must be in `SHARED` with a reason or in
  `KNOWN_READS` with the reason it is inert — *and the table is checked in both directions*, so an
  entry with nothing reading it fails rather than rotting into a list of things that used to be true).
- `build_xnu_arm_kernel.sh` — `OPTION_FIRST_PLACEHOLDER` resolved per file to
  `-I$OPTION_HEADERS/$SRC_COMPONENT` when that directory exists, the flat one otherwise; a **refusal**
  when a component that declares options has no slice; and the check invocation.
  The refusal's predicate is *"declares options"*, read from the same `conf/files` the generator
  reads, and the first version of it — *"is in `COMPONENT_LIST`"* — fired on `san`. The measurement
  that settles it: `san` is the one component with a `conf/files` and no `OPTIONS/` line, **and** the
  only one of the eight whose `Makefile.template` has no `-include meta_features.h`. It was right to
  fire, for the wrong reason. Apple's build gives `san` no option view at all.
- `tools/check_option_headers.py` also carries the lesson from its own first run: `includes_of` had to
  become a regex rather than `line.endswith(">")`, because the shared lines carry a trailing comment —
  and the version that assumed nothing follows the `>` reported five components *missing the header it
  had just written*. **A parser that assumes nothing follows the `>` is a claim about the writer.**

## Prediction, written before the build, and before the run

- the check passes for RELEASE and STAGE90_BOOT, and fails when a slice is made to disagree;
- pool **695 objects → 696, 3 failures → 2**, `bsd_kern_subr_prof.o` the new one;
- `nm -u bsd_kern_bsd_init.o` no longer lists `kmstartup`; `bsd_kern_subr_xxx.o` loses `cfreemem`;
- **every other object byte-identical**, and the reason is a property the whole design rests on:
  membership can only move `#ifdef`/`#ifndef`/`defined()`, because `#if X` reads 0 either way — and
  those are the two reads above, one inert and one held by the shared list;
- entry image stubs **44 → 43**, `.text` −0x24;
- the run: `bsd_init` runs past `+0x844` and **returns**, and the stop moves into
  `kernel_bootstrap_thread`.

Falsifiers: a stop that is still `kmstartup`; a new `data abort` naming a profiler-era symbol;
`bsd_init` still not returning with the stop inside its own body; a `struct task` sized differently in
two objects (a fault on a task pointer, not a compile error).

## Measured, 2026-09-19

**No falsifier fired, and *three* predictions were wrong.** They are worth more than the hits.

**The check and the pool, exactly as predicted.** Both configurations pass:

    ok: RELEASE option headers carry their component (osfmk 30, bsd 70, libkern 9, iokit 7,
        pexpert 1, libsa 1, security 6; + 1 shared; flat 91 = the union;
        inert cross-component read(s): bsd/GPROF)

**695 → 696 objects, 3 → 2 failures**, `bsd_kern_subr_prof.o` new. And the byte comparison over all
695 before-hashes is the part that carries the design argument:

    exactly 2 objects changed:  bsd_kern_bsd_init.o   bsd_kern_subr_xxx.o
    the other 693 are byte-identical
    nm -u bsd_kern_bsd_init.o   U kmstartup   -> nothing
    nm -u bsd_kern_subr_xxx.o   U cfreemem    -> nothing

`bsd_kern_kern_clock.o` — whose `#ifdef GPROF` wraps an `#include <sys/gmon.h>` and nothing else, the
one the prediction called identical — **is identical**. Two changed files out of 695 is what
*"membership only moves `#ifdef`"* means when it is measured rather than argued.

**Miss (a): the stub count went 44 → 42, not 44 → 43, because `subr_prof.c` is not the profiler.**
The prediction said the new object "defines nothing — `kmstartup`, `mcount`, `sysctl_doprof` and
`_gmonparam` all vanish with it". The file says otherwise: `#ifdef GPROF` is at 83 and
`#endif /* GPROF */` at 339, and **`addupc_task` at line 370 is outside it** (so are `PROFILE_LOCK`,
`PROFILE_UNLOCK` and `PC_TO_INDEX` at 341-351). `resourcevar.h:124` expands `addupc_task` behind
nothing but a runtime flag test, and `kern_clock.c:379` and `kern_sig.c:3346` call it — so it *was* a
stub, and the object that retires it is the one this step made compile:

    nm out/stage90/xnu_arm_entry.elf   T addupc_task 0x8028bb7c

**Two stubs retired from two opposite directions**: `kmstartup` because its *caller* stopped existing,
`addupc_task` because its *definer* started existing. The reading to keep: **a file's name is not its
`#ifdef` structure, and "an object defines nothing" is a claim about a line range that was never
checked.**

**Miss (b): `.text` grew `0x4B4AC0 → 0x4B4C00`, +0x140, not −0x24 — and changed nothing else at
all.** (The hex endpoints in this heading and the `+0x40` in the block below are corrected here, by
439: the build log's own `text size 4934656` is `0x4B4C00`, and `4934656 − 4934336 = 320 = 0x140`.
`0x4B4B00` and `+0x40` were this doc's transcription of it, and the *conclusion* — that `.text`
grew while everything else stayed — is unaffected. See `mi4-measurement-defects`.)

    text size    4934656 (.text)      <- was 4934336: +0x140 = 0x4B4C00
    image bytes  5141584              <- unmoved
    .data        0x804B8000 (0x2E360) <- unmoved, address and size
    .sysctl_set  0x804E6360 (0xFD8)   <- unmoved
    .init_array  0x804E7338 (0x118)   <- unmoved
    bss          0x804E7480 .. 0x805388F8 (332920)   <- unmoved
    copied image ends 48 bytes below __bss_start     <- unmoved
    entry bin    sha256 2e7354519b9ecf81573be3729a068de3cf1afb0faa5df3dc9e41d9eb1d99250a

The real `addupc_task` body more than pays for the two stub bodies and their name slots; the whole
growth sat in `.text`'s own alignment slack; and the entry bin's hash moved anyway. **Equal size is
not equal layout** — `mi4-linker-fill-term` from the other side: Δ`.text` was +0x40 and Δimage was 0.
The entry link's closure went **423 → 424 objects**, and `copyin`/`copyout` — the new object's only
unresolvable references — cost nothing, because the image already defines both (`T copyin
0x8000d3e4`, `T copyout 0x8000d4cc`).

**Miss (c), and it is the run: the stop is not in `kernel_bootstrap_thread` at all.** The result line
has **no `stub_hit=` field**, for the first time since the instrument has had one, and a fault block
that 436 and 437 never printed:

    real XNU entry: exception: prefetch abort
    xnu_entry_prefetch_abort_ifar = 0xE52DE004
    xnu_entry_prefetch_abort_ifsr = 0x00000005        translation fault, section
    xnu_entry_prefetch_abort_lr   = 0xE52DE008        = the faulting PC + 4
    xnu_entry_prefetch_abort_spsr = 0xA0000013        SVC mode
    xnu_entry_prefetch_abort_ttbr0/1 = 0x8070404A      ttbcr = 1, sctlr = 0x30C5787D
    xnu_entry_checks=5  xnu_entry_failures=0  xnu_entry_abort_entries=0
    xnu_entry_kv_written=0x346 (838)                  <- was 0x226 (550): +288 = 9 records
    xnu_entry_kv_dropped=0x00000000

**The records are the interesting part of the instrument's behaviour**: 437 did not print zeros for
these keys, it printed *nothing*, because `fleh_prefabt` writes its nine records into the buffer only
when it runs. `0x346 − 0x226 = 0x120 = 9 × 32` is exactly those nine, and nothing else in the buffer
moved — so **no stub was hit at all on this run**, which is what "no `stub_hit` field" means.

`0xE52DE004` is the ARM encoding of `push {lr}`. Both `IFAR` and `LR_abt` agree on it, and they are
independent registers, so the processor really was fetching from that address. It appears in the image
as a *code* word in exactly 43 places — and the measurement that identifies them is a search for the
little-endian word, not a reading:

    42 of the 43 are the `push {lr}` inside the 42 stub bodies
    the 43rd is the first instruction of `inv_shift_rows` (a real function, and a side note)

The stub bodies answer the rest. Every stub is 0x18 bytes of

    movw r0, #<name low>        word 0
    push {lr}                   word 1   = 0xE52DE004
    mov  r1, lr                 word 2
    movt r0, #<name high>       word 3
    pop  {lr}                   word 4
    b    entry_stub_hit         word 5

and they are laid out **in the order of `out/stage90/xnu_arm_entry_undef.txt`** — a second,
independent measurement of the stub count: 42 hits, spaced exactly 0x18, from the symbol for entry 1
(`bpf_attach` at 0x80444bb4) to entry 42 (`_Z35upl_get_internal_vectorupl_pagelistP3upl` at
0x80444f8c), with `pseudo_inits` — entry **28** of the alphabetical list — landing at 0x80444e3c.

**And that is the whole stop.** `bsd_init.c:861` is `bsd_autoconf();`, inside `bsd_init` itself, and
`bsd_autoconf` is this:

    kern_return_t bsd_autoconf(void)              bsd/kern/bsd_init.c:1084
    {
        kprintf("bsd_autoconf: calling kminit\n");
        kminit();
        {   struct pseudo_init *pi;
            for (pi = pseudo_inits; pi->ps_func; pi++)
                (*pi->ps_func) (pi->ps_count);
        }
        return( IOKitBSDInit() );
    }

with `struct pseudo_init { int ps_count; int (*ps_func)(int count); }` — **eight bytes, and
`bsd/dev/busvar.h:46` declares it `extern struct pseudo_init pseudo_inits[]`.** The linked image's
`bsd_autoconf` is exactly that loop:

    8003b4e0:  movw r4, #0x4e3c ; movt r4, #0x8044    r4 = 0x80444e3c = &pseudo_inits
    8003b4e8:  ldr  r1, [r4, #4]                       ps_func = word[1]
    8003b4f0:  beq  8003b50c                           <- the 0 test that ends the loop
    8003b4f4:  ldr  r0, [r4]                           ps_count = word[0]
    8003b4f8:  blx  r1                                 <- the indirect call
    8003b4fc:  ldr  r1, [r4, #12] ; add r4, r4, #8     next entry

and at `pseudo_inits`:

    80444e3c <pseudo_inits>:
    80444e3c:  e3020928  movw r0, #10536              -> ps_count = 0xE3020928
    80444e40:  e52de004  push {lr}                    -> ps_func  = 0xE52DE004   <- the fault address

**`pseudo_inits` is a *function* stub where XNU's own header says the symbol is an *array of
structs*.** The `0` that terminates the walk is the second word of the array's first entry, and our
stand-in put its own prologue there. So `pi->ps_func` is non-NULL — it is `push {lr}` — the `blx r1`
at `bsd_autoconf+0x28` jumps to it, and the processor faults fetching 0xE52DE004. The tool and the
device agree about the same instruction: `tools/xnu_entry_callwalk.py --root bsd_autoconf` says
*"walk from `bsd_autoconf` reached no stub on the straight-line path … indirect calls the walk could
not follow: `bsd_autoconf+0x28: blx r1`"* — **the walk's blindness and the device's fault are the same
`blx`,** separated by one wrong word.

So the third face of this instrument's symptom, and the first of its kind measured on hardware:

> **The stop is not a missing symbol, not an invented zero and not a boot-arg string: it is a
> stand-in of the wrong *kind*.** `pseudo_inits` is in the 42-stub list and the undefined list shows
> only *names* — never kinds — so nothing host-side could have said the symbol was declared an array.
> The one-line `nm` view (`T pseudo_inits`) is wrong only relative to `busvar.h:46`, which this build
> does not read at that point.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`preflight_boot_check.sh --allow-xnu-entry`,
then `run_and_capture.sh --allow-xnu-entry`, which re-runs the gate and refuses on failure); nothing
flashed, nothing written to storage. 25 × `persistent_write_attempted=0x00000000`, 87 ×
`failure_mask=0x00000000`, `abort_entries=0`, `kv_dropped=0`, **no `panic:` line**; the only
`exception:` line is the entry instrument's own report of the abort it caught. Hardware watchdog the
only net across the jump (`enabled=1`, `counter_running=1`, `bite_truncated=0x00000000`, `timeout_s=25`,
`hz=0x7ffd`), dead-man disarmed before it (`disarm_isenabler0 0x000C7FFF → 0x00007FFF`). Payload
7970 KB accepted. **The device returned to Android on its own and was confirmed there** (`ro.product.device`
cancro, `ro.build.version.release` 10). 302532 bytes / 3994 lines, ending `No errors detected`.

Per-run logs stay apart: `/tmp/run425_kmsg.txt` … `/tmp/run438_kmsg.txt`.

## Where the frontier is now: `bsd_autoconf`'s `pseudo_inits` walk, and it is a *kind*, not a name

`bsd_init` still has not returned — the stop is in `bsd_autoconf`, which `bsd_init.c:861` calls from
inside `bsd_init`. The step that follows is the one this measurement names:

**Supply `pseudo_inits` as the array XNU's header declares, not as a function.** `mkioconf.c:79-100`
says exactly what the array contains — one `{count, func}` per `PSEUDO_DEVICE` entry in the
configuration's device table that has a `d_init`, terminated by `{0, 0}` — so the content is
*generated*, from the same device table `tools/xnu_config/device_table.py` already produces, and the
terminator is what makes the walk end. The check that has to stop the build is a *kind* check: a
symbol whose declaration is an array must not be stubbed as a function, and the undefined-symbol list
cannot answer that — the declared types have to come from the tree's own headers.

**Still owed and unchanged: the timer** (`ml_init_timebase` plus an MSM8974 `tbd_ops_t` over the GPT
at `0xf9020000`, and 405's `IOCPUInterruptController`). Nothing on the path from here to `vm_pageout`
has yet taken a deadline.
