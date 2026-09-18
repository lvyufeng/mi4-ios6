# Experiment 206 — `arm_init`'s Tail Runs, XNU's Own Platform Interrupt Mapping Is Reached, and the Frontier Is `io_map`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000011
 xnu_entry_kv_in_dram=0x00000011
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=io_map

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own.

`io_map` is the prediction written down before the run, and the reason it is not `early_random` -
the next *stub* in `arm_init`'s source order - is most of the content of this experiment. Three
statements ran past the previous frontier:

```
	PE_create_console()          returns (its initialize_screen -> switch_to_serial_console branch)
	PE_init_printf(FALSE)        -> vcattach, a tail call, which ran
	cpu_machine_idle_init(TRUE)  -> cpu.c:562
	                                ml_io_map(ml_vtophys((vm_offset_t)gPhysBase), PAGE_SIZE)
	                                -> ml_io_map is a tail call to io_map. Stop.
```

**The symbol was predicted and the route was not.** The prediction written into experiment 205's doc
says the stop would be `io_map` reached through `PE_init_platform(TRUE, &BootCpuData)` ->
`pe_arm_init_interrupts` -> `pe_arm_map_interrupt_controller`. The stop is `io_map`, and it is six
statements earlier in the same function, in `cpu_machine_idle_init`. The correction is the rest of
this section, and it is worth the space because the wrong route was arrived at by reading source
order for the *interesting* caller instead of reading the object for the *reachable* one.

## Which caller, settled on the host

The argument is four measurements, and the first two alone close it.

1. **`io_map` has exactly one caller in this image.** `nm -u` over all 46 linked objects has one hit:
   `osfmk_arm_machine_routines.o`, whose `ml_io_map` is `mov r2, #7` then `b io_map` - a tail call.
   So the stop is one of `ml_io_map`'s three callers (also measured the same way):
   `osfmk_arm_cpu.o`, `pexpert_arm_pe_identify_machine.o`, `pexpert_arm_pe_serial.o`.
2. **A stub hit is terminal, and `cpu_machine_idle_init` precedes `PE_init_platform`.**
   `entry_stub_hit` writes the name and calls `entry_epilogue`, which never returns. The linked
   `arm_init` (`out/stage90/xnu_arm_entry.elf`) calls `PE_init_printf` (0x206268), then
   `cpu_machine_idle_init` (0x2036f0), then `PE_init_platform` (0x203e38) - and
   `cpu.c:562` is reached unconditionally inside that second call, because the two statements before
   it are a `jtag`/`wfi` boot-arg parse and neither name is on this project's command line
   (`boot_args.c:19`), so `wfi` defaults to 1 and both `if (wfi == …)` bodies are skipped. XNU
   therefore hit the `io_map` stub on the way *into* `cpu_machine_idle_init` and never reached
   `PE_init_platform`. Nothing after call 34 in that sequence ran.
3. `pexpert_arm_pe_serial.o` cannot be it: experiment 204 measured `serial_init` returning 0 through
   the `else` at `pe_serial.c:803`, before any `ml_io_map` - the three serial-node lookups all fail.
4. `pexpert_arm_pe_identify_machine.o` cannot be it either: its `ml_io_map` is inside
   `pe_arm_map_interrupt_controller`'s `if (DTFindEntry("interrupt-controller", "master", ...) ==
   kSuccess)` block, and **this project's tree has no such node**. `DTFindEntry` matches a property's
   *string value* (`device_tree.c:200`), the tree carries `interrupt-controller` as a `u32` of 1
   (`stage90_main.c:765`), and `tools/host_dt_harness.c:236` asserts the absence on purpose - with
   the reason written next to it: the interrupt controller is deliberately not findable until Phase 3
   resolves the `reg` model, because finding it today would map `0xf2000000`. `tools/host_dt_check.sh`
   prints it as `absent* DTFindEntry("interrupt-controller", "master")`. So
   `pe_arm_map_interrupt_controller` returns 0 at its `gPicBase == 0` check and `pe_arm_init_interrupts`
   returns 0 without calling `ml_io_map` at all. It was never reached anyway, by (2).

## So what the frontier actually is

`cpu_machine_idle_init` is `osfmk/arm/cpu.c:537`, and the statement it stops at is the one that maps
the kernel's own first physical page and copies the low exception vectors into it:

```c
	LowExceptionVectorsAddr = (void *)ml_io_map(ml_vtophys((vm_offset_t)gPhysBase), PAGE_SIZE);
	bcopy((void *)&ExceptionLowVectorsBase, (void *)LowExceptionVectorsAddr, 0x90);
	bcopy((...)+0xA0, (...)+0xA0, ARM_PGBYTES - 0xA0);
```

`gPhysBase` is this project's entry-image base, `0x00200000` (the payload logs
`xnu_entry_args_physBase`), and `virBase == physBase` by construction, so `ml_vtophys` is the
identity here and the call is `ml_io_map(0x00200000, 4096)`. `kernel_map` is still the generated
4-byte zero-filled stand-in, which is the *correct* value at this point in the boot - XNU creates
`kernel_map` in `kmem_init`, long after `arm_init` - so `io_map` takes its "VM is not initialized"
branch: carve `round_page(size)` off `virtual_space_start` (set by `pmap_bootstrap` in experiment
197) and `pmap_map_bd` it.

The Phase 3 meeting point is still ahead, two calls later - `PE_init_platform(TRUE, &BootCpuData)`
is call 37 and has not run yet. Linking `io_map` is what lets `cpu_machine_idle_init` finish and the
run get there.

## The object that does not stop

`osfmk_console_serial_general.o` - 824 bytes of text, 5 of `.bss`, 14 references:

```
resolved (8):  console_is_serial console_printbuf_clear console_printbuf_putc
               console_printbuf_state_init serial_keyboard_init serialmode
               switch_to_old_console switch_to_serial_console
added   (5):   _serial_getc cons_cinput cons_ops_index console_write nconsops
389 -> 386 undefined
```

Both directions link, taken by standing an empty object in for `osfmk_console_serial_general.o`.

`switch_to_serial_console` is three statements and calls nothing:

```c
	int old_cons_ops = cons_ops_index;
	cons_ops_index = SERIAL_CONS_OPS;
	return old_cons_ops;
```

So this step resolved the previous frontier and did not stop inside it - `initialize_screen`'s
no-video branch completed (`gc_graphics_boot = FALSE; disableConsoleOutput = FALSE;
gc_acquired = TRUE;`), the console index moved to serial, and the run continued into `arm_init`'s
tail. The one new obligation it needed is `cons_ops_index`: storage, 4 bytes,
`osfmk_console_serial_console.o`, sized by the generator from that object.

## Cost

| | exp-205 | now |
| --- | --- | --- |
| entry objects linked | 45 | 46 (`osfmk/console/serial_general.o`) |
| entry text | 270864 B | 271504 B |
| entry image | 371008 B | 371008 B |
| entry `.bss` | 101520 B | 101584 B |
| undefined | 389 | 386 |
| stubs | 330 functions, 59 storage | 326 functions, 60 storage |
| boot_args offset | +479232 | +479232 |
| headroom below `topOfKernelData` | 1625720 B | 1625656 B |
| payload text | 862830 B | 862830 B |

## What is next: `osfmk/arm/io_map.o`, and the first mapping this kernel creates for itself

The frontier is `io_map` and the object is `osfmk_arm_io_map.o`: **360 bytes of text, no data, no
`.bss`, seven references, and it defines two symbols** - `io_map` and `io_map_spec` (both currently
generated stubs). Five of the seven references are already real - `panic` (experiment 201),
`pmap_map` / `pmap_map_bd` / `pmap_map_bd_with_options` (pmap.o, experiment 197),
`virtual_space_start` (vm_resident.o, experiment 195) - and the other two, `kernel_map` and
`kmem_alloc_pageable`, are *already undefined names in this image*, so linking the object introduces
no new obligation at all. The measured cost is therefore 2 resolved / 0 added.

What will run is the branch `kernel_map == VM_MAP_NULL` takes, and on this device that is the right
branch - `kernel_map` is still the generated 4-byte zero stand-in, and XNU creates the real one in
`kmem_init`, long after `arm_init`. So `io_map(0x00200000, 4096, VM_WIMG_IO)`:

```c
	start = virtual_space_start;                 /* 0x40000000, see below */
	virtual_space_start += round_page(size);
	(void) pmap_map_bd(start, phys_addr, phys_addr + round_page(size), VM_PROT_READ|VM_PROT_WRITE);
	return (start + start_offset);
```

Two details are worth having measured before the run rather than after it.

- **`virtual_space_start` is `0x40000000`.** `pmap_bootstrap` sets it to
  `(gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000` (`arm_vm_init.c:505`) with `gVirtBase` =
  `0x00200000` and `MEM_SIZE_MAX` = `0x40000000` (`:134`), and `arm_vm_init.c:517` builds the page
  tables for exactly that VA with the same expression - the 1280-page loop of experiment 197. So
  `pmap_pte(kernel_pmap, 0x40000000)` finds a real PT entry and `pmap_map_bd` writes one PTE into it.
- **Flags are 7, so the `pmap_map_bd` leg runs, not the `WCOMB` leg.** `ml_io_map` is `mov r2, #7`
  and `io_map` tests `cmp r9, #6` for `VM_WIMG_WCOMB`. Seven is `VM_WIMG_IO`
  (`VM_MEM_COHERENT | VM_MEM_NOT_CACHEABLE | VM_MEM_GUARDED`, `pmap.h:294`). The
  `assert(flags == VM_WIMG_WCOMB || flags == VM_WIMG_IO)` is compiled out - this is RELEASE, where
  `assert(ex)` is `((void)0)` (`assert.h:106`) - so the only `panic` calls left in the compiled
  `io_map` are the `round_page` overflow checks.

The write that follows lands on this image's own first page, and that is intended rather than
accidental: `ml_vtophys(gPhysBase)` is `0x00200000` because `gVirtBase == gPhysBase` here, so XNU
maps PA `0x00200000` and copies its low exception vectors into it. In a real ARM kernel that page
*is* the low-vectors page. Here it is `_start` and the Mach-O header `entry_macho.s` writes, both
already consumed (experiment 201 read the header; `_start` returned long ago), so the cost is
nil - and it is the first time in this sequence that XNU creates a kernel VA mapping at runtime.

**The prediction is `bcopy_phys`.** Reading the object's call order rather than the source's, the
statements after `ml_io_map` are two `bcopy`s into the new mapping (real, `osfmk_arm_bcopy.o`), three
`ml_static_vtop` calls (real), and then:

```
 9e0:	bl	bcopy_phys          <-- a generated stub. Stop.
```

`bcopy_phys` is `osfmk/arm/loose_ends.c` (`osfmk_arm_loose_ends.o`), which is not linked. It is
reached before `CleanPoC_DcacheRegion` (`caches_asm.s`), which is also still a stub, and before the
`bcopy(running_signature, IOS_STATE, 8)` that would write into the new mapping at
`LowExceptionVectorsAddr + 0x80`. So the run should stop one call short of finishing
`cpu_machine_idle_init`, having done the mapping and the vector copy for real.

If it stops somewhere else - a `panic` leg reporting `PEHaltRestart`, or a `pmap_pte` returning NULL -
that is a finding about `virtual_space_start` or the PT pages, not about `io_map`, and the doc for
the next experiment should say which.

## Reproduce

```bash
# the step: 8 resolved, 5 added, 389 -> 386
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_SERIAL_GENERAL_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 8 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 5 added    (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=io_map

# the statements that ran, and the one after them
sed -n '369,382p' external/xnu-4570.1.46/osfmk/arm/arm_init.c
sed -n '558,566p' external/xnu-4570.1.46/osfmk/arm/cpu.c

# which caller: the whole call graph between arm_init's stop and ml_io_map
arm-none-eabi-nm -A --defined-only out/xnu_kernel_obj/*.o | grep -E " io_map$"
for f in out/xnu_kernel_obj/*.o; do arm-none-eabi-nm -u "$f" | grep -qE "U io_map$" && echo "$f"; done
for f in out/xnu_kernel_obj/*.o; do arm-none-eabi-nm -u "$f" | grep -qE "U ml_io_map$" && echo "$f"; done
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_machine_routines.o | sed -n '/<ml_io_map>:/,+3p'

# the terminal stop, and the order of the two call sites (34: cpu_machine_idle_init, 37: PE_init_platform)
sed -n '/^void entry_stub_hit/,/^}/p' stages/stage90/xnu_arm_boot/entry_stubs.c | tail -5
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | sed -n '/<arm_init>:/,/^$/p' \
  | grep -oE "bl\s+[0-9a-f]+ <(PE_init_printf|cpu_machine_idle_init|PE_init_platform)>"

# why pe_arm_map_interrupt_controller cannot be it: the tree has no interrupt-controller/master
./tools/host_dt_check.sh | grep -A1 'interrupt-controller'
grep -n "interrupt-controller" tools/host_dt_harness.c | sed -n '1,3p'
sed -n '781,790p' stages/stage90/stage90_main.c
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
