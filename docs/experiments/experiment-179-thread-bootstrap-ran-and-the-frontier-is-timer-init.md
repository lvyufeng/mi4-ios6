# Experiment 179 — `thread_bootstrap` ran its assignments, and the frontier is `timer_init`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=timer_init

No errors detected
```

`thread_bootstrap` (`osfmk/kern/thread.c:232`) ran and reached `timer_init(&thread_template.user_timer)`.
That was the prediction from reading the function before the run — it is a long sequence of
assignments to the global `thread_template` and then three `timer_init()` calls — and the run
confirms it, which is the point: the assignments executed, including the `memset` under `MONOTONIC`
and the guarded blocks, and the first thing the function asks the rest of the kernel for is
`timer_init`.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own. (The `data abort: dfar=0xdeadc000` line in the log is
the payload's own fault-injection probe from an earlier stage, preceded and followed by its own
contract lines, and it is present in every run.)

## The object that tests the unit

exp-176 raised whether "one object" is still the right unit when the object named by the device is
large; exp-177 answered yes for a 71-function object. `osfmk/kern/thread.o` is four times that, and
the measurement is stark:

```
resolved:  thread_bootstrap
added:     135
```

154 − 1 + 135 = 288 (252 functions, 36 storage). One object, 16944 bytes of text, 77 functions, 161
undefined references — and it resolved **exactly one symbol** while adding 135. The 135 are not a
frontier in any meaningful sense: they are the scheduler (`sched_tick`, `thread_setrun`, `pset0`,
`sched_multiq_dispatch`), the zone allocator (`zalloc`, `zfree`, `zinit`, `zone_change`), the stack
allocator, IPC and vouchers, ledgers, thread policy, and `machine_thread_create`.

So the honest reading of this run is: the method still *works* — the image links, the run advances,
the next edge is named — but the cost per edge went from about four new obligations to 135, and the
cause is not the rule, it is that `thread.c` is a 17 KB file. The unit is "the object the device
named", and an object can be 320 bytes or 17 KB.

That last number is not a guess. The object this run named is:

| | `kern/thread.o` (this run) | `kern/timer.o` (next) |
| --- | --- | --- |
| text | 16944 B | **320 B** |
| functions | 77 | **7** |
| undefined refs | 161 | **3** |
| new to the image | 135 | **1** |

`timer_init` is four field assignments to a `timer_t` — a leaf. The step after this one is the
smallest in twenty experiments. The step size varies by fifty-fold between adjacent objects, which
is a fact about XNU's file layout rather than about the frontier, and it means the question exp-176
asked has a better answer than yes or no: the unit is fine, and the *specific* object is what makes
a step expensive.

## The probe is gone, and why nothing replaced it

`thread_bootstrap` had a hand-written probe in `entry_stubs.c` during exp-178 — it is what printed
the three `CpuDataEntries` addresses. Linking `thread.o` ended it the way every probe ends:

```
thread.o: in function `thread_bootstrap':
thread.c:(.text+0x0): multiple definition of `thread_bootstrap';
xnu_arm_entry_stubs.o:entry_stubs.c:(.text+0x3b8): first defined here
```

That is the fourth time the link has said which hand-written definition had to go, after `DTInit`
(exp-170), `ml_parse_cpu_topology` (exp-177) and `cpu_processor_alloc` (exp-178). This time nothing
took its place, and the reason is worth recording rather than leaving as a gap: a probe exists to
print something the generated stubs cannot. The last three each printed a number XNU's own code had
computed — `ranges[1]` out of `arm-io`, four out of `/cpus`, and the physBase/virtBase identity in
the two halves of `CpuDataEntries`. At this edge there is no such number: `thread_bootstrap` writes
`thread_template` and calls out, and the generated stub names the call. When there is another value
worth printing, the block in `entry_stubs.c` says where its probe goes.

| | exp-178 | now |
| --- | --- | --- |
| XNU objects linked | 17 | 18 (`osfmk/kern/thread.o`) |
| text | 70276 B | 90980 B |
| image | 148792 B | 165248 B |
| `.bss` | 0x00224378 – 0x002267c8 | 0x00228390 – 0x0022bfc8 |
| boot_args offset | +163840 | +184320 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 153 (133 functions, 20 storage) | 288 (252 functions, 36 storage) |
| headroom | 1939512 B | 1916984 B |

`topOfKernelData` still has not moved. The image has grown from 132384 to 165248 bytes in three
experiments and is 1.9 MB below the limit exp-175 derives — which is the property that makes the
135-symbol step affordable at all.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 90980, image 165248, 288 undefined, 252 stubs

# what the object cost: 1 resolved, 135 added
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_KERN_THREAD_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort /tmp/u_no_t.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # thread_bootstrap
comm -13 <(sort /tmp/u_no_t.txt) <(sort out/stage90/xnu_arm_entry_undef.txt) | wc -l   # 135

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the prediction, from the source, made before the run
sed -n '310,320p' external/xnu-4570.1.46/osfmk/kern/thread.c

# the size of the next step
arm-none-eabi-size out/xnu_kernel_obj/osfmk_kern_timer.o
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_timer.o | grep -c ' T \| t '
sed -n '76,90p' external/xnu-4570.1.46/osfmk/kern/timer.c
```
