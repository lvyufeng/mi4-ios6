# Experiment 217 — `cchmac_final` Resolves, Nothing New Executes, and the Frontier Is `ccdigest_final_64be`

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
 xnu_entry_kv_written=0x0000001e
 xnu_entry_kv_in_dram=0x0000001e
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ccdigest_final_64be

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x1e = 30 = strlen("ccdigest_final_64be") + 11`.
One `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=ccdigest_final_64be` and it held - the third consecutive prediction
made from the disassembly rather than from a relocation list, and the second that needed no arm
analysis at all, because `cchmac_final` has no conditional branch before its first indirect call.

## This is a step with nothing new executing, and that is worth saying

`cchmac_final` is now real, and it contains a `memcpy`, an arithmetic block, and *two* calls through
`di->final` - the `blx r3` at 0x24 and the tail `bx r3` at 0x74. The first of the two is a stub, and
it is the first thing the function does after its prologue, so the run stops five instructions in:

```
   0: push  {r4, r5, r6, lr}
   4: mov   r6, r0              ; di
   8: ldr   r0, [r0, #4]        ; di->state_size
   c: ldr   r3, [r6, #28]       ; di->final
  10: mov   r4, r2              ; out
  ...
  24: blx   r3                 ; <-- STUB, the stop
```

So unlike experiments 214, 215 and 216, this run's own log is the whole of what it measured: a real
object's prologue executed and an indirect call was made through a pointer that is still missing.
Nothing new was computed, and the digest machinery is no further along than it was after 216.

The step still counts, and it is the one the frontier rule names: the symbol that stopped the previous
run is now defined by real code, one object at a time, and the cost is stated below. But it is the
first step in the sequence whose *measurement* is its own accounting and nothing else, which is worth
a sentence rather than a paragraph of implied progress.

## Cost

`osfmk_corecrypto_ccsha1_src_ccdigest_final_64be.o` - the object is in the `ccsha1` directory even
though what it defines is a generic digest helper: **500 bytes of text, no data, no `.bss`, 1
definition, 0 undefined symbols, 2 indirect calls** (0xb4 and 0x19c), both through `di->compress`.

```
resolved (1):  cchmac_final
added    (0):  -
366 -> 365 undefined
```

Both directions link, taken by standing an empty object in for the new object. That this object has
**zero undefined symbols** is why it adds nothing: everything it reaches is either already real
(`sha1_compress`, via `di->compress`) or the `memcpy` long since linked.

| | exp-216 | now |
| --- | --- | --- |
| entry objects linked | 56 | 57 (`ccdigest_final_64be.o`) |
| entry text | 287984 B | 288080 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 366 | 365 |
| stubs | 305 functions, 61 storage | 304 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880090 B | 880090 B |

120 bytes of object replace a 12-byte generated stub, so the symbol arithmetic says +108 and `.text`
grows by 96 - the same 16-byte section-tail rounding experiment 216 named, checked the same way
(`nm -S` says exactly one symbol changed size, `cchmac_final`, `0x0000000c` -> `0x00000078`, and none
was added or removed).

## What is next: `ccdigest_final_64be`, and the prediction is `cchmac`

The frontier is `ccdigest_final_64be`, defined by the object linked in this step. **The prediction is
`stub_hit=cchmac`**, and this one is the first in three that needs the arm analysis again - but it is
the *same* answer on both arms, which is a different kind of "safe" from experiment 216's.

After `cchmac_final` returns, `hmac_dbrg_update` resumes at 0x4b8 or 0x4c4, and the two arms differ:

```
 4b4: bl   cchmac_final        ; real now, returns
 4b8: b    4d0                 ; ---- arm A ----
 4bc: ...
 4c0: bl   cchmac_final        ; real now, returns
 4c4: orr  r0, r6, r9          ; ---- arm B: (da_len | db_len)
 4c8: cmp  r0, #1
 4cc: bne  578                 ; if not 1 -> 0x578
 4d0: mov  sl, r7              ; arm A continues here
 ...
 4f8: bl   cchmac              ; <-- STUB, arm A's stop
 ...
 578: mov  r7, #0              ; arm B's own path
 ...
 5a8: bl   cchmac              ; <-- STUB, arm B's stop
```

Both arms call the same symbol - `cchmac`, the one-shot init/update/final wrapper, 128 bytes and still
a 12-byte stub at `0x0023967c`. So this prediction does not need to know which arm is taken, and it
also cannot be wrong in a way that teaches anything about the data: if it is wrong, the mistake is in
the reading of the disassembly, not in a guess about a length argument.

What makes the step *after* this one different in kind: with `ccdigest_final_64be` real,
`cchmac_final` runs to completion for the first time - its `memcpy`, its arithmetic block, and both of
its calls into `di->final` (which returns to `hmac_dbrg_update` on the second one, because it is a
tail call). That is the first time the whole HMAC finalisation executes on the device, and it
compresses real data with a real `sha1_compress`. The qualifier from experiment 214 still stands and
still matters: the SHA-1 state it finalises started from `ccsha1_initial_state`, which remains a
zeroed stub, so the digest produced is not SHA-1's - the *machinery* is complete, not the value.

## Reproduce

```bash
# the step: 1 resolved, 0 added, 366 -> 365
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CCHMAC_FINAL_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=ccdigest_final_64be

# why the next stop is cchmac, on both arms
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o \
  | sed -n '/<hmac_dbrg_update>:/,/^$/p' | sed -n '/4b4:/,/5b0:/p' | grep -E "cchmac|cmp|bne|orr"
arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | grep -E "\bcchmac$"
arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac.o
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
