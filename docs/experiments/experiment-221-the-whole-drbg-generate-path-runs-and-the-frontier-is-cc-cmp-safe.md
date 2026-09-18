# Experiment 221 — the Whole DRBG Generate Path Runs to Its FIPS Compare, and the Frontier Is `cc_cmp_safe`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=cc_cmp_safe

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x16 = 22 = strlen("cc_cmp_safe") + 11`.
No `exception:` line anywhere, and one `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=cc_cmp_safe` and it held - the seventh consecutive prediction made from
the disassembly rather than from a relocation list, and the one that ended the longest single stretch
of PRNG work in this project's history.

## What ran: the DRBG's instantiate sequence completed, less its last two instructions

The stop is inside `hmac_dbrg_update` at its own FIPS compare, and everything before that returned:

```
 hmac_dbrg_update:
   3b4: cchmac_init       <- exp-212 stopped here
   3c8: cchmac_update     <- exp-214
   3c0..: the conditional additional-input updates
   4b4: cchmac_final      <- exp-216
   4f8: cchmac            <- exp-218, a complete one-shot HMAC (exp-219)
   510: cchmac_init       \  the second key schedule,
   524: cchmac_update     |  three more complete HMACs
   538: cchmac_update     |
   564: cchmac_update     |
   574: cchmac_final      /
   5a8: cchmac            \  two more, each ending in cc_clear -> memset_s
   5cc: cchmac            /
   5e4: cc_cmp_safe       <-- the stop
```

With `cc_clear`/`memset_s` real, the wipes inside each `cchmac` returned, so all of the above
executed. `hmac_dbrg_update` is called once from `init`, which is `ccdrbg_init`'s implementation, so
what this run measured is the **HMAC_DRBG instantiation running to its FIPS 140-2 4.9.2 conditional
test** - every HMAC it needs, and nothing left but the compare and the return. At least five complete
HMAC-SHA1 computations ran on the device in this one boot.

The qualifier is unchanged and still the whole of the value claim: `ccsha1_initial_state` is 20 zero
bytes, so every one of those HMACs was computed over a zeroed IV. The machinery is entirely real; the
digest is not SHA-1's. And the same note as experiments 217 and 219 applies: a run whose log reads
exactly like the previous one is not evidence of new work by itself - here the evidence is the
*position* of the stop, one call past five HMACs, not the text of the log.

## Cost

`osfmk/kern/memset_s.o` - **80 bytes of text, no data, no `.bss`, 1 definition, 1 reference**:

```
resolved (1):  memset_s
added    (0):  -
363 -> 362 undefined
```

Both directions link, taken by standing an empty object in for the new object. Three-term
decomposition: symbol sizes +68 (80-byte object, 12-byte stub), symbol region +64, tail -0, `.text`
+64 - `288640 -> 288704` in the ELF, `288656 -> 288720` as `build_entry.sh` reports it.

| | exp-220 | now |
| --- | --- | --- |
| entry objects linked | 60 | 61 (`memset_s.o`) |
| entry text | 288656 B | 288720 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 363 | 362 |
| stubs | 302 functions, 61 storage | 301 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880090 B | 880090 B |

## What is next: `cc_cmp_safe`, and the prediction is `bsd_scale_setup`

The frontier is `cc_cmp_safe`, defined by `out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_cmp_safe.o` -
**376 bytes of text, no data, no `.bss`, 1 definition, 0 undefined symbols**. It is a constant-time
compare, so with it linked there is **nothing missing anywhere in the DRBG**.

**The prediction is `stub_hit=bsd_scale_setup`**, and it is by far the longest prediction in this
sequence - it spans `hmac_dbrg_update`, `init`, `early_random`, `arm_init`, `machine_startup` and
`kernel_bootstrap`, and it says the PRNG is finished. Each step of it was read rather than assumed:

```
 1. cc_cmp_safe returns non-zero          (else 0x5ec bne -> 0x608; see the caveat below)
 2. hmac_dbrg_update returns r7 = 0       (0x578 mov r7,#0; 0x60c mov r0,r7)  = CCDRBG_STATUS_OK
 3. init returns OK                        (0xb8 is its last call; only a pop follows)
 4. early_random's two rc checks pass      (both `if (rc != OK) panic(...)`)
 5. early_random calls ccdrbg_generate     -> generate
 6. generate: addl_len = 0, so no hmac_dbrg_update; bytesLeft = 0, so
      0x210 -> 0x254 -> 0x25c cchmac (real) -> 0x290 cc_cmp_safe (now real)
      -> copies out 8 bytes -> 0x2b4 return
 7. early_random returns the 8 bytes
 8. arm_init stores them in __stack_chk_guard:
      328: bl  early_random
      330: bic r0, r0, #0xff00     ; the stack canary, from early_random
      338: str r0, [r1]            ; __stack_chk_guard
      340: bl  machine_startup
 9. machine_startup (real, 0x206b84, 244 bytes): four PE_parse_boot_argn (real) then
      206c6c: bl kernel_bootstrap  -- unconditional, the last thing it does
10. kernel_bootstrap (real, 0x20d560, 900 bytes):
      20d570: bl _consume_printf_args   (real, no calls of its own)
      20d584: bl PE_parse_boot_argn     (real; the one branch here, beq at 0x20d58c,
                                         skips a str and no call)
      ...   : bl PE_parse_boot_argn x3, PE_parse_boot_arg_str (real, calls DTLookupEntry
                                         and DTGetProperty, both real)
      20d604: bl bsd_scale_setup        <-- STUB at 0x00239900, the stop
```

So the run after this one should leave the PRNG, finish `arm_init`, enter `machine_startup` and then
`kernel_bootstrap` - the kernel's own C startup - and stop at the first symbol *there* that this
image does not provide. `bsd_scale_setup` is reached unconditionally: the only conditional branch
between `kernel_bootstrap`'s entry and it skips a store, not a call.

**The caveat is that this is the first prediction whose failure mode is not a wrong symbol but a
panic.** Both FIPS compares - `hmac_dbrg_update`'s at 0x5e4 and `generate`'s at 0x290 - branch to
`cc_clear` + `cc_try_abort` on the *equal* result, and `cc_try_abort`'s one reference is `panic`,
which is real in this image. `panic` reaches `TRAP_DEBUGGER`, a `udf`, which routes into the payload's
undefined-instruction handler. So there are three distinguishable outcomes to watch for, and the log
tells them apart:

| outcome | what it means |
| --- | --- |
| `stub_hit=bsd_scale_setup` | the prediction held: the PRNG is done and the kernel's C startup is the frontier |
| `exception:` with `ifar` inside `panic` | a FIPS compare came out equal - the DRBG's own test failing, which is a finding |
| `stub_hit=` something in the DRBG | the reading above is wrong somewhere, and the symbol names where |

The third case is the one that would be diagnostic rather than merely disappointing, which is why the
path is written out symbol by symbol above rather than summarised.

The NEON hazard noted in experiment 220 does not apply here: `cc_cmp_safe`'s vector path needs a
length of 32 or more, and both calls in play pass 20 (`state->vsize`, SHA-1's output size).

## Reproduce

```bash
# the step: 1 resolved, 0 added, 363 -> 362
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_MEMSET_S_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A): memset_s
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=cc_cmp_safe

# the path the next run should take, read out of the entry ELF rather than assumed
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o \
  | sed -n '/<init>:/,/<reseed>:/p' | grep -E "R_ARM_CALL|pop"
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_arm_init.o \
  | sed -n '/<arm_init>:/,/^$/p' | sed -n '/ 328:/,/ 348:/p'
arm-none-eabi-objdump -d --start-address=0x00206b84 --stop-address=0x00206c78 out/stage90/xnu_arm_entry.elf | tail -4
arm-none-eabi-objdump -d --start-address=0x0020d560 --stop-address=0x0020d608 out/stage90/xnu_arm_entry.elf | tail -6
arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | grep -E "\b(machine_startup|kernel_bootstrap|bsd_scale_setup)$"
arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_cmp_safe.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_cmp_safe.o   # empty
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
