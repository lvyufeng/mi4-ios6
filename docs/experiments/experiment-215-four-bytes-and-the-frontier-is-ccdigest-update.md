# Experiment 215 — the Smallest Object There Is, `cchmac_update` Resolves and Adds Nothing, and the Frontier Is `ccdigest_update`

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
 xnu_entry_kv_written=0x0000001a
 xnu_entry_kv_in_dram=0x0000001a
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ccdigest_update

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x1a = 26 = strlen("ccdigest_update") + 11`.
One `undef` breadcrumb, at line 3454, before the jump - as in every run since 207.

The prediction was `stub_hit=ccdigest_update` and it held, on the second application of the corrected
method.

## The step: four bytes, one instruction, nothing added

`osfmk_corecrypto_cchmac_src_cchmac_update.o` is the smallest object this link will ever carry, and
its entire content is a tail call:

```
00000000 <cchmac_update>:
   0: eafffffe 	b	0 <ccdigest_update>
			0: R_ARM_JUMP24	ccdigest_update
```

So the step resolves one symbol and pays no new obligation at all: `ccdigest_update` is *already* a
stub, added two experiments ago by experiment 213, and a `b` to a name the image already treats as
missing is free - the same accounting as experiments 207, 208, 209 and 212. This is the first step in
the sequence with **zero added**, and the fourth zero-added step overall (204, 207, 209, 215).

It is also the only step so far whose cost is *negative*: entry text went from 287760 to 287728,
down 32 bytes. The generated stub that stood in for `cchmac_update` was larger than the function
that replaces it - a produced stub has to report a name and reach the epilogue, where the real thing
is one branch. Nothing else moved: the image, `.bss`, the boot-args offset and the headroom are all
identical.

## What this leaves in place

Because `cchmac_update` is a tail call, `hmac_dbrg_update`'s call at 0x3c8 now transfers control to
`ccdigest_update` - the generated stub - which reports and ends the run. Nothing between the two is
executed, so this run's own measurement is only that the tail call was taken.

What is worth recording is what the *next* run will exercise, because `ccdigest_update` is the first
object in this sequence that makes a **second** call through a pointer this project has already
repaired once. Its 280 bytes contain two indirect calls, and every input it reads is real as of
experiment 214:

```
  28: udiv r1, r5, r2       ; 32-bit hardware divide, real on this core
  30: ldr  r3, [r7, #24]    ; di->compress
  3c: blx  r3               ; -> sha1_compress, real since 214 and a leaf
  5c: ldmib r7, {r0, r2}    ; di->state_size = 20, di->block_size = 64
```

So the run after this one should be the first in which a `ccdigest_*` helper runs to completion - and
`hmac_dbrg_update`'s own body already contains `sha1_compress` executing from experiment 214, so the
new thing is specifically that the *generic* digest helper completes rather than the compression.

## Cost

```
resolved (1):  cchmac_update
added    (0):  -
368 -> 367 undefined
```

Both directions link, taken by standing an empty object in for the new object.

| | exp-214 | now |
| --- | --- | --- |
| entry objects linked | 54 | 55 (`cchmac_update.o`) |
| entry text | 287760 B | 287728 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 368 | 367 |
| stubs | 307 functions, 61 storage | 306 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880090 B | 880090 B |

The payload did not change at all - it embeds the entry image, and the entry image's file size did
not move, so the twenty-four bytes the image lost are inside padding the payload had already
reserved.

## What is next: `ccdigest_update`, and the prediction is `cchmac_final`

The frontier is `ccdigest_update`, defined by `osfmk_corecrypto_ccdigest_src_ccdigest_update.o` -
**280 bytes of text, no data, no `.bss`, 2 references (`memcpy`, and `di->compress` indirectly), 1
definition**.

**The prediction is `stub_hit=cchmac_final`**, and it does not need the generic helper to succeed or
fail: with `ccdigest_update` real, `hmac_dbrg_update` continues past it, and the *only* remaining
stubbed symbol on any path from there is `cchmac_final`. Its disassembly shows both arms reaching it,
so the prediction does not depend on which of `da`/`db`/`dc` is non-empty:

```
 3dc: bl   cchmac_update      ; real now (tail call to ccdigest_update)
 3e0: cmp  r9, #0             ; the da/db/dc emptiness tests
 404: bne  420                ; -> 418: bl cchmac_update  (real)
 41c: ...
 49c: bne  4bc                ; -> 4c0: bl cchmac_final
 4a4: bl   cchmac_update      ; real
 4b4: bl   cchmac_final       ; <-- STUB, the stop, on the fall-through arm
 4b8: b    4d0
 4bc: ...
 4c0: bl   cchmac_final       ; <-- STUB, the stop, on the branch arm
```

`cchmac_final` is 120 bytes and references `memcpy`. It is the last of the four `cchmac_*` functions,
and the one after it will reach `di->final` = `ccdigest_final_64be`, which experiment 214 observed to
be a stub - so that is the prediction after this one, subject to the same rule.

That the two arms converge is itself a consequence of the corrected method: the earlier habit of
reading a relocation list would have given the same answer here by luck, because both arms lead to
the same symbol. The reason to state it is that the *previous* step's answer would have been wrong
the same way, and there is no way to tell the two cases apart except by reading the disassembly.

## Reproduce

```bash
# the step: 1 resolved, 0 added, 368 -> 367
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CCHMAC_UPDATE_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

# the whole object
arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_update.o
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_update.o \
  | sed -n '/<cchmac_update>:/,/^$/p'

# ... and it ran
./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=ccdigest_update

# why the next stop is cchmac_final, on both arms
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o \
  | sed -n '/<hmac_dbrg_update>:/,/^$/p' | sed -n '/3dc:/,/4c8:/p'
arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_final.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_final.o
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
