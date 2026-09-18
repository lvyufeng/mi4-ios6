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
the next *stub* in `arm_init`'s source order - is the whole content of this experiment. Six
statements ran, and the sixth is the one this project has been building toward from the other side:

```
	PE_create_console()                  returns
	PE_init_printf(FALSE)                -> vcattach (a tail call), which ran
	cpu_machine_idle_init(TRUE)          ran
	if (arm_diag & 0x8000) ...           not taken
	PE_init_platform(TRUE, &BootCpuData) -> pe_arm_init_interrupts(args)  <-- PHASE 3'S FUNCTION
	cpu_timebase_init(TRUE)              not reached
```

Inside `pe_arm_init_interrupts(NULL-checked, args)`, with `args` = `&BootCpuData` so non-NULL:

```c
	if (!pe_arm_map_interrupt_controller()) return 0;
	return pe_arm_init_timer(args);
```

and `pe_arm_map_interrupt_controller()` did, in order:

- `kprintf("pe_arm_init_interrupts: args: %p\n", args)` - a no-op through `_consume_kprintf_args`,
  because this kernel is built with `CONFIG_NO_KPRINTF_STRINGS=1`;
- `gSocPhys = pe_arm_get_soc_base_phys()` - **it found `arm-io` in this project's device tree and
  read `ranges[1]` out of it**;
- `DTFindEntry(<name>, <interrupt-controller>)` - **it found the interrupt-controller node in this
  project's device tree**;
- `DTGetProperty(entry, "reg", &reg_prop, &prop_size)` - it read the node's `reg`;
- `ml_io_map(soc_base + reg[0], reg[1])` - and `ml_io_map` is a **tail call to `io_map`**, which is a
  stub. Stop.

## Why this is the two lines of the project meeting

`pe_arm_init_interrupts` is not incidental to this project - it is the subject of **Phase 3**. The
phase note says it "must *replace* that function, not configure it", that "its historical
`map`/`dispatch` path has a caller and works" (experiment 134), and that `pe_arm_init_interrupts` is
"now fully accounted for: map (replaced), dispatch (replaced), `tbd_ops` (run), FIQ (measured
unavailable)" (experiment 147). All of that was measured by the *payload calling it*, because XNU
was not running.

XNU is now running enough to call it itself, and the thing that stops the boot is the stock mapping
call - `ml_io_map(soc_phys + reg[0], reg[1])`, the line experiment 134 measured as
`map_apple_pic_base=0xf2000000` where the hardware's interrupt controller is at `0xf9000000`,
because it takes Apple's `soc_phys + reg[0]` from the device tree's `ranges` rather than the address
this SoC actually uses.

So the frontier is no longer "which object does XNU need next"; it is the same question as before
with a much better answer available: **the next symbol is `io_map`, and this project already knows
what the code calling it is for and why it cannot succeed.**

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

## What is next: `io_map`, and the question of *which* io_map semantics

The frontier is `io_map`, and the object is `osfmk/arm/io_map.c`. Before it is linked there is a
question worth settling on the host, because the answer decides what the next run means:

- `ml_io_map` is `osfmk/arm/machine_routines.c`'s tail call to `io_map`, and `io_map` on this ARM
  layer is the **kernel-map** version: it takes `kernel_map` and the pmap and creates a mapping. Its
  object will therefore pull in `pmap`/`vm_map` obligations that this image already has - `pmap.o`,
  `vm_resident.o`, `vm_map` pieces - and possibly new ones.
- The address it is asked to map is the *stock* one, `soc_phys + reg[0]`. Experiment 134's
  measurement says that is `0xf2000000`, not `0xf9000000`. Whether a successful mapping of the wrong
  address is better or worse than a stop here is a decision to make with the next run's result
  rather than in advance - but it is the first time in this sequence that the frontier's real
  question is not "is this object present" but "is this call the right thing to do at all".

That is Phase 3's question, and it now has a device-side answer available: the payload's replacement
for this step is `stage90_xnu_msm8974_shim.c` behind `STAGE90_XNU_MSM8974_SHIM`, which maps
`0xf9000000` and `0xf9020000` and was measured working in experiment 134. What it has never had is a
caller inside XNU. It has one now.

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

# the six statements that ran, and the sixth
sed -n '318,400p' external/xnu-4570.1.46/osfmk/arm/arm_init.c
sed -n '/^PE_init_platform(/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_init.c | sed -n '18,30p'

# the function this run reached, which is Phase 3's subject
sed -n '/^pe_arm_init_interrupts/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
sed -n '/^pe_arm_map_interrupt_controller/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
arm-none-eabi-nm -A --defined-only out/xnu_kernel_obj/*.o | grep -E " pe_arm_init_interrupts$"

# the tail call that makes io_map the frontier
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_machine_routines.o | sed -n '/<ml_io_map>:/,+4p'

# and the mapping call Phase 3 exists to replace, measured in experiment 134
grep -rn "map_apple_pic_base\|0xf9000000" stages/stage90/xnu_msm8974_shim.c | head -5
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
