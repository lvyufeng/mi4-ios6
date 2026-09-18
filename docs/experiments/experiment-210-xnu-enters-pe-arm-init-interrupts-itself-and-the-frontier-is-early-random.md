# Experiment 210 — XNU Enters `pe_arm_init_interrupts` Itself, `cpu_machine_idle_init` Returns, and the Frontier Is `early_random`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000017
 xnu_entry_kv_in_dram=0x00000017
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=early_random

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `no_exception=0x00000001`, `safety_boundary_preserved=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `ttbr0_before == ttbr0_after == 0x00114000` and
`sctlr_before == sctlr_after == 0x00c5487b`, so `mmu_unchanged = 1`.

## This is the meeting point

`stub_hit=early_random` is four statements past the previous frontier, and the four are the ones
this project has been building toward since experiment 134:

```
cpu_machine_idle_init(TRUE)          RETURNS - 20 calls, all of them now executed  (cpu.c:537)
if (arm_diag & 0x8000) ...           NOT TAKEN  (see below)
PE_init_platform(TRUE, &BootCpuData) RUNS AND COMPLETES                          (arm_init.c:379)
cpu_timebase_init(TRUE)              runs - 0 calls, real (osfmk_arm_cpu.o)
fiq_context_init(TRUE)               runs - 0 calls, real (machine_routines_asm.o)
__stack_chk_guard = early_random()   STUB  <-- the stop                          (arm_init.c:390)
machine_startup(args)                not reached
```

All six are plain statements of `arm_init` in sequence - verified in the linked image, not just in
the source - so control reaching the sixth is proof that the first five ran. And the third of them
is `PE_init_platform(TRUE, ...)`, whose `else` branch is

```c
	} else {
		pe_arm_init_interrupts(args);
		pe_arm_init_debug(args);
	}
```

**`pe_arm_init_interrupts` - Phase 3's own function, its `map`/`dispatch` path, the thing the shim
exists to replace - has now been entered and returned by XNU itself, not called from the payload.**
That is a change in kind rather than in degree: every previous measurement of this function
(experiments 134, 143, 147) was the payload calling it because XNU was not running. It has a caller
inside XNU now.

### What the device proves, and what it does not

Proven by the run: control passed through `PE_init_platform(TRUE, ...)`, so `vm_initialized` was
taken as TRUE and both `pe_arm_init_interrupts(args)` and `pe_arm_init_debug(args)` were entered;
`pe_arm_init_interrupts` returned (nothing panicked - `panic` is real as of experiment 201 and would
have reported `stub_hit=PEHaltRestart`); and `pe_arm_init_timer(args)` was **not** called, because
`pe_arm_init_interrupts` returns 0 at `if (!pe_arm_map_interrupt_controller()) return 0;`.

**Not proven by the run: which reads inside those functions succeeded.** This kernel is built with
`CONFIG_NO_KPRINTF_STRINGS=1`, so every `kprintf` in them is `_consume_kprintf_args(0, ...)` and
writes nothing, and neither function leaves an observable side effect in the log. That the
`DTFindEntry("name", "arm-io", ...)` lookup succeeded - it is the line that sets `gSocPhys` - and
that `DTFindEntry("interrupt-controller", "master", ...)` failed are on the *host-side* evidence:
`tools/host_dt_check.sh` runs XNU's own tree walker over this project's tree and reports `ok
DTFindEntry("name", arm-io)` and `absent* DTFindEntry("interrupt-controller", "master")`. That is a
different kind of evidence from a device run and the two are not interchangeable, which is this
project's seventeenth measurement defect and the reason the distinction is written down here rather
than folded into one sentence (see experiment 206's correction).

### `arm_diag` was zero, measured

`if (arm_diag & 0x8000) set_mmu_control((get_mmu_control()) ^ SCTLR_PREDIC);` sits between
`cpu_machine_idle_init` and `PE_init_platform`. If it had been taken, SCTLR would differ either side
of the jump. It does not: `sctlr_before == sctlr_after == 0x00c5487b`. So the run measured that
`arm_diag == 0` - which the source says it should be, since `arm_diag` is assigned only from the
`diag` boot argument (`arm_init.c:278`) and `boot_args.c`'s command line has none.

## The step cost eleven symbols and added none

`osfmk_arm_caches.o` (`osfmk/arm/caches.c`) - **2744 bytes of text, no data, no `.bss`, 17
definitions, 26 references**:

```
resolved (11): cache_sync_page cache_xcall_handler clean_dcache flush_dcache
               platform_cache_batch_wimg platform_cache_disable platform_cache_flush_wimg
               platform_cache_idle_enter platform_cache_idle_exit platform_cache_init
               platform_cache_shutdown
added   (0):   -
371 -> 360 undefined
```

Both directions link, taken by standing an empty object in for `osfmk_arm_caches.o`.

Zero added, and for a stronger reason than the last two experiments: every one of the object's 26
references is already a real definition in this image - the cache-maintenance functions experiment
209 linked, the pmap and machine-routines symbols earlier steps paid for, and `kvtophys`,
`flush_core_tlb`, `cpu_signal`, `cache_info`, `up_style_idle_exit`, which the assembly layer brings.
This is the first step in the sequence that needs nothing at all from the generator.

`clean_dcache` ran as the `phys == FALSE` leg with `cpu_cache_dispatch` still
`(cache_dispatch_t) NULL` - `cpu_data_init` zeroed `BootCpuData` in experiment 168 and nothing has
set that field - so it was one `CleanPoC_DcacheRegion` over the `cpu_data_t` that `getCpuDatap()`
returns from `TPIDRPRW`, and it reached neither `kvtophys` nor the dispatch callback.

## Cost

| | exp-209 | now |
| --- | --- | --- |
| entry objects linked | 49 | 50 (`osfmk/arm/caches.o`) |
| entry text | 275696 B | 278064 B |
| entry image | 371008 B | 371008 B |
| entry `.bss` | 0x0025a4f8–0x002731c8 (101584 B) | unchanged |
| undefined | 371 | 360 |
| stubs | 311 functions, 60 storage | 300 functions, 60 storage |
| boot_args offset | +479232 | +479232 |
| headroom below `topOfKernelData` | 1625656 B | 1625656 B |
| payload text | 862830 B | 862830 B |

The image size did not move for the fifth experiment running - `.text` still ends inside the
alignment gap before `.data` at `0x23C000` - and the payload did not change either.

## What is next: `osfmk/prng/random.c`, and the prediction is a panic

The frontier is `early_random`, which is `osfmk/prng/random.c:318` → `osfmk_prng_random.o`: **2488
bytes of text, 488 of data, 20 of `.bss`, 26 references, 17 definitions** - `entropy_buffer_read`,
`entropy_readall`, `early_random`, `read_erandom`, `read_frandom`, `read_random`, `write_random`,
`prng_cpu_init`, `prng_factory_register`, `EntropyData` and `erandom` among them.

`EntropyData` and `erandom` are *currently generated storage stubs* in this image, and unlike most
of the stand-ins this project has retired, the values matter: `entropy_data_t EntropyData = {
.index_ptr = EntropyData.buffer }` is an initialized struct, and `erandom.seedset` is the flag
`early_random` branches on.

That branch is the whole prediction. The object's disassembly, read rather than the source's order,
says:

```
 130:	ldr	r0, [r4, #16]      ; erandom.seedset
 134:	cmp	r0, #0
 138:	beq	14c                ; <- TAKEN: seedset is 0 in BSS
 ...
 150:	mov	r1, #64            ; sizeof(EntropyData.buffer)
 164:	bl	PE_get_random_seed
 168:	cmp	r0, #63
 16c:	bhi	184                ; <- NOT TAKEN
 180:	bl	panic              ; panic("EntropyData needed %lu bytes, but got %u.\n", 64, r0)
```

and `PE_get_random_seed` (`pexpert/gen/pe_gen.c:119`, real in this image since experiment 173) is:

```c
	if ((DTLookupEntry(NULL, "/chosen", &entryP) == kSuccess)
	    && (DTGetProperty(entryP, "random-seed", (void **)&dt_random_seed, &size) == kSuccess)) {
		...
	}
	return(size);
```

**This project's device tree has no `random-seed` property under `/chosen`** - nothing in
`stage90_main.c`, `apple_dt.c` or the tools writes one (`grep -rn random-seed stages/stage90/ tools/`
is empty). So the `&&` fails, `size` stays 0, and `early_random` calls
`panic("EntropyData needed 64 bytes, but got 0.\n")`.

`panic` is real (experiment 201) and is a tail call to `panic_trap_to_debugger`, whose path is all
real until `PEHaltRestart(kPEPanicBegin)` in `iokit_Kernel_PlatformExpert.o` - a stub. So:

**The prediction is `stub_hit=PEHaltRestart`**, meaning: XNU panicked, and the panic is
"EntropyData needed 64 bytes, but got 0". Experiment 201's doc warned about exactly this confusion -
`PEHaltRestart` does not say a panic happened, so the *name* alone would be uninformative; it is the
prediction written down beforehand that makes this run's result readable.

### The finding, and where the fix belongs

This is not a defect in the frontier method and not really a missing symbol. **It is a gap in the
simulated handoff contract**: on real hardware iBoot supplies entropy in `/chosen`'s `random-seed`,
and XNU treats its absence as fatal - "Insufficient entropy is fatal. We must fill the entire entropy
buffer during initialization." `PE_get_random_seed` returns 0 both when the property is missing and
when it is all zero bytes, and it *nulls the property in the tree* after copying it so it cannot be
read twice.

That makes this the third gap of the same kind this project has found in its own device tree, after
`state` on the cpu nodes (experiment 193) and `device_type = "timer"` (found by
`tools/host_dt_check.sh`): a node or property XNU reads and this tree was not written to provide.
The difference is that this one is load-bearing — `early_random` feeds `__stack_chk_guard`, which
`arm_init` sets immediately after, so nothing past this point runs until a seed exists.

The fix belongs in `stages/stage90/stage90_main.c` with the other handoff-contract properties, not
in the frontier's object sequence, and the honest form of it is a *simulated* iBoot seed: 64 bytes,
non-zero, derived from something recorded in the log so the run's behaviour is reproducible. That is
the next experiment's work, and it is work of a different shape from the last fifty - a contract
property rather than an object. What the run immediately before it costs (`osfmk_prng_random.o`) is
a separate question: it drags in the CCPRNG stack (`ccdrbg_factory_nisthmac`, `ccsha1_eay_di`,
`cc_clear`) and lock and wait primitives, and by this project's own rule the object is linked because
the previous run named the symbol it defines, whatever the object's other contents.

## Reproduce

```bash
# the step: 11 resolved, 0 added, 371 -> 360
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CACHES_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 11 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=early_random

# the four statements that ran, as plain statements of arm_init
sed -n '371,392p' external/xnu-4570.1.46/osfmk/arm/arm_init.c
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | sed -n '/<arm_init>:/,/^$/p' \
  | grep -oE "bl\s+[0-9a-f]+ <(cpu_machine_idle_init|PE_init_platform|cpu_timebase_init|fiq_context_init|early_random|machine_startup)>"

# that arm_diag was zero: the SCTLR check either side of the jump
grep -E "stage90_xnu_entry_stub_sctlr_(before|after)=" /tmp/cancro-last_kmsg.txt
grep -n "arm_diag" external/xnu-4570.1.46/osfmk/arm/arm_init.c

# Phase 3's function, now entered by XNU
sed -n '/^PE_init_platform(/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_init.c | sed -n '24,30p'
sed -n '/^pe_arm_init_interrupts/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
sed -n '/^pe_arm_map_interrupt_controller/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c | sed -n '1,12p'
./tools/host_dt_check.sh | grep -E 'arm-io|interrupt-controller'

# the next frontier, and the panic it leads to
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_prng_random.o | sed -n '/<early_random>:/,/^$/p' | sed -n '1,35p'
sed -n '/^PE_get_random_seed/,/^}/p' external/xnu-4570.1.46/pexpert/gen/pe_gen.c
sed -n '/^early_random/,/^	if (!erandom.seedset)/p' external/xnu-4570.1.46/osfmk/prng/random.c
grep -rn "random-seed" stages/stage90/ tools/ ; echo "(empty: the tree has no seed)"
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_debug.o \
  | sed -n '/<panic_trap_to_debugger>:/,/^$/p' | grep -E "R_ARM_CALL|R_ARM_JUMP24" | head -5
grep -qx PEHaltRestart out/stage90/xnu_arm_entry_undef.txt && echo "PEHaltRestart is a stub"
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Correction, added after the run (experiment 211)

Two claims in the "What is next" section above are wrong, and experiment 211's run is what settled
them. Neither changes the result - `stub_hit=early_random` was the right stop - but both would have
misled the step after it, so they are corrected here rather than left standing.

**1. "`EntropyData` and `erandom` are *currently generated storage stubs* in this image."** Neither
was. `EntropyData` was the hand-written `uint8_t EntropyData[68]` at `entry_stubs.c:140` - which is
why retiring it did not move the generator's storage count - and `erandom` was not a stand-in of any
kind: `grep -c '^erandom$'` against experiment 211's A-side undefined list is 0, because nothing in
the image referenced it while `early_random` was a stub. The real thing is `entropy_data_t
EntropyData = { .index_ptr = EntropyData.buffer }`, initialized, which is what makes
`osfmk_prng_random.o`'s `.data` 488 bytes.

**2. "The prediction is `stub_hit=PEHaltRestart`."** There is no stub anywhere on the panic path, so
no stub could have been hit. `PEHaltRestart` sits behind `CPUDEBUGGERCOUNT > NESTEDDEBUGGERENTRYMAX`
and the count is `db_entry_count` - zeroed `.bss`, so 1 after the increment. The other candidates are
skipped for reasons measurable in the image (`PE_arm_debug_panic_hook`, `write_trace_on_panic`,
`kdebug_enable` are all `B`, so NULL and 0; `panic` passes `ctx = NULL`). What remains is
`TRAP_DEBUGGER` - an ARM `udf` - and then `panic_stop()`, which on ARM is `panic_spin_forever()`
(`debug.c:136`; the `pmCPUHalt` form is x86-only), real, ending in `for (;;) { }`. The `udf` would
have gone through the payload's VBAR-installed vectors into `stage90_undef_c_handler` and returned,
so the run would have produced an `undef` breadcrumb from inside XNU and then hung, with no
`stub_hit` line at all.

That is why experiment 211 added the `/chosen` `random-seed` property in the same step that linked
the object: aiming a device run at a branch whose stated purpose is to stop the machine is not a
measurement. See experiment 211's doc for the full disassembly.
