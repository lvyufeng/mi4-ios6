# Experiment 262 — The Scheduler Is Two Objects, and `sched_init` Completes

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change: two objects, and the reason is measured rather than convenient

261's stop was `sched_init` (`osfmk/kern/sched_prim.c:357`,
`out/xnu_kernel_obj/osfmk_kern_sched_prim.o`: 31296 bytes of text, 107 references, 83 satisfied,
**24 new**). But `sched_init` does not call the scheduler directly. It loads the address of a **table**
and reads its slots as function pointers:

```
 78: movw r6, #:lower16:sched_multiq_dispatch   ; a *data* symbol
 84: ldr  r4, [r6]                              ; slot 0  = the scheduler's name string
12c: ldr  r0, [r6, #4]                          ; slot 1  = SCHED(init)
130: ldr  r7, [r6, #12]                         ; slot 3  = SCHED(processor_init)
134: ldr  r5, [r6, #16]                         ; slot 4  = SCHED(pset_init)
138: ldr  r6, [r6, #140]                        ; slot 35 = SCHED(rt_init)
13c: blx r0     14c: blx r6     184: blx r5     19f: bx r1
```

`sched_multiq_dispatch` is a **storage stand-in** in this image — sixteen zero bytes at `0x800f9d40`.
Linking `sched_prim.o` alone would therefore have made slot 0 a NULL string,
`strlcpy(sched_string, NULL, 48)` a NULL dereference and `blx r0` a jump to 0: a stop that measures
*this step's omission* rather than the frontier. So the table is linked with it —
`osfmk/kern/sched_multiq.c` (manifest:580), 7418 bytes of text, 52 references, 25 satisfied, 27 new
(`run_queue_*`, `sched_timeshare_*`, `sched_compute_timeshare_priority`, `update_priority`,
`choose_processor`, …), whose `sched_multiq_dispatch` is **statically initialised in `.rodata`**
(172 bytes of slots, `R sched_multiq_dispatch`). Where a slot names a symbol the image does not have,
the `blx` now lands on that symbol's *stub* and stops the run cleanly.

`SCHED()` resolves to the multiq scheduler here — decided by `SCHED(sched_name)`, the table's first
slot — which is why the second object is `sched_multiq.o` and not `sched_traditional.o` or
`sched_dualq.o`: all three are in the manifest (`out/xnu_arm_manifest.txt:579-582`) and the config
selects exactly one.

**`OSFMK_KERN_SCHED_PRIM_OBJ` and `OSFMK_KERN_SCHED_MULTIQ_OBJ` are the change.**

## The prediction, read out of the linked table

Resolving the table in the linked image (`sched_multiq_dispatch` → `0x800b82e8`) and reading its slots
says which functions `sched_init` will call; each was then followed one level:

| slot | offset | target | what it is |
| --- | --- | --- | --- |
| 1 | 0x04 | `sched_multiq_init` | real; calls PE_parse_boot_argn, the printf helper, `zinit`, `zone_change`, `lck_*` — all real — and **tail-calls `sched_timeshare_init`** |
| 35 | 0x8c | `sched_rtglobal_init` | real; calls `sched_rtglobal_runq` through the table and `arm_usimple_lock_init`, tails into `memset` |
| 4 | 0x10 | `sched_multiq_pset_init` | real, two instructions, a tail call to `run_queue_init` |
| 3 | 0x0c | `sched_multiq_processor_init` | likewise, tail into `run_queue_init` |

and `sched_timeshare_init`, `sched_rtglobal_init`, `sched_rtglobal_runq` and `run_queue_init` were each
checked for the stub body — a `movw r0, #<name>` followed by `push {lr}` — and none of them has it.

So `sched_init` completes, and the stop should be **past** it, at the next stub in
`kernel_bootstrap`'s line:

```
8000dc54  bl ltable_bootstrap    ; STUB at 0x800a71a0
```

**The prediction: `stub_hit=ltable_bootstrap`, `xnu_entry_stub_caller=0x8000dc58`** — `caller - 4` =
`0x8000dc54` = `kernel_bootstrap+0x214`.

### A name that no delta can see

`sched_timeshare_init` and `sched_rtglobal_init` appear in **neither** the resolved nor the added list,
and that is correct rather than a mistake. Nothing in the image referenced either name before this
step, so neither was in the previous `undef` file to be subtracted from. **The resolved/added split is a
delta of two undefined sets, not a statement about what changed in the image** — a name that was absent
from the frontier entirely is invisible to it. Both are real code in the new image, defined by the
objects this step added.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003d
 xnu_entry_kv_in_dram=0x0000003d
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ltable_bootstrap
 xnu_entry_stub_caller=0x8000dc58

No errors detected
```

`0x8000dc58` resolves to `kernel_bootstrap+0x218`, whose `caller - 4` is
`8000dc54: bl 800a71a0 <ltable_bootstrap>` — the prediction, for the **fifth step in a row**, and this
time it was a prediction about a *chain*: four functions reached indirectly through a data table, each
of their own callees checked, all of it completing. `failure_mask=0x00000000` in all 87 contracts that
report one, `persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on
its own.

`kv_written == kv_in_dram == 0x3d`, six above 261's `0x37`: `ltable_bootstrap` is six characters longer
than `sched_init`.

## Cost

| | exp-261 | now |
| --- | --- | --- |
| undefined | 633 | **631** (30 resolved, 28 added) |
| function stubs | 554 | **555** |
| storage stubs | 79 | **76** |
| entry text | 730436 B | **768836 B** (+38400) |
| entry image | 835080 B | **867888 B** (+32808) |
| entry `.bss` end | 0x800facc8 | **0x80103108** |
| derived `args` offset | +1032192 | **+1069056** |
| `topOfKernelData` | +2097152 | **+3145728** |
| headroom | 1069880 B | **2084600 B** |
| payload text | 1327298 B | **1360106 B** (+32808) |

The 30 resolved are the thread and scheduling API the two objects supply — `thread_block`,
`thread_setrun`, `sched_tick`, `assert_wait`, `idle_thread`, `sched_startup` and the rest — which had
been stand-ins since the image first referenced them. The image grew 32808 bytes and `.bss` followed it;
`topOfKernelData` moved a whole megabyte because it is derived at that granularity, and the headroom
grew with it. Everything derived is regenerated into the payload's header, so nothing is hard-coded.

## What is next

`ltable_bootstrap` — `osfmk/kern/ltable.c`, and the last of the three “bootstrap” calls in this line
before the run-in ends. Behind it:

```
8000dc64  bl waitq_bootstrap    <- stub
8000dc74  bl ipc_bootstrap      <- stub
```

`waitq_bootstrap` is the interesting one, because 262's link just added seven `waitq_*` boundaries
(`waitq_lock`, `waitq_unlock`, `waitq_assert_wait64_locked`, `waitq_pull_thread_locked`,
`waitq_wakeup64_all`, `waitq_wakeup64_identify`, `waitq_wakeup64_thread`) — the scheduler's own
wait-queue dependency, which `waitq_bootstrap` is very likely the object that answers. If so, the next
step resolves names this one created, exactly as 261 did for 259.

## Reproduce

```bash
# the table is a data symbol, and the second object is the one that defines it
arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | grep sched_multiq_dispatch
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_sched_multiq.o | grep dispatch
arm-none-eabi-objdump -t out/xnu_kernel_obj/osfmk_kern_sched_multiq.o | grep dispatch   # O .rodata, 0xac

# sched_init's indirect calls, and the slots they read
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_sched_prim.o \
  | awk '/<sched_init>:/{f=1} f{print} f&&/^$/{exit}' | grep -E 'blx|sched_multiq_dispatch'

# the table as linked, and what each slot holds
T=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk '$3=="sched_multiq_dispatch"{print $1}')
arm-none-eabi-objdump -s -j .text --start-address=0x$T --stop-address=$((0x$T + 0xb0)) out/stage90/xnu_arm_entry.elf
./tools/host_resolve_entry_addr.sh 0x800a3fe8    # slot 1  -> sched_multiq_init
./tools/host_resolve_entry_addr.sh 0x8009d080    # slot 35 -> sched_rtglobal_init

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8000dc58     # -> kernel_bootstrap+0x218
```
