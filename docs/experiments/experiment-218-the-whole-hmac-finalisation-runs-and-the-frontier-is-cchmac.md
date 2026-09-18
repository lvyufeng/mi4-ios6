# Experiment 218 — the Whole HMAC Finalisation Runs on the Device, the Text Arithmetic Gets Three Terms, and the Frontier Is `cchmac`

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
 xnu_entry_kv_written=0x00000011
 xnu_entry_kv_in_dram=0x00000011
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=cchmac

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x11 = 17 = strlen("cchmac") + 11`.
One `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=cchmac` and it held - the fourth consecutive prediction made from the
disassembly rather than from a relocation list.

## Where the stop is *is* the evidence this time

Experiment 217's doc noted that a clean stub hit one symbol further along does not by itself mean
anything new ran. This run is the other case, and the difference is positional rather than textual:
the stop is in the **caller**, two calls past the thing that was linked.

```
 cchmac_final (real since 217)
   0x24: blx   di->final   -> ccdigest_final_64be   (real now: 500 bytes)
   0x5c: bl    memcpy
   0x74: bx    di->final   -> ccdigest_final_64be   (second invocation, a tail call)
   ...returns to hmac_dbrg_update...
 0x4f8 or 0x5a8: bl  cchmac                        <-- STUB, the stop
```

So for `hmac_dbrg_update` to reach `cchmac`, `cchmac_final` must have returned, which requires both of
its calls through `di->final` to have entered real code, executed, and returned - and each of those
invocations is a full generic digest finalisation, with two `blx r3` calls of its own into
`sha1_compress` on the paths it takes. That is the complete HMAC-SHA1 finalisation: the inner digest
finalised, copied into the outer context by `cchmac_final`'s own `memcpy`, and the outer digest
finalised by the tail call. The machinery of `early_random`'s DRBG is now whole from the compression
function down to the HMAC wrapper.

**The same qualifier as experiments 214 and 218's predecessors still applies, and it still matters.**
`ccsha1_initial_state` is a stub - 20 zero bytes - so the SHA-1 state that gets finalised started from
a zeroed IV. The *machinery* is complete; the digest is not SHA-1's. Nothing about this run
distinguishes the two, and the only way to see it is the symbol table:

```
$ arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | grep -w ccsha1_initial_state
002773c0 00000014 B ccsha1_initial_state
```

`B` is `.bss`: 20 bytes, zeroed, with no initialiser anywhere in the image. It is one of the two
symbols `ccsha1_eay.o` brought in in experiment 214 and has not been reached by the frontier yet,
because no run has named it.

## Cost

`osfmk_corecrypto_ccsha1_src_ccdigest_final_64be.o` - **500 bytes of text, no data, no `.bss`, 1
definition, 0 undefined symbols, 2 indirect calls** (0xb4 and 0x19c), both through `di->compress`:

```
resolved (1):  ccdigest_final_64be
added    (0):  -
365 -> 364 undefined
```

Both directions link, taken by standing an empty object in for the new object.

| | exp-217 | now |
| --- | --- | --- |
| entry objects linked | 57 | 58 (`ccdigest_final_64be.o`) |
| entry text | 288080 B | 288528 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 365 | 364 |
| stubs | 304 functions, 61 storage | 303 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880090 B | 880090 B |

The entry image's *file* size has been 387904 bytes since experiment 214 and does not move: its
window is fixed and every step grows inside it. That is why the payload's sections never change while
its hashes do - the image is carried as a fixed-size blob in the payload's `.rodata`
(`xnu_arm_entry_blob.o`, 387908 bytes in `stage90.map`), so a step shows up in the payload as a
different hash at the same size, never as a different size.

## The text arithmetic has three terms, not two

Experiments 216 and 217 both reported that `.text` grew by a number that did not match the symbol
arithmetic, and experiment 216 proposed that the difference was the section's tail being rounded up
to 16. **This step's numbers falsified that**: 500 bytes of real text replace a 12-byte stub, so the
symbol sizes grow by 488, and `.text` grows by 448 - a difference of 40, which no rounding to 16 can
produce. The corrected account, measured across all four builds of this stretch:

```
                         end of last    symbol region   tail after it    .text
                         text symbol    (0x200000..)    (unsymbolized)   total
  exp-216 A (stub)       0x00244b54       281428            6284          287712
  exp-216 B (real)       0x00244c5c       281692            6276          287968
  exp-217 B (real)       0x00244ccc       281804            6260          288064
  exp-218 B (real)       0x00244eb4       282292            6220          288512

  exp-216 step:  symbols +264   tail -8    .text +256      (object 280 B replacing a 12 B stub)
  exp-217 step:  symbols +112   tail -16   .text  +96      (object 120 B replacing a 12 B stub)
  exp-218 step:  symbols +488   tail -40   .text +448      (object 500 B replacing a 12 B stub)
```

`.text` does not end with the last symbol. `objdump -s` over its tail shows NUL-terminated strings -
`zinit`, `zone_change`, `zone_free_count`, and `__TEXT`/`__DATA` section descriptors - so the
unsymbolized region holds the generated stubs' own name pool together with the kernel's section-name
table. It *shrinks* as stubs are removed, and it is aligned, so the distance from the last symbolized
byte to its start moves with the symbol region as well. That is why the numbers are 8, 16 and 40
rather than the removed names' lengths, and the tail figure above is not a measurement of a name - it
is everything after the last symbolized byte, alignment slack included. The rule that replaces
experiment 216's is therefore: **`.text` = symbol region + gaps + this tail, and its growth is only
accounted for by measuring all three; the tail is not padding and its size is not derivable from the
object's size.**

The corrections are also appended to experiment 216's and 217's own documents, in place.

## What is next: `cchmac`, and the prediction is `cc_clear`

The frontier is `cchmac`, defined by `osfmk_corecrypto_cchmac_src_cchmac.o` - **128 bytes of text, no
data, no `.bss`, 1 definition, 4 references: `cchmac_init`, `cchmac_update`, `cchmac_final` (all
three real now) and `cc_clear` (a stub)**.

**The prediction is `stub_hit=cc_clear`**, and this is the easiest one in the sequence to justify
because the object has no branches at all - it is straight-line code, so there is no arm analysis and
no data dependence to reason about:

```
   0: push  {r4, r5, r6, sl, fp, lr}
   ...
  2c: sub   r5, sp, r0           ; the HMAC context, allocated on the stack (VLA-sized)
  34: mov   r0, r4               ; di
  38: mov   r1, r5               ; ctx
  3c: bl    cchmac_init          ; real
  40: ldr   r3, [fp, #8]         ; key/msg pointers from the argument block
  50: bl    cchmac_update        ; real, a tail call into ccdigest_update
  54: ldr   r2, [fp, #12]
  60: bl    cchmac_final         ; real since 217
  64: ldmib r4, {r0, r1}         ; di->state_size, di->block_size
  68: add   r0, r1, r0, lsl #1
  70: add   r0, r0, #12          ; the context size, as cchmac_ctx_size computes it
  74: bl    cc_clear             ; <-- STUB, the stop
  78: sub   sp, fp, #16
  7c: pop   {r4, r5, r6, sl, fp, pc}
```

`cc_clear` is a 12-byte stub at `0x00239834` and is defined by
`out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_clear.o` - so this step resolves one symbol and adds
none, the same accounting as the four steps before it, and the step after it will be the first in
this stretch whose object is *not* in the `cchmac`/`ccdigest` family.

## Reproduce

```bash
# the step: 1 resolved, 0 added, 365 -> 364
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CCDIGEST_FINAL_64BE_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

# the three-term arithmetic: build each configuration, then read .text and the last symbol
# 216A: all three empty   216B: ccdigest_update real   217B: + cchmac_final real   218B: all real
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CCDIGEST_UPDATE_OBJ=/tmp/empty.o STAGE90_ENTRY_CCHMAC_FINAL_OBJ=/tmp/empty.o \
   STAGE90_ENTRY_CCDIGEST_FINAL_64BE_OBJ=/tmp/empty.o ./build_entry.sh && \
   cp ../../../out/stage90/xnu_arm_entry.elf /tmp/216A.elf)
# ... and the same three times, dropping one empty object each time
for e in /tmp/216A.elf /tmp/216B.elf /tmp/217B.elf /tmp/218B.elf; do
  printf '%s ' "$e"; arm-none-eabi-size -A $e | sed -n '3p'
  arm-none-eabi-nm -S --numeric-sort $e | awk '$3=="T"||$3=="t"' | tail -1
done

# the tail is a name pool, not padding
arm-none-eabi-objdump -s --start-address=0x00246500 --stop-address=0x00246540 /tmp/216A.elf | tail -4
arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | grep -w ccsha1_initial_state   # B, 0x14, zeroed

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=cchmac

# why the next stop is cc_clear: cchmac has no branches at all
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac.o \
  | sed -n '/<cchmac>:/,/^$/p'
arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | grep -E "\bcc_clear$"
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_clear.o
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
