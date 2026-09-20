# Experiment 490 — the fault was the design

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `tools/check_fault_recovery.py` (new), `tools/check_saved_state_offsets.py`,
`stages/stage90/xnu_arm_boot/{entry_saved_state.h,entry_trace.c,entry_stubs.c,build_entry.sh}`

**Result: the two `Lcopyin_wordwise_loop` faults 489 named as the frontier are Apple's designed
`EFAULT` route, and the run now says so in three numbers that cannot be read off `far` and `pc` at
all.** Of this run's **32** data aborts, **28** carried a non-zero `thread->recover` — the recovery
address a copy arms before it touches user memory — and **26** of those 28 the handler actually spent,
re-pointing `pc` at `copyio_error` so the copy returned `EFAULT` instead of the faulting instruction
being retried. The two that were armed and *not* spent are the two `copyout`s on the exec path, whose
copies the console's own silence says succeeded. And the arithmetic leaves the run's unnamed tail with
no freedom at all: 28 armed − 4 in the named band = the whole of `seq 9..32`, and 26 redirected − 2 in
the same band = the whole of `seq 9..32` again — so **every one of the 24 aborts the run does not name
by site is a copy-path fault the kernel converted into an `EFAULT` return.**

Two device runs, both through the gate, both exit 0, device back on Android on its own. The second is
the shipped build: the check's last two claims were added *after* it, the rebuild reproduces
`xnu_arm_entry.bin` at sha256 `1bec72f6af4c5f3123c2ecf40eb7f4b44074136cce8222b9fd41fd275c70ddb5` and the
payload `stage90-qcdt.img` at `1230f688b603919b33e1f11908443a76a14cb105fd60dfb59b05d135f3c62d2e`, both
byte-identical to what the run was of — so that run's log *is* the image's log and is quoted rather
than re-measured.

## The inversion 489 left open

489's document ends by naming the frontier: after the two user aborts, "the next two published faults
are both `Lcopyin_wordwise_loop` with `far = 0` … `copyin` faults rather than returning `EFAULT`, which
is a property of this image's `copyin`/`copyout` on a user pointer that is not mapped."

The source says the opposite, and it says it in four places:

    machine_routines_asm.s:542   COPYIO_SET_RECOVER   `adr r3, copyio_error`; `str r3, [r12, TH_RECOVER]`
    machine_routines_asm.s:584   COPYIO_BODY         the loop that faults
    trap.c:290-291               sleh_abort          `recover = thread->recover; thread->recover = 0;`
    trap.c:456-461               sleh_abort          only after `arm_fast_fault` **and** `vm_fault` fail,
                                                     `if (recover != 0) { regs->pc = recover & ~0x1; ... }`

A `copyin` that faults has already armed an exit for exactly that event; the fault is what makes the
`EFAULT` return *possible*, and the instruction that faulted is replaced rather than retried. So the
record 489 read as "where the boot stopped" is the record of a call the kernel answered, and the
number that says so is `thread->recover` — not `far`, not `pc`. A copy of an unmapped user address and
the process's own thread faulting on the same address produce the same `far`/`pc` pair; they do not
produce the same `thread->recover`.

## What the run does with that word: 28 armed, 26 spent

Both runs read the field before `__real_sleh_abort` — it must be before, because the handler's second
statement zeroes it, and a post-call read would report 0 on every entry, which is the same number
"nothing was armed" produces. The second run also reads the *frame* after the call:

| | run 1 (`_recovered` = 28) | run 2 (shipped) |
| --- | --- | --- |
| data aborts (`xnu_live_sleh_seen`) | 32 = `0x20` | 32 = `0x20` |
| armed at entry (`xnu_live_sleh_armed`) | 28 = `0x1c` | 28 = `0x1c` |
| handler spent it (`xnu_live_sleh_redirected`) | — not instrumented | **26 = `0x1a`** |
| `getpid` calls, each answered 1 | 8,388,608 = `0x800000` | 8,388,608 = `0x800000` |
| timer interrupts / quantum expiries | `0x800` / `0x800` | `0x800` / `0x800` |
| entries into `sleh_abort` that returned (in the 8-record band) | — | 6 of 8 |

Run 1's count reached the same 28 under the name `xnu_live_sleh_recovered`; the rename to `_armed` is a
rename and not a change of number, which is itself the control that the name is what moved. It moved
because the old one was wrong: `_recovered` counted the word being *non-zero at entry*, and "the kernel
had a plan" is not "the kernel used the plan". A key named for the outcome it does not measure is the
same defect as a comment that asserts a property nothing checks, and it is why the shipped build
publishes `_armed` from the entry-time test and `_redirected` from the post-call comparison.

## The eight records the run names, and what each of them is

The frames band (`seq <= 16`) names the site of the first sixteen; the recovery band (`seq <= 8`) gives
the word. Run 2's first eight, in order — `pc` is the instruction that faulted and `lr` is where the
copy would have returned, so the call is at `lr - 4` and every one of these five `lr`s is the
instruction after a `bl` to the copy routine:

| seq | `pc` (faulting instruction) | `lr` (the call site) | `far` | `recover` | spent |
| --- | --- | --- | --- | --- | --- |
| 1 | `80014ac0` `Lcopyout_wordwise_loop` `stmia r1!` | `8028bccc` `load_init_program_at_path+0x30` | `0x00001000` | `80014bcc` | **no** |
| 2 | `8000aadc` `L_64loop` `stmia ip!` | `8028d680` `exec_save_path+0x38` | `0xc80ed000` | `0` | — |
| 3 | `8000a7a8` `L64loop+0x8` `stmia r0!` | `800424a0` `copypv+0x84` | `0xc812e000` | `0` | — |
| 4 | `80014ac0` `Lcopyout_wordwise_loop` | `8028d03c` `exec_copyout_strings+0x128` | `0x00101f28` | `80014bcc` | **no** |
| 5 | `00001118` (user) | `00000000` | `0x00102000` | `0` | — |
| 6 | `00001124` (user) | `00000000` | `0x00102000` | `0` | — |
| 7 | `800149d4` `Lcopyin_wordwise_loop` `ldm r0!` | `800a8d34` `telemetry_take_sample+0x158` | `0` | `80014bcc` | **yes** |
| 8 | `800149d4` `Lcopyin_wordwise_loop` | `800a8d34` `telemetry_take_sample+0x158` | `0` | `80014bcc` | **yes** |

Read as four pairs, the field separates the run's faults into kinds that `far`/`pc` alone could not:

- **1 and 4 are `copyout` to a *user* address** (`0x1000`, `0x101f28`), armed and **not spent**: the
  handler's `arm_fast_fault`/`vm_fault` paged the page in and the copy was retried. The console agrees
  from the other direction — 489's reading is that silence after `attempting to load /sbin/launchd` is
  the exec *succeeding*, and an `EFAULT` out of `copyout` would have printed `failed loading … errno 14`.
  So the two readings are the same fact seen from two sides, and this run has both.
- **2 and 3 are `memset` and `bcopy` to a *kernel* address** (`0xc80ed000`, `0xc812e000`), `recover = 0`:
  libkern's copy routines have no user-fault contract and arm nothing, so the handler serviced a demand
  fault on the kernel's own memory. The distinction is the *call site*, not the loop: `L_64loop` and
  `L64loop` are the same shape of bulk store that `copyout` uses.
- **5 and 6 are the fixture's own program** (`pc` `0x1118` and `0x1124` = `entry + 0x38` and `entry +
  0x44`, the `ldr r3, [r0]` and `str r0, [r0]` on the page `mmap` returned), in user mode, no word armed:
  serviced, and the thread continued.
- **7 and 8 are `copyin` from a null user address** (`far = 0`), armed **and spent**: `telemetry_take_sample`
  copies `task->all_image_info_addr` into a stack buffer (`ldm` of 176 bytes from address 0), the copy
  returns `EFAULT`, and the sampler's own `if (copyin(...) == 0)` tolerates it. This is the site 489
  named as the frontier.

And they are not eight records out of eight. `xnu_live_sleh_seen` reaches 32 while the frames band stops
at 16 and the recovery band at 8, so the last 24 records have a count and no site — and the two counters
leave them no room: armed 28 − (1, 4, 7, 8) = the 24 of `seq 9..32`, and redirected 26 − (7, 8) = the same
24. **Every abort this run does not name is a copy-path fault the handler converted into `EFAULT`**, and
the first eight of them (`seq 9..16`, in the frames band) are the same `ldm r0!, {…}` at `lr =
800a8d34` with `far = 0` and the same `sp` as 7 and 8 — the same call, entered again.

## The third outcome, and why two records have no return

Run 2's band has eight entries into `__wrap_sleh_abort` and six returns from it, and the two without a
return are exactly the two whose frame says user mode. That is `trap.c`'s own control flow, not a lost
write:

    if (result == KERN_SUCCESS || result == KERN_ABORTED)   /* user half, after the page-in */
        goto exception_return;
    ...
    exception_return:
        if (recover) thread->recover = recover;
        thread_exception_return();                          /* NOTREACHED */

A user fault the kernel serviced **returns to user mode from inside the handler**, so nothing after
`__real_sleh_abort` runs and `xnu_live_sleh_back` is never written for that entry. The absence of that
key is therefore a reading with three cases behind it — serviced for a user thread, the recovery arm
taken and the function returned, or the handler dying — and the first of those is now named. It is also
the reason the entries/returns pair is `8/6` and not `8/8`, which a reader would otherwise have to
explain away.

## The control: the same eight events on five builds

The records are not this run's; they are the boot's. `486`'s run D, `487`'s run, `488`'s two runs and
this run take the same eight aborts, at the same sites, in the same order, on the same two threads:

| build | the eight `pc`s | `seen` |
| --- | --- | --- |
| 486 run D | `80014480`, `8000a488`, `8000a154`, `80014480`, `1118`, `1124`, `80014394`, `80014394` | `0x20` |
| 487 run 1 | `800149e0`, `8000a9f4`, `8000a6c0`, `800149e0` — **four records, then nothing** | `0x4` |
| 488 runs 1, 2 | `800149e0`, `8000a9f8`, `8000a6c4`, `800149e0`, `1118`, `1124`, `800148f4`, `800148f4` | `0x20` |
| 490 run 1 | `80014a80`, `8000aa84`, `8000a750`, `80014a80`, `1118`, `1124`, `80014994`, `80014994` | `0x20` |
| 490 run 2 | `80014ac0`, `8000aadc`, `8000a7a8`, `80014ac0`, `1118`, `1124`, `800149d4`, `800149d4` | `0x20` |

Only the faulting `pc`s move, and they move because the entry image's own copy loops are rebuilt —
`+0x560` from 486 to 488 and `+0x40` from 490 run 1 to run 2. Every return address is a *kernel*
address and is fixed where the kernel image is: `8028bccc`, `8028d680`, `800424a0`, `8028d03c` and
`800a8d34` in 486's run D, both 488 runs and both 490 runs, and 487's build has the same kernel at
`−0x40` (`8028bc8c`, `8028d640`, `80042450`, `8028cffc`), which is four of those five because its run
stopped before the records that carry the other two.
The fixture's own two `pc`s are `0x1118` and `0x1124` in every run of every build, because they are
addresses inside the RAM disk's fixed Mach-O. 487's run stopping after four is the outlier, and it is
the build whose `+`-attribute expansion dropped `CONFIG_TELEMETRY` — which is also why its records are
the four `seq 1..4` and not the two `copyin` ones.

The fifth reading is the console. The OS console block (the `[os-console-459]` heading through the last
`load_init_program: attempting to load /sbin/launchd`, 1307 bytes over 26 lines) has sha256
`7ecfdceb001961f5805917425e367f00f7d8ce01dd375c5eb0cadbc4af1b2d99` in 486's run D, 487's run, both 488
runs and this one: **byte-identical over five runs of five builds.** The boot's console output has not
moved in three steps, and neither has its abort record.

## What the run is doing while this happens

None of the above is a stop. In both runs `xnu_live_getpid_count` doubles to `0x800000` — **8,388,608
`getpid` syscalls** — with `getpid_last = 1` and `getpid_error = 0`, and there are **zero** `undef`
records and zero `stub_hit=` lines: the fixture's program is past the four checks in `entry_ramdisk.s`
that would have sent it to `udf #1` (the pid, the `mmap` errno, the fresh page's zero, the read-back)
and is spinning on the last of its 27 instructions, which is the *only* place that loop can be reached
from. `xnu_live_irq_timer_count` and `xnu_live_tmr_qexp_over` both pass `0x800`, so the timer is
delivering and the scheduler is taking quanta; `xnu_live_sleh_storm = 9`. The run ends because the
payload's nets bring the phone back — the SoC watchdog's own timeout is the only thing across the jump
(`hw_watchdog_timeout_s = 0x19`), and the kernel is alive at that moment — so a log that ends in the
middle of a sampler's tolerated `EFAULT` is a log cut by the clock and not by a stop.

## Two numbers that do not move between two different images

The entry report carries `xnu_entry_checksum = 0x9071ec79`, `image_bytes = 0x00537a74`,
`bss_start`/`bss_end`, `entry_base`/`entry_va` — **identical in 488's runs and both 490 runs**, and the
reason is not that the images are the same (they are not: the entry image's sha256 is
`1bec72f6…` here against 488's `759c49f1…`). `stage90_xnu_entry_checksum` XORs the *report's own
fields* (`stage90.h:7051-7061`), and `image_bytes` is the embedded blob's size, which is rounded up to
the payload's own alignment. A reader comparing two runs' identity blocks would conclude the image had
not changed. It is worth one sentence because it was nearly written down that way here: the number
names the report and measures the report.

## Defects of this step's own instruments, recorded

- **`_recovered` was a name asserting what it did not measure.** It counted the recovery word being
  non-zero at entry, which is "the kernel had a plan", and a reader — including this step's first
  device run — would read it as "the handler used the plan". Renamed to `_armed`, with `_redirected`
  beside it for the event the first name implied. The check now asserts both names *and* the test each
  is fed by, because a rename alone would be a claim in a comment.
- **`body_of` read to the next `objdump` heading, so it reported that `copyin` "contains no store into
  `[..., #664]`" and that two *local labels* "are not copy functions" — on a correct image.** `objdump`
  prints a heading for every local label, so a body read that way is a label's body and not a
  function's. Fixed with `nm`'s kind column: globals for functions, `label_body` for the two labels the
  claims are actually about. This is 486's `getMetaClass` lesson recurring in a new checker.
- **Seven selftest mutations stopped mutating when step 3 relinked and moved `copyin`.** They spelled
  the disassembly lines out — address, opcode and trailing comment — so a rebuild moved the target and
  the selftest reported seven `ACCEPTED` on a check that was right. That is 228's class exactly: a
  mutation written against a literal current value. Every mutation now addresses its line by address
  (`_edit_line_at`) or resolves the arming instruction by what it does (`_arming_of`).
- **A needle that matched two functions.** `the_handler_stops_consuming_the_word` edited the first
  occurrence of `recover = thread->recover; thread->recover = 0;` in `trap.c`, which is in
  `sleh_undef`; the mutation was accepted because `sleh_abort` was untouched. The needle now carries
  the statement that follows it in `sleh_abort`.
- **A mutation that raised instead of mutating.** `something_else_in_the_image_arms_it` picked its host
  function by name from `nm`, and the first host is a *local* symbol, so `body_of` returned `None` and
  the mutation died with `TypeError`. A mutation that cannot run is not a mutation; the host is now a
  global text symbol that reads the offset.
- **And one reading of the run that had to be re-derived rather than trusted**: the first pass over the
  live channel attributed each record's `pc`/`lr`/`user` to the *next* record, because
  `entry_note_sleh` writes `_seen` before it writes `_pc`, and a parser that snapshots on `_seen`
  collects the previous record's frame fields. The bands as published in this document are parsed
  band-by-band from `xnu_live_sleh_seq` to the next `xnu_live_sleh_seq`.

## What is owed

- **The unnamed tail is pinned by arithmetic and named by nothing.** 24 of the 32 aborts are known to
  be redirected copy-path faults and the run records the site of only the first eight of them. A
  frames band that took the first eight *distinct* `(pc, lr)` pairs rather than the first eight entries
  would name the tail with one record instead of a subtraction — and would have caught this run's
  question without the two counters.
- **Unchanged and still owed:** 487's third census of the platform expert instance `0xc0591740`'s own
  service children; the release as a reading; whether the exec path's copies are serviced demand
  faults via **`vm_fault`** (`osfmk/arm/trap.c:443` user, `:563` kernel) as a *caller*-side record
  rather than as an inference from the console; 488's deferred flag-list derivation.
- **One observation to hand on, not this step's answer:** `xnu_live_dec_same` (the count of armings that
  repeat 484's reference deadline value) is absent from 488's two runs and from every run before them
  except **485's run 2**, where it reached `0x20`; in this step's runs it passed `0x800`. 484 wrote the
  census to separate "a clock nothing waits on" from "a fixture that never asks to wait", and the
  difference between runs of nearly the same image is now itself the reading that needs explaining.

## The build

Step 3 (`build_entry.sh`, `STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1`) is green with
`xnu_entry_490` and `--selftest: all 31 mutations were refused`, beside 489's 30, 488's 21 and 486's 46.
Step 4 (`stages/stage90/build.sh`, `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'`) is green with
`kernel_size = 5966100`, `dt_size = 2521088`. The entry image is 5470836 bytes in `.bin` (unchanged in
size from 487's build), `.elf` 6638284, `.text` 5254728, `.data` 206796, `.bss` 361032,
`bss 0x80537a80 .. 0x8058fcc8`, headroom 1508152 below `topOfKernelData` — the `.bin` size and the
headroom are the same as 487's and the `.text` moved by +160, and between 487 and here the tree also
carries 488's fix and 489's non-image change, so no part of that delta is this step's alone. 26
undefined symbols, entry base `0x80000000`, entry point `0x80000074`. Adding this step's last two
claims to the check and rebuilding reproduces the image and the payload byte-for-byte, which is why
the second run's log is the shipped build's.

`xnu_arm_entry.bin` sha256 `1bec72f6af4c5f3123c2ecf40eb7f4b44074136cce8222b9fd41fd275c70ddb5`,
`xnu_arm_entry.elf` `2f500abaaccd5d1223d889c31fe5122001bdf8ec99a2f2a1aa7e64993463e440`,
`stage90.bin` `9dec264ea0753acebb4b921692ee15a805d9f4e73e331020585b44803dd6724e`,
`stage90-qcdt.img` `1230f688b603919b33e1f11908443a76a14cb105fd60dfb59b05d135f3c62d2e`.

## Safety

Both runs: a non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of
either, `xnu_entry_checks = 5` / `xnu_entry_failures = 0`, no `exception:` line, no `panic_entered`, no
`stub_hit=` line, and the device back on Android on its own (`MI 4LTE`, release 10) after each run:
517704 bytes captured for the first, 518176 for the second.
