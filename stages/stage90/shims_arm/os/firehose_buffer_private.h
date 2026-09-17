#ifndef MI4IOS6_SHIM_OS_FIREHOSE_BUFFER_PRIVATE_H
#define MI4IOS6_SHIM_OS_FIREHOSE_BUFFER_PRIVATE_H
/*
 * `os/firehose_buffer_private.h` is not in the xnu-4570.1.46 tarball, and this is the one shim in
 * the tree whose contents are *derived* rather than chosen.
 *
 * Three files include it: `libkern/os/log.c:29`, `bsd/kern/subr_log.c:76` and
 * `libkern/c++/OSKext.cpp:36`. What they take from it is a type set that IS published, in
 * `libkern/firehose/`:
 *
 *     firehose_chunk_t          chunk_private.h:65
 *     firehose_stream_t         firehose_types_private.h:69
 *     firehose_buffer_t         firehose_types_private.h:263
 *     firehose_tracepoint_id_t  tracepoint_private.h
 *
 * and `libkern/firehose/Makefile:37-42` exports all four (`EXPORT_MI_DIR = firehose`). So this is
 * the forwarding header the missing one most plausibly is, written from the names its users need
 * rather than from a guess at its contents.
 *
 * The check that decides whether that is right is the build: if `log.c` and `subr_log.c` compile
 * and the symbols they define appear, the forwarding is sufficient. If something else in it is
 * needed, those files keep failing and say so.
 */
#include <firehose/firehose_types_private.h>
#include <firehose/chunk_private.h>
#include <firehose/tracepoint_private.h>
#include <firehose/ioctl_private.h>
#include <firehose/private.h>
#endif
