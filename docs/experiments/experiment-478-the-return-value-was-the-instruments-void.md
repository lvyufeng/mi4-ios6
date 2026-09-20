# Experiment 478 — the return value was the instrument's, and the panic it caused

**477's agenda was to name the `wait_result`, and the number that came back names something else
first.** The wrapper that this step was supposed to extend had been declared `void`, and `thread_block`
returns a value: so from 447 until this step the image handed every caller of `thread_block` an
arbitrary `r0`, and `ipc_mqueue_receive` compares that register with 3 and indexes a jump table with it
(`0x800e9f44`, `cmp r0, #3` / `bhi panic` in this image). **The panic that 476 and 477 both stopped on
— `"ipc_mqueue_receive_results: strange wait_result"` — was this image's own doing.** With the ABI
fixed, the run does not stop there at all: it goes on to kill process 1 and panic because init died,
which is a **later and more informative** stop than the one two documents were built on.

## The capture, and the number it found

The value is captured (and returned unchanged, which is the whole ABI of a `--wrap`) and the histogram
the report writes has one slot per member of `wait_result_t`:

    xnu_entry_block_returned            0x0000000f     15 blocks came back
    xnu_entry_block_results_k0          0x0000000f     ... every one of them THREAD_AWAKENED
    xnu_entry_block_results_k1 .. _k6   0x00000000     nothing else, not once
    xnu_entry_block_first_odd_seq       0x00000000     no return was ever outside 0..3
    xnu_entry_block_result              0x00000000     the last return, from 0x800e9f40
    xnu_entry_block_result_caller       0x800e9f40     = the `bl` at 0x800e9f3c in ipc_mqueue_receive

and the live channel says the same thing in its own order - `xnu_live_block_result=0x00000000` on every
one of the fifteen `xnu_live_block_return` records, `_returns` running 1..15, and **no
`xnu_live_block_odd_*` key at all**, which is the first-odd branch not being taken and therefore also
the first live evidence that the branch exists and is not firing.

**So the two candidates the 477 doc named - 10 (`THREAD_NOT_WAITING`) and -1 (`THREAD_WAITING`) - are
both wrong, and neither was ever there.** The wake always came. The 57 of 72 blocks that never returned
(`xnu_entry_block_count = 0x48`, `_returned = 0x0f`) are threads that are still blocked in a machine
with no timer and no interrupt controller to wake them - a *different* observation, and one that was
already in the record.

The call site is worth reading in full, because it is what the next paragraph is about:

    800e9f2c: movw r0, #0x9e0c ; movt r0, #0x800e   ; r0 = 0x800e9e0c = ipc_mqueue_receive_continue
    800e9f34: bl __wrap_thread_block                ; thread_block(continuation) - never taken here
    800e9f38: mov r0, #0
    800e9f3c: bl __wrap_thread_block                ; thread_block(THREAD_CONTINUE_NULL)
    800e9f40: mrc 15, 0, r1, cr13, cr0, {4}         ; the return address recorded in the log
    800e9f44: cmp r0, #3
    800e9f48: bhi 800e9fe8                          ; ... which is `ipc_mqueue_receive_results`'s
    800e9f4c: add r2, pc, #0                        ;     `default:` arm, inlined, panicking
    800e9f50: ldr pc, [r2, r0, lsl #2]              ; jump table on the value in r0

The first call is the continuation form and **was never executed in this run** (there is no
`xnu_live_block_enter = 0x800e9f38` anywhere in the log), so every record from `0x800e9f40` is the
`THREAD_CONTINUE_NULL` call whose result the switch reads directly out of `r0`.

## The defect: a `--wrap` is an ABI substitution, so no compiler could see it

From 447 to 477 the wrapper's two declarations were these, verbatim from `git show 1649447`:

    void __real_thread_block(void *continuation);
    void __wrap_thread_block(void *continuation)
    {
        uint32_t caller = ...;
        entry_note_block(caller, continuation, entry_thread(), entry_counter());
        __real_thread_block(continuation);
        entry_note_block_return(caller);
    }

`thread_block` is `extern wait_result_t thread_block(thread_continue_t continuation)`
(`osfmk/kern/sched_prim.h`, `sched_prim.c:2964` returning `thread_block_reason`'s
`return (self->wait_result)`), so this is a header value **declared away**, and the two shapes compile
to different code. Built for `armv7` with this image's own flags:

    __wrap_thread_block_void:                     __wrap_thread_block_int:
      mov  r4, lr                                   mov  r5, lr
      bl   __real_thread_block                      bl   __real_thread_block
      mov  r0, r4        ; the caller's `lr`        mov  r4, r0        ; the value, saved
      mov  r1, #0                                   mov  r0, r5
      b    note          ; tail call, r0 = note's   mov  r1, r4
                                                     bl   note
                                                     mov  r0, r4        ; the value, restored
                                                     pop  {pc}

The `void` shape ends in a **tail call to a `void` function**, so the `r0` its caller sees is whatever
`entry_note_block_return` leaves there - in this image `entry_live_write`'s last `bl entry_write_kv`'s
return value. It is not `thread_block`'s, and it is not 0..3 by accident. **And nothing in the build
could say so**: `ipc_mqueue_receive` is compiled from Apple's own source with Apple's own prototype and
reads `r0` as a `wait_result_t`; the wrapper is substituted at *link* time by `ld --wrap`, so the two
disagree only in the register file at run time.

This is the project's "a measurement can be the thing that is wrong" class with a new tell, and it is
the first one where the instrument changed the *program* rather than the report: the fault was not that
a number was read wrongly, it was that a number the kernel asked for was replaced by an instrument's.

## What the run did instead: process 1 was killed by SIGILL, and init's death is fatal

The OS's own console text is the reading, and it gained exactly one line over 476's and 477's
(`xnu_entry_ostext_chars` 0x50b → **0x552**, `_lines` 0x19 → **0x1a**, and 71 characters is that line
plus its CR and LF):

    load_init_program: attempting to load /sbin/launchd
    pid 1 exited -- exit reason namespace 2 subcode 0x4, description none

`namespace 2` is `OS_REASON_SIGNAL` and `0x4` is **SIGILL** (`bsd/sys/reason.h:87`), which is the `udf
#0` at `0x10e0` - the RAM disk Mach-O's entry point, and 474/475/477's measurement arriving at its
consequence. The kernel triaged the bad instruction, killed pid 1, and then ran
`bsd/kern/kern_exit.c:514`'s `launchd_crashed_panic`, which panics **unconditionally**: `proc_prepareexit`
(`:846`) is `if (p == initproc) { launchd_crashed_panic(p, rv); /* NOTREACHED */ }`.

**The panic is identified from two independent sources and neither of them is the log's prose.** The
trap's `r9` - the last format string this image put in the register, the reading 461 built - resolves in
*this* image to

    0x804b7953   "%s %s -- exit reason namespace %d subcode 0x%llx description: %.800s"

which occurs once in the tree, at `bsd/kern/kern_exit.c:574`, inside `launchd_crashed_panic`. And the
argument walk (`xnu_entry_panic_args_ok = 1`) yields exactly that format's first two `%s` arguments:
`arg0 = 0x804f637b` is the **empty string** - `(p->p_csflags & CS_KILLED) ? "CS_KILLED" : ""` - and
`arg1 = 0x804b78f8` is `"initproc exited"`, the literal the function selects when `p_name` is not
`preinit`. A format that needs a string, an empty string, and a second string that the source at that
line spells: the record names the function, the line, and the two values.

Everything else in the trap is unchanged from 477: `xnu_entry_panic_entered = 1` (only `panic`'s `udf`
reached this image's `fleh_undef`, and the user `udf` went to Apple's through the vector-page split),
`undef_pc = 0x800364c0` = `DebuggerTrapWithState+0x28`, `spsr = 0x60000093` = SVC; and the live channel's
`xnu_live_undef_pc = 0x000010e0` / `_spsr = 0x00000010` / `_user = 1` / `_lr = 0` says the same thing
about process 1 that 477 measured.

## The check: the mapping is now a build failure

The step's reading is a comparison of six numbers this image writes out by hand against
`osfmk/kern/kern_types.h`, so `tools/check_block_result_slots.py` makes it structural. It reads the
`THREAD_*` values out of **Apple's header** (between the `Possible wait_result_t values` comment and the
`thread_continue_t` typedef, so it cannot pick up `THREAD_UNINT`'s neighbours) and compares them with
`entry_block_result_slot`'s `case` labels *and the member name in each case's comment* - the comment is
the claim, and a case labelled `/* THREAD_RESTART */` holding 10 is exactly the defect a diff cannot
show. It then requires the four values `ipc_mqueue_receive_results` accepts to be slots 0..3
(the property that makes the epilogue's histogram and the live values the same statement), every header
member to be a case, `ENTRY_BLOCK_RESULT_SLOTS` to be the member count plus the default, the seven key
names to exist once and in slot order, `g_block_results` to be declared with that macro, and the two
histogram writes to be loops over it. `--selftest` breaks each claim in turn - **all 14 mutations
refused**, plus a mutated header, which is the control that says the values are being read from
`kern_types.h` and not from a table in this image.

## What the two documents before this one got wrong

**476's and 477's read of the panic is retracted, and the mechanism they named as its cause is not
it.** 476's doc reasoned: "`thread_block` was entered from `ipc_mqueue_receive` (0x800e9f40), it
**returned**, and the very next event is `panic`'s `udf`. The statement after the block in
`ipc_mqueue_receive` is `ipc_mqueue_receive_results(wresult)`. **What `wresult` was is not in this
log**". That was right about the site and right that the value was missing; the value was missing
*because the wrapper discarded it*, not because the log was silent. 477's doc then wrote the sentence
this step was built to test - "the two readings are different claims about the machine: 10 says the
receive path was entered with a wait that could not happen, -1 says the wake never came" - and the
answer is that neither reading applies, because the number the switch received was never
`thread_block`'s.

**The lesson is not "the wrapper had a bug".** It is that a `--wrap` is the one place in this build
where a wrong declaration cannot be caught by any number of checks on *values*, because the compiler
never sees both sides at once: the caller's prototype comes from Apple's header and the callee's is
written in this image, and `ld --wrap` is what joins them. Every other wrapper in `entry_trace.c` that
returns what the real function returns was verified by reading the real function's declaration; this
one was verified by reading the wrapper, and the wrapper said `void`.

## What 479 has to do

**Retire `launchd_crashed_panic` by not killing process 1, and the way to do it is the vector table's
slot 2.** The stop is now `if (p == initproc) launchd_crashed_panic(p, rv); /* NOTREACHED */`, reached
because the fixture's first instruction is a `udf #0`. Any exit of process 1 panics, so the fixture
cannot be given an `exit` or a fatal fault: it has to be given something that **keeps running**, and the
only way for a user instruction to do that in this image is a `svc` the kernel can service - which means
slot 2 (`fleh_swi`) goes to Apple's own first-level handler the way 467 moved slots 3 and 4 and 477 moved
slot 1. That is the same move on a third slot, and the interesting part is what it buys: the syscall
entry is the first thing in this boot that would run *because process 1 asked for it*, and with no
timer and no interrupt controller it is also the first thing that could park a process legitimately
instead of hanging it.

**A user-mode busy loop is not a step.** It is the obvious cheap alternative - one instruction in a
fixture this project owns, no new kernel facility - and it retires the name by construction. But its
only reading would be "the kernel no longer runs", and the record already has that number from the other
side: `xnu_entry_block_count = 0x48` against `_returned = 0x0f` says **57 of 72 blocks are still waiting
for a wake that has nothing to send it**. A parked process 1 with a working timer is progress; a
spinning one without a timer is the same hang with more CPU time in it.

**And the timer is now on the critical path in a way it was not before.** `ml_init_timebase` plus an
MSM8974 `tbd_ops_t` over the GPT at `0xf9020000` (19.2 MHz, IRQ 19) and `IOCPUInterruptController` were
owed before this step; what is new is that they are now what stands between "process 1 parks in a
syscall" and "the boot continues", rather than a name on a list.

**Measured:** gate passed, exit 0, device back on Android by itself, log 488475 bytes / 5721 lines /
3933 payload lines, `.text` 5225568, image bytes 5438068 (unchanged - inside `.data`'s 16 KB alignment
slack), `.bss` 0x8052fa80..0x80587548, 44 `--wrap`ped symbols (41 reached by a branch, 1 same-object
only, 1 never called, 1 by address only), undefined 26, `xnu_entry_kv_written = 8170` of 8192 with
`_dropped = 0x8213`, the trace channel still full (461's defect on the shared buffer, unchanged and not
this step's), and payload `stage90-qcdt.img` 8458240 sha256
`dfa2aabd42c39968fc75bf5e757dd9644b0b17199192202930d06f11e37ff9f5`. The run's own counters:
`xnu_live_sleh_seq = 4` with `_back = 4`, every entry `type = 4` with `frame_ok = 1` and `_user = 0`.
