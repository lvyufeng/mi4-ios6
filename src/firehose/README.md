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

The one file this project already ships for this seam — `src/shims_arm/os/firehose_buffer_private.h`,
added by experiment 162 — is what lets `bsd/kern/subr_log.c` compile today.

## The port, and its status

Experiment 256 measured the blocker: the source includes `<os/atomic_private.h>`, which is not in this
tree, not in libplatform, and not in `libdispatch-913` — it is in `xnu` at `main`. `portinc/` is that
newer tree's `libkern/os/` atomics surface plus the newer firehose headers, placed where `<os/...>`
resolves ahead of the tree's copies. `tools/build_xnu_arm_kernel.sh` compiles this directory in its
`FIREHOSE_SOURCES` block, with the same flags as everything else it builds.

`firehose_kernel_config.c` is the one file here that is **not** Apple's: the two configuration
variables the ported code reads (`__firehose_buffer_kernel_chunk_count`,
`__firehose_num_kernel_io_pages`) are defined in Apple's closed `libfirehose_kernel` library and
nowhere published, so this project supplies them with the values Apple's own header publishes as the
defaults. See that file's comment - the chunk count is not free, because `oslog_init` sized the
allocation for it.

Status: **it compiles and it runs.** Experiment 257 linked the object (4096 bytes of text, 12
references) into the entry image — `__firehose_buffer_create` resolved, six kernel-side names added —
and the device executed it: the stop moved from `__firehose_buffer_create` to `__firehose_allocate`,
*i.e.* from a missing symbol to a call *inside* this implementation. Experiment 258 finished the
kernel side - the four functions are defined in the tree, by `libkern/os/log.c`, and the two
variables by `firehose_kernel_config.c` - and `oslog_init` then **returned**: the stop is past the
firehose entirely, at `telemetry_init` in `kernel_bootstrap`. See
`docs/experiments/experiment-257-...` and `-258-...` for the logs.

`portinc/` is port material, unmodified, from `apple-oss-distributions/xnu` at `main`.

Licence: the sources here carry Apple's Apache-2.0 header; they are redistributed unmodified.
