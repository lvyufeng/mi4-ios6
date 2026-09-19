# Experiment 433 — one slot gets a body, and the frontier leaves the table it just arrived at — the step that links nothing and moves the frontier further than any link could

**Step:** **no object linked.** `LINK_OBJS` is unchanged from 432 — the entry image is rebuilt from the
same 695-object pool and the same `stage90_pthread_functions.o`, with one edit in the *source* of that
object (`stages/stage90/xnu_supply/stage90_pthread_functions.c`): the table's `pthread_init` slot stops
pointing at a stand-in and points at a function with a body.

This is the first step in the walk whose whole content is a *value* rather than a *name*, and the
second in a row whose effect on the link's accounting is exactly nothing.

**Effect:** **0 resolved / 0 added.** The three counts stand at **768 undefined, 616 function, 152
storage**, exactly as 431, 432 and now 433 leave them:

    stage90_pthread_functions.o   83 definitions, 4 references
    resolved (0: 0 function, 0 storage)   added (0: 0 function, 0 storage)
    of the 4 references, 4 are already satisfied
    predicted counts: 768 -> 768 undefined, 616 -> 616 function, 152 -> 152 storage

The two references 433 adds to 432's pair are `entry_kv` — **the entry image's own record writer**,
defined by `entry_stubs.c` beside `entry_stub_hit` — and `pthread_functions`, the `.bss` pointer
`bsd_kern_pthread_shims.o` defines and this object now reads instead of merely sitting behind.

## Why 432's own write-up was wrong about what 433 would be

432's closing section ends with a statement about its successor:

> `nwk_wq_init` … is still a plain missing symbol, and **433 is shaped like every step from 425 to
> 431: link the object that defines it.**

**That is wrong, and the image was the thing that said so.** 432's run stopped at
`stage90_pthread_functions.pthread_init`, and `bsd_init` reaches that call four `bl`s *before* it
reaches `bl <nwk_wq_init>`:

    0x8003b1e4  bl 801ecb38 <pthread_init>      <- 432's stop, and still a stand-in
    0x8003b1e8  bl 801ea474 <pshm_cache_init>
    0x8003b1ec  bl 801eb960 <psem_cache_init>
    0x8003b1f0  bl 801a6958 <time_zone_slock_init>
    0x8003b1f4  bl 801ed620 <select_waitq_init>
    0x8003b1f8  bl 802062ac <nwk_wq_init>       <- 425's prediction, four calls later

Linking the definer of `nwk_wq_init` changes what sits at `0x802062AC`; it does nothing to the slot at
`0x80231228 + 4`, and the run stops there every time. **So no object appended to `LINK_OBJS` could have
moved this boot one instruction further**, which is the same property 432 discovered from the other
side: 432 moved the frontier by writing a value, and 433 is the step where that value's *first
consumer* has to be given something to do.

## What the body is, and what it deliberately is not

    static void stage90_pthread_functions_init(void)
    {
        entry_kv("xnu_entry_stage90_pthread_functions_ptr", (uint32_t)(uintptr_t)pthread_functions);

        if (pthread_functions != &stage90_pthread_functions) {
            entry_stub_hit("stage90_pthread_functions.not_registered", 0u);
        }
    }

**It records, and it stops if the premise did not hold. It does not return silently.** That keeps the
rule 432's header states — "a silent no-op is the wrong-value hazard", a kernel told that work happened
when it did not — while breaking the letter of it, because what the kernel asked for here is not state
but a *notification*: the whole kernel-side contract of the call is `pthread_shims.c:277`,
`pthread_functions->pthread_init();` as a statement, `void`, with nothing consuming a value from it.
`pthread.kext`'s own initializer builds the kext's hash tables and workqueue state; there is no kext
here, so "the pthread subsystem is initialised" has no content beyond the call returning, and the
honest way to say so is a body that writes down the one number that makes the claim checkable.

**It does not dereference the pointer.** A NULL or wrong `pthread_functions` must be a *stop that names
itself*, not a fault: `entry_stub_hit` produces `stub_hit=stage90_pthread_functions.not_registered`
with `abort_entries=0`, while a dereference would produce a `first_dfar` and a fault status — a report
that says something went wrong but not *what was expected to be there*. That distinction is the one
every stand-in in this table is built on, and it is why the body reads `[r4]` into a register and
compares rather than branching through it. The compiler agrees: the disassembly is `movw/movt r4,
<pthread_functions>`, `ldr r1, [r4]` (the value that gets recorded), `ldr r0, [r4]`, `cmp r0, r1`,
`popeq {r4, pc}`, and on the other path `mov r1, #0` then a *tail branch* to `entry_stub_hit`, so even
the failure stop is entered with `pthread_init`'s own caller in `lr` and reports the same call site.

## The prediction, and the measurement

**Prediction, written before the build: `stub_hit=nwk_wq_init`, caller key `0x8003B1FC`** — the return
address of the `bl <nwk_wq_init>` at `bsd_init + 0x808`. With the slot returning, the four calls
between it and `nwk_wq_init` run for the first time in this walk; all four are real since 423 and
`xnu_entry_callwalk.py` answers "no stub on the straight-line path" for each (`time_zone_slock_init`
carries one guarded site, `zalloc_internal+0x60c -> trace_backtrace`, which is a branch that must not
be taken). The stop is therefore the next stub on the line, which is the name `--root bsd_init` has
answered since 425.

**Measured on hardware: exactly that, and the value it records is the second thing the prediction
needed.**

    line 3933:  xnu_entry_kv_written=0x00000090          <- 432: 0x77
    line 3935:  xnu_entry_kv_dropped=0x00000000
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973:  xnu_entry_stage90_pthread_functions_ptr=0x80231228
    line 3974:  stub_hit=nwk_wq_init
    line 3975:  xnu_entry_stub_caller=0x8003b1fc
    line 3979:  No errors detected

`0x80231228` is the table's own linked address — `arm-none-eabi-nm` says `80231228 t
stage90_pthread_functions` — so the pointer the kernel holds is the table this image registered, and
the `not_registered` stop did not fire. **Two independent routes to the same fact**: the image's own
read of the kernel's `.bss` pointer, and the host's symbol table for the image that was linked.

`caller - 4 = 0x8003B1F8 = bsd_init + 0x808` is the `bl <nwk_wq_init>` in the disassembly above, read
off the linked image rather than inferred; `bsd_init` is at `0x8003A9F0` and did not move, because the
object this step changed is linked after it.

**And the positive evidence is the negative one, in the only form that can carry it.** The run contains
no `panic:`, no `exception:`, no `abort_entries`, and **no `stage90_pthread_functions.pthread_init`
line** — the two things that stopped 431 and 432 respectively are both absent, and the record the
kernel could only have written if the body ran (`xnu_entry_stage90_pthread_functions_ptr`) is present.

## The reading: the frontier moved further than the step's own content

**Five calls of `bsd_init` were retired between 432 and 433** — `bl <pthread_init>` (which in 432
entered the real `pthread_init` and stopped inside it) and the four calls the stop was hiding — and
the step itself did one thing: it changed what one word of one table points at. Stated as
the walk's own accounting would state it, this step **adds no name, resolves no name, and changes no
count**, and the tool that predicts every other step's effect answers `0 -> 768 / 616 / 152` for it
exactly as it does for a no-op.

**That is now the second consecutive step in this walk whose frontier moved by writing a value rather
than by retiring a name**, and 433 is the one that shows what the pattern costs: once a table the image
itself supplies is on the path, every one of its slots is a stop that only *editing this project's own
source* can move. 432's write-up called the table's remaining slots "a second frontier that no link can
retire"; the run above is the measurement that makes the first of them real.

## Layout

    entry text   0x236000 (2318336)     <- was 0x235FA0: +0x60
    entry image  0x2542B0 (2441904)     <- unmoved, second step running
    .init_array  0x8025421C .. 0x802542B0 (0x94)   <- unmoved; this object's word still at 0x802542A4
    bss          0x802542C0 .. 0x802961D8 (270104) <- start, end and size all unmoved
    layout       args 0x80298000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1482280 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 faed62be1633865e4b0a686fb7120077e5d5dab5f4eb527d694ee2d404e9ec61 (5462016 bytes)
    entry bin    2441904 bytes; sha256
                 6dbba896b5eb8c6305ddaec338dec175428c396a8c1b5ced9c5c84b27424fb1a

**The arithmetic is small enough to close exactly, and it is the whole of the step.** The object's
inputs moved by **+0x5E**: `.text` 0x2CC → **0x300** (+0x34 — a 0x44-byte body replacing a 0x10-byte
stand-in) and `.rodata.str1.1` 0x6F1 → **0x71B** (+0x2A — the record's key (39 bytes + NUL) and the
failure stop's name (41 + NUL) in, the retired `pthread_init` stand-in's name (38 + NUL) out), with
`.rodata`'s 0x1FC table unmoved. Measured `Δ.text` is **+0x60**, so the fill term is **2 bytes** — the
same quantity that made earlier steps miss by 0x12, 0x55, 0x11 and 0x7, here at its smallest so far.

**The string term is the one that does not close to the byte, and the reason is the section's kind.**
Raw, the three strings come to 40 bytes in, 42 in and 39 out (net +43) against a measured **+42**;
`.rodata.str1.1` is a *mergeable* section, so the object's own size for it is an upper bound the object
gives no sign of (the defect this project has a memory about), and the same 1-byte-per-string
`align4(len + 1)` slack that earlier steps had to measure rather than predict is what the difference
is. The section's measurement is sound; the model applied to it is the part that is approximate, and
this step states which is which rather than writing one number for both.

The object's code still begins at **0x80203D48**, the seventh consecutive landing on the contiguous
`.text` chain of `entry.ld:45`, and the table is at **0x80231228** with its first two words
`01000000 a43d2080` — `version = 1`, `pthread_init = 0x80203DA4` — read out of the linked image with
`objdump -s`. `.init_array` still holds `0x80203D48` at `0x802542A4`, in the same three-word run
(`stage90_pthread_functions_register`, `MSM8974PlatformExpert`, `last_kernel_constructor`) that 432
recorded: the constructor's order is unchanged even though its content is not.

**And the growth is visible in the image's own addresses, in exactly one direction.** Every symbol
after this object moved by **+0x34**: the generated stub region starts 0x34 later, so `nwk_wq_init` is
at `0x802062AC` where 432's image had it at `0x80206278`. Nothing before `0x80203D48` moved —
`bsd_init` is at `0x8003A9F0` in both images, which is what makes the caller key comparable across
the two runs at all.

## Which image ran: the marker that *did not* change, read correctly

    432: xnu_entry_checksum=0x9040e1ed
    433: xnu_entry_checksum=0x9040e1ed

**The same value across a link change is the tell this project has a defect about** (defect 130: a
marker stable across a link change cannot confirm which image ran). Here the stability is correct and
checkable: `stage90_xnu_entry_checksum` XORs the words of `struct stage90_xnu_entry_result` up to
`checksum` (`stage90.h:7048`), and every one of those words — `status`, `base`, `va`, `image_bytes`,
`bss_start`, `bss_end`, `args_pa`, `top_of_kernel_data`, `checks`, `failures` — is identical between
the two runs, because the object this step changed is linked *after* `bsd_init`, did not move the
image's start, its `.bss`, or its entry point, and changed no counter. A checksum that covers only
unchanged quantities is *supposed* to be unchanged.

**The evidence that a different image ran is elsewhere, and it is specific rather than aggregate:**
`xnu_entry_kv_written` went 0x77 → **0x90** (one more record, the body's), and
`xnu_entry_kv_words` — 661 words sampled from the image beginning at `entry_kv` — differs from 432's in
**43 of its 661 words, every one of them by exactly +0x34**, so every differing word is a
`movw`/`movt` pair naming a symbol beyond the inserted object and the shift is the object's own `.text`
growth. **And the 433 fingerprint matches the 433 ELF byte for byte at `0x80002024`, and does not match
432's** — the fingerprint and the linked image are the same bytes, which is the reading defect 130 asks
for and the one the checksum cannot give.

**The description of that fingerprint in 432's write-up is wrong** (recorded as defect 139): it called
`xnu_entry_kv_words` "the word-for-word dump of the vector table's first 2644 bytes", but
`entry_stubs.c:1116` is `entry_probe_dump_kv_words("xnu_entry_kv_words", (uint32_t)(uintptr_t)entry_kv,
661u)` — the region begins at `entry_kv` and covers the report writer's own code (`entry_kv`,
`entry_write_kv`, `entry_epilogue`, `entry_skip_pad`), while the vector trampolines
(`vec_tramp_0` at `0x80001F00`) lie *before* it and are not in the dump at all. The numbers were right
and the sentence around them was not.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure); nothing flashed, nothing written to storage.
25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`,
`checks=5` / `failures=0`, no `exception:` and no `panic:` line. The only net armed across the jump was
the hardware watchdog (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bite_truncated=0x00000000`), with the dead-man PPI disarmed before it
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). The two `data abort` lines in the log are the payload's
own deliberate probes, at lines 201 and 3443 — the same two lines, byte for byte, as 432's. Device
returned to Android on its own and was confirmed there (`ro.product.device` cancro,
`ro.build.version.release` 10). 301860 bytes / 3979 lines, ending `No errors detected`.

Per-run logs stay apart: `/tmp/run425_kmsg.txt` … `/tmp/run432_kmsg.txt`, `run433_kmsg.txt`.

## Where the frontier is now, and what 434 is

**`nwk_wq_init`** — `bsd_net_nwk_wq.o` (`bsd_init + 0x808`, key `0x8003B1FC`), a plain missing symbol
for the first time in six steps, and the first stop this walk has reached *through* a table it supplies
rather than around one. It is the shape 425–431 had: 434 links the object that defines it, and the
stop moves to `dlil_init` (`bsd_init + 0x80C`, key `0x8003B200`) unless `bsd_net_nwk_wq.o` reaches a
stub of its own first.

**The table's other 38 slots are still a second frontier**, unmoved by any link, each a stop until this
project implements it or gives it a body; nothing on the boot's path calls any of them yet, and
`pthread_shims.c`'s shims are the call sites that will.

**Still owed and unchanged: the timer.** `ml_init_timebase` with an MSM8974 `tbd_ops_t` over the GPT at
`0xf9020000`, plus 405's `IOCPUInterruptController`. Nothing on this step's path takes a deadline; the
mount machinery that follows `dlil_init` — `IOFindBSDRoot`, `vfs_mountroot`, `IOSecureBSDRoot` — is
where waiting unconditionally stops being possible.
