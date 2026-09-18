# Experiment 199 — `printf_init` Runs, the Four Bytes Are Now the Object, and the Frontier Is `bsd_log_init`

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
 xnu_entry_kv_written=0x00000017
 xnu_entry_kv_in_dram=0x00000017
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=bsd_log_init

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`, `xnu_entry_checks=5`,
`xnu_entry_failures=0x00000000`, and `persistent_write_attempted=0x00000000` in all 25 contracts
that report it. The device returned to Android on its own.

`printf_init` is three statements and two of them ran:

```c
	simple_lock_init(&printf_lock, 0);
	simple_lock_init(&bsd_log_spinlock, 0);
	bsd_log_init();
```

The two `simple_lock_init` calls are `arm_usimple_lock_init`, which this image has had since the
lock subsystem came in, so both executed and the third statement is where it stopped. That is the
whole of what this object bought on the device - and it is the point, because the object cost
fifteen new obligations to get there.

## The frontier's name is a no-op, which is worth saying plainly

`bsd_log_init` is `bsd/kern/subr_log.c`, and this is its entire body:

```c
void
bsd_log_init(void)
{
	/* After this point, we must be ready to accept characters */
}
```

The method stops on a *symbol*, and a symbol is not work. The next step is therefore not "unblock
the log system" - it is "supply a function that does nothing", and the object that supplies it
(`bsd_kern_subr_log.o`: 5851 bytes of text, 4316 of data, 12496 of `.bss`, **42 references**) is
large, most of its references are things like `tsleep`, `kernel_map`, `mach_vm_map_kernel`,
`kalloc_canblock`, `selwakeup` and the two `__firehose_*` calls, and *none of them are reachable
from `bsd_log_init`*. What the method measures is the next missing edge in the call graph. Whether
the far side of that edge is a paragraph or a comment is something the run does not say and the
source does - so read the source before sizing the step.

This is the first time in this sequence the next symbol has been a no-op, and it is also the first
time the object behind `bsd_log_init` touches `bsd/` at all: the frontier leaves `osfmk/`.

## What the object cost

`osfmk_kern_printf.o` - 5607 bytes of text, 4 of data, 296 of `.bss`, 26 references:

```
resolved (4):  _consume_printf_args doprnt_hide_pointers printf printf_init
added   (15):  PE_kputc _os_log_default bsd_log_init cngetc cnputc_unbuffered console_is_serial
               console_printbuf_clear console_printbuf_putc console_printbuf_state_init
               debug_putc disable_serial_output kernel_debugger_entry_count log_putc
               os_log_with_args paniclog_flush
356 -> 367 undefined
```

Both directions link, taken by standing an empty object in for `osfmk_kern_printf.o`.

The fifteen are the console stack, and they are stubs now - each one stops the run and names itself
if reached. `printf_init` reaches none of them.

## The decision this reverses, and why the reversal is the honest one

`entry_stubs.c` carried a paragraph arguing that this object should *not* be linked:

> Linking `printf.o` to obtain those four bytes would bring 5607 bytes of text across 23 functions
> and 24 obligations the image does not have, nearly all of them the console stack ... that
> `no_printf_str` and `no_kprintf_str` are the configuration's way of keeping out.

That argument was correct and is still correct, and it was an argument about **four bytes**. The
question it answers is "should this object be linked in order to define `_consume_kprintf_args`" -
and the answer is no, because the stand-in already in the file is the real function: an empty
variadic body, `bx lr`, four bytes.

The question this experiment asks is different, and the same object is the only possible answer:
`printf_init` is `osfmk/arm/arm_init.c:328`, the device's next statement, and it lives in
`osfmk/kern/printf.c` and nowhere else. There is no four-byte trick for a function with three
statements in it. So the object is linked, and the consequence that follows from *that* is the one
the old paragraph did not consider: **the hand-written `_consume_kprintf_args` is now a second
definition and had to be compiled out.**

That is the third stand-in this sequence has retired, and the pattern is the same every time: a
stand-in exists to close one link, and the step that makes the real function reachable makes the
stand-in a duplicate. `arm_init`'s stand-in went in experiment 159, `pmap_bootstrap`'s probe in
experiment 197, this one here. Each one is kept under its own switch rather than deleted, because
the switch is the record of what the step cost.

## Cost

| | exp-198 | now |
| --- | --- | --- |
| entry objects linked | 38 | 39 (`osfmk/kern/printf.o`) |
| entry text | 224716 B | 230572 B |
| entry image | 313120 B | 329504 B |
| entry `.bss` | 0x0024c3e0 – 0x00253c08 (30760 B) | 0x002503e0 – 0x00257dc8 (31208 B) |
| undefined | 356 | 367 |
| stubs | 303 functions, 53 storage | 311 functions, 56 storage |
| boot_args offset | +348160 | +364544 |
| headroom below `topOfKernelData` | 1754104 B | 1737272 B |
| payload text | 804934 B | 821318 B |

The image grew 16 KB - the same 16 KB as experiment 198, and again it is alignment rather than
code: `.bss` moved from 0x0024c3e0 to 0x002503e0, which is the next 16 KB boundary, because
`osfmk_kern_printf.o`'s `.bss` carries the same `PAGE_MAX_SIZE` alignment requirement
`lowGlo` does. The text is up 5856 bytes for an object of 5607.

## What is next: `bsd_kern_subr_log.o`, and then `panic_init`

The frontier is `bsd_log_init`, whose object is `bsd/kern/subr_log.c` - `bsd_kern_subr_log.o`,
5851 bytes of text, 4316 of data, 12496 of `.bss`, 42 references, and the first `bsd/` object in
the sequence. Its function is empty (above), so the run should pass straight through it.

Walking the image with that object added gives the stop after it, and it is back in `arm_init`:
**`panic_init`**, which is `osfmk/arm/arm_init.c:330` - the statement after `printf_init()` - and
whose object is `osfmk_kern_debug.o` (5549 bytes of text).

Two of `subr_log.o`'s 42 references are worth naming before they arrive: `__firehose_buffer_create`
and `__firehose_merge_updates` are the os_log firehose, which is already open host-side work in this
project. They will become stubs like everything else, and they are not reachable from an empty
function - but this is the step where they first appear in the entry image's undefined set.

## Reproduce

```bash
# the step: 4 resolved, 15 added, 356 -> 367
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_KERN_PRINTF_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 4 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 15 added   (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=bsd_log_init

# the frontier's name is a no-op, and the object behind it is not
sed -n '/^bsd_log_init/,/^}/p' external/xnu-4570.1.46/bsd/kern/subr_log.c
arm-none-eabi-size out/xnu_kernel_obj/bsd_kern_subr_log.o
arm-none-eabi-nm -u out/xnu_kernel_obj/bsd_kern_subr_log.o | wc -l      # 42

# what the object replaced: the stand-in that was four bytes and is now the real function
sed -n '534,562p' stages/stage90/xnu_arm_boot/entry_stubs.c

# what the run reaches next, once subr_log.o is in
python3 tools/entry_frontier.py --from arm_init --list 13 \
    out/xnu_kernel_obj/osfmk_arm_pmap.o $(cat /tmp/objpaths200.txt)   # panic_init
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
