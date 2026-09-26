/*
 * The kernel's two firehose configuration variables. **This file is not Apple's** - everything else
 * in this directory is (see README.md); this one is this project's, and this comment says exactly
 * why it has to exist.
 *
 * `firehose_buffer.c`, beside it, reads both variables and defines neither:
 *
 *     firehose_buffer.c:356   size = FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT * FIREHOSE_CHUNK_SIZE;
 *     firehose_buffer.c:384   const uint16_t num_kernel_io_pages = __firehose_num_kernel_io_pages;
 *
 * and `portinc/os/firehose_buffer_private.h:66` is what makes the first one a read of a variable:
 *
 *     #define FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT __firehose_buffer_kernel_chunk_count
 *
 * Neither definition is published anywhere. They live in Apple's closed kernel firehose library -
 * the one `libfirehose_kernel` links and the tarball does not ship (`libkern/firehose/Makefile` has
 * `KERNELFILES =` empty, the measurement that made experiment 256 a port rather than another link).
 * They are not in `apple-oss-distributions/xnu` at `main` either: that whole tree has no
 * `osfmk/kern/firehose.c`. So supplying them is part of the port.
 *
 * **The chunk count is not a free choice.** `bsd/kern/subr_log.c:863` sizes the kernel's firehose
 * allocation from `FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT` - compiled there against this project's shim
 * (`src/shims_arm/os/firehose_buffer_private.h:54`, the constant `16`, taken from
 * `libdispatch`'s `os/firehose_buffer_private.h:43` at `libdispatch-913.30.4`) - and
 * `firehose_buffer_create` then lays out the buffer's header for the value *this* variable holds.
 * Experiment 255 measured that allocation on the device: **73728 bytes = 65536 + 2 guard pages**,
 * i.e. 16 chunks of `FIREHOSE_CHUNK_SIZE` (`chunk_private.h:35`, 4096) plus
 * `KMA_GUARD_FIRST | KMA_GUARD_LAST`. A different value here would make the header describe a ring
 * the allocation cannot hold, which is a write past the end of a kernel allocation.
 *
 * Both generations of the header agree, and that is the whole reason 16 may be used:
 *
 *     libdispatch-913.30.4   #define FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT     16
 *     xnu at main            FIREHOSE_BUFFER_KERNEL_MIN_CHUNK_COUNT        16
 *                            FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT  MIN
 *                            FIREHOSE_BUFFER_KERNEL_DEFAULT_IO_PAGES     8
 *
 * so the values below are read from those macros rather than written out a second time - the newer
 * header is on this file's include path first (the `FIREHOSE_SOURCES` block in
 * `tools/build_xnu_arm_kernel.sh`), which is also what makes `<os/firehose_buffer_private.h>` resolve
 * to the ported copy and not to the 4570 tree's.
 *
 * `__firehose_num_kernel_io_pages` only divides the ring: `firehose_buffer_create` sets `fbs_io_bank`
 * to it and `fbs_mem_bank` to `FIREHOSE_BUFFER_CHUNK_PREALLOCATED_COUNT - io_pages` (15 - 8 = 7),
 * and Apple's validator `__firehose_kernel_configuration_valid` is what a newer kernel uses to reject
 * a boot-arg pair. There is no boot-arg here, so the published defaults are the whole answer.
 *
 * Both are `uint8_t` - the header declares them that way and `firehose_buffer_create` reads them with
 * `ldrb`. That is why they cannot be left as stubs: a stub is read as *data*, not called, so it would
 * not stop the run - it would shift the stub's own instruction encoding and hand the buffer a garbage
 * size. Experiment 257 recorded the shape (`0x40 << 12`); this file is what removes it.
 */

/* `firehose_buffer.c:37` does the same thing, and for the same reason: the firehose headers are
 * gated on `OS_FIREHOSE_SPI`, which Apple's build supplies as a define
 * (`libfirehose_kernel.xcconfig`: `GCC_PREPROCESSOR_DEFINITIONS = ... KERNEL=1 ...` and
 * `COPY_HEADERS_UNIFDEF_FLAGS = -DKERNEL=1 -DOS_FIREHOSE_SPI=1`). Defining it in the file keeps this
 * port's two sources configured identically. */
#define OS_FIREHOSE_SPI 1

#include <stdbool.h>
#include <stdint.h>
#include <vm/vm_kern.h>             /* vm_offset_t, which the header's declarations use */

/* The four the header's own declarations are written in terms of, in the order the port's other
 * source includes them (`firehose_buffer.c:93-95`): `firehose_buffer_t`, `firehose_tracepoint_t` and
 * `firehose_tracepoint_id_u`, all of which `os/firehose_buffer_private.h` uses and none of which it
 * defines. Their absence is three compile errors, not one. */
#include <firehose_types_private.h>
#include <tracepoint_private.h>
#include <chunk_private.h>
#include <os/firehose_buffer_private.h>

uint8_t __firehose_buffer_kernel_chunk_count = FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT;
uint8_t __firehose_num_kernel_io_pages = FIREHOSE_BUFFER_KERNEL_DEFAULT_IO_PAGES;
