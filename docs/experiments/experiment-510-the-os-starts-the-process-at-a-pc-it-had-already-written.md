# Experiment 510 — the OS starts the process at a PC it had already written

**One line.** `thread_setentrypoint` is wrapped, and the console block ends with two lines that name
one number twice:

    load_init_program: attempting to load /sbin/launchd
    mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80048eb8)
    mini4: the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)

The function is four instructions — `sv = get_user_regs(thread); sv->pc = entry;`
(`osfmk/arm/status.c:662-675`) — so the two halves of that line are the two facts it holds: the
*argument* (what the exec decided) and the *word* (what the thread now holds). The wrapper reads the
word before the call too, and the pair is the reading this step is for: `xnu_live_entrypt_seq = 1`,
`_caller = 0x80291cb0` (`activate_exec_state + 0xe8`, the OS's own call site, read out of the linked
instructions by the build), `_thread = 0xc05c81a0`, `_entry = 0x000010e0`, `_entry_hi = 0`,
**`_before = 0x000010e0`** — the one prediction this step got wrong, and the run's finding — and after
the call `xnu_live_entrypt_done_seq = 1`, `_done_entry = 0x000010e0`, `_after = 0x000010e0`. The
prediction was that `_before` would be 0 because `activate_exec_state` calls `thread_state_initialize`
immediately before; the run answers that `thread_setstatus` had already written the same word from the
image's own `LC_UNIXTHREAD`, so the boot holds **two independent derivations of one address** and
`0x10e0` is the number a reader can now see the kernel and the file agree on. Nothing else in the boot
moved: no `stub_hit`, no `undef`, no `osr`, no `panic`, no trap record, no report — and the fixture
still ran its whole program (`getpid` answer 1, the fork, the child's `exit`, `SIGCHLD`, the reap,
`xnu_live_wait_status = 0x00000300`, `_wait_error = 0x0a`).

    bash stages/stage90/xnu_arm_boot/build_entry.sh          # prints xnu_entry_510's two clauses
    python3 tools/check_saved_state_offsets.py --selftest    # all 21 mutations were refused
    python3 tools/check_saved_state_offsets.py               # ok: STAGE90_ACT_PCBDATA 848, assym.s 848
    python3 tools/host_ramdisk_macho_check.py --selftest out/stage90/xnu_arm_entry.elf  # all 128 refused
    python3 tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf
    python3 tools/check_pthread_table_slots.py --selftest    # all 7 refused
    python3 tools/check_timer_sources.py --selftest          # all 48 refused
    python3 tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 30 refused
    python3 tools/check_fault_recovery.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 31 refused
    python3 tools/check_driver_catalogue.py --selftest       # all 131 refused
    python3 tools/check_experiment_index.py                  # 487 rows

## What the step was for

509 measured that the init image is loaded *for* process 1 by a thread that is running in `kernproc`
(`xnu_live_exec_done_current = 0`), which is exactly the reading that promoted `thread_bootstrap_return`
from a name on the standing list to the frontier: the process has an image, and nothing yet says where
its own thread will start running. The two candidates were the mechanism (`entry_rcu`,
`thread_bootstrap_return`) and the number (the PC). This step takes the number, because it is the one
that can be read with a wrapper and because it is the one a reader asks for — *where does the OS start
my program* — while the mechanism stays owed.

**The instrument is one wrapper with two reads, and the reason is the function's own shape.** A wrapper
that published only `entry` would report what the caller *said*; a `thread_setentrypoint` that returned
without storing anything produces the same argument, so the argument alone cannot tell a real store
from no store at all. That shape is already on this project's list of defects more than once (a
`write(2)` that published nothing, a report path whose buffer held another writer's text), so the
wrapper reads the thread's saved PC before the call and again after it. The word is at
`thread + STAGE90_ACT_PCBDATA + STAGE90_SS_PC`, because `get_user_regs(thread)` is
`&thread->machine.PcbData` (`status.c:502-505`) and `PcbData` is the first member of
`struct machine_thread` — and the second half of that is not a claim made in this step: the generated
`assym.s` has carried `ACT_PCBDATA_PC - ACT_PCBDATA == SS_PC` as a checked agreement since 474, and
510 adds `STAGE90_ACT_PCBDATA` itself to the two-way comparison against `assym.s` (848 both sides).

**Why `--wrap` sees this call at all.** `thread_setentrypoint` is defined in `osfmk/arm/status.c` and
called from `bsd/kern/kern_exec.c` — different objects, so the reference is genuinely undefined and
455's negative case (`__mac_execve`, whose only callers share its own object) does not apply. The build
then reads the `bl` out of `activate_exec_state`'s instruction range, so "the record this run wrote is
the exec's call" is a fact about the linked image and not about the boot.

## What the build checks

Two clauses, and the second is the one that cost a run:

  - **the call site goes to the wrapper** — `bl <__wrap_thread_setentrypoint>` inside
    `activate_exec_state`'s range (`0x80291bc8..`), and **exactly one** such branch in the whole image.
    The one record this step writes is therefore the exec's decision and nobody else's.
  - **the entry is passed through as the register pair the caller used** — every call to the real
    `thread_setentrypoint` in the image is inside the wrapper, and every one of them **writes r2 and r3
    first**. This is the check the first build did not have and the reason run 1 was thrown away: the
    declaration is `void thread_setentrypoint(thread_t, mach_vm_offset_t)` (`osfmk/kern/thread.h:980`)
    and `mach_vm_offset_t` is 64-bit, so the entry travels in **r2:r3 with r1 unused** — the caller's
    own object shows it (`out/xnu_kernel_obj/bsd_kern_kern_exec.o`: `ldr r2, [r8, #4]` / `mov r3, #0` /
    `bl`). A wrapper that declared the parameter `uint32_t` reads r1 and leaves r3 alone.

The second clause took three builds and produced two refusals, and both are worth keeping because both
are the same kind of defect — a check that could not see what it was checking:

  - **the first version asserted `sites == 1` for the real function and the build refused the step**:
    `this image calls the real thread_setentrypoint from 2 site(s)`. gcc had **tail-duplicated** the
    `bl`, so a count of call sites is not a statement about callers.
  - **the second version read the r2/r3 writes out of a window of the disassembly and refused
    again**: `510's width check found no call to the real thread_setentrypoint inside its own window
    reader, so it measured nothing`. The reader printed the three lines preceding a match and split on
    `---`, so the `bl` line itself — the only line carrying the operands — never survived the split. A
    check that prints nothing and passes is indistinguishable from a check that passes.
  - **the negative side was then exercised on purpose** by narrowing the parameter back to `uint32_t`
    and rebuilding: `of 510's 2 call(s) to the real thread_setentrypoint, 0 write r2 first and 0 write
    r3` — the intended sentence, in the build, before a device was touched.

The first clause's negative side is the census below: 68 wrapped symbols, and the count that moved from
509's 67 to 510's 68 is `thread_setentrypoint` arriving in the **reached by a branch** category rather
than in the by-address one.

## The readings

Line numbers are `/tmp/cancro-last_kmsg.txt` from the second run (572646 bytes, 8355 lines):

| line | record | what it says |
|---|---|---|
| 3992-3994 | `load_init_program: attempting to load /usr/local/sbin/launchd.development` / `failed loading …: errno 2` / `attempting to load /sbin/launchd` | 509's console block, unchanged |
| 3995 | `mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80048eb8)` | 509's line, unchanged |
| **3996** | **`mini4: the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)`** | **this step: the OS's own start address for pid 1, printed by the OS's own `printf`, beside the word the kernel had already written it into** |
| 7651-7653 | `xnu_live_entrypt_seq = 1`, `_caller = 0x80291cb0`, `_thread = 0xc05c81a0` | the record written before the call: the caller is `activate_exec_state + 0xe8`, the thread is pid 1's |
| **7654-7656** | **`_entry = 0x000010e0`**, **`_entry_hi = 0x00000000`**, **`_before = 0x000010e0`** | **the argument, its top half (zero, so it is an address), and the word *before* the store — the step's wrong prediction and its finding** |
| 7657-7659 | `xnu_live_entrypt_done_seq = 1`, `_done_entry = 0x000010e0`, `_after = 0x000010e0` | the read-back after the call: the store happened, and `_before == _after` |
| 7712, 7727 | `xnu_live_sleh_seq = 5`, `_user = 1`, `_pc = 0x00001118`; `_seq = 6`, `_user = 1`, `_pc = 0x00001124` | the process the record names, running its own instructions a few words past the PC the OS named |
| 7851, 7900-7906 | `xnu_live_fork_seq = 1`; `xnu_live_pth_delete_seq = 1`, `xnu_live_sigchld_signal = 0x14` | 506/507's fork, death and signal, unchanged |
| 7838, 7950-7956 | `xnu_live_read_word_after = 0xfeedface`; `xnu_live_wait_error = 0x0a`, `xnu_live_wait_status = 0x00000300`, `xnu_live_getpid_last = 1` | 504's read and 508's reap, unchanged |
| 7984 and seven more | `xnu_live_sleh_pc = 0x80016454` (`Lcopyin_wordwise_loop`), `_lr = 0x800aad34` (`telemetry_take_sample + 0x158`), `_far_frame = 0`, `_user = 0`, `_redirected = 1`, `_frame_ok = 1` | the copy-loop fault 508 and 509 named, still there — eight published records at that one site, with `xnu_live_sleh_seen` reaching `0x22` and `_armed` `0x1d` |
| — | `xnu_live_stub_hit_*`, `xnu_live_undef_*`, `xnu_live_osr_*`, `xnu_entry_abort_entries`, `xnu_entry_panic_entered`, `trap record`, `pid 1 exited` | **all absent** — the print added nothing to the path |

## Two readings this step's own prediction got wrong

### `_before` is 0x10e0, not 0 — the boot derives the entry point twice

The writer's comment predicted a **cleaned** state: `activate_exec_state` calls
`thread_state_initialize(thread)` and then `thread_setstatus` immediately before this call
(`bsd/kern/kern_exec.c:730-760`), so the word the exec is about to write is one the kernel has just
zeroed, and a non-zero `_before` would say the exec is reusing a thread whose saved state was never
cleared. The run answers `_before = 0x000010e0` — the entry point, before the call that sets the entry
point — and the mechanism is in the second of the two calls the prediction named and did not follow
through: `thread_setstatus(thread, ARM_THREAD_STATE, ...)` (`kern_exec.c:743-758`) copies the image's
whole `LC_UNIXTHREAD` state, **`pc` included**, into exactly this word. Then
`load_threadentry` → `thread_entrypoint` reads `state->pc` back out of that same state
(`status.c:683-705`) and hands it to `thread_setentrypoint`. So the boot holds two derivations of one
address — the mapped thread state, and the exec's argument — and `_before == _after` is the reading
rather than a disappointing one.

**What it buys is a cross-check that no single record could give.** `xnu_live_entrypt_entry` is the
kernel's *decision*; `_before` is the kernel's *state* from a different write; and both are `0x10e0` —
which is also the `pc` word of the RAM disk's own Mach-O, read out of the blob on the host by
`tools/host_ramdisk_macho_check.py` and asserted there to be inside `__TEXT` (the file's own line in
the same build output: *"pc 0x10e0 is file offset 0xe0, and the 78 words there are the program this
file describes"*). Three sources, one number, and the console line puts two of them in the same block
with no filesystem between them. The prediction was written from the *name* of the initializer
(`thread_state_initialize`) rather than from the sequence of calls after it, which is the same kind of
slip as 509's `_done_current` — a prediction from a function's name instead of from its callers.

### The first run measured the wrapper instead of the kernel, and the ABI is why

Run 1's console line read `mini4: the OS starts the process at 0x8 (the thread's user pc was 0x10e0)`
and its records read `_entry = 0x00000008`, `_before = 0x000010e0`, `_after = 0xde500000` — the last
being the RAM console's own base address, a word the wrapper itself had left in a register. The boot
then answered in kind: `pid 1 exited -- exit reason namespace 2 subcode 0xb`, `xnu_entry_panic_entered
= 1` (`panic_len = 0x1320`), one `stub_hit`, four `osr`, one trap record, and every fixture key zero —
`getpid_last = 0`, `fork_seq = 0`, `wait_status = 0`. A 32-bit declaration had the wrapper read **r1**,
a register the call does not use, and pass r2/r3 through untouched to the real function, which stored
whatever they held. The run is kept here because it is the measurement's own negative side: the
instrument perturbed the kernel it was measuring, and the *only* thing that says so from inside the log
is the two-record pair (`_entry` bad, `_before` good) beside the console line that agrees with
neither.

The fix is the declaration (`uint64_t`), the second build clause above, and one new key: `_entry_hi` is
published because a 32-bit address has a zero top half, so a run whose `_entry_hi` is not zero is a run
whose entry is not an address at all.

## The image

  - **No fixture change and no kernel change.** One flag, one wrapper, one writer pair, one print.
    `.text` 5287968 → **5288480** (+512: the wrapper, its two record calls and the format string), the
    entry image's file is **5503612 bytes** again, as it has been for every build in this step and the
    several before it — `__bss_start 0x8053fa80` with 362888 bytes to 0x80598408, **27 symbols
    undefined**, and the wrap
    census is **68 wrapped symbols: 56 reached by a branch, 1 same-object-only
    (`_ZN9IOService12matchPassiveEP12OSDictionaryj`), 1 never called here (`sleep`), 10 by address only**
    (`vcputc getpid mmap poll open read fork exit wait4 thread_quantum_expire`).
    `thread_setentrypoint` is in the first category, which is the census's own confirmation of the
    first clause.
  - the wrapper is at **`0x8047b270`** (`__wrap_thread_setentrypoint`), and the one branch to it is at
    `0x80291cac`, inside `activate_exec_state` (`0x80291bc8`) — the same function `_caller` names, one
    instruction after the `bl`.
  - the five files this step is: `stages/stage90/xnu_arm_boot/entry_trace.c` (the wrapper),
    `stages/stage90/xnu_arm_boot/entry_stubs.c` (the two writers and their keys),
    `stages/stage90/xnu_arm_boot/entry_saved_state.h` (`STAGE90_ACT_PCBDATA`),
    `stages/stage90/xnu_arm_boot/build_entry.sh` (the flag, the two clauses, the wrap-list count 67 →
    68), `tools/check_saved_state_offsets.py` (the new constant in the comparison and in the selftest's
    mutation list, 20 → 21), and this document.
  - the payload, from the build that ran: `stage90.bin` 5999040 sha256
    `0d81aef662c45c6006e4c4c0224debc5ce5e3133ba080af17d6d92618a5a1e3a`, `stage90.img` 6002688 sha256
    `e11151968410e94733f26e672388dde6700f0df489ff9f16bcebceae21255d86`, `stage90-qcdt.img` 8523776
    sha256 `92994a59ff32f1508a169ad36ca17c975eeb3a8370a9f7cce00d6fe68523a895`. A rebuild after the
    comment edits reproduced all three byte for byte (`cmp` clean), so the artifact the device ran and
    the artifact in `out/stage90/` are the same bytes.
  - **run 1's log was not kept** — it was read as `/tmp/cancro-last_kmsg.txt` (8407 lines, 580576
    bytes, `No errors detected`) and then overwritten by run 2, because the run was understood to be
    the ABI's fault from its own readings rather than from a document. Its readings are quoted above
    as they were read; run 2 is the run this document's table cites, and its log is the one on disk.
  - **one address reading worth keeping**: `xnu_live_entrypt_thread` is `0xc0552df0` in run 1 and
    `0xc05c81a0` in run 2, and `_caller` is `0x80291cb0` in both. The offset (`activate_exec_state +
    0xe8`) is the reading; a thread pointer is not comparable across runs and the caller is.

## What is owed

  - **The mechanism, which this step deliberately did not take.** `_entry` is where pid 1's thread will
    start; nothing here says *when*, or by what path, or what `thread_bootstrap_return` and `entry_rcu`
    do with a thread that has an entry point and no user-mode history. The open question 509 promoted
    is now half answered — the number is measured, the route is not — and the next instrument is the
    one that reads the thread's saved state at the moment it first leaves the kernel for user mode,
    rather than the moment the kernel writes the PC into it.
  - **`_before` is now a reading with a second source, and the first source is not published.** The
    `LC_UNIXTHREAD` state arrives in this word through `thread_setstatus`, and nothing in this image
    reads that state where it is copied. A wrapper on `thread_setstatus` would publish the same
    number from the other side and turn "two derivations agree" from a comparison of one measurement
    with a host-side file read into a comparison of two measurements.
  - **`_entry_hi` has never been non-zero**, which is the shape of every check that has only ever
    passed: the key is published for the case where the entry is not a 32-bit address, and no run has
    produced one. It is worth a *build* clause rather than a run — the declaration's width is checked,
    the value's is not.
  - **A record that cannot be lost** (508's own defect, unchanged): the console's two-writer exposure
    dropped `xnu_live_wait_done_seq = 1`. This step's nine records all landed, which is luck as much as
    design.
  - **The L2 descriptor for the fixture's page, read on both sides of a `copyout`** (508);
    **`p->p_xstat` published by nothing** (506/507/508), the report's empty `xnu_entry_why`, the
    `trap record:` gate, the watchdog as an ending, 505's corpse-path slot `0x802933b4`, 504's
    `xnu_live_mdevadd_base` as a page number / an arrival record at `mdevopen` / a second `read` at an
    offset, 503's leeway table row / the ~1.8 ms difference / a third ask, and 502's long list.
  - **And the telemetry copy loop**, named in 508 and unchanged here. This run's records name both ends
    of it: `_pc = 0x80016454` = `Lcopyin_wordwise_loop` with `_lr = 0x800aad34` =
    `telemetry_take_sample + 0x158`, `_far_frame = 0`, `_user = 0`, and `_redirected = 1` on every one
    — the kernel's own copy of a user address that has no translation, planned and converted, eight
    times in the published records and `0x22` times in `xnu_live_sleh_seen`. Why the sampler walks an
    unmapped address is the question, and it is not this step's.

## Safety

Every device touch went through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both boots were non-persistent `fastboot boot`
and nothing was flashed. **Two runs**, because run 1's instrument had the wrong argument width and
perturbed the boot it was measuring: that run ended in `pid 1 exited` and a `panic`, the software
dead-man and the hardware watchdog did their work, and the device came back on Android by itself with
`No errors detected`. Run 2 is the one this document reads: it ended on the hardware watchdog's 25 s
timeout, as 507's, 508's and 509's did, with the device back on Android by itself and no stop of any
kind — the print added one line to the OS's console and no new call on any path the boot depends on.
