# Experiment 450 — the block stops terminating the run, and the scheduler gets a chance to dispatch the matching thread

**Status: built, gated, run on hardware — and the run is *silent*.** The step does what it set out to
do, and the measurement of what it did had to wait for 451/452 to build the channel that can see it.
The run below is this step's own result; the confirmation of its predictions is in
[452](experiment-452-the-live-console-works-and-the-boot-publishes-iobsd.md), and the reason this
step could not read its own result is in
[451](experiment-451-the-live-console-refuses-and-the-refusal-is-invisible.md). `.text` 5004992 ->
**5005952** (+0x3C0), entry bin and window unchanged, `.bss` `0x804f7a40 .. 0x80548b58`, payload
`1435f822...` 8228864 bytes.

## Why: 449's finding is a fact about a wrapper, and the wrapper is ours

449 measured `__wrap_thread_block` to be **terminal since experiment 268** — it recorded the caller
and the continuation and ran `entry_epilogue_block` instead of calling `__real_thread_block` — and
that the first `thread_block` on this boot is `ml_get_max_cpus+0x3c`, one instruction before the
scheduler would hand the CPU to `_IOConfigThread::main`, the async service-matching thread
`registerService` had already created (`kthread_cont1 = 0x80132960`, site `pingConfig+0x124`).

Three consequences, and this step is the first two:

  * 446/447/448's sentence — "the run ends in `ml_get_max_cpus`'s `thread_block`" — is true of the
    **report** and false of the **boot**. The wrapper did not observe the end of the boot; it *made*
    it.
  * The whole frontier model built on that sentence falls with it: 447's `initmax_cpus_count = 0`
    ("the flag's writer never ran") was measured *at the halt*, so it says nothing about what happens
    after. `MSM8974PlatformExpert::start` is exactly what runs after.
  * The untraced runs already say so from the other side. 439 stopped at `stub_hit=bpf_init` at
    `bsd_init+0x870` and 440 panicked in XNU's own device-tree check at `bsd_init+0x880`. `bsd_init`
    (`osfmk/kern/startup.c:629`) is *after* `PE_init_iokit` (545) and `vm_shared_region_init` (576),
    so those runs passed the block — which can only happen if `ml_init_max_cpus` ran
    (`osfmk/arm/machine_routines.c` writes `MAX_CPUS_SET` and wakes the waiter; nothing else sets
    that flag), i.e. if the platform expert's `start` ran. **The trace layer was what kept the boot
    from reaching it, and no image other than a traced one has ever failed to.**

## Why the switch should happen at all, read rather than assumed

`sched_startup` (`sched_prim.c:4704`) calls `thread_block(THREAD_CONTINUE_NULL)` too — at
`kernel_bootstrap_thread`'s line 431, *before* `PE_init_iokit` — and it does **not** switch away, and
the reason is in `thread_select` (`sched_prim.c:1782`), not in the scheduler being absent:

    boolean_t still_running = ((thread->state &
        (TH_TERMINATE|TH_IDLE|TH_WAIT|TH_RUN|TH_SUSP)) == TH_RUN);

`sched_startup` blocks **without** `assert_wait`, so `TH_WAIT` is clear, `still_running` is true, and
the dequeue that follows requeues a thread of equal priority rather than switching to it
(`sched_init_thread` is `MAXPRI_KERNEL`, the bootstrap thread's own priority — equal, not higher).
`ml_get_max_cpus` is the other shape: it calls `assert_wait((event_t)&max_cpus_initialized,
THREAD_UNINT)` first, so `TH_WAIT` is set, `still_running` is false, and `thread_invoke` has to commit
a switch to the best runnable thread. That is `_IOConfigThread::main` (created by
`kernel_thread_start` with priority -1, which `thread_create_internal` resolves to
`parent_task->priority` = `kernel_task`'s) over the idle thread.

There is a second, independent reason the first *recorded* block is `ml_get_max_cpus+0x3c` and not
`sched_startup`'s: **`sched_startup` and `thread_block` are in the same object** (`sched_prim.o`), so
the call is intra-object and `--wrap` does not rename it — 449's rule, applied to the very function
that report is about. `sched_startup`'s block has been the *real* one on every traced run since 268,
which is also the first evidence this project has that the real `thread_block` is safe to enter on
this device.

## The change

Two functions and a call in `entry_stubs.c`, one wrapper in `entry_trace.c`, and no new `--wrap`
(`TRACE_LDFLAGS` stays at eighteen; 108 `bl __wrap_thread_block` sites, unchanged):

    void __wrap_thread_block(void *continuation)
    {
        uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);

        entry_note_block(caller, (uint32_t)(uintptr_t)continuation);
        __real_thread_block(continuation);
        entry_note_block_return(caller);
    }

`entry_note_block` keeps the first **eight** `(caller, continuation)` pairs in a ring, the first pair
(a second time, as `_caller`/`_continuation`, because it is the site 446/447 resolved), the last pair,
and a count; `entry_note_block_return` counts the returns and keeps the first and last returning
caller. Eleven new `.bss` slots and eighteen new keys, all printed by the epilogue **outside** the
dump, which is 446's mechanism and the reason they survive a full record buffer.

Why the pair of counters and not one: `_count` above `_returned` is a boot thread that blocked and was
never woken (the match ran and did not set the flag); `_returned` rising with `_count` is the
scheduler working. One caller slot cannot tell those apart, and this step's whole question is which
one happens.

**What `entry_epilogue_block` does now: nothing.** The function stays, with a comment saying which
step retired it and why a terminal wrapper is still the right shape for something that really is the
end of a run — deleting it would hide which step took it out of use.

## The prediction, written before the run

| key | predicted | why |
| --- | --- | --- |
| `xnu_entry_block0_caller` | **`0x800087e4`** = `ml_get_max_cpus+0x3c` | unchanged from 446/449: the first *wrapped* block, because `sched_startup`'s is intra-object and was always real (`caller - 4` is the `bl`, as everywhere) |
| `xnu_entry_block_count` | **>= 2** | the boot thread's block, then at least one more from the resumed boot (the work-loop thread and any `semaphore_wait`/`assert_wait` site the boot now reaches) |
| `xnu_entry_block_returned` | **>= 1** | the block switches away and `thread_wakeup(&max_cpus_initialized)` from `ml_init_max_cpus` brings the bootstrap thread back |
| `xnu_entry_initmax_cpus_count` | **1**, `_arg` **1** | `MSM8974PlatformExpert::start`'s third statement — the writer 447 measured at zero, and the headline row of this step |
| `xnu_entry_pub20_key` | **`0x8046d690`** = `"IORTC"` | `start`'s second statement, `IOService::publishResource("IORTC")`; 449's four-deep ring exists for this |
| `xnu_entry_alloc_count` | **>= 1** | matching instantiated a class by name, which is `probeCandidates` reached |
| `xnu_entry_maxcpus_count` | **1**, unchanged | the first call is still the blocking one |
| the run's report | a **`stub_hit=`** on one of the image's 27 stubs, or a fault | the stop moves to whatever `bsd_init`'s tail asks for next, which is the point of the step |

Ranked alternatives, so a miss names its own cause:

  1. **`block_count >= 1`, `block_returned = 0`, `initmax_cpus_count = 0`.** The switch happened and
     nothing woke the bootstrap thread: the matching thread ran but did not complete a match, or did
     and did not reach `ml_init_max_cpus`. Then `pub2_count` and `alloc_count` say which half.
  2. **`block_count = 1` and `block_returned = 1` with `initmax_cpus_count = 0`.** The block returned
     *without* switching — `still_running` was true because `assert_wait`'s `TH_WAIT` was cleared
     before `thread_block` (the mechanism above is wrong, not the apparatus), and the boot then
     continued with `max_cpus_initialized == MAX_CPUS_WAIT` and `machine_info.max_cpus` = 0.
  3. **A panic out of Apple's fallback.** If `MSM8974PlatformExpert`'s `IONameMatch` does not match
     the provider, the config table's second personality (`IOPanicPlatform`, `IOProbeScore = 0:32`, no
     `IONameMatch`) matches *any* `IOPlatformExpertDevice` provider and XNU panics. That would be
     `pub2_count = 0` plus a `panic:` line — the table doing exactly what this project built it to do.
  4. **Silence.** If the run ends in a hang rather than a stop, nothing reports: `entry_write_kv`
     writes the ram console at VA `0xde500000`, and that address has no translation while XNU's own
     page tables are live (268's own `exception: data abort`, `dfar=0xde500000`, `pc` inside
     `entry_write_kv`). **This is the step's one new exposure and it is a real one** — everything the
     instrument has recorded up to now is in `.bss` and `.bss` is only read out by the epilogue. The
     nets answer it (watchdog, dead-man, `fastboot boot` only), and a live channel from inside XNU is
     its own step; the candidates are (a) `ml_io_map(0xde500000, size)` if that object's pmap path is
     usable this early, (b) a section descriptor installed into TTBR1's L1 for VA=PA 0xde500000 with
     a TLB invalidate, (c) a scratch register that survives a reset. None is attempted here.

Falsifiers of the instrument, named in advance: `block_count = 0` (the wrapper is not on the path at
all — the routing was checked, 108 sites); `block_returned > block_count` (impossible, so the
counters are not what they claim); and a `block0_caller` of `sched_startup+...`, which would falsify
449's intra-object rule at the one call site that can test it.

## What the run said: 294738 bytes, and no record of the step at all

    wrote 294738 bytes to /tmp/cancro-450-last_kmsg.txt
    25 x persistent_write_attempted=0x00000000
    87 x failure_mask=0x00000000
    no stub_hit=, no exception:, no panic:
    last line: MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
    device returned on its own

Every `xnu_entry_*` key in that log is the payload's ladder, written before the jump; **not one key
of this step's eighteen appears**, and neither does any of the instrument's other reports. The step
therefore *bears on nothing* in this run's log: it neither confirmed nor refuted its own prediction,
and the honest reading of 450 alone is "the boot was no longer stopped at the block" (which 449 had
already established by removing the halt) plus "the instrument cannot see where it went next".

## What 452 then measured, on this design

The live console 451 built and 452 repaired is what turned this step's prediction table into data,
and every row of it held, on the image of 452:

| key | predicted | measured |
| --- | --- | --- |
| `block0_caller` | the first *wrapped* block, `ml_get_max_cpus+0x3c` | `0x80009234` = **`ml_get_max_cpus+0x3c`** — the same offset 446/449 measured, the `bl` at `+0x38`; only the address moved, because the image did |
| `block_count` | >= 2 | **33** |
| `block_returned` | >= 1 | **8** |
| `initmax_cpus_count` | 1, `_arg` 1 | **1**, `_arg=0x00000001`, caller `MSM8974PlatformExpert::start+0x28` |
| `pub20_key` | `"IORTC"` | **`"IORTC"`** (`0x8046e7e8`, and a second publish of `"IOBSD"`) |
| `alloc_count` | >= 1 | **2** |
| `maxcpus_count` | 1 | **4** (`commpage_populate+0x70`, `mcache_init+0x20`, `mbinit+0x684`, `sysctl_mib_init+0xc0`) |
| the run's report | a `stub_hit=` or a fault | **neither, for a long time** — the boot ran on past every stub that 439/440 stopped at and ended in a *wait*, not a stop |

So the step's whole claim is confirmed from the inside: the wrapper's retirement did not move the
boot, it removed the instrument's own stop, and the scheduler that 449 could only predict does run —
33 blocks, 8 returns, and the flag's writer reaching its third statement with `max_cpus == 1`.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the
device returned to Android on its own.
