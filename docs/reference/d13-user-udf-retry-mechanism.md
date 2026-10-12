# The user `udf` retry: why the kernel prints 236 identical dumps

Scope: after `launchd` starts, dyld executes an undefined instruction (`udf`). The kernel prints
the *same* "usermode undefined instruction" register dump 236 times in a row (993's boot;
`/tmp/cancro-last_kmsg.txt`). 991's earlier boot printed 231 identical `usermode data abort` dumps
at `0xffff4090` (`/tmp/cancro-last_kmsg-991.txt`). This note answers where the dump comes from,
what the kernel does after it, what bounds the run of dumps, whether the user-mode undefined path
is wired to a signal, and what would end the loop.

Read-only investigation. Every citation is `file:line` in the D13 build tree
`/mnt/data/mi4-ios6/external/xnu-hd2-darwin13/xnu` or the port layer `/mnt/data/mi4-ios6/src`.

---

## 1. Where the dump string comes from

`osfmk/arm/trap.c:906`, inside `void sleh_undef(arm_saved_state_t * state)` (`trap.c:802`):

```c
        printf
            (ANSI_COLOR_RED "%s[%d]: " ANSI_COLOR_GREEN "usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x%08x in map %p (pmap %p)" ANSI_COLOR_RESET "\n",
             proc_name_address(thread->task->bsd_info),
             proc_pid(thread->task->bsd_info), arm_ctx->pc, map, map->pmap);
        printf("Thread has ARM register state:\n" ... );
        printf("dyld_all_image_info_addr: 0x%08x   dyld_all_image_info_size: 0x%08x\n", ...);
```

991's twin is `osfmk/arm/trap.c:641` in `sleh_abort` (`trap.c:279`), the
`usermode data abort, EXC_BAD_ACCESS ... (Translation fault on page)` line.

Both are reached from the high-vector first-level handler. In this port the user branch of slot 1
branches to Apple's own `undef_from_user` (`src/entry/entry_vectors.s:108-121`,
`vec_tramp_1_user_handler: .word locore_fleh_undef`), whose body in
`osfmk/arm/traps_lo.s:64-81` saves the interrupted state and ends:

```
    blx     _sleh_undef
    b       _thread_exception_return
```

So the *producer of the text* is the D13 kernel's own `sleh_undef`; the port only routes to it.

## 2. What the kernel does after printing: **(b), re-execute the same pc** — it is not (a) and not (c)

It is not a panic: 993's log ends with `No errors detected` (i.e. no panic record) after the 236th
dump. It is not SIGILL-and-die either. The path is the Mach exception machinery, and it fails
closed into a retry:

1. `sleh_undef` sets `exception_type = EXC_BAD_INSTRUCTION` (`trap.c:935-937`) and, because it is
   nonzero, calls `doexception(exception_type, exception_subcode, 0)` (`trap.c:948`;
   `doexception` at `trap.c:132`) → `exception_triage(exc, codes, 2)`.
2. `exception_triage` (`osfmk/kern/exception.c:301`) tries three levels:
   `exception_deliver` at thread (`:322`), task (`:332`) and host (`:342`).
3. `exception_deliver` (`exception.c:126`) returns `KERN_FAILURE` immediately when the level's port
   is invalid: `if (!IP_VALID(exc_port)) { ... return KERN_FAILURE; }` (`exception.c:153-156`).
   Every level's port is `IP_NULL`: `thread`/`task` `exc_actions` start `IP_NULL`, and the host's
   are explicitly set so at `osfmk/kern/ipc_host.c:133` (`realhost.exc_actions[i].port = IP_NULL`).
   `bsdinit_task`'s host exception-port registration (`bsd/kern/bsd_init.c:1016`,
   `host_set_exception_ports(..., ux_exception_port, ...)`) installs a port that is *not* a real
   receive right in this image, so delivery still fails.
4. Nobody handled it, so `exception_triage` does
   `(void) task_terminate(task);` (`exception.c:349`) — **and that call does nothing**:

   ```c
   kern_return_t
   task_terminate(task_t task)
   {
       if (task == TASK_NULL)
           return (KERN_INVALID_ARGUMENT);

       if (task->bsd_info)          /* osfmk/kern/task.c:686 */
           return (KERN_FAILURE);

       return (task_terminate_internal(task));
   }
   ```

   Every BSD process's task has `bsd_info` set — `set_bsdtask_info(child_task, child_proc)` at
   `bsd/kern/kern_fork.c:789` — so `task_terminate` returns `KERN_FAILURE` without terminating
   anything. This guard is **genuine Apple code**, not a fork edit: it is byte-identical in
   `external/xnu-upstream/osfmk/kern/task.c:658` and `external/xnu-2050.18.24:656`.
5. Control falls through to `thread_exception_return();` (`exception.c:353`). In
   `osfmk/arm/cswitch.s:144-180` this restores `sp` from `TH_PCB_USS`, reloads `spsr` and the saved
   `pc` (`ldr lr, [sp, #0x3C]`), and returns with `movs pc, lr` — **to the unchanged faulting pc**.

Nothing on this path ever writes the saved frame: `sleh_undef` only reads it, `exception_deliver`
never calls `thread_setstatus` because it never reaches a behavior switch, and `task_terminate`
refuses. So the `udf` is re-executed, the same undefined-instruction exception fires again with
byte-identical registers, and the dump repeats. **Infinite retry, no SIGILL, no `sendsig`, no
panic.**

## 3. What bounds the run of dumps at ~230: **the 128 KB OS-console capture block, not a counter**

The bound is *output*, not control flow. The loop above is unbounded; what stops at ~230 is the
port's ability to *print*, because its console sink silently drops every character once the
reserved block is full.

- `src/entry/entry_stubs.c:2506`: `#define ENTRY_OS_BLOCK 0x00020000u /* 128 KB */`.
  `entry_os_reserve()` (`entry_stubs.c:2675`) publishes `size + ENTRY_OS_BLOCK`, space-fills the
  block, and writes marker `"\n[os-console-459]\n"`.
- `entry_os_console_char()` (`entry_stubs.c:2713`) drops silently at the end:

  ```c
  if (g_os_at >= g_os_end) {
      if (g_os_limited == 0u) {
          g_os_limited = 1u;
          entry_write_kv("xnu_live_ostext_limited", g_os_limited);
          entry_write_kv("xnu_live_ostext_chars", g_os_chars);
          entry_write_kv("xnu_live_ostext_lines", g_os_lines);
      }
      return;
  }
  ```

**Byte proof that the bound is the block and not a fixed count.** The two runs have *different*
dump counts (236 vs 231) and *different* per-dump sizes, but the *same* total budget:

- 993: consecutive `usermode undefined instruction` headers are spaced exactly **530** bytes
  (235 gaps × 530 = 124550).
- 991: consecutive `usermode data abort` headers are spaced exactly **542** bytes
  (230 gaps × 542 = 124660).
- 542 − 530 = **12**, exactly the length difference of the two rendered header texts: the abort
  line carries the extra ` (Translation fault on page)` suffix (`trap.c:641`) that the undefined
  line (`trap.c:906`) lacks. Same block, same dump shape, only the header width differs — so the
  *count* moves while the *bytes* hold.

Log-side confirmation: the block's own counters stop at the ceiling —
`xnu_live_ostext_total=0x0001ffee` (= 131054 = 0x20000 − 18-byte marker),
`xnu_live_ostext_chars=0x0001ffef` (131055), and `xnu_live_ostext_limited=0x00000001` is written
once, at the instant of the first drop. `xnu_live_ostext_heals=0x00000080` = 128, one heal per
kilobyte of text (the heal re-raises the size field; `entry_stubs.c:2779-2800`). Truncation
artifacts agree: 993's 236th dump is cut mid-line (`Thread has A`) and 991's 231st is cut at
`  cpsr: 0x`, each immediately followed by `xnu_live_ostext_at=0x00048296`. The live-channel cap
(`ENTRY_LIVE_CAP` = 16384 records, `entry_stubs.c:2284`; `xnu_live_capped=0x00004000` at
`entry_stubs.c:2473-2475`) is *not* what binds here: the log's tail region above the block holds
16773 lines of storage records and the `xnu_live_capped` record appears among them, well after the
dumps have stopped.

The 5-dump difference between 991 and 993 is therefore not a counter going off by five; it is
`floor(budget / per-dump-size)` landing on a different integer because the header text changed.

## 4. Is the user-mode undefined path wired to deliver a signal? **No — and the port does not resume either**

The signal path and the exception path are separate in XNU. `threadsignal`
(`bsd/kern/kern_sig.c:1666`) and `sendsig` (`kern_sig.c:2846`) are BSD; `bsd_exception`
(`osfmk/kern/exception.c` `bsd_exception`) is only reachable from `do_bsdexception`
(`bsd/kern/kern_sig.c:3080`, `return(bsd_exception(exc, codes, 2));`), and `do_bsdexception` is
called from exactly one place, `kern_sig.c:2343`, on the software-signal route. The undefined-
instruction exception here never enters `do_bsdexception`: it goes `doexception` →
`exception_triage` (Mach), which is the path in §2. So no `SIGILL` is generated for this fault in
this build.

The port's entry layer does **not** intercept-and-resume. It delegates the user case to the
kernel's own handler (`entry_vectors.s:108-121`) and *wraps* the second-level C handler only for
instrumentation: `build_entry.sh:197` adds `--wrap=sleh_abort --wrap=sleh_undef`, and
`__wrap_sleh_undef` (`src/entry/entry_trace.c:5477`) records counters and the saved pc/lr/cpsr,
then calls `__real_sleh_undef(regs, vfp_ss)`. It never mutates the frame, never advances the pc and
never terminates the thread. `__wrap_sleh_abort` (`entry_trace.c:5603`) is the same shape.

One observation that is *not* explained by this note and is recorded rather than claimed: the
`xnu_live_undef_seq` / `xnu_live_undef_user_seq` records are **absent from both logs**, and
`xnu_live_sleh_seq` maxes at 3, even though `__wrap_sleh_undef` is provably in the linked image and
on the call path (`out/stage90/xnu_arm_entry.elf`: `8049ddd0 T __wrap_sleh_undef`,
`8001af44: bl 8049ddd0 <__wrap_sleh_undef>` inside `undef_from_user`). The wrap binding and the
console text therefore disagree, and that disagreement is a separate thread to pull; it does not
change the mechanism above, which is read from the kernel source and corroborated by the byte
accounting in §3.

## 5. Verdict: why 236 identical dumps, and what would make the process actually die

**Why 236 identical dumps.** One undefined instruction at `pc = 0x2be7e0c4` (993) /
one data abort at `0xffff4090` (991) is retried forever. Each iteration re-executes the faulting
instruction, re-enters `sleh_undef`/`sleh_abort`, prints the same dump, fails to deliver the
exception at all three port levels, calls `task_terminate` which refuses because the task has
`bsd_info`, and returns to the same pc via `thread_exception_return`. The only thing that changes
across iterations is the port's console write pointer; when the 128 KB `ENTRY_OS_BLOCK` fills, the
sink drops the text silently and the log goes quiet while the CPU keeps spinning.

**What would make the process die (and expose the next wall).** The loop ends only if the exception
is actually *handled*, or the retry is broken at the source:

- Register a real Mach exception port at thread/task/host level (e.g. a crash reporter, or a
  working `ux_exception_port` receive right) so `exception_deliver` returns `KERN_SUCCESS` and
  `exception_triage` takes the `goto out` instead of falling into `task_terminate`. `ux_exception`
  would then convert `EXC_BAD_INSTRUCTION` to `SIGILL` (`bsd/uxkern/ux_exception.c:451-452`) and
  `threadsignal` it — the process dies by signal.
- Make `task_terminate` actually terminate the initproc task (bypass the `bsd_info` guard for this
  case, or call `task_terminate_internal` directly). If pid 1 really exits, `kern_exit.c:372`
  prints `pid 1 exited (signal %d, exit %d)` and panics with `State at Last Exception:` +
  `init_task_failure_data` — i.e. the next wall becomes visible as a panic rather than a silent
  spin.
- Break the retry in the handler itself: advance the saved pc past the `udf`, or force a signal
  from `sleh_undef`. The D13 tree already *contains* exactly this machinery for this exact
  signature — the `BOARD_CONFIG_QSD8250_LEO` dyld witness (`trap.c:74-125`) matches pid 1,
  `launchd`, `all_image_info_size == 0xa4`, the `udf` encoding and the dyld-relative pc/r0, and
  `ios7leo_dyld_repeat` (`trap.c:95`) suppresses the repeated dump. It is compiled **out** in this
  build (the build only defines `-DBOARD_CONFIG_MSM8974_CANCRO=1`), so the log carries no
  `Leo DYLD witness` line. Restoring that gate would collapse the 236 dumps to one and let the
  first fault's identity be read.

**Secondary finding worth carrying forward.** `sleh_undef`'s very first check returns *without any
exception* for any instruction that is not the dyld `udf`:

```c
        if(instruction != 0xfedeffe7)
        {
            /* NEON instruction. */
            ...
            return;              /* osfmk/arm/trap.c:877-899 */
        }
```

So an undefined user instruction other than `0xfedeffe7` is retried **silently** — no dump at all,
an infinite loop with no output. A future boot that "hangs with no print" in userspace should be
checked against this branch before anything else.

---

### Key citations

| Fact | Citation |
| --- | --- |
| Undefined dump producer | `osfmk/arm/trap.c:906` (fn `sleh_undef` at `:802`) |
| Data-abort dump producer | `osfmk/arm/trap.c:641` (fn `sleh_abort` at `:279`) |
| Silent NEON/other-instruction retry | `osfmk/arm/trap.c:877-899` |
| `doexception` call | `osfmk/arm/trap.c:948`; `doexception` at `:132` |
| Triage, three levels | `osfmk/kern/exception.c:301`, `:322`, `:332`, `:342` |
| Deliver refuses on invalid port | `osfmk/kern/exception.c:126`, `:153-156` |
| Host ports default `IP_NULL` | `osfmk/kern/ipc_host.c:133` |
| `task_terminate` refuses on `bsd_info` | `osfmk/kern/task.c:686` (same in `xnu-upstream:658`, `xnu-2050.18.24:656`) |
| Every BSD task gets `bsd_info` | `bsd/kern/kern_fork.c:789` |
| `thread_exception_return` restores unchanged pc | `osfmk/arm/cswitch.s:144-180` |
| User `udf` reaches kernel handler | `osfmk/arm/traps_lo.s:64-81`; `src/entry/entry_vectors.s:108-121` |
| Console block and silent drop | `src/entry/entry_stubs.c:2506`, `:2675`, `:2713`, `:2762-2770` |
| Live-channel cap | `src/entry/entry_stubs.c:2284`, `:2473-2475` |
| Port wraps only (no resume) | `src/entry/build_entry.sh:197`; `src/entry/entry_trace.c:5477`, `:5603` |
| Signal path is separate | `bsd/kern/kern_sig.c:1666`, `:2846`, `:3080`, `:2343`; `bsd/uxkern/ux_exception.c:451-452` |
| initproc exit panics | `bsd/kern/kern_exit.c:372` |
| LEO dyld witness (compiled out) | `osfmk/arm/trap.c:74-125`, `:900-901`, `:944` |
