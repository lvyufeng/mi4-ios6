# Experiment 476 — the prefetch slot joins the data slot, and the price of a call is measured

**475's run ended on a fault it could not name.** The prefetch handler was still this image's own, its
nine keys went through the shared 8 KB trace channel, and that channel had refused 33292 writes by the
time the fault arrived: `xnu_entry_kv_written = 8170` of 8192, `_dropped = 33292`, and no
`xnu_entry_prefetch_abort_*` key anywhere. So the step had two things to decide and one of them it
could not: **whether the fault was serviceable** (the kernel's business) and **what it was** (this
image's business). This step gives the slot to Apple's own `locore_fleh_prefabt` and gets the second
answer from a channel that cannot be full.

## The record survives because it was already on the live channel

475's agenda asked for "a buffer of its own, sized for the record, printed under its own heading" — the
fix 461 applied to `fleh_undef`. **It was already there and the step is what proved it.** 467's wrapper
around `sleh_abort` writes to the *live* channel, which the payload captures whether or not the epilogue
runs; 474 had already given it the six words of `struct arm_saved_state`; and Apple's
`locore_fleh_prefabt` calls `sleh_abort(regs, T_PREFETCH_ABT)` exactly as `locore_fleh_dataabt` calls it
with `T_DATA_ABT`. So handing slot 3 to Apple's body moves the record from a channel that was full to the
one 474 and 475 arrived through, without a new buffer and without a new key. The run says so:

    seq 1..4  type 4  frame_ok 1  back 1..4     the exec's four data aborts, unchanged
    seq 5     type 3  fsr 0x0000000d  far 0x80006d04  thr 0xc052c270
              pc 0x80006d04  lr 0x00000000  sp 0x00101efc  cpsr 0x00000010
              fsr_frame 0x0000000d  far_frame 0x80006d04  frame_ok 1  user 1  **no `_back`**

**What 475's nine keys became.** `fleh_prefabt` reported IFAR, IFSR, LR_abt, PC, SPSR, TTBR0, TTBR1,
TTBCR and SCTLR. The live record carries five of those nine outright (`pc`, `lr`, `cpsr` from the frame
and the fault pair from the coprocessor) plus the thread and the mode — and **TTBR0, TTBR1, TTBCR and
SCTLR are not in it**. That is a deliberate, named loss: 241 and 242 read those four to tell which page
tables were live, so a later step that needs the mapping state has to add them to the wrapper rather
than expect them from a handler that is no longer installed. `fleh_prefabt` is kept in `entry_stubs.c`
beside `fleh_dataabt` (uninstalled since 467) and the build check asserts that **neither** is in the
vector table, because a slot that quietly kept the old handler looks exactly like a kernel that refused
to service the fault.

## The class is what says which coprocessor pair is the fault pair

The other half of the step is one `if`. A data abort's fault pair is DFSR (`c5,c0,0`) and DFAR
(`c6,c0,0`); a prefetch abort's is IFSR (`c5,c0,1`) and IFAR (`c6,c0,2`), and `__wrap_sleh_abort` reads
`fsr`/`far` for itself before calling the real handler. It was reading the *data* pair on both classes,
which for a prefetch abort means two stale numbers from the last data abort — and the numbers are the
reason the record exists. The wrapper now selects the pair from the `type` it is handed
(`STAGE90_T_PREFETCH_ABT` / `STAGE90_T_DATA_ABT`, `osfmk/arm/trap.h`), and **the run validates that
selection on the very entry that motivated it**:

**`frame_ok = 1` on seq 5.** The vector fills `SS_STATUS`/`SS_VADDR` from the same two coprocessor
registers, so the frame's own copy of the pair must equal what the wrapper read — and on a *prefetch*
abort it can only equal the IFSR/IFAR pair. Had the wrapper kept the data pair, seq 5 would have
reported `0x805`/`0x00101f28` (seq 4's, stale) against a frame saying `0x0000000d`/`0x80006d04`, and
`frame_ok` would read 0. So 474's control, which was built for the offset question, is also the
class-selection's control, and it is a reading rather than an argument.

## The fifth abort is the instrument's, and the arithmetic is Apple's

`0x80006d04` is inside **this image's `fleh_undef`** (476's link reports that function at `0x80006638`,
and 0x80006d04 is 0x6cc into it). It is where 475's forward put its `bl locore_fleh_undef`, and that
call is a property of 476's image rather than an inference from the log: 475's `--forward` check walked
that function's range, computed every `bl` target from its encoding, and required the one it found to be
`locore_fleh_undef` at `0x80014024`. It is also the only instruction in `fleh_undef` that hands control
to Apple's resume path, and the reason it is the resumed PC is Apple's own first three instructions
(`osfmk/arm/locore.s`, `0x8001402c`/`0x80014030` in 476's image):

    locore_fleh_undef:  mrs sp, SPSR
                        tst sp, #32          ; 32 = the Thumb bit of the *interrupted* PSR
                        subeq lr, lr, #4     ; 0x8001402c
                        subne lr, lr, #2     ; 0x80014030
    ...
    undef_from_user:    str lr, [., SS_PC]   ; the saved PC *is* this `lr`

On a **vector entry** that is correct: the hardware leaves `lr_und` = the instruction after the
trapping one, so the subtraction recovers the trapping instruction (which is why 474's and 475's records
both read `pc = lr - 4`). On a **`bl` from C** it is wrong in both halves: `lr` is the *return address*
of this image's call, so the subtraction points at the `bl` itself — and it is a *kernel* address, which
the kernel then hands back to the user thread as its PC. `cpsr = 0x10` in seq 5 is that handoff: the
fault was taken **in user mode**, `IFSR = 0xd` is a permission fault on a section, and
`sp = 0x00101efc` is the *user* stack. **So the kernel resumed process 1 at this image's own
instruction, in user mode, where a fetch of a supervisor-only section faulted.** The abort is the
instrument's, not the kernel's, and the fix cannot be a better call: `lr` is the one register Apple's
entry depends on, so a user `udf` has to be reached by a *branch*, with the interrupted registers
untouched. That is 477's whole change.

**And the fault was not serviceable either**, which is the other reading in the record: seq 5 has no
`xnu_live_sleh_back`. `sleh_abort` was entered and did not return, so the kernel took the abort's
decision — where 475's run had the *instrument* take it (`entry_epilogue("exception: prefetch abort")`)
and stop with the address refused. That was the point of the step, and the contrast is measurable: 475
stopped on this class of fault with nine refused keys and no address; 476 recorded it with the class,
both fault numbers, the thread, the frame, the mode and the missing return, and then went on.

## The stop is a kernel panic, and the record holds two of them

The run does not end there. After seq 5 the live channel shows one more `thread_block` —
`caller 0x800e9f40` = **inside `ipc_mqueue_receive`**, thread `0xc052c270` (the same thread that
faulted) — and that block *returned* (`_return`, `_returns = 0xe`). The next thing in the log is this
image's `fleh_undef` reporting a `udf` it was entered for:

    xnu_entry_panic_entered = 0x00000002
    xnu_entry_undef_lr      = 0x800364c4   (second block)
    xnu_entry_undef_pc      = 0x800364c0   = DebuggerTrapWithState + 0x28, the `udf #0`
    xnu_entry_undef_spsr    = 0x60000093   SVC mode, I and F set
    xnu_entry_undef_user    = 0x00000000
    xnu_entry_trap_r9_fmt   = 0x8048bb31   "ipc_mqueue_receive_results: strange wait_result"

**`pc = 0x800364c0` is `panic`'s own `udf`** (`DebuggerTrapWithState` at 0x80036498, which is the same
site 461 named at a lower address in a smaller image), and it was reached **from kernel mode**
(`spsr = 0x93`), i.e. through `panic()` and not through the user path. `entry_kv_into` **appends** and
does not dedupe, so the trap record holds **two** four-key blocks and `panic_entered = 2` is the tell:

    xnu_entry_undef_lr=0x000010e4  _pc=0x000010e0  _spsr=0x00000010  _user=0x00000001   ← the user's
    xnu_entry_undef_lr=0x800364c4  _pc=0x800364c0  _spsr=0x60000093  _user=0x00000000   ← panic's

**The first block is 475's forward arriving at this handler**, and reading it as the panic's frame is
how this step first went wrong: a per-key `sort -u` over the log keeps the *first* occurrence of each
key, which is the opposite end of the pair from the one being asked about, and "a panic at the user
`udf`" is what that produces. The record itself says the opposite, in order, and the count says it too.
(474's own entry-5 reading — `lr = 0x10e0` because `return_to_user_now` loads the user's `pc` into
`lr_svc` before `movs pc, lr` — is consistent with the first block and was never in question.)

**The panic's message is in a register, and that is the road that works.** `r9` at the trap holds the
last format-string argument of the panicking call, and 0x8048bb31 resolves in *this* image to
`"ipc_mqueue_receive_results: strange wait_result"` — `osfmk/ipc/ipc_mqueue.c:871`, the `default:` arm
of the switch over `wait_result_t`, reached when `ipc_mqueue_receive` is resumed with a value that is
none of `THREAD_AWAKENED` (0), `THREAD_TIMED_OUT` (1), `THREAD_INTERRUPTED` (2) or `THREAD_RESTART` (3).
The live channel's own order is the last mile of the chain: `thread_block` was entered from
`ipc_mqueue_receive` (0x800e9f40), it **returned**, and the very next event is `panic`'s `udf`. The
statement after the block in `ipc_mqueue_receive` is `ipc_mqueue_receive_results(wresult)`. **What
`wresult` was is not in this log** — the trap frame is `DebuggerTrapWithState`'s own (`r4` to `r9` all
zero) — and that is 477's frontier, not a gap in this record.

**478 retracts the reading of this section's first sentence, and the site was right.** The panic *was*
at `ipc_mqueue_receive`'s switch - `0x800e9f44`'s `cmp r0, #3` / `bhi panic` in this image - but the
value it switched on was **not** `thread_block`'s. This image's wrapper was declared
`void __wrap_thread_block(void *)` against a `wait_result_t`-returning function, so every `r0` the
switch read was garbage the instrument had left there. `wresult` was not missing from the log; it was
missing from the machine. See `experiment-478`, whose run does not stop here at all: with the ABI fixed
the boot reaches process 1's death and panics in `launchd_crashed_panic` instead. The measurements
*above* are unaffected - the fifth abort, `frame_ok = 1`, the class-dependent pair, the `lr`-derivation
argument and the two-block trap record all stand, and the two `xnu_entry_trap_r9_fmt` values this doc
compares are format strings either way.

**Measured:** gate passed, exit 0, device back on Android by itself, log 487314 bytes / 5689 lines,
entry image `.text` 5224416, image bytes 5438068, `.bss` 0x8052fa80..0x80587500, 43 `--wrap`ped symbols
(40 reached, 1 same-object-only, 1 never called, 1 by address), undefined 26, console
`xnu_entry_ostext_chars = 0x50b` / `_lines = 0x19` — **byte-identical to 475's**, ending at
`load_init_program: attempting to load /sbin/launchd` with no failure line and no panic text — and
payload `stage90-qcdt.img` 8458240, sha256 `eefbb474…845fdfe2f`.

**What would have made this wrong, and did not.** If the wrapper had kept the data pair, `frame_ok`
would be 0 on seq 5 — the one entry in the run whose two numbers could not have agreed by accident. If
slot 3 had kept this image's handler, the run would have ended at seq 5 with `entry_epilogue`'s
`exception: prefetch abort` heading and the address in a full channel, which is 475's log. If Apple's
handler had returned, seq 5 would have an `xnu_live_sleh_back`; it has none. And if `pc = 0x80006d04`
were a legitimate user address, `IFSR` would not be `0xd` on a section — a permission fault is what a
user fetch of kernel text produces.

## What 477 has to do

**Move the split into the vector page, because a call is what broke it.** The user case cannot be
reached by any `bl` from C — Apple's handler derives the saved PC from `lr` — so slot 1's trampoline has
to decide from `SPSR` before any C code runs and branch, leaving the interrupted registers intact. And
the reading 475 got from the forward has to come from somewhere: `sleh_undef` itself is the function
every `udf` reaches, so a `--wrap` of it gives the same four numbers with the frame in hand, on both
modes, with no handler of this image's involved.

**Say what `thread_block` returned.** It is `self->wait_result`, one word, and the wrapper already
exists: reading it turns the panic from a message into a value. The enum's members are 0, 1, 2, 3 and
**10** (`THREAD_NOT_WAITING`, `kern_types.h:81`), and a thread that was never woken still holds **-1**
(`thread.c:250`: `thread_template.wait_result = THREAD_WAITING`), so the two candidates are both
outside the switch and the key separates them: `-1` says the wake never came, `10` says the wait was
refused before it started.
