# Experiment 453 — the thread identity, and the site above the sleep

**Status: built, gated, run on hardware.** 452's trace could say *what* the boot was waiting in and
never *who* was waiting or *where* the wait was asked for. 453 adds both, and the second one turns out
to name a path the primitive's own caller list had hidden: the frontier is **`init_thread` — XNU's own
static bootstrap thread (`osfmk/kern/thread.c:184`, a `.bss` symbol at `0x804fcdd0` in this image) —
blocked in `lck_mtx_sleep_deadline` at a site that is not `_sleep`, and therefore one of IOKit's two
`...SleepDeadline` entries**, which 452's candidate list did not contain. The run is 301811 bytes:
**221 live records** (206 after the header), 33 `thread_block`s with 8 returns, **23 distinct
threads**, 6 calls into the sleep family, 16 `kernel_thread_start`s, and `"IOBSD"` published again.
`.text` 5009408 -> **5011584** (seven wrappers), entry bin 5208596, `.bss` `0x804f7a40 .. 0x80548c18`,
payload `57ded60f...` 8228864 bytes.

## The thread identity: one instruction, and it verifies itself

`entry_note_block` records the address the wrapper was called from, which names the primitive that
slept and nothing else. On ARMv7 the thread is free: `current_thread()` is a read of TPIDRPRW
(`osfmk/arm/cpu_data.h:58`), and `cswitch.s` writes that register on every context switch
(`cswitch.s:84`, and again in `Load_context` and `Thread_continue`), so a read taken between two
switches is the thread running there. No memory is touched, nothing can fault, and there is no
per-CPU lookup to get wrong:

    mrc p15, 0, %0, c13, c0, 4

The wrapper carries it, and the block record becomes a triple `(thread, caller, continuation)`:
`g_block_ring_thread[8]`, `g_block_last_thread`, `g_block_first_thread`, and a live
`xnu_live_block_thr` beside every `xnu_live_block_enter`.

**The reading checks itself.** The first block's thread is `0x804fcdd0`, and that address is
`init_thread` in the image's own symbol table - `static struct thread thread_template, init_thread`
(`thread.c:184`), the structure `thread_bootstrap` copies and hands to
`machine_set_current_thread` (`thread.c:397-398`). So the very first value the instrument ever read
out of TPIDRPRW is the address of XNU's own bootstrap thread, which is a claim about the *image* and
not about the run: `arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | grep init_thread` says
`0x804fcdd0`, 0x680 bytes, `b`. Every other thread in the trace is above `0xc0000000`, i.e. heap, i.e.
allocated by `kernel_thread_start`.

The kernel agrees, in its own code: `Call_continuation` begins with the same read -
`0x800e8d20: mrc 15, 0, r9, cr13, cr0, {4}` followed by `0x800e8d24: ldr sp, [r9, #1464]`, i.e. read
the thread from TPIDRPRW and take its stack pointer out of it - so the register the wrapper reads is
the register XNU switches stacks by, not a convention of this instrument.

## The site above the sleep: no frame pointers, so wrap the family

One frame up from the block is not reachable by walking. This tree compiles C with
`-fomit-frame-pointer`, and it is measurable rather than assumed: only **9** functions in the whole
5 MB image set `fp` from `sp` (`backtrace`, `m_devget`, `inet_pton4`, `kpc_set_running`,
`nd6_sysctl_drlist`, `tcq_enqueue_ifclassq`, `thread_affinity_exec`, `apple_protect_pager_trim`,
`IOCPUInterruptController::setCPUInterruptProperties`), and none of them is on this path. `msleep`'s
prologue is `push {r4, r5, r6, r7, r8, r9, fp, lr}` - the `fp` is there for 8-byte stack alignment
and is never written - so `__builtin_return_address(1)` and an r11 chain are both fiction.

Wrapping is not fiction, and the wrap set is decidable. `_sleep` is `static` in `kern_synch.c` and
`lck_mtx_lock_contended` is a local symbol (`nm` says `t`), so neither can be intercepted: `--wrap`
renames an *undefined reference*, and a call to a local symbol in its own object is neither. But
`grep -n '_sleep(' bsd/kern/kern_synch.c` finds exactly **seven** call sites - 311, 328, 346, 357,
371, 386, 397 - and all seven are global functions:

| id | entry | fifth argument | `nm` |
| --- | --- | --- | --- |
| 0 | `sleep(chan, pri)` | - | `800...T` |
| 1 | `msleep(chan, mtx, pri, wmsg, ts)` | `struct timespec *` | `T` |
| 2 | `msleep0(chan, mtx, pri, wmsg, timo, cont)` | `int timo` in seconds | `T` |
| 3 | `msleep1(chan, mtx, pri, wmsg, abstime)` | `u_int64_t abstime` | `T` |
| 4 | `tsleep(chan, pri, wmsg, timo)` | `int timo` | `T` |
| 5 | `tsleep0(chan, pri, wmsg, timo, cont)` | `int timo` | `T` |
| 6 | `tsleep1(chan, pri, wmsg, abstime, cont)` | `u_int64_t abstime` | `T` |

Wrapping those seven is therefore **complete** for every sleep in the BSD boot, without a frame
pointer and without touching a line of XNU - and it has to be: the first version of this step wrote
six, having assumed from the name that `tsleep0` was the deadline variant of `tsleep`. It is not
(`kern_synch.c:375-386`: `tsleep0` takes `int timo` and computes the deadline itself, and the
`u_int64_t abstime` variant is `tsleep1`, seven lines below at 390). The table above is written from
the source lines rather than by analogy for exactly that reason.

Each wrapper records what its own `lr` was on entry - the function that asked to sleep, one call
above `_sleep` - plus the arguments the source's own branch test uses, so the report can name the
wait in the site's own words: `site`, `thr`, `chan`, `wmsg`, `pri` and `tmo`. `wmsg` is the wait
message string, and it is worth having because it resolves host-side against the image (452's
`pub2_key` reading, applied to a string the site itself chose). Nothing changes an argument or a
return value; `TRACE_LDFLAGS` goes 18 -> 25.

## The measurement

    wrote 301811 bytes to /tmp/cancro-453-last_kmsg.txt
    221 x xnu_live_* records, of which 15 are the 452 header
    33 block_enter, 8 block_return, 23 distinct threads
    6 x sleep_ent, 7 records each
    25 x persistent_write_attempted=0x00000000
    87 x failure_mask=0x00000000
    no stub_hit=, no exception:, no panic:
    device returned on its own

The 33 blocks, in order, with the thread that ran each and the call resolved against this image
(`caller - 4` is the `bl`):

| # | thread | call site | outcome |
| --- | --- | --- | --- |
| 1 | `init_thread` | `ml_get_max_cpus+0x38` | returned |
| 2, 3 | `c0547780`, `c0545080` | `zone_replenish_thread+0x6c` | never |
| 4 | `c0544a00` | `mapping_replenish+0x28c` | never |
| 5, 7, 15, 16, 31, 33 | four threads (six records) | `Call_continuation+0x18` | never |
| 6 | `init_thread` | `lck_mtx_sleep_deadline+0x88` | returned |
| 8, 10, 13, 14, 18 | `init_thread`, `c0543000` | `lck_mtx_lock_contended+0xb4` | returned |
| 9 | `init_thread` | `lck_mtx_sleep+0x80` | returned |
| **19** | **`init_thread`** | **`lck_mtx_sleep_deadline+0x88`** | **never** |
| 20 | `c057f480` | `memorystatus_thread+0x88` | never |
| 21 | `c0549180` | `async_work_continue+0x40` | never |
| 11, 17, 22, 32 | four threads | `thread_terminate_self+0x2f8` | never |
| 12, 26, 27, 28, 30 | five threads | `_sleep+0x110` | never |
| 23, 24, 25 | `c057e100`, `c057e780`, `c057d400` | `mbuf_worker_thread+0x48`, `aio_work_thread+0x18c` x2 | never |
| 29 | `c057c080` | `lck_mtx_sleep+0x80` | never |

`Call_continuation+0x18` is `blx r6` (`0x800e8d38`) and the record is its return address, so those six
records are 449's tail-branch rule at thread startup: the thread's own entry point is *not* in the
key. The offsets in this table are the **calls**; the records carry the return addresses, four higher.

And the six sleep calls, each with the wmsg string read out of the image, paired with the block the
*same thread* reached a moment later:

| # | entry | site | thread | wmsg | block |
| --- | --- | --- | --- | --- | --- |
| 1 | `msleep0` | `bcleanbuf_thread+0x90` | `c0549800` | `"blaundry"` | 12 `_sleep+0x110` |
| 2 | `msleep0` | `nwk_wq_thread_func+0x44` | `c057cd80` | `"nwk_wq_thread_func"` | 26 `_sleep+0x110` |
| 3 | `tsleep0` | `pf_purge_thread_fn+0x2c` | `c057ee00` | `"pf_purge"` | 27 `_sleep+0x110` |
| 4 | `msleep0` | `flowadv_thread_func+0x44` | `c057c700` | `"flowadv"` | 28 `_sleep+0x110` |
| 5 | `msleep` | `dlil_main_input_thread_func+0x80` | `c057c080` | (NULL) | 29 `lck_mtx_sleep+0x80` |
| 6 | `msleep0` | `ifnet_detacher_thread_func+0x44` | `c057ba00` | `"ifnet_detacher"` | 30 `_sleep+0x110` |

Every pair is the source's own branch: `msleep0`'s callers here pass a NULL mutex, so `_sleep` takes
the `else` branch and calls `thread_block` directly (`_sleep+0x110`), while `dlil_main_input_thread`
passes a mutex and no deadline, so `_sleep` calls `lck_mtx_sleep` (`kern_synch.c:199`) and the block
is inside *that*. The pairing is the step's second result: a block and a sleep on the same thread
address are the same event seen from both ends, and the thread pointer is what makes them the same
thread. All six carry `tmo = 0`: **not one of the six has a timeout**, which is the normal shape of a
worker parking on a channel, and it is what makes the boot thread's wait stand out.

## The finding: the frontier is `init_thread`, and it is an IOKit wait

The boot thread has **6 blocks and 0 sleeps**. That negative is the result. It is not a gap in the
instrument - it is arithmetic on the image:

  * the block record for #19 says the primitive is `lck_mtx_sleep_deadline`;
  * that symbol has exactly **three** callers in this image: `_sleep+0x21c` (`kern_synch.c:197`),
    `IOLockSleepDeadline+0x20`, and `IORecursiveLockSleepDeadline+0x38`;
  * `_sleep` is `static`, so it is reachable only from the seven global entries - and `grep -nE
    '^\s*(return\s+)?(msleep|msleep0|msleep1|tsleep|tsleep0|tsleep1|sleep)\s*\(' kern_synch.c`
    matches only the seven *definitions*, i.e. nothing inside that object calls them, so every call
    into them from anywhere else is an undefined reference and every one is now wrapped;
  * the boot thread called none of the seven.

Therefore the boot thread's wait came from `IOLockSleepDeadline` or `IORecursiveLockSleepDeadline`.
452 said this primitive has two callers - `msleep` and `vm_pageout.c:3491` - and both halves of that
were wrong: `vm_pageout_wait`, the only caller at that line, is inside `#if !CONFIG_EMBEDDED` and this
boot is `CONFIG_EMBEDDED=1` (`nm | grep -c vm_pageout_wait` = **0**), while the two IOKit entries that
*are* in the image were not in the list at all. The candidate list was read from three files and
never checked against the link.

What the boot thread was doing around it is legible from the same table: it waited once already in the
same primitive and that wait **returned** (block 6), took and released contended mutexes (8, 10, 13,
14, 18) and a non-deadline mutex sleep (9), and then waited a second time and did not come back (19) -
while the threads it created went off and parked one by one (12-33). That is the shape of a boot
thread waiting for something to be published by a service that is not coming, with a deadline that
has not fired.

## What the next step measures

`IOLockSleepDeadline(IOLock *, void *event, AbsoluteTime deadline, UInt32 interType)` and
`IORecursiveLockSleepDeadline` are both global (`T`), so both are wrappable (`25 -> 27`), and the
call sites of each are a closed list of five in this image:

| entry | caller in this image | would read |
| --- | --- | --- |
| `IORecursiveLockSleepDeadline` | `IOService::waitForMatchingService(OSDictionary *, unsigned long long)` | `...+0xe0` |
| `IOLockSleepDeadline` | `IOService::waitMatchIdle(unsigned long)` | `+0x60` |
| `IOLockSleepDeadline` | `IOService::scheduleTerminatePhase2(unsigned long)` | `+0x14c` |
| `IOLockSleepDeadline` | `IOService::waitForPMDriverCall(IOService *)` | `+0x144` |
| `IOLockSleepDeadline` | `IOPMrootDomain::handlePlatformHaltRestart(unsigned long)` | `+0xb64` |

so one more step names the wait exactly, by the same mechanism and with the same argument-recording
(the `deadline` word is an `AbsoluteTime`, a `uint64_t` register pair, and it is the one argument that
says whether the wait can ever end - which is where the missing timebase
(`ml_init_timebase` + an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`) stops being a separate
work item and becomes a property of this frontier).

Safety, unchanged and re-measured: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed; the device returned
to Android on its own; 25 x `persistent_write_attempted=0x00000000`, 87 x
`failure_mask=0x00000000`, no `exception:` line. The trace itself is read-only with respect to the
boot: every wrapper records and calls through, and the 23 threads that ran are the 23 the sources say
should exist by the time `bsd_autoconf` has published `IOBSD`.
