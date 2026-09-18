# Experiment 204 — `serial_init` Runs, a Step With No Cost, and a Prediction That Was Wrong Because There Are Two Command Lines

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x0000001c
 xnu_entry_kv_in_dram=0x0000001c
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=initialize_screen

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own.

**The predicted stop was `switch_to_serial_console` and the actual stop is `initialize_screen`.** The
run is the more informative of the two, and the reason the prediction missed is worth the whole of
the next section.

What did happen is four more statements of `arm_init`:

- `PE_init_kprintf(FALSE)` returned.
- `serial_init()` **ran and returned 0**. It executed `pe_arm_get_soc_base_phys()`, then asked this
  project's device tree for `boot-console`, `uart0` and `uart1` - three real `DTFindEntry` calls
  through device-tree code this project built in experiments 168-171 - and found none of them, so it
  took the `else return 0` at `pe_serial.c:803`. `PE_kputc` therefore stayed `cnputc`: XNU's console
  output path on this device is the polled console, not a UART.
- The `serialmode` block ran and did **not** call `switch_to_serial_console()`.
- `PE_create_console()` was entered, ran `check_for_panic_log()` and `PE_initialize_console()`, and
  stopped at `initialize_screen`.

## The prediction was wrong, and the reason is a second command line

`arm_init.c:340-350` reads the serial mode out of the **boot_args structure**:

```c
	serialmode = 0;
	if (PE_parse_boot_argn("serial", &serialmode, sizeof(serialmode))) { ... }
	if (serialmode & SERIALMODE_OUTPUT) { switch_to_serial_console(); ... }
```

`PE_parse_boot_argn` reads `PE_state.bootArgs->CommandLine`, and the authoritative command line is
built in `stages/stage90/boot_args.c:19`:

```
debug=0x144 mi4ios6.stage=83 xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm
live-pmap tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine no-pub-peinit no-pub-dtinit
no-pub-peid no-pub-thread no-pub-cpuboot no-pub-rtclock
```

**There is no `serial=0x1` in it.** That string is in `stage90_main.c:665`, the `boot-args`
*property* of the device tree's `/chosen` node - a different consumer with a different command line,
and the one that appears in `grep` output and in the payload's own log. So:

```
serialmode = 0            ->  PE_parse_boot_argn("serial", ...) returns FALSE
kern_feature_override(2)  ->  FALSE  (KF_SERIAL_OVRD is 0x2, kern_feature_overrides is 1)
serialmode & SERIALMODE_OUTPUT  ->  0 & 1 == 0
```

both routes to `switch_to_serial_console` are closed, and the disassembly of `arm_init` shows the
two branches that would have reached it - `bne 2028bc` after `kern_feature_override` and
`beq 2028c4` after `tst r0, #1`. Neither was taken.

This is the project's oldest defect class, `[[mi4-one-value-two-definitions]]`, in a new place: **one
value with two definitions - a command line in `boot_args.c` and a command line in the device tree -
where only one of them reaches the function being predicted about.** The prediction read the one in
the `grep` result. The correction is one line in the doc and nothing in the code: `/chosen`'s
`boot-args` is XNU's *device-tree* command line, consumed by other code, and `boot_args.c:19` is the
one `PE_parse_boot_argn` sees.

Worth recording as a near miss rather than a mistake: **`kern_feature_override`'s boot-arg is
`validation_disables`, not `kern_feature_overrides`** - the name is in the object's `.rodata` at
`debug.o:0x1d1`, and it is what the function parses when `kern_feature_overrides` is 0. Neither is
on the command line, so the first call leaves `kern_feature_overrides = 1` (the `orr #1`) and every
`KF_*` query answers FALSE.

## The step itself had no cost at all

`pexpert_arm_pe_serial.o` - 421 bytes of text, six references, **every one already satisfied**:

```
resolved (3):  serial_init uart_getc uart_putc
added   (0):   -
388 -> 385 undefined
```

Both directions link, taken by standing an empty object in for `pexpert_arm_pe_serial.o`.

Zero added. The first step since `cpu.o` in experiment 168 that costs nothing, and the reason is
that `serial_init`'s dependencies (`ml_io_map`, `DTFindEntry`, `DTGetProperty`,
`pe_arm_get_soc_base_phys`, `PE_parse_boot_argn`, `arm_debug_read_dscr`) were all paid for by earlier
experiments for other reasons.

## Cost

| | exp-203 | now |
| --- | --- | --- |
| entry objects linked | 43 | 44 (`pexpert/arm/pe_serial.o`) |
| entry text | 243312 B | 243664 B |
| entry image | 333864 B | 333864 B |
| entry `.bss` | 100056 B | 100056 B |
| undefined | 388 | 385 |
| stubs | 329 functions, 59 storage | 326 functions, 59 storage |
| boot_args offset | +438272 | +438272 |
| payload text | 825686 B | 825686 B |

The image size did not move for the **third** experiment in a row - `.text` still ends below
`.data`'s 0x23C000 alignment - and the payload did not change either, because no payload source
changed. Both numbers are sizes, not checksums; the entry image's bytes did change.

## What is next: `osfmk/console/video_console.o`, and the honest cost of the frontier method

The frontier is `initialize_screen`, and `PE_create_console` reaches it unconditionally:
`PE_initialize_console(info, kPETextMode)` (`pe_init.c:387-389`, both branches of the console mode)
falls to its `default:` case, which is `initialize_screen(info, op)`.

The object is `osfmk_console_video_console.o`: **27079 bytes of text, 4376 of data, 1360 of `.bss`
and 31 references** - the largest step since `pmap.o`, and the video console. This is the object the
goal statement puts out of scope ("暂时不考虑图形界面的问题"), and it is on the boot path anyway,
because XNU's console *is* the video console until something else replaces it.

There is one branch worth predicting from, and it is the one this device should take. `initialize_screen`'s
first act, with `boot_vinfo` non-null, is to check for a framebuffer:

```c
		if (!newVideoVirt && !new_vinfo.v_physaddr) {            /* no framebuffer */
			kprintf("initialize_screen: No video - forcing serial mode\n");
			new_vinfo.v_depth = 0;                           /* vc routines are nop */
			(void)switch_to_serial_console();                /* <-- the stop */
			gc_graphics_boot = FALSE;
			disableConsoleOutput = FALSE;
			gc_acquired = TRUE;
		}
```

`PE_state.video.v_baseAddr` comes from the boot_args `Video` field, which this payload leaves zeroed
(`boot_args.c` sets only `Revision`, `Version`, `virtBase`, `physBase`, `memSize`,
`topOfKernelData`, `machineType`, `deviceTreeP`, `deviceTreeLength`, `CommandLine`, `bootFlags`,
`memSizeActual`). So the prediction is `switch_to_serial_console` inside `initialize_screen` - the
same symbol the previous prediction named, one call frame deeper, and this time reached through the
no-video path rather than through the boot-arg.

That the same name appears twice by different routes is the reason it is worth being explicit about
which one. If the stop is `switch_to_serial_console`, the no-video branch ran and XNU has decided
this device has no framebuffer; if it is something else in the object, the framebuffer path was
taken and there is more to look at than the prediction assumed.

## Reproduce

```bash
# the step: 3 resolved, 0 added, 388 -> 385
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_PE_SERIAL_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 3 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 0 added

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=initialize_screen, not the predicted switch_to_serial_console

# the two command lines, and which one PE_parse_boot_argn reads
grep -n "const char cmd\[\]" stages/stage90/boot_args.c          # no serial=... in it
grep -n "consistent-debug-root\|boot-args" stages/stage90/stage90_main.c | head -3   # serial=0x1 is here
grep -rn "PE_boot_args" external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c

# the branches that would have reached switch_to_serial_console, and were not taken
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | sed -n '/20288c:/,/2028c8:/p'

# the boot-arg kern_feature_override actually parses
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_debug.o | grep '\.L\.str\.26$'
arm-none-eabi-objdump -s -j .rodata.str1.1 out/xnu_kernel_obj/osfmk_kern_debug.o | sed -n '/01d0/p'
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
