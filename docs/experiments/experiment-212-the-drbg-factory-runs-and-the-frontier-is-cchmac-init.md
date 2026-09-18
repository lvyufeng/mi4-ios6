# Experiment 212 — the DRBG Factory Runs, `early_random` Makes Its First Indirect Call, and the Frontier Is `cchmac_init`

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
 xnu_entry_kv_written=0x00000016
 xnu_entry_kv_in_dram=0x00000016
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=cchmac_init

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `no_exception=0x00000001`, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001` (`ttbr0_before == ttbr0_after == 0x00118000`,
`sctlr_before == sctlr_after == 0x00c5487b`), `persistent_write_attempted=0x00000000` in all 25
contracts that report it, and the device returned to Android on its own.
`kv_written == kv_in_dram == 0x16`, which is `strlen("cchmac_init") + strlen(" stub_hit=") + 1`, so
the transfer that carries the name to DRAM worked - the same check the last two runs have used.

Exactly one `undef` breadcrumb in the capture, at line 3454 (`addr=0x00049b3c lr=0x00049b40`, the
payload's own Stage88 self-test), 455 lines *before* the jump into XNU at line 3909. Nothing after
the jump was an undefined-instruction trap.

## What the run proves: the first indirect call went where the factory pointed it

`ccdrbg_factory_nisthmac` is a stub no longer, and what it was standing in for is not a return value
but four function pointers and a size:

```c
void ccdrbg_factory_nisthmac(struct ccdrbg_info *info, const struct ccdrbg_nisthmac_custom *custom)
{
	info->size = sizeof(struct ccdrbg_nisthmac_state) + sizeof(struct ccdrbg_nisthmac_custom);
	info->init = init;  info->generate = generate;  info->reseed = reseed;  info->done = done;
	info->custom = custom;
};
```

`ccdrbg_init` is not a symbol in this image and does not need to be: it is a `static inline` in
`ccdrbg.h`, so `early_random`'s call to it compiled to the **indirect** `blx r7` at 0x214, with `r7`
loaded from `drbg_info.init`. The stub was returning without writing that field, so the pointer was
NULL - and this run is the first in the sequence where the two halves of a step are a *write* and a
*read through what was written*.

So `stub_hit=cchmac_init` proves, in order:

1. `ccdrbg_factory_nisthmac` was called and returned - it is in the image as of this step, and the
   call at 0x1b4 is a direct `bl`;
2. it stored real addresses into `erandom.drbg_info`, because the next thing executed was `blx r7`
   with `r7 = drbg_info.init` and it did not fault;
3. the function it called is `ccdrbg_nisthmac.c`'s `static int init`, which ran far enough to read
   its digest through `state->custom->di` and reach `hmac_dbrg_update`, whose first call is
   `cchmac_init`.

That is a different kind of statement from the last fifty runs. Every previous stop was "control
reached a call to a symbol this image lacks". This one is "control reached a call **the kernel's own
data** routed it to", which is only true because the object's initializer ran.

## What the run does *not* prove, and it matters more here than usual

The run proves `init` got **past** its digest-size guard. It does not prove the guard's input was
correct, and the two are easy to conflate. From `init`'s disassembly:

```
   8: ldr  r0, [r0, #20]   ; info->custom        1c: strd r0, [r4]   ; state->custom = custom
  18: ldr  r2, [r0]        ; custom->di          20: ldr  r2, [r2]   ; di->output_size
  24: cmp  r2, #64                               28: bhi  cc        ; -> error path -> cc_clear
  50: ...                 ; success              88: bl   memset    ; 0 bytes
  b8: bl   hmac_dbrg_update                      3b4:  (its first call) cchmac_init
```

Had `bhi` at 0x28 been taken, control would have gone to `mvn r9, #0; b 0x40` and hit the **`cc_clear`
stub** - which is real and unidentified, so the run would have reported `stub_hit=cc_clear` instead.
It reported `cchmac_init`, so `di->output_size <= 64`. That is all the run says.

`erandom`'s initializer is `.drbg_custom = { .di = &ccsha1_eay_di, .strictFIPS = 0 }`, and
`ccsha1_eay_di` is a **zeroed storage stand-in** - `B 002773c0` in this image, 44 bytes of zeroed
`.bss` where the real thing is an initialized `ccdigest_info` - so the value it read was 0, which is
one of the values consistent with the measurement. The real SHA-1 digest's `output_size` is 20, which
is also ≤ 64 and would produce the identical run. **So this experiment measured the guard, not the
digest, and it cannot tell those two apart.**

The consequence is the uncomfortable part, and it is [[mi4-stand-in-size-is-not-value]] in its
sharpest form yet: `init` proceeds as if its parameters were valid, `memset` writes 0 bytes where it
would write 64, and the DRBG is "initialised" over an all-zero digest. Every step from here until
`osfmk_corecrypto_ccsha1_src_ccsha1_eay.o` is linked will pass **for the wrong reason** - the code
runs, the guards do not fire, and nothing reaches DRAM that says the parameters were empty. The step
that fixes it is 4932 bytes defining `ccsha1_eay_di` and `sha1_compress`, and until it lands, the
honest description of `early_random` is "it runs", not "it works".

## The step cost one symbol and added six

`osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o` (`osfmk/corecrypto/ccdbrg/src/ccdrbg_nisthmac.c`) -
**1620 bytes of text, 24 of data, no `.bss`, 9 references, 7 definitions**:

```
resolved (1):  ccdrbg_factory_nisthmac
added    (6):  cc_cmp_safe cchmac cchmac_final cchmac_init cchmac_update cc_try_abort
361 -> 366 undefined
```

Both directions link, taken by standing an empty object in for the new object.

`cc_clear` is the object's seventh reference and it is **free**: it is already an undefined name in
this image (added by experiment 211), and adding a second reference to a name the image already
treats as missing costs nothing - the same accounting as experiments 207, 208 and 209. The six that
are added were referenced by nothing anywhere in the pool, so they had never been stubs and now are.
`ccdrbg_nisthmac_info` is the object's `D` definition and is simply linked; nothing in the image
names it (`early_random` uses `erandom.drbg_info`, its own).

## A structural fact this step found: corecrypto is one object per function

`cchmac_init` is not in `cchmac.o`. The pool has it in `osfmk_corecrypto_cchmac_src_cchmac_init.o`,
and the four members of that API are four objects:

| object | text | defines | references |
| --- | --- | --- | --- |
| `cchmac_init.o` | 444 B | `cchmac_init` | `ccdigest_init` `ccdigest_update` `memcpy` `memset` |
| `cchmac_update.o` | **4 B** | `cchmac_update` | `ccdigest_update` |
| `cchmac_final.o` | 120 B | `cchmac_final` | `memcpy` |
| `cchmac.o` | 128 B | `cchmac` | `cc_clear` `cchmac_init` `cchmac_update` `cchmac_final` |

So for the next stretch the frontier advances **one symbol at a time**, not one file: each step
resolves exactly what the previous run named and pays a few hundred bytes. That is a change in the
cost model worth recording, because the last twenty steps had been "one object, several symbols" and
this makes the remaining count look worse than the remaining work is. `cchmac_update` is four bytes -
a tail call to `ccdigest_update` - which is the smallest thing this project has ever linked.

## Cost

| | exp-211 | now |
| --- | --- | --- |
| entry objects linked | 51 | 52 (`ccdrbg_nisthmac.o`) |
| entry text | 280560 B | 282320 B |
| entry image | 387880 B | 387904 B |
| entry `.bss` | 0x0025e680–0x00277408 (101768 B) | 0x0025e698–0x00277408 (101744 B) |
| undefined | 361 | 366 |
| stubs | 300 functions, 61 storage | 305 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880054 B | 880078 B |

1760 bytes of text for one function and six stubs, and 24 bytes of image - which is the object's
`.data` (the `ccdrbg_nisthmac_info` struct and the statics' initializers) and nothing else: `.bss`
did not grow at all, because the six additions are function stubs and the storage count is unchanged.
The `.bss` region's *start* moved forward by exactly those 24 bytes while its end stayed at
`0x00277408`, so the region is 24 bytes smaller; the tree, the boot-args offset and the headroom are
untouched. The handoff device tree is still 29528 of 32768 bytes.

## What is next: `cchmac_init`, and the prediction is `ccdigest_init`

The frontier is `cchmac_init`, defined by `osfmk_corecrypto_cchmac_src_cchmac_init.o` - **444 bytes
of text, no data, no `.bss`, 4 references, 1 definition**. The prediction is *not*
`cchmac_update`, which is the next symbol in `hmac_dbrg_update`'s own body, and the disassembly is
what settles it:

```
cchmac_init's calls:   28: ccdigest_init      3c: ccdigest_update      f0: memset
                      10c: memcpy            17c: memcpy
hmac_dbrg_update's:   3b4: cchmac_init       3c8: cchmac_update       3dc: cchmac_update ...
```

`cchmac_init` calls `ccdigest_init` **before** `ccdigest_update`, and `hmac_dbrg_update` calls
`cchmac_init` before anything else, so the first stub reached with `cchmac_init` real is
`ccdigest_init`. Both `ccdigest_init` and `ccdigest_update` are absent from this image entirely -
defined by nothing, referenced by nothing - so they arrive as new stubs in that step; the other two
references are `memcpy` and `memset`, both real.

**And `ccdigest_init` is benign with a zeroed digest, which is the thing to check before aiming a run
at it.** Its whole body is a `memcpy` and three stores - there is no indirect call in it:

```
   8: ldr  r2, [r0, #4]     ; di field
   c: ldr  r1, [r0, #20]    ; di field
  14: add  r0, r4, #8
  18: bl   memcpy           ; copies however many bytes those fields said - 0, if di is zeroed
  28: ldmib r5, {r1, r2}    ; two more di fields, also 0
  38: pop  {r4, r5, fp, pc}
```

So the step after this one runs and returns rather than faulting, and the run's only claim about the
digest will again be that its size guard did not fire. The step that gives `ccsha1_eay_di` a value is
the one that ends that - `osfmk_corecrypto_ccsha1_src_ccsha1_eay.o`, 4932 bytes, defining
`ccsha1_eay_di` and `sha1_compress` and referencing `ccdigest_final_64be` and
`ccsha1_initial_state` - and it should be taken deliberately rather than reached, because until it is,
several consecutive runs will pass for a reason that is not the reason they appear to.

## Reproduce

```bash
# the step: 1 resolved, 6 added, 361 -> 366
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CCDRBG_NISTHMAC_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 6 added    (unique to B)

./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|OK:"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=cchmac_init
grep -n "undef: addr=\|jumping to XNU's _start" /tmp/cancro-last_kmsg.txt   # 1 breadcrumb, before the jump

# the write/read pair this step is: the factory, then the indirect call through what it wrote
sed -n '/^ccdrbg_factory_nisthmac/,/^}/p' \
  external/xnu-4570.1.46/osfmk/corecrypto/ccdbrg/src/ccdrbg_nisthmac.c
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_prng_random.o \
  | sed -n '/<early_random>:/,/^$/p' | sed -n '/1b4:/,/21c:/p'      # bl factory ... blx r7
grep -n "ccdrbg_init" external/xnu-4570.1.46/EXTERNAL_HEADERS/corecrypto/ccdrbg.h | head -3

# that the digest it read through is a zeroed stand-in, and that the guard cannot tell
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep ccsha1_eay_di
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o \
  | sed -n '/<init>:/,/^$/p' | sed -n '1,12p'
sed -n '/\.drbg_custom = {/,/}/p' external/xnu-4570.1.46/osfmk/prng/random.c

# the next object, and why the stop after it is ccdigest_init and not cchmac_update
arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_init.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_init.o
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_init.o \
  | sed -n '/<cchmac_init>:/,/^$/p' | grep -E "R_ARM_CALL" | head -3
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o \
  | sed -n '/<hmac_dbrg_update>:/,/^$/p' | grep -E "R_ARM_CALL" | head -2

# corecrypto builds one object per function - the cost model for the next stretch
for o in cchmac_init cchmac_update cchmac_final cchmac; do
  printf '%-16s ' "$o"
  arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_$o.o | tail -1 | awk '{print $1" bytes"}'
done
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Correction, added after the run (experiment 213)

The "What is next" section above predicts `stub_hit=ccdigest_init`, and **experiment 213's run
disproved it** - the run took a prefetch abort at address zero instead, and produced no stub hit at
all. The error is worth keeping because of *how* it was made: the prediction was read out of
`cchmac_init`'s relocation table (`objdump -dr | grep R_ARM_CALL`), and **an indirect call has no
relocation**. `cchmac_init` has three (`blx r3` at 0x58, 0x130, 0x198) that this method cannot see.

So the ordering argument in the paragraph above - `ccdigest_init` at 0x28 before `ccdigest_update` at
0x3c, therefore `ccdigest_init` is the stop - is true about the *source order of two direct calls*
and says nothing about which call control reaches, because neither is reached. Three instructions
above them, `cmp r0, r2; bcs 0x94` compares `di->block_size` against `key_len`, and with the zeroed
`ccsha1_eay_di` stand-in both are 0, so the branch is taken and the whole `ccdigest_*` pair is
skipped. What runs instead is `blx r3` with `r3 = di->compress` = NULL.

The paragraph about `ccdigest_init` being "safe to aim at" was right about that object - its 60 bytes
were read in full and do contain no indirect call - but it was answering a question about a call the
run never makes. See experiment 213's doc for the full trace and for the method fix: an object's call
graph is `objdump -d` plus a grep for `blx`/`ldr pc`, and a prediction needs the register values
feeding the branches as much as it needs the call list.
