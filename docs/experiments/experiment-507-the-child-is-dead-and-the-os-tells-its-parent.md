# Experiment 507 — the child's death is finished, and the OS tells its parent

**One line.** `pth_proc_hashdelete` — the slot 506's run stopped in — now has a body, the child's `exit`
ran the whole of `proc_exit`, and the two records that have never appeared in any log in this walk
appeared together: `xnu_live_pth_delete_seq = 1` for the child's proc `0xc05b3e38`, and then
**`psignal(pp, SIGCHLD)`** — `xnu_live_sigchld_seq = 1`, **`_signal = 0x14` = 20**, `_to = 1`,
`xnu_live_psignal_calls = 1`, and the returned pair after the real call. **There is no `stub_hit` in the
run at all, no `panic`, no abort and no `undef`**: the child became a zombie, its parent — process 1, the
same process the fixture runs — was told, and the parent never left its loop (`getpid_count` reached
`0x800000` over 23 records, `_last = 1` at every one). This is the first inter-process event in the
walk, and it is also **the first run that ends by the hardware watchdog rather than by a stop**: with no
stand-in left on this path there is nothing to stop it, so it ran to the 25 s timeout and the device
returned on its own. Two readings corrected this step's own prediction, both recorded below: the source
distance from the retired slot to the `psignal` is **338** lines and not the 275 four documents said,
and `xnu_live_sigchld_from` is **0**, not the dying child's pid.

    python3 tools/check_pthread_table_slots.py --selftest                            # all 7 refused
    python3 tools/check_pthread_table_slots.py                                       # 507's new clause
    python3 tools/host_ramdisk_macho_check.py --selftest out/stage90/xnu_arm_entry.elf  # all 111 refused
    python3 tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 48
    python3 tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf
    python3 tools/check_timer_line.py --selftest --verbose                        # all 20 refused
    python3 tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 30 refused
    python3 tools/check_irq_routing.py --image out/stage90/xnu_arm_entry.elf --selftest # all 57 refused
    python3 tools/check_driver_catalogue.py --selftest                            # all 131 refused
    python3 tools/check_experiment_index.py                                       # 484 rows

## What the step was for

506's run ended in `proc_exit`, at the first call on the exit path this image answered with a stand-in:

    xnu_live_stub_hit_seq = 1
    xnu_live_stub_hit_name_ptr = 0x804c157a          <- "stage90_pthread_functions.pth_proc_hashdelete"
    xnu_live_stub_hit_caller   = 0x80294150          <- proc_exit + 0x188

A stand-in hit is terminal (`entry_stub_hit` ends in `entry_epilogue`, `entry_stubs.c:6107`), so the
child's exit stopped 338 source lines short of `psignal(pp, SIGCHLD)` (`kern_exit.c:1443`) — the record
505's document named as "the first inter-process event in this walk" and 506's as the thing it could not
buy. 507 buys it, and it is one function in `xnu_supply` with no change to the fixture or to the kernel.

## The body

`pth_proc_hashdelete` is the fifth slot this image fills by hand, after `pthread_init` (433),
`pth_proc_hashinit` (465) and the two workqueue slots (473). The real one frees the per-process psynch
hash `pth_proc_hashinit` built — the storage `p->p_pthhash` points at — and **465's body left
`p->p_pthhash` NULL**, which is exactly the state a delete would find nothing to free in. So the honest
implementation of "there is no psynch hash for this proc to drop" is a body that records the call and
returns, which is what 433, 465 and 473 each did for their own slot:

```c
static void
stage90_pthread_slot_pth_proc_hashdelete(proc_t p)
{
    stage90_pth_delete_calls++;
    entry_note_live("xnu_live_pth_delete_seq", stage90_pth_delete_calls);
    entry_note_live("xnu_live_pth_delete_p", (uint32_t)(uintptr_t)p);
    entry_note_live("xnu_live_pth_delete_tbl", (uint32_t)(uintptr_t)pthread_functions);
    entry_kv("xnu_entry_pth_delete_p", (uint32_t)(uintptr_t)p);
}
```

It does not dereference `p` (433's rule: a NULL or wrong pointer must be a stop that names itself, not a
fault) and does not reach into the callbacks table. Like `workqueue_mark_exiting`, its shim is a *tail
branch* — `pthread_shims.c:364` is `pthread_functions->pth_proc_hashdelete(p);`, emitted as the
`ldr r1, [r1, #36]` / `bx r1` at `0x80205d0c..0x80205d1c` — so the body is entered with `proc_exit`'s
return address in the link register, which is why 506's record names `proc_exit + 0x188` and not a shim.

### What the check had to grow to see this

**The existing check could not tell a body from a stand-in, and this step is the first one where that
mattered.** `check_pthread_table_slots.py` compares each of the table's 40 named words in the image with
the symbol the member's name implies — and `stage90_pthread_slot_<member>` is the name the macro gives a
stand-in *and* the name a hand-written body is given, so a slot retired by a body and a slot still
pointing at a stand-in compare equal. The two are told apart by the **object's own literals**, which is
one fact about each kind:

  - a stand-in is defined by the name it hands to `entry_stub_hit` — `STAGE90_PTHREAD_SLOT_DEF` pastes
    `"stage90_pthread_functions." #name`, so there is one such literal per unretired member;
  - a body is defined by the live key it publishes — `xnu_live_pth_hashinit_seq` and its siblings — and a
    stand-in has none.

So the new clause is a **set equality against Apple's own member list**: the object's stand-in name
literals are exactly the members that have no body, and every member that has one carries its live key.
The only thing the check is told is which members those are (a `BODIES` table of five member/key pairs);
both sides are read out of the artifacts. It refuses two failures that were previously invisible — a body
written below the table but left in the macro list (today a compile error only by luck of the duplicate
symbol), and a body whose key is spelled differently from the key the log is searched for.

It was tested against the state it was written for *before* the rebuild: run against the 506 object it
fails with both sentences, `the object carries the stand-in literal ... pth_proc_hashdelete` and `no
'xnu_live_pth_delete_seq' literal in the object`. Its `--selftest` is 7 mutations now — the four table
mutations from 473, plus three on a copy of the object's bytes: a body's live key literal destroyed, a
body's stand-in name planted over another slot's, and an unretired slot's stand-in name destroyed.

### A check whose failure path had never run

The first run of that clause reported the failure as a Python traceback, not as the sentence. The cause
was one line in the checker that had been there since 473:

```python
            say("FAIL: %s" % problem, file=sys.stderr)      # `say` takes no `file=`
```

The build stops either way — an uncaught exception exits 1 — but on a traceback, with the finding
nowhere in it. It survived because **the check had never failed**: every slot it compared had been
right since the day it was written, so the one line that reports a problem was the one line nothing had
ever executed. It is the same shape as 500's accepted mutations and 501's needle in the prose, one level
further in: not a clause that never matched, but a *report* that never ran.

## The readings

In the order the log writes them (line numbers are `/tmp/cancro-last_kmsg.txt`, 8328 lines):

| line | record | what it says |
|---|---|---|
| 3994 | `load_init_program: attempting to load /sbin/launchd` | the console block's last line, as in 505 and 506 — no `failed loading` after it, and **no `pid 1 exited` anywhere in the run** |
| 7827 | `xnu_live_pth_hashinit_seq = 2`, `p = 0xc05b3e38` | 465's slot, on the child's proc, inside `forkproc` |
| 7833 | `xnu_live_fork_seq = 1`, `uap0 = 0x000011e0`, `error = 0`, `ret_lo = 2`, `ret_hi = 0` | the parent's answer, as 505 and 506 measured it |
| 7918-7934 | `sleh_seq = 8`: `type = 3`, `far = pc = 0x000011a4`, `sp = 0x00101efc`, `cpsr = 0x40000010`, `user = 1` | **the child's first dispatch**, its inherited map faulting on the word after the fork's `svc` |
| 7935 | `xnu_live_exit_seq = 1`, `_caller = 0x80285898`, `_pid = 2`, `_rval = 3` | the child's own arm, unchanged from 506 — with a **different address for the same call site** (below) |
| 7939-7944 | `xnu_live_pth_wqmark_seq = 2` / `_wqexit_seq = 2`, `p = 0xc05b3e38`, `tbl = 0x804c1168` | 473's pair, on the child's own proc, in `proc_exit` |
| 7945 | `xnu_live_pth_delete_seq = 1`, `_p = 0xc05b3e38`, `_tbl = 0x804c1168` | **this step**: the slot 506 stopped in, entered from `proc_exit + 0x188`, recording the same proc 473's pair did |
| 7948-7954 | `xnu_live_sigchld_seq = 1`, `_from = 0`, **`_to = 1`**, **`_signal = 0x14`**, `xnu_live_psignal_calls = 1`, `_returned_from = 0`, `_returned_to = 1` | **the OS told process 1 that process 2 is gone** — `SIGCHLD` = 20 (`bsd/sys/signal.h:108`), the first inter-process event in this walk, and the real `psignal` returned |
| 7955-9827 | `xnu_live_getpid_count` `0x2000 … 0x800000` (23 records, `_last = 1`), `tmr_qexp_seq` to `0x0b`, `tmr_dl_seq` to `0x17`, `timerdrv_fires = 2`, `irq_timer_count = 0x10`, `sleh_armed = 0x1c` / `seen = 0x21` / `redirected = 0x1a` | the parent's loop, still being answered 1, for the rest of the run |
| — | `xnu_live_stub_hit_*`, `xnu_live_undef_*`, `xnu_live_osr_*`, `xnu_live_getpid_change_*`, `xnu_entry_abort_entries`, `xnu_entry_panic_entered`, `MI4IOS6_STAGE90_XNU real XNU entry`, `trap record` | **all absent.** No stop, no fault, no panic, and no report at all: with nothing left to stop the boot, the epilogue never ran |

The last row is the new shape. Every previous run in this walk ended in a stop (a `stub_hit`, a trap, a
panic, or the ladder's own terminal), so the log's tail was the epilogue's report — `xnu_entry_why`,
`kv_written`, the trap buffer. This run has none of that: it ends mid-sentence with the parent's loop and
the timer's records, and the log is 571870 bytes where 506's was 591886, because **the hardware watchdog
fired at its 25 s timeout** (`hw_watchdog_timeout_s = 0x19`) and the device returned to Android by
itself. That is the first run in this walk whose end is the watchdog's rather than a stop's, and it is
what a boot looks like when the boot path runs out of missing pieces.

## Two readings this step's own prediction got wrong

### `_from` is 0, not the dying child's pid

The 507 header in `stage90_pthread_functions.c` predicted `xnu_live_sigchld_signal = 20 _to = 1
_from = 2`. `_to` and the signal number are right; `_from` is **0**, and the mechanism is in the code
above the call rather than in the wrapper:

```c
kern_exit.c:1370   p->task = TASK_NULL;
kern_exit.c:1371   set_bsdtask_info(task, NULL);         /* 72 source lines above the psignal */
kern_exit.c:1443           psignal(pp, SIGCHLD);
```

`current_proc()` is `get_bsdtask_info(current_task())`, and when that is NULL it does not return NULL —
its own comment says "Never returns a NULL" — it returns `kernproc` (`bsd/kern/bsd_stubs.c:104-105`),
whose pid is 0 (`kernproc = &proc0`, "implicitly bzero'ed", `bsd_init.c:476`). So at the moment of the
notification the dying process is no longer `current_proc()` at all: the first `psignal` in this boot
reports its sender as pid 0, and the wrapper's null guard (written for a *hypothetical* signal with no BSD
process behind it) fired for a real reason on its first use. The record is right, the prediction was
wrong, and the correction is the reading: **the `from` half of a `psignal` taken in `proc_exit` cannot
name the process that died** — the pid is on the wrapper's other side, in the `exit` record's `_pid = 2`.

### The distance is 338 lines, and four documents said 275

506's fixture header, its document, the README row and the memory index all say the `psignal` is "275
source lines below" the slot that stopped the boot. It is **338**: `pth_proc_hashdelete(p)` is
`kern_exit.c:1105` and `psignal(pp, SIGCHLD)` is `:1443`. The number was never computed from those two
line numbers — it was estimated once and then quoted, so all four places agreed with each other and none
agreed with the file. It is corrected in this step's commit and recorded in the defects ledger, and it is
the same defect 506 recorded in a different medium: a pair of numbers written from one derivation.

## The image

  - **No fixture change and no kernel change.** `.text` 5286944 -> **5287040** (+96: the body's four
    calls and its three key literals), and the entry image's file is 5503612 bytes again — the same
    number 502 through 506's five builds had; `__bss_start 0x8053fa80` with 362888 bytes to
    `0x80598408`; the wrap census is unchanged at **65 wrapped symbols: 54 reached by a branch, 1
    same-object-only, 1 never called here, 9 by address only**.
  - the table is at `0x804c1168` and the object's five bodies and 34 stand-ins are exactly the members
    the new clause expects: `xnu_entry_507: ... 5 of the 39 members have no
    'stage90_pthread_functions.<member>' name and carry their live key instead`.
  - the three files this step is: `stages/stage90/xnu_supply/stage90_pthread_functions.c` (the body, the
    section, and the count 35 -> 34), `tools/check_pthread_table_slots.py` (the clause, three object
    mutations, and the failure path that had never run), and this document. Everything written **after**
    the run — this section, the measured paragraph under the prediction, the 275 -> 338 correction in
    506's fixture header — is comments only, and that is proved rather than asserted: the rebuild's
    `stage90.img`, `stage90.bin` and `stage90-qcdt.img` compare byte-identical (`cmp`) to the three the
    device ran, so the artifact this document describes and the artifact the run measured are one file.
  - the payload: `stage90.bin` 5999040 sha256
    `4d869e7c3edf6279b9e5cadea7f1c25c2ccf9cf7bdfa65f3696b3ef329d9e398`, `stage90.img` 6002688 sha256
    `eb714d649b90d40d8283ac5adc38b50202168b6ac9ba0d34387a4b5492f56f06`, `stage90-qcdt.img` 8523776
    sha256 `481e2d7782ef94dbac9190bfdaf4b6759fb58cec0a8a89c75286969aa333626d`;
    `kernel_size=5999040 dt_size=2521088 page_size=2048`.

**One address reading worth keeping**: `xnu_live_exit_caller` is `0x80285898` in this run and
`0x80285848` in 506's, and both are `unix_syscall + 0x100` — `unix_syscall` itself moved from
`0x80285748` to `0x80285798` because 507's `.text` grows before it in link order. The offset is the
reading; the address is not comparable across builds. (503/504's defect ledger entry, which this run
would have re-earned had it compared the two numbers.)

## What is owed

  - **`wait`, which is the next step and closes the fixture's program.** The child is a zombie and the
    parent never asks for it; `wait4(2, &status, 0, NULL)` is `sysent[7]` and it is the third of the
    three calls a Unix process's life is: `fork`, `exit`, `wait`. It also buys the number 506's
    prediction could not name and this image publishes nowhere: **`status` is `W_EXITCODE(3, 0)` =
    `0x300`**, read out of the kernel's own answer by the process that asked. The fixture's program
    would then be complete — make a process, lose it, reap it — and the run would measure `wait1continue`
    → `reap_child_locked`, the free side of the corpse path 505 hit.
  - **`p->p_xstat`, the composed status, published by nothing.** The value is on the dying proc, which is
    not `current_proc()` at the `psignal` (above); the two routes are the wrapper's other side (the
    parent's `p_children` list, whose head is the zombie, with the offset derived rather than written) or
    `wait`'s own answer.
  - **The entry report's own debt, unchanged from 506**: `xnu_entry_why` came out as `0x1034e1a0` with an
    empty printed line although every `entry_epilogue` caller passes a literal, and both places it is
    read are after the epilogue turns the caches and the MMU off (`entry_stubs.c:3728-3746`). This run
    took the other branch of that question and cannot help: with no stop there is no report at all.
  - **The watchdog as an ending, now measured once.** 25 s, `hw_watchdog_timeout_s = 0x19`,
    `countdown_first = 0`, `countdown_second = 0xb6e`; the run's records reach `tmr_dl_now = 0x224f9797`
    (~574 M ticks at 19.2 MHz ≈ 30 s from whatever base the counter started at — a duration this step
    does not claim). A step that wants a *bounded* run without a stop would have to add a duration to the
    fixture; the log's own last records say when it ended, not why.
  - **From 506**: the report cannot name its own reason; the `trap record:` gate that disagreed with the
    report's own `panic_entered`; 505's "six quantum expiries" against 506's five; 505's corpse-path slot
    (`0x802933b4`), whose log no longer exists.
  - **Still owed from 504**: `xnu_live_mdevadd_base` as a page number, an arrival record at `mdevopen`,
    and a second `read` at an offset. **From 503**: which row of `timer_compute_leeway`'s table the
    `poll` thread took, the ~1.8 ms `microuptime`-vs-`mach_absolute_time` difference, and a third ask
    (5/40/400 ms). **From 502**: the frame's `0x038`/`0x03C` pair, intid 39, the `AckC`/EOI question,
    the registry's 4-slot capacity stop, claim 16's one-level derivation reader, the citation rule over
    the other cited files, the unstamped `out/xnu_asm_obj`, 497's conditional-clause mutation, the
    unregister guard's asymmetry, `/timer`'s second definition, the other device nodes,
    `MSM8974RootResource`'s `state0 = 0`, a name/class reader wider than eight characters, the release
    as a reading, `vm_fault`, 488's flag-list, 490's frames band, `xnu_live_dec_same`.

## Safety

Every device touch went through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; the boot was a non-persistent `fastboot boot` and
nothing was flashed. **One run.** It ended on the hardware watchdog's 25 s timeout — armed at
`0xf9017000`, `enabled = 1`, the payload's own dead-man timer beside it — and **not on a stop**, so the
epilogue never ran and the report does not exist; the device returned to Android on its own and the
capture is 571870 bytes over 8328 lines with `No errors detected`. Two processes were alive when it
ended: pid 1 in its loop, and pid 2 a zombie that nothing has reaped — which is the kernel's own
definition of a process that exited and was not waited for, and the reason the next step is `wait`.
