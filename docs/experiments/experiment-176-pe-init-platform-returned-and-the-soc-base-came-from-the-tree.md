# Experiment 176 — `PE_init_platform` returned, and the SoC base came out of the tree

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
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_soc_base_phys=0xf9000000
 stub_hit=ml_parse_cpu_topology

No errors detected
```

Two things happened, and the second is the larger one.

**`PE_init_platform` returned for real.** Its last statement is `pe_init_debug` (exp-173), and
`pe_init_debug` is four `PE_parse_boot_argn` calls plus a bitmask (exp-174). With `PE_boot_args`
linked, XNU's own command-line parser ran to the end of `pe_init_debug` against the command line
this payload put in `boot_args` — and control came back. `arm_init.c:159` and everything after it
is now real XNU code executing to completion.

**`arm_init` reached line 217.** On arm32 everything between `PE_init_platform` (`arm_init.c:159`)
and `ml_parse_cpu_topology` (`:217`) is inside `#if __arm64__`, so the run went straight from 159 to
217 and stopped there. That is `arm_init` past the whole platform-expert bring-up, at the first
function in `osfmk/arm/machine_routines.c`.

## The value that makes this run more than a name

`ml_parse_cpu_topology` is a stub, and exp-171's probe in `entry_stubs.c` runs *before* it reports
its own name. It calls `pe_arm_get_soc_base_phys()` and logs what it returns. It returned
**`0xf9000000`**.

That number is checkable against this project's own source. `pe_identify_machine.c:225` is:

```c
if (DTFindEntry("name", "arm-io", &entryP) == kSuccess) {
        if (gPESoCDeviceType == 0) {
                DTGetProperty(entryP, "device_type", (void **)&tmpStr, &prop_size);
                strlcpy(gPESoCDeviceTypeBuffer, tmpStr, SOC_DEVICE_TYPE_BUFFER_SIZE);
                gPESoCDeviceType = gPESoCDeviceTypeBuffer;
                DTGetProperty(entryP, "ranges", (void **)&ranges_prop, &prop_size);
                gPESoCBasePhys = *(ranges_prop + 1);
        }
        return gPESoCBasePhys;
}
return 0;
```

and `stage90_main.c:23` emits the `arm-io` node's `ranges` as

```c
static const uint32_t io_ranges[] = { 0x00000000u, 0xf9000000u, 0x07000000u };
```

`*(ranges_prop + 1)` is `0xf9000000`. So the run says, on hardware, that `DTFindEntry` found the
node by the property `name == "arm-io"`, `DTGetProperty` read `device_type` (and `strlcpy` copied
it), `DTGetProperty` read `ranges`, and the second word of the property arrived intact — five XNU
functions and a property walk, verified not by a symbol name but by a value this project can be
wrong about. `0` here would have meant the node was not found; a shifted value would have meant the
property-read path was off by a word.

That path had only ever been exercised by `host_dt_check.sh` against the host build of XNU's own
reader. This is the first time it ran in XNU's ARM platform code with XNU's page tables live.

## One object, and why it travels alone

`pexpert/arm/pe_bootargs.o` — the whole file is the one accessor. Measured by linking an empty
object in its place and diffing the undefined sets:

```
resolved:  PE_boot_args
added:     (nothing)
```

105 − 1 + 0 = 104. The object names exactly one undefined symbol, `PE_state`, and
`pexpert/arm/pe_init.o` — in this link since exp-172 — defines it as real storage
(`B 0x002218fc`, `pexpert_arm_pe_init.o`'s `.bss`). So this object adds a definition and no
obligation, which is the opposite of the `pe_gen`/`bootargs` pair in exp-174 and the reason it did
not need a companion.

| | exp-174 | now |
| --- | --- | --- |
| XNU objects linked | 14 | 15 (`pexpert/arm/pe_bootargs.o`) |
| text | 62308 B | 62276 B |
| image | 132384 B | 132384 B |
| `.bss` | 0x00220378 – 0x00221e48 | 0x00220378 – 0x00221e48 |
| undefined | 105 (92 functions, 13 storage) | 104 (91 functions, 13 storage) |

Text went *down* by 32 bytes: the generated stub for `PE_boot_args` is a call to `entry_stub_hit`
with a string literal, and the real function is 20 bytes that load two fields. The image and the
bss did not move at all — the 32 bytes came off the end of a section the binary pads to alignment.

## What is next, and why it is not a small step

The device named `ml_parse_cpu_topology`, which lives in `osfmk/arm/machine_routines.o`. That
object is 4135 bytes of text across **71 functions** and references **79** undefined symbols, of
which **56** are neither defined nor stubbed in the image today. Every linked object so far added
three to five. The function that actually runs needs four device-tree calls, `strncmp`,
`PE_parse_boot_argn` and `panic` — all already present except `panic` — and the other 70 functions
in the file are what the 56 come from.

That is a measurement, not a decision, and it is the next experiment's subject: whether the unit of
"one object" is still the right unit when the object named by the device is a 71-function file, or
whether the frontier should be advanced by fewer symbols than the object carries. Nothing about it
is started here.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 62276, image 132384, 104 undefined, 91 stubs

# the object, and the measurement that it adds nothing
arm-none-eabi-nm -u out/xnu_kernel_obj/pexpert_arm_pe_bootargs.o     # PE_state, and only that
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_PE_BOOTARGS_OBJ=/tmp/empty.o ./build_entry.sh)      # then diff the undefined sets

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|soc_base_phys\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the value, against the tree that produced it
sed -n '225,240p' external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
sed -n '23,25p' stages/stage90/stage90_main.c

# what the next object costs
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_machine_routines.o
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_arm_machine_routines.o | grep -c ' T \| t '
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_machine_routines.o | wc -l
```
