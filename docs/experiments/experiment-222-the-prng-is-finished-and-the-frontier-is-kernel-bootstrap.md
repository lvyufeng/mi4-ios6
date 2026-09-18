# Experiment 222 — the PRNG Is Finished, `arm_init` Completes, and the Frontier Is `kernel_bootstrap`'s `bsd_scale_setup`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=bsd_scale_setup

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x1a = 26 = strlen("bsd_scale_setup") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=bsd_scale_setup`, and it was the longest one in this sequence: it named
a symbol in a function - `kernel_bootstrap` - that the entry image had never reached.

## The PRNG is finished

The stop is inside `kernel_bootstrap`, which means everything between it and the previous stop
returned. Read out of the entry ELF rather than inferred:

```
 cc_cmp_safe (376 B, constant-time compare)       <- this step
   -> hmac_dbrg_update returns CCDRBG_STATUS_OK
   -> `init` returns OK                    (its last call is hmac_dbrg_update; only a pop follows)
   -> early_random's two `if (rc != OK) panic(...)` checks pass
   -> ccdrbg_generate -> generate: cchmac, its own cc_cmp_safe, 8 bytes copied out
   -> early_random returns those 8 bytes
   -> arm_init:  __stack_chk_guard = (r0 & ~0xff00)     <-- the DRBG's output is now the canary
                 machine_startup
   -> machine_startup: four PE_parse_boot_argn, then kernel_bootstrap
   -> kernel_bootstrap: _consume_printf_args, PE_parse_boot_argn x4, PE_parse_boot_arg_str,
                        the cluster/scale arithmetic, and then
      bsd_scale_setup                                   <-- the stop
```

**The frontier is no longer in corecrypto.** `early_random` returned a value, `arm_init` completed,
`machine_startup` ran, and the run is executing `kernel_bootstrap` - XNU's own C startup, the
function that brings up the scheduler, IPC, the VM subsystem and BSD. Two statements of what that
means, kept apart deliberately:

- **Measured, from this run:** every call on the path above returned, so the HMAC_DRBG's
  instantiate-and-generate sequence is complete; `early_random` returns; `arm_init` reaches its last
  statement; `machine_startup` and `kernel_bootstrap`'s first several hundred bytes execute.
- **Not measured, and not implied:** that the *randomness* is any good, or that XNU is "running the
  OS". `ccsha1_initial_state` is still 20 zero bytes, so every HMAC behind that canary was computed
  over a zeroed IV, and `arm_init`'s stack canary is a deterministic function of the device tree and
  the timebase rather than of real entropy. The machinery is real; the value is not SHA-1's, and it
  never will be via this route until that stub is replaced.

The FIPS compares did not fire. That is evidence, and only evidence, that the DRBG's V and V' differ
as intended: the alternative was `cc_try_abort` → `panic` → `TRAP_DEBUGGER` → an `exception:` line,
and the log has none. Whether the two are unequal *cryptographically* or merely different in their
low bits is not something this run can tell - it only tells that they are not equal.

## Cost

`osfmk/corecrypto/cc/src/cc_cmp_safe.o` - **376 bytes of text, no data, no `.bss`, 1 definition, 0
undefined symbols**, the last object of the corecrypto stretch:

```
resolved (1):  cc_cmp_safe
added    (0):  -
362 -> 361 undefined
```

Both directions link, taken by standing an empty object in for the new object. Three-term
decomposition: symbol sizes +364 (376-byte object, 12-byte stub), symbol region +356, tail -4, `.text`
+352 - `288704 -> 289056` in the ELF, `288720 -> 289072` as `build_entry.sh` reports it.

| | exp-221 | now |
| --- | --- | --- |
| entry objects linked | 61 | 62 (`cc_cmp_safe.o`) |
| entry text | 288720 B | 289072 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 362 | 361 |
| stubs | 301 functions, 61 storage | 300 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880090 B | 880090 B |

## What is next: `bsd_scale_setup`, and the prediction is `bsd_exec_setup`

The frontier is `bsd_scale_setup`, defined by `out/xnu_kernel_obj/bsd_dev_unix_startup.o`
(`bsd/dev/unix_startup.c`) - **1291 bytes of text, 96 of data, 48 of `.bss`, 4 definitions, 21
references**, and the whole of `bsd_scale_setup` is one instruction:

```
0000045c <bsd_scale_setup>:
 45c: eafffffe 	b	0 <bsd_exec_setup>
			45c: R_ARM_JUMP24	bsd_exec_setup
```

**The prediction is `stub_hit=bsd_exec_setup`**, and it follows from two facts that need no arm
analysis: the tail call is unconditional, and `bsd_exec_setup` is not in this image - it is defined by
`bsd_kern_bsd_init.o` (`bsd/kern/bsd_init.c`), which is not linked yet. So the symbol the *next* run
stops on is one this step adds, exactly as experiment 220's step added `memset_s`.

That makes this the largest step in a while, and the table for it will be unlike the last seven:

- `bsd_dev_unix_startup.o` defines **four** symbols - `bsd_startupearly`, `bsd_mbuf_cluster_reserve`,
  `bsd_bufferinit` and `bsd_scale_setup` - so the single-symbol frontier becomes a group, none of
  which is reached except the last;
- it references **21**, of which `__aeabi_uldivmod`, `bzero`, `panic`, `PE_get_default` and
  `PE_parse_boot_argn` are already real, and the other sixteen are either already stubs from earlier
  objects or arrive now - among them `bsd_exec_setup`, `bufinit`, `kernel_memory_allocate`,
  `kmem_suballoc`, `sysctl_handle_int`, and the storage the BSD side needs (`kernel_map`, `mb_map`,
  `mbutl`, `desiredvnodes`, `sane_size`, `nmbclusters`, `mbuf_default_ncl`, `buf_headers`,
  `sysctl__kern_children`, `tcp_recvspace`, `tcp_sendspace`). The measured split is in the next
  experiment's table, not in this document: the build reports 300 -> 304 function stubs and 61 -> 67
  storage stubs, and the entry image's file size moves for the first time since experiment 214.

So the run after this one should stop on `bsd_exec_setup` immediately, whatever the group's size,
because `bsd_scale_setup`'s single instruction branches straight into it - and the step after that
will be `bsd_kern_bsd_init.o`, which is where BSD's own startup begins. Worth noting for the table:
this is the first step since experiment 214 whose undefined *count* will go **up**.

## Reproduce

```bash
# the step: 1 resolved, 0 added, 362 -> 361
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CC_CMP_SAFE_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A): cc_cmp_safe
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=bsd_scale_setup
grep -c 'exception:' /tmp/cancro-last_kmsg.txt                      # 0: no FIPS compare fired

# the path that got here, and the next one's single instruction
arm-none-eabi-objdump -d --start-address=0x0020d604 --stop-address=0x0020d690 out/stage90/xnu_arm_entry.elf | tail -8
arm-none-eabi-objdump -dr out/xnu_kernel_obj/bsd_dev_unix_startup.o | grep -E "^[0-9a-f]+ <|R_ARM_JUMP24"
arm-none-eabi-size out/xnu_kernel_obj/bsd_dev_unix_startup.o
arm-none-eabi-nm -u out/xnu_kernel_obj/bsd_dev_unix_startup.o | wc -l    # 21
for o in out/xnu_kernel_obj/*.o; do arm-none-eabi-nm --defined-only "$o" 2>/dev/null \
  | grep -qw bsd_exec_setup && echo "bsd_exec_setup is defined by: $o"; done
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
