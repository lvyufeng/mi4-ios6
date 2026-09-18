# Experiment 211 — the PRNG Links, the Handoff Tree Grows Boot Entropy, and the Frontier Is `ccdrbg_factory_nisthmac`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90 chosen_random_seed_bytes=0x00000040
MI4IOS6_STAGE90 chosen_random_seed_sum=0x00001e17
MI4IOS6_STAGE90 chosen_random_seed_zero_bytes=0x00000001
...
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000022
 xnu_entry_kv_in_dram=0x00000022
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ccdrbg_factory_nisthmac

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `no_exception=0x00000001`, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001` (`ttbr0_before == ttbr0_after == 0x00118000`,
`sctlr_before == sctlr_after == 0x00c5487b`), `persistent_write_attempted=0x00000000` in all 25
contracts that report it, and the device returned to Android on its own.

`ccdrbg_factory_nisthmac` is the first stub on the path *after* `early_random`, which is what the
prediction said it would be. Exactly one `undef` breadcrumb appears in the whole capture - at line
3454, `undef: addr=0x00049b3c lr=0x00049b40`, which is the payload's own Stage88 self-test
`trigger_stage90_undef_test`, and it is 450 lines *before* the jump into XNU at line 3909. Nothing
after the jump was an undefined-instruction trap. That is the part of this result that matters
most, and it is a negative: see below.

## Two things this experiment did, and why they are one step

The frontier rule is unchanged - the previous run named `early_random`, so this step links
`osfmk/prng/random.c` → `osfmk_prng_random.o`, and it is the link, not a decision, that does it. But
this step also adds a property to the device tree, and that is a departure worth stating rather than
burying.

**The object cannot run without the property, and the property's absence is fatal by design.**
`arm_init.c:390` is `__stack_chk_guard = (unsigned long)early_random();`. `early_random` with
`erandom.seedset == 0` asks `PE_get_random_seed` for `sizeof(EntropyData.buffer)` = 64 bytes, and
`random.c:311-317` says of the short case:

```c
		if (cnt < sizeof(EntropyData.buffer)) {
			/*
			 * Insufficient entropy is fatal.  We must fill the
			 * entire entropy buffer during initializaton.
			 */
			panic("EntropyData needed %lu bytes, but got %u.\n", ...);
		}
```

`PE_get_random_seed` (`pexpert/gen/pe_gen.c:119`) returns 0 both when `/chosen` has no `random-seed`
property and when every byte it copied was null. This project's tree had none - `grep -rn
"random-seed" stages/stage90/ tools/` was empty before this experiment. So the first version of this
step, which linked the object and left the tree alone, walked straight into that `panic`.

Running into it deliberately would be aiming a device at a branch whose stated purpose is to stop the
machine, and the forward version settles the same question just as well: if the run reports a stub
past `early_random`, then `PE_get_random_seed` returned 64 and `entropy_readall` ran, which is only
possible if the seed was there. It was, and it did.

**This is the third gap of its kind this project has found in its own tree**, after `state` on the
cpu nodes (experiment 193) and `device_type = "timer"` on `/arm-io`: a node or property XNU reads and
the tree was not written to provide. The difference is that this one is load-bearing - nothing past
`arm_init`'s fifth statement runs without it - and that it is *data* rather than a shape, so the two
`state`-style checks (`tools/host_dt_requirements.py`, `tools/host_dt_check.sh`) can only confirm its
presence and length, not its content.

### The seed, and what it is not

The device's real iBoot entropy is not available to a `fastboot boot` payload, so the value is
simulated, and the whole of the rule is in `stage90_main.c` next to the other handoff properties:

```c
seed[i] = (uint8_t)(("mi4ios6-cancro-stage90"[i % 22]) ^ (uint8_t)(i * 31))
```

Twenty-two ASCII bytes for reproducibility, `i * 31` so the sixty-four bytes are not that cycle
repeated - `PE_get_random_seed` would not reject a repeated cycle, but it is a weaker stand-in than it
needs to be. Exactly one of the sixty-four bytes comes out zero, and that is worth stating rather than
glossing: the test is `null_count == size`, a count against the whole length, not a presence check, so
one null in sixty-four is not the condition it rejects. The run prints the length, the byte sum and
the zero count (`0x40`, `0x1e17`, `1`) so the value is auditable against the rule in the source rather
than taken on trust.

What it is *not*: a measurement. Nothing here measured the device's entropy, and the doc says so
because a plausible-looking 64-byte seed would otherwise be indistinguishable from a real one.

## The panic path, corrected: there was no stub on it

This is the part of the step that was wrong the first time, and the wrong version would have made the
run unreadable, so it is recorded here rather than quietly fixed.

`panic` is real as of experiment 201 and is a tail call to `panic_trap_to_debugger`. The earlier
reading, taken from `debug.c` top to bottom, was that its path is all real until
`PEHaltRestart(kPEPanicBegin)` in `iokit_Kernel_PlatformExpert.o` - a stub - and so the run would
report `stub_hit=PEHaltRestart`. Disassembling the function instead of reading it says otherwise:

```
 96c: bl   ml_wants_panic_trap_to_debugger     ; returns FALSE, so the `beq` is taken
 9a0: bl   current_processor                   ; CPUDEBUGGERCOUNT++ -> 1
 9b8: cmp  r0, #6
 9bc: bcc  a14                                 ; TAKEN - 1 < NESTEDDEBUGGERENTRYMAX + 1
 9c4: bl   PEHaltRestart                       ; NOT REACHED
```

`PEHaltRestart` sits behind `CPUDEBUGGERCOUNT > NESTEDDEBUGGERENTRYMAX` (`debug.c:173`, 5) and that
count is `PROCESS_DATA(...).db_entry_count`, zeroed `.bss`. The other candidates on the path are
skipped too, and each for a reason measured in the image rather than assumed:

| branch | why it is not taken |
| --- | --- |
| `PE_arm_debug_panic_hook` | `B` in the image, so NULL |
| `write_trace_on_panic && kdebug_enable` → `kdbg_dump_trace_to_file` | both `B`, so 0 |
| `ctx != NULL` → `DebuggerSaveState` + `handle_debugger_trap` | `panic` passes `ctx = 0` (`mov r3, #0`, 0x244) |

What remains is `DebuggerTrapWithState`, which after `DebuggerSaveState` executes `TRAP_DEBUGGER` -
`debug.c:121`, `#define TRAP_DEBUGGER __asm__ volatile("trap")`, an ARM `udf` - and then
`panic_stop()`.

**`panic_stop()` is `panic_spin_forever()`, not `pmCPUHalt`.** `debug.c:133-137` defines the
`pmCPUHalt(PM_HALT_PANIC)` form only `#if defined(__i386__) || defined(__x86_64__)`; everything else
gets `#define panic_stop() panic_spin_forever()`. Both `panic_spin_forever` and
`paniclog_append_noflush` are real in this image, so the whole ending is a write into the panic log
and then `for (;;) { }`.

The `udf` matters because of where it goes. The payload installs its own vector table with a VBAR
write in `start.S` - a control register, unaffected by the MMU - and its `vector_undef` calls
`stage90_undef_c_handler`, which logs `MI4IOS6_STAGE90 undef: addr=... lr=...` and returns to the
next instruction. So the sequence would have been: an `undef` breadcrumb from inside XNU, then
`panic_spin_forever` spinning forever.

**So there was no undefined symbol anywhere on the panic path, and no stub could have been hit.** The
run would have produced no `stub_hit` line at all, and the payload's watchdog and dead-man - armed on
every run whether or not a hang is expected - would have been the only way out. That is why the
`PEHaltRestart` prediction is not merely a wrong name: it described a run that could not happen.

## The step cost three symbols and added four

`osfmk_prng_random.o` (`osfmk/prng/random.c`) - **2488 bytes of text, 488 of data, 20 of `.bss`, 26
references**:

```
resolved (4):  early_random EntropyData prng_cpu_init read_random
added    (4):  cc_clear ccdrbg_factory_nisthmac ccdrbg_factory_yarrow ccsha1_eay_di
361 -> 361 undefined
```

Both directions link, taken by standing an empty object in for `osfmk_prng_random.o`.

**The flat 361 → 361 needs unpacking, because the two-direction measurement is not the same
comparison as the one against experiment 210's image.** The A side (empty object) also removes the
hand-written `EntropyData` stand-in, because both are behind `STAGE90_ENTRY_REAL_ARM_INIT`; so A is
not exp-210's image, and A's 361 includes an `EntropyData` the generator had to produce. Against
exp-210's 360 the movement is **three resolved and four added, 360 → 361**, plus `EntropyData`
changing hands from a hand-written array to the object's initialized struct.

The stub counts read the same way: **300 functions, 61 storage**. Three functions resolved and three
were added, so the function count is unchanged; storage went 60 → 61 because `EntropyData` was never
in the generator's count in the first place (it was hand-written in `entry_stubs.c`) while
`ccsha1_eay_di` - `R`, read-only data - now is.

The four additions come from four different objects, which is the shape of the next several steps:

| added | defined by |
| --- | --- |
| `cc_clear` | `osfmk_corecrypto_cc_src_cc_clear.o` |
| `ccdrbg_factory_nisthmac` | `osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o` |
| `ccdrbg_factory_yarrow` | `osfmk_prng_prng_yarrow.o` |
| `ccsha1_eay_di` | `osfmk_corecrypto_ccsha1_src_ccsha1_eay.o` |

Only the first of them is reached: `early_random`'s disassembly puts `ccdrbg_factory_nisthmac` at
0x1b4 and `cc_clear` at 0x224, and a stub hit is terminal.

### `EntropyData`, and a correction to experiment 210's doc

The stand-in at `entry_stubs.c:140` was `uint8_t EntropyData[68]`, sized from `nm -S` and correct in
size - and it was the *wrong value*, because the real thing is initialized:
`entropy_data_t EntropyData = { .index_ptr = EntropyData.buffer }`. That is why the object's `.data`
is 488 bytes rather than empty. A size check cannot catch this, and the way it finally surfaced was
not a measurement at all: the link reported `multiple definition of 'EntropyData'` because the
stand-in had been left outside any guard while its four predecessors were each put behind one. It is
the fifth stand-in retired under its own switch (`STAGE90_ENTRY_REAL_ENTROPY_DATA`, after
`STAGE90_ENTRY_REAL_ARM_INIT`, `..._REAL_PMAP_BOOTSTRAP`, `..._REAL_KPRINTF`, `..._REAL_PANIC`) and
the second whose replacement is an initialized value rather than zero, after the four regions of
`data.s` in experiment 196.

**Correction to experiment 210's doc**, which said "`EntropyData` and `erandom` are *currently
generated storage stubs* in this image". Neither was: `EntropyData` was the hand-written array in
`entry_stubs.c`, which is why retiring it does not move the generator's storage count, and `erandom`
was not a stand-in of any kind - `grep -c '^erandom$'` against this step's A-side undefined list is
0, because nothing in the image referenced it while `early_random` was a stub. After the step both
are the object's own symbols, at `EntropyData 0x0025e4f8` (`D`, 68 bytes) and `erandom 0x0025e53c`
(`d`, local).

## Cost

| | exp-210 | now |
| --- | --- | --- |
| entry objects linked | 50 | 51 (`osfmk/prng/random.o`) |
| entry text | 278064 B | 280560 B |
| entry image | 371008 B | 387880 B |
| entry `.bss` | 0x0025a4f8–0x002731c8 (101584 B) | 0x0025e680–0x00277408 (101768 B) |
| undefined | 360 | 361 |
| stubs | 300 functions, 60 storage | 300 functions, 61 storage |
| boot_args offset | +479232 | +495616 |
| headroom below `topOfKernelData` | 1625656 B | 1608696 B |
| payload text | 862830 B | 880054 B |

The image size moved this time - the first time in six experiments - because `EntropyData`'s 488
initialized bytes are now really in `.data` and the four new stubs follow it. The device tree grew by
about 100 bytes with the seed: the host harness and the device agree exactly on the result,
`29528` bytes built on the host against `stage90_xnu_entry_stub_device_tree_length=0x00007358` in the
run, which is the first time this project has had the two numbers side by side. The tree is now at
90.1% of its 32768-byte buffer, and `host_dt_check.sh` warns above 95% - so the next couple of
properties are affordable and the one after that may not be.

## What is next: `ccdrbg_factory_nisthmac`, and the state it was supposed to build

The frontier is `ccdrbg_factory_nisthmac` (`osfmk/corecrypto/ccdbrg/src/ccdrbg_nisthmac.c:494`),
which is `osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o`: **1620 bytes of text, 24 of data, no
`.bss`, 9 references, 7 definitions** - `ccdrbg_factory_nisthmac`, `ccdrbg_nisthmac_info`, and the
five statics `init`, `generate`, `reseed`, `done`, `hmac_dbrg_update`.

What the stub skipped is not a return value but four function pointers and a size:

```c
void ccdrbg_factory_nisthmac(struct ccdrbg_info *info, const struct ccdrbg_nisthmac_custom *custom)
{
	info->size = sizeof(struct ccdrbg_nisthmac_state) + sizeof(struct ccdrbg_nisthmac_custom);
	info->init = init;
	info->generate = generate;
	info->reseed = reseed;
	info->done = done;
	info->custom = custom;
};
```

`ccdrbg_init` is not a symbol in this image and does not need to be - it is inlined, and in
`early_random` it is the **indirect** `blx r7` at 0x214, where `r7 = drbg_info.init`. So the step
after this one has a shape this project has not met in fifty experiments: linking the object makes
the factory real, and the very next thing that happens is a call through a pointer the factory just
wrote.

**The prediction for experiment 212 is `stub_hit=cchmac_init`.** Read from `init`'s disassembly
rather than from the source's nesting, because the first call in the function is not the one that
runs:

```
   8: ldr  r0, [r0, #20]      ; info->custom
  18: ldr  r2, [r0]          ; custom->di
  1c: strd r0, [r4]          ; state->custom = custom; state->bytesLeft = 0
  20: ldr  r2, [r2]          ; di->output_size
  24: cmp  r2, #64
  28: bhi  cc                ; NOT TAKEN - the stub's field reads 0
  ...
  3c: bls  50                ; TAKEN
  ...
  48: bl   cc_clear          ; NOT REACHED - the failure path from 0x40/0xcc
  50: ...                    ; success: state->vptr/nextvptr, vsize = keysize = 0
  88: bl   memset            ; memset(key, 0, 0)
  9c: bl   memset            ; memset(v, 1, 0)
  b8: bl   hmac_dbrg_update  ; -> whose first call is cchmac_init (0x3b4)
```

`cc_clear` is on the error path and `hmac_dbrg_update`'s own body reaches `cc_clear` only after
`cc_cmp_safe` (0x5f8, against `cchmac_init` at 0x3b4). `cchmac_init` is defined by nothing in this
tree's linked set and referenced by nothing - it is absent from the image entirely - so it does not
exist as a stub today and will arrive as a new one in exactly the step that makes `init` real.

### The thing to settle before that run

`erandom`'s initializer is `.drbg_custom = { .di = &ccsha1_eay_di, .strictFIPS = 0 }`, and
`ccsha1_eay_di` is, as of this experiment, a **storage stub** - one of the four obligations the step
added. `init` reaches the digest through it: `state->custom->di`, then `di->output_size`, then
`di->state_size` and `di->block_size` for `cchmac_ctx_decl`. A zeroed stand-in for a `ccdigest_info`
reads 0 for all of those, which is the *right-size-and-wrong-value* class this project has now met
six times, and it is worse than an ordinary missing symbol because nothing reports it: the pointer is
non-NULL, the fields read as zero, and the run's disassembly above shows the failure path being
*skipped* because `di->output_size` is 0. What stops the boot is the next missing symbol, not this
one. So linking `ccdrbg_nisthmac.o` is not by itself enough for a working DRBG, and the step that
links `osfmk_corecrypto_ccsha1_src_ccsha1_eay.o` is the one that gives `ccsha1_eay_di` a value.

## Reproduce

```bash
# the step: 4 resolved, 4 added, 361 -> 361 (against exp-210's image: 3 resolved, 4 added, 360 -> 361)
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_PRNG_RANDOM_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 4 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 4 added    (unique to B)

# the contract fix, and that the tree and the device agree on its size
grep -rn "random-seed" stages/stage90/ ; echo "(empty before this experiment)"
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|random-seed"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=ccdrbg_factory_nisthmac
grep -c "undef: addr=" /tmp/cancro-last_kmsg.txt          # 1, and it is before the jump
grep -n "undef: addr=\|jumping to XNU's _start" /tmp/cancro-last_kmsg.txt

# the seed the run used, against the rule in the source
grep -E "chosen_random_seed" /tmp/cancro-last_kmsg.txt
sed -n '/STAGE90_CHOSEN_RANDOM_SEED_BYTES/,/^}/p' stages/stage90/stage90_main.c

# the panic path, and that nothing on it is undefined
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_debug.o \
  | sed -n '/<panic_trap_to_debugger>:/,/^$/p' | sed -n '1,25p'
sed -n '130,140p' external/xnu-4570.1.46/osfmk/kern/debug.c
sed -n '651,656p' external/xnu-4570.1.46/osfmk/kern/debug.c
for s in PEHaltRestart kdbg_dump_trace_to_file panic_spin_forever paniclog_append_noflush; do
  grep -qx $s /tmp/B.txt && echo "STUB $s" || echo "$s: defined or absent - check the ELF"
done
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E " (write_trace_on_panic|kdebug_enable|PE_arm_debug_panic_hook)$"

# the seed that was there before too, and the four obligations the step added
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_prng_random.o
sed -n '/^ccdrbg_factory_nisthmac/,/^}/p' \
  external/xnu-4570.1.46/osfmk/corecrypto/ccdbrg/src/ccdrbg_nisthmac.c
arm-none-eabi-size out/xnu_kernel_obj/osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_prng_random.o \
  | sed -n '/<early_random>:/,/^$/p' | sed -n '/1b4:/,/22c:/p'
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
