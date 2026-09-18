# Experiment 193 — `/defaults` was the missing node, `arm_vm_init` was reached, and the F bit of CPSR does not read 1 on this device

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_avm_xmaxmem_lo=0x5e500000
 xnu_entry_avm_xmaxmem_hi=0x00000000
 xnu_entry_avm_args=0x0023b000
 xnu_entry_avm_cpsr=0x60000093
 xnu_entry_avm_current=0x00237bd0
 xnu_entry_avm_bootcpu=0x0022e000
 xnu_entry_avm_master_ptr=0x00236ea8
 xnu_entry_avm_pset0_bitmask=0x00000001
 xnu_entry_avm_pset0_count=0x00000001
 xnu_entry_avm_proc_count=0x00000001
 stub_hit=arm_vm_init

No errors detected
```

Ten values. Eight came out as predicted; the two that did not are `CPSR` and `current`, and one of
them is a real finding about the platform while the other is a wrong assumption about a register.

## The step: a node, not an object

Nothing was linked for this experiment. `STAGE90_APPLE_DT_ROOT_CHILDREN` went from 20 to 21, and the
payload's device tree grew one root child:

```
/defaults
    hw.memsize = 0x5e500000
```

which is `apple_dt_prop_u32(b, "hw.memsize", RAM_CONSOLE_BASE - RAM_PHYS_BASE)` — the same
expression `boot_args.c:11` and the `/memory` node's `reg` already use. Exp-192's run stopped at
`stub_hit=IODTGetDefault` because `PE_get_default` could not find this node and fell through to a
symbol this image does not define. With the node present the first branch of
`PE_get_default` (`pexpert/gen/bootargs.c:391`) succeeds, `IODTGetDefault` is never called, and the
run carried on to the probe that was already written and linked.

`apple_dt selftest ok` and `apple_dt_root_children=0x00000015` (21) are in the payload log, and
`tools/host_dt_check.sh` walks the new tree with XNU's own reader and reports
`every single-field corruption was detected` over 10 cases including `/ nChildren 21+1`. The node is
asserted by name in `apple_dt.c` alongside the others, so a hardware run positively confirms it
rather than only confirming that the tree still walks to the right length.

**The value is a byte-order measurement as well as a size.** `DTGetProperty` does not swap and
`PE_get_default` `memcpy`s the property's bytes into `uint32_t memsize`, so the four bytes have to be
in the machine's own order. Written natively, `xmaxmem` reads back **0x5e500000**; had the tree been
expected big-endian the same bytes would have arrived as `0x0000505e`. That is the check exp-192's
doc promised for this run, and it settles the question for every numeric property in this tree.

## Three values that must have changed, and did

`processor_init`'s locked region is what exp-191 stopped at the top of and exp-192 ran through. Its
three effects are the only values in the probe that were supposed to *differ* from the last two runs:

| | exp-191 | now | where |
| --- | --- | --- | --- |
| `pset0.cpu_bitmask` | 0 | **0x00000001** | `bit_set(pset->cpu_bitmask, cpu_id)`, `cpu_id = 0` |
| `pset0.cpu_set_count` | 0 | **0x00000001** | `pset->cpu_set_count++` (`processor.c:180`) |
| `processor_count` | 0 | **0x00000001** | `processor_count++` (`processor.c:193`) |

They agree with each other and with the bit index, which is what makes them a check that the run did
not stop early rather than three independent numbers. `master_ptr = 0x00236ea8` is `BootProcessor` in
this build (`nm -S` puts it there, `0x6a8` bytes), the same object exp-190 traced from
`cpu_processor_alloc(TRUE)`, and `args = 0x0023b000` is the boot_args pointer the payload placed
after `.bss`, carried through `arm_init`'s `__aeabi_memcpy4` into `const_boot_args` and back out as
`arm_vm_init`'s second argument.

## `xmaxmem` is a size, and the clamp behind it is a no-op

`arm_vm_init` opens with

```c
	mem_size = args->memSize;                                              /* 0x00800000 */
	if ((memory_size != 0) && (mem_size > memory_size))
		mem_size = memory_size;
	if (mem_size > MEM_SIZE_MAX) mem_size = MEM_SIZE_MAX;
	static_memory_end = gVirtBase + mem_size;
```

so a `memory_size` *larger* than `args->memSize` cannot change anything, and 0x5e500000 is larger
than the 8 MB window this image maps. That was the reason for choosing it before the run, and the
run confirms the choice was inert: the kernel's memory map is still `[0x00200000, 0x00a00000)` and
the next step starts from the same map this one found.

## `current = 0x00237bd0` is `&init_thread`, and the assumption behind the prediction was wrong

The probe compares `current_thread()` against `&BootCpuData` and predicted they would be equal. They
are not, and the reason is that **TPIDRPRW holds the current thread, not the per-CPU data**:

```
a30:	ee1d4f90 	mrc	15, 0, r4, cr13, cr0, {4}     ; r4 = TPIDRPRW
a34:	e59405bc 	ldr	r0, [r4, #1468]               ; thread->machine.CpuDatap
a38:	e5d00030 	ldrb	r0, [r0, #48]                 ; CpuDatap->cpu_pending_ast
a3c:	e3100004 	tst	r0, #4                        ; AST_URGENT
```

That is `ml_set_interrupts_enabled`'s own enable path, two dereferences down from the register, and
it only type-checks if the register holds a `thread_t`. `0x00237bd0` is `init_thread`
(`thread.c:184`, `0x680` bytes, the same size as `thread_template`), which `thread_bootstrap` makes
current at `thread.c:397-398` — `init_thread = thread_template; machine_set_current_thread(&
init_thread);` — and which exp-176's stop proved is reached. So the value is a real pointer to a real
object and the prediction was the thing that was wrong, not the code.

One consequence is worth recording because exp-192's doc left it open: the enable path that the last
experiment called unsafe is safe **by this point on the path**, because `arm_init:510` sets
`thread->machine.CpuDatap = &BootCpuData` and `cpu_bootstrap`/`cpu_init` (`arm_init.c:430`, `:512`)
have called `machine_set_current_thread(cpu_data_ptr->cpu_active_thread)`. Before `thread_bootstrap`
it would have dereferenced a stale register.

## `CPSR = 0x60000093`, and the F bit

The prediction was `0x600000d3`: exp-191 read `0x60000093` at the top of `processor_init`'s critical
section with **I set and F clear**, and the real `ml_set_interrupts_enabled(FALSE)` ends its disable
path with

```
 a20:	f10c00c0 	cpsid	if
```

which masks both, so F should have come back set. It did not. The value is byte-for-byte the one
exp-191 read, and it is not a fluke of the decode: `cpsid if` encodes the two flags in the
instruction's bits 7 and 6, the positions they have in CPSR itself, and `0xF10C00C0` has both set.
The instruction ran, and it is the *pre-clamp* of the working session — the mask that decides which
way `splx` goes — that the two runs agree on.

What makes this a finding rather than a mistake is the whole record. **Every CPSR value this project
has ever read has F clear:**

| value | where | I | F |
| --- | --- | --- | --- |
| `0x60000093` | exp-191, this experiment | 1 | 0 |
| `0x60000193` | exp-11 (SGI, before) | 1 | 0 |
| `0x20000193` | exp-11 (after) | 1 | 0 |
| `0x80000193` | exp-06, exp-12 | 1 | 0 |
| `0x60000113` | exp-143, after `cpsid f` **and** an explicit attempt to unmask | 0 | 0 |
| `0x00000013` | exp-06, `cpsr_mode` | 0 | 0 |

Seven distinct values over seven experiments, including one where F was written on purpose. None has
bit 6 set.

Exp-143 already has this in its log, as a defect it caught and explained: its FIQ probe masked FIQ
with `cpsid f`, read CPSR to confirm, and read F clear; the explanation recorded there is that the
payload's IRQ return path is `subs pc, lr, #0`, which reloads the whole CPSR including F from
`SPSR_irq`, so any IRQ taken between the mask and the read puts F back.

**That explanation cannot apply here**, and this experiment is where it stops being sufficient:
`cpsid if` masks I as well as F, so no IRQ can be taken between the mask and the probe's `mrs`. What
remains is the other half of exp-143's own result — MSM8974 will not deliver an FIQ to non-secure
PL1 at all — of which "the non-secure F bit reads back clear whatever is written" is the register
corollary. This experiment does not decide between those two mechanisms and does not need to: what
it decides is that **the CPSR is not a usable check for this step**, and that the evidence the real
`ml_set_interrupts_enabled` ran is structural — the run reached a symbol that `arm_init` only reaches
after `processor_init` returns, which a stub cannot do because this image's stubs do not return.

The practical consequence is already known and already recorded: XNU's timer arrives on FIQ on this
build (`__ARM_TIME__` is defined nowhere, so `locore.s:147` takes the `#else` branch), and FIQ is the
one interrupt path this platform will not give the kernel. Nothing about this experiment changes that
conclusion; it adds the register-level observation to it.

## Cost

| | exp-192 | now |
| --- | --- | --- |
| entry objects linked | 33 | 33 (no object: the step is a device-tree node) |
| entry text | 137452 B | 137452 B |
| entry image | 215136 B | 215136 B |
| entry undefined | 321 | 321 |
| payload text | 706766 B | 706950 B |
| device tree length | 0x00007294 (29332 B) | 0x000072f4 (29428 B) |
| device tree root children | 20 | 21 |

The entry image is unchanged, which is the point: this experiment cost one node and 96 bytes of
device tree, and it bought the last symbol before the pmap. `xnu_entry_checks=5` /
`xnu_entry_failures=0` re-checked exp-175's four invariants.

## What is next: `arm_vm_init`, the largest step in the sequence

The frontier is now `osfmk/arm/arm_vm_init.o` — **8176 bytes of text, 8 of data and 216 of `.bss`
across 23 references**, the largest object since `thread.o` in exp-179, and the first thing in this
sequence that the original goal statement is about. **The probe has to move again**, because that
object defines the symbol the probe defines.

The move is named by the source rather than by a run. Once `arm_vm_init` returns, `arm_init` does
four things before its next undefined symbol: it reads `debug` through `PE_parse_boot_argn`, tests
`(debugmode & MIN_LOW_GLO_MASK) == MIN_LOW_GLO_MASK`, and calls `patch_low_glo()`. `MIN_LOW_GLO_MASK`
is `0x144` (`arm_init.c:127`) and **this payload's own cmdline contains `debug=0x144`**, so the test
is true and the call is taken — `patch_low_glo` is `osfmk/arm/lowmem_vectors.c:74`, in
`osfmk_arm_lowmem_vectors.o` (72 bytes of text, **988 of data** — the `lowGlo` table — and 6
references). So the next probe belongs at `patch_low_glo`.

The step itself is not a single symbol the way the last fifteen were, and it is worth saying so
before it is taken. Four of the object's 23 references point outside everything linked so far and
into the pmap and the VM: `kernel_pmap` (currently a 4-byte storage stub) and `kvtophys` (a 12-byte
function stub) stay stubs, and `pmap_bootstrap`, `pmap_init_pte_page`, `pmap_init_pte_static_page`
and `vm_set_page_size` are not in the image at all. Two more are the object's own globals
`cpu_tte`/`cpu_ttep`. And a group of them are **Mach-O artifacts** — `_mh_execute_header`,
`last_kernel_symbol`, `_lastkerneldataconst`, alongside `getsegdatafromheader`,
`getsectbynamefromheader`, `getsegbynamefromheader` and `nextsect` — symbols whose whole purpose is
to read the section addresses out of a Mach-O header that this ELF image does not have. That is the
same wall `ld64.lld` closed for `stage5` and it is the reason `arm_vm_init` is where "link the next
object" stops being the whole method: part of this object's input is a *header format* this image
does not produce, and that is a design decision to take deliberately rather than to discover as a
`stub_hit`.

## Reproduce

```bash
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
./tools/host_dt_check.sh          # walks the tree with XNU's own reader, 10/10 corruption cases
./tools/xnu_dt_requirements.py    # "every node header agrees with the properties it emits"
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'avm_\|stub_hit\|apple_dt selftest\|apple_dt_root_children\|No errors detected' \
   /tmp/cancro-last_kmsg.txt | tail -18

# the node, and the constant both DT checks read
grep -n 'defaults' -A4 stages/stage90/stage90_main.c | head -12
grep -n 'STAGE90_APPLE_DT_ROOT_CHILDREN' stages/stage90/stage90.h stages/stage90/stage90_main.c \
   stages/stage90/mmu.c

# the register that was read, and why it is a thread and not a CpuData
arm-none-eabi-nm -n -S out/stage90/xnu_arm_entry.elf | grep -B2 -A1 'init_thread'
sed -n '393,400p' external/xnu-4570.1.46/osfmk/kern/thread.c

# every CPSR this project has recorded - none has bit 6 set
grep -rhoiE "cpsr[a-z_0-9]*=0x[0-9a-f]{8}" docs/ stages/ tools/ | sort -u

# the next step, and the Mach-O half of it
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_arm_vm_init.o
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_lowmem_vectors.o
for s in $(arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_arm_vm_init.o | awk '{print $NF}'); do
   arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -q " $s\$" || echo "not in image: $s"
done
sed -n '320,330p' external/xnu-4570.1.46/osfmk/arm/arm_init.c
grep -n 'MIN_LOW_GLO_MASK' external/xnu-4570.1.46/osfmk/arm/arm_init.c
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it, and
the device returned to Android on its own.
