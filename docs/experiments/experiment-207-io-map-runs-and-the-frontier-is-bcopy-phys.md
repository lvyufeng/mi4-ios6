# Experiment 207 — `io_map` Runs, XNU Creates a Kernel Mapping for Itself, and the Frontier Is `bcopy_phys`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000015
 xnu_entry_kv_in_dram=0x00000015
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=bcopy_phys

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`,
`stage90_xnu_entry_stub_safety_boundary_preserved=0x00000001`,
`stage90_xnu_entry_stub_mmu_unchanged=0x00000001`
(`ttbr0_before == ttbr0_after == 0x00114000`, `sctlr_before == sctlr_after == 0x00c5487b`),
`stage90_xnu_entry_stub_failure_mask=0x00000000` against `required_mask=0x0000ffff`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device
returned to Android on its own.

**The prediction was `bcopy_phys` and the stop is `bcopy_phys`.** It was made by reading the call
order out of `osfmk_arm_cpu.o` rather than out of `cpu.c`, which is the habit experiment 206's
correction bought: the statements after `ml_io_map` are two `bcopy`s into the new mapping (real),
three `ml_static_vtop` calls (real), and then `bcopy_phys` - `osfmk/arm/loose_ends.c`, which is not
linked. `CleanPoC_DcacheRegion` and the `bcopy(running_signature, IOS_STATE, 8)` come after it.

## What this run did that no earlier run has

The stub is `bcopy_phys`, one call short of the end of `cpu_machine_idle_init`, and reaching it means
several things happened on the device for the first time:

1. **`io_map` took its `kernel_map == VM_MAP_NULL` branch** - which is the correct branch here,
   because `kernel_map` is still the generator's 4-byte zero stand-in and XNU creates the real map in
   `kmem_init`, long after `arm_init`. So:
2. **`virtual_space_start` was read and advanced.** It is `0x40000000`, from
   `pmap_bootstrap((gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000)` (`arm_vm_init.c:505`). That
   expression is also the VA `arm_vm_init.c:517` builds the page tables at, so the value is not
   merely arithmetic - the 1280-page loop of experiment 197 already covered it.
3. **`pmap_map_bd` found a real PT entry and wrote a PTE.** `pmap_pte(kernel_pmap, 0x40000000)` did
   not return `PT_ENTRY_NULL`, so the `panic("pmap_map_bd")` legs were not taken and a new
   kernel-virtual → physical mapping exists: **VA `0x40000000` → PA `0x00200000`**, Strongly-ordered
   with `AP_RWNA`, flags 7 rather than 6. This is the first mapping this kernel has created for
   itself; every mapping before it came from the payload or from `arm_vm_init`.
4. **XNU copied its low exception vectors into that mapping** - `0x90` bytes then `ARM_PGBYTES-0xA0`
   - which means the write landed on **PA `0x00200000`, this image's own first page**. That is XNU's
   design and not an accident: `gVirtBase == gPhysBase == 0x00200000` here, so
   `ml_io_map(ml_vtophys(gPhysBase), PAGE_SIZE)` maps the kernel's base page, and in a real ARM
   kernel that page *is* the low-vectors page.

The last point is worth being explicit about because the page it lands on is not empty in this
project. VA `0x200000`..`0x201000` holds `_start` (`0x200074`), `arm_init_tramp` (`0x20037c`),
`start_cpu` (`0x200008`) and `resume_idle_cpu` (`0x200000`) - the `locore` code `xnu_arm_start.o`
brings in. `_start` and `arm_init_tramp` have already run and nothing returns to them. `start_cpu`
and `resume_idle_cpu` are the *secondary-CPU* entry points: their **addresses** are about to be
stored into `cpu_data_ptr->cpu_reset_handler` and the reset-handler block, while the code they point
at is now exception vectors. On a single-core bring-up nothing jumps to them, so this costs nothing
today - but it is a real state change and the next experiment that brings up a second core would
have to know it, because the fix is not obvious from either the source or the log.

## The step cost two symbols and added none

`osfmk_arm_io_map.o` - **360 bytes of text, no data, no `.bss`, seven references, two definitions**:

```
resolved (2):  io_map io_map_spec
added   (0):   -
386 -> 384 undefined
```

Both directions link, taken by standing an empty object in for `osfmk_arm_io_map.o`.

Zero added, and the reason is that five of the object's seven references were already real for other
reasons - `panic` (exp-201), `pmap_map`, `pmap_map_bd`, `pmap_map_bd_with_options` (pmap.o, exp-197),
`virtual_space_start` (vm_resident.o, exp-195) - and the remaining two, `kernel_map` and
`kmem_alloc_pageable`, were *already undefined names in this image and therefore already stubs*.
Adding a reference to a name the image already treats as missing is free. `kmem_alloc_pageable` is
also in the branch that was not taken, so the one function stub this step could have needed was
never called.

## Cost

| | exp-206 | now |
| --- | --- | --- |
| entry objects linked | 46 | 47 (`osfmk/arm/io_map.o`) |
| entry text | 271504 B | 271792 B |
| entry image | 371008 B | 371008 B |
| entry `.bss` | 0x0025a4f8–0x002731c8 (101584 B) | unchanged |
| undefined | 386 | 384 |
| stubs | 326 functions, 60 storage | 324 functions, 60 storage |
| boot_args offset | +479232 | +479232 |
| headroom below `topOfKernelData` | 1625656 B | 1625656 B |
| payload text | 862830 B | 862830 B |

The image size did not move - `.text` still ends below the `.data` alignment - and the payload did
not change either, because no payload source changed. Both numbers are sizes, not checksums.

## What this means for Phase 3, one experiment later

Experiment 206's correction said the interrupt controller is still ahead. It still is, and its state
is now specifically observable:

- `PE_init_platform(TRUE, &BootCpuData)` is `arm_init`'s 37th call and has still not run - the
  frontier is `cpu_machine_idle_init`'s 17th call.
- When it does run, `pe_arm_init_interrupts(args)` calls `pe_arm_map_interrupt_controller()`, which
  finds `arm-io` (so `gSocPhys` is non-zero), then does
  `DTFindEntry("interrupt-controller", "master", ...)` - and **this project's tree deliberately has
  no such node** (`tools/host_dt_harness.c:236`, `stage90_main.c:781`). It returns 0 at the
  `gPicBase == 0` check, and `pe_arm_init_interrupts` returns 0 before `pe_arm_init_timer(args)`.
- So the *stock* mapping call that Phase 3 exists to replace is not reachable until the `reg` model
  is reconciled - and the decision recorded at `stage90_main.c:739-748` (the tree carries absolute
  addresses because the project's own iokit contract selftests read them; Apple's XNU wants offsets
  from the SoC base) is what stands in front of it.

This run also settles a related question the phase note could not have settled before: `io_map`
itself works on this device. Whatever Phase 3 does about the `reg` model, the mapping primitive it
would call is no longer a question mark.

## What is next: `osfmk/arm/loose_ends.o`, and a prediction five calls long

The frontier is `bcopy_phys`, and the object is `osfmk_arm_loose_ends.o` - **3671 bytes of text and
one function of it matters**, `bcopy_phys` at offset 0. It defines the rest of a grab bag:
`bzero_phys`, `ml_phys_read*` / `ml_phys_write*`, `ml_probe_read`, `ffs`/`fls`/`bcmp`/`memcmp`,
`setbit`/`clrbit`/`testbit`, `copypv`, `copyin_validate`/`copyout_validate`, `ml_thread_policy`.

```
to resolve (7):  bcopy_phys bzero_phys copyin_validate copyout_validate ffs setbit testbit
to add     (1):  flush_dcache64
```

`flush_dcache64` is the only reference the object does not already satisfy, and it is in
`osfmk/arm/caches_asm.s` (`out/xnu_asm_obj/caches_asm.o`), which is not linked. It is only
referenced from `copypv` - nothing on the boot path calls `copypv` - so the step adds a function
stub it will not call.

**The prediction is `CleanPoC_DcacheRegion`.** With `bcopy_phys` real, `cpu_machine_idle_init`
continues from call 13 to call 16 (`bcopy_phys` again, for `CpuDataEntries_paddr`) and stops at call
17, `CleanPoC_DcacheRegion((vm_offset_t)phystokv((char *)gPhysBase), PAGE_SIZE)` - `caches_asm.s`
too, and also not linked. The two calls after it are `ml_static_vtop` (real) and the
`bcopy(running_signature, IOS_STATE, 8)` that writes into the new mapping at
`LowExceptionVectorsAddr + 0x80`.

So this should be the run that finishes `cpu_machine_idle_init`'s exception-vector work and gets one
call away from the end of it. Worth noting for the one after: `clean_dcache` is the function's last
call, and it is `osfmk_arm_caches.o`, which is in the closure already - so the tail of this function
may cost one more cache object and then `arm_init` moves on to `PE_init_platform(TRUE, ...)`.

## Reproduce

```bash
# the step: 2 resolved, 0 added, 386 -> 384
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_IO_MAP_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 2 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=bcopy_phys

# the prediction was read off the object's call order, not the source's
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_cpu.o \
  | sed -n '/<cpu_machine_idle_init>:/,/^$/p' | grep -E "R_ARM_CALL|R_ARM_JUMP24" | nl

# what io_map was asked to map, and that the answer is a live VA
sed -n '505,520p' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E " (virtual_space_start|kernel_map)$"
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_io_map.o | sed -n '/<io_map>:/,/^$/p' | tail -25

# the page the vector copy lands on, and what was there
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | sort | awk 'strtonum("0x"$1) < 0x201000'

# the next object and its one new obligation
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_loose_ends.o
arm-none-eabi-nm -A --defined-only out/xnu_asm_obj/*.o | grep -E " flush_dcache64$"
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
