# Experiment 214 — `ccsha1_eay_di` Gets Its Value, Real SHA-1 Compression Runs on the Device, and the Frontier Is `cchmac_update`

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
 xnu_entry_kv_written=0x00000018
 xnu_entry_kv_in_dram=0x00000018
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=cchmac_update

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x18 = 24 = strlen("cchmac_update") + 11`, and
exactly one `undef` breadcrumb, before the jump, as in every run since 207.

This is the first prediction made with the method experiment 213's failure forced,
and it is the first run in which the log carries the renamed contract field:
`stage90_xnu_entry_stub_prehandoff_no_exception=0x00000001`.

## This is the step that gave the DRBG a digest

Experiment 213's prefetch abort at address 0 was `cchmac_init` doing `blx r3` with
`r3 = di->compress` = NULL, because `ccsha1_eay_di` was a zeroed storage stand-in. `ccdigest_info`
is `{output_size, state_size, block_size, oid_size, oid, initial_state, compress, final}`, and after
this step the linked image holds real values in all eight slots. Read out of the ELF at
`ccsha1_eay_di` = `0x00244b3c`, not out of the source:

| offset | field | value |
| --- | --- | --- |
| 0x00 | `output_size` | `0x00000014` = 20 |
| 0x04 | `state_size` | `0x00000014` = 20 |
| 0x08 | `block_size` | `0x00000040` = 64 |
| 0x0c | `oid_size` | `0x00000007` = 7 |
| 0x10 | `oid` | `0x00244b34` |
| 0x14 | `initial_state` | `0x002773c0` - `ccsha1_initial_state`, **a stub** |
| 0x18 | `compress` | `0x00238058` - `sha1_compress`, **real** |
| 0x1c | `final` | `0x002494c4` - `ccdigest_final_64be`, **a stub** |

The three fields the zeros of which the previous three runs each observed one of - `output_size` by
experiment 212's `bhi` guard, `initial_state` by the NULL source of a zero-length `memcpy` in
experiment 213, `compress` by the prefetch abort - are now all real or explicitly not-yet-real, and
their real values are known rather than inferred.

**And `sha1_compress` ran.** `cchmac_init` calls `blx r3` twice with `r3 = di->compress` (0x130 and
0x198), the object's `sha1_compress` is a **leaf** - `objdump -dr` over it shows zero `R_ARM_*`
entries and no `blx`/`ldr pc` - so both calls entered real code, executed, and returned, and
`cchmac_init` reached its `pop` at 0x1b8. `hmac_dbrg_update` then executed the four instructions
between its two calls and stopped at `cchmac_update`.

That is the first cryptographic computation in this project's history to execute on the device. It is
also the most carefully-qualified claim in this document, because of what is still zero:

**`ccsha1_initial_state` is a stub.** `nm -S` in this image reports `ccsha1_initial_state` at
`0x002773c0`, size `0x00000014` (20 bytes), type `B` - zeroed. `cchmac_init` `memcpy`s those 20 bytes
into the digest context before compressing, so the compression ran over a **zeroed** SHA-1 state
rather than the standard IV. So the honest statement is: the SHA-1 compression *code* executed and
returned; the digest it produced is not SHA-1's. And `di->final` is `ccdigest_final_64be`, a function
stub, so the finalisation step would still report a stub hit rather than produce anything.

The reason this is worth separating so carefully is that the run's own output - a clean stub hit one
symbol past the previous one - looks exactly like every other successful step. Nothing in it
distinguishes "real SHA-1 ran" from "real SHA-1 ran on zeros", and the only thing that does is the
symbol table.

## Cost

`osfmk_corecrypto_ccsha1_src_ccsha1_eay.o` (`osfmk/corecrypto/ccsha1/src/ccsha1_eay.c`) - **4932
bytes of text, no data, no `.bss`, 2 references**:

```
resolved (1):  ccsha1_eay_di
added    (2):  ccdigest_final_64be ccsha1_initial_state
367 -> 368 undefined
```

Both directions link, taken by standing an empty object in for the new object.

| | exp-213 | now |
| --- | --- | --- |
| entry objects linked | 53 | 54 (`ccsha1_eay.o`) |
| entry text | 282800 B | 287760 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 367 | 368 |
| stubs | 306 functions, 61 storage | 307 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880078 B | 880090 B |

4960 bytes of text, and `.bss` did not move: `ccsha1_eay_di` was a 68-byte zeroed stand-in and is now
68 bytes of real `.rodata`, so the storage stub that disappears and the storage stub that appears
(`ccsha1_initial_state`, 20 bytes) are both inside the region the image already had. Nothing above
the image moved, which is why the payload grew by 12 bytes rather than by 5 KB - it embeds the entry
image, and the entry image's *file* size is unchanged.

## What is next: `cchmac_update`, four bytes of object, and then `ccdigest_update`

The frontier is `cchmac_update`, defined by `osfmk_corecrypto_cchmac_src_cchmac_update.o` - **4 bytes
of text**, which is the smallest object this project will ever link, and its entire content is one
instruction:

```
00000000 <cchmac_update>:
   0: eafffffe 	b	0 <ccdigest_update>
			0: R_ARM_JUMP24	ccdigest_update
```

So it resolves `cchmac_update` and adds nothing: `ccdigest_update` is *already* a stub in this image,
added by experiment 213. It is also checkable in one line the corrected way - a tail call has no
`blx`, and the object has no branches to speak of.

**The prediction is `stub_hit=ccdigest_update`**, and the reasoning is now the kind experiment 213
demanded. The `b` lands in the stub, so nothing further matters for the stop - but it is worth
recording what `ccdigest_update` will do once it is real, because it is the first object in this
sequence to make a *second* call through a pointer this project just repaired. Its disassembly
(280 bytes, **2 indirect calls**) does:

```
  28: udiv r1, r5, r2       ; a 32-bit hardware divide, real on this core
  30: ldr  r3, [r7, #24]    ; di->compress
  3c: blx  r3               ; -> sha1_compress, real since this step, and a leaf
  5c: ldmib r7, {r0, r2}    ; di->state_size, di->block_size - 20 and 64, real
```

Every input it reads is real as of this step, and its indirect call now lands in a leaf. So the step
after next should be the first in which a `ccdigest_*` helper runs to completion on the device - and
the prediction for it will be made from that disassembly rather than from the relocation list.

## Reproduce

```bash
# the step: 1 resolved, 2 added, 367 -> 368
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CCSHA1_EAY_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 2 added    (unique to B)

# ... and the run's result, plus the renamed contract field
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|OK:"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=cchmac_update
grep -n "prehandoff_no_exception" /tmp/cancro-last_kmsg.txt

# what the digest is now made of, read out of the ELF rather than the source
addr=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="ccsha1_eay_di"{print "0x"$1}')
arm-none-eabi-objdump -s --start-address=$addr --stop-address=$((addr+32)) out/stage90/xnu_arm_entry.elf | tail -4
arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | grep -wE "ccsha1_initial_state|ccsha1_eay_di"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -w sha1_compress

# sha1_compress is a leaf, which is what makes "it ran to completion" a claim the log cannot contradict
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_ccsha1_src_ccsha1_eay.o \
  | sed -n '/<sha1_compress>:/,/^$/p' | grep -cE "R_ARM_|blx|ldr\s+pc"     # 0

# the next object, in full - it is four bytes and one instruction
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_update.o \
  | sed -n '/<cchmac_update>:/,/^$/p'
# ... and the one after it, whose inputs are now all real
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_ccdigest_src_ccdigest_update.o \
  | sed -n '/<ccdigest_update>:/,/^$/p' | sed -n '1,20p'
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
