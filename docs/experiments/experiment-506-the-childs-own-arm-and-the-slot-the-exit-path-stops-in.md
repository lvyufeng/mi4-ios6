# Experiment 506 — the child's own arm, and the slot the exit path stops in

**One line.** The branch on `r1` is the one the step was for: the child took **its own** arm
(`xnu_live_exit_pid = 2`, entered from `unix_syscall+0x100`, the console never says `pid 1 exited`, and
the parent's `getpid` count is still climbing at the dump), and the `exit` it asked for reached the
kernel's own teardown — `proc_exit`, entered at its `pth_proc_hashdelete(p)` call, `+0x188` — and
**stopped there**, because that slot is one this image's `pthread_functions` table fills with a
stand-in (`stage90_pthread_functions.c:477`) and **a stub hit is terminal**: `entry_stub_hit` ends in
`entry_epilogue("a symbol this image does not provide was called")` (`entry_stubs.c:6107`). So the log's
last live record *is* the hit, and the `psignal(pp, SIGCHLD)` this step predicted is 338 source lines
further down the same arm (275 when this was written - 507 computed it from the two line numbers,
`:1105` to `:1443`, and corrects it here rather than in a footnote) — the parent was never told because the boot stopped first. **Run 1 of this
step measured the opposite** and is kept as a reading: `bne` in place of `beq` put `initproc` in the
child's arm, and the console's `pid 1 exited -- no exit reason available -- (signal 0, exit 3)` with
`xnu_entry_panic_entered = 1` is the cost. The next step is one function in `xnu_supply` and none in the
fixture: a body for `pth_proc_hashdelete`, the way 465 gave one to `pth_proc_hashinit` and 473 to the two
workqueue slots — both of which this same run measures working.

    python3 tools/host_ramdisk_macho_check.py --selftest out/stage90/xnu_arm_entry.elf  # all 111 refused
    python3 tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 48
    python3 tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf  # fork: munger 0, sy_narg 0
    python3 tools/check_timer_line.py --selftest --verbose                        # all 20 refused
    python3 tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 30 refused
    python3 tools/check_irq_routing.py --image out/stage90/xnu_arm_entry.elf --selftest # all 57 refused
    python3 tools/check_driver_catalogue.py --selftest                            # all 131 refused
    python3 tools/check_experiment_index.py                                       # 483 rows

## What the step was for

505's document ended with the next step in one sentence: "**one instruction in the fixture and none in
the kernel** - branch on `r1`" - and its owed list named the two readings that had never appeared in any
log in this walk: `xnu_live_exit_pid = 2` with the status `exit` composes out of it, and
`psignal(pp, SIGCHLD)` = 20 to pid 1, "the first inter-process event in this walk". 506 makes the first,
and measures exactly why the second cannot happen yet.

The premise 505 established is what makes the edit one instruction: `fork`'s halves are
`(r0 = pid, r1 = 0)` for the parent (`arm_prepare_u32_syscall_return`, `bsd/dev/arm/systemcalls.c:293`)
and `(r0 = pid, r1 = 1)` for the child (`thread_set_child`, `osfmk/arm/status.c:722`, called from
`fork1` *after* `thread_dup`, `bsd/kern/kern_fork.c:636`). The zero the fixture writes before the `svc`
is read by nobody. So the register to test is `r1`, and the value to compare it with is the flag
`thread_set_child` writes.

## The program: one register, one number, and 61 words either way

    - cmp  r0, #0        (+196)   the word 505's run read as "zero here means child"
    + cmp  r1, #CHILD_FLAG (+196) the word the kernel writes 1 into, for the child only
      beq  entry_child   (+200)   unchanged: taken when the test matched, which is the child

`CHILD_FLAG` is `.equ`d to 1 and the check reads it back out of the instruction stream, so the fixture
and the check disagree about the number only by failing the build. Everything else in the 61 words is
505's, address for address: `0x11a4` is still where the child is first fetched, `0x11d0` is still
`entry_failed: udf #1`, and `mov r0, #0` at `+184` stays because it is the word the `fork` record
publishes as `xnu_live_fork_uap0` - the *previous* call's argument buffer, not a value either process
reads.

**The sense of the comparison is not free, and run 1 is the proof.** With the register already changed,
`beq` is the condition that means "this thread's `r1` is the flag": `bne` asks the complementary
question and is true of the *parent*, so `initproc` executes `entry_child` and exits.

### Run 1: the inversion, and the check that agreed with it

Run 1 was built and run with `bne entry_child`. Two of this step's own predictions came back inverted
and the device said so in both the places the step is about:

  - the console, where the process that exited is pid 1: `pid 1 exited -- no exit reason available --
    (signal 0, exit 3)`;
  - the live records: `xnu_live_exit_seq = 1`, `_caller = 0x80285848`, **`_pid = 1`**, `_rval = 3`, with
    no `xnu_live_sigchld_*` (pid 1 has no parent to be told) and a panic —
    `xnu_entry_panic_entered = 1` with `_len = 0x13fa`, i.e. `launchd_crashed_panic` from
    `proc_prepareexit` (`kern_exit.c:849`), the branch 478's run measured.

**The check could not refuse it, and the reason is the defect this step closes.** The decode table
asserted `bne` - it had been written from the same derivation as the file, "the child's r1 is 1, which is
*not* equal to zero, so branch on inequality" - and the mutation that would have caught the inversion was
written *from that table*, so the two agreed with each other and only the device disagreed. A third
opinion was needed, and it has to be a *relation between two fields of the encoding* rather than a
second copy of the expectation:

```python
immediate = decoded[FORK_TEST_WORD][1][1]      # the number the test compares r1 with
condition = decoded[FORK_BRANCH_WORD][1][0]    # the condition the branch is taken on
want = COND["ne"] if immediate == 0 else COND["eq"]
```

which is the rule the two-process ABI implies and nothing else: a test against zero is a test for the
parent's `r1` and must branch on inequality, and a test against a non-zero flag is a test for the
child's and must branch on equality. It is exercised on the *built* artifact rather than on the source:
patching word 50 of `out/stage90/xnu_arm_entry.elf` to `0x1A000000` (run 1's program) and running the
checker gives two failures, the table's and the clause's -

    0x10e0: word 50 at 0x11a8 is bne to word 52, and this check requires beq to word 52 ...
    0x10e0: the `fork`'s test at word 49 compares against 1 and the branch at word 50 is taken on
            inequality, which is the claim that the child is the thread whose r1 is not 1 - the
            opposite of what `thread_set_child` writes ...

- so run 1's program is refused twice, once by each opinion, and this time by the opinion that does not
restate the table.

### The child took the child's arm

The two runs' record streams are the same up to the dispatch, which is what makes the branch the only
variable: the fork's record, the parent's `getpid` count climbing (14 records, powers of two, `2` through
`0x4000`), the parent's quantum expiries, and then `xnu_live_sleh_seq = 8` - **the child's first
dispatch, the same lazy prefetch abort at the same address, `far = pc = 0x000011a4`, `thr = 0xc04f11a0`,
`sp = 0x00101efc`, `cpsr = 0x40000010`, `user = 1`** - which is 505's record with a different thread
pointer, i.e. `machine_thread_dup`'s copy seen from the far side again. One record later:

    xnu_live_exit_seq = 1   _caller = 0x80285848 (unix_syscall+0x100)   _pid = 2   _rval = 3

pid **2** - the child, and only the child can be: `proc_pid` of the dispatcher's own `proc`. And the
other half of the reading is an absence with a number behind it: the console's block is unmoved and
still ends at `load_init_program: attempting to load /sbin/launchd` with **no `pid 1 exited` line
anywhere in the log**, while the parent's spin kept being answered 1 (`xnu_live_getpid_last = 1` at
every one of its 14 records), the last of them written beside the child's exit record. Two processes,
one of them exiting - and the OS is alive.

### And the exit path stops one call later

The run's **last** live record is a stub hit, and it is the only one in the run:

| key | value |
|---|---|
| `xnu_live_stub_hit_seq` | `0x1` |
| `xnu_live_stub_hit_name_ptr` | `0x804c157a` → `"stage90_pthread_functions.pth_proc_hashdelete"` (`w0 = 0x67617473` "stag", `w1 = 0x5f303965` "e90_") |
| `xnu_live_stub_hit_caller` | `0x80294150` = `proc_exit + 0x188` |

The chain is three files long and each link is checkable:

  - `proc_exit` calls `pth_proc_hashdelete(p)` at `bsd/kern/kern_exit.c:1105` (under `#if PSYNCH`), and
    the linked image's call is at `0x8029414c` with the return address `0x80294150` — the number the
    record carries, exactly;
  - the shim is Apple's own: `bsd/kern/pthread_shims.c:364`, `pthread_functions->pth_proc_hashdelete(p)`
    — the `ldr r1, [r1, #36]` / `bx r1` at `0x80205d0c..0x80205d1c`, a tail call, which is why the
    stand-in is entered with the *shim's* caller in the return register;
  - the slot is a stand-in, and the stand-in is terminal: `entry_stub_hit` ends with
    `entry_epilogue(...)` (`entry_stubs.c:6107`), so the record is written and the run ends. Nothing in
    the log after it is a live record — the next line is the report's own header.

**So the SIGCHLD is absent for a second and unrelated reason.** 505's absence was "the arm was not
taken". This one is: the arm ran, reached the kernel's teardown, and the teardown needs a slot this
image does not supply. `psignal(pp, SIGCHLD)` at `kern_exit.c:1443` is **338** source lines below
`pth_proc_hashdelete` on the same path (`:1105`; this said 275, an estimate quoted rather than a distance
computed - corrected by 507, which then measured what is at the end of it), and pid 1 is never told
because the boot stops 338 lines short.

**The prediction's `_rval` was also one number wrong**, and the run corrected it: `xnu_live_exit_rval` is
`uap->rval` as the munger marshalled it — the ABI the wrapper is declared by is
`(*(callp->sy_call))(proc, &uthread->uu_arg[0], &uthread->uu_rval[0])` — so the record is **3**.
`W_EXITCODE(3, 0)` is computed *inside* the kernel (`exit`, `kern_exit.c:684`) and lands in `p->p_xstat`
(`:826`), a field nothing in this image reads back; 0x300 was named when the key was assumed to hold the
composed value. Both the fixture's header and the checker's note said so and now say this instead.

## The readings

In the order the log writes them (line numbers are `/tmp/cancro-last_kmsg.txt`, 8781 lines):

| line | record | what it says |
|---|---|---|
| 3994 | `load_init_program: attempting to load /sbin/launchd` | the console block's last line, **and there is no `failed loading` after it and no `pid 1 exited` anywhere** |
| 7827 | `xnu_live_pth_hashinit_seq = 2`, `p = 0xc058d3a8`, `tbl = 0x804c1128` | 465's slot, doing what 465 gave it a body for: the child's proc, built inside `forkproc` |
| 7833 | `xnu_live_fork_seq = 1`, `caller = 0x80285848`, `uap0 = 0x000011e0`, `error = 0`, `ret_lo = 2`, `ret_hi = 0` | the parent's answer, as 505 measured it |
| 7839-7915 | `xnu_live_getpid_count` `2, 4, 8, … 0x4000` (14 records), `_last = 1` every time | the parent never leaves its loop, and it is still pid 1 |
| 7902-7917 | `xnu_live_tmr_qexp_seq` `2..5`, `thread = 0xc04f1830` | the parent's quantum expires, the fifth **immediately before the child's first dispatch** — the switch to the child is the parent's slice ending |
| 7910-7913 | `xnu_live_irq_timer_count = 8`, `pend_before = pend_after = 0x20400000`, `late_count = 0` | 502's IRQ records, unchanged |
| 7918-7932 | `sleh_seq = 8`: `type = 3`, `fsr = 5`, `far = pc = 0x000011a4`, `thr = 0xc04f11a0`, `sp = 0x00101efc`, `cpsr = 0x40000010`, `user = 1` | **the child's first dispatch**, on the word after the fork's `svc`, with the parent's stack and flags |
| 7933-7936 | `xnu_live_exit_seq = 1`, `_caller = 0x80285848`, **`_pid = 2`**, `_rval = 3` | **the child's own arm ran**: `cmp r1, #1` matched, so `beq 0x11b0` was taken and the word at `+208` is `mov r0, #3` |
| 7937-7943 | `xnu_live_pth_wqmark_seq = 2` / `_wqexit_seq = 2`, `p = 0xc058d3a8` | 473's pair, doing what 473 gave them bodies for: `proc_exit`'s slot, on the child's own proc |
| 7944-7947 | `xnu_live_stub_hit_seq = 1`, `name_ptr = 0x804c157a` = `"…pth_proc_hashdelete"`, `caller = 0x80294150` = `proc_exit + 0x188` | **where the run ends**: the exit path's first slot this image does not supply, and a stub hit is terminal |
| — | `xnu_live_sigchld_*`, `xnu_live_undef_*`, `xnu_live_osr_*`, `xnu_live_getpid_change_*` | **absent**: no SIGCHLD (the run stopped first), no fault, no death to handle, and the child never reached `getpid` again |
| 7961, 8087 | `xnu_entry_abort_entries = 0x0`, `xnu_entry_panic_entered = 0x0` (`_len = 0x8a`) | no abort, and the report's own reading of the panic word is zero |

**Run 1's readings**, taken from its log while the corrected fixture was being written (that log has
since been overwritten by run 2's capture; the numbers are the ones filed in `entry_ramdisk.s`'s 506
section at the time): console `pid 1 exited -- no exit reason available -- (signal 0, exit 3)`;
`xnu_live_exit_seq = 1`, `_caller = 0x80285848`, **`_pid = 1`**, `_rval = 3`; no `xnu_live_sigchld_*`; no
`xnu_live_undef_*`; `xnu_entry_panic_entered = 1`, `_len = 0x13fa`; log 579953 bytes; the device came
back on its own with `No errors detected`.

Four absences and one number carry as much as the rows: the child's `exit` arm ran and the parent's loop
never stopped; **the `exit` record's pid is the discriminator the whole step turns on**, and it is read
from the kernel's side of the fork rather than from the fixture's.

## The image

  - the fixture is **61 words again**, and 506 changes **two** of them in place: word 49 (`cmp r0, #0` →
    `cmp r1, #CHILD_FLAG`, `0xe3500000` → `0xe3510001`) and word 50's condition (`0x1a000000` run 1 →
    `0x0a000000` run 2). The blob is the payload's `g_stage90_ramdisk` (`0x8050d000`, 8192 bytes,
    unchanged) as a `__PAGEZERO` + `__TEXT` Mach-O: pc `0x10e0` is file offset `0xe0`, and `__TEXT`'s
    4096 bytes are the file's first 4096. **The booted image was decoded, not just the object**: the
    16 words from `0x5863f0` of `out/stage90/stage90.img` are
    `e3a00000 e3a0c002 ef000080 e3510001 0a000000 ea000002 e3a00003 e3a0c001 ef000080 e3a0c014
    ef000080 e3500001 1a000000 eafffffa e7f000f1` — the fork's four, the branch pair, the `exit`'s
    three, and the parent's loop behind it.
  - **`.text` is 5286944 bytes, unchanged from 505**, and so is the entry image's 5503612 bytes: the
    step's whole edit is inside the RAM disk blob, and the two files it touches are the fixture and a
    host-side checker. `.text`'s growth in 505 (+1120) is what paid for the 505 instrument; 506 adds
    nothing to the image at all. `__bss_start 0x8053fa80`, 362888 bytes to `0x80598408`; the wrap census
    is unchanged at **65 wrapped symbols: 54 reached by a branch, 1 same-object-only, 1 never called
    here, 9 by address only** — `exit` and `fork` are two of the nine.
  - the two files: `stages/stage90/xnu_arm_boot/entry_ramdisk.s` (+113) and
    `tools/host_ramdisk_macho_check.py` (+155) — 200 insertions and 68 deletions, of which the
    load-bearing part is two words of the program, the sense clause, and two corrected claims. The
    fixture's edits after run 2 (this section, the corrected `_rval`, the falsifier marker) are
    **comments only, and that is proved rather than asserted**: the rebuild's `stage90.img`,
    `stage90.bin` and `stage90-qcdt.img` compare byte-identical (`cmp`) to the three the device ran, so
    the artifact the run measured and the artifact this document describes are one file.
  - the payload: `stage90.bin` 5999040 sha256
    `b07fb6f610f3c0ff5cd404fc130d0f7a292c2074ce103bb73ea08261e7b53f5e`, `stage90.img` 6002688 sha256
    `516b77f6e98e0a420754f731180ed1d3ad35b2e4cebfd102e94e1483f671efb5`, `stage90-qcdt.img` 8523776
    sha256 `cd3cb6ac24e589b0193a34018ae5be9ab99825985f2b0729ba5475a801f7586a`;
    `kernel_size=5999040 dt_size=2521088 page_size=2048`; `check_sysent_table.py` still reads `sysent[1]`
    = `__wrap_exit` with munger `munge_w` and `sysent[2]` = `__wrap_fork` with munger 0, `sy_narg` 0.

## The rest of the boot is unmoved

The OS console block is the same 19 lines it has been since 459, character for character, including
`Added memory device md0/rmd0 (02000000/0D000000) at 000000008050D000 for 0000000000002000`,
`BSD root: md0, major 2, minor 0` and `VM_TEST_DEVICE_PAGER_TRANSPOSE: PASS`, and it still ends at
`load_init_program: attempting to load /sbin/launchd` with no `failed loading` after it - 490's reading
of a *successful* exec, so the process that forked, exited a child, and survived is the process
`load_init_program` loaded. The report is as full as it has been since 461:
`xnu_entry_kv_written = 0x1fff` of its 8192 bytes with `_dropped = 0x92ba` (37562 refusals), so the
readings above are the live channel's.

## What is owed

  - **`pth_proc_hashdelete`'s body, which is the next step and is in `xnu_supply`, not in the fixture.**
    The run stops there, and the exit arm this step added is 338 source lines long with its SIGCHLD at
    the far end. The two slots this same run watches working are the shape to copy: 465's `pth_proc_hashinit`
    and 473's pair record the call, do not dereference `p`, and do not reach into the callbacks table.
    What a *delete* has to undo here has a measured answer — 465's body leaves `p->p_pthhash` NULL,
    which is the value the psynch slots would find — but that argument belongs to 507's file.
  - **The composed status.** `xnu_live_exit_rval` is the argument; `p->p_xstat` (0x300) is published by
    nothing in this image. `exit`'s own line (`kern_exit.c:684`) is where the two are one number, and a
    record read from `current_proc()`'s `p_xstat` at the `psignal` wrapper would be the first one taken
    from the *proc* rather than from the argument buffer.
  - **The report cannot name its own reason.** The header line prints nothing after the colon and
    `xnu_entry_why = 0x1034e1a0`, which is not the address of any literal in this image, although every
    caller of `entry_epilogue` passes a literal — the one this run took is `entry_stubs.c:6107`. Both
    places the reason is read (`:3746` and `:3799`) are *after* the epilogue has turned the caches and
    the MMU off (`:3728-3745`), which is the leading candidate; the same epilogue's live-channel dump
    reads correctly, so the mechanism is owed rather than explained. Beside it, one disagreement: the
    report prints `xnu_entry_panic_entered = 0x0` and the epilogue's own gate, 800 lines later, printed
    the trap buffer — whose four keys (`xnu_entry_stub_caller_e`, `xnu_entry_irq_iar`,
    `xnu_entry_irq_ispendr0`, `xnu_entry_irq_isenabler0`) are not a trap record.
  - **The quantum count, which two documents now state differently.** This run's parent expired its
    quantum four times before the child's first dispatch (`xnu_live_tmr_qexp_seq` 2..5,
    `thread = 0xc04f1830`; `seq 1`, `thread = 0x8054a4c0`, is the *boot* thread's and is earlier in the
    run), where 505's document records six before the same dispatch point. 505's log is no longer on
    disk, so the difference is owed a re-read rather than explained — and the *duration* is still owed
    in both directions: the log has no tick at the fork, only an order.
  - **From 505, still owed**: which pthread slot the corpse path hit (`xnu_live_stub_hit_caller =
    0x802933b4`). This run resolved its own slot through the live channel's `name_ptr` and the string's
    first eight bytes, which is the method 505's log would have needed — and no longer exists to be asked.
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
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both boots were non-persistent `fastboot boot`
and nothing was flashed. **Two runs**, and they are worth separating:

  - **run 1** (the inverted condition) panicked - `launchd_crashed_panic` through `proc_exit`, which is
    478's failure mode reached by this step's own branch - and the device returned on its own inside the
    capture window with `No errors detected` in 579953 bytes of log. That is the recovery net doing what
    it was proved for, and it is the reason the fixture's header calls the sense of the comparison a
    measurement rather than an argument.
  - **run 2** (the corrected condition) is the step's run: 591886 bytes of log, 8781 lines,
    `No errors detected`, the device handed back to Android on its own, and the hardware watchdog armed
    at `0xf9017000` with a 25 s timeout and not needed.

The run that counts ended with the OS alive, one process in its loop, and the other stopped inside the
kernel's teardown at a call this image answers with a stand-in — which is the ordinary shape of this
walk, and the safer of the two ways for a second process to stop.
