# Experiment 187 — Three lock groups, read out of XNU's own list, and the frontier is `timer_call_get_priority_params`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_lock_grp1_mid=0x62697461
 xnu_entry_lock_grp2_mid=0x61635f72
 xnu_entry_lock_grp3_mid=0x6f6c5f72
 stub_hit=timer_call_get_priority_params

No errors detected
```

Three values and a name, all four predicted before the run: the third frontier in a row that came
out where the source said it would, and the first that reports a structure rather than a field.

## Three groups, in order, with the names XNU was told to give them

`0x62697461` is `a`,`t`,`i`,`b`, `0x61635f72` is `r`,`_`,`c`,`a`, `0x6f6c5f72` is `r`,`_`,`l`,`o`.
Those are characters 4..7 of the three strings the kernel hands its `lck_grp_init` calls:

| | group | name | characters 4..7 |
| --- | --- | --- | --- |
| 1 | `LockCompatGroup` | `"Compatibility APIs"` | `atib` |
| 2 | `timer_call_lck_grp` | `"timer_call"` | `r_ca` |
| 3 | `timer_longterm_lck_grp` | `"timer_longterm"` | `r_lo` |

Each was reached by following `next` from the one before, so what the run measured is not three
values but a **list**: `lck_mod_init` bootstrapped the first group itself, `timer_call_init`'s
`lck_grp_init` added the second, and `timer_longterm_init`'s added the third, in that order, on the
one global queue `lck_grp_init` enqueues onto. Only real calls with those two strings produce them,
and the reading cannot be an accident of a wrong offset: every offset it uses is one the compiler
uses, at `lck_grp_link.next` = 0 (the first member of `lck_grp_t`, `locks.h:108`, and visible in
`lck_grp_init`'s inlined `enqueue_tail` as `ldrd r8, [r6]`) and `lck_grp_name` = 28 (the measured
`add r0, r4, #28` of exp-186).

The head of the list is not reachable from here - `lck_grp_queue` is `static` in `locks.c` - but
`enqueue_tail` (`queue.h:274`) links each new element at `que->prev` with `elt->next = que`, so the
list is circular and `LockCompatGroup` is its first element. Walking forward from the group this
file already knows the address of reaches the other two without ever needing the head, which is why
the probe reads a chain rather than a count.

## What the stop establishes, and what it cost

`timer_call_get_priority_params` is the first statement of `timer_call_init_abstime`, which
`timer_call_init` calls last. Stopping there means the image ran, in order:

- the whole of real `lck_mod_init`, including `lck_mtx_init_ext` — which is the mutex
  implementation, not the code that initializes one, and is the first real lock this image has;
- `timer_call_init`'s `lck_attr_setdefault`, `lck_grp_attr_setdefault` and `lck_grp_init`;
- the whole of `timer_longterm_init`, which is in `timer_call.c` (not `timer.c`), does its own
  attribute and group initialization, and calls `timer_call_setup`.

That is the entire lock and timer-call bring-up of `kernel_early_bootstrap`, minus one call.

The step that made it reachable is `osfmk/arm/locks_arm.o` — 9868 bytes of text, 64 functions,
37 references:

```
resolved (18): arm_usimple_lock_init  _enable_preemption  lck_mtx_destroy  lck_mtx_ilk_unlock
               lck_mtx_init  lck_mtx_init_ext  lck_mtx_lock  lck_mtx_lock_spin
               lck_mtx_lock_spin_always  lck_mtx_unlock  lck_rw_done  lck_rw_lock
               lck_rw_lock_exclusive  lck_rw_lock_shared  LcksOpts  lck_spin_lock
               lck_spin_try_lock  lck_spin_unlock
added (2):     ast_taken_kernel  not_in_kdp
353 -> 337 undefined
```

Eighteen resolutions for two new obligations, and the sixteen extra ones are the whole mutex, rw and
spin lock API. `LcksOpts` is among them: it stops being a `B 0x4` storage stub and becomes the real
`unsigned int` that `osfmk/arm/locks_arm.c:115` defines. `ast_taken_kernel` and `not_in_kdp` are
the slow-path calls a mutex only makes when it has to wait — nothing has contended yet, and the run
did not reach them.

| | exp-186 | now |
| --- | --- | --- |
| XNU objects linked | 26 | 27 (`osfmk/arm/locks_arm.o`) |
| text | 118412 B | 127692 B |
| image | 198368 B | 198440 B |
| `.bss` | 0x002304a8 – 0x002359c8 | 0x002304a8 – 0x00235a08 |
| boot_args offset | +225280 | +225280 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 353 (307 functions, 46 storage) | 337 (291 functions, 46 storage) |
| headroom | 1877560 B | 1877496 B |

## Placing a probe, three runs on

| run | probe placed at | how it was chosen | fired? |
| --- | --- | --- | --- |
| exp-185 | `timer_call_get_priority_params` | following the call chain out of `lck_mod_init` | no |
| exp-186 | `lck_mtx_init_ext` | the next symbol in the *body* of `lck_mod_init` | yes |
| exp-187 | `timer_call_get_priority_params` | the next symbol in the body of `timer_call_init_abstime`, after checking every call between is already real | yes |

The rule the three make: read the function the last frontier lives in, statement by statement, and
prefer the next symbol in *that* function over the next symbol the call graph suggests - then check
that everything between the last stop and the probe is defined in an object already linked. The
call graph said `timer_call_get_priority_params` in exp-185 and was right about the symbol and wrong
about the run, because the run never leaves `lck_mod_init`.

## What is next

`timer_call_get_priority_params` is defined in **`osfmk/arm/arm_timer.c:276`**, and it is one line:

```c
timer_coalescing_priority_params_ns_t * timer_call_get_priority_params(void)
{
	return &tcoal_prio_params_init;
}
```

The object is small — `osfmk_arm_arm_timer.o`, 1044 bytes of text across 11 functions and 168 bytes
of data, 11 undefined references — so the step should be cheap, and what it brings is a table:

```c
	.timer_coalesce_bg_ns_max = 100 * NSEC_PER_MSEC,
	.timer_coalesce_kt_ns_max = 1 * NSEC_PER_MSEC,
	...
	.latency_qos_ns_max = {1 * NSEC_PER_MSEC, 5 * NSEC_PER_MSEC, 20 * NSEC_PER_MSEC,
			       75 * NSEC_PER_MSEC, 10000 * NSEC_PER_MSEC, 10000 * NSEC_PER_MSEC},
```

`timer_call_init_abstime` then converts every one of those thresholds from nanoseconds to
`rtclock_sec_divisor` ticks with the real `nanoseconds_to_absolutetime`, in a loop over
`NUM_LATENCY_QOS_TIERS` — the first thing in this image that does XNU's own arithmetic over a table
of XNU's own constants, and the first place a value can be checked against the table's source rather
than against a single constant. `arm_timer.c` is also the ARM generic-timer driver, so it is where
the frontier stops being "the kernel initializing itself" and starts being "a driver", which is
what the goal for this stage is about.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 127692, image 198440, 337 undefined, 291 stubs

# what the object cost: 18 resolved, 2 added
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_LOCKS_ARM_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_with_locks_arm.txt) | wc -l   # 18
comm -13 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_with_locks_arm.txt)          # 2

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'lock_grp\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -5

# the three strings the words are 4..7 of
sed -n '248,258p' external/xnu-4570.1.46/osfmk/kern/timer_call.c
grep -n 'lck_grp_init' external/xnu-4570.1.46/osfmk/kern/timer_call.c

# the offsets the walk uses, from the compiler rather than the header
sed -n '108,116p' external/xnu-4570.1.46/osfmk/kern/locks.h
sed -n '274,287p' external/xnu-4570.1.46/osfmk/kern/queue.h

# the size of the next step
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_arm_timer.o
sed -n '276,280p' external/xnu-4570.1.46/osfmk/arm/arm_timer.c
```
