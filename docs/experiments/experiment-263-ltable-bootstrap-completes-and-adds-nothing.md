# Experiment 263 — `ltable_bootstrap` Completes, and the Link Adds Nothing

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

262's stop was `ltable_bootstrap`, `osfmk/kern/ltable.c` (manifest:567),
`out/xnu_kernel_obj/osfmk_kern_ltable.o`. **`OSFMK_KERN_LTABLE_OBJ` is the change**, and it is the
cheap shape again — the same one 260's `console_init` had, and the clearest form of it so far:

```
5175 bytes of text, 272 of bss
19 references — and all 19 already satisfied by this image
resolved: ltable_bootstrap (itself)
added:    nothing at all
```

`ltable_bootstrap` calls only `lck_grp_init` and `PE_parse_boot_argn`, both real, so it completes.

## The prediction

**`stub_hit=waitq_bootstrap`, `xnu_entry_stub_caller=0x8000dc68`** — `caller - 4` = `0x8000dc64` =
`kernel_bootstrap+0x224`, the next stub in the same straight line.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003c
 xnu_entry_kv_in_dram=0x0000003c
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=waitq_bootstrap
 xnu_entry_stub_caller=0x8000dc68

No errors detected
```

`0x8000dc68` resolves to `kernel_bootstrap+0x228`, whose `caller - 4` is
`8000dc64: bl 800a9f90 <waitq_bootstrap>` — the prediction, for the **sixth step in a row**.
`failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

`kv_written == kv_in_dram == 0x3c`, one below 262's `0x3d`: `waitq_bootstrap` is one character shorter
than `ltable_bootstrap` — both end in the same nine characters, so the difference is `ltable` (6)
against `waitq` (5).

## Cost

| | exp-262 | now |
| --- | --- | --- |
| undefined | 631 | **630** (1 resolved, **0 added**) |
| function stubs | 555 | **554** |
| storage stubs | 76 | **76** |
| entry text | 768836 B | **773988 B** (+5152) |
| entry image | 867888 B | **884272 B** (+16384) |
| entry `.bss` end | 0x80103108 | **0x80107208** |
| derived `args` offset | +1069056 | **+1085440** |
| headroom | 2084600 B | **2067960 B** |
| payload text | 1360106 B | **1376490 B** (+16384) |

The image moved one alignment block; everything derived moved with it as the layout block intends.

## What is next

`waitq_bootstrap` — and it is the interesting one, because **262 created its dependencies**:

```
waitq_lock   waitq_unlock   waitq_assert_wait64_locked   waitq_pull_thread_locked
waitq_wakeup64_all   waitq_wakeup64_identify   waitq_wakeup64_thread
```

those seven were added to the image's undefined set by the scheduler link, and `waitq_bootstrap` is in
`osfmk/kern/waitq.c`, which is very likely the object that answers them. If so, the next step resolves
names the previous step created — exactly as 261 did for 259, and the second time this pattern has
appeared. After it, the last of the three bootstrap calls in this line is `ipc_bootstrap`, and then
`kernel_bootstrap` is past its initialisation block: the run-in to `bsd_init`, where "XNU loads, enters
the OS and runs its basic drivers" stops being a forecast.

## Reproduce

```bash
grep -n 'osfmk/kern/ltable.c' out/xnu_arm_manifest.txt
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_ltable.o | wc -l          # 19
arm-none-eabi-size out/xnu_kernel_obj/osfmk_kern_ltable.o

(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
comm -23 <(sort /tmp/undef_262.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # ltable_bootstrap
comm -13 <(sort /tmp/undef_262.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # empty

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8000dc68     # -> kernel_bootstrap+0x228
```
