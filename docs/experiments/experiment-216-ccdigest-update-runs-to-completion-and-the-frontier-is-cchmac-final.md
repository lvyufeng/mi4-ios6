# Experiment 216 — `ccdigest_update` Runs to Completion, the First Generic Digest Helper on the Device, and the Frontier Is `cchmac_final`

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
 xnu_entry_kv_written=0x00000017
 xnu_entry_kv_in_dram=0x00000017
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=cchmac_final

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x17 = 23 = strlen("cchmac_final") + 11`.
One `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=cchmac_final` and it held. Because `cchmac_final` is reached on **both**
arms of `hmac_dbrg_update`'s `da`/`db`/`dc` emptiness tests (0x4b4 and 0x4c0), the run does not
distinguish them - which was the point of predicting it that way: the prediction had to be right
whichever way the tests came out.

## This is the step where a generic digest helper ran

Three firsts, in the order they happen inside `ccdigest_update`:

1. **The first object with no stubbed symbol on its own path.** Every input it reads is real -
   `di` is the `ccsha1_eay_di` given its value in experiment 214, so `di->compress` is
   `sha1_compress`, `di->state_size` and `di->block_size` are 20 and 64, and its one reference,
   `memcpy`, has been real since long before this sequence.
2. **Its `blx r3` at 0x3c is the first indirect call in this project to land in *real* code and
   return.** Experiment 214's `sha1_compress` calls were indirect *into* a leaf that was already
   real, but they were made from `cchmac_init`, which is itself a stub-free object only because
   everything it needs is a value; here the caller is the generic helper and the call site is
   reached after the object's own data-dependent branches (`cmp r2,#64`/`cmp r2,#128` at 0x78/0x80,
   the `ldmib r7, {r0, r2}` block-size read at 0x5c) have been taken.
3. **Its `udiv r1, r5, r2` at 0x28 - a 32-bit hardware divide.** Krait has it, and it is a single
   instruction with no relocation, so there is nothing that could stand in its way. The payload's own
   C code has certainly divided before, so the claim is not "the first divide on this SoC"; it is the
   first one on the XNU entry path, and the only reason to name it is that everything else this
   object does not need is stubbed, so the arithmetic itself is the part that is new.

None of the three is visible in the log. The log says only that the next symbol is `cchmac_final`,
which is what every successful step's log says. What makes the three claims is the object's own
disassembly plus the values the previous steps put in `ccsha1_eay_di` - and the qualifier from
experiment 214 still applies: the digest is still computed over a zeroed state, because
`ccsha1_initial_state` remains a stub. What ran to completion here is the generic machinery, not
SHA-1's IV.

## Cost

`osfmk_corecrypto_ccdigest_src_ccdigest_update.o` (`osfmk/corecrypto/ccdigest/src/ccdigest_update.c`)
- **280 bytes of text, no data, no `.bss`, 1 definition, 1 reference (`memcpy`), 2 indirect calls**,
both through `di->compress` (0x3c and 0xd8):

```
resolved (1):  ccdigest_update
added    (0):  -
367 -> 366 undefined
```

Both directions link, taken by standing an empty object in for the new object. `nm -S` over the two
ELFs says exactly one symbol changed size - `ccdigest_update`, `0x0000000c` -> `0x00000118` - and no
symbol was added or removed.

| | exp-215 | now |
| --- | --- | --- |
| entry objects linked | 55 | 56 (`ccdigest_update.o`) |
| entry text | 287728 B | 287984 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 367 | 366 |
| stubs | 306 functions, 61 storage | 305 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880090 B | 880090 B |

**The text arithmetic is named rather than rounded.** 280 bytes of real text replace a 12-byte
generated stub, so the sum of the symbol sizes grows by 268, and it does: summing every symbol size
in the two ELFs gives 366280 -> 366548 exactly. But `.text` grows by 256, not 268. Measurements:

```
                                    A          B     delta
sum of all symbol sizes          366280     366548     +268
everything after the last
  symbolized byte of .text         6284       6276       -8
.text section size (ELF)         287712     287968     +256
"text size" as build_entry.sh
reports it                       287728     287984     +256
```

No symbol was added or removed and `nm -S` says exactly one changed size - `ccdigest_update`,
`0x0000000c` -> `0x00000118` - so the 12 bytes are in the unsymbolized part of `.text`, not in a
third symbol.

**Correction, added after the run (experiment 218).** This section originally said that every `.text`
size here is a multiple of 16, so the section grows by the *rounded* content change and the 12 bytes
are the section's tail padding. That is wrong, and experiment 218's step is what falsified it: there
the symbol sizes grew by 488 while `.text` grew by 448, a difference of 40, which no rounding to 16
can produce. `.text` does not end with the last symbol; `objdump -s` over its tail shows
NUL-terminated strings - `zinit`, `zone_change`, `zone_free_count`, and `__TEXT`/`__DATA` section
descriptors - i.e. the generated stubs' own name pool and the kernel's section-name table. That
unsymbolized region shrinks when a stub is removed, and it is *aligned*, so the distance from the
last symbolized byte to its start also changes with the symbol region; the -8 above is the two of
those together, not the length of `ccdigest_update`. The decomposition measured for the three steps
of this stretch is in experiment 218's document. What survives from the original paragraph is only
the part that was measured: the two ways of counting differ, and both are stated here.

## What is next: `cchmac_final`, and the prediction is `ccdigest_final_64be`

The frontier is `cchmac_final`, defined by `osfmk_corecrypto_cchmac_src_cchmac_final.o` - **120 bytes
of text, no data, no `.bss`, 1 definition, 1 reference (`memcpy`), 2 indirect calls** (0x24 and 0x74,
the second a tail `bx`).

**The prediction is `stub_hit=ccdigest_final_64be`.** The reason is one line of the object, made the
corrected way - from the disassembly and the register values, not from a relocation list, since
neither indirect call has a relocation at all:

```
   0: push  {r4, r5, r6, lr}
   4: mov   r6, r0              ; r6 = di          (cchmac_final(di, ctx, out))
   8: ldr   r0, [r0, #4]        ; di->state_size   = 20
   c: ldr   r3, [r6, #28]       ; di->final        = ccdigest_final_64be
  10: mov   r4, r2              ; out
  14: add   r0, r1, r0          ; ctx + state_size - the state area inside the hmac ctx
  18: mov   r5, r1              ; ctx
  1c: add   r2, r0, #8
  20: mov   r0, r6              ; -> di
  24: blx   r3                 ; <-- STUB ccdigest_final_64be, the stop
```

There is no conditional branch anywhere before 0x24, so the call is unconditional and does not
depend on the caller's data either - which is why this prediction needs no arm analysis, unlike the
previous one. The `blx` at 0x74 is a tail call to the same `di->final` and is unreachable while the
first one is a stub.

The signature is worth confirming from the header rather than from the call site, because the
argument order is the kind of thing this project has been wrong about before:
`void cchmac_final(const struct ccdigest_info *di, cchmac_ctx_t ctx, unsigned char *digest)`
(`EXTERNAL_HEADERS/corecrypto/cchmac.h:84`) - `di` first, so `r6 = r0 = di` and `[r6, #28]` really is
`di->final` and not a field of the context. `hmac_dbrg_update` agrees: at 0x4ac/0x4b0 it sets
`r0 = r5`, `r1 = r8` immediately before `bl cchmac_final`, and the previous step's `cchmac_init`
reads `di->block_size` and `di->state_size` from the same `r5`.

`cchmac_final` resolves one symbol and adds nothing, the same accounting as the previous step. The
step after it will be `osfmk_corecrypto_ccsha1_src_ccdigest_final_64be.o` - **500 bytes, zero
undefined symbols, two indirect calls both through `di->compress`** (0xb4, 0x19c) - which is the
object that will finally make `di->final` real, and whose own stop will therefore be past itself
rather than inside itself.

## Reproduce

```bash
# the step: 1 resolved, 0 added, 367 -> 366
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CCDIGEST_UPDATE_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt; cp out/stage90/xnu_arm_entry.elf /tmp/A_elf
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

# the 268 vs 256: one symbol changed size, and the rest is section padding
arm-none-eabi-nm -S /tmp/A_elf | sort -k4 > /tmp/symA.txt
arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | sort -k4 > /tmp/symB.txt
diff /tmp/symA.txt /tmp/symB.txt | grep ccdigest_update
arm-none-eabi-size -A /tmp/A_elf | head -3; arm-none-eabi-size -A out/stage90/xnu_arm_entry.elf | head -3

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=cchmac_final

# why the next stop is ccdigest_final_64be: the object has no conditional branch before its first
# indirect call, and [r6,#28] is di->final because r0 is di
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_final.o \
  | sed -n '/<cchmac_final>:/,/^$/p'
sed -n '84p' external/xnu-4570.1.46/EXTERNAL_HEADERS/corecrypto/cchmac.h
arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_ccsha1_src_ccdigest_final_64be.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_corecrypto_ccsha1_src_ccdigest_final_64be.o
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
