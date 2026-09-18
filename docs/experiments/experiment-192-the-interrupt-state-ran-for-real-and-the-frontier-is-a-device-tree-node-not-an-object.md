# Experiment 192 — `ml_set_interrupts_enabled` ran for real, and the frontier is `IODTGetDefault`: a device-tree node, not an object

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=IODTGetDefault

No errors detected
```

No values. The probe written for this experiment sits at `arm_vm_init`, and it did not fire — the
run stopped one symbol earlier, at a *generated* stub, the kind that names itself and halts. So the
experiment has two results: the step that was taken, and the fact that it was the wrong step.

## What the stop proves, one symbol at a time

`IODTGetDefault` is reached from `PE_get_default` (`pexpert/gen/bootargs.c:386`) at `:424`, and
`PE_get_default` is called from `arm_init` at `arm_init.c:282`. That call is *after*
`processor_bootstrap` and *after* `processor_init` returns. So the run got through all of this for
real:

| what ran | where | how it is known |
| --- | --- | --- |
| `ml_set_interrupts_enabled(FALSE)`, the `splsched()` call | `machine_routines_common.c:507` | this step linked it; `processor_init` cannot reach its `splx` without returning from it |
| `ml_set_interrupts_enabled(s)` with `s = 0`, the `splx` | same | same, and `s` is 0 because the first call's `(state & PSR_IRQF) == 0` and exp-191 measured the I bit set |
| the rest of `processor_init` — `bit_set`, `cpu_set_count++`, `processor_count++` | `processor.c:170-200` | `processor_bootstrap` returns only after it |
| the four `PE_parse_boot_argn` calls for `diag`, `maxmem`, `up_style_idle_exit`, `immediate_NMI` | `arm_init.c:278-317` | real since exp-170, and `PE_get_default` is the fifth read in the same group |
| `PE_get_default("hw.memsize", &memsize, 4)` | `arm_init.c:282` | the stop |

The value exp-191 predicted would change did change, but not in a way this run can read: the two
counters are past the probe, and the probe is now at `arm_vm_init`, so the check that they became 1
moves to the next experiment along with everything else in the probe.

## Why the frontier is `IODTGetDefault`

`PE_get_default` has two branches:

```c
	if (kSuccess == DTLookupEntry(NULL, "/defaults", &dte)) {          /* pexpert/gen/bootargs.c:391 */
		if (kSuccess != DTGetProperty(dte, property_name, ...))
			return FALSE;
		memcpy(property_ptr, property_data, property_size);
		return TRUE;
	}
	return IODTGetDefault(property_name, property_ptr, max_property) ? FALSE : TRUE;   /* :424 */
```

`DTLookupEntry` and `DTGetProperty` are real in this image (`pexpert/gen/device_tree.o`, linked for
the boot-arg work of exp-170). `DTLookupEntry(NULL, "/defaults", …)` looks for a root child named
`defaults` and there is none, because **the payload's device tree has no `/defaults` node** — the
grep is one line and has been empty the whole time:

```bash
grep -rn '"defaults"\|hw\.memsize' stages/stage90/ | grep -v '\.md:'
#   (nothing)
```

So the first branch does not run, and control reaches the fallback. `IODTGetDefault` is
`iokit/Kernel/IODeviceTreeSupport.cpp:313`, no object in this image defines it, and the entry image
generates a stub for every such symbol — a stub whose whole body is `entry_stub_hit("<its own
name>")`. It named itself.

## The mistake in the prediction, and it is a class worth writing down

The reachability analysis done before this run was right and drew the wrong conclusion from it. It
found that exactly five `bl` targets lie between `processor_init`'s `splx` and the probe — four
`PE_parse_boot_argn` and one `PE_get_default` — that all five are real, and that `arm_vm_init` is
therefore the first undefined symbol on the path. It then *also* noticed `IODTGetDefault` in the
undefined list, inside `PE_get_default`, and dismissed it with "undefined here, so it is a generated
stub returning 0".

That is the error: **in this image a generated stub does not return.** `entry_stub_hit`
(`entry_stubs.c:344`) writes `stub_hit=<name>` and calls `entry_epilogue`, which turns the MMU off
and stops. The "stub that returns 0" is the *other* image — the payload's own build, where the
224 stubs exist so the tree can be linked. Having two stubs with the same name and different
behaviour is exactly the "one value, two definitions" shape this project has hit before, and the
tell was available without a run: `nm` shows both as `T` and 12 bytes, but only one of them is in
`xnu_arm_entry.elf`.

The practical rule: **when counting which undefined symbols lie ahead, a symbol that is undefined is
a stop, not a return.** The analysis should have ended at `IODTGetDefault`, one symbol earlier, and
predicted this result.

## The step, and its measured cost

`osfmk/arm/machine_routines_common.o` — 2923 bytes of text, 44 of data, 108 of `.bss`, 18
references. The probe moved off `ml_set_interrupts_enabled` **before** the object was linked, which
is the rule exp-190 derived: the object defines the symbol the exp-191 probe defined, and a link
that sees two definitions leaves a partial undefined-reference list that looks like a number. Both
directions of this measurement link cleanly:

```
resolved (1): ml_set_interrupts_enabled
added    (3): mach_absolutetime_asleep  mt_fixed_counts  mt_perfcontrol
319 -> 321 undefined
```

Three added obligations for one resolution, and the three are the *object's own* new references —
they appear in the with-object list and not in the without-object list because nothing else in the
image refers to them yet, so they become three new stubs. The five names that looked like they would
be new (`ast_taken_kernel`, `get_threadtask`, `kernel_task`, `proc_get_effective_thread_policy`,
`thread_get_perfcontrol_class`) are not in the delta at all: they were already undefined before this
step, from other objects. And none of the eight is called on this path — they belong to the AST
drain and the perfcontrol callbacks, which live behind `enable == 1`.

`get_preemption_level` is worth naming separately, because it is *not* in the delta and it is the
one that would matter. It is already a stub returning 0, and `ml_set_interrupts_enabled`'s enable
path takes `if (get_preemption_level() == 0)` and then dereferences the per-CPU base read out of
TPIDRPRW. That path is never entered here because both calls pass `enable = 0`, and `enable` is 0
because exp-191 measured the CPSR that decides the return value. The disable path the run does take
is four instructions — `cpsid if`, and a `bic` that turns the saved bit into the return value.

## Why `IODTGetDefault` cannot simply be linked

The obvious reading of "the frontier is an undefined symbol" is "link the object that defines it".
Here that object is `iokit_Kernel_IODeviceTreeSupport.o`: **11303 bytes of text and 57 undefined
references**, and they are not a few more ARM objects. They are C++:

```
_ZN12OSDictionary12withCapacityEj      OSUnserialize
_ZN14IODeviceMemory12withSubRangeEPS_mm  IORegistryEntry::fromPath
IOLockAlloc  IOService  OSData  OSDynamicCast  ml_static_mfree ...
```

That is the IORegistry plus libkern's C++ runtime plus the memory descriptor layer — a component,
not a step, and it is the wrong direction anyway. `IODTGetDefault` looks up `/defaults` through the
*IORegistry* plane; the branch above it in the same function looks up `/defaults` through the *PE
device-tree* plane. Both are asking for the same device-tree node:

```c
	defaults = IORegistryEntry::fromPath( "/defaults", gIODTPlane );   /* IODeviceTreeSupport.cpp:318 */
	if ( defaults == 0 ) return -1;
	defaultObj = OSDynamicCast( OSData, defaults->getProperty(key) );
```

So the missing thing is not a symbol, it is a **node**. This is the first frontier in the sequence
that is a piece of the device tree rather than an object, and it is the first one whose fix is on
the payload's side of the line rather than XNU's.

## What is next: `/defaults`, and the one number it needs

Adding a root child named `defaults` with a 4-byte `hw.memsize` property makes the first branch of
`PE_get_default` succeed, and the fallback is then never reached. That takes the run past
`arm_vm_init`'s call site and into the probe that is already written and linked.

Two decisions belong to that experiment and are recorded here so they can be checked rather than
assumed.

**The value.** `hw.memsize` is "the number of bytes of physical memory in the system"
(`bsd/sys/sysctl.h:989`). The payload's own `boot_args` declares `memSize = 0x00800000` — the 8 MB
this image maps and the only memory XNU's page tables cover — so **0x00800000** is the value that is
consistent with the object XNU is being handed, and `arm_vm_init`'s opening clamp
(`if ((memory_size != 0) && (mem_size > memory_size)) mem_size = memory_size;`) is then a no-op
whichever way it goes. The device has more DRAM than that; the probe reports the value it receives,
so the next experiment can say what XNU actually got.

**The byte order.** `DTGetProperty` returns the property's bytes and `PE_get_default` `memcpy`s them
into a `uint32_t`, with no swap anywhere between. So the property has to be in the machine's own
order. Apple's numeric device-tree properties are consumed the same way — `pe_serial.c:744` is
`uart_base = ml_io_map(soc_base + *reg_prop, *(reg_prop + 1))`, which only produces a real address
if `reg` is a native word — and our own numeric properties are already read natively by
`ml_parse_cpu_topology`, which has been real and running since exp-176. The probe makes this
falsifiable rather than argued: **0x00800000** means native order, **0x00008000** means the four
bytes went in the other way round.

## Cost

| | exp-191 | now |
| --- | --- | --- |
| XNU objects linked | 32 | 33 (`osfmk/arm/machine_routines_common.o`) |
| text (includes the probe) | 134444 B | 137452 B |
| image | 215088 B | 215136 B |
| `.bss` | 0x00234550 – 0x00239dc8 (22648 B) | 0x00234580 – 0x00239ec8 (22856 B) |
| boot_args offset | +241664 | +241664 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 319 (275 functions, 44 storage) | 321 (276 functions, 45 storage) |
| headroom | 1860152 B | 1859896 B |

Both numbers moved by more than this step: the probe was rewritten (ten `entry_kv` calls with longer
keys where the last one had eight), so `text size` is 3008 bytes up for a 2944-byte object, and the
321 above is 319 − 1 resolved + 3 added with the probe's own change to `arm_vm_init` folded in — the
probe now defines `arm_vm_init`, so that name left the undefined list and does not appear in the
delta. `image bytes` moved 48, which is the whole of the object's real contribution to the file:
`.bss` start moved by that much and the `.bin` ends at the first `.bss` byte. `xnu_entry_checks=5` /
`xnu_entry_failures=0` re-checked exp-175's four invariants with `args` still at +241664.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 137452, image 215136, 321 undefined, 276 stubs

# what the object cost: 1 resolved, 3 added. Both builds must link - the probe moved off
# `ml_set_interrupts_enabled` first, because this object defines it.
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_MACHINE_ROUTINES_COMMON_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # ml_set_interrupts_enabled
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # mach_absolutetime_asleep mt_fixed_counts mt_perfcontrol

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'real XNU entry\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -5

# the two branches of PE_get_default, and the one the run took
sed -n '386,425p' external/xnu-4570.1.46/pexpert/gen/bootargs.c
grep -rn "IODTGetDefault" external/xnu-4570.1.46/ | grep -v '\.md:'
sed -n '313,330p' external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp

# there is no /defaults node in the payload's tree
grep -rn '"defaults"' stages/stage90/ | grep -v '\.md:'

# why not just link it: 57 references, and they are C++
arm-none-eabi-size out/xnu_kernel_obj/iokit_Kernel_IODeviceTreeSupport.o
arm-none-eabi-nm -u out/xnu_kernel_obj/iokit_Kernel_IODeviceTreeSupport.o | head -30

# the disable path that did run, and the enable path that did not
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_machine_routines_common.o | \
   awk '/<ml_set_interrupts_enabled>:/{f=1} f{print} f&&/^$/{exit}'

# what became 0 and what became 1 inside the critical section this step ran through
sed -n '165,200p' external/xnu-4570.1.46/osfmk/kern/processor.c
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it, and
the device returned to Android on its own.
