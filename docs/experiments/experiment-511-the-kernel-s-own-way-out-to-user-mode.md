# Experiment 511 — the kernel's own way out to user mode

**One line.** `bsd_ast` is wrapped, and the console block the OS left off at in 509 — which 509 and 510
had each added a line to — now reads:

    load_init_program: attempting to load /sbin/launchd
    mini4: the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)
    mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80048eb8)
    mini4: the AST is done -- pid 1's thread is at 0x10e0 for user mode (sp 0x101efc)

`bsd_ast(thread_t)` is the function `ast_taken_user` calls on the way out to user mode
(`osfmk/arm/locore.s:1897-1943` is `load_and_go_user`, the tail of `thread_exception_return`, and the
AST is what runs immediately before its restore-and-return), so a before/after pair on the thread's
saved PC — the same word 510 read, one frame out — brackets everything the AST did, and for the first
call that is the exec itself. **The run answers with two calls and closes the route with one thread
pointer crossing four records written by three different instruments.** `bsd_ast` #1 arrives on thread
`0xc0485d20` with a **zeroed** saved state (`_before = _after = _sp = 0`, `_cpsr = 0x10`) on
**`_pid = 0`** — the bootstrap thread, which has never been in user mode — and *inside it* the exec runs
and writes 0x10e0 into a different thread. `bsd_ast` #2 arrives on thread **`0xc04c4be0`** with
`_before = _after = 0x000010e0`, `_sp = 0x00101efc`, `_cpsr = 0x10`, **`_pid = 1`**. That thread is the
one `thread_setentrypoint` wrote (`xnu_live_entrypt_thread = 0xc04c4be0`), and it is the one that takes
the process's first two faults in user mode (`xnu_live_sleh_seq = 6`, `_user = 1`, `_pc = 0x00001118`,
`_sp = 0x00101efc`; and `_seq = 7`, `_pc = 0x00001124`, same `_sp`). Nothing else in the boot moved: no
`stub_hit`, no `undef`, no `osr`, no `panic`, no trap record, no report — and the fixture still ran its
whole program (`getpid` answer 1, the fork, the child's `exit`, `SIGCHLD`, the reap,
`xnu_live_wait_status = 0x00000300`, `_wait_error = 0x0a`).

    bash stages/stage90/xnu_arm_boot/build_entry.sh          # prints xnu_entry_511's clause
    python3 tools/check_saved_state_offsets.py --selftest    # all 21 mutations were refused
    python3 tools/check_saved_state_offsets.py               # ok: ACT_PCBDATA/SS_PC/SS_SP/SS_CPSR
    python3 tools/host_ramdisk_macho_check.py --selftest out/stage90/xnu_arm_entry.elf  # all 128 refused
    python3 tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf
    python3 tools/check_pthread_table_slots.py --selftest    # all 7 refused
    python3 tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 48 refused
    python3 tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 30 refused
    python3 tools/check_fault_recovery.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 31 refused
    python3 tools/check_driver_catalogue.py --selftest       # all 131 refused
    python3 tools/check_experiment_index.py                  # 488 rows

## What the step was for

509 promoted one question to the frontier and 510 answered half of it:

> what starts pid 1's own thread at a user PC

510 made the PC a number — `_entry = _before = _after = 0x000010e0`, on thread `0xc05c81a0`, written by
`activate_exec_state`'s call to `thread_setentrypoint`. What it did not measure is the *return*: the
moment the kernel actually goes back to user mode, which is the only moment at which the process is
running. **`thread_bootstrap_return` is the name the standing list carries for that, and it cannot be
wrapped**, which is the first thing this step had to establish: it is a label at the *head* of
`load_and_go_user` (`osfmk/arm/locore.s:1897-1912`, where `LEXT(thread_bootstrap_return)` falls
straight through into `LEXT(thread_exception_return)`), and nothing calls it — it is a
`thread_continue_t`, an *address* set into a thread's PCB and jumped to by machinery that never
returns. A C wrapper there would put a prologue and a `bx lr` in front of a thread that has no return
address. `thread_exception_return` is the same address (the assembler emits both labels at 0x80019f98)
and is used the same way.

**So the instrument is one frame in from the label, at the earliest point on the path that is a call.**
The path is

    activate_exec_state                       (510's call site: the exec's state commit)
      -> execve -> load_init_program -> bsdinit_task
        -> bsd_ast                            (bsd/kern/kern_sig.c; called by ast_taken_user)
          -> ast_taken_user                   (osfmk/kern/ast.c; called by load_and_go_user)
            -> load_and_go_user -> return_to_user_now   (locore.s; the actual restore and return)

and `bsd_ast` is a real function with a real caller in another object, which is what `--wrap` needs.
Everything the route needs is in one place there: it takes the thread as an argument, the thread's
saved state is already the state `load_and_go_user` is about to restore, and 509's and 510's own
console lines come from *inside* the first call.

**The pair is the same before/after on the same word, and the reason it is worth a run rather than an
argument is the `_before` half.** `_after` alone would be 510's number read twice. `_before` is the
falsifier for a possibility no other record can see: whether the thread that arrives at the AST has a
user state at all, and — if it does — where it came from. The prediction was that `cloneproc` in
`bsd_utaskbootstrap` (`bsd_init.c:1135-1166`, "clone the bootstrap process from the kernel process")
produces a thread with no user-mode history, so its saved PC would be whatever that clone left. The run
says something sharper: **there are two calls, the zeroed state is the *bootstrap* thread's, and the
exec runs inside the first call** — so by the time pid 1's thread takes its AST, 0x10e0 is already in
the word, written by the other instrument's own moment. That is recorded below as the step's wrong
prediction.

## What the build checks

Four properties, and the design choice they are protecting is that this wrap must not be able to
damage the path it measures:

  - **membership** — `bsd_ast` is not in pass 1's undefined set, so the function the AST delivers to is
    XNU's own and `__real_bsd_ast` is not a stand-in. The kernel's own count line reads
    `none of 69 wrapped name(s) nor any __real_ stand-in is in pass 1's undefined set`.
  - **the caller is a different object, and the branch reaches the wrapper** — `bl <__wrap_bsd_ast>`
    (0x8047b324) inside `ast_taken_user`'s range (0x8011643c..0x80116664), which is 455's hazard
    checked structurally: a wrapper whose callers share its object links, defines its symbol and never
    runs.
  - **every reference to it in the pool is a call** — the clause reads the relocation records of all
    708 objects and requires the set to be exactly `R_ARM_CALL`. This is the check that makes the
    choice of `bsd_ast` over `thread_exception_return` a fact about the tree: an
    `R_ARM_ABS32`/`MOVW`/`MOVT` would be an address taken, `--wrap` rewrites those too, and the
    wrapper's address could then end up in a continuation slot a thread is jumped to and never returns
    from. Measured: one `R_ARM_CALL` in `osfmk_kern_ast.o` and nothing else.
  - **the reader has no pipe in it**, which is a defect of this step's own check and is recorded in the
    build as one — see below.

## The readings

Line numbers are `/tmp/511-run2-kmsg.txt` (573837 bytes, 8392 lines, the second run):

| line | record | what it says |
|---|---|---|
| 3994 | `load_init_program: attempting to load /sbin/launchd` | the line every run before 509 ended on |
| 3995 | `mini4: the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)` | 510's line: the exec's decision |
| 3996 | `mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80048eb8)` | 509's line: the image is in place |
| **3997** | **`mini4: the AST is done -- pid 1's thread is at 0x10e0 for user mode (sp 0x101efc)`** | **this step: the AST whose return is the kernel's return to user mode, on pid 1's thread, at the exec's own PC** |
| 7557-7560 | `xnu_live_ast_seq = 1`, `_caller = 0x80116500`, `_thread = 0xc0485d20`, **`_before = 0x00000000`** | the first call, written before it: `_caller` is `ast_taken_user + 0xc4`, and the thread's saved PC is zero |
| 7685-7690 | `_done_seq = 1`, **`_after = 0x00000000`**, **`_sp = 0x00000000`**, `_cpsr = 0x00000010`, **`_pid = 0x00000000`** | after it: the state is still zero, and `current_proc()` is `kernproc` — **a thread with no user-mode history**, which is what the run found |
| 7695-7698 | `_seq = 2`, `_caller = 0x80116500`, **`_thread = 0xc04c4be0`**, **`_before = 0x000010e0`** | the second call, on a *different* thread, whose saved PC is already the entry point |
| 7699-7704 | `_done_seq = 2`, **`_after = 0x000010e0`**, **`_sp = 0x00101efc`**, `_cpsr = 0x00000010`, **`_pid = 0x00000001`** | after it: pid 1's thread, with a user stack, in the mode `load_and_go_user` demands |
| 7656-7657 | `xnu_live_entrypt_thread = 0xc04c4be0`, `_entry = 0x000010e0` | 510's instrument — **the same thread pointer as `_seq = 2`** |
| 7738, 7753 | `xnu_live_sleh_seq = 6`, `_user = 1`, `_pc = 0x00001118`, `_fsr = 0x07` (a read), **`_thr = 0xc04c4be0`**, **`_sp = 0x00101efc`**; then `_seq = 7`, `_pc = 0x00001124`, `_fsr = 0x080f` (a write), same `_thr`, same `_sp` | the process's own first two faults, on the **entry page** `mmap` returned (`_far = 0x00102000`) — **same thread, same user stack** |
| 7908 | `xnu_live_sleh_seq = 8`, `_type = 3` (prefetch abort), `_user = 1`, `_thr = 0xc04c31a0`, `_pc = 0x000011a4`, `_sp = 0x00101efc` | 507's forked child, on a thread of its own, with the same stack value because `fork` copied the address space |
| 7838, 7950-7956 | `xnu_live_read_word_after = 0xfeedface`; `xnu_live_wait_error = 0x0a`, `xnu_live_wait_status = 0x00000300`, `xnu_live_getpid_last = 1` | 504's read and 508's reap, unchanged |
| — | `xnu_live_stub_hit_*`, `xnu_live_undef_*`, `xnu_live_osr_*`, `xnu_entry_abort_entries`, `xnu_entry_panic_entered`, `trap record`, `pid 1 exited` | **all absent** |

**The route, as one row per instrument:**

| instrument (the wrapper that wrote it) | thread | user PC | user SP | pid |
|---|---|---|---|---|
| `thread_setentrypoint` (510) | `0xc04c4be0` | `0x000010e0` written | — | — |
| `bsd_ast` (511), call 2 | `0xc04c4be0` | `0x000010e0` in the word | `0x00101efc` | 1 |
| `sleh_abort` (474), seq 6 | `0xc04c4be0` | `0x00001118` faulting | `0x00101efc` | — |
| `sleh_abort` (474), seq 7 | `0xc04c4be0` | `0x00001124` faulting | `0x00101efc` | — |

Four records, three instruments, one thread pointer and one stack pointer. The addresses in the middle
column are also the fixture's own arithmetic: `0x10e0` is the Mach-O's entry (`__TEXT.vmaddr` 0x1000 +
28 + `sizeofcmds` 0xc4), `0x1118` is `entry + 0x38` (the `ldr r3, [r0]` of the page `mmap` returned) and
`0x1124` is `entry + 0x44` (the `str r0, [r0]` that follows it) — the host-side check prints those three
addresses before the device is touched.

## Two readings this step's own prediction got wrong

### The first build's console line named the wrong call, and the wrong event

The first build printed on `seq == 1`, on the reasoning that the first AST is the one that carries the
exec. The run answered

    mini4: the AST is done -- pid 0's thread is at 0x0 for user mode

which is a true statement about `_seq = 1` (`_thread = 0xc050bec0`, `_after = 0`, `_sp = 0`,
`_pid = 0`) and the wrong line for a reader: it names the bootstrap thread, which is not a process and
is not going anywhere in user mode, and it says `0x0` where the step is about `0x10e0`. **The exec does
run inside the first AST — 509's and 510's lines both appear above 511's in the same block — but it
runs on *another thread*, and the call that carries pid 1 out to user mode is the second one.** The
print condition was changed from a position in a count to a reading: `after != 0u`, "this thread has a
user PC to return to", with a once-flag. The image was rebuilt and the step run again; both runs'
readings and both runs' hashes are below. This is 509's defect (a durable artifact claiming the wrong
thing) arriving from the other side — not an event that had not happened yet, but an event that had
happened to someone else.

### The zeroed saved state is the bootstrap thread's, not the cloned thread's

The prediction in the wrapper's own comment was that the thread `cloneproc` produces would be the one
with no user-mode history, so `_before` would be whatever the clone left. The run says: **`_seq = 1` is
the zeroed thread** (`_before = _after = _sp = 0`, `_pid = 0`) and **`_seq = 2` is pid 1's thread with
0x10e0 already in the word**. Both halves of the prediction are therefore wrong in an instructive way.
The first call is delivered on the *bootstrap* thread — the one running `bsd_init`, `kernproc`'s — and
that is the thread whose saved state is empty, precisely because a kernel thread has never had a user
state. And pid 1's thread is *not* empty by the time its AST arrives, because the exec that fills the
word runs inside the first AST: `bsd_ast` → `bsdinit_task` → `load_init_program(initproc)` → `execve`
→ `activate_exec_state` → `thread_setentrypoint(0xc04c4be0, 0x10e0)`. The prediction was written from
`cloneproc`'s name ("clone the bootstrap process") rather than from the order of the two ASTs, which is
the same slip as 509's `_done_current` and 510's `_before`: a prediction from a function's name instead
of from its callers.

**What the correction buys is that the step's own object is now measured rather than assumed.**
`xnu_live_ast_calls = 2` is what makes the two calls visible as two; `_pid` on each side of them is what
separates "the thread the AST is delivered on" from "the process the work is done for"; and
`_thread = 0xc04c4be0` on `_seq = 2` is what joins the exec's decision to the process's first two faults.

## The image

  - **No fixture change and no kernel change.** One flag, one wrapper, one writer pair, one print.
    `.text` 5288480 → **5289056** (+576: the wrapper, its two record calls and the format string), the
    entry image's file is **5503612 bytes** again, `__bss_start 0x8053fa80` with **362896** bytes to
    0x80598410 — **exactly +8 on 510's 362888**, which is the two words this step adds (`g_ast_calls`
    and the wrapper's `ast_printed` static) and is the one row of this build that closes to the byte —
    **27 symbols undefined**, and the wrap census is **69 wrapped symbols: 57 reached by a branch, 1
    same-object-only** (`_ZN9IOService12matchPassiveEP12OSDictionaryj`), **1 never called here**
    (`sleep`), **10 by address only** (`vcputc getpid mmap poll open read fork exit wait4
    thread_quantum_expire`). `bsd_ast` is in the first category, which is the census's own confirmation
    of the call-site clause. **The `.text` after-value and the delta in this bullet were corrected by
    512**, which read 5288960 and +480 here for two steps: this build's own log prints `text size
    5289056 bytes (.text)` (`/tmp/511-run2-build.log:863`) and 510's prints 5288480, so the row was 96
    low. The before-value was a reading and the after-value was not, which is why they were not checked
    against each other at the time.
  - the wrapper is at **`0x8047b324`** (`__wrap_bsd_ast`), delivered from `ast_taken_user + 0xc4`
    (`0x80116500`).
  - the six files this step is: `stages/stage90/xnu_arm_boot/entry_trace.c` (the wrapper),
    `stages/stage90/xnu_arm_boot/entry_stubs.c` (the two writers and their keys),
    `stages/stage90/xnu_arm_boot/build_entry.sh` (the flag, the clause, the wrap-list count 68 → 69),
    `stages/stage90/run_and_capture.sh` (the capture fix below), and this document. No header change:
    `SS_PC`, `SS_SP`, `SS_CPSR` and `ACT_PCBDATA` are all already there and already checked.
  - the payload, from the build that ran (run 2): `stage90.bin` 5999040 sha256
    `faf96e9b530393cbef78902b1be525a00f57c9c6cd6502f1d41695d53637a9ad`, `stage90.img` 6002688 sha256
    `d10a9114c31ad0105dc78eab54ef21e0ac5895015c0cf56f22889113f1602731`, `stage90-qcdt.img` 8523776
    sha256 `a9bf7a4a337eeabe302b56df3353bab28736cafd537741bd9510ef2fd496ed10`. Run 1's three are
    `dc56fbeedee4e4de18796b31a5c8bc63184c2536c34b30564ee48c2b4e57d431` /
    `87252f8ebbdf38cf715034235fb65dac68ce832aad62f7efeb93283f4090789d` /
    `0e3947de062eec862a056b9db54e1fcf68fd4c53defb1f2849c2ebcf0eac4dae`.
  - **three build/run observations that are not about the image, recorded because they cost time and
    would cost it again:**
    - **Two background builds of this same tree died silently, at two different checks.** `nohup
      /tmp/stage90-511-rebuild.sh … &` ended after `xnu_entry_510`'s clause with the new 511 clause's
      failure sentence, and a second such run ended immediately after
      `check_os_entry.py --selftest`, with no message at all — which is `set -euo pipefail` doing what
      it does to a command that exits non-zero without printing. **Every one of those commands passes
      when re-run by hand**: the clause's own pipeline matched 200 times out of 200 in a loop, and
      `check_fault_recovery.py --verbose` exited 0 in 30 runs out of 30. The same tree built to
      completion in the foreground, twice. The cause is **not established** and is not claimed; what is
      usable is the practice — this build belongs in the foreground, and a background run's failure
      needs the command re-run by hand before it is believed.
    - **The 511 clause's own reader had a pipe in it, and the pipe is this file's oldest recorded
      defect.** `set -o pipefail` (line 25) turns `objdump | grep -q` into a pipeline whose left side
      can die of SIGPIPE the moment `grep` exits on its match, reporting a successful match as exit
      141 — the failure mode `build_entry.sh` already carries notes about twice. The reader now
      captures the disassembly into a variable first, so the status is `grep`'s and nothing else's.
    - **`run_and_capture.sh` could not write its own log, after the boot, with the run already spent.**
      `/tmp/cancro-last_kmsg.txt` was owned by the invoking user (a hand re-capture with
      `sudo adb … > /tmp/…` does that: the *shell* creates the file), and root's `O_CREAT` on an
      existing file in a sticky `/tmp` is refused — the signature of `fs.protected_regular` with a
      container root that has no `CAP_FOWNER`, evidenced by three commands: root's create on the
      existing file fails, root's `rm -f` on it succeeds, root's create of a non-existent file succeeds.
      The script now removes the log before the redirect; verified. Run 1's log was recovered by hand
      from `/proc/last_kmsg` immediately afterwards (the device had not taken another boot), and was
      then **overwritten by run 2** — the convention is one path, and this step did not copy it aside
      first. Its readings are quoted in this document as they were read; run 2 is the run the table
      cites.

## What is owed

  - **The route is measured from the kernel's side and the last instruction is still an inference.**
    `_seq = 2`'s `_after = 0x10e0`, `_sp`, and `_cpsr` are the state `load_and_go_user` restores, and
    the process faults at `0x1118` a few instructions later on the same thread and stack — but the
    `rfefd`/restore itself is in `locore.s` and nothing records the instant of it. A reading taken
    *after* the return would need a hook on the user side, which is the fixture's own instruction at
    `0x10e0` and is not instrumentable without changing the program.
  - **The first AST's `_thread = 0xc0485d20` has not been identified beyond `_pid = 0`.** The step
    measures that it is a `kernproc` thread with a zeroed saved state and that the exec happens inside
    it; it does not say *which* kernel thread, or why an `AST_BSD` was pending on it. `bsd_utaskbootstrap`
    sets the AST on the thread `cloneproc` returns, and the record says the first delivery is somewhere
    else — the caller (`ast_taken_user + 0xc4`) is the same for both calls and does not separate them.
  - **`_cpsr` is the same `0x10` on both calls and on both sides of both calls.** That is the mode
    `load_and_go_user` demands and the value `machine_thread_set_state` leaves (the host check's line:
    "machine_thread_set_state keeps only the flags from this word and takes the mode from
    PSR_USERDFLT (0x10)"), so it is consistent — and it also means the key has never once distinguished
    anything. It is worth a build clause rather than a run (the frame's `SS_CPSR` is checked against
    `assym.s`; the *value's* mode bits are not), the same shape as 510's `_entry_hi`.
  - **`xnu_live_ast_calls` is 2 in every run so far**, and the second call's AST has no name yet: 507's
    `SIGCHLD` is the obvious candidate for a third that never came, and nothing in this image records
    whether an `AST_BSD` was pending for the fixture's own syscall storm.
  - **A record that cannot be lost** (508's own defect, unchanged): the console's two-writer exposure
    dropped `xnu_live_wait_done_seq = 1`. This step's twenty records (ten per AST call) all landed, which is
    luck as much as design.
  - **The L2 descriptor for the fixture's page, read on both sides of a `copyout`** (508);
    **`p->p_xstat` published by nothing** (506/507/508), the report's empty `xnu_entry_why`, the
    `trap record:` gate, the watchdog as an ending, 505's corpse-path slot `0x802933b4`, 504's
    `xnu_live_mdevadd_base` as a page number / an arrival record at `mdevopen` / a second `read` at an
    offset, 503's leeway table row / the ~1.8 ms difference / a third ask, and 502's long list.
  - **And the telemetry copy loop**, named in 508 and unchanged here: this run's last record names both
    ends — `_pc = 0x80016574` = `Lcopyin_wordwise_loop`, `_lr = 0x800aad34` =
    `telemetry_take_sample + 0x158`, `_far_frame = 0`, with `_seen = 0x22` (34 faults seen) and
    `_redirected = 0x1a` (**26** of them converted to `EFAULT`) — the kernel's own copy of an address
    with no translation, planned and converted. **And it is on this step's own thread**: that record's
    `_thr` is `0xc04c4be0`, the thread pid 1's entry point was written to and the thread the user-mode
    faults were taken on, because `telemetry_take_sample` sits on the syscall path the fixture's own
    `getpid` loop drives. That is the one place in this image where the instrument's thread is not
    merely adjacent to the measurement but is the subject of it.

## Safety

Every device touch went through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both boots were non-persistent `fastboot boot`
and nothing was flashed. **Two runs**, because run 1's console line named the bootstrap thread instead
of pid 1; the image was rebuilt and the step run again. Run 1's boot returned on its own and the device
was on Android when the script's capture failed on the log file's permissions — the log was recovered
by hand while `/proc/last_kmsg` still held the payload's boot, which is the only reason its readings
are in this document at all; the script is fixed so the next step does not have to rely on that. Run 2
went through the whole script and ended on the hardware watchdog's 25 s timeout, as 507's through 510's
did, with the device back on Android by itself, `No errors detected`, and no stop of any kind: the
wrapper adds one line to the OS's console and reads three words of a thread's saved state on a path the
kernel was already on.
