# Experiment 219 — a Whole One-Shot HMAC Runs, `cchmac` Resolves, and the Frontier Is `cc_clear`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=cc_clear

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x13 = 19 = strlen("cc_clear") + 11`.
One `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=cc_clear` and it held - the fifth consecutive prediction made from the
disassembly rather than from a relocation list, and the one that needed the least: `cchmac` is
straight-line code with not a single branch, so there was no arm to reason about and no data to
reason with.

## A complete one-shot HMAC ran

`cchmac` is the convenience wrapper: `cchmac(di, ctx, key, key_len, digest)` allocates its own
context on the stack and runs `cchmac_init` (0x3c), `cchmac_update` (0x50) and `cchmac_final` (0x60)
in a row, then wipes the context with `cc_clear` (0x74). The stop is that wipe, so every call before
it returned, and exp-218 established that `cchmac_final` returning means the whole HMAC finalisation
- inner digest, `memcpy` into the outer context, outer digest - completed.

So this run executed **one complete HMAC-SHA1 computation from key to digest**, with every function
in the path real: the compression function, the generic digest init/update/final helpers, and the
HMAC wrapper above them. It is the first run in which a whole HMAC of any kind finishes, as opposed
to the partial paths the previous five steps measured.

The qualifier is unchanged and still load-bearing: `ccsha1_initial_state` is a 20-byte zeroed `.bss`
stub, so the HMAC that completes is computed over a zeroed IV. What is real is the machinery, not
SHA-1's value - and, as in experiment 218, nothing in this run's log distinguishes the two.

It is also worth naming what did *not* happen: this is a step whose cost is one object, one resolved
symbol and no new obligation, so the run is a measurement of the frontier again rather than of the
DRBG making progress. `early_random` still has not reached its own code past the HMAC layer.

## Cost

`osfmk_corecrypto_cchmac_src_cchmac.o` - **128 bytes of text, no data, no `.bss`, 1 definition, 4
references** (`cchmac_init`, `cchmac_update`, `cchmac_final`, `cc_clear`), and no branch instruction
anywhere in it:

```
resolved (1):  cchmac
added    (0):  -
364 -> 363 undefined
```

Both directions link, taken by standing an empty object in for the new object.

| | exp-218 | now |
| --- | --- | --- |
| entry objects linked | 58 | 59 (`cchmac.o`) |
| entry text | 288528 B | 288656 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 364 | 363 |
| stubs | 303 functions, 61 storage | 302 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880090 B | 880090 B |

The three-term decomposition experiment 218 established, applied here: the symbol *sizes* grow by
116 (128-byte object, 12-byte stub), the symbol region grows by 112, the tail after it by 16, and
`.text` by 128 - `288512 -> 288640` in the ELF, `288528 -> 288656` as `build_entry.sh` reports it.

## What is next: `cc_clear`, and the prediction is `memset_s`

The frontier is `cc_clear`, defined by `out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_clear.o` -
**20 bytes of text, no data, no `.bss`, 1 definition, 1 reference**, and the whole of it is a tail
call with an argument shuffle:

```
00000000 <cc_clear>:
   0: mov  r3, r0          ; n
   4: mov  r0, r1          ; p    cc_clear(n, p) -> memset_s(p, n, 0)
   8: mov  r1, r3
   c: mov  r2, #0
  10: b    <memset_s>      ; (R_ARM_JUMP24)
```

**The prediction is `stub_hit=memset_s`**, and this is the case the frontier rule was built for: the
run named `cc_clear`, the object that defines `cc_clear` reaches for a *new* symbol the moment it
executes, and that symbol is not in the image. `memset_s` is not in this image's undefined list today
(`grep -w memset_s out/stage90/xnu_arm_entry_undef.txt` is empty) because nothing has referenced it -
so this step is the first since experiment 214 that **adds** an undefined symbol rather than merely
resolving one, and the count should stay at 364 for the first time in this stretch: one resolved, one
added.

`memset_s` is not `memset`, and the object name settles that rather than the two symbols' similar
spellings. The image already has, from the ARM assembly layer:

```
00202a78 T bcopy          00202a84 T memcpy
00202dac T memset         00202dac T secure_memset     <- the same address: one body, two names
00202dc4 T bzero
```

so `memset` and `secure_memset` are two names for one function, and neither of them is `memset_s`.
`memset_s` is a different function in a different file (`osfmk/kern/memset_s.c`, 80 bytes of text,
one reference), and it is the C11 Annex K shape - it clamps `n` to `smax`, returns
`EINVAL`/`E2BIG`/`EOVERFLOW` as a status, and exists precisely so that a wipe cannot be optimised
away -

```c
memset_s(void *s, size_t smax, int c, size_t n)
{
        if (s == NULL) return EINVAL;
        if (smax > RSIZE_MAX) return E2BIG;
        if (n > smax) { n = smax; err = EOVERFLOW; }
        secure_memset(s, c, n);          /* assembly, in osfmk/arm/bzero.s */
        return err;
}
```

That last line is why `memset_s` is one step and not the last one: its own reference is
`secure_memset`, which is **already real** in this image at `0x00202dac`, from `osfmk/arm/bzero.s` -
the ARM assembly layer this project assembles itself and has linked since the earliest steps. So
linking `memset_s.o` after this will resolve one symbol and add none, and the stop after that moves on
to `cc_cmp_safe`.

What the step after this one looks like is now worth stating, because the run will not stop where the
call list suggests: with `cc_clear` real, control returns to `cchmac` at 0x78, pops, and goes back to
`hmac_dbrg_update` at 0x4fc. The next stubbed symbol on that path is `cc_cmp_safe` at 0x5e4 - but only
*after* two more `cchmac` invocations (0x5a8 and 0x5cc), each of which calls `cc_clear` again and
therefore `memset_s` again. So a run whose stop is `memset_s` will be reached from inside `cchmac`,
and `cc_cmp_safe` is the frontier after `memset_s` is real, not before.

## Reproduce

```bash
# the step: 1 resolved, 0 added, 364 -> 363
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CCHMAC_OBJ=/tmp/empty.o ./build_entry.sh)
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
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=cc_clear

# why the next stop is memset_s, and why memset_s is not memset
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_clear.o \
  | sed -n '/<cc_clear>:/,/^$/p'
grep -w memset_s out/stage90/xnu_arm_entry_undef.txt        # empty: nothing references it yet
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E "\bmemset"     # memset and memcpy are real
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
