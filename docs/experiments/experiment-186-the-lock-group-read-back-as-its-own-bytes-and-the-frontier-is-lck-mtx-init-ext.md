# Experiment 186 — The lock group read back as its own bytes, and the frontier is `lck_mtx_init_ext`

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
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_lock_compat_refcnt=0x00000001
 xnu_entry_lock_compat_name_head=0x706d6f43
 xnu_entry_tree_timebase_lo=0x0124f800
 xnu_entry_tree_timebase_hi=0x00000000
 stub_hit=lck_mtx_init_ext

No errors detected
```

Four numbers, three of them predictions that came out exactly, and the fourth the name of the next
edge — also predicted, from the source.

## `0x706d6f43` is `Comp`, and it is the new object's work

`lck_mod_init` (`osfmk/kern/locks.c:140`) ends by writing through two pointers:

```c
        bzero(&LockCompatGroup, sizeof(lck_grp_t));
        (void) strncpy(LockCompatGroup.lck_grp_name, "Compatibility APIs", LCK_GRP_MAX_NAME);
        ...
        LockCompatGroup.lck_grp_refcnt = 1;
```

`LockCompatGroup` is a `lck_grp_t`, and this image includes no XNU headers to type it with, so the
two fields are read at offsets. **Those offsets are measured, not assumed**: `lck_grp_init` in the
same object is compiled code that writes both fields, and it does it as `str r5, [r4, #8]` with
r5 = 1, and `add r0, r4, #28` before its `strlcpy`. So 8 and 28 are what the compiler used.

- `refcnt = 1` is the value `locks.c:172` writes.
- `name_head = 0x706d6f43` is `C`,`o`,`m`,`p` little-endian: the first four bytes of
  `"Compatibility APIs"`, in the field `strncpy` was handed.

The second of those is a measurement of **the object this experiment linked**. `strncpy` was a
stub until now; a stub returns without copying, and the field would still hold the `bzero`'d zero.
It holds the right characters instead, which is only reachable if the real `strncpy`
(`osfmk/arm/strncpy.c`) took the three pointers `lck_mod_init` passed it and copied through them.
It is the first time in this sequence that a newly linked object's *work* is confirmed by a value
rather than by the run continuing past it, and the reason it is possible is that the value is
checkable: a wrong offset or a wrong source string cannot produce `Comp`.

## The other two numbers

`xnu_entry_tree_timebase_lo = 0x0124f800` is 19200000 — the frequency this project's device tree
advertises (`stage90_main.c:692`), returned by `nanoseconds_to_absolutetime(1000000000)`.
`rtclock.c:443`:

```c
*result = (t64 = nanosecs / NSEC_PER_SEC) * rtclock_sec_divisor;
nanosecs -= (t64 * NSEC_PER_SEC);
*result += (nanosecs * rtclock_sec_divisor) / NSEC_PER_SEC;
```

exp-184 measured the forward direction and got exactly 1000000000 nanoseconds from 19200000 ticks;
this is the same constant approached from the other side, and it lands on the tree's number again.
It is also the first run of `nanoseconds_to_absolutetime`, which no code had reached.

`stub_hit=lck_mtx_init_ext` is `locks_arm.c:2292`, the **last** statement of `lck_mod_init`. So the
whole of real `lck_mod_init` ran except its final call: the boot-arg parse, `queue_init`, the
`bzero`, the `strncpy` - which is the experiment's own new code - `enqueue_tail`, the field writes,
and `lck_grp_attr_setdefault` and `lck_attr_setdefault`.

## Placing a probe: the call graph is not the function body

exp-185's probe was written for `timer_call_get_priority_params` because that is the first
statement of `timer_call_init_abstime`, two lines after `lck_mod_init` in the call chain, and it did
not fire. This experiment's probe is three statements earlier in the *same function* as the edge,
and it did. Both probes were "the next thing after the last frontier by reading the source", and
only one was right, because the question is not what calls what — it is what the function the
frontier lives in does, statement by statement, from the top. `lck_mod_init` stops at its fourth
statement, before it ever reaches `timer_call_init`, and no amount of call-graph reading says so.

The check that makes this predictable is cheap and worth stating as the rule it has become: put the
probe at the next symbol in the *body* of the function the last run named, and confirm that
everything between the last stop and the probe is defined in an object already linked. Here that
was `lck_grp_attr_setdefault` and `lck_attr_setdefault`, both in `locks.o`.

## The smallest step so far

`osfmk_arm_strncpy.o` (104 B of text) plus `osfmk_arm_strnlen.o` (184 B, assembly, a leaf). Both
were this step for the same reason the image has carried `strlcpy.o` next to `strlen.o` since
exp-173: `strncpy.c`'s only undefined reference beyond `memcpy`/`memset` (already in the image) is
`strnlen`, and linking the edge without its leaf would spend a hardware run reporting a name the
host can print.

```
resolved:  strncpy  strnlen
added:     (nothing)
355 -> 353 undefined, text 118156 -> 118412, image 198368 -> 198368
```

**Zero new obligations** — the first object in this sequence that brings none. 256 bytes of text
went into the alignment padding at the end of `.text`, so the binary is byte-for-byte the same
size as exp-185's; exp-175's derivation makes that a non-event, and the four invariants it checks
were re-checked by the build.

| | exp-185 | now |
| --- | --- | --- |
| XNU objects linked | 24 | 26 (`osfmk/arm/strncpy.o`, `osfmk/arm/strnlen.o`) |
| text | 118156 B | 118412 B |
| image | 198368 B | 198368 B |
| `.bss` | 0x002304a8 – 0x002359c8 | 0x002304a8 – 0x002359c8 |
| boot_args offset | +225280 | +225280 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 355 (309 functions, 46 storage) | 353 (307 functions, 46 storage) |
| headroom | 1877560 B | 1877560 B |

## What is next

`lck_mtx_init_ext`, in `osfmk/arm/locks_arm.c` — the same file that defines six of the `hw_atomic_*`
functions and `locks_arm.o`'s `LockDefaultLckAttr`. It is **9868 bytes of text across 64 functions
with 37 undefined references**, so it is a step of the size `thread.o` was in exp-179 rather than
the size of the last three, and it should be measured before it is taken. It is also where the
mutex itself is: `lck_mtx_lock`, `lck_mtx_unlock`, `lck_mtx_ilk_unlock`, `mutex_pause`, `MutexSpin`
and the `hw_lock_*` primitives are all in it, which is the first time this image would contain a
working lock rather than the code that initializes one.

After that, `lck_mod_init` is finished and `kernel_early_bootstrap` moves on to `timer_call_init`,
which is already real: its three lock calls are real now, and behind them are `timer_longterm_init`
(also in `timer_call.o`) and `timer_call_init_abstime`, whose first statement is the
`timer_call_get_priority_params` that exp-185's probe was written for.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 118412, image 198368, 353 undefined, 307 stubs

# what the object cost: 2 resolved, nothing added
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_STRNCPY_OBJ=/tmp/empty.o STAGE90_ENTRY_STRNLEN_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_with_strncpy.txt)   # resolved

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'lock_compat\|tree_timebase\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -6

# where the offsets 8 and 28 come from - the compiled code, not the header
arm-none-eabi-objdump -d out/xnu_kernel_obj/osfmk_kern_locks.o | \
   awk '/<lck_grp_init>:/{f=1} f{print} f&&/^$/{exit}' | grep -E 'add\s+r0, r4|str\s+r5, \[r4'
sed -n '108,118p' external/xnu-4570.1.46/osfmk/kern/locks.h

# the statements the stop is between
sed -n '140,176p' external/xnu-4570.1.46/osfmk/kern/locks.c

# the size of the next step
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_locks_arm.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_locks_arm.o | wc -l
sed -n '2289,2300p' external/xnu-4570.1.46/osfmk/arm/locks_arm.c
```
