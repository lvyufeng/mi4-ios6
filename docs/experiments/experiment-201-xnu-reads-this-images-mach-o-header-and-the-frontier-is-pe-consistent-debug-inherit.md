# Experiment 201 — XNU Reads This Image's Mach-O Header, the Panic Stand-In Is Retired, and the Frontier Is `PE_consistent_debug_inherit`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000026
 xnu_entry_kv_in_dram=0x00000026
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=PE_consistent_debug_inherit

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`, `xnu_entry_checks=5`,
`xnu_entry_failures=0x00000000`, and `persistent_write_attempted=0x00000000` in all 25 contracts
that report it. The device returned to Android on its own.

`panic_init()` ran, and its first statement is the interesting one:

```c
	uuid = getuuidfromheader(&_mh_execute_header, &uuidlen);
	if ((uuid != NULL) && (uuidlen == sizeof(uuid_t))) {
		kernel_uuid = uuid;
		uuid_unparse_upper(*(uuid_t *)uuid, kernel_uuid_string);
	}
	if (!PE_parse_boot_argn("assertions", &mach_assert, sizeof(mach_assert))) {
		mach_assert = 1;
	}
```

`getuuidfromheader` is `libkern/kernel_mach_header.o`, which searches a Mach-O header for an
`LC_UUID` load command. **The header it searched is this image's own** - the one experiment 194
added so that `getlastaddr()` and the section readers would work - and it has `ncmds = 2` and two
`LC_SEGMENT` commands, so the search returned NULL, the `uuid_unparse_upper` call was not made, and
`PE_parse_boot_argn("assertions", &mach_assert, ...)` ran.

This is the first time XNU's own code has read the Mach-O header this project built. It read it and
correctly found nothing, which is what the header says about itself.

## A prediction the tool got wrong, and the two things that settled it

`tools/entry_frontier.py`, walking `panic_init`, reports exactly one stop: `uuid_unparse_upper`. It
is the wrong answer, and the experiment was run expecting that:

- the call is inside `if (uuid != NULL)`, and the tool does not model branches - its own docstring
  says so, and this is the first time in this sequence the difference mattered;
- `uuid` comes from this image's Mach-O header, and `tools/host_entry_macho_check.sh` prints
  `ncmds=2` with two `LC_SEGMENT` commands and no `LC_UUID`.

So the source and the header together said the branch is not taken, and the run agreed with them:
the stop is `PE_consistent_debug_inherit`, which is the statement after `panic_init()` returns.
**Both readings are worth keeping.** The tool's name is the thing to expect if the Mach-O header
ever grows an `LC_UUID`; the run is what decides, and this time it decided against the tool.

## The fourth retired stand-in, and the first one that was not equivalent

`osfmk_kern_debug.o` defines `panic`, and `entry_stubs.c` had a hand-written `panic` that ended the
run with:

```
panic() - XNU rejected something; see which call precedes this line
```

That message was written when the image had no panic at all, and it is a *diagnosis this project
chose* - 4570's `device_tree.c` calls `panic` on a malformed tree, so the message is the difference
between "the tree was rejected" and "the run stopped at some stub". XNU's own `panic` is
`panic_trap_to_debugger(...)`, and the first thing on its path that this image does not have is
`PEHaltRestart(kPEPanicBegin)` in `iokit_Kernel_PlatformExpert.o`. **So a panic from here on reports
`stub_hit=PEHaltRestart`, which does not say that a panic happened.**

That cost is real and it is paid deliberately:

- the stand-in was a stand-in for an image with no panic in it, and an image that runs XNU's own code
  should panic the way XNU panics;
- the alternative is to keep a bespoke `panic` and never link `osfmk/kern/debug.o`, which means
  never running `panic_init` and never satisfying the frontier;
- and the hand-written version is kept in `entry_stubs.c` under `STAGE90_ENTRY_REAL_PANIC`, so the
  trade can be reversed with one build variable if the panic signal turns out to matter more than
  XNU's own behaviour.

This is the fourth stand-in retired by the step that satisfied it - after `arm_init` (exp-159),
`pmap_bootstrap`'s probe (exp-197) and `_consume_kprintf_args` (exp-199) - and the first whose
replacement does *not* do the same thing.

## What the object cost

`osfmk_kern_debug.o` - 5549 bytes of text, 40 of data, 721 of `.bss`, 61 references:

```
resolved (11):  Debugger debug_buf_base debug_buf_ptr debug_log_init debug_putc
                kern_feature_override kernel_debugger_entry_count mach_assert oslog_is_safe
                panicDebugging panic_init
added   (16):   DebuggerXCallEnter DebuggerXCallReturn OSCompareAndSwapPtr SavePanicInfo
                do_stackshot kalloc_large_total kdbg_dump_trace_to_file kdp_raise_exception
                kext_dump_panic_lists num_zones panic_include_zprint panic_kext_memory_info
                panic_kext_memory_size stack_total uuid_unparse_upper zone_array
385 -> 390 undefined
```

Both directions link, taken by standing an empty object in for `osfmk_kern_debug.o`.

`panic` appears in neither list, which is the collision being settled rather than discovered: it was
never undefined because the hand-written one defined it, and it is now defined by the object.

## Cost

| | exp-200 | now |
| --- | --- | --- |
| entry objects linked | 40 | 41 (`osfmk/kern/debug.o`) |
| entry text | 236848 B | 242416 B |
| entry image | 333824 B | 333864 B |
| entry `.bss` | 0x00251460 – 0x0025c048 (44008 B) | 0x00251470 – 0x00269b88 (100120 B) |
| undefined | 385 | 390 |
| stubs | 326 functions, 59 storage | 329 functions, 61 storage |
| boot_args offset | +385024 | +438272 |
| headroom below `topOfKernelData` | 1720248 B | 1664120 B |
| payload text | 825638 B | 825686 B |

The image grew by **40 bytes** while `.bss` grew by **56112**, and the whole of that is one storage
stand-in: `zone_array`, which `nm -S` reports as `0xd800` = 55296 bytes in `osfmk_kern_zalloc.o`,
plus `task_ledgers` (`0x60`) and four 4-byte words. `debug.o` brought 61 references and 56 KB of
`.bss` to run four statements. The generator sized all of it from the objects that define the
symbols, which is the only reason a 56 KB stand-in is safe: an undersized `zone_array` would be
overwritten by the first zalloc call, and what it overwrote would be whatever the linker put after
it. `.bss` now ends at 0x00269b88, still 1.6 MB below `topOfKernelData`.

## What is next: `pexpert_arm_pe_consistent_debug.o`, and a prediction that depends on a switch

The frontier is `PE_consistent_debug_inherit` - `pexpert/arm/pe_consistent_debug.c:35` - and the
object is `pexpert_arm_pe_consistent_debug.o`: **394 bytes of text, 0 of data, 4 of `.bss`, 4
references** (`DTGetProperty`, `DTLookupEntry`, `ml_map_high_window`, `OSCompareAndSwap64`). It is
the smallest object this sequence has linked, and three of its four references are already
satisfied - `ml_map_high_window` is in `osfmk_arm_machine_routines.o`.

The function is:

```c
	if ((DTLookupEntry(NULL, "/chosen", &entryP) == kSuccess))
		if (DTGetProperty(entryP, "consistent-debug-root", (void **)&prop_data, &size) == kSuccess)
			root_pointer = prop_data[0];
	if (root_pointer == 0)
		return -1;
	consistent_debug_registry = (dbg_registry_t *)ml_map_high_window(root_pointer, sizeof(dbg_registry_t));
```

**The prediction depends on a payload switch, and the switch is off.** `stage90_main.c:647-661`
adds that property only under `#if STAGE90_XNU_REAL_DT`, whose default is `0`
(`stage90.h:6922-6923`) - so `/chosen` exists with four properties and not five, `DTGetProperty`
fails, `root_pointer` stays 0, and the function returns -1 without calling `ml_map_high_window`.
The stop should therefore be the statement after it in `arm_init`, **`PE_init_kprintf`** - not a
symbol in this object at all.

That is worth stating before the run rather than after, because it is the same shape as the previous
experiment's wrong prediction: a call that exists in the source, is reachable in the call graph, and
is not reachable on this configuration.

## Reproduce

```bash
# the step: 11 resolved, 16 added, 385 -> 390
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_KERN_DEBUG_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 11 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 16 added    (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=PE_consistent_debug_inherit

# the header XNU's own code read, and the command it did not find
./tools/host_entry_macho_check.sh | head -1              # ncmds=2
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/libkern_kernel_mach_header.o | grep getuuidfromheader
sed -n '/^panic_init/,/^}/p' external/xnu-4570.1.46/osfmk/kern/debug.c

# the prediction the tool got wrong, and what settled it
python3 tools/entry_frontier.py --from panic_init --list 6 \
    out/xnu_kernel_obj/osfmk_kern_debug.o $(cat /tmp/objpaths201.txt)   # uuid_unparse_upper - untaken

# the 56 KB storage stand-in behind a 40-byte image growth
grep -E '^data (zone_array|task_ledgers) ' out/stage90/xnu_arm_entry_stubnames.txt
arm-none-eabi-nm -S out/xnu_kernel_obj/osfmk_kern_zalloc.o | grep zone_array

# what a panic will now report, and the switch that can bring the old message back
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_debug.o | grep -wE "panic|panic_spin_forever"
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/iokit_Kernel_IOPlatformExpert.o | grep PEHaltRestart
grep -n "STAGE90_ENTRY_REAL_PANIC" stages/stage90/xnu_arm_boot/build_entry.sh

# and the next prediction: /chosen has four properties, not five, in this build
grep -n "STAGE90_XNU_REAL_DT" stages/stage90/stage90.h | head -3
sed -n '645,662p' stages/stage90/stage90_main.c
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
