# Experiment 465 — the first pthread slot gets a body, and the terminal record stops being cuttable

**Status: on the device, one run, 477837 bytes of log, exit 0, device back on Android by itself (Android
10, `up 1 min`).** Two changes, both in the same direction — one to the *code* the boot stops on, one to
the *instrument* that reports it. `stage90_pthread_functions.c` gives the pthread table's
`pth_proc_hashinit` slot a body, retiring the symbol 464's run stopped on; and `entry_stub_hit` now writes
the terminal record to the **live channel first**, so the one record that names a stop cannot be cut by a
full buffer. The frontier moved from `forkproc`'s call into that slot to **`thread_bootstrap_return`**, the
ARM port's return-to-userland bootstrap — `osfmk/arm/locore.s`'s `load_and_go_user` — reached from
process 1's own thread through `task_wait_to_return` (`osfmk/kern/task.c:478-503`), which 464's run had
not yet reached.

## The two changes

**1. The slot.** `pth_proc_hashinit` was a generated stand-in (16 bytes, `entry_stub_hit`); it is now a
hand-written body, in the same shape as 433's `pthread_init` — declared below the macro block, defined
below the table, and set explicitly in the table's initializer:

```c
    .pthread_init       = &stage90_pthread_functions_init,
    .pth_proc_hashinit  = &stage90_pthread_slot_pth_proc_hashinit,
```

**2. The terminal record.** `entry_stub_hit` (`entry_stubs.c`) now writes, *before* its buffer record:

```
 xnu_live_stub_hit_seq        <- how many times a stand-in has been entered
 xnu_live_stub_hit_name_ptr   <- the name string's own address in this image
 xnu_live_stub_hit_name_w0    <- the name's first four bytes
 xnu_live_stub_hit_name_w1    <- and the next four
 xnu_live_stub_hit_caller     <- lr, so the call site is caller-4
```

The buffer copy is kept — it is what the epilogue prints as a block — but it is no longer the only route,
and it is no longer able to corrupt anything: 464's run filled it to exactly `ENTRY_KV_BUF` (8192) and
`g_kv_buf[g_kv_len] = '\0'` therefore stored at `&g_kv_buf + 8192` = **`0x80558000` = the first byte of
the kernel's `ExceptionVectorsTable`** (defect 183). The loop's bound is now `+ 2u`, reserving the `'\n'`
*and* the `'\0'`; this run's report reads `xnu_entry_kv_written = xnu_entry_kv_in_dram = 0x1fff`, which is
8191 — the array's last index — and the byte above the buffer is untouched.

## What the slot's body does, and what it deliberately does not

The real slot builds the per-process psynch waitqueue hash: `hashinit()`-backed storage reached through the
proc's `p_pthhash` field via the *kernel's* half of the table (`proc_set_pthhash`), read only by psynch's
own syscalls — seven more slots in the same table, unreachable in a boot with no userland. There is no
kext here, so the body records and returns, and:

- it **does not dereference `p`** (433's rule: a NULL or wrong pointer must be a stop that names itself,
  not a fault);
- it **does not call the callbacks table**: `proc_set_pthhash(p, NULL)` would write NULL over a field
  `forkproc`'s own `bzero` has already made NULL, and every call into the kernel is a new way for this
  step to fault;
- it leaves `p->p_pthhash` NULL, which is the value any psynch slot would find anyway.

The call cannot change what the boot does next: `forkproc` ignores the return (`void`) and the statement
has no error path (`kern_fork.c:1393`, under `#if PSYNCH`).

## The prediction, and all five of its falsifiers falling

The prediction was written in the supply file before the build (`stage90_pthread_functions.c`, the section
above the table): `xnu_live_pth_hashinit_seq = 1`, a `0xc0...` pointer for `p`, `xnu_live_pth_hashinit_tbl`
equal to whatever `nm` says the table is in this build, and **no** `stub_hit` for the retired slot. Measured:

```
 xnu_live_pth_hashinit_seq=0x00000001
 xnu_live_pth_hashinit_p=0xc057db80
 xnu_live_pth_hashinit_tbl=0x80499978
```

`0x80499978` is `nm -n`'s address for `stage90_pthread_functions` in this link, to the digit — the same
value 433's `pthread_init` body records, on the other channel, in the same run (below). The `p` value is a
`0xc0...` heap pointer of the shape `forkproc`'s allocations have; **it is not compared against another
run's absolute pointer** (defect 182's rule — this run's heap starts where this run's payload put it).
None of the falsifiers fired: no stop at the slot, no stop at `not_registered`, no `panic()`, no
`xnu_live_capped`, and no truncated name (the point of the second change).

## The instrument's own measurements

| key | 464 | 465 |
| --- | --- | --- |
| `xnu_entry_kv_written` / `_in_dram` | `0x2000` | `0x1fff` |
| `xnu_entry_kv_dropped` | `0x8080` | `0x80ed` |
| `xnu_entry_stub_caller_in_buf` | — | `0x00000000` |
| `xnu_entry_stub_caller_w0` / `_w1` | `0x20800069` / `0x0080006a` | `0x00000000` / `0x00000000` |
| `xnu_entry_stub_hit_count` | — | `0x00000001` |
| `xnu_entry_live_records` / `_refusals` | `0x2da` / `0` | `0x34c` / `0` |

The first row is the off-by-one, as a number. The next three are defect 184: in 464
`g_stub_caller_digits` was `0x2019`, **25 bytes past the end of the 8192-byte buffer**, because the two
`entry_kv` caller records had been refused — and `entry_image_ptr` *passed*, because the address is inside
the image, so the probe printed the bytes of the kernel's `ExceptionVectorsTable` as if they were the
caller's digits. The report now says which case it is (`_in_buf = 0`) and gates the two words on it. The
new stop's name, in the log, without any host tool:

```
 xnu_live_stub_hit_seq=0x00000001
 xnu_live_stub_hit_name_ptr=0x804dba8c
 xnu_live_stub_hit_name_w0=0x65726874      "thre"
 xnu_live_stub_hit_name_w1=0x625f6461      "ad_b"
 xnu_live_stub_hit_caller=0x800eae3c
```

and the buffer's own copy of the same record is ` stub_hit=thread_boo` — still cut, by design, because the
buffer is still full; the difference is that the name no longer *depends* on it. `0x804dba8c` is
`thread_bootstrap_return`'s name string in this image, three independent ways: the live words, the string
at that address in the ELF, and the symbol file.

## Where the boot went: process 1's thread, and the last gap before userland

The stop is `thread_bootstrap_return`, and every link to it is now identified:

```
 bsd/kern/bsd_init.c:1033        bsd_utaskbootstrap();                     #if CONFIG_EMBEDDED
 bsd/kern/bsd_init.c:1144            thread = cloneproc(TASK_NULL, COALITION_NULL, kernproc, FALSE, TRUE);
 bsd/kern/kern_fork.c:966                child_proc = forkproc(parent_proc);        <- 464's frontier, now retired
 bsd/kern/kern_fork.c:811-814            result = thread_create_waiting(child_task,
                                             (thread_continue_t)task_wait_to_return,
                                             task_get_return_wait_event(child_task), &child_thread);
 bsd/kern/bsd_init.c:1033            task_clear_return_wait(get_threadtask(thread));   # bsd_utaskbootstrap's tail
 osfmk/kern/task.c:478-503           void task_wait_to_return(void)
                                         do { ...; thread_block(THREAD_CONTINUE_NULL); ... } while (TF_LRETURNWAIT);
                                         thread_bootstrap_return();                <- the stub
 osfmk/arm/locore.s:1897-1907            load_and_go_user:                     <- what it should be
```

`fork_create_child`'s own comment states the design — "The new thread is waiting on the event triggered by
`task_clear_return_wait`" — and `task_wait_to_return`'s loop is the wait. The thread is started by
`Call_continuation` (`osfmk/arm/cswitch.s:107`, entered from `call_continuation`, `osfmk/arm/pcb.c:291-294`),
which is why the caller key reads as it does. The image's own disassembly of both ends:

```
800eae38:  e12fff36   blx  r6                    Call_continuation+0x18  <- calls the thread's continuation
800eae3c:  (the return address recorded as xnu_entry_stub_caller_v and as the caller of the last blocks)

800c3454:  e8bd4830   pop  {r4, r5, fp, lr}
800c3458:  ea0e5b29   b    8045a104 <thread_bootstrap_return>   task_wait_to_return's tail: a TAIL branch
```

The tail branch is why the recorded caller is `Call_continuation+0x1c` and not an address inside
`task_wait_to_return`: the compiler preserves `lr` across it, and `lr` was set by the `blx`. The same
effect explains the three block records that immediately precede the stub — three *different* threads
(`0xc04ab380`, `0xc04ad400`, `0xc04afb00`) all recording `enter = 0x800eae3c`, i.e. threads whose
continuations (`thread_stack_daemon`, `io_reprioritize_thread`, `vm_pressure_thread`, from the report's
ring) end in a tail branch to `thread_block` — `io_reprioritize_thread+0xc8` is exactly
` b 8045a79c <__wrap_thread_block>`.

`thread_bootstrap_return` is in this link's undefined set (`xnu_arm_entry_undef.txt:15`) and its stand-in
is the generated twenty bytes at `0x8045a104` — `movw r0, #0xba8c`, `movt r0, #0x804d`, `mov r1, lr`, tail
branch to `entry_stub_hit` — which is what put `0x804dba8c` in the live record. **The whole of `bsd_init`
ran, `forkproc` completed, process 1's task and thread were created, and the boot is now at the one
function between itself and user mode.**

The count of blocks says the same thing from a second side: 66 blocks over 45 distinct threads this run
against 60 over 45 in 464, and four of the six extra are blocks whose `enter` is `Call_continuation+0x1c`
(9 records this run against 5 in 464, whose histogram has the same site at `0x800eac5c` — 0x1e0 below,
which is the shift this image's whole middle of `.text` shows: `_IOConfigThread::main` and the
`IOSecureBSDRoot` caller moved by exactly 0x1e0 between the two builds). That comparison is the only
cross-build address arithmetic in this document, and the identification under it does not need the
arithmetic: **both runs' block rings pair that site with the same two continuations**,
`thread_stack_daemon` and `io_reprioritize_thread` (464: `0x8000dd98`, `0x80078ce0`; 465: `0x8000df88`,
`0x80078ec0` — the same two functions, printed with each build's own addresses). The heaviest site this
run is the thread-terminate path (`thread_terminate_self+0x2fc`, 30 of the 66) — threads terminating,
which is what a boot that finishes more work looks like.

## The tracer's buffer has been full since the IOKit bring-up, and one record proves it

464's write-up recorded that the *stub's* name was cut. What it did not ask is *when* the buffer filled —
and this step answered it while looking for a different record. 464's report contains **no**
`xnu_entry_stage90_pthread_functions_ptr`, the record 433's `pthread_init` body writes when `bsd_init:798`
calls it. That record is not missing because `pthread_init` did not run: the buffer's own content is the
*early* boot's allocations and nothing else — the dump's `t268_kalloc_ret` values run from `0xc05c0520` to
`0xc05d9800` while the frontier's own objects are at `0xc06062e8` (`getResourceService()`) and
`0xc06088c0` (the platform expert) — so the tracer's 8 KB buffer was already full during the IOKit
bring-up, and every `entry_kv` record written after it was refused in silence (32896 of them in 464).

This run is the controlled comparison, inside one boot, of the same call on two channels:

| record | channel | 464 | 465 |
| --- | --- | --- | --- |
| `xnu_entry_stage90_pthread_functions_ptr` | buffer | absent | **absent** |
| `xnu_entry_pth_hashinit_p` | buffer | — | **absent** |
| `xnu_live_pthread_init_ptr` | live | — | **`0x80499978`** |

Same call, same value, two channels: the buffer's copy is refused in both runs, the live copy arrives. A
record that was *refused* and a record that was never *written* are the same absence in the dump, which is
defect 187 — and it is why this step's own new record (`xnu_entry_pth_hashinit_p`) is written to both
channels, and why the live one is written first.

## The build, and the run

Platform objects first (`build_xnu_arm_kernel.sh --platform-only` with **both** `XNU_KERNEL_CONFIG` and
`XNU_MASTER_LOCAL`), then the entry image, then the payload — 462's and 463's sequence. The entry image's
`.text` grew 5102304 -> **5103168** (+864: two bodies, five live records, four report keys) and nothing
else moved: image bytes 5307400 unchanged, `.bss` `0x80510000..0x805a5f58` (614232 bytes, +32 for the four
new `.bss` words), `headroom` 1417384 (−32), the `IOPlatformExpert` metaclass address unchanged at
`0x8058b4f0`. 40 wraps, unchanged: 37 reached by a branch, 1 same-object-only, 1 never called here
(`sleep`), 1 by address only (`vcputc`), none dead; pass 1 = 27 undefined; all the mangled-name checks
pass. `g_kv_buf` is still at `0x80556000` and `ExceptionVectorsTable` still at `0x80558000`, so the
off-by-one fixed here is the one 464's run wrote into.

The table itself, read out of the built ELF at `0x80499978`, word by word:

```
 0 0x1         version            5 0x8026a10c  workqueue_exit
 1 0x8026a088  pthread_init       6 0x8026a11c  workqueue_mark_exiting
 2 0x8026a0dc  fill_procworkqueue 7 0x8026a12c  workqueue_thread_yielded
 3 0x8026a0ec  __unused1          8 0x8026a13c  pth_proc_hashinit   <- 465's body
 4 0x8026a0fc  __unused2          9 0x8026a1a0  pth_proc_hashdelete
```

and the shim that reaches it, `pth_proc_hashinit` at `0x801f0c3c`, loads `ldr r1, [r1, #32]` — **0x20**,
word 8. 464's write-up and README row say `#0x18`, "word 6", which is `workqueue_mark_exiting`: the stated
evidence contradicted the stated conclusion, which was right for a different reason (`nm` names
`0x801f0a5c` `pth_proc_hashinit`, and the caller is `#if PSYNCH`'s line in `forkproc`). Corrected in both
places in this step's commit; recorded as defect 185. The README row's 64-hex payload hash is also named
`md5` there while the build prints it with `sha256sum` (defect 186 — a reader who verifies with `md5sum`
gets a mismatch and concludes the artifact changed); both rows are corrected.

One `fastboot boot` through the two gates. XNU's own console block is **byte-identical to 464's** (1050
bytes to the end of the `BSD root` line, 22 lines including the capture's heading; `ostext_chars = _total
= 0x40a` (1034) and `_lines = 0x15` (21) in the report, `xnu_live_ostext_chars = _total = 0x3ee` (1006) and
`_tank = 0x262` live — the same numbers as 464's run, to the digit). No new OS text, and that is the
expected result rather than a disappointment: the boot stops *before* returning to user mode, and
`load_init_program`'s `printf` is on the far side of it. No `exception:`, `abort_entries = 0`,
`panic_entered = 0`, one `stub_hit`, `No errors detected`, exit 0, device back on Android 10 by itself.

Build identities, with the algorithm named: entry image `.bin` 5307400 bytes, md5
`ca3cebfbf465e1450a690fc9b905b34e`, embedded **verbatim** in the payload at offset `0x791b4`; payload
`stage90-qcdt.img` 8327168 bytes, sha256
`19b0e5b19d05cef09dba3f10237c79c4ca27c565cc8f15e63c94ca7c9686465e`. Two consecutive payload builds in
this session printed identical sha256s for all five artifacts, and a comment-only edit to the platform
object was proved inert the same way — by rebuilding and `cmp`-ing the entry `.bin` (byte-identical), which
is the check the project uses for exactly that question.

## What is not measured

1. **The thread that hit the stub.** `entry_stub_hit` still records no thread, and the block ring's
   `thread` slots hold the *first* eight blocks. That the stopped thread is the one `fork_create_child`
   created for process 1 is an argument from the source chain above plus `block_count`, not a reading.
2. **`p`'s identity.** `0xc057db80` is a `0xc0...` pointer and nothing else is recorded about it; the
   report has no other record of `child_proc` to compare it with.
3. **What the real slot would put in the proc.** `p_pthhash`, `hashinit`'s bucket count and the psynch
   hash table's layout are all unmeasured here, because nothing in this image allocates or reads them.
4. **The six extra blocks' threads as a set.** The live records give each block's `enter` and `thread`, so
   four of the six extra are Call_continuation-shaped and the rest are singletons; no block record carries
   the *continuation* unless it is one of the first eight, so the extra threads are not individually named.
5. **The kalloc stream's tail.** Unchanged from 464: the buffer holds the first 8191 bytes of a much longer
   stream, and 33005 writes were refused.

## Next: 466, `thread_bootstrap_return` — the last gap before userland

1. **The object of the step is `thread_bootstrap_return`, and unlike every slot before it, this one cannot
   be answered by a body.** Its real code is `osfmk/arm/locore.s:1897-1907`, which falls through into
   `thread_exception_return` and `load_and_go_user` — the sequence that checks pending ASTs, restores user
   state and drops to user mode. A stand-in that returns would return into `task_wait_to_return`'s caller,
   which is `Call_continuation`'s `thread_terminate` path: the honest options are to *link* the ARM layer's
   return-to-user path or to keep the stop. So the first thing 466 must measure is that path's closure —
   `tools/xnu_entry_callwalk.py` cannot follow the continuation (it is passed by *address* to
   `thread_create_waiting`, which is precisely why the straight-line walk from `forkproc`/`cloneproc`/
   `bsd_utaskbootstrap` answered "no stub on the straight-line path" while the stop was in `task.c`), so
   the closure question is a link question, not a walk question.
2. **The stack and the page tables are the other half.** Returning to user mode means `load_and_go_user`
   restoring a user context from process 1's `uthread`; whether process 1 has one that this boot ever
   built is the second measurement, and the answer decides whether 466 is a link step or a frontier in the
   *exec* path (`load_init_program`, `kern_exec.c:5119`, whose `printf` is the next new OS console text
   this boot can print).
3. Still owed from 461-465: 448's `_bad` slots as a pair of keys naming each one's sense; the timer
   (`ml_init_timebase` + an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`, 19.2 MHz, IRQ 19);
   `IOCPUInterruptController`; the pthread table's other 37 slots; `osfmk/kperf/kperfbsd.c`; making the
   epilogue's key list a data table; and the kalloc tracer's buffer, which this step measured to be full
   since the IOKit bring-up and which therefore costs the report every `entry_kv` record written after it.
