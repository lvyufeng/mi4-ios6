# Experiment 189 — The CPU identified itself, and the frontier is `processor_bootstrap`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

Three runs: the first two stopped somewhere other than the probe, and each stop is part of the
result. The third is below.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_cpuid_value=0x512b06f1
 xnu_entry_cpuid_family=0x00000000
 xnu_entry_cpu_type=0x0000000c
 xnu_entry_cpu_subtype=0x0000000c
 xnu_entry_cache_unified=0x00000000
 xnu_entry_cache_isize=0x00004000
 xnu_entry_cache_dsize=0x00004000
 xnu_entry_cache_type=0x00000004
 xnu_entry_cache_linesz=0x00000080
 xnu_entry_cache_assoc=0x00000008
 xnu_entry_cache_l2size=0x00200000
 xnu_entry_cache_colors=0x00000040
 xnu_entry_mvfp_neon=0x00000001
 xnu_entry_mvfp_hpfp=0x00000001
 stub_hit=processor_bootstrap

No errors detected
```

Fourteen values. Eleven came out as predicted, three did not — and the three are the interesting
part of this experiment, because each is a place where the code does not do what the hardware
datasheet would lead you to expect.

## The CPU, from a register our own payload read seven stages ago

`cpuid_info()->value = 0x512b06f1`. `do_cpuid` is two statements — `cpuid_cpu_info.value =
machine_read_midr()` and then, in the compiled code, `mov r1, #11` / `bfi r0, r1, #16, #4` — so the
low 24 bits and the top byte are MIDR and bits 19:16 are 0xb. The MIDR came out **0x512f06f1**,
which is not derived from anything in this tree: it is the value `cp15_midr` recorded in
experiment-06, this device's own `MRC p15, 0, r0, c0, c0, 0`, measured before XNU was ever linked.
The two runs agree, seven stages apart, through different code.

That also settles `11`. `ARMA7` makes `proc_reg.h:75` define `__ARM_SUB_ARCH__` as
`CPU_ARCH_ARMv7k`, and the compiler's `#define` beats the `CPU_ARCH_ARMv7` the `#else` would have
used (`cpuid.h` says that is 8, not 11). Reading the header rather than the code would have
predicted an arch nibble of 8, and then:

- `cpu_subtype` would have been predicted as 9, and it is **12** — from `cpu_init`'s switch, whose
  jump table is indexed by `arch - 2` over ten entries and whose entry 9 is `mov r0, #12`,
  `CPU_SUBTYPE_ARM_V7K`;
- `cpuid_get_cpufamily()` would have been predicted wrongly too, though that one is **0**
  (`CPUFAMILY_UNKNOWN`) for an unrelated reason: `cpuid.c`'s switch has cases for implementor
  0x41, 0x44, 0x4d, 0x56, 0x69 and 0x61, and this device's is 0x51, so the `default:` is what runs.
  The SoC is not one of the parts XNU's table knows, and the kernel says so itself.

`cpu_type = 12` is the other half of the same statement — `cpu_init` opens with
`if (cdp->cpu_type != CPU_TYPE_ARM)`, the compiled test being `cmp r0, #12` — and its being 12
afterwards is the evidence that the boot-CPU branch was taken, which the two defined candidates for
that branch's first call (`do_cpuid`, real; `pmap_cpu_data_init`, a stop that names itself)
already say independently.

## The cache: 16 KB of L1 that do not appear where the fields are named

Offsets 4 and 12 of `cpuid_cache_info` both hold **0x4000**. `cache_info_t` calls those `c_isize`
and `c_dsize`; `do_cacheid` reads the L1 *data* CCSIDR once and assigns the same product to both
(`cpuid.c:250,253`). So the pair is one measurement stated twice.

Offsets 24, 28 and 32 hold **0x80**, **8** and **0x200000**: 128-byte lines, 8-way, 2 MB. And these
are L2's, not L1's — which is the first of this experiment's three wrong predictions.

`do_cacheid` writes `c_linesz` and `c_assoc` for L1 at `cpuid.c:231-232`, and then writes both
fields **again** inside the level-2 block at `:275-276`. The probe's first full run predicted 0x40
(a 64-byte L1 line) at offset 24 and 0x200 for the colors below, and read 0x80 and 0x40. The
compiled code shows the double write plainly — `stm ip, {r2, r3, r6}` to offsets 20, 24 and 28 in
the first block, then `str r2, [r5, #24]` and `str r7, [r5, #28]` in the second — and the source
says the same thing. By the time `do_cacheid` returns, the L1's line size and associativity are
gone.

That in turn is why `vm_cache_geometry_colors` is **0x40** and not 0x200. `cpuid.c:290` computes it
as `((NumSets + 1) * c_linesz) / PAGE_SIZE`, and `c_linesz` at that point is the L2's, so it is the
L2's sets × line size / 4096 and the L1's 16 KB is not in it at all: 2048 × 128 / 4096 = 64.

The three L2 numbers then have to agree with each other, and they do exactly:
2048 sets × 128 bytes × 8 ways = 2,097,152 = 0x200000, the measured `c_l2size`. The sets come from
the measured colors, the line size is measured directly, and `c_assoc = 8` was predicted from that
product before the third run and read as 8 — which is what makes it a measurement rather than a
restatement of the other two.

## The third wrong prediction, and a field in the wrong place

`c_type` came out **4**, `CACHE_UNKNOWN`, where the predication said 1 — the prediction being "the
L1 data cache of an ARMv7 core is write-back", which is true of the hardware and false of the
code.

XNU declares the CCSIDR type as a 4-bit field at bits **31:28** (`cpuid.c:72`) and switches over
the one-hot nibble values 1, 2, 4 and 8. The ARM ARM puts `CCSIDR.Type` at bits [2:0] and leaves
[31:28] RES0, so on a conforming implementation the field XNU reads is always zero and the switch
always reaches its `default:`. The compiled code is exactly that: `mvn r0, #0` then
`add r3, r0, r1, lsr #28` indexes a table over 1..8, and 0 is outside it, so the `mov r2, #4` that
was already there stands.

The other three CCSIDR fields XNU reads *are* the ARM ARM's — `and r3, r1, #7` for LineSize at
[2:0], `ubfx r1, r1, #3, #10` for Assoc at [12:3], `ubfx r6, r1, #13, #15` for NumSets at [27:13] —
which is what makes the type the odd one out rather than the whole struct belonging to a different
register. One field is misplaced by twenty-eight bits, and the consequence is that
`cpuid_cache_info.c_type` is `CACHE_UNKNOWN` on this device and the `kprintf` still in the source
at `cpuid.c:298` would have printed `Unknown` for a cache that is write-back.

## MVFR1: VFP and NEON, and the coprocessor was already on

`machine_do_mvfpid` is four instructions: `vmrs r0, mvfr0`, `vmrs r0, mvfr1`, then
`ubfx r1, r0, #20, #4` and `ubfx r0, r0, #16, #4`, stored to offsets 4 and 0. So MVFR0 is read and
discarded and the two reported values are MVFR1's HPFP nibble and its SP nibble, both **1** — the
core reports single-precision NEON and half-precision conversion support. The deeper point is that
the instruction ran: `vmrs` needs CP10/CP11 enabled, `start.s:371-377` writes `CPACR |= 0xf << 20`
and `start.s:404-413` sets `FPEXC.EN`, with the comment that VFP is enabled for the `arm_init` path
precisely so it cannot take an undefined-instruction fault before there is a handler. Exp-188 had
already shown it the hard way — `timer_call_init`'s shift block is copied with `vld1.32`/`vst1.32`
and read back correctly — and this run reads a coprocessor register through the same enablement.

## Run 1: the frontier was not the function, it was what the function prints

The probe as first written stopped nowhere: the run ended at `stub_hit=_consume_kprintf_args`.

Linking `cpuid.o` put `do_cacheid`'s two `kprintf` calls on the boot path (`cpuid.c:290,298` — the
geometry print and the summary line), and in this kernel a `kprintf` is not a call into the print
subsystem. `build_xnu_arm_kernel.sh:84` defaults the configuration to `RELEASE`; Apple's
`BSD_RELEASE = [ BSD_BASE no_printf_str no_kprintf_str secure_kernel ]` (`MASTER.arm:24`) is its BSD
half; `make_defines.sh RELEASE` emits `-DCONFIG_NO_KPRINTF_STRINGS=1`; and `pexpert.h:204-210`
therefore rewrites every `kprintf` in osfmk as

```c
#define kprintf(x, ...) _consume_kprintf_args( 0, ## __VA_ARGS__ )
```

whose target, `osfmk/kern/printf.c:197`, is `void _consume_kprintf_args(int a __unused, ...) { }` —
an empty variadic function, four bytes, `bx lr` at offset 8 of `osfmk_kern_printf.o`.

So the second run defines it:

```c
void _consume_kprintf_args(int a, ...) { (void)a; }
```

which is not an approximation of the real one. An empty variadic function *is* the real one, and
the four bytes a link would fetch from `printf.o` would not change a single instruction. What that
link would cost is 5607 bytes of text across 23 functions and 24 obligations the image does not
have, nearly all of them the console stack — `cnputc`, `cnputc_unbuffered`, `PE_kputc`,
`console_printbuf_*`, `debug_putc`, `disable_serial_output`, `os_log_with_args`, `paniclog_flush`,
`bsd_log_init`. Those are the things `no_printf_str` and `no_kprintf_str` exist to keep out: this
kernel's print path is stripped by configuration, and the first run is the evidence, because the
only thing that stopped the image was the function whose whole meaning is "nothing to print".

## Run 2: eleven values, and two of them wrong

Run 2 reached the probe and reported eleven values: `cpuid_value=0x512b06f1`,
`cpuid_family=0`, `cpu_type=0xc`, `cpu_subtype=0xc`, `cache_isize=0x4000`, `cache_dsize=0x4000`,
`cache_l2size=0x200000`, `mvfp_neon=1`, `mvfp_hpfp=1` as predicted, and `cache_linesz=0x80` and
`cache_colors=0x40` where 0x40 and 0x200 were predicted.

Run 3 is the same probe plus the three fields those two errors left unexplained — `c_unified` at
offset 0, `c_type` at 20 and `c_assoc` at 28 — and it is the run at the top. Eleven of fourteen
right is the honest summary, and the three that were not are the three findings above.

## What the stop establishes

`processor_bootstrap` is `osfmk/kern/processor.c:120`, and the statement in `arm_init` immediately
after `cpu_init()` is a store — `EntropyData.index_ptr = EntropyData.buffer`, which is a write into
a generated storage stub, because `EntropyData` is defined in `osfmk/prng/random.o` and that object
is not in this image — so `processor_bootstrap` is the next symbol `arm_init` *reaches*. Stopping
here means the whole of the boot CPU's identification ran for real:

- `do_cpuid` read MIDR and set the arch nibble;
- `do_cacheid` read CLIDR, then CSSELR/CCSIDR twice — once for L1 and once for L2 — and computed
  eight fields and the kernel's cache-color count;
- `do_mvfpid` read MVFR1 through the VFP coprocessor;
- `do_debugid` read ID_DFR0, and DBGDIDR through CP14;
- `cpuid_info` and `cache_info` returned what those filled in;
- `cpu_init`'s `switch (cpu_info_p->arm_info.arm_arch)` chose `CPU_SUBTYPE_ARM_V7K`, and the
  function returned; `arm_init` continued past it.

That is the whole of `cpu_init`. What is left in `arm_init` before the scheduler exists is
`processor_bootstrap` and the boot-arg reads after it (`diag`, `maxmem`, `up_style_idle_exit`).

## What it cost

Two objects. `osfmk/arm/cpuid.o`, 850 bytes of text across 9 functions and 10 references, and
`osfmk/arm/machine_cpuid.o`, 220 bytes across 9 functions with **no undefined references of its
own** — every one of `cpuid.o`'s nine `machine_*` references lives in it, which is why the two are
one step rather than two.

```
resolved (8): arm_mvfp_info  cache_info  cpuid_get_cpufamily  cpuid_info
              do_cacheid  do_cpuid  do_debugid  do_mvfpid
added:        (nothing)
```

Measured by linking both, then an empty object in their place. `vm_cache_geometry_colors` — the
only new obligation either object has — is not in that delta because the *probe* names it too, and
probe references are not object cost. It is defined in `osfmk/vm/vm_resident.o`, which this image
does not link, so it is a generated storage stub: the store lands in `.bss` and is read back, which
is how 0x40 was measured.

| | exp-188 | now |
| --- | --- | --- |
| XNU objects linked | 28 | 30 (`osfmk/arm/cpuid.o`, `osfmk/arm/machine_cpuid.o`) |
| text | 128716 B | 129836 B |
| image | 198608 B | 198608 B |
| `.bss` | 0x00230550 – 0x00235b48 | 0x00230550 – 0x00235b48 (22008 B) |
| boot_args offset | +225280 | +225280 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 326 (280 functions, 46 storage) | 320 (273 functions, 47 storage) |
| headroom | 1877304 B | 1877176 B |

The image is the same size as exp-188's: 1120 bytes of text went into the fixed 128 KB text region
without moving the file. `xnu_entry_checks=5` / `xnu_entry_failures=0` re-checked exp-175's four
layout invariants. The 326 → 320 line is not the step's cost alone — the probe's own definitions
changed too: it defines `_consume_kprintf_args` and no longer defines `do_cpuid`.

## A note about the host, for the record

Measuring this step required linking the two objects temporarily, and `build_xnu_arm_kernel.sh`
begins by deleting every object in its output directory (`:146`), so a run of it with `--dir osfmk`
takes the other components' objects with it. Recovered by running it with no `--dir`, which is
108 objects' worth of work and the reason the entry-image build's next error was a missing
`pexpert_arm_pe_init.o` rather than anything to do with this experiment. The full run reports
612/615 C and 83/83 C++ for RELEASE — and the interesting part of that line is the C++ half, which
used to be blocked entirely by one line in `misc_protos.h` and is now clean.

## What is next

`processor_bootstrap` is `osfmk/kern/processor.c:120`, and it is the last of the CPU-and-scheduler
scaffolding `arm_init` calls before it starts parsing boot arguments. It is:

```c
	pset_init(&pset0, &pset_node0);
	pset_node0.psets = &pset0;
	simple_lock_init(&pset_node_lock, 0);
	queue_init(&tasks);  queue_init(&terminated_tasks);
	queue_init(&threads);  queue_init(&corpse_tasks);
	simple_lock_init(&processor_list_lock, 0);
	master_processor = cpu_to_processor(master_cpu);
```

`processor.c` is a large file and the step was measured on the host rather than guessed:
`osfmk_kern_processor.o` is 4784 bytes of text, 96 of data and 1616 of `.bss`, with 42 undefined
references, and it resolves eleven symbols this image currently stubs:

```
resolved: master_cpu  master_processor  processor_init  processor_pset  processor_set_primary
          processor_state_update_explicit  pset0  sched_stats_active  tasks_threads_lock
          threads  threads_count
```

Eleven resolutions for 42 references is the shape of a step where the object is mostly *state* —
`processor_array`, `pset0`, `pset_node0`, the run queues — and where the calls it makes
(`pset_init`, `cpu_to_processor`, `simple_lock_init`, `queue_init`) are the ones to check for being
already real before the step is taken. `pset0` is worth noting: it is among both the object's
definitions and the current stub set, so this step replaces a storage stub with the real object
the scheduler is about to start using.

After it, `arm_init` reads `diag`, `maxmem` and `up_style_idle_exit` through `PE_parse_boot_argn`
— real since exp-170 — and what follows in `arm_init` is the first code in this sequence that will
want a working `pmap`.

Nothing was flashed in any of the three runs: `persistent_write_attempted=0x00000000` in all 25
contracts that report it, each time, and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 129836, image 198608, 320 undefined, 273 stubs

# what the objects cost: 8 resolved, nothing added (both linked, then an empty object in their place)
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_ARM_CPUID_OBJ=/tmp/empty.o \
   STAGE90_ENTRY_ARM_MACHINE_CPUID_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_without_cpuid.txt)   # 8
comm -13 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_without_cpuid.txt)   # nothing

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'cpuid_\|cpu_type\|cache_\|mvfp_\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -16

# the arch nibble the compiler used, and why it is 11 and not CPU_ARCH_ARMv7's 8
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_cpuid.o | \
   awk '/<do_cpuid>:/{f=1} f{print} f&&/^$/{exit}'
sed -n '70,80p' external/xnu-4570.1.46/osfmk/arm/proc_reg.h
grep -n 'CPU_ARCH_ARMv7\b\|CPU_ARCH_ARMv7k\|CPU_VID_\|CPUFAMILY_UNKNOWN' \
   external/xnu-4570.1.46/osfmk/arm/cpuid.h external/xnu-4570.1.46/osfmk/mach/machine.h

# the double write that makes offsets 24 and 28 the L2's
sed -n '228,295p' external/xnu-4570.1.46/osfmk/arm/cpuid.c
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_cpuid.o | \
   awk '/<do_cacheid>:/{f=1} f{print} f&&/^$/{exit}' | grep -E 'str r2, \[r5, #24\]|str r7, \[r5, #28\]|stm'

# the type field at 31:28, and the switch that cannot match
sed -n '69,74p' external/xnu-4570.1.46/osfmk/arm/cpuid.c
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_cpuid.o | \
   awk '/<do_cacheid>:/{f=1} f{print} f&&/^$/{exit}' | grep -E 'lsr #28|mov r2, #4'

# the kprintf the configuration strips
tools/xnu_config/make_defines.sh RELEASE | grep -i kprintf
sed -n '197,199p' external/xnu-4570.1.46/osfmk/kern/printf.c
arm-none-eabi-objdump -d out/xnu_kernel_obj/osfmk_kern_printf.o | grep -A2 '<_consume_kprintf_args>:'

# the size of the next step, and what it would resolve
arm-none-eabi-size out/xnu_kernel_obj/osfmk_kern_processor.o
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_processor.o | awk '{print $NF}' | sort > /tmp/proc_defs.txt
comm -12 /tmp/proc_defs.txt <(sort out/stage90/xnu_arm_entry_undef.txt)   # 11 symbols
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_processor.o | wc -l      # 42 references
sed -n '120,140p' external/xnu-4570.1.46/osfmk/kern/processor.c
```
