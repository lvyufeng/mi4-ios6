# Experiment 489 — the silence was the success

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `tools/check_os_entry.py` (new), `stages/stage90/xnu_arm_boot/build_entry.sh`

**Result: the boot reached the OS, and the log that was read as a stop is the proof.** The console's
last line — `load_init_program: attempting to load /sbin/launchd` and then nothing — was read for two
steps as *"`execve` never returned, process 1 never started"*, and 487's next step was aimed at the
exec. It is the opposite: `load_init_program` prints on every failure and on nothing else, and its last
statement is a `panic`, so **silence after the last name in `init_programs[]` is the success reading**.
Four independent readings in 488's own log say the same thing, and a fifth ties them to one process; the
two user-mode faults the run then takes are the two the RAM disk's own bytes predict.

No device run was made for this step, and that is deliberate rather than an omission: **the check
changes no image byte.** The build re-run reproduces `xnu_arm_entry.bin` at sha256
`759c49f1b3096991fbcb9bba1cf3485354fab5fc8e3b99204790b677dc1df664` and the `.elf` at
`959b0a6932a75f1675decb1614fc4f2ed5e6d7b2a221a1a564d87850177e785d` — the exact artefacts 488's two runs
were of — so those runs' logs *are* this image's logs, and the readings below are quoted from them
rather than re-measured. A fresh boot would produce the same log at the cost of a device risk, which is
the one thing this project does not spend without a reason.

## The inversion, and what it cost

487's doc, 488's doc and the phase record all read the console's last line as the place the boot stopped
— 488's doc says in so many words "that is Apple's own control flow saying the exec is still running",
and the phase record says process 1 never started. The source says otherwise.

    bsd/kern/kern_exec.c
    5119  load_init_program(proc_t p)
    5135      if (PE_parse_boot_argn("launchdsuffix", ...)) {     <- the block that can print outside the loop
    5141          printf("load_init_program: attempting to load /sbin/launchd\n");      release suffix
    5142          error = load_init_program_at_path(...)                                 attempt 1
    5143          if (!error)
    5144              return;                                                            <- success: silent
    5146          panic("Process 1 exec of launchd.release failed, errno %d", error);
    5151          printf("load_init_program: attempting to load %s\n", launchd_path);
    5152          error = load_init_program_at_path(...)                                 attempt 2
    5153          if (!error) { return; } else { printf("... failed loading %s: errno %d", ...); }
    5164  for (i = 0; i < sizeof(init_programs)/sizeof(init_programs[0]); i++) {
    5165      printf("load_init_program: attempting to load %s\n", init_programs[i]);    attempt 3
    5166      error = load_init_program_at_path(...)
    5167      if (!error) { return; } else { printf("... failed loading %s: errno %d", ...); }
    5173  panic("Process 1 exec of %s failed, errno %d", ...);                           the loop cannot fall out

Three `attempting to load` printfs, two `failed loading` printfs, **no printf on any success path**, and
one `panic` after the loop. The rule is therefore not "the console has no failure line, so the exec is
still running" but its contrapositive, and the contrapositive is the useful one: *if the console has no
failure line and no panic after a `load` line, the exec of that name succeeded.*

Which line the loop printed is a second, separate question, and the log answers it by count.
`DEBUG` is off and `DEVELOPMENT` is on in this configuration, so `init_programs[]` is
`{"/usr/local/sbin/launchd.development", "/sbin/launchd"}` and the loop prints two `attempting to load`
lines with one `failed loading` between them. The log has exactly that, and it has **no**
`launchdsuffix`-block line — which the block's own guard says it would have printed had
`PE_parse_boot_argn("launchdsuffix", ...)` found the argument. Three source printfs, two in the log: the
log's lines came from the loop.

## The four readings that follow, from 488's own log

The console's three lines are not the evidence; what the image recorded after them is. 488's second run
(`/tmp/stage90-488-run2.log`, sha256 `e0a332ed5a7983e1354a48abd4592fa6848d6c4ec98a7b6de4b66f9d216aff90`):

| line | reading | what it rules out |
| --- | --- | --- |
| 6278 | `xnu_live_lmf_ret=0x00000000`, with **no `osr_*` record anywhere** | `load_machfile` — the first thing `execve` does — ran its whole real body and returned success |
| 5582-5720 | `xnu_live_tail_seq` 1…5 with `tail_idx` 0…4, the fifth written by `__wrap_vm_pageout` | the boot thread ran all five tail calls; `vm_pageout` is only reachable after `bsd_init()` returned |
| 6300-6302 | `xnu_live_getpid_value=0x00000001`, `error=0`, `caller=0x80280248` | something in *user mode* asked the running kernel for its pid and the kernel answered `1` |
| 6306-6317 | `xnu_live_mmap_value=0x00102000` with all eight argument words | the mmap was serviced as a real demand operation, not reflected |

The first two are independent of the exec's outcome and the last two are only possible if it succeeded:
`getpid` returning `1` means a process *is* pid 1, and a user-mode `svc` reaching the kernel means the
kernel returned to user mode through the real path rather than through a stub.

That last point is what 488 was about. 487's run stopped at `xnu_live_stub_hit_caller = 0x80017f94` =
`return_to_user_now + 4`, because `locore.o` had been assembled without the configuration's options; with
that fixed the same jump returns to user mode properly, and the first thing it does is fault where the
fixture says it will. So 488 and 489 are one story read from two ends: 488 fixed the return to user
mode, and 489 walks past the silence it left in the console.

## The two fault sites, predicted by the file and named by the run

The RAM disk in this image is a 27-instruction Mach-O built by `entry_ramdisk.s`, and `check_os_entry.py`
reads it out of the linked image byte by byte: header `__TEXT.vmaddr = 0x1000`, `sizeofcmds = 0xC4`, so
the entry is `0x1000 + 28 + 0xC4 = 0x10E0` — and the RAM disk's own `LC_UNIXTHREAD` pc says the same
independently. From there:

    entry+0x38   0xe5903000   ldr r3, [r0]    the read of the page mmap just returned
    entry+0x44   0xe5800000   str r0, [r0]    the write that follows it, through the same register

which are VA **0x1118** and **0x1124**. The run takes exactly those two aborts, in that order, and the
second is a store:

| `sleh_seq` | pc (line) | fsr | far | `sleh_user` | thread (line) |
| --- | --- | --- | --- | --- | --- |
| 5 (6318) | **`0x00001118`** (6324) | `0x007` (read) | `0x00102000` | 1 | `0xc0464690` (6322) |
| 6 (6332) | **`0x00001124`** (6338) | `0x80f` (write) | `0x00102000` | 1 | `0xc0464690` (6336) |

Only the first eight faults publish their pc/fsr/far (`SLEH_LIVE_MAX`), and these are the fifth and
sixth; the run's count reaches `sleh_seen = 0x20` and its summary `sleh_storm = 9`.

Three things here that the claim does not assert and the run nevertheless gives. The `far` is
`0x00102000` — **the page `mmap` returned** (line 6306), so the fault is on the page the fixture asked
for rather than on a neighbouring one. `sleh_user = 1` in both, so these are the user-mode aborts and not
kernel ones with a coincidentally similar address. And the `thr` in both records is `0xc0464690`, which
is `xnu_live_mmap_thread` (line 6307) — **the faulting thread is the thread whose `mmap` returned that
page**, which is the fifth reading and the one that makes the other four one process rather than four
coincidences. The `getpid` and `mmap` records share `caller = 0x80280248`, the fixture's own instruction
after its `svc`, for the same reason.

## The check

`tools/check_os_entry.py` states the reading over the *file*, so a log is never interpreted against source
the image was not built from. Five claims, 30 mutations, all refused:

1. **The chain.** `bsd_ast` runs `bsdinit_task()` under a once-only `bsd_init_done`
   (`kern_sig.c:3443-3446`); `load_init_program(p)` is the last call in its body; `/sbin/launchd` is in
   `init_programs[]` and is not its first entry; `load_init_program_at_path` ends
   `return execve(...)` (`:5076`), so the errno a failure line prints is execve's own.
2. **The silence rule.** Stated as three separable properties rather than one count: (a) the function's
   whole output is three `attempting to load` and two `failed loading` printfs out of five `printf(`
   calls; (b) per call site — each of the three `load_init_program_at_path` calls is followed by an
   `if (!error)`, and after that arm there is a `failed loading` line or a `panic`; (c) no `if (!error)`
   arm prints, taken by *shape* because the file writes both `if (!error)\n\t\treturn;` and
   `if (!error) {\n\t\t\treturn;\n\t\t}`; (d) what follows the loop is exactly the one `panic` and
   nothing that prints; (e) the block outside the loop still reads the `launchdsuffix` boot argument.
3. **The order.** `bsd_init()` precedes all five tail calls in `kernel_bootstrap_thread`, the build
   `--wrap`s all five, the wrapper's index is `4u` against `ENTRY_TAIL_CALLS 5u`, and both censuses are
   called from `__wrap_vm_pageout`'s body **between the tail record and `__real_vm_pageout()`, which is
   that body's last statement** — `vm_pageout` is followed by Apple's own NOTREACHED, so a census after
   it would never run and a record written after it would say the wrapper returned.
4. **The two fault sites**, read out of the RAM disk's own bytes as one load and one store through `r0`,
   with the entry point re-derived from the header's own fields.
5. **The readings are published by this image**: `__wrap_load_machfile` passes the real call's return,
   `--wrap=load_machfile` is in the build, and `entry_stubs.c` writes the three `getpid` keys.

Claims 1-3 and 5 are the *instructions* this reading rests on; claim 4 is the only one that is about
data, and it is the one the hardware confirms to the address.

### Three defects found in this check, all of the classes this project keeps meeting

The first version failed its own baseline on three counts, and each was a claim that was wrong about the
file rather than a file that was wrong:

- **The count was 3, not 2.** `load_init_program` has three `attempting to load` printfs, because the
  `launchdsuffix` block has one in its release-suffix branch and one in its suffixed branch. The claim
  was rewritten to be per-site — which is what it should have been from the start, since "every attempt
  reports its failure" is the property and "there are two printfs" was only ever a proxy for it.
- **The `panic(` count was 2, not 1**, for the same reason: the release-suffix branch panics as well.
  Fixed by scoping the count to what follows the loop, which is the fact that matters.
- **A declaration was being counted as a call site.** `extern void *entry_probe_dt_children(void);`
  matches the same shape a call does — a name, parentheses, a semicolon — and the check reported a
  second definition of the census where there was only a type. Declarations are now removed by the
  keyword before counting, the same way `strip_comments` removes comments by their syntax.

And one selftest defect, which is 488's class exactly. Three mutations were **accepted**, and two of them
for a reason worth recording: they edited `facts["boot_tail_body"]` — a *derived* field — while the
selftest re-derives every body from the source text after mutating. The mutation was therefore thrown
away before the claims saw it, and its acceptance was a fact about the harness, not about the check.
Every mutation now edits source text and re-derives, with the re-derivation written once at the end so
it cannot be forgotten. The same is now true of `load_init_program_body`: the four mutations that used
to edit it directly edit `kern_exec.c` and let the reader produce the body.

The third accepted mutation was `a_failure_line_moves_to_the_other_arm`, and it was a real hole. Moving a
`failed loading` line from one attempt to another — keeping the body's counts at 3 and 2 and leaving the
post-loop panic intact — was invisible to a claim written over counts. The per-call-site rule (2b) exists
because of it.

## What is owed

- **The console is now read, not interpreted.** 475/480's "process 1 did X" readings and this step's
  readings come from different channels; a single place that states the boot's progress in terms of the
  console text *and* the instrument records would make the next step's claim shorter and would have made
  this inversion impossible to hold for two steps.
- **Unchanged and still owed:** 487's third census of the platform expert instance `0xc0591740`'s own
  service children; the release as a reading; whether the copies that build process 1's arguments are
  serviced demand faults, via **`vm_fault`** (`osfmk/arm/trap.c:443` user, `:563` kernel) rather than
  `arm_fast_fault`; and 488's deferred flag-list derivation.
- **New, from this step's fault records:** after the two user aborts the run takes two more published
  faults, both at `Lcopyin_wordwise_loop` with `far = 0`, `lr = 0x800a8d34` =
  `telemetry_take_sample+0x158` (whose instruction before the call is `ldr r0, [r0]` then
  `bl copyin`, 176 bytes to a stack buffer), on thread `0xc0464690` — the fixture's own thread.
  **Process 1 is alive while this happens**: that thread's `getpid_count`
  keeps doubling to `0x800000` after the second abort, `sleh_seen` reaches `0x20` and `sleh_storm = 9`.
  And it is not new. 486's run D has the **same eight published records in the same order and on the same
  two threads**: the same four kernel copies, the same two user aborts with the same two addresses and
  the same `far`, and the same recurring `Lcopyin_wordwise_loop` fault with the same `lr` `0x800a8d34`
  and `far = 0`, reaching `sleh_seen = 0x20` / `sleh_storm = 9` as well. Only the four copy-loop pcs
  differ, and they differ because the rebuild moved them (`Lcopyin_wordwise_loop` and the copy loops sit
  0x560-0x570 higher in this image) — every `lr` in the two runs is the same address.

  **Corrected by 490, which is the step that measured it.** The sentence that stood here — "`copyin`
  faults rather than returning `EFAULT`, which is a property of this image's `copyin`/`copyout` on a user
  pointer that is not mapped … so the frontier this step opens is the telemetry sampling path's
  `copyin`" — is the inverse of what the source and the run say. `copyin` arms `thread->recover` with
  `copyio_error` before it touches user memory (`COPYIO_SET_RECOVER`, `machine_routines_asm.s:542-550`)
  and `sleh_abort` re-points `pc` at that address when the page cannot be paged in (`trap.c:456-461`), so
  `far = 0` inside `Lcopyin_wordwise_loop` is a call the kernel answered, not a stop: 490's run reports
  28 of its 32 aborts armed and 26 of those redirected, and the two `far = 0` records are two of the 26.
  The `0x8000` in the sentence above was also a slip for the value the log actually ends on, `0x800000`.
  Nothing else in this document depends on either: the four readings the exec succeeded are unaffected,
  and the `0x560-0x570` and same-`lr` observations are what 490's own five-build table re-measures.

## The build

Step 3 (`build_entry.sh`, `STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1`) ends green with
`xnu_entry_489` and `--selftest: all 30 mutations were refused` beside 488's `--selftest: all 21
mutations were refused`. The entry image and the payload are unchanged from 488 —
`xnu_arm_entry.bin` `759c49f1b3096991fbcb9bba1cf3485354fab5fc8e3b99204790b677dc1df664`,
`.elf` `959b0a6932a75f1675decb1614fc4f2ed5e6d7b2a221a1a564d87850177e785d`, payload
`4b80513fcf2c97d6272e7af46abd7033006af6060e1520f9cd755163dc468e41` — so this step's only artefact is a
check and a comment, and nothing about the device's behaviour has moved.
