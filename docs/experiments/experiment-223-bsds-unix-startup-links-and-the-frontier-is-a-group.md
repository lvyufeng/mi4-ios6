# Experiment 223 — BSD's `unix_startup` Links, the Frontier's First Group, and the Next Stop Is `kernel_debug_string_early`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000019
 xnu_entry_kv_in_dram=0x00000019
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=bsd_exec_setup

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x19 = 25 = strlen("bsd_exec_setup") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=bsd_exec_setup` and it held - the ninth consecutive prediction made from
the disassembly rather than from a relocation list.

## The first step whose frontier is a group

This is the first step in this sequence where linking the object defines **more than one** symbol the
image was missing, and where it takes on **more than one** new obligation:

```
resolved (2):  bsd_scale_setup      (4 bytes of text: one tail call)
               serverperfmode       (4 bytes of .bss - a storage stub until now)

added   (12):  bsd_exec_setup  buf_headers  bufinit  desiredvnodes  kmem_suballoc  mb_map
               mbuf_default_ncl  mbutl  nmbclusters  sysctl_handle_int
               tcp_recvspace  tcp_sendspace

361 -> 371 undefined          <-- the first increase since experiment 214
```

The object defines five symbols in all - `bsd_startupearly`, `bsd_mbuf_cluster_reserve`,
`bsd_bufferinit`, `bsd_scale_setup` and `serverperfmode` - and only the last two were missing from
the image; the other three arrive unused. It references 21, of which five were already real and four
were already stubs, so twelve are genuinely new. The undefined count going **up** is not a
regression: it is what a step into a new subsystem looks like, and the count is a measure of what is
left to provide, not of progress.

What executed is correspondingly small: `bsd_scale_setup`'s one instruction, a tail call, and then
the stub. Nothing of `bsd_startupearly` or `bsd_mbuf_cluster_reserve` ran, and nothing measured
anything about BSD.

## Cost, and two numbers that moved for the first time since experiment 214

`bsd_dev_unix_startup.o` (`bsd/dev/unix_startup.c`) - **1291 bytes of text, 96 of data, 48 of
`.bss`**:

| | exp-222 | now |
| --- | --- | --- |
| entry objects linked | 62 | 63 (`bsd_dev_unix_startup.o`) |
| entry text | 289072 B | 290456 B |
| entry image | 387904 B | **388000 B** |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | 0x0025e6f8–0x00277608 (**102160 B**) |
| undefined | 361 | **371** |
| stubs | 300 functions, 61 storage | 304 functions, 67 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | **1608184 B** |
| payload text | 880090 B | **880186 B** |

The entry image's **file size changed for the first time since experiment 214** - it has been 387904
bytes for nine steps, because every step until now grew inside a window the image had already
reserved, and this one grew past it. And with it the payload's text moved for the first time in the
same stretch: `880090 -> 880186`, exactly the image's +96. Those two facts are one fact, and they are
worth having written down because the last nine tables all said "unchanged" and the next ones may
not.

`.text` went `289056 -> 290432` (+1376) against an object of 1291 bytes of text minus a 12-byte stub
plus twelve new 12-byte stubs and their name strings - the three-term decomposition of experiment 218,
doing its job on a step with four moving parts instead of one.

## What is next: `bsd_exec_setup`, and the prediction is `kernel_debug_string_early`

The frontier is `bsd_exec_setup`, defined by `out/xnu_kernel_obj/bsd_kern_bsd_init.o`
(`bsd/kern/bsd_init.c`) - **3653 bytes of text, 80 of data, 2720 of `.bss`**, the largest single
object this link will take on so far.

**The prediction is `stub_hit=kernel_debug_string_early`**, and it follows from `bsd_exec_setup`
being a **leaf** - it calls nothing at all:

```
00000c58 <bsd_exec_setup>:
 c58: cmp  r0, #7
 c5c: bhi  c7c                 ; r0 > 7 -> the default arm (0x201, 0x844000)
 c60: ... .Lswitch.table.bsd_exec_setup[18] and [0]  ; two jump tables indexed by r0
 c7c: movw r1, #0x2000 / movw r0, #0x201 / movt r1, #0x844
 c88: str  r1, [bsd_pageable_map_size]
 c94: str  r0, [bsd_simul_execs]
 ca0: bx   lr
```

`kernel_bootstrap` calls it with `r0 = 0` (`20d600: mov r0, #0`), so the `bhi` is not taken and the
switch table path runs; either way every arm writes two of this object's own `.bss` variables and
returns. So control comes back to `kernel_bootstrap` at 0x20d608, where there is **no call and no
branch** between there and the next one:

```
20d608-20d678: movw/movt/ldr/str/add/asr    ; the cluster and scale arithmetic, straight line
20d67c: bl    kernel_debug_string_early     <-- STUB at 0x0023a2a4, the stop
```

`kernel_debug_string_early` is a 12-byte stub, and linking `bsd_kern_bsd_init.o` does not change that:
the object does not mention the symbol at all - checked with `nm -u`, which is empty for it - so
whatever defines it, this step does not.

**What makes this step large, and worth its own paragraph in the table.** `bsd_kern_bsd_init.o`
references **132 symbols, of which 28 are already in the image and 104 are not** - so linking it adds
on the order of a hundred stubs at once. Nothing in it executes except `bsd_exec_setup`, because
`bsd_init`, `bsd_early_init`, `bsd_autoconf`, `bsdinit_task` and `bsd_utaskbootstrap` are only
*linked*, not reached; but the image, the `.bss` and the stub set all move together, which is the
first time in this project that a single step carries that much. The headroom below
`topOfKernelData` is 1608184 bytes, so there is room - but the next tables will need to be read
rather than assumed, and if the image ever approaches its window that will show up here first.

## Reproduce

```bash
# the step: 2 resolved, 12 added, 361 -> 371
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_BSD_DEV_UNIX_STARTUP_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 2 resolved: bsd_scale_setup, serverperfmode
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 12 added
wc -l /tmp/A.txt /tmp/B.txt                      # 361 and 371

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=bsd_exec_setup

# why the next stop is kernel_debug_string_early: bsd_exec_setup is a leaf
arm-none-eabi-objdump -dr out/xnu_kernel_obj/bsd_kern_bsd_init.o | sed -n '/<bsd_exec_setup>:/,/^$/p'
arm-none-eabi-objdump -d --start-address=0x0020d608 --stop-address=0x0020d684 out/stage90/xnu_arm_entry.elf | tail -4
arm-none-eabi-size out/xnu_kernel_obj/bsd_kern_bsd_init.o
arm-none-eabi-nm -u out/xnu_kernel_obj/bsd_kern_bsd_init.o | wc -l     # 132
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$2=="T"||$2=="B"||$2=="D"||$2=="R"{print $3}' \
  | sort -u > /tmp/in_image.txt
arm-none-eabi-nm -u out/xnu_kernel_obj/bsd_kern_bsd_init.o | awk '{print $2}' | sort -u > /tmp/refs.txt
comm -23 /tmp/refs.txt /tmp/in_image.txt | wc -l                      # 104 new
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
