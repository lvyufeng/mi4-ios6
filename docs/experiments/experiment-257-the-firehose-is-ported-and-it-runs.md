# Experiment 257 — The Firehose Is Ported and It Runs: `__firehose_buffer_create` Is Real

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change: a port, not a link

Experiments 254 and 255 stopped at `__firehose_buffer_create`, and 256 measured that the symbol has no
implementation anywhere in `external/xnu-4570.1.46` — `libkern/firehose/Makefile` ships headers with
`KERNELFILES =` empty. This step compiles Apple's own implementation, from
`apple-oss-distributions/libdispatch`, for `armv7-unknown-netbsd-eabi` freestanding, and links it into
the entry image. **It is the first time this project has built a component Apple ships outside the
tree.**

Three pieces:

1. **`stages/stage90/firehose/`** — `firehose_buffer.c` (46930 bytes, Apache-2.0, unmodified) and its
   four headers, brought in by 256 with a README recording provenance.
2. **`stages/stage90/firehose/portinc/`** — the headers a *newer* XNU ships and 4570 does not, placed
   where they shadow the tree's: `os/` (`atomic.h`, `atomic_private.h`, `atomic_private_arch.h`,
   `atomic_private_impl.h`, `base.h`, `base_private.h`, `cpp_util.h`, `firehose_buffer_private.h`) and
   the newer `firehose_types_private.h`, `chunk_private.h`, `ioctl_private.h`, `tracepoint_private.h`,
   `private.h`. 256's measurement is the reason they are needed: `os/atomic_private.h` is not in this
   tree, not in libplatform, and not in libdispatch-913 — it is in `xnu` at `main`.
3. **`tools/build_xnu_arm_kernel.sh`** — a `FIREHOSE_SOURCES` block compiled with the loop's own
   `CC_ARGS`/`FORCE_INCLUDES`/`DEFINES`, the way `RUNTIME_SOURCES` (`xnu_aeabi_runtime.c`) already is,
   because a second flag list is this project's most repeated defect. Its include roots go **first**:
   ordered after the tree's, `$XNU/libkern/os/base.h` shadows the ported `os/base.h` and `OS_OPTIONS`
   disappears — which is exactly how the first attempt failed.

The object: **4096 bytes of text**, 12 references, defining `__firehose_buffer_create`,
`__firehose_merge_updates`, `__firehose_buffer_tracepoint_reserve`/`_flush`,
`firehose_buffer_create`, `firehose_buffer_tracepoint_reserve_slow` and
`__firehose_kernel_configuration_valid`.

## The prediction

`__firehose_buffer_create` is 60 bytes and calls exactly one thing on the first-call path — through
`firehose_buffer_create`, which is compiled in the same object:

```
80094e48  ldr  r0, [r5]            ; the cached buffer at 0x800f1144
80094e50  beq  +0x44               ; first call -> the creation path
80094e80  bl   firehose_buffer_create     <- inside the ported object
80094e64  ldrb r1, [..]            ; __firehose_buffer_kernel_chunk_count, read as DATA
```

and `firehose_buffer_create`'s only call is `__firehose_allocate`, which the link has just made a stub:

```
80094118  bl __firehose_allocate   <- STUB (one of the six names this step added)
```

**The prediction: `stub_hit=__firehose_allocate`, caller `firehose_buffer_create+0x28`.**

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000040
 xnu_entry_kv_in_dram=0x00000040
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=__firehose_allocate
 xnu_entry_stub_caller=0x8009411c

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x40` (64 bytes
= 30 + 34), `0x8009411c` resolves to `firehose_buffer_create+0x28`, and its `caller - 4` is
`80094118: bl 80095a30 <__firehose_allocate>`.

**The ported code ran.** The stop did not move to "the next missing symbol in XNU"; it moved *inside
Apple's firehose implementation*, which is now linked into the image and executing — from
`oslog_init`'s call, through `__firehose_buffer_create`, into `firehose_buffer_create`, to the first
kernel-side callback that implementation asks for.

### A stub that would have been silent, and is not

The link resolved 2 and added 6, and one of the six is not like the others:

```
resolved  __firehose_buffer_create  __firehose_merge_updates
added     __firehose_allocate  __firehose_buffer_kernel_chunk_count
          __firehose_buffer_push_to_logd  __firehose_critical_region_enter
          __firehose_critical_region_leave  __firehose_num_kernel_io_pages
```

`__firehose_buffer_kernel_chunk_count` is **read as data** — `ldrb r1, [r1]` with the symbol's own
address — not called, so a stub at that address would not have stopped the run: it would have loaded
the first byte of the stub's instruction encoding, shifted it left twelve bits, and handed
`oslog_init` a garbage buffer size. The four `__firehose_*` names Apple's `os/firehose_buffer_private.h`
declares as "implemented by the kernel" are the ones that must be *functions*; this one is a byte of
configuration, and its stand-in is wrong in a way no stub report can see. It is the `mi4-stand-in-size-is-not-value`
shape in a new place: a stand-in can be the right size and the wrong value, and this one will not even
announce itself.

## An incident, recorded because it cost a rebuild

Two `./tools/build_xnu_arm_kernel.sh --limit 1` runs were used to check the port quickly. The script
**truncates its output directory at the start** (deliberately — "a build script that appends leaves the
previous run's failures in the list"), so `--limit 1` leaves exactly one object behind: `out/xnu_kernel_obj`
went from 695 objects to 0, and the entry build then failed with
`no …/osfmk_arm_arm_init.o - run ./tools/build_xnu_arm_kernel.sh first`. The recovery was the full
build, which reproduces the same 612 of 615 C files (3 failures, all previously recorded) and 83 of 83
C++ files. **`--limit` is not a dry run**; it is a build of one file that deletes the rest. Adding the
firehose source touched no tree object, so nothing else was at risk.

## Cost

| | exp-255 | now |
| --- | --- | --- |
| undefined | 595 | **599** (2 resolved, 6 added) |
| function stubs | 514 | **518** |
| storage stubs | 81 | **81** |
| entry text | 692865 B | **697217 B** (+4352) |
| entry image | 802176 B | **802176 B**, unchanged |
| entry `.bss` / layout / headroom | 0x800f28c8 / args +999424 / 1103672 B | **unchanged** |
| payload text | 1294394 B | **1294394 B**, unchanged |
| ported object | — | 4096 B of text, 12 references |

The object's 4096 bytes plus churn is +4352, and it fitted inside the linker script's alignment
padding: nothing moved, and the payload was unchanged because its embedded `.bin` kept its size.

## What is next

The six added names are the kernel's side of the firehose interface, and the four that are *functions*
(`__firehose_allocate`, `__firehose_buffer_push_to_logd`, `__firehose_critical_region_enter`/`_leave`)
plus `__firehose_num_kernel_io_pages` are what the ported code will call next. They are small — the
stub bodies are 18 bytes — and in a newer XNU they live in `osfmk/kern/firehose.c`, which does not
exist in 4570: **the same port problem as this step, one layer down**, with the same remedy
(`FIREHOSE_SOURCES` takes a list, and a second source goes in the same place). `libkern/os/log.c` is
in the tree and 162 recorded that `__firehose_allocate` (`:580`) lives there, so at least that one may
be answerable from the tarball rather than from a port.

Reading the ported object's own reference list is the way to predict it: `__firehose_allocate` is
already known to be the first stop, because `firehose_buffer_create` has exactly one call.

## Reproduce

```bash
# the port
ls stages/stage90/firehose/ stages/stage90/firehose/portinc/ stages/stage90/firehose/portinc/os/
grep -n 'FIREHOSE_SOURCES' -A22 tools/build_xnu_arm_kernel.sh | head -30
./tools/build_xnu_arm_kernel.sh --blockers 5      # NOT --limit: that deletes the rest of the objects
ls -l out/xnu_firehose_obj/firehose_buffer.o

# what it defines, and what it needs
arm-none-eabi-nm -S -P --defined-only --extern-only out/xnu_firehose_obj/firehose_buffer.o
arm-none-eabi-nm -u out/xnu_firehose_obj/firehose_buffer.o

# the link, and the prediction
grep -n 'FIREHOSE_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
comm -13 <(sort /tmp/undef_256.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the added 6
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<__firehose_buffer_create>:/{f=1} f{print} f&&/^$/{exit}'
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<firehose_buffer_create>:/{f=1} f{print} f&&/^$/{exit}' | grep '\tbl\t'

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8009411c      # -> firehose_buffer_create+0x28
```
