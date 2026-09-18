# Firehose port material

The implementation of `__firehose_buffer_create` and `__firehose_merge_updates` is **not in
`external/xnu-4570.1.46`** — that tree ships the `libkern/firehose/` headers with `KERNELFILES =`
empty. `bsd/kern/subr_log.c` calls `__firehose_buffer_create` (`:874`) and nothing in the tree
defines it, so experiment 255 stopped there.

The files here are that implementation, ported rather than written:

| file | origin |
| --- | --- |
| `firehose_buffer.c` | `apple-oss-distributions/libdispatch`, `src/firehose/firehose_buffer.c`, Apache-2.0 (the licence header is the file's own, unmodified) |
| `firehose_buffer_internal.h`, `firehose_buffer_private.h`, `firehose_inline_internal.h`, `firehose_internal.h` | the same release's firehose headers |

The one file this project already ships for this seam — `stages/stage90/shims_arm/os/firehose_buffer_private.h`,
added by experiment 162 — is what lets `bsd/kern/subr_log.c` compile today.

Status: **it does not compile yet**, and the blocker is a header from a *newer XNU* than the one this
project builds. See `docs/experiments/experiment-256-...` for the measurement. Nothing in this
directory is linked into any image.

Licence: the sources here carry Apple's Apache-2.0 header; they are redistributed unmodified.
