#ifndef MI4IOS6_SHIM_OS_FIREHOSE_BUFFER_PRIVATE_H
#define MI4IOS6_SHIM_OS_FIREHOSE_BUFFER_PRIVATE_H
/*
 * `os/firehose_buffer_private.h` is not in the xnu-4570.1.46 tarball, and until now this shim stood
 * in for it by forwarding the type set its three users need (`libkern/os/log.c:29`,
 * `bsd/kern/subr_log.c:76`, `libkern/c++/OSKext.cpp:36`) out of the published `libkern/firehose/`:
 *
 *     firehose_chunk_t          chunk_private.h:65
 *     firehose_stream_t         firehose_types_private.h:69
 *     firehose_buffer_t         firehose_types_private.h:263
 *     firehose_tracepoint_id_t  tracepoint_private.h
 *
 * That forwarding is still right, and the four declarations below are the rest of the same header.
 * It is not derived: **the header is published**, in a different component's tarball.
 *
 *     libdispatch / os / firehose_buffer_private.h
 *     apple-oss-distributions/libdispatch, tag libdispatch-913.30.4 (the 10.13 GM)
 *
 * Its `#ifdef KERNEL` block is `// implemented by the kernel` (four functions that `xnu` supplies,
 * and declares itself in `libkern/os/firehose.h`) followed by `// exported for the kernel`: the four
 * below. Their signatures are the ones these two files call, argument for argument -
 * `log.c:426`, `log.c:462`, `subr_log.c:874`, `subr_log.c:813` - which is what identifies this as
 * the header rather than one of its relatives.
 *
 * Only these five things are taken from it. `xnu` already declares the other four in
 * `libkern/os/firehose.h`, and a second declaration of the same function is the "one value, two
 * definitions" shape this project has met sixteen times - here the two would agree today and drift
 * the first time either moved.
 *
 * The value, from the same file at the same tag:
 *
 *     FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT   16        (os/firehose_buffer_private.h:43)
 *
 * and it is corroborated by the next generation of the same header, which made the constant a
 * runtime variable and kept 16 as both the floor and the default:
 *
 *     FIREHOSE_BUFFER_KERNEL_MIN_CHUNK_COUNT      16
 *     FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT  FIREHOSE_BUFFER_KERNEL_MIN_CHUNK_COUNT
 *
 * Four of `log.c`'s and `subr_log.c`'s uses want only the product with `FIREHOSE_CHUNK_SIZE`
 * (`chunk_private.h:35`, 4096) - 65536 bytes, plus two guard pages, is what `oslog_init` allocates -
 * and the fifth, `log.c:591`, writes the boot chunk into chunk `COUNT - 1`, the last of the ring.
 * Every value >= 1 satisfies all five; 16 is the one Apple published.
 */
#include <stdbool.h>
#include <stdint.h>

#include <firehose/firehose_types_private.h>
#include <firehose/chunk_private.h>
#include <firehose/tracepoint_private.h>
#include <firehose/ioctl_private.h>
#include <firehose/private.h>

#define FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT 16

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

#endif
