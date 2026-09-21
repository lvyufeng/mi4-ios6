# Experiment 509 — the OS's own console says its init image loaded

**One line.** `load_init_program` is wrapped, and the console capture every run in this walk has ended
with now reads one line longer:

    load_init_program: attempting to load /sbin/launchd
    mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80048eb8)

The wrapper is a *reading* rather than a trace because of the function's own shape: `void
load_init_program(proc_t p)` returns on every arm that loaded an image (`if (!error) return;`) and its
only other exit is `panic("Process 1 exec of %s failed, errno %d")` (`kern_exec.c:5168`) — so code
written after the call runs **if and only if** the OS loaded its init image. Beside the print are three
live records: `xnu_live_exec_seq = 1`, `_caller = 0x80048eb8` (that is `bsdinit_task + 0x88`, the OS's
own call site, read out of the linked instructions by the build), `_who = 1`, `_proc = 0xc06acb80`, and
after it `xnu_live_exec_done_seq = 1`, `_done_who = 1`, **`_done_current = 0`**. That last `0` is the
one prediction this step got wrong and the run's finding, recorded below. Nothing else in the boot
moved: no `stub_hit`, no `undef`, no `osr`, no `panic`, no trap record, no report — and the fixture
still ran its whole program (`getpid` answer 1, the fork, the child's `exit`, `SIGCHLD`, the reap,
`xnu_live_wait_status = 0x00000300`, `_wait_error = 0x0a`).

    bash stages/stage90/xnu_arm_boot/build_entry.sh          # prints xnu_entry_509's clause
    python3 tools/host_ramdisk_macho_check.py --selftest out/stage90/xnu_arm_entry.elf  # all 128 refused
    python3 tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf
    python3 tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 30 refused
    python3 tools/check_experiment_index.py                  # 486 rows

## What the step was for

Every console capture this project has taken ends with the same line and nothing after it. 489 settled
what that silence *is* — `load_init_program` prints on each attempt and each failure and on nothing
else, so a boot that stops printing names has loaded its init — and named four readings that say the
boot reached userspace: `lmf_ret = 0` from `load_machfile`'s wrapper, the five `tail_seq` records
emitted from inside `__wrap_vm_pageout` (which only run after `bsd_init` returned), the fixture's own
`getpid` answer, and its `mmap` result. All four are correct, and all four are **inferences from
elsewhere**: the one artifact a reader actually looks at — the OS's console — kept saying the boot
stopped. This step makes the console say the rest, in the same sink, on the same path, from the same
task.

**The sink is `printf` and it is the OS's own.** `load_init_program`'s body calls the real
`printf` (`osfmk/kern/printf.c`, `bl 800399b4 <printf>` at four sites in the linked image, one
immediately before each `load_init_program_at_path`), and that is the function that produced every line
of the captured block. `IOLog` is deliberately not used although it is also linked: `_IOLogv` opens
with `assertf(ml_get_interrupts_enabled() || ...)` (`IOLib.cpp:1184`), and an assertion that fires here
is a panic rather than a missing line.

**The wrapper is around the loader and not around `execve`**, which is worth a paragraph because the
first design of this step made the other choice and would not have worked. `execve` and
`load_init_program_at_path` both call `__mac_execve`, and `__mac_execve` is defined **in
`bsd_kern_kern_exec.o`, which also holds both of its callers** — the shape 455 measured and paid for: a
`--wrap` rewrites an *undefined* reference, so a wrapper whose callers are all in the object that
defines the symbol links, defines its symbol, never runs, and leaves a run that looks exactly like one
where the measured thing did not happen. `load_init_program`'s single call site is
`bl 80291114 <load_init_program>` at `0x80048eb4` inside `bsdinit_task` (`bsd_kern_bsd_init.o`) — **a
different object from the `bsd_kern_kern_exec.o` that defines it** — and `bsdinit_task` is the chain
link 489 named by reading source. The build now reads that link out of the image instead of trusting
the flag list.

## What the build checks

Three properties, and the second and third are the ones that make the step a reading:

  - **membership** — neither `load_init_program` nor `printf` is in pass 1's undefined set, so the
    loader the boot thread runs is XNU's own and the print goes to the OS's own console. A stand-in for
    either would make the record and the line facts about the stub.
  - **the loader's failure arm is a panic** — `bl $panic <panic>` inside `load_init_program`'s own
    instruction range (`0x80291114..0x802912ec`, read with `sym_addr`/`sym_next`), because a loader that
    fell through to a `return` on failure would turn `xnu_live_exec_done_seq` into a record that says
    nothing at all.
  - **the call site goes to the wrapper** — `bl <__wrap_load_init_program>` inside `bsdinit_task`'s
    range (`0x80048e30..0x80048ed0`), which is the 455 hazard checked structurally, before a device run
    is spent on it.

The third clause's negative side was exercised by pointing the same grep at functions that do not call
the loader (`bsd_ast`, `bsd_init`): zero branches, against one in `bsdinit_task`. That is a test of the
reader rather than of the artifact — the artifact's own negative side is the *previous* build, whose
symbol table has no `__wrap_load_init_program` at all, which is what the `sym_addr` failure in the same
clause would report.

## The readings

Line numbers are `/tmp/cancro-last_kmsg.txt` from the second run (573221 bytes, 8371 lines):

| line | record | what it says |
|---|---|---|
| 3992-3993 | `load_init_program: attempting to load /usr/local/sbin/launchd.development` / `failed loading …: errno 2` | the `DEBUG`/`launchdsuffix` arm's first candidate, unchanged from 470 on |
| 3994 | `load_init_program: attempting to load /sbin/launchd` | the line every previous run in this walk ended on |
| **3995** | **`mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80048eb8)`** | **this step: the OS's console says the load returned — which, by the loader's own shape, means pid 1's image is in place** |
| 7578-7581 | `xnu_live_exec_seq = 1`, `_caller = 0x80048eb8`, `_who = 1`, `_proc = 0xc06acb80` | the record written **before** the loader runs: the caller is `bsdinit_task + 0x88`, the process is 1 |
| 7669-7671 | `xnu_live_exec_done_seq = 1`, `_done_who = 1`, **`_done_current = 0`** | the record written after it returned — reachable only on the success arm — with the loader's own process on the other side |
| 7809-7820 | `xnu_live_getpid_last = 1`, the `read` pair (`_word_before = 0x102000` → `_word_after = 0xfeedface`) | the fixture's program, running under the image this step's line names |
| 7857-7871, 7897-7914 | `sleh_seq = 7`, `_user = 1`, `_pc = 0x000011a4`; `sleh_seq = 8`, `_user = 0` | user mode, entered: the process the loader's record names is the one taking user-mode faults |
| 7885-7890 | `xnu_live_sigchld_seq = 1`, `_signal = 0x14`, `_to = 1` | 507's inter-process event, still there |
| 7931-7936 | `xnu_live_wait_done_seq = 2`, `_error = 0x0a`, `_status = 0x00000300` | 508's reap and composed status, still there |
| — | `xnu_live_stub_hit_*`, `xnu_live_undef_*`, `xnu_live_osr_*`, `xnu_entry_abort_entries`, `xnu_entry_panic_entered`, `trap record` | **all absent** — the print added nothing to the path, and the run ends as 507's and 508's did, on the hardware watchdog |

## Two readings this step's own prediction got wrong

### `_done_current` is 0, not 1 — the loader is not the process it loads for

The wrapper's comment predicted the pair would read "the same pid on both sides of an image
replacement", on the reasoning that after a successful exec the calling task's BSD info is the process
whose image was replaced. The run answers **`_who = 1`, `_done_current = 0`**, and the mechanism is the
one 507 measured for `psignal`'s sender, arriving from the other direction: `load_init_program` is
called by `bsdinit_task`, which runs on the **kernel** task, so `current_proc()` inside it is
`kernproc` — pid 0 (`bsd_init.c:476`, "implicitly bzero'ed") — while the process the image is being
loaded *for* is `initproc`, pid 1. The prediction was written from the name of the function ("the
loader loads its own process") rather than from the call graph, and the pair is what caught it: a
single key could not have.

**What it buys is a question the standing list already carries.** The image is loaded for process 1 by
a thread that is not process 1's, which is why the next unanswered item on this walk's list —
`thread_bootstrap_return`, the mechanism by which a newly-imaged process's thread actually begins
running at a user PC — is now the natural successor rather than a name on a page.

### The console line claimed a microsecond that had not happened yet, and the first run was thrown away

The first run's line read `mini4: the init image is loaded -- pid 1 is in user mode (loaded by
0x80048eb8)`. It is true of the run (the same log has pid 1 taking user-mode faults at
`0x1118`/`0x1124`), but at the instant of the print the wrapper has only measured that the *loader
returned* — user mode is the next event, in another record. A console line is the durable artifact of
this step and the one place a reader will look, so the wording was changed to what the wrapper
measures, the image was rebuilt, and the step was run a second time. Both runs' artifacts are recorded
below; the hashes differ by that string and by nothing else that this step can see (`.text`, image
size, `__bss_start`, the undefined count and the wrap census are the same in both builds).

## The image

  - **No fixture change and no kernel change.** One flag, one wrapper, one writer pair, one print.
    `.text` 5287616 → **5287968** (+352: the wrapper, its two record calls and the format string), the
    entry image's file is **5503612 bytes** again — the same number eight builds in a row —
    `__bss_start 0x8053fa80` with 362888 bytes to 0x80598408, **27 symbols undefined**, and the wrap
    census is **67 wrapped symbols: 55 reached by a branch, 1 same-object-only
    (`_ZN9IOService12matchPassiveEP12OSDictionaryj`), 1 never called here (`sleep`), 10 by address only**
    (`vcputc getpid mmap poll open read fork exit wait4 thread_quantum_expire`). `load_init_program` is
    in the first category, which is the census's own confirmation of the check above.
  - the wrapper is at **`0x8047b1ec`** (`__wrap_load_init_program`), and its body is `bl proc_pid` /
    `bl entry_note_exec` / `bl 80291114 <load_init_program>` / `bl current_proc` / guarded `bl proc_pid` /
    `bl entry_note_exec_returned` / `b 800399b4 <printf>` — the print is a **tail call** and that is
    safe here because the wrapper is `void` and both records are written before it (478's defect was a
    tail call that skipped the record, not a tail call as such).
  - the three files this step is: `stages/stage90/xnu_arm_boot/entry_trace.c` (the wrapper),
    `stages/stage90/xnu_arm_boot/entry_stubs.c` (the two writers and their keys),
    `stages/stage90/xnu_arm_boot/build_entry.sh` (the flag, the clause, and the wrap-list count 66 →
    67), and this document.
  - the payload, from the build that ran: `stage90.bin` 5999040 sha256
    `7c94e35ee82dc7fe8858b287b4ff3dbf783c7741f5625a6dec6c311c2dd799dd`, `stage90.img` 6002688 sha256
    `41dcef79d9c0ab8c5a77efaae47a67957b95bd4def831a0fb68d6aa109309599`, `stage90-qcdt.img` 8523776
    sha256 `92a1bdf4ebd8a9dbb7fd83f99abffcd1c76f42ec6d9925f12a4597b620b9b28e`. The first run's three are
    `8580b6803e4dd7ac5aa4096b8f139b9eacb1923c4716a37f42ea2c92ff250638` /
    `175d3f4c0b066b689d11e35ffc103dc8083e2e8ff0014822f8c7f5069ceba9b2` /
    `e1c833c929b4f3d9fdad6e8ea57e0a32b658b9e751d0e8d98e662ebd6e63d801`.
  - **one address reading worth keeping**: `xnu_live_exec_proc` is `0xc0546b80` in the first run and
    `0xc06acb80` in the second, and `_caller` is `0x80048eb8` in both. The *offset* (`bsdinit_task +
    0x88`) is the reading; a proc pointer is not comparable across runs and the caller is comparable
    only within a build.

## What is owed

  - **`thread_bootstrap_return`, and now it is the frontier rather than a name on the list.** This step
    measures that the init image is loaded for process 1 by a thread that is running in `kernproc`
    (`_done_current = 0`). What it does not measure is where the thread that will *run* pid 1 comes
    from, or at what PC it starts — `entry_rcu`/'`thread_bootstrap_return` are the standing names, and
    the fixture's own first user instruction is only visible today as the absence of a fault before
    `0x1118`. A reader of the init thread's saved state, taken after this wrapper returns, would make
    that PC a number instead of an inference.
  - **A record that cannot be lost** (508's own defect, unchanged): the console's two-writer exposure
    dropped `xnu_live_wait_done_seq = 1`. This step's records landed, and that is luck as much as
    design — the fix (claim the size field before filling it, or a cursor per writer) is still owed.
  - **The L2 descriptor for the fixture's page, read on both sides of a `copyout`** (508's other owed
    item): why `wait4`'s copyout took a write permission fault on a page the fixture's own `str` had
    already written.
  - **`_ticks` around a call that blocked** (508), **`p->p_xstat` published by nothing** (506/507/508),
    the report's empty `xnu_entry_why`, the `trap record:` gate, the watchdog as an ending, 505's
    corpse-path slot `0x802933b4`, 504's `xnu_live_mdevadd_base` as a page number / an arrival record at
    `mdevopen` / a second `read` at an offset, 503's leeway table row / the ~1.8 ms difference / a third
    ask, and 502's long list.
  - **And the telemetry copy loop**, named in 508 and unchanged here: 26 of this run's aborts are
    `telemetry_take_sample + 0x158` calling `copyin` with `far = 0`, every one armed and redirected.

## Safety

Every device touch went through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both boots were non-persistent `fastboot boot`
and nothing was flashed. **Two runs**, because the first one's console wording claimed more than the
wrapper measures; the second is the one this document reads. Both ended on the hardware watchdog's
25 s timeout — as 507's and 508's did — with the device back on Android by itself, `No errors
detected`, and no stop of any kind: the print added one line to the OS's console and no new call on any
path the boot depends on.
