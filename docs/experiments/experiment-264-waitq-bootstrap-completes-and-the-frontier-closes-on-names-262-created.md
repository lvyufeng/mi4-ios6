# Experiment 264 — `waitq_bootstrap` Completes, and the Frontier Closes on Names 262 Created

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

263's stop was `waitq_bootstrap`, and the prediction of *which object answers it* was already written
into 262's cost table. The scheduler link added seven `waitq_*` boundaries —

```
waitq_lock   waitq_unlock   waitq_assert_wait64_locked   waitq_pull_thread_locked
waitq_wakeup64_all   waitq_wakeup64_identify   waitq_wakeup64_thread
```

— and the object named `waitq_bootstrap` is `osfmk/kern/waitq.c` (manifest:603),
`out/xnu_kernel_obj/osfmk_kern_waitq.o`: 21084 bytes of text, 8 of data, 172 of bss, 62 definitions,
**49 references — 48 of them already satisfied by this image**. **`OSFMK_KERN_WAITQ_OBJ` is the
change.**

So this is the second time the frontier has resolved names a *previous* step created — 261 did it for
259's `stack_snapshot_from_kernel` — and the first time it resolves a whole block of them at once.
Twelve stand-ins become real code:

```
resolved  waitq_lock   waitq_unlock   waitq_assert_wait64_locked   waitq_pull_thread_locked
          waitq_wakeup64_all   waitq_wakeup64_identify   waitq_wakeup64_thread     <- the seven, from 262
          waitq_bootstrap   waitq_init   waitq_assert_wait64   waitq_wakeup64_one   _global_eventq
added     waitq_set__CALLING_PREPOST_HOOK__
```

The one added name is called from inside `waitq_wakeup64_identify`'s prepost-hook path
(`waitq.c:4338`), which the bootstrap does not execute — so the cost of this step is one boundary,
taken off the first-call path.

## The prediction

`waitq_bootstrap` is 774 bytes and is 13 calls with only eight distinct targets, all of them read out
of the object's relocations:

```
PE_parse_boot_argn       x4   0x80006834  real
kernel_memory_allocate   x1   8007e670   real - the allocation of global_waitqs
panic                    x1   8002d774   real
hw_lock_init             x1   80011318   real
zinit                    x1   8006d6c0   real - the waitq_set zone
zone_change              x1   80070260   real
_consume_printf_args     x2   8002a954   real
ltable_init              x2   800a5be0   real - **made real by 263**
```

**All eight real**, so it completes, and what it does on the way is the fourth real kernel allocation
this frontier has made: `kernel_memory_allocate(kernel_map, &global_waitqs, size, 0, 6, 17)` sized from
`g_num_waitqs`, which it derives from `thread_max` unless `waitq_nwaitqs` overrides it — the same
boot-arg shape as `telemetry_init`'s buffer and `stackshot_init`'s lock group. It then walks the table
initialising each waitq's `hw_lock` and `waitq`, and finishes with two latch tables
(`zinit` + `zone_change` + `ltable_init` for `g_wqlinktable` and `g_prepost_table`).

It returns to `kernel_bootstrap+0x230`, where the straight line is `kernel_debug_string_early` (real)
and then

```
8000dc74  bl ipc_bootstrap    ; STUB at 0x800ac9c0
```

**The prediction: `stub_hit=ipc_bootstrap`, `xnu_entry_stub_caller=0x8000dc78`** — `caller - 4` =
`0x8000dc74` = `kernel_bootstrap+0x234`.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003a
 xnu_entry_kv_in_dram=0x0000003a
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ipc_bootstrap
 xnu_entry_stub_caller=0x8000dc78

No errors detected
```

`0x8000dc78` resolves to `kernel_bootstrap+0x238`, whose `caller - 4` is
`8000dc74: bl 800ac9c0 <ipc_bootstrap>` — the prediction, address for address, for the **seventh step
in a row**. `failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

`kv_written == kv_in_dram == 0x3a`, two below 263's `0x3c`: the KV buffer records the stub's name
verbatim, and `ipc_bootstrap` is two characters shorter than `waitq_bootstrap`.

The important part is what did *not* happen. `waitq_bootstrap` is the first target in this line that
could plausibly have failed rather than stopped: it takes a real allocation, draws it from another
subsystem's global (`thread_max`), and then loops over the table touching every slot it allocated. It
returned. The wait-queue subsystem is now real code in the image — 21084 bytes of it, including the
seven functions 262's scheduler was already calling — and the run continued past it into
`kernel_bootstrap`'s next line.

## Cost

| | exp-263 | now |
| --- | --- | --- |
| undefined | 630 | **619** (12 resolved, 1 added) |
| function stubs | 554 | **543** |
| storage stubs | 76 | **76** |
| entry text | 773988 B | **794436 B** (+20448) |
| entry image | 884272 B | **900664 B** (+16392) |
| entry `.bss` end | 0x80107208 | **0x8010b2c8** |
| derived `args` offset | +1085440 | **+1101824** |
| `topOfKernelData` | +3145728 | **+3145728**, unchanged |
| headroom | 2067960 B | **2051384 B** |
| payload text | 1376490 B | **1392882 B** (+16392) |

+16392 is one 16 KB alignment block plus 8 — the shape this project has seen at 255, 259, 261 and 263
— and the payload moved by exactly the same 16392 because its embedded `.bin` grew. `topOfKernelData`
did not move this time because it is derived at megabyte granularity and the growth stayed inside the
megabyte it was already in.

## What is next

`ipc_bootstrap` — `osfmk/ipc/ipc_init.c` or `osfmk/kern/ipc_*.c`, and the last of the three
"bootstrap" calls. Behind it `kernel_bootstrap`'s straight line continues, measured from the linked
image and every target still a stub:

```
8000dc74  bl ipc_bootstrap      <- stub   0x800ac9c0
8000dc84  bl mac_policy_init    <- stub
```

`ipc_bootstrap` is the interesting one in a different way from the scheduler: the IPC subsystem is the
one whose initialisation is most likely to reach *back* into the thread and wait-queue code that this
step just made real, so the resolved/added split may invert — a step that adds little because the two
steps before it supplied what it needs.

## Reproduce

```bash
# the seven names 262 added are the map to this step's object
grep -n 'osfmk/kern/waitq.c' out/xnu_arm_manifest.txt
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_waitq.o | grep -E ' T waitq_| B | D '
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_waitq.o | wc -l        # 49

# 48 of the 49 were already satisfied, so the cost is one name
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_waitq.o | awk '{print $2}' | sort -u > /tmp/wq.txt
arm-none-eabi-nm --defined-only out/stage90/xnu_arm_entry.elf | awk '{print $3}' | sort -u > /tmp/img.txt
comm -23 /tmp/wq.txt /tmp/img.txt                                       # the 1 added

# waitq_bootstrap's own calls: 13 of them, eight distinct names, all real
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_waitq.o \
  | awk '/<waitq_bootstrap>:/{f=1} f{print} f&&/^$/{exit}' | grep -o 'R_ARM_CALL\s*\S*' | sort | uniq -c

# the link, and the prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
comm -23 <(sort /tmp/undef_263.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the resolved 12
comm -13 <(sort /tmp/undef_263.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the 1 added
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/^8000dc74:/{print; exit}'

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8000dc78     # -> kernel_bootstrap+0x238
```
