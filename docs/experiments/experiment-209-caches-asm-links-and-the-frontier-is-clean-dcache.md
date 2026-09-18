# Experiment 209 — the Cache-Maintenance Surface Links, `cpu_machine_idle_init` Reaches Its Last Call, and the Frontier Is `clean_dcache`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=clean_dcache

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `no_exception=0x00000001`, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001`, `persistent_write_attempted=0x00000000` in all 25 contracts that report
it, and the device returned to Android on its own.

`clean_dcache` is the prediction, and it is the **last call of `cpu_machine_idle_init`** - 20th of
20. So everything between experiment 208's frontier and the end of that function ran:

| # | call | what it did |
| --- | --- | --- |
| 17 | `CleanPoC_DcacheRegion(phystokv(gPhysBase), PAGE_SIZE)` | 128 `mcr p15, 0, r0, c7, c10, 1`, one per 32-byte line, over VA `0x00200000`..`0x00201000` |
| 18 | `ml_static_vtop(&resume_idle_cpu)` | real, and `resume_idle_cpu` is at VA `0x00200000` |
| 19 | `bcopy(running_signature, IOS_STATE, 8)` | wrote `XSOMNNUR` to `LowExceptionVectorsAddr + 0x80` |
| 20 | `clean_dcache(cpu_data_ptr, sizeof(cpu_data_t), FALSE)` | **stop** |

Call 19 is the one worth naming. `IOS_STATE` is `LowExceptionVectorsAddr + 0x80` (`cpu.c:76`) - the
byte range the vector copy deliberately steps over (`0x90` then `+0xA0`) - and `running_signature` is
`{'X','S','O','M','N','N','U','R'}` (`cpu.c:79`). **XNU has now written its running mark into the
low-vectors page**, in the slot it keeps for exactly this: it is the word the sleep/wake machinery
reads to decide whether the OS was running or suspended. It is RAM, not persistent storage - the
page is the one experiment 207 mapped at VA `0x40000000` → PA `0x00200000` - so it says nothing about
the device's flash and lasts only until the next boot.

## `CleanPoC_DcacheRegion` ran, and it is bounded

The prediction was made from the source because the alternative shape is a known defect here. It is
a fixed-trip-count loop:

```asm
	and		r2, r0, #((1<<MMU_CLINE)-1)
	bic		r0, r0, #((1<<MMU_CLINE)-1)
	add		r1, r1, r2
	sub		r1, r1, #1
	mov		r1, r1, LSR #MMU_CLINE				// Set cache line counter
ccdr_loop:
	mcr		p15, 0, r0, c7, c10, 1				// Clean dcache line to PoC
	add		r0, r0, #1<<MMU_CLINE
	subs	r1, r1, #1
	bpl		ccdr_loop
```

The count comes from the `length` argument, never from `CTR`/`CCSIDR` - which is the difference
between it and experiment 196's whole-cache flush, whose geometry the payload read out of the L2 and
got wrong twice. With `CACHE_MODE = NONE` the caches are off and the `mcr`s are no-ops, so what the
run establishes is that the loop terminates and returns, not that a clean happened.

## The step cost eleven symbols and added none

`out/xnu_asm_obj/caches_asm.o` (`osfmk/arm/caches_asm.s`) - **596 bytes of text**, assembled by the
same clang pipeline as `machine_routines_asm.o`, and nineteen globals:

```
resolved (7):  CleanPoC_Dcache CleanPoC_DcacheRegion CleanPoC_DcacheRegion_Force
               CleanPoU_Dcache CleanPoU_DcacheRegion flush_dcache64 InvalidatePoU_Icache
added   (0):   -
378 -> 371 undefined
```

Both directions link, taken by standing an empty object in for `out/xnu_asm_obj/caches_asm.o`.

The accounting said 7 resolved because the other twelve of the file's nineteen globals are not
referenced by anything in this image and had never become stubs - the generator emits a stub for a
name the link is *missing*, not for every name in the tree. Zero added, for the same reason as
experiments 207 and 208: the object's ten references are eight already-real symbols
(`EntropyData ExceptionVectorsBase fiqstack_top gPhysBase gPhysSize gVirtBase intstack_top
kdebug_enable`) plus `clean_dcache` and `flush_dcache`, which are already undefined names here.

## Cost

| | exp-208 | now |
| --- | --- | --- |
| entry objects linked | 48 | 49 (`osfmk/arm/caches_asm.s`) |
| entry text | 275344 B | 275696 B |
| entry image | 371008 B | 371008 B |
| entry `.bss` | 0x0025a4f8–0x002731c8 (101584 B) | unchanged |
| undefined | 378 | 371 |
| stubs | 318 functions, 60 storage | 311 functions, 60 storage |
| boot_args offset | +479232 | +479232 |
| headroom below `topOfKernelData` | 1625656 B | 1625656 B |
| payload text | 862830 B | 862830 B |

352 bytes of text for seven functions. The image size did not move for the fourth experiment running,
because `.text` still ends inside the alignment gap before `.data` at `0x23C000` - `image bytes` is a
size, not a checksum.

## What is next: `osfmk/arm/caches.o`, and then a prediction four calls out

The frontier is `clean_dcache`, which is `osfmk/arm/caches.c` → `osfmk_arm_caches.o`: **2744 bytes of
text, no data, no `.bss`, 26 references, 17 definitions** - `cache_sync_page`, `cache_xcall`,
`cache_xcall_handler`, `clean_dcache`, `flush_dcache`, `flush_dcache_syscall`,
`dcache_incoherent_io_flush64`, `dcache_incoherent_io_store64`, `platform_cache_batch_wimg`,
`platform_cache_clean`, `platform_cache_disable`, `platform_cache_flush`, `platform_cache_flush_wimg`,
`platform_cache_idle_enter`, `platform_cache_idle_exit`, `platform_cache_init`,
`platform_cache_shutdown`.

```
to resolve (11):  cache_sync_page cache_xcall_handler clean_dcache flush_dcache
                  platform_cache_batch_wimg platform_cache_disable platform_cache_flush_wimg
                  platform_cache_idle_enter platform_cache_idle_exit platform_cache_init
                  platform_cache_shutdown
to add     (0):   -
371 -> 360 undefined
```

Every one of its 26 references is already a real definition in this image - `cache_info`,
`CpuDataEntries`, `cpu_number`, `cpu_signal`, `flush_core_tlb`, `kvtophys`, `ml_get_timebase`,
`ml_set_interrupts_enabled`, `pmap_cache_attributes`, the three `pmap_*_cpu_windows_copy*`,
`real_ncpus`, `up_style_idle_exit`, the `g*` globals - or one of the seven cache-maintenance functions
experiment 209 just made real. So this is a free step too: 11 resolved, 0 added.

`clean_dcache`'s body, which is what will run, is the `phys == FALSE` leg with
`cpu_cache_dispatch` still `(cache_dispatch_t) NULL` - `cpu_data_init` (experiment 168) zeroes
`BootCpuData` and nothing has set that field - so it is one call to `CleanPoC_DcacheRegion(addr,
length)`, which experiment 209 linked, over the `cpu_data_t` that `getCpuDatap()` returns from
`TPIDRPRW`. No branch of it reaches `kvtophys` or the dispatch callback.

**The prediction is `early_random`, not `clean_dcache`.** With that object linked, the remaining path
is:

```
cpu_machine_idle_init returns                        (cpu.c:537)
if (arm_diag & 0x8000) get_mmu_control/set_mmu_control    NOT TAKEN - `arm_diag` is only set from
                                                     the `diag` boot arg (arm_init.c:278), and
                                                     boot_args.c's command line has none
PE_init_platform(TRUE, &BootCpuData)                 real, and it now completes
cpu_timebase_init(TRUE)                              0 calls, real (osfmk_arm_cpu.o)
fiq_context_init(TRUE)                               0 calls, real (machine_routines_asm.o)
__stack_chk_guard = (unsigned long)early_random();   STUB  <-- the stop
machine_startup(args)                                real
```

All four of the calls before it are real, and `PE_init_platform(TRUE, ...)` needs nothing new. That
is the important part: **its `else` branch is `pe_arm_init_interrupts(args); pe_arm_init_debug(args);`
and both bodies complete on this tree without a single new symbol.**

- `pe_arm_init_interrupts(args)` with `args = &BootCpuData` calls `pe_arm_map_interrupt_controller()`,
  which does `kprintf` (a no-op through `_consume_kprintf_args`), `gSocPhys =
  pe_arm_get_soc_base_phys()` - which *does* find `arm-io` and read its `ranges` - then
  `DTFindEntry("interrupt-controller", "master", ...)`, which **fails on purpose**, and returns 0 at
  its `gPicBase == 0` check before ever reaching `ml_io_map`. So `pe_arm_init_timer(args)` is not
  called either.
- `pe_arm_init_debug(args)` returns at `DTFindEntry("device_type", "cpu-debug-interface", ...)`,
  which this tree also does not have.

So the next run should be the one where **Phase 3's function is entered for the first time** - by XNU
itself, not by the payload - and where the thing that stops the boot is no longer in it. That is the
meeting point experiment 206 said was ahead and experiment 207 confirmed had not moved. It is worth
being precise about what it will and will not prove: it will prove XNU calls
`pe_arm_init_interrupts`, executes its prologue, reads `arm-io` from this project's tree, and returns
0 because the interrupt controller is deliberately unfindable. It will **not** prove the mapping path
works, because the mapping path is exactly what the missing node is holding back.

## Reproduce

```bash
# the step: 7 resolved, 0 added, 378 -> 371
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CACHES_ASM_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 7 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=clean_dcache

# the running mark, and the page it goes into
grep -n "IOS_STATE\|running_signature" external/xnu-4570.1.46/osfmk/arm/cpu.c | head -4
sed -n '556,600p' external/xnu-4570.1.46/osfmk/arm/cpu.c

# the loop that just ran, and why it is bounded
sed -n '210,232p' external/xnu-4570.1.46/osfmk/arm/caches_asm.s

# the next frontier, and the four calls that now stand between it and Phase 3
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_cpu.o \
  | sed -n '/<cpu_machine_idle_init>:/,/^$/p' | grep -E "R_ARM_CALL|R_ARM_JUMP24" | nl | tail -3
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | sed -n '/<arm_init>:/,/^$/p' \
  | grep -oE "bl\s+[0-9a-f]+ <(cpu_machine_idle_init|PE_init_platform|cpu_timebase_init|fiq_context_init|early_random|machine_startup)>"
grep -qx early_random out/stage90/xnu_arm_entry_undef.txt && echo "early_random is a stub"

# and that Phase 3's function needs nothing new to run
sed -n '/^PE_init_platform(/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_init.c | sed -n '24,30p'
sed -n '/^pe_arm_init_interrupts/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
./tools/host_dt_check.sh | grep -A1 'interrupt-controller'
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
