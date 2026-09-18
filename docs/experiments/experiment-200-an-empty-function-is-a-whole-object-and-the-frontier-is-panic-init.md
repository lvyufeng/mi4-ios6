# Experiment 200 — An Empty Function Is a Whole Object, `arm_init` Walks Back Into `osfmk/`, and the Frontier Is `panic_init`

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
 xnu_entry_kv_written=0x00000015
 xnu_entry_kv_in_dram=0x00000015
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=panic_init

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`, `xnu_entry_checks=5`,
`xnu_entry_failures=0x00000000`, and `persistent_write_attempted=0x00000000` in all 25 contracts
that report it. The device returned to Android on its own.

Three returns happened in this run, and none of them are visible in the log - which is what a run
that only reports *stops* looks like when the step it took was to make something stop stopping:

- `bsd_log_init()` returned. Expected, and the whole point of the previous experiment's closing
  paragraph: its body is a comment (`bsd/kern/subr_log.c`), and the object that supplies it is
  5851 bytes of text, 12496 of `.bss` and 42 references.
- `printf_init()` returned, two experiments after it was reached.
- `arm_init` reached its next statement, `panic_init()` - `arm_init.c:330` - which is back in
  `osfmk/` after one object in `bsd/`.

So the sequence the device has now executed, contiguously and in order, is

```
	arm_vm_init(xmaxmem, args);           experiment 197-198: completed, returned
	patch_low_glo();                      experiment 198 (debug=0x144 is on the command line)
	printf_init();                        experiment 199
	panic_init();                         here
```

which is four consecutive statements of `osfmk/arm/arm_init.c:321-330` with no stub between them.

## What the object cost

`bsd_kern_subr_log.o` - 5851 bytes of text, 4316 of data, 12496 of `.bss`, 42 references:

```
resolved (3):  bsd_log_init log_putc oslog_init
added   (21):  __firehose_buffer_create __firehose_merge_updates current_task get_task_map
               gsignal hz kmem_alloc_flags mach_make_memory_entry_64 mach_vm_map_kernel
               oslog_is_safe oslog_s_error_count proc_signal selrecord selthreadclear
               selwakeup sysctl__kern_children sysctl_io_number tsleep uio_resid uiomove
               wakeup
367 -> 385 undefined
```

Both directions link, taken by standing an empty object in for `bsd_kern_subr_log.o`.

**Twenty-one new obligations, three resolved, for a function whose body is a comment.** That is the
method's cost measured honestly: the step is chosen by the *name* the previous run reported, and the
name is the only thing that is cheap. Here it bought three symbols - `bsd_log_init` itself, plus
`log_putc` and `oslog_init`, which are in the same file and which `printf.o`'s console stack had
been calling as stubs - and paid twenty-one.

The twenty-one are also a boundary: the entry image's undefined set has never before contained
`tsleep`, `wakeup`, `selwakeup`, `uiomove`, `current_task`, `get_task_map`, `kmem_alloc_flags`,
`mach_vm_map_kernel`, `proc_signal`, `sysctl_io_number` or the two `__firehose_*` calls. Those are
the BSD layer's own dependencies - threads blocking, the task structure, the sysctl tree, the
select/poll waitlist, kernel memory allocation, and os_log's firehose - and none of them are
reachable from an empty function. They are stubs, and they will stay stubs for a while: what
reaches them is `log_putc`, which is the function that actually writes a log record, and nothing has
called it yet.

## Cost

| | exp-199 | now |
| --- | --- | --- |
| entry objects linked | 39 | 40 (`bsd/kern/subr_log.o`) |
| entry text | 230572 B | 236848 B |
| entry image | 329504 B | 333824 B |
| entry `.bss` | 0x002503e0 – 0x00257dc8 (31208 B) | 0x00251460 – 0x0025c048 (44008 B) |
| undefined | 367 | 385 |
| stubs | 311 functions, 56 storage | 326 functions, 59 storage |
| boot_args offset | +364544 | +385024 |
| headroom below `topOfKernelData` | 1737272 B | 1720248 B |
| payload text | 821318 B | 825638 B |

The image grew 4320 bytes for a 5851-byte object, which is the smallest ratio since `pmap.o`: no
16 KB alignment this time, and 12496 bytes of the object's `.bss` cost 12480. `.bss` ends at
0x0025c048, still 1.7 MB below `topOfKernelData`.

## What is next: `panic_init`, and a prediction the call graph gets wrong on purpose

The frontier is `panic_init` - `osfmk/kern/debug.c` - and the object is `osfmk_kern_debug.o`: **5549
bytes of text, 40 of data, 721 of `.bss`, 61 references.** Its body is four things:

```c
	uuid = getuuidfromheader(&_mh_execute_header, &uuidlen);
	if ((uuid != NULL) && (uuidlen == sizeof(uuid_t))) {
		kernel_uuid = uuid;
		uuid_unparse_upper(*(uuid_t *)uuid, kernel_uuid_string);
	}
	if (!PE_parse_boot_argn("assertions", &mach_assert, sizeof(mach_assert))) {
		mach_assert = 1;
	}
	/* the `#if !CONFIG_EMBEDDED` block is not compiled in: `PE_i_can_has_debugger`
	   is not among this object's references */
```

`tools/entry_frontier.py` walks it and reports one stop, `uuid_unparse_upper`. **That prediction is
wrong, and it is wrong for a reason the tool cannot see.** The call is inside `if (uuid != NULL)`,
and `uuid` comes from `getuuidfromheader(&_mh_execute_header, ...)` - a walk of *this image's own
Mach-O header*, the one experiment 194 added. That header has `ncmds = 2` and two `LC_SEGMENT`
commands, and no `LC_UUID`, so the search returns NULL, the branch is not taken, and the stop the run
should report is the statement after `panic_init` returns: **`PE_consistent_debug_inherit`**.

This is the tool's documented blind spot - branches are not modelled, so a call on an untaken path is
walked anyway - and it is the first time in this sequence that the *source* and the *header* together
settle a prediction the tool gets wrong. Both readings are worth keeping: the tool's name is what to
watch for if the Mach-O header ever grows an `LC_UUID`, and the run is what decides.

There is also a collision to settle before that run, and it is the fourth retired stand-in:
`osfmk_kern_debug.o` defines `panic`, and `entry_stubs.c` has a hand-written `panic` that ends the
run with `"panic() - XNU rejected something"`. Linking the object makes that a second definition. It
is not a cosmetic change - the real `panic` does considerably more than name itself - and it is the
first stand-in in this sequence whose replacement is not equivalent to it.

## Reproduce

```bash
# the step: 3 resolved, 21 added, 367 -> 385
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_BSD_SUBR_LOG_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 3 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 21 added   (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=panic_init

# the four consecutive arm_init statements the device has now executed
sed -n '318,332p' external/xnu-4570.1.46/osfmk/arm/arm_init.c

# what the next object does, and both halves of the prediction it will settle
sed -n '/^panic_init/,/^}/p' external/xnu-4570.1.46/osfmk/kern/debug.c
./tools/host_entry_macho_check.sh | head -2                 # ncmds=2, two LC_SEGMENT, no LC_UUID
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/libkern_kernel_mach_header.o | grep -E "getuuidfromheader"
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_debug.o | grep -E "uuid|PE_i_can_has_debugger"
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_debug.o | grep -w panic

python3 tools/entry_frontier.py --from panic_init --list 6 \
    out/xnu_kernel_obj/osfmk_kern_debug.o $(cat /tmp/objpaths201.txt)   # uuid_unparse_upper - untaken
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
