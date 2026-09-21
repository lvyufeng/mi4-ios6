# Experiment 503 — the first block, and the leeway the kernel adds to every deadline

**One line.** Process 1 asks the kernel for *time* twice - `poll(NULL, 0, 5)` and `poll(NULL, 0, 40)` -
and the two intervals, timed with the kernel's own counter, come back **164047 and 900814 ticks** for
asks of 96000 and 768000: the ratio is **5.49:1 rather than 8:1**, and the two numbers it decomposes
into are both now named - the kernel's *requested* deadline is **33841 / 35662 ticks** longer than the
ask, and the wake arrives **33360 / 96810 ticks** after the deadline the kernel itself armed, because
`timer_call_enter_internal` adds a **coalescing leeway** to every deadline and `TIMEOUT_NO_LEEWAY` does
not suppress it (`deadline += slop`, `osfmk/kern/timer_call.c:620`). This is the first time on this
machine that a user process has made the kernel block.

    ./tools/host_ramdisk_macho_check.py --selftest               # 73 mutations refused
    ./tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf --selftest   # 48 refused
    ./tools/check_sysent_table.py --selftest                     # 14 refused, 230 = __wrap_poll
    ./tools/check_timer_line.py --selftest                       # 44 citations, 20 refused
    ./tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest        # 30 refused
    ./tools/check_irq_routing.py --image out/stage90/xnu_arm_entry.elf --selftest     # 57 refused
    ./tools/check_driver_catalogue.py --selftest                 # 18 claims / 131 refused
    ./tools/check_experiment_index.py                            # 479 rows

## What the step was for

Every experiment in this walk since 479 has run the same user land, and the walk's ledger has said for
a while what it is: `/sbin/launchd` is `g_stage90_ramdisk`, the program is a fixed sequence of
instructions, and the log shows it running `getpid` eight million times and nothing else. **The exec is
not the frontier and never was** - `load_init_program` prints on failure and on nothing else
(`kern_exec.c:5079-5173`), the console has the `attempting` line for `/sbin/launchd` and no `failed`
line after it, so the exec returned 0 and pid 1 has been running the fixture since 479. What the
fixture never did was *wait*. `getpid` answers out of `p->p_pid`, `mmap` answers out of the map, and the
third syscall in the program's history is the one that parks the thread on the kernel's clock.

So the object is the smallest block there is: **`poll` with no descriptors and a timeout.** Apple says
so in Apple's own words - "If user space passed 0 FDs, then respect any timeout value passed. This is
an extremely inefficient sleep" (`bsd/kern/sys_generic.c:1785`) - and the chain from there to a running
thread again is entirely inside this image:

    svc #0x80 (r12 = 230) -> fleh_swi -> unix_syscall -> sysent[230].sy_call
    the two asks -> poll_nocancel -> kqueue_scan                      sys_generic.c:1666/1674, kern_event.c:6009
    waitq_assert_wait64_leeway -> timer_call_enter_with_leeway(&thread->wait_timer, ...)
                                                                     kern_event.c:6094, waitq.c:2565
    thread_block_parameter -> the thread parks
    ... the virtual timer's interrupt (483's line):
      rtclock_intr -> timer_intr -> timer_queue_expire               osfmk/arm/rtclock.c, arm_timer.c
      -> thread_timer_expire -> clear_wait_internal(THREAD_TIMED_OUT)
                                                                     osfmk/kern/sched_prim.c:639
    kqueue_scan's switch -> EWOULDBLOCK, which `poll` maps to 0      sys_generic.c:1817-1820

## The program is two asks and a ratio

`entry_ramdisk.s` goes from twenty-seven instructions to **thirty-seven**: 480's block, then ten
instructions that make the two calls, then 479's loop moved down by ten and the same `udf #1` behind
it. The two asks are the same call with different numbers:

    mov r0, #0                 poll(NULL, 0, 5)      - no descriptor is waited on
    mov r1, #0                 nfds = 0              - so the timeout is the whole of the call
    movw r2, #POLL_SHORT_MS
    mov r12, #SYS_POLL         - 230, and `munge_www` marshals exactly r0..r2
    svc #0x80
    ... the same four instructions with POLL_LONG_MS = 40 ...

**The munger is what makes "exactly r0..r2" a check and not a hope**, and it is the one place where
this call differs in *kind* from 480's: `mmap`'s prototype has an 8-byte `off_t`, so its munger is
`munge_wwwwwl` and the word between `fd` and the offset is the struct's padding, which is why 480 has a
`movw r5` whose only job is to be recognisable. `poll`'s prototype is three 4-byte arguments, its
munger is `munge_www` (`out/xnu_generated/init_sysent.c:1317`), and **there is no padding word at all**
- so the two mungers' difference *is* the claim, and `tools/check_sysent_table.py` now requires both
words by name: `sysent[197].sy_arg_munge32` = `munge_wwwwwl` and `sysent[230].sy_arg_munge32` =
`munge_www`. An image whose slot named the six-argument munger would call `poll` with a timeout in the
wrong word of a struct `poll` reads as three words - and the call would still *work*, with a number the
fixture never asked for. That is a mutation now (`the six-argument munger in 230's slot`).

`SYS_POLL` is 230, read out of `bsd/kern/syscalls.master:330`
(`230 AUE_POLL ALL { int poll(struct pollfd *fds, u_int nfds, int timeout); }`) by
`syscall_poll()`, the same shape as `syscall_getpid()` and `syscall_mmap()` - and `check_sysent_table.py`'s
witness list gained `(230, "poll")`, so the fixture's three syscall numbers are now ten table entries
proved at one stride rather than nine.

**And neither ask branches on the answer, which is a change of kind rather than of degree.** Every
other check in this program takes a wrong answer to `entry_failed`, which is `udf #1` - and this
program *is* `/sbin/launchd`, so a fault taken here kills `initproc` and 478's run measured what
follows (`pid 1 exited -- exit reason namespace 2 subcode 0x4`, then `launchd_crashed_panic`). A `poll`
that returned an errno is a reading about the kernel's timer path and not a reason to end the boot, so
the two calls are made, the wrapper records the return either way, and the next instruction is the
loop. `host_ramdisk_macho_check.py`'s `program_expectations` states that as the words it requires
(`mov r12, #230; svc #0x80` and then the loop's first word) - not as an absence.

**The ratio is the control, and it is the whole reason there are two asks.** A wake that came from
anywhere other than the countdown - a fixed polling interval, a stray interrupt, the quantum - would
return after about the same number of ticks whatever was asked for, and one sample cannot tell that
from a clock. 5 ms and 40 ms are 8:1, and the property is checked as a property rather than against a
header (there is no header that says how long a fixture should sleep): `poll_timeouts()` reads the two
`movw r2` immediates back out of the instruction stream, and `check_program` refuses a pair that is
equal, reversed or zero, with four mutations that make each of those happen.

## The readings

Both calls returned, both returned **0**, and the caller is the kernel's own dispatcher - so the whole
path from the user's `svc` to the slot this wrapper occupies is a record and not an inference:

    call 1  timeout   5 ms   caller 0x80284848 (unix_syscall+0x100)  fds 0  nfds 0  error 0  retval 0
            before 0x070f469f  after 0x0711c76e  ticks 0x000280cf = 164047  (8.544 ms)
    call 2  timeout  40 ms   caller 0x80284848                       fds 0  nfds 0  error 0  retval 0
            before 0x0711c83b  after 0x071f8709  ticks 0x000dbece = 900814  (46.917 ms)

and the deadline family - `source 2`, `timer_call_enter_with_leeway`, the family `waitq.c` arms
`thread->wait_timer` through - was entered **18 times in this boot against 1 in 502's**, which is what
503's second change to `entry_timebase.c` exists to make visible (below). Two of the 18 are the polls,
and they are identifiable by construction: their `call` is a heap thread struct and their `flags` are
`0x10`, which is `TIMEOUT_URGENCY_USER_NORMAL` and **not** the `0x21` =
`TIMER_CALL_SYS_CRITICAL|TIMER_CALL_LEEWAY` the delayed-call timers use. Their `now` is 910 and 342
ticks after the respective wrapper's `before` - the time it takes `poll_nocancel` to get from the
wrapper's entry to `waitq_assert_wait64_leeway`:

    arming 1  call 0xc049a668  flags 0x10  now 0x070f4a2d  deadline 0x0711455e  requested 129841  (6.763 ms)
    arming 2  call 0xc049a668  flags 0x10  now 0x0711c991  deadline 0x071e0cdf  requested 803662 (41.857 ms)

`entry_counter()` is `mrrc p15, 0, lo, hi, c14` - the same counter `mach_absolute_time` reads on this
target - so every number in this section is measured with the kernel's own clock. The 19.2 MHz is the
run's own: `xnu_live_timerdrv_freq` and `xnu_live_timerdrv_cntfrq` are both `0x0124f800` and
`xnu_live_timerdrv_frame_freq_agree` is 1.

Two derivations, and they are the step's result:

  - **the kernel's requested deadline is 33841 / 35662 ticks above the ask** (1.763 / 1.857 ms), and
  - **the wake is 33360 / 96810 ticks after the deadline the kernel itself armed** (1.738 / 5.042 ms).

## The leeway the kernel adds, and why `TIMEOUT_NO_LEEWAY` does not stop it

The second number has a documented cause, and it is Apple's own arithmetic rather than this image's:

    timer_call_enter_internal(call, param1, deadline, leeway, flags, ratelimited)
    {
        urgency = (flags & TIMER_CALL_URGENCY_MASK);
        slop = timer_call_slop(deadline, ctime, urgency, current_thread(), &slop_ratelimited);
        if ((flags & TIMER_CALL_LEEWAY) != 0 && leeway > slop)
            slop = leeway;
        ...
        deadline += slop;
                                                    osfmk/kern/timer_call.c:1837 (the call), :620 (the add)

So `TIMEOUT_NO_LEEWAY` - which is what `poll` passes, `kern_event.c:6097` - is not "no leeway": it is
*no floor*, because the leeway argument is only consulted to raise the slop. `timer_call_slop`
(`:1784-1830`) computes the slop as

    adjval = MIN((deadline - now) >> tcs_shift, tcs_max_abstime);
    adjval += (adjval * timer_user_idle_level) >> 7;

with `tcs_shift`/`tcs_max_abstime` chosen from the urgency and the thread's priority out of the table
in `osfmk/arm/arm_timer.c:256-274` (`timer_coalesce_kt_shift` 3 and `timer_coalesce_kt_ns_max` 1 ms are
the kernel-thread row; the background row is `-5` and 100 ms). Two things follow, and both are what
the run shows: the slop **is a fraction of the time-to-deadline** (so the lateness grows with the ask)
and it is **capped** (so it stops growing). 33360 against a requested 129841 is 25.7%; 96810 against
803662 is 12.0%. Which row of that table this thread took, and what `timer_user_idle_level` was, are
**not** settled here - see *What is owed* - and the numbers above are what has to be explained rather
than the explanation.

The point of publishing them anyway is the shape: **a user-mode timeout on this machine is not a
timeout, it is a timeout plus a coalescing leeway the caller cannot decline**, and that is a fact about
every `poll`, `select`, `psynch` and `sleep`-with-timeout in the OS rather than about this fixture.

## The second excess: the two clocks `poll` moves between

The first number has no such source, and the place it comes from is six lines of `poll_nocancel`:

    if (uap->timeout != -1) {
        atv.tv_sec  = uap->timeout / 1000;
        atv.tv_usec = (uap->timeout % 1000) * 1000;
        if (itimerfix(&atv)) { error = EINVAL; goto out; }
        getmicrouptime(&rtv);                       /* <- the sbt system clock */
        timevaladd(&atv, &rtv);
    }
                                                    bsd/kern/sys_generic.c:1719-1723
    ...
    clock_get_uptime(&now);                         /* <- mach_absolute_time */
    nanoseconds_to_absolutetime(...atv..., &deadline);
    if (now >= deadline) { error = EWOULDBLOCK; break; }
    deadline -= now;
                                                    bsd/kern/kern_event.c:6054-6063

`getmicrouptime` is `microuptime` (`bsd/sys/time.h:208`) and `microuptime`
(`bsd/kern/kern_time.c:745`) is `clock_get_system_microtime`; `clock_get_uptime`
(`osfmk/kern/clock.c:1274`) is `mach_absolute_time()`. The timeout is converted into an absolute time
on one clock and the subtraction is done on the other, so **anything the two clocks disagree about
lands in the deadline with no sign that it did** - and in this image they disagree by 1.76 and 1.86 ms,
which is 33841 and 35662 ticks. That is arithmetic on two measured numbers (the requested interval and
the ask), not a remembered value: what would settle it *directly* is one more record in the poll
wrapper, and it is owed below rather than asserted here.

## The two claims this step's own instruments needed

  - **The deadline family had no rule that published it.** 481's window samples the first
    `STAGE90_TMR_ENTER_SHOWN` armings of *any* source, and in this boot those 64 are all the quantum
    metronome - source 3, the same arming over and over. Source 2 is the family `waitq.c` arms a
    parked thread's `wait_timer` through, i.e. the one a block on a deadline is *made of*, and
    `xnu_live_tmr_src2` was 1 in 502's run: the step that exists to read a deadline would have
    published the metronome and dropped its own subject. `entry_timebase_note_timer_enter` now branches
    on `source == 2u` first and publishes under its own keys (`xnu_live_tmr_dl_*`), with its own bound,
    and `check_timer_sources.py` gained six claims and six mutations for it: the branch exists, its
    eight keys are published, its window is bounded, the armings past the bound are *counted* rather
    than dropped, and **the two rules do not share a key** - because one key written under two rules is
    one key whose meaning depends on the run. 42 mutations -> **48**.
  - **The bound on that window is fitted to a measured count and says so.** It was 8, which published
    the boot's five delayed-call armings and would have dropped calls 9 and 10 - the polls. The run's
    own `xnu_live_tmr_src2` is 18 and `xnu_live_tmr_dl_calls` reaches 20, so the bound is **24** and the
    count is published at every power of two past it, so a boot that overflowed prints a number rather
    than a smaller table. `entry_timebase.c`'s comment says which of those two numbers the constant
    came from, because a constant chosen after the measurement is exactly the kind of thing this
    project's defect list is made of.

## The claims, and what the step's own code had to be told

  - `tools/host_ramdisk_macho_check.py`: the program is **37 words** against 27, the word-by-word
    comparison covers words 21..35 (the two asks and the loop) and every branch target moved with them
    (`entry_failed` is word 36, `spin` is word 31). Three words are **deliberately not fixed values**
    and the check says why: word 8 (the `mmap` pad marker, whose property is "nonzero and unlike every
    argument") and words 23 and 28, the two timeouts, whose property is the *ratio*. The low-bit
    mutation loop now excludes all three and four named mutations take their place - the second ask as
    long as the first, a timeout of zero, `poll`'s syscall number replaced by `mmap`'s, and a timeout
    written into `nfds`' register. **73 mutations refused.**
  - `tools/check_sysent_table.py`: `WRAPPED` and `WITNESSES` gain 230, `MUNGERS` gains
    `230: "munge_www"`, and two mutations are new (the real `poll` in the slot - the one that would
    leave a fixture that blocks for 5 ms and 40 ms and publishes *nothing* - and the six-argument
    munger in 230's slot). **10 witnesses, 14 mutations refused.**
  - `tools/check_timer_sources.py`: 48 mutations, as above.
  - `tools/check_timer_line.py` (44 citations, 20 refused), `check_os_entry.py` (30 refused - its two
    fault sites at `entry+0x38` and `entry+0x44` are in 480's block and did not move),
    `check_irq_routing.py` (9 claims, 57 refused) and `check_driver_catalogue.py` (18 claims / 131
    refused) are unchanged by this step and were re-run against the new image.
  - `build_entry.sh`'s wrap census now counts **60** `--wrap`ped symbols with `poll` in the
    `by_address` list beside `getpid` and `mmap`, because its only reference in the whole image is the
    word `sysent[230].sy_call` - the same *table* shape as the two before it, and the reason
    `check_sysent_table.py` reads the address out of the image rather than trusting the flag.

## The image

  - `.text` **5280384 -> 5280960** (+576: the ten new instructions in the fixture, the poll wrapper,
    and the census branch in `entry_timebase_note_timer_enter` - the new claims are host-side and cost
    the image nothing).
  - the entry image's **file is 5503612 bytes before and after**, so the payload does not move at all:
    `kernel_size` 5999040, `stage90.img` 6002688, `stage90-qcdt.img` 8523776, the RAM disk still
    `0x8050d000 + 0x2000`, `sizeofcmds` still `0xC4`, `.bss` still 362824 to `0x805983c8`. The 576
    bytes fitted inside the padding, which is 502's lesson read forwards: **the `.text` delta is the
    number that moves, and the file's is a band.**
  - and `STAGE90_TMR_DL_SHOWN` 8 -> 24 is a constant change that `.text` does not see - the same
    instruction with a different immediate - which the rebuild confirms at **5280960 both times**.

## The rest of the boot is unmoved

  - **The OS console block is byte-identical to 502's**, 131083 bytes with sha256 prefix
    `a6222524d5fa6a9b`, and the frontier line is the same one: `load_init_program: attempting to load
    /sbin/launchd`. This is *stronger* than 502's own evidence, which was a one-line diff: 503 did not
    move `STAGE90_XNU_RAMDISK_VA`, so even the line 502 had to normalise away is identical. The block's
    own counters agree: `_ostext_chars` = `_ostext_total` = 0x3ee with `_ostext_heals` 1, so it was
    neither truncated nor healed.
  - the fixture ran on through both blocks: `xnu_live_getpid_count` still reaches `0x800000`,
    `xnu_live_sleh_storm` is still 9, and 502's three deliveries reproduce (`_isr_calls` 1/2/3 on
    `_isr_intid` 0x28), with `_irq_timer_count` at `0x800` and `_irq_late_count` 0 - **the countdown
    the OS arms is still never late**, which is what makes the polls' leeway a property of the
    wait-timer path and not of the interrupt.
  - the log is 569236 bytes, `No errors detected`, exit 0, and the device came back on its own inside
    the capture window.

## What is owed

  - **Which row of `timer_compute_leeway`'s table this thread took, and what `timer_user_idle_level`
    was.** The two latenesses (33360 and 96810) fix the *shape* - a fraction of the time-to-deadline,
    capped - but not the fraction or the cap: 25.7% and 12.0% are not the same ratio, so the cap is
    binding on the second and not on the first, and the candidate rows differ by two orders of
    magnitude in `max`. The instrument is a `--wrap` on `timer_call_slop` (or on
    `timer_compute_leeway`) publishing `tcs_shift`, `tcs_max_abstime`, `timer_user_idle_level` and the
    returned slop beside the deadline it was added to - **a wrapper that publishes its own answer is a
    reading; a wrapper that publishes its inputs and lets the reader re-derive the answer is a
    second implementation of the function it is measuring.**
  - **The 1.8 ms between `microuptime` and `mach_absolute_time`.** The instrument is two lines in
    `__wrap_poll`: read `mach_absolute_time()` and `microuptime(&tv)` in the same breath (bracketed by
    a second `mach_absolute_time` so the read cost is in the record as well) and publish both. Then the
    excess stops being derived subtraction and becomes a clock difference with a bound on it.
  - **A third ask, so the leeway has three points instead of two.** The two latenesses are 25.7% and
    12.0% of their own time-to-deadline, and the model that produced them has three unknowns - the
    shift, the cap and `timer_user_idle_level`. Two points constrain a line and this is not a line, so
    a third ask (5, 40 and 400 ms, say) is what would say whether the first is below the cap and the
    second above it or whether both are below and the fraction itself is not constant. It also costs
    the fixture two more instructions and no new key.
  - **A third ask on the other side of a different boundary** is worth *not* doing: `itimerfix`
    (`bsd/kern/kern_time.c:601`) reads like the function that rounds a sub-tick timeout up to `tick`,
    but in 4570 it only validates - `tv_sec` in range, `tv_usec` in `[0, 1000000)` - and the rounding
    is gone. So there is no floor under these two asks, and the whole 1.763 ms is the clock difference;
    the draft of this section owed a 1 ms ask to test a floor that does not exist, and reading the
    function is what removed it.
  - **Still owed from 502, unchanged**: the frame's `0x038`/`0x03C` pair (what the register *is* needs a
    write, so it needs a design that cannot move XNU's own deadline), the frame's second line intid 39,
    the `AckC`/EOI question for a level line, the registry's 4-slot capacity stop, claim 16's
    one-level derivation reader, the citation rule over the other cited files, the unstamped
    `out/xnu_asm_obj`, 497's conditional-clause mutation, the unregister guard's asymmetry, a timeout
    that outlives its asker, `/timer`'s second definition, the other device nodes, the two services the
    catalogue answers with nothing, `MSM8974RootResource`'s `state0 = 0`, a name/class reader wider
    than eight characters, the release as a reading, `vm_fault`, 488's flag-list, 490's frames band,
    `xnu_live_dec_same`.
  - **And the one that matters for the goal.** User mode on this machine has only ever run this
    fixture, and the fixture's ceiling is now visible from both sides: it can ask for a page, block on
    a deadline and fault, and the *kernel* answers all three. What it cannot do is be an init - it has
    no way to open a file, and there is nothing for it to open. `load_init_program` execs
    `/sbin/launchd` out of a RAM disk the payload publishes, and every step from here that makes the OS
    *proceed* past `attempting to load` has to give that image more than 37 instructions: a driver that
    answers `open`, or a second fixture that runs instead of the spin and exits, so that
    `launchd_crashed_panic` is not the only way off the end.

## Safety

Every device touch went through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; the boot is a non-persistent `fastboot boot` and
nothing was flashed - the payload lives in RAM and the device was handed back to Android twice.
Two runs were spent on this step: the build as first written, and the build with the deadline window
widened from 8 to 24. Both returned `No errors detected` with the device back on its own inside the
capture window, and both recovery nets - the hardware watchdog and the software dead-man - were armed
in both.
