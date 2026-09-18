# Experiment 258 — The Firehose's Kernel Side Is in the Tree, and the Stop Leaves `oslog_init`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

Experiment 257 linked Apple's ported firehose into the entry image and stopped one call *inside* it:
`__firehose_allocate`, from `firehose_buffer_create`. The link then had six names it could not
resolve, and 257's "what is next" predicted they lived one layer down in a newer XNU's
`osfmk/kern/firehose.c`. **That prediction was wrong, and this step is the measurement that says so.**

Four of the six are defined *in this tree*, by an object the manifest has been compiling all along and
the entry image had simply never linked:

```
external/xnu-4570.1.46/libkern/os/log.c        (out/xnu_arm_manifest.txt:403)
  :571  __firehose_buffer_push_to_logd
  :577  __firehose_allocate
  :596  __firehose_critical_region_enter
  :602  __firehose_critical_region_leave
```

Experiment 162 had already found `__firehose_allocate` at `log.c:580`; the measurement here is that all
four are in **one** object, and that the object was already built at
`out/xnu_kernel_obj/libkern_os_log.o`. So this step is the oldest method in this project — link the
object that defines the name — and not a second port. **`LIBKERN_OS_LOG_OBJ` is the change.**

The other two are what 257 said they were, and are *not* functions:

```
portinc/os/firehose_buffer_private.h:60   extern uint8_t __firehose_buffer_kernel_chunk_count;
portinc/os/firehose_buffer_private.h:61   extern uint8_t __firehose_num_kernel_io_pages;
```

`firehose_buffer_create` reads both with `ldrb`, so a stub at either address is read as *data* — it
would not stop the run, it would hand the buffer `0x40 << 12` (the first byte of a stub's `movw`
encoding; 257 recorded the byte). Their definitions are in Apple's closed `libfirehose_kernel`, which
no published tree contains: not 4570, not `xnu` at `main` (its whole tree has no
`osfmk/kern/firehose.c`), not libplatform, not libdispatch. So the port gains a second file,
**`stages/stage90/firehose/firehose_kernel_config.c`**, which is this project's and not Apple's, and
takes the values from the header beside it:

```
uint8_t __firehose_buffer_kernel_chunk_count = FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT;  /* 16 */
uint8_t __firehose_num_kernel_io_pages = FIREHOSE_BUFFER_KERNEL_DEFAULT_IO_PAGES;           /*  8 */
```

**16 is not a free choice.** `bsd/kern/subr_log.c:863` sizes the allocation from
`FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT` as this project's shim spells it — `16`, from
`libdispatch-913.30.4` — and 255 measured the result on the device: **73728 bytes = 65536 + 2 guard
pages**. `firehose_buffer_create` lays out the ring for the *variable's* value, so any other value
would describe a ring the allocation cannot hold. Both generations of the header agree on 16 (the
older as a constant, the newer as `MIN` = `DEFAULT`), which is the only reason the two spellings can
be used at all.

The object, measured before the run: 5551 bytes of text, 68 of data, 49 of bss, 26 definitions and
**44 references — 40 of which this image already satisfies.** That is what makes the step cheap: it
adds four names, all off the first-call path (`atm_get_diagnostic_config`,
`mach_continuous_approximate_time`, `OSKextKextForAddress`, `_os_trace_addr_in_text_segment`), and it
*replaces* nine current stand-ins, because the stub set is generated from the undefined list after the
link attempt.

## The prediction

`firehose_buffer_create` has exactly one call — `__firehose_allocate` — so once that is real the whole
create path completes: the header at `kernel_firehose_addr`, the 15-entry ring, the bank split
(15 - 8 = 7), the write-back of `size`. `oslog_init` then returns, and its caller is
`kernel_bootstrap`'s straight line:

```
8000dbc8  bl oslog_init                    ; kernel_bootstrap+0x188
8000dbcc  movw r0, #0xf43                  ; "telemetry_init"
8000dbd4  bl kernel_debug_string_early     ; real: it calls only strlen and strncpy
8000dbd8  bl telemetry_init                ; STUB at 0x80098a04
```

**The prediction: `stub_hit=telemetry_init`, `xnu_entry_stub_caller=0x8000dbdc`** — the stub hands
`entry_stub_hit` its own `lr`, so `caller - 4` = `0x8000dbd8` = `kernel_bootstrap+0x198` — the first
stop past the firehose, and the first inside `kernel_bootstrap`'s own body since 253's `cs_init`.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003b
 xnu_entry_kv_in_dram=0x0000003b
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=telemetry_init
 xnu_entry_stub_caller=0x8000dbdc

No errors detected
```

`0x8000dbdc` resolves to `kernel_bootstrap+0x19c`, whose `caller - 4` is
`8000dbd8: bl 80098a04 <telemetry_init>` — the prediction, address for address.
`failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

**The firehose is finished and it is behind us.** `oslog_init` returned: the ported implementation ran
to completion, the kernel-side callbacks it asked for are real code in the tree, and the buffer's own
configuration is a real variable rather than a stub's instruction encoding. The stop is no longer
anywhere in the firehose, or in the allocation path — it is the next initialisation call in
`kernel_bootstrap`'s straight line, which is where this project's frontier method lives.

`kv_written == kv_in_dram == 0x3b`, where 257 had `0x40`. That is not a discrepancy: `g_kv_buf` records
the **stub's name verbatim**, and `__firehose_allocate` is five characters longer than `telemetry_init`
(19 vs 14). The contract that matters — written equals in-DRAM — holds.

## Cost

| | exp-257 | now |
| --- | --- | --- |
| undefined | 599 | **592** (11 resolved, 4 added) |
| function stubs | 518 | **514** |
| storage stubs | 81 | **78** |
| entry text | 697217 B | **702564 B** (+5347) |
| entry image | 802176 B | **802248 B** (+72) |
| entry `.bss` end | 0x800f28c8 | **0x800f2888** (0x40 lower) |
| headroom | 1103672 B | **1103736 B** |
| payload text | 1294394 B | **1294466 B** (+72) |

The 11 resolved are the six firehose names plus the five stand-ins the real object replaces
(`_os_log_internal`, `os_log_with_args`, `_os_log_default`, `startup_serial_logging_active`,
`oslog_s_error_count`); `.bss` moved *back* 64 bytes because three of those were data stand-ins.
The image grew 72 bytes while the text grew 5347 — the rest fitted the linker script's alignment
padding, and the payload moved by exactly the image's 72.

## `--limit` is not a dry run, and what to do instead

257 recorded that `./tools/build_xnu_arm_kernel.sh --limit 1` empties `out/xnu_kernel_obj` (it
truncates its output directory by design) and cost a full rebuild. This step used the port-check
technique that avoids it, and it is worth recording because the port will be checked again:

```bash
XNU_KERNEL_OBJ_OUT=/tmp/scratch_obj ./tools/build_xnu_arm_kernel.sh --dir libkern
```

`XNU_KERNEL_OBJ_OUT` (`tools/build_xnu_arm_kernel.sh:61`) redirects the *tree* objects, so the
truncation lands in the scratch directory; the firehose block is outside the component loop and
unconditional, so it still writes the real `out/xnu_firehose_obj`; and `out/xnu_kernel_obj` kept all
695 objects through three such runs. The compile errors this loop caught were real and would otherwise
have cost three full builds: `firehose_kernel_config.c` needs `OS_FIREHOSE_SPI` defined before the
firehose headers (which `firehose_buffer.c:37` does for itself), and it needs
`<firehose_types_private.h>`, `<tracepoint_private.h>` and `<chunk_private.h>` before
`<os/firehose_buffer_private.h>` — whose declarations use `firehose_buffer_t`,
`firehose_tracepoint_t` and `firehose_tracepoint_id_u` and define none of them. Three missing
includes were three separate compile errors.

## What is next

`telemetry_init` is the frontier. Unlike the firehose it is an ordinary tree symbol with an ordinary
answer: the question is only which object defines it, and the answer is cheap to measure — it is
either one more object to link, as `libkern/os/log.c` was, or another of the manifest's omissions.

Behind it, `kernel_bootstrap`'s straight line is already readable from the disassembly and is a
preview of the shape of the next several steps:

```
8000dc24  bl console_init      <- stub   0x80096cac
8000dc34  bl stackshot_init    <- stub   0x8009871c
8000dc44  bl sched_init        <- stub   0x80098464
8000dc54  bl ltable_bootstrap  <- stub   0x80097abc
8000dc64  bl waitq_bootstrap   <- stub   0x8009967c
8000dc74  bl ipc_bootstrap     <- stub   0x80097414
```

each of them checked for the stub body (`movw r0, #<name>` first) in this image, with
`PE_i_can_has_debugger` and `PE_parse_boot_argn` between the first two already real, and
`kernel_debug_string_early` — real, and only `strlen`/`strncpy` — between every pair. That is the run-in
to `bsd_init`, which is where "XNU loads, enters the OS and runs its basic drivers" stops being a
forecast.

## Reproduce

```bash
# the four names are in one object the manifest already builds
grep -n 'libkern/os/log.c' out/xnu_arm_manifest.txt
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/libkern_os_log.o | grep firehose
arm-none-eabi-nm -u out/xnu_kernel_obj/libkern_os_log.o | wc -l    # 44

# the two data names, and why they must be 16 and 8
sed -n '54,68p' stages/stage90/firehose/portinc/os/firehose_buffer_private.h
sed -n '863,876p' external/xnu-4570.1.46/bsd/kern/subr_log.c

# build the port without emptying the tree's object directory
XNU_KERNEL_OBJ_OUT=/tmp/scratch_obj ./tools/build_xnu_arm_kernel.sh --dir libkern
arm-none-eabi-objdump -s out/xnu_firehose_obj/firehose_kernel_config.o | grep -A1 '.data'   # 10 08

# the link, and the prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
comm -13 <(sort /tmp/undef_257.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the added 4
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | sed -n '/<kernel_bootstrap>:/,/^$/p' | grep -A1 -B1 telemetry_init

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8000dbdc      # -> kernel_bootstrap+0x19c
```
