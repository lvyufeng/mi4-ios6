# Experiment 208 — `bcopy_phys` Runs, the Secondary-CPU Handshake Is Written Into the Low-Vectors Page, and the Frontier Is `CleanPoC_DcacheRegion`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000020
 xnu_entry_kv_in_dram=0x00000020
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=CleanPoC_DcacheRegion

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `no_exception=0x00000001`, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001`, `persistent_write_attempted=0x00000000` in all 25 contracts that report
it, and the device returned to Android on its own.

**The prediction was `CleanPoC_DcacheRegion` and the stop is `CleanPoC_DcacheRegion`** - four calls
past the previous frontier, which is the useful part. Between `bcopy_phys` and this stop, three
things ran:

- the **second** `bcopy_phys`, for `CpuDataEntries_paddr`;
- an `ml_static_vtop` for `resume_idle_cpu`;
- and, before all of them, the first `bcopy_phys`.

`bcopy_phys` is `osfmk/arm/loose_ends.c:58` and it is not a memcpy. It resolves the two pages
through `pmap_cache_attributes`, tests `mmu_kvtop_wpreflight(phystokv(dst))`, and then either takes a
plain `bcopy` through the identity mapping or - if the destination is not writable or either page's
attributes are not `VM_WIMG_DEFAULT` - goes through the **CPU copy windows**
(`pmap_map_cpu_windows_copy` / `pmap_cpu_windows_copy_addr` / `pmap_unmap_cpu_windows_copy`), with a
`panic("bcopy extends beyond copy windows")` guarding a copy that crosses a page. It has a `panic`,
so a bad page number would have reported `stub_hit=PEHaltRestart`; instead the run continued.

**Which of the two paths it took is not in the log, and this doc will not claim one.** The fast path
needs `mmu_kvtop_wpreflight` to answer yes for `phystokv(dst)` and both pages' `pp_attr_table` entries
to read `VM_WIMG_DEFAULT` (which is 0, and `pmap_bootstrap`'s `memset` zeroed that table - so the
attribute half of the test is true by construction, and the answer turns on the writability test).
The cheap way to settle it is one `entry_write_kv` of `mmu_kvtop_wprefill`'s result and of the two
`wimg_bits_*` words in a future run; it is recorded here rather than guessed because "which path"
decides whether the *copy-window* machinery has now been exercised on this device, and that is a
different claim about XNU than "a memcpy worked".

## What the writes were for, and where they landed

The two `bcopy_phys` calls put `BootArgs_paddr` and `CpuDataEntries_paddr` into the low-vectors page
experiment 207 created, at

```c
	(addr64_t)(gPhysBase + ((unsigned int)&(ResetHandlerData.boot_args) - (unsigned int)&ExceptionLowVectorsBase))
```

and the same expression `+8` for `cpu_data_entries`. `ResetHandlerData` is `locore.s`'s block at the
head of the vector page that a **secondary CPU** reads when it is released: it is how a core brought
up later finds the kernel's `boot_args` and its per-CPU data array. So as of this run, the low-vectors
page at PA `0x00200000` contains the exception vectors XNU copied in experiment 207 *and* a valid
reset-handler handshake.

That is worth stating plainly because experiment 207's doc recorded the cost side of the same write:
the page it lands on is `_start`, `arm_init_tramp`, `start_cpu` and `resume_idle_cpu`, and the copy
overwrites them while `cpu_machine_idle_init` goes on to store `start_cpu`'s and `resume_idle_cpu`'s
*addresses* into `cpu_data_ptr->cpu_reset_handler`. On this single-core bring-up nothing jumps there.
A future experiment that releases a second core would find the vector page well-formed and the code
those two addresses point at gone.

## The step cost eight symbols, seven of them for free

`osfmk_arm_loose_ends.o` - **3671 bytes of text, no data, no `.bss`**:

```
resolved (7):  bcopy_phys bzero_phys copyin_validate copyout_validate ffs setbit testbit
added   (1):   flush_dcache64
384 -> 378 undefined
```

Both directions link, taken by standing an empty object in for `osfmk_arm_loose_ends.o`.

`flush_dcache64` is the only new obligation, and it is not on the boot path: the object references it
twice, both inside `copypv`, and nothing in this image calls `copypv`. It is `osfmk/arm/caches_asm.s`
(`out/xnu_asm_obj/caches_asm.o`), which is not linked - which is also why the six other functions of
that file that this image already treats as missing stayed missing.

## Cost

| | exp-207 | now |
| --- | --- | --- |
| entry objects linked | 47 | 48 (`osfmk/arm/loose_ends.o`) |
| entry text | 271792 B | 275344 B |
| entry image | 371008 B | 371008 B |
| entry `.bss` | 0x0025a4f8–0x002731c8 (101584 B) | unchanged |
| undefined | 384 | 378 |
| stubs | 324 functions, 60 storage | 318 functions, 60 storage |
| boot_args offset | +479232 | +479232 |
| headroom below `topOfKernelData` | 1625656 B | 1625656 B |
| payload text | 862830 B | 862830 B |

3552 bytes of text for seven functions, none of which the image was going to do without: `ffs`,
`setbit`, `testbit`, `bcopy_phys`, `bzero_phys` and the two `copy*_validate` wrappers all have
callers elsewhere in XNU, and the step that named `bcopy_phys` was always going to pay for the file.

## What is next: `osfmk/arm/caches_asm.s`, and the last call of `cpu_machine_idle_init`

`CleanPoC_DcacheRegion` is `osfmk/arm/caches_asm.s` - **596 bytes of text**, and it defines nineteen
globals: `CleanPoC_Dcache`, `CleanPoC_DcacheRegion` (aliased with `CleanPoC_DcacheRegion_Force`),
`CleanPoU_Dcache`, `CleanPoU_DcacheRegion`, `FlushPoC_Dcache`, `FlushPoC_DcacheRegion`,
`FlushPoU_Dcache`, `clean_dcache64`, `clean_mmu_dcache`, `flush_dcache64`, `invalidate_icache`,
`invalidate_icache64`, `invalidate_mmu_cache`, `invalidate_mmu_dcache`,
`invalidate_mmu_dcache_region`, `invalidate_mmu_icache`, `InvalidatePoU_Icache`,
`InvalidatePoU_IcacheRegion`.

```
to resolve (7):  CleanPoC_Dcache CleanPoC_DcacheRegion CleanPoC_DcacheRegion_Force CleanPoU_Dcache
                 CleanPoU_DcacheRegion flush_dcache64 InvalidatePoU_Icache
to add     (0):  -
378 -> 371 undefined
```

Zero added: the object's ten references resolve to `EntropyData`, `ExceptionVectorsBase`,
`fiqstack_top`, `gPhysBase`, `gPhysSize`, `gVirtBase`, `intstack_top` and `kdebug_enable`, all of
which the image already has (data.s and the asm layer), plus `clean_dcache` and `flush_dcache`, which
are *already* undefined names here and therefore already stubs. Adding a reference to a name the
image already treats as missing is free - the same accounting as experiment 207.

`CleanPoC_DcacheRegion` itself is worth having read before it runs, because it is the one function in
this step that will execute on the device. It is a **bounded** loop and not a geometry-driven one:

```asm
	and		r2, r0, #((1<<MMU_CLINE)-1)
	bic		r0, r0, #((1<<MMU_CLINE)-1)			// Cached aligned
	add		r1, r1, r2
	sub		r1, r1, #1
	mov		r1, r1, LSR #MMU_CLINE				// Set cache line counter
ccdr_loop:
	mcr		p15, 0, r0, c7, c10, 1				// Clean dcache line to PoC
	...
```

The trip count comes from the `length` argument, not from `CTR`/`CCSIDR`, so with `CACHE_MODE = NONE`
(the caches off, as in every stage so far) it is 128 no-op `mcr`s and not a loop whose count depends
on a register this project has already measured as unreliable - which is the shape of experiment 196's
defect, and the reason to check. `CleanPoC_DcacheRegion((vm_offset_t)phystokv((char *)gPhysBase),
PAGE_SIZE)` is `ml_static_vtop`-free here: `phystokv(0x200000)` is `0x200000` because
`gVirtBase == gPhysBase`, so the region cleaned is the low-vectors page the two `bcopy_phys` calls
just wrote.

**The prediction is `clean_dcache`.** With `CleanPoC_DcacheRegion` real, `cpu_machine_idle_init`
continues to call 18 (`ml_static_vtop`, real), call 19 - `bcopy(running_signature, IOS_STATE, 8)`,
real `bcopy`, writing into the new mapping at `LowExceptionVectorsAddr + 0x80` - and stops at call
**20, `clean_dcache`**, which is the *last* call of `cpu_machine_idle_init`

```c
	if (cpu_data_ptr == &BootCpuData) {
		bcopy(((const void *)running_signature), (void *)(IOS_STATE), IOS_STATE_SIZE);
	};
	cpu_data_ptr->cpu_reset_handler = resume_idle_cpu_paddr;
	clean_dcache((vm_offset_t)cpu_data_ptr, sizeof(cpu_data_t), FALSE);
}
```

`clean_dcache` is `osfmk/arm/caches.c` (`osfmk_arm_caches.o`), which is in the link closure but is
**not linked** - the two are different things and experiment 207's build comment blurred them. So
this step should be the one that takes `cpu_machine_idle_init` to its final call, and the step after
it, if `clean_dcache` is the only thing left, is the one where `cpu_machine_idle_init` **returns** and
`arm_init` reaches `get_mmu_control`/`set_mmu_control` and then
`PE_init_platform(TRUE, &BootCpuData)` - Phase 3's meeting point, which has been two calls ahead
since experiment 206 and has not moved.

## Reproduce

```bash
# the step: 7 resolved, 1 added, 384 -> 378
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_LOOSE_ENDS_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 7 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 added    (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=CleanPoC_DcacheRegion

# what the two bcopy_phys calls were for, and the panic that guards them
sed -n '58,95p' external/xnu-4570.1.46/osfmk/arm/loose_ends.c
sed -n '568,580p' external/xnu-4570.1.46/osfmk/arm/cpu.c

# the next object, and the function in it that will actually execute
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_loose_ends.o
arm-none-eabi-size out/xnu_asm_obj/caches_asm.o
arm-none-eabi-nm --defined-only out/xnu_asm_obj/caches_asm.o
sed -n '210,232p' external/xnu-4570.1.46/osfmk/arm/caches_asm.s

# and the last call of cpu_machine_idle_init, which is the next frontier
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_cpu.o \
  | sed -n '/<cpu_machine_idle_init>:/,/^$/p' | grep -E "R_ARM_CALL|R_ARM_JUMP24" | nl | tail -4
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
