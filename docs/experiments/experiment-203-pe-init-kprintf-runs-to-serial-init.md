# Experiment 203 — `PE_init_kprintf` Runs Its Whole Body Up to `serial_init`, and Two Storage Stubs Become Real Variables

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000016
 xnu_entry_kv_in_dram=0x00000016
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=serial_init

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own.

`serial_init` is `pe_kprintf.c:35`, the call that decides `PE_kputc`, and the prediction made before
the run was exactly this name. Three checks stood behind it, all settled on the host first:

- **`PE_state.initialized` is not FALSE**, so the `panic("Platform Expert not initialized")` at
  `pe_kprintf.c:29` is not taken. `PE_state` is real (`pexpert_arm_pe_init.o:0xc`, in the image since
  experiment 168) and `PE_init_platform(FALSE, args)` sets it TRUE at `pe_init.c:291`, which
  `arm_init.c:158` runs before anything here.
- **`usimple_lock_init` and `usimple_lock_try` are real** (`osfmk_arm_locks_arm.o`), so
  `simple_lock_init(&kprintf_lock, 0)` is not a stop.
- **`debug=0x144` has no `DB_KPRT` bit**, so the `disable_serial_output = FALSE` branch is not taken -
  and `disable_serial_output` is real storage as of this experiment anyway.

The first of the function's four executed statements that the image cannot supply is
`serial_init()`, so everything before it ran:

```c
	if (PE_state.initialized == FALSE) panic(...);   /* not taken - PE_state is real and TRUE */
	if (!vm_initialized) {                           /* TRUE: arm_init calls PE_init_kprintf(FALSE) */
		simple_lock_init(&kprintf_lock, 0);      /* ran - arm_usimple_lock_init is real */
		if (PE_parse_boot_argn("debug", &boot_arg, sizeof (boot_arg)))
			if (boot_arg & DB_KPRT) ...      /* not taken */
		if (serial_init())                       /* <-- the stop */
```

## Two storage stubs became real variables

`pe_kprintf.o` defines `PE_kputc` and `disable_serial_output`, and this image had been carrying both
as **generated storage stand-ins** - `uint8_t PE_kputc[0x4]` and `uint8_t disable_serial_output[0x4]`,
sized by the generator from this very object. Linking it is what turns them into the variables XNU's
own code assigns:

```
resolved (3):  PE_init_kprintf PE_kputc disable_serial_output
added   (1):   uart_putc
390 -> 388 undefined
```

Both directions link, taken by standing an empty object in for `pexpert_arm_pe_kprintf.o`.

The stand-ins were the right size and the wrong *kind*: `PE_kputc` is the console output function
pointer - `PE_init_kprintf` sets it to `serial_putc` or `cnputc` - and `printf.o` calls it through
that pointer. A zero-filled 4-byte array is a null function pointer, which is a data abort the first
time anything prints. It never happened, because nothing in the image has ever printed. This is the
`[[mi4-stand-in-size-is-not-value]]` pattern a third time, and the first time the stand-in is a
pointer that *the image's own code* was waiting to call.

## Cost

| | exp-202 | now |
| --- | --- | --- |
| entry objects linked | 42 | 43 (`pexpert/arm/pe_kprintf.o`) |
| entry text | 242768 B | 243312 B |
| entry image | 333864 B | 333864 B |
| entry `.bss` | 100120 B | 100056 B |
| undefined | 390 | 388 |
| stubs | 329 functions, 61 storage | 329 functions, 59 storage |
| boot_args offset | +438272 | +438272 |
| payload text | 825686 B | 825686 B |

`.bss` went *down* by 64 bytes: two 4-byte stand-ins plus their 64-byte alignment left, and
`pe_kprintf.o`'s own 48 bytes arrived. The image size did not move for the second experiment in a
row, for the same reason - `.text` still fits inside the gap before `.data` at 0x23C000.

## What is next: `pexpert_arm_pe_serial.o`, and a step with no cost at all

The frontier is `serial_init` - `pexpert/arm/pe_serial.c:669` - and the object is
`pexpert_arm_pe_serial.o`: **421 bytes of text, and six references, every one of them already
satisfied** by objects this image has had since experiments 168-171 (`PE_parse_boot_argn`,
`pe_arm_get_soc_base_phys`, `DTFindEntry`, `DTGetProperty`, `ml_io_map`, `arm_debug_read_dscr`).
That makes it the first step since `cpu.o` in experiment 168 that adds no obligation at all.

Its compiled body was read out of the object rather than out of the source, and the source is
misleading here: the `#ifdef S3CUART` / `#elif defined(ARM_BOARD_CONFIG_MV88F6710)` blocks at
`pe_serial.c:778-802` are both compiled out - the object carries no `strcmp` reference and none of
their four strings - which leaves the `else` at line 803 binding to the **last** `else if`, the
`uart1` lookup. So the executable shape is: `dcc` is not a boot arg; `pe_arm_get_soc_base_phys()` is
non-zero; then `DTFindEntry("boot-console", NULL, ...)`, `DTFindEntry("name","uart0",...)` and
`DTFindEntry("name","uart1",...)` in turn, and `else return 0`.

This project's tree has no serial node under any of those three names, so the prediction is that
`serial_init` returns 0 - `PE_kputc = cnputc` - and the run continues to whatever `arm_init` reaches
next. It is linked anyway, because the *lookup* is the measurement: `serial_init` is the first XNU
code to ask this project's device tree for a serial device.

## Reproduce

```bash
# the step: 3 resolved, 1 added, 390 -> 388
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_PE_KPRINTF_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 3 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 added    (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=serial_init

# the two storage stand-ins this step turned into real variables
grep -E '^data (PE_kputc|disable_serial_output) ' out/stage90/xnu_arm_entry_stubnames.txt   # before
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/pexpert_arm_pe_kprintf.o | grep -E "PE_kputc|disable_serial_output"

# the three checks behind the prediction
sed -n '24,45p' external/xnu-4570.1.46/pexpert/arm/pe_kprintf.c
sed -n '288,292p' external/xnu-4570.1.46/pexpert/arm/pe_init.c
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_arm_locks_arm.o | grep -E "usimple_lock_(init|try)"

# and why serial_init's source is not its shape
arm-none-eabi-nm -u out/xnu_kernel_obj/pexpert_arm_pe_serial.o      # 6 refs, no strcmp
sed -n '760,811p' external/xnu-4570.1.46/pexpert/arm/pe_serial.c
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
