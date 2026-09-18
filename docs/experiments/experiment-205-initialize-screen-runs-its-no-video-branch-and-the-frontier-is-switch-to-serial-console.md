# Experiment 205 — `initialize_screen` Runs Its No-Video Branch, XNU Decides There Is No Framebuffer, and the Frontier Is `switch_to_serial_console`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000023
 xnu_entry_kv_in_dram=0x00000023
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=switch_to_serial_console

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own.

This is the prediction experiment 204 wrote down and did not get, reached one call frame deeper and
by the other route. The chain is

```
arm_init -> PE_create_console -> PE_initialize_console(kPETextMode)
         -> initialize_screen -> switch_to_serial_console
```

and it says something specific: `initialize_screen`'s first act, with `boot_vinfo` non-null, is to
look for a framebuffer, and the `else` for "there isn't one" ran:

```c
		if (!newVideoVirt && !new_vinfo.v_physaddr) {
			kprintf("initialize_screen: No video - forcing serial mode\n");
			new_vinfo.v_depth = 0;                           /* vc routines are nop */
			(void)switch_to_serial_console();
			gc_graphics_boot = FALSE;
			disableConsoleOutput = FALSE;
			gc_acquired = TRUE;
		}
```

**XNU has decided this device has no framebuffer and forced serial mode.** That is what the payload's
boot_args say: `boot_args.c` sets twelve fields and `Video` is not among them, so `v_baseAddr` is 0.

The prediction held because it was made from that fact rather than from the symbol appearing twice:
experiment 204 predicted `switch_to_serial_console` through the *boot-arg* route and the run came
back with `initialize_screen`; this one predicted it through the *no-video* route and the device
agreed. The two routes agree on the name and disagree on what it means, which is the reason the
previous experiment's doc says which one to expect.

## The object is the one the goal defers, and it is on the boot path anyway

`osfmk_console_video_console.o` - **27079 bytes of text, 4376 of data, 1360 of `.bss`, 31
references** - is the video console, and the goal statement puts graphics out of scope
("暂时不考虑图形界面的问题"). It is linked because there is no alternative: XNU's console *is* the
video console until something replaces it, `PE_create_console` calls `PE_initialize_console`, whose
`default:` case calls `initialize_screen` unconditionally, and `initialize_screen` is defined in
this object and nowhere else. What the goal defers is the framebuffer, not this - and on this run
the framebuffer path is the branch that was *not* taken.

```
resolved (4):  initialize_screen vc_display_icon vc_progress_initialize vcattach
added   (8):   clock_deadline_for_periodic_event clock_interval_to_deadline io_map_spec
               thread_call_cancel thread_call_enter_delayed thread_call_setup
               video_scroll_down video_scroll_up
385 -> 389 undefined
```

Both directions link, taken by standing an empty object in for `osfmk_console_video_console.o`.

`vcattach` matters more than it looks: `PE_init_printf` turns out to be a **tail call to it**, so
linking this object is also what makes `PE_init_printf` - the next statement of `arm_init` - do
anything at all.

## Cost

| | exp-204 | now |
| --- | --- | --- |
| entry objects linked | 44 | 45 (`osfmk/console/video_console.o`) |
| entry text | 243664 B | 270864 B |
| entry image | 333864 B | 371008 B |
| entry `.bss` | 100056 B | 101520 B |
| undefined | 385 | 389 |
| stubs | 326 functions, 59 storage | 330 functions, 59 storage |
| boot_args offset | +438272 | +479232 |
| headroom below `topOfKernelData` | 1664184 B | 1625720 B |
| payload text | 825686 B | 862830 B |

The payload grew 37 KB with the entry image, because the payload embeds it. `.bss` ends at
0x00273188, still 1.6 MB below `topOfKernelData`.

## What is next: `osfmk/console/serial_general.o`, and a function that does not stop

`switch_to_serial_console` is `osfmk/console/serial_general.c`, and its whole body is three
statements with no calls in them:

```c
	int old_cons_ops = cons_ops_index;
	cons_ops_index = SERIAL_CONS_OPS;
	return old_cons_ops;
```

So the object (824 bytes of text, 14 references, and it also defines `switch_to_old_console`,
`serial_keyboard_init`, `console_is_serial`, the console print buffer and `serialmode`) is linked
for a function the run will pass straight through. The one new obligation is `cons_ops_index` -
storage, 4 bytes, `osfmk_console_serial_console.o`.

The frontier therefore moves into `arm_init`'s tail, and the prediction is worth writing down
properly because the tail is longer than it looks. The statements after `PE_create_console` are:

```
	PE_init_printf(FALSE);        real: a tail call to vcattach, which this step's predecessor provided
	cpu_machine_idle_init(TRUE);  real: osfmk_arm_cpu.o, in the image since experiment 168
	PE_init_platform(TRUE, &BootCpuData);   real, and its TRUE branch is the one that has not run
	cpu_timebase_init(TRUE);      real: osfmk_arm_cpu.o
	fiq_context_init(TRUE);       real: machine_routines_asm.o
	__stack_chk_guard = early_random();     STUB: osfmk_prng_random.o is not linked
	machine_startup(args);        real: osfmk_arm_machine_routines.o
```

**The prediction is `io_map`**, not `early_random`. `PE_init_platform(TRUE, ...)` takes the branch
that has never run - the `vm_initialized` one - and that branch is

```c
	} else {
		pe_arm_init_interrupts(args);
		pe_arm_init_debug(args);
	}
```

`pe_arm_init_interrupts` is **Phase 3's own function** (`pexpert/arm/pe_identify_machine.c:0x68c`,
in the image since experiment 171). With `args` non-NULL it calls `pe_arm_map_interrupt_controller()`,
which does `pe_arm_get_soc_base_phys()`, finds the interrupt-controller node in this project's device
tree with `DTFindEntry`, reads its `reg` property, and calls `ml_io_map(soc_base + reg[0], reg[1])`.
`ml_io_map` is real and is a tail call to `io_map`, which is a stub.

That the two lines of this project meet here is worth saying plainly: the device-side `arm_init` has
now reached the function the Phase 3 platform shim exists to replace, and what stops it is the
*stock* mapping call - the one experiment 134 measured as unable to succeed on MS8974
(`soc_phys + reg[0]` gives `0xf2000000` where the hardware is at `0xf9000000`).

### Correction, added after the run (experiment 206)

The paragraph above names the right symbol by the wrong route, and the run is what showed it. The
stop was `io_map`, and it was reached six statements earlier in the same function - in
`cpu_machine_idle_init(TRUE)` at `osfmk/arm/cpu.c:562`, which calls
`ml_io_map(ml_vtophys((vm_offset_t)gPhysBase), PAGE_SIZE)` unconditionally, and which `arm_init`
calls at `arm_init.c:371` *before* `PE_init_platform` at `:379`. `pe_arm_init_interrupts` was never
reached, and `pe_arm_map_interrupt_controller` could not have reached `ml_io_map` in any case: this
project's tree deliberately has no node matching `DTFindEntry("interrupt-controller", "master")`
(`tools/host_dt_harness.c:236`, `stage90_main.c:781`), so it returns 0 at the `gPicBase` check.

The mistake was reading the source for the caller that was interesting rather than the object for the
caller that is reachable - `arm_init` has two paths to `ml_io_map` and this doc picked one. The
correction is worked out in experiment 206's doc; `io_map` was still the right prediction.

## Reproduce

```bash
# the step: 4 resolved, 8 added, 385 -> 389
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_VIDEO_CONSOLE_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 4 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 8 added    (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=switch_to_serial_console

# the branch that ran, and why: boot_args leaves Video zeroed
sed -n '/^initialize_screen/,/^}/p' external/xnu-4570.1.46/osfmk/console/video_console.c | sed -n '30,60p'
grep -n "Video" stages/stage90/boot_args.c
sed -n '16,36p' external/xnu-4570.1.46/pexpert/arm/pe_init.c      # PE_state.video <- boot_args->Video

# and the reason this object was unavoidable
sed -n '/^PE_create_console/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_init.c
sed -n '/^PE_initialize_console/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_init.c | tail -8
arm-none-eabi-nm -A --defined-only out/xnu_kernel_obj/*.o | grep -E " initialize_screen$"

# the next prediction, in the tail
sed -n '/^PE_init_platform(/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_init.c | sed -n '20,32p'
sed -n '/^pe_arm_init_interrupts/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E " (io_map|early_random)$"
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
