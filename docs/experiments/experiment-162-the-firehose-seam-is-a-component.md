# Experiment 162 — the firehose seam is a component, not a value

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `stages/stage90/shims_arm/os/firehose_buffer_private.h` (the one file changed)

| `RELEASE` | before | after |
| --- | --- | --- |
| C | 610 of 615 | **612 of 615** |
| C++, unchanged | 82 of 83 | 82 of 83 |
| undefined after a whole-kernel link | 97 | **72** |
| boot-path stubs, from `arm_init` | 12 of 97 | **14 of 72** (first written as 7 — see the note below) |
| `.text` in the measurement image | 4896304 bytes | 4908480 bytes |

| `STAGE90_BOOT` | before | after |
| --- | --- | --- |
| C | 416 of 426 | **418 of 426** |
| C++, unchanged | 82 of 83 | 82 of 83 |
| undefined, measurement link | 238 | **229** |
| boot-path stubs (`stub_blockers.py --min`) | 24 | **27** |

30 symbols closed, 5 opened. No line of XNU's code was changed and no compiler flag was added: the
whole stage is one header, and the header was already there.

## The thing that was recorded four times as absent

`FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT` is used three times and defined nowhere in the tarball, and
that was recorded as a limitation in four separate experiments — 132, 148, 151 and 153 — each time
in the same words:

> a value with no evidence in the tarball

The wording is accurate. The conclusion the project drew from it was not. `os/firehose_buffer_private.h`
is **published**, in `apple-oss-distributions/libdispatch`, at `os/firehose_buffer_private.h` — and
the file is not merely the source of one constant. Its `#ifdef KERNEL` block is

```c
// implemented by the kernel
extern void __firehose_buffer_push_to_logd(firehose_buffer_t fb, bool for_io);
extern void __firehose_critical_region_enter(void);
extern void __firehose_critical_region_leave(void);
extern void __firehose_allocate(vm_offset_t *addr, vm_size_t size);

// exported for the kernel
firehose_tracepoint_t
__firehose_buffer_tracepoint_reserve(uint64_t stamp, firehose_stream_t stream,
		uint16_t pubsize, uint16_t privsize, uint8_t **privptr);

void
__firehose_buffer_tracepoint_flush(firehose_tracepoint_t vat,
		firehose_tracepoint_id_u vatid);

firehose_buffer_t
__firehose_buffer_create(size_t *size);

void
__firehose_merge_updates(firehose_push_reply_t update);
```

The first four are the ones `libkern/os/firehose.h` already declares and `log.c` already defines.
The second four are the four symbols that no file in the tarball defines, called at

```
libkern/os/log.c:426    __firehose_buffer_tracepoint_reserve
libkern/os/log.c:462    __firehose_buffer_tracepoint_flush
bsd/kern/subr_log.c:874 __firehose_buffer_create
bsd/kern/subr_log.c:813 __firehose_merge_updates
```

with those signatures, argument for argument. **The description of the interface is published; only
the implementation is not.** The header the project has been shimming is a real file, and the
shim's own comment said so about the types it forwards — `firehose_chunk_t`, `firehose_stream_t`,
`firehose_buffer_t`, `firehose_tracepoint_id_t` — while still calling itself *derived*.

This is the shape the project has met before and named: a wall that dissolves when the question
becomes "which component owns this path" rather than "is it in the tarball". The header is `os/…`,
neither the include path nor the tarball owns `os/`, and the component that owns `os/` is
libdispatch. See `mi4-not-absent-its-build-output`.

## The value

`os/firehose_buffer_private.h:43` at tag `libdispatch-913.30.4` (the 10.13 GM):

```c
#define FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT		16
```

and the same file in the next generation — `libdispatch-1008.200.78` onward, `OS_FIREHOSE_SPI_VERSION
20180226` — turned the constant into a runtime variable and kept 16 as both its floor and its
default:

```c
#define FIREHOSE_BUFFER_KERNEL_MIN_CHUNK_COUNT	16
#define FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT FIREHOSE_BUFFER_KERNEL_MIN_CHUNK_COUNT
```

So 16 is not a guess in either generation. Which generation this XNU is, is a separate question and
is not settled by this stage: the four declarations above are identical in both, and both give 16.
The tarball's own `libkern/firehose/private.h:24` contributes a third stamp, `FIREHOSE_SPI_VERSION
20170907` (the userland header's is `OS_FIREHOSE_SPI_VERSION 20170222`), so no published header
carries our exact revision — and the discriminator that will matter for the next stage is the
`firehose_chunk_range_s`/`fcr_offset` spelling, which is xnu's own and appears in neither
libdispatch header (they use `firehose_buffer_range_s`/`fbr_offset`, same two `uint16_t`s).

Every value ≥ 1 satisfies all five uses. Four of them want the product with `FIREHOSE_CHUNK_SIZE`
(`chunk_private.h:35`, 4096) — 65536 bytes plus two guard pages is what `oslog_init` allocates and
what `LOGBUFFERMAP` hands to logd — and the fifth, `log.c:591`, writes the boot chunk into chunk
`COUNT - 1`, the last of the ring. 16 is the one Apple published, so it is the one used.

## What the shim now is, and what it declines to be

It forwards the same four type headers, adds `<stdbool.h>` and `<stdint.h>` so the declarations below
do not depend on include order, and adds the macro and the four declarations. It does **not** add the
other four declarations from the same `#ifdef KERNEL` block: `xnu` declares those in
`libkern/os/firehose.h`, which `log.c:34` includes, and a second declaration of the same function is
the "one value, two definitions" shape this project has met sixteen times. Here the two would agree
today and drift the first time either one moved.

## What it bought, precisely

The 30 that closed are the definitions of the two files that now compile, and both files are
`standard` in their `conf/files` — `bsd/conf/files:428` and `libkern/conf/files:54` — so neither is
conditional on an option and neither had an alternative provider:

- **24** from `bsd/kern/subr_log.c`: `bsd_log_init`, `logclose`, `log_dmesg`, `logioctl`, `log_open`,
  `logopen`, `log_putc`, `log_putc_locked`, `logread`, `logselect`, `log_setsize`, `logwakeup`,
  `msgbufp`, `oslogclose`, `oslog_init`, `oslogioctl`, `oslogopen`, `oslogselect`, `oslog_setsize`,
  `oslog_streamclose`, `oslog_streamioctl`, `oslog_streamopen`, `oslog_streamread`,
  `oslog_streamselect` — the `/dev/log` and `/dev/oslog` device entry points, the two character
  sinks `printf` writes through, and the `struct msgbuf` the BSD kernel logs into.
- **6** from `libkern/os/log.c`: `_os_log_default`, `_os_log_internal`, `os_log_with_args`,
  `firehose_trace_metadata` (defined there at `:506`, declared in `tracepoint_private.h:156`),
  and `startup_serial_logging_active` / `startup_serial_num_procs` (`:78-79`) — the two globals
  `kern_fork.c:1232` flips once the startup burst of 300 processes has forked.

The 5 that opened are what "a file entering the link brings its references with it" means: the four
`__firehose_*` above, and `OSKextKextForAddress`, which `log.c:59` declares and `libkern/OSKextLib.cpp`
does not yet define because that file does not compile.

The attribution that produced the "21 from `subr_log.c`" figure in the previous measurement was
textual and over-attributes: `startup_serial_logging_active` was filed under
`bsd/kern/kern_fork.c`, which only *uses* it. The lists above are read off the symbol diff, not the
regex.

## The cost, and it is not zero: the boot path got wider

162 closes 25 more than it opens and still **raises** `STAGE90_BOOT`'s boot-path stub count from 24
to 27. That is not a mistake in either number. A whole-kernel link leaves fewer undefined symbols;
a *boot* reaches the code that is now in the image, and that code calls things.

```
   4  __firehose_buffer_create            oslog_init <- kernel_bootstrap <- machine_startup <- arm_init
   5  __firehose_buffer_tracepoint_flush  _firehose_trace <- _os_log_to_log_internal <- os_log_with_args <- kprintf <- arm_init
   5  __firehose_buffer_tracepoint_reserve _firehose_trace <- _os_log_to_log_internal <- os_log_with_args <- kprintf <- arm_init
   4  OSKextKextForAddress               _os_log_to_log_internal <- os_log_with_args <- kprintf <- arm_init
```

`kprintf` now reaches `_os_log_to_log_internal`, which is the edge that did not exist before. So the
four symbols this stage opened are not a footnote: `__firehose_buffer_create` is on the boot path at
distance 4 and the reserve/flush pair at 5. They are the next stage, and they now have a source —
`libdispatch/src/firehose/firehose_buffer.c`, 1188 lines at `libdispatch-913.30.4`, with a `#ifdef
KERNEL` half written for exactly this.

They are also **safe** in the measurement image, which is what makes the next stage a port rather
than a repair. Every stub `measure_link.sh` emits is `mov r0, #0; bx lr`, and:

- `reserve`'s 0 is `NULL`, and `log.c:427-447` already has a `NULL` branch — it falls back to
  `firehose_boot_chunk` and `firehose_chunk_tracepoint_try_reserve`, the pre-`oslog_boot_done` path
  Apple wrote for exactly this case;
- `flush` is only reached when `reserve` returned non-`NULL`, so it is unreachable given the above;
- `merge_updates` is called from `subr_log.c:813`, inside `oslogioctl(LOGFLUSHED)` — an ioctl on
  `/dev/oslog`, off every boot path;
- `create`'s 0 lands in `kernel_firehose_addr`, which `__firehose_allocate` (`log.c:580`) tests for
  before using, and which has **no caller in the tree at all** — it is the userspace half of the
  interface.

Two of the four therefore coincide with a branch the OSS source already contains and the other two
are unreachable in this image. That is a claim about safety, not about correctness.

## A number in this log that was wrong

The `RELEASE` boot-path count above was first written as **7 of 72**, and 7 was the number of rows
visible in a `tail -20` of `stub_reach.py`'s output. The tool prints the count in a header line and
then up to `--list` (15 by default) rows nearest the entry first, so truncating the output from the
*top* removes the rows with the smallest distance and leaves the count unread. The true figure is
**14**, measured again in experiment-163 by rebuilding this exact image, and the seven rows that a
`tail` showed are the seven *furthest* ones. `--list 40` is the fix, and the header is the number.

This is the eighth instance of the project's measurement-defect class (memory:
`mi4-measurement-defects`): not a wrong tool, but a right tool read through a truncation.

## Verified, not assumed

- **`build_xnu_arm_layer.sh` unchanged**: 32 of 32, 445 undefined symbols — the same two numbers,
  and the layer is the one other build that puts `shims_arm` on its include path.
- **`libkern/c++/OSKext.cpp` is the third file that includes this header** (`:36`) and still
  compiles: 82 of 83, unchanged. The added declarations are inert there.
- **The two link routes still agree**: 72 by `nm -u` on the merged relocatable link and 72 by
  `measure_link.sh`. `STAGE90_BOOT`: 229 and 229.
- **The remote files are quoted, not paraphrased**: `OS_FIREHOSE_SPI_VERSION`, the chunk-count
  defines and the `#ifdef KERNEL` block above are read out of the raw files at the two tags named,
  and `libkern/firehose/` in `apple-oss-distributions/xnu` at `main` still ships headers only —
  the implementation is not published under `xnu` in any release.

## What this does not settle

The `#ifdef KERNEL` block gives the four functions' *signatures* and, for `create`, the shape of the
contract (`size` in, buffer out, and the caller's own `kmem_alloc`'d address already in hand). It
does not give the buffer's invariants. Those are in `firehose_buffer.c`, which is a real port:
1188 lines that include `mach/vm_statistics.h`, define their own `dispatch_lock`,
`dispatch_compiler_barrier` and `_dispatch_wait_until`, and keep a whole `#ifndef KERNEL` half for
the logd side. Whether it compiles for `armv7-unknown-netbsd-eabi` freestanding is the next
measurement, and the answer is allowed to be no.

## Reproduce

```bash
./tools/build_xnu_arm_kernel.sh                  # C 612 of 615, C++ 82 of 83
./tools/measure_link.sh --keep-stubs             # 72 undefined, 72 stubs
./tools/stub_reach.py --from arm_init --list 40  # 14 of 72
./tools/build_xnu_arm_layer.sh                   # 32 of 32, 445 undefined

XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/out/xnu_arm_manifest_min.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh                # C 418 of 426, C++ 82 of 83
./tools/measure_link.sh --min --keep-stubs       # 229 undefined
./tools/stub_blockers.py --min                   # 27 boot-path stubs, from stub_reach.py

# the symbol diff this stage's two lists are read off
arm-none-eabi-ld -r -o /tmp/xnu_all_objs.o out/xnu_kernel_obj/*.o out/xnu_asm_obj/*.o
arm-none-eabi-nm -u /tmp/xnu_all_objs.o | awk '{print $2}' | sort -u | wc -l   # 72
```

The published files, if they move:

```bash
curl -sSfL https://raw.githubusercontent.com/apple-oss-distributions/libdispatch/libdispatch-913.30.4/os/firehose_buffer_private.h
curl -sSfL https://raw.githubusercontent.com/apple-oss-distributions/libdispatch/main/os/firehose_buffer_private.h
```
