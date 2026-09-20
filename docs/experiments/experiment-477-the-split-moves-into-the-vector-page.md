# Experiment 477 — the split moves into the vector page, and the panic is the one left

**476 measured what a call costs.** `locore_fleh_undef` derives the saved PC from `lr` (`subeq lr, lr,
#4`), so 475's `bl` from C resumed process 1 *at the `bl` itself*, in user mode, where a fetch of a
supervisor section permission-faulted — and the fault, the record and the panic that followed were the
instrument's. This step takes the call away: **slot 1 decides the mode in the vector page, before any C
code runs, and branches.** The reading the forward used to produce comes from the kernel's own call path
instead, and the run that follows is the first in which a user `udf` and `panic`'s `udf` reach *different
handlers*, decided by hardware state and nothing else.

## The branch, in assembly, with the one register that is safe to use

    vec_tramp_1:
        mrs     sp, spsr            ; the *banked* SP: the interrupted SP is saved and banked by the
        and     sp, sp, #0x1f       ; exception, so nothing of the interrupted context is touched
        cmp     sp, #0x10           ; PSR_MODE_MASK, PSR_USER_MODE (osfmk/arm/proc_reg.h)
        beq     vec_tramp_1_user
        ldr     sp, vec_tramp_1_stack
        ldr     pc, vec_tramp_1_handler        ; fleh_undef, this image's C handler
    vec_tramp_1_user:
        ldr     pc, vec_tramp_1_user_handler   ; locore_fleh_undef, Apple's
    vec_tramp_1_stack:          .word entry_vectors_stack_top
    vec_tramp_1_handler:        .word fleh_undef
    vec_tramp_1_user_handler:   .word locore_fleh_undef

Three things about it are deliberate. **`mrs sp, spsr` is Apple's own way of looking at the interrupted
mode** — the first instruction of `fleh_undef` and the second of both abort handlers (after their
`sub lr, lr, #8` and `sub lr, lr, #4`) — and it is the only scratch register an exception can use
without losing anything: the interrupted SP has already been banked into SVC's own. **`and`/`cmp` cannot
carry a symbol**, so the two numbers are written here — and checked where they can be, by
`entry_saved_state.h` holding the same pair for the C side and by the ELF audit below. **The kernel
branch keeps the macro's stack load and the user branch does not**, which is the reason this slot is
written out rather than passed to `VECTOR_TRAMP`: this image's handler is C and needs a stack, while
Apple's `undef_from_user` re-derives `sp` from `TPIDRPRW` and the thread's `kstackptr` and never reads
this one.

**The record moves to `sleh_undef`.** The user case no longer passes through this image at all, so the
milestone reading comes from `--wrap=sleh_undef` — the function *every* undefined instruction reaches,
user or kernel, with the frame in hand. It is 467's arrangement for `sleh_abort` applied to the other
trap, and it writes the same four live keys plus two counters. **`lr` in it is a different quantity from
475's and 476's**: those read `LR_und` (the hardware's, `pc + 4`, inside `fleh_undef`), while this one
reads `SS_LR` out of the frame — the *thread's* own link register.

## The run

    xnu_live_undef_seq       = 0x00000001
    xnu_live_undef_user_seq  = 0x00000001
    xnu_live_undef_pc        = 0x000010e0     the fixture Mach-O's `udf #0`
    xnu_live_undef_lr        = 0x00000000     SS_LR: the thread state's own lr, which entry_ramdisk.s
                                              writes as zero (+212: "the thread never returns")
    xnu_live_undef_spsr      = 0x00000010     user mode
    xnu_live_undef_user      = 0x00000001

**`xnu_entry_undef_user = 0x00000000`, and that is the falsifier firing the right way.** The condition
was written into the source before the build: this handler must never see a user `udf` again, and if it
did, the trampoline's test had not fired. The report says it did not:

    xnu_entry_panic_entered = 0x00000001      (476: 2)
    xnu_entry_undef_lr      = 0x800364c4
    xnu_entry_undef_pc      = 0x800364c0      DebuggerTrapWithState + 0x28: panic's own `udf #0`
    xnu_entry_undef_spsr    = 0x60000093      SVC mode, I and F set
    xnu_entry_undef_user    = 0x00000000

and the trap record holds exactly **one** block, where 476's held two. So the one `udf` that reached
this image's handler came from `panic()`, in kernel mode — the split put the user's at Apple's body, and
`panic_entered` fell from 2 to 1 in the same report that has carried it since 461.

**The spurious fault is gone, and the channel says so in its own order.** The live records are the four
aborts of the exec — `xnu_live_sleh_seen` 1 to 4, every one with a `_back`, every one `frame_ok = 1`,
all four `type = 4` — and there is **no fifth entry and no `type = 3` at all**, where 476's run had both.
The last three events, in the order the channel appended them:

    block_enter 0x800f6efc (Call_continuation)  thr 0xc0578480  seq 0x46
    undef_seq 1 / user_seq 1 / pc 0x10e0 / lr 0 / spsr 0x10 / user 1
                                    ← the split fired: recorded by `sleh_undef`, i.e. from Apple's path
    block_enter 0x800e9f40 (inside ipc_mqueue_receive)  thr 0xc057b270  seq 0x47
    block_return 0x800e9f40  block_returns 0x0000000e                       ← and it returned
    … then the panic's `udf`

`0x800e9f40` is inside **`ipc_mqueue_receive`**, and the panic's message is the same one 476's trap
record named:

    xnu_entry_trap_r9_fmt = 0x8048bc59   "ipc_mqueue_receive_results: strange wait_result"

The address differs from 476's (`0x8048bb31`) because the image moved between the two builds; the string
is the *same text read out of this image*, and the two addresses are 0x128 apart.
`osfmk/ipc/ipc_mqueue.c:871` is the `default:` arm of the switch over `wait_result_t`, reached
when the receive path is resumed with a value that is none of `THREAD_AWAKENED` (0),
`THREAD_TIMED_OUT` (1), `THREAD_INTERRUPTED` (2) or `THREAD_RESTART` (3); the statement after the
`thread_block` in `ipc_mqueue_receive` is `ipc_mqueue_receive_results(wresult)`, and the channel shows
the block returning into that function immediately before the panic. **So the two runs' stops are the
same stop** — 476's reached it too, behind an instrument-caused fault that this step removed.

**The rest is where 475 left it.** The four aborts are `copyout` from `load_init_program_at_path`
(`pc` 0x80011a00, `far` 0x1000), **`bzero`'s `L_64loop` from `exec_save_path`** (`pc` 0x80007a08,
`far` 0xc8215000), **`bcopy`'s `L64loop` from `copypv`** (`pc` 0x800076d4, `far` 0xc8256000), and
`copyout` from `exec_copyout_strings` (`pc` 0x80011a00, `far` 0x00101f28, the user stack) — the same
pairs 474's table has, with `pc` 0x20 lower because this step's entry code is that much smaller ahead
of them, and with the two middle pairs *named* rather than inferred from a nearest-symbol search
(`bzero.s:104` is `L_64loop`, `bcopy.s:88` is `L64loop`). **474's prose lists those two pairs one
position apart from its own table** — the table has `lr` 0x8028a680 on seq 2 and 0x8003f4a0 on seq 3,
which is `exec_save_path` and `copypv` — so the pair `bzero`/`exec_save_path` belongs to seq 2 and
`bcopy`/`copypv` to seq 3; 474's doc and its index row have been corrected to the table's order. The OS
console is byte-identical to 475's and 476's — `xnu_entry_ostext_chars = 0x50b` over 0x19 lines, ending
at `load_init_program: attempting to load /sbin/launchd` with no failure line and no panic text — and
`xnu_entry_kv_written = 8170` of 8192 with `_dropped = 33287` is the trace channel still full, which is
why nothing this step reads comes from it.

**Measured:** gate passed, exit 0, device back on Android by itself, log 487099 bytes / 5681 lines,
entry image `.text` 5224832 (+416), **image bytes 5438068 (unchanged — the growth is inside `.data`'s
16 KB alignment slack again)**, `.bss` 0x8052fa80..0x80587508, 44 `--wrap`ped symbols (41 reached, 1
same-object-only, 1 never called, 1 by address), undefined 26; payload `stage90-qcdt.img` 8458240,
sha256 `c9b3dd18…138718`.

## The build refused this image, and the refusal was wrong

**This is the part of the step that has to be written down.** The `--split` check failed:

    check_undef_handler: the split is wrong:
      `fleh_undef`'s source calls `fleh_undef`: the image's own code would be entering a handler whose
      frame arithmetic assumes a vector entry

and it failed **after** `objcopy` had written the `.bin` and the header, so the payload build and the
device run that followed used that image. The failure is the check's and not the image's, in the
cheapest way possible: `extract_function` returns a definition *including its signature*, so
`void fleh_undef(void)` contains the text `fleh_undef(` at brace depth zero, and the source half of the
check searched for exactly that. The ELF half — which computes every `bl` target from its encoding
inside the handler's range and requires none of them to be a handler — passed, and that half is the one
that answers the question.

Three things come out of it, and the first is the fix.

  - **The source search is now a depth rule** (`calls_at_body_depth`): a call inside a function is at
    depth >= 1 and a definition's own name is not. `--selftest` gained the two controls that would have
    caught it: the *unmutated* source must pass the source half, and a call inserted into the body must
    be refused. It also had a control that had never worked — `--selftest`'s call scan was pointed at
    Apple's `locore_fleh_dataabt`, whose `nm` size is 0 because `locore.s` emits no `.size`, so
    `handler_range` guessed "the next symbol above" and got **16 bytes**: the scan read zero `bl`s and
    the control failed for the wrong reason. `handler_range` now refuses a size it cannot read, and the
    control points at `fleh_irq` (size 36) with `entry_epilogue` forbidden, requiring at least one `bl`
    read.
  - **The image is proved to be the one the run used.** The entry build was re-run with the fixed check
    (exit 0, `xnu_entry_477: slot 1 splits on the interrupted mode in the vector page - user ->
    locore_fleh_undef (0x80014024), kernel -> fleh_undef (0x80006650)`), and the three artifacts are
    **byte-identical**: `xnu_arm_entry.bin` sha256 `efbda6ac…415144856`, `.elf` `ac44508b…dfb98bfb0`,
    `.h` `2a39d21f…b3e5109c2a` before and after. The check is not part of the image, so this had to be
    true; saying so is what makes the run's reading valid anyway.
  - **A build's status must be read before its artifact is used.** The failure printed and nothing acted
    on it: the image was written at 12:59:33, the check failed at 12:59:49, the payload was built at
    13:02 and the device booted at 13:04. The command that ran the build is not in the log — only its
    captured output is — so *where* the status went is not knowable from here, and the rule this step
    leaves is the one that does not depend on the answer: **when a step's artifact and its check can
    disagree, record the artifact's identity**, which is what the three hashes above are.

**What would have made this wrong, and did not.** If the trampoline's test were inverted, the user case
would have gone to `fleh_undef` and `xnu_entry_undef_user` would read 1 with `panic_entered` at 2 — the
report says 0 and 1. If the user literal pointed at this image's handler, the build would have failed
(the same `--split`, on the ELF half this time), and if it resolved to nothing the link would have. If
the two literals were swapped, `--split` compares both against `nm`. And if the split had not fired, the
five-entry live record of 476 would be back: `type = 3`, `far = 0x80006d04`, taken in user mode — there
is no `type = 3` in this run at all.

## What 478 has to do

**Name the `wait_result`.** `thread_block` returns `self->wait_result` (`sched_prim.c`'s
`thread_block_reason`), the wrapper for it already exists in `entry_trace.c`, and it **discards the
return value**. Capturing it is one line and one live key, and it turns the panic from a message into a
number. Two values are consistent with the enum and both are outside the switch: **10**
(`THREAD_NOT_WAITING`, `kern_types.h:81` — the wait was refused before it started, and
`waitq_assert_wait64_locked` returns it when the thread was already flagged awake) and **-1**
(`THREAD_WAITING` — the thread blocked and nothing woke it, and it is what `thread.c:250` initialises
every thread's `wait_result` to). The two readings are different claims about the machine: 10 says the
receive path was entered with a wait that could not happen, -1 says the wake never came — and with no
timer and no interrupt controller in this image, "the wake never came" is the reading that has an
already-owed fix behind it.

**478 did the capture and the answer is neither value, because the wrapper discarded more than the
number.** The "one line" above was the whole defect: `void __wrap_thread_block(void *)` against a
`wait_result_t`-returning function means the `r0` `ipc_mqueue_receive` switches on was whatever the
wrapper's tail call left there — so the panic this section's last paragraph reasons about, and the two
candidates above, are about a number the kernel never produced. With the declaration corrected the value
is `0` (`THREAD_AWAKENED`) on every one of the fifteen returns, and the run does not stop here at all.
See `experiment-478`; everything else this doc measured — the split, `SS_LR` vs `LR_und`, the absent
`type = 3` — stands, and the two paragraphs above are kept because *why* they looked right is the
lesson.
