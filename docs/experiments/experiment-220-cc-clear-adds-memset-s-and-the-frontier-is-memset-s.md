# Experiment 220 — `cc_clear` Resolves and Adds `memset_s`, `.text` Does Not Move, and the Frontier Is `memset_s`

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
 xnu_entry_kv_written=0x00000013
 xnu_entry_kv_in_dram=0x00000013
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=memset_s

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x13 = 19 = strlen("memset_s") + 11`.
One `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=memset_s` and it held - the sixth consecutive prediction made from the
disassembly rather than from a relocation list.

## The route was the analysed one, not the obvious one

Experiment 219's document said that with `cc_clear` real the run would stop at `memset_s` reached
*from inside `cchmac`*, and not at `cc_cmp_safe` - the next stubbed symbol in the caller, at 0x5e4 -
because two more `cchmac` invocations sit between. That is what happened, and it is the first time in
this stretch that the object's own internal call has been the stop rather than the caller's next
call. The distinction matters for the step after this one: it means `cc_cmp_safe` is still untouched,
and the run has two `cchmac` invocations and one `cc_clear` left to go before it gets there.

## Nothing new executed, and for the first time nothing new was added to `.text` either

`cc_clear`'s four instructions ran and the tail branch went into the `memset_s` stub, so this run
computed nothing. What is new is in the accounting rather than on the device:

```
resolved (1):  cc_clear
added    (1):  memset_s        <-- the first added symbol since experiment 214
363 -> 363 undefined           <-- the count is unchanged for the first time in this stretch
```

`cc_clear` is a four-instruction argument shuffle whose whole content is `b memset_s`, and `memset_s`
was not in the image, so the frontier *moved sideways*: one symbol resolved, one obligation taken on.
That is the frontier rule's own shape rather than an exception to it - the object named by the run is
linked, and what it needs decides the next run - but it is the first step since experiment 214 whose
undefined count has not gone down, and the first step in this stretch whose `.text` has not moved at all: `288640 ->
288640` in the ELF, `288656 -> 288656` as `build_entry.sh` reports it. Twenty bytes of real text
replace a twelve-byte stub and a new twelve-byte stub appears, and the tail absorbs the difference.

| | exp-219 | now |
| --- | --- | --- |
| entry objects linked | 59 | 60 (`cc_clear.o`) |
| entry text | 288656 B | 288656 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 363 | 363 |
| stubs | 302 functions, 61 storage | 302 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880090 B | 880090 B |

## What is next: `memset_s`, and the prediction is `cc_cmp_safe`

The frontier is `memset_s`, defined by `out/xnu_kernel_obj/osfmk_kern_memset_s.o` - **80 bytes of
text, no data, no `.bss`, 1 definition, 1 reference**. Its reference is `secure_memset`, which is
**already real** in this image:

```
00202a78 T bcopy          00202a84 T memcpy
00202dac T memset         00202dac T secure_memset
00202dc4 T bzero
```

so this step resolves one symbol and adds none - and its own body cannot stop anywhere, because
nothing on its path is missing. `memset` and `secure_memset` sharing `0x00202dac` is worth having
written down again here: it means the wipe that `memset_s` performs is, in this image, eventually the
same assembly body that plain `memset` is.

**The prediction is `stub_hit=cc_cmp_safe`.** With `memset_s` real, the first `cc_clear` (inside the
first `cchmac`) completes and returns to `cchmac` at 0x78, which pops and returns to
`hmac_dbrg_update` at 0x4fc. From there the remaining calls - `cchmac_init` 0x510, `cchmac_update`
0x524 and 0x538, the conditional `cchmac_update` 0x564, `cchmac_final` 0x574, then the two more
`cchmac` at 0x5a8 and 0x5cc - are all real, and the first stubbed symbol on the path is
`cc_cmp_safe` at 0x5e4. Unlike the last one, this prediction is a plain list-walk: the object
resolves cleanly, so the stop is where the caller's next missing call is.

**A hazard to record before the step after that, because it is the first one in this sequence that
could end somewhere other than a stub hit.** `cc_cmp_safe.o` is 376 bytes with **zero undefined
symbols**, and once it is real the code at `0x5e8` forks on its result:

```
 5e4: bl   cc_cmp_safe        ; the DRBG's V-before against V-after
 5e8: cmp  r0, #0
 5ec: bne  608                ; different -> return normally
 5f0: mov  r0, #224
 5f4: mov  r1, r4
 5f8: bl   cc_clear
 5fc: mov  r0, #0
 600: bl   cc_try_abort       ; equal -> the DRBG failed its own check
 604: mvn  r7, #3
```

`cc_try_abort.o` is 16 bytes and its single reference is `panic`, which is **real** in this image
since the panic object was linked. So if the compare ever comes out equal, that run ends in
`panic` - and XNU's ARM `panic` reaches `TRAP_DEBUGGER`, which is a `udf`, which routes through VBAR
into the payload's undefined-instruction handler rather than into a stub. The log would then show an
`exception:` line instead of a `stub_hit=`.

That would be a *finding*, not a failure of the mechanism. The source says which check it is
(`osfmk/corecrypto/ccdbrg/src/ccdrbg_nisthmac.c:211` and `:440`, the two `cchmac`-based generate
paths):

```c
/* FIPS 140-2 4.9.2 Conditional Tests
   "Each subsequent generation of an n-bit block shall be compared with the previously
    generated block. The test shall fail if any two compared n-bit blocks are equal." */
if (0==cc_cmp_safe(state->vsize, state->vptr, state->nextvptr)) {
    /* The world as we know it has come to an end */
    cc_clear(sizeof(*state), state);
    cc_try_abort(NULL);
```

- the DRBG's own FIPS health check, whose failure case is deliberately fatal. It is also the first
thing in this sequence that could legitimately stop a run somewhere other than a stub, which is worth
knowing in advance so a run ending that way is read correctly. Nothing about this step's numbers
suggests it will: `cc_cmp_safe` is called with `state->vsize` (20 for SHA-1) and two 20-byte buffers
that are expected to differ.

There is a second thing in `cc_cmp_safe.o` that will matter later and not for the next run: it
contains **NEON** instructions (`vmov.i32 q8`, `vld1.8`, `veor`, `vorr`), reached only when the
length is 32 or more (`cmp r0,#32; bcc 0xc4`). The call at 0x5e4 passes 20, so the scalar path runs
and no NEON instruction executes. If a later call passes 32 or more, this becomes the first NEON code
in the project's history to run, and whether CPACR/FPEXC allow it in this CPU state is a question
nothing has asked yet. It is recorded here so that the run which asks it is recognised.

## Reproduce

```bash
# the step: 1 resolved, 1 added, 363 -> 363
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CC_CLEAR_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A): cc_clear
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 added    (unique to B): memset_s
wc -l /tmp/A.txt /tmp/B.txt                      # 363 and 363

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=memset_s

# why the next stop is cc_cmp_safe, and what could end a later run somewhere else
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_memset_s.o          # only secure_memset, real
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E "\b(memset|secure_memset)$"
arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_cmp_safe.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_cmp_safe.o   # empty
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_cmp_safe.o \
  | sed -n '/<cc_cmp_safe>:/,/^$/p' | grep -cE "vmov|vld1|veor|vorr"            # NEON, length >= 32
arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_try_abort.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_try_abort.o  # panic, real
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
