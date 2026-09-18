# Experiment 202 — The Smallest Object So Far, a Prediction That Depended on a Payload Switch, and the Frontier Is `PE_init_kprintf`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x0000001a
 xnu_entry_kv_in_dram=0x0000001a
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=PE_init_kprintf

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`, `xnu_entry_checks=5`,
`xnu_entry_failures=0x00000000`, and `persistent_write_attempted=0x00000000` in all 25 contracts
that report it. The device returned to Android on its own.

The stop is **in no object this experiment linked**, and that was the prediction written down before
the run. `PE_consistent_debug_inherit()` did what the payload's configuration says it must:

```c
	if ((DTLookupEntry(NULL, "/chosen", &entryP) == kSuccess))          /* yes - the node exists */
		if (DTGetProperty(entryP, "consistent-debug-root", ...) == kSuccess)   /* no - not written */
			root_pointer = prop_data[0];
	if (root_pointer == 0)
		return -1;                                                      /* taken */
	consistent_debug_registry = (dbg_registry_t *)ml_map_high_window(...);  /* not reached */
```

`stage90_main.c:647-661` writes `consistent-debug-root` only under `#if STAGE90_XNU_REAL_DT`, whose
default is `0` (`stage90.h:6922-6923`), so `/chosen` has four properties and not five. The device
found `/chosen`, failed to find the property, and returned -1 - which is the same result the
property being absent is *for*. `ml_map_high_window` is real and in the image, and it was not called.

## What the object cost

`pexpert_arm_pe_consistent_debug.o` - 394 bytes of text, 0 of data, 4 of `.bss`, 4 references:

```
resolved (1):  PE_consistent_debug_inherit
added   (1):   OSCompareAndSwap64
390 -> 390 undefined
```

Both directions link, taken by standing an empty object in for `pexpert_arm_pe_consistent_debug.o`.

One for one, and the undefined count is unchanged. `OSCompareAndSwap64` is referenced by
`PE_consistent_debug_register` and not by anything on the path that ran.

## Cost

| | exp-201 | now |
| --- | --- | --- |
| entry objects linked | 41 | 42 (`pexpert/arm/pe_consistent_debug.o`) |
| entry text | 242416 B | 242768 B |
| entry image | 333864 B | **333864 B** |
| entry `.bss` | 0x00251470 – 0x00269b88 (100120 B) | unchanged |
| undefined | 390 | 390 |
| stubs | 329 functions, 61 storage | 329 functions, 61 storage |
| boot_args offset | +438272 | +438272 |
| headroom below `topOfKernelData` | 1664120 B | 1664120 B |
| payload text | 825686 B | **825686 B** |

**Nothing outside `.text` moved, including the image size**, and the reason is worth a line because
it looks like a measurement that failed: `.text` ends at 0x23B518 and `.data` begins at 0x23C000,
so 352 bytes of new text fitted inside the section-alignment gap and the binary's last byte - which
is in `__DATA,__data` at 0x251828 - did not move. `image bytes` is a size, not a checksum: two
different images can have it, and the entry image's own bytes did change (the payload's hash list
covers that).

## What is next: `pexpert_arm_pe_kprintf.o`, and the first statement that could panic

The frontier is `PE_init_kprintf` - `pexpert/arm/pe_kprintf.c:24` - and the object is
`pexpert_arm_pe_kprintf.o`: **536 bytes of text, 4 of data, 48 of `.bss`, 16 references.** The
function is:

```c
	if (PE_state.initialized == FALSE)
		panic("Platform Expert not initialized");

	if (!vm_initialized) {
		simple_lock_init(&kprintf_lock, 0);

		if (PE_parse_boot_argn("debug", &boot_arg, sizeof (boot_arg)))
			if (boot_arg & DB_KPRT)
				disable_serial_output = FALSE;

		if (serial_init())
			PE_kputc = serial_putc;
		else
			PE_kputc = cnputc;
	}
```

`arm_init` calls it as `PE_init_kprintf(FALSE)`, so the whole body runs, and the prediction is that
the stop is **`serial_init`** - `pexpert_arm_pe_serial.o`, not linked. Three things had to be checked
for that prediction to be worth writing down, and all three are settled in the image as it stands:

- **`PE_state.initialized` is not FALSE.** `PE_state` is real - `pexpert_arm_pe_init.o:0xc`, in the
  image since experiment 168 - and `PE_init_platform(FALSE, args)` sets `PE_state.initialized = TRUE`
  (`pe_init.c:290-291`) at `arm_init.c:158`, before anything here. If it were a zero-filled storage
  stand-in instead, this function would **panic**, and with experiment 201's real `panic` installed
  the run would report `stub_hit=PEHaltRestart` - a name that does not say a panic happened. That is
  the cost experiment 201 recorded, one experiment later, one check away from being paid.
- **`usimple_lock_init` and `usimple_lock_try` are real** (`osfmk_arm_locks_arm.o`), so the two
  locks in this file are not stops.
- **`boot_arg & DB_KPRT` is false, and even if it were not, `disable_serial_output` is storage.**
  `debug=0x144` has no `DB_KPRT` bit, so that branch does nothing.

So of the sixteen references, the first one that stops the run should be `serial_init`. The nine
behind it - `cnputc`, `uart_getc`, `uart_putc`, `_doprnt_log`, `os_log_with_args`,
`_os_log_default`, `ml_get_interrupts_enabled`, `ml_set_interrupts_enabled`, `cpu_number` - are all
past that point: `serial_init()` is evaluated to decide which of `serial_putc` and `cnputc`
`PE_kputc` gets, so nothing downstream is reached until it returns.

## Reproduce

```bash
# the step: 1 resolved, 1 added, 390 -> 390
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_PE_CONSISTENT_DEBUG_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 added    (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=PE_init_kprintf

# why the stop is not in the object this step linked: the property is behind a payload switch
sed -n '/^int PE_consistent_debug_inherit/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
sed -n '645,662p' stages/stage90/stage90_main.c
grep -n "define STAGE90_XNU_REAL_DT " stages/stage90/stage90.h

# why the image size did not move while .text did
arm-none-eabi-size -A out/stage90/xnu_arm_entry.elf | head -6
stat -c%s out/stage90/xnu_arm_entry.bin

# the three checks behind the next prediction
arm-none-eabi-nm --defined-only out/stage90/xnu_arm_entry.elf | grep -E " PE_state$"
sed -n '288,292p' external/xnu-4570.1.46/pexpert/arm/pe_init.c
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_arm_locks_arm.o | grep -E "usimple_lock_(init|try)"
sed -n '/^PE_init_kprintf/,/^}/p' external/xnu-4570.1.46/pexpert/arm/pe_kprintf.c
arm-none-eabi-nm -u out/xnu_kernel_obj/pexpert_arm_pe_kprintf.o
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
