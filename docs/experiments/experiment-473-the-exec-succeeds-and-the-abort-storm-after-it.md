# Experiment 473 — the exec succeeds, and the frontier is the abort storm after it

**472 put the boot inside `load_machfile`'s task transition and stopped there, on a stand-in of this
image's own pthread table.** 473 retires the two slots that block is made of, and this is the run in
which the OS's exec path finally *returns success*: `/sbin/launchd`, the RAM disk's own Mach-O, is
accepted, activated and installed as process 1.

**The step's content is two more bodies in `stages/stage90/xnu_supply/stage90_pthread_functions.c`.**
The table has 39 named slots; 433 gave `pthread_init` a body and 465 gave `pth_proc_hashinit` one, and
the two this step retires are the two calls in the `in_exec` block — `workqueue_mark_exiting` (slot word
6) and `workqueue_exit` (word 5, `pthread_shims.c`'s `ldr r1, [r1, #0x14]` and `#0x18`). Both forwards in
`bsd/kern/pthread_shims.c:331-340` are statements whose callee returns `void`, and both real slots
maintain *libpthread's* per-process workqueue state — which does not exist in this image, because the
kext that owns it does not (432). So the honest implementation of "there is no workqueued work for `p`"
is 433's shape: record the call, do not dereference `p`, return.

**Why both words in one step, rather than the one this project usually retires.** They are two calls in
one basic block with only real code between them (`task_complete_halt`), so retiring the first would stop
the boot at the second one device run later, having measured nothing new. The records are per-slot and
counted, so the log still says which of the two was entered and how many times.

**The prediction, written in the source before the build**, was: the two live records appear; the run
does not stop on either slot; and then process 1 runs its `udf #0` in User mode, reported as
`xnu_entry_undef_pc = 0x10e0` with `xnu_entry_undef_spsr = 0x10`. **The first two held and the third did
not** — there is no `udf` trap in the log at all — which is the whole of this step's result:

    xnu_live_pth_wqmark_seq = 1
    xnu_live_pth_wqmark_p   = 0xc0535e38
    xnu_live_pth_wqmark_tbl = 0x804b3b40      <- nm says this is stage90_pthread_functions
    xnu_live_pth_wqexit_seq = 1
    xnu_live_pth_wqexit_p   = 0xc0535e38
    xnu_live_pth_wqexit_tbl = 0x804b3b40
    xnu_live_lmf_caller     = 0x80285434
    xnu_live_lmf_header     = 0xc8146000
    xnu_live_lmf_ret        = 0x00000000

**`lmf_ret = 0` is a reading this time, and the two keys beside it are what make it one.** 472's log had
`xnu_entry_lmf_calls = 0` — the wrapper was never entered, because the run stopped inside the function it
wraps — so its `lmf_ret = 0` said nothing. Here the caller key is non-zero, and the caller is the
instruction after the call: `__wrap_load_machfile` is at `0x8047304c` and the only `bl` to it is at
`0x80285430` in `exec_mach_imgact`, so `0x80285434` is the instruction after it — `cmp r0, #0`, followed
by `beq 0x80285480`, which is taken exactly when the return value is zero. `load_machfile` returned
**`LOAD_SUCCESS`** and the success path was entered. `lmf_header = 0xc8146000` is the Mach-O's own header
inside the kernel's mapping of the RAM disk — the same address the run's third `sleh` record faults on.

**The stop after it is a data abort, repeated into a storm, and the live channel names its address but
not its instruction.**

    xnu_live_sleh_seq = 4
    xnu_live_sleh_type = 4
    xnu_live_sleh_dfsr = 0x00000805          translation fault, section, read
    xnu_live_sleh_dfar = 0x00101f28
    xnu_live_sleh_thr  = 0xc049e1a0
    xnu_live_block_enter = 0x800f6efc        Call_continuation + 0x1c
    xnu_live_block_thr   = 0xc0464830
    xnu_live_block_now   = 0x04d994d3
    xnu_live_block_seq   = 0x00000046
    xnu_live_sleh_storm  = 0x00000005        the cap was reached: at least five aborts

Records 1 to 3 of the same counter are the ones every run of this project has — `dfar = 0x1000` for the
page-zero probe, then two reads of the RAM disk's kernel mapping (`0xc8105000`, `0xc8146000`) — and 472's
run had exactly those three and no more (`xnu_entry_sleh_storm = 0`). **Record 4 is the first abort on
the far side of the exec**, its address is a *user* address in the first megabyte, and the run then
stormed: the sequence is at least five aborts, at which point the instrument stops recording rather than
looping (269's lesson, `SLEH_LIVE_MAX`).

**The run hung, and that is measured rather than assumed.** The epilogue never ran: this log has 77
`xnu_entry_` keys where a completed run has 385, and **not one** of them is `xnu_entry_undef_*`,
`xnu_entry_panic_*`, `xnu_entry_trap_*` or `xnu_entry_abort_*` — the groups that need either a trap or
the report. The device came back on its own, and Android's `No errors detected` follows the payload's
output, so the hardware watchdog did the recovering: the summary the OS console ends on is
`load_init_program: attempting to load /sbin/launchd` with **no** `failed loading` line after it, which
is what a successful exec prints (nothing) and what a failed one does not.

**The frontier is therefore not the entry point and not the file — it is one abort, and the instrument
that can name it is already in the call path.** The abort is serviced by Apple's `locore_fleh_dataabt`
(vector slot 4 since 467), whose `sleh_abort(struct arm_saved_state *regs, int type)` is wrapped by
`__wrap_sleh_abort(void *regs, int type)`. The wrapper reads `DFSR`, `DFAR` and `TPIDRPRW` from the
coprocessors and records those; **it has `regs` in hand and does not yet read it.** Apple's own
`osfmk/mach/arm/thread_status.h` defines the state it points at — `r[13]`, `sp`, `lr`, `pc`, `cpsr`,
`fsr`, `far`, `exception` — so the faulting instruction is `regs->pc` at a fixed offset. And the offset
is **checkable rather than transcribed**: `regs->fsr` and `regs->far` are the same two numbers the wrapper
already reads from `p14`, so a wrapper that records both pairs says whether it read the right words —
which is the check 468's `assym.s` defect and 465's mis-numbered slot both argue for. What that reading
will distinguish: a kernel-mode read of an unmapped *user* address (the exec path's own `copyin`/
`copyout`, or the string/stack copyout that `exec_copyout_strings` does), versus the process-1 thread
failing on its way to `0x10e0` — the two answers have different fixes, and the log cannot tell them apart
as it stands.

**A new check makes the table's own layout structural, because this step is the third time a slot has
been retired by hand.** `tools/check_pthread_table_slots.py` reads the 40 named words out of the *linked
image* and compares each against the symbol its member name implies
(`stage90_pthread_functions_init` for `pthread_init`, `stage90_pthread_slot_<member>` for the rest),
taking the member names, their order, `_pad[87]` and `PTHREAD_FUNCTIONS_TABLE_VERSION` from Apple's own
`bsd/sys/pthread_shims.h` — so there is no second definition of the layout to drift from. **The hazard it
closes is not a stop and not a fault**: a swapped pair of designators is two non-NULL words that pass the
constructor's NULL scan, and the body that runs would name itself correctly and just name the *other*
slot. Measured: `all 39 named words of struct pthread_functions_s at 0x804b3b40 read back as the
functions their member names say (both channels agree on 1 + 39 + 87 = 508 bytes; version 1)`, and
`--selftest` — which swaps two pairs and zeroes a word and `version` in a copy of the table in memory —
refuses all four mutations.

**Measured:** device run exit 0, the device back on Android by itself (watchdog), log
`/tmp/cancro-473b-last_kmsg.txt`, 455596 bytes. Entry image `.text` 5223808 (+288 on 472), image bytes
5438068, `.bss` `0x8052fa80 .. 0x805874c0` (unchanged), undefined 26, `stage90-qcdt.img` 8458240 bytes,
sha256 `7fa1bc2ecd984dfe2c98881175c63de7397c73c78805ccf994656a46ac2d32fd`.

**And one capture of this step's run was not the payload's output at all, which is why the capture is now
checked.** The first read of `/proc/last_kmsg` after this image's boot returned 455596 bytes — *the same
length* as the valid capture — whose first 41 characters were the payload's banner
(`MI4IOS6_STAGE90 v1 entered; Stage-owned arm_`) and whose remaining 455525 were binary: a self-similar
region with a 65520-byte period, holding kernel pointers, found in none of the images and in neither
other log. Neither the byte count nor the first characters of the banner distinguishes it from a run that
died at character 41 stop. `stages/stage90/run_and_capture.sh` therefore counts the lines the payload
owns after capturing (`grep -a -c '^MI4IOS6_STAGE90'`, at least 8) and re-reads the file up to three
times — a read, so it cannot cost a run — and if it is still short, it says so instead of letting the log
be read as a result: `0` lines of payload for that first capture, `3930` for the valid one. The same
image reproduced cleanly on the next attempt, and the run above is that attempt.
