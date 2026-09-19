# Experiment 445 — the trace names the silent stop as a block, and drops the record that names its site

**Status: measured. This run was taken as a diagnostic before its own doc existed, so it is recorded as
a *measurement* and not as a test of a prediction — a weaker claim, stated here rather than implied.
The falsifiable claim it leaves behind is in the last section, and experiment 446 tests it.**

## Why: 444's run was silent, and the project has two instruments that report without a trap

444 moved the device tree below `topOfKernelData` and the run stopped reporting entirely: the payload's
ladder, the jump line, and then nothing, where 443 had written 112 lines. Both instrument headers in
`xnu_arm_boot/` have said for a long time what that means — "the payload's whole ladder completes, the
jump line is written, and then no third line. No `stub_hit`, no `exception: <vector>`, no epilogue …
silence means the CPU never reached reporting code: a loop, a spin, or a block that never wakes"
(`entry_checkpoint.c:8-14`, `entry_trace.c:17-20`).

Of those three shapes, one has an instrument that was built for it and is still wired:
`entry_trace.c`'s `thread_block` wrapper, which is **terminal by design** — "the epilogue runs instead
of the block, so a block that would have hung becomes a line in the log" — and which reports the block's
**caller** and its **continuation**, beside every `kalloc_canblock` (requested size, size served,
`canblock`, the caller, **the returned pointer**, and `vm_page_free_count` read at that moment) and every
`kernel_memory_allocate` return value. It changes no source, so it was run as it stood.

## What was run, and the build it was run from

```
STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1 ./build_entry.sh     # stages/stage90/xnu_arm_boot
STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh                 # stages/stage90
```

The entry image grew by the wrappers and nothing else: `.text` 5000512 → **5001344** (+832), entry bin
still **5208596** bytes, `.bss` unmoved at `0x804f7a40 .. 0x80548a18`, layout unchanged (`args
+5545984, topOfKernelData +7340032, tree +7208960, window 0x01000000`), the two addresses XNU's own
boot path writes still `0x80548a08`/`0x80548a0c`. The five wrappers are in the image and the call sites
are routed to them: `__wrap_thread_block` `0x80453ae8` (**108** `bl` sites), `__wrap_vm_page_wait`
`0x80453ab8` (19), `__wrap_kalloc_canblock` `0x8045393c` (239), `__wrap_kernel_memory_allocate`
`0x80453a34`, `__wrap_lck_grp_alloc_init` `0x804539e4`. Payload `out/stage90/stage90-qcdt.img`,
**8228864** bytes, sha256 `b158c9f51a3926316433337a8f6c537602dfada9868c69b9cb0c10258d43108e`.

## The measurement: the silent stop is a `thread_block`

The log is **309832 bytes / 4254 lines** — 322 more than 444's, and its first 3931 are the payload's,
unchanged. From there:

```
MI4IOS6_STAGE90_XNU real XNU entry: t268: thread_block was called
 xnu_entry_kv_written=0x00001fea
 xnu_entry_kv_in_dram=0x00001fea
 xnu_entry_kv_dropped=0x00003f8d
 xnu_entry_why=0x804c2f3c
 xnu_entry_why_byte=0x00000074          <- 't', the first byte of the reason string
 ...
 xnu_entry_stub_caller_w0=0x36327420    <- "t2 8" of "t268_t..."
 xnu_entry_stub_caller_w1=0x6d6b5f38    <- "_8km"
```

**The stop 444 could not name is a `thread_block`.** The wrapper ran the epilogue in place of the block,
which is the one thing it was built to do, and the reading is a *name* where 444 had silence. No
`exception:` line and no `stub_hit=` line appears in the 322 lines the entry image wrote, so the frontier
in this image is neither a fault nor a missing symbol.

**And the record that would name its call site was refused.** `t268_thread_block_caller` and
`t268_thread_block_continuation` are **not in the log**. `xnu_entry_kv_dropped=0x3f8d` = **16269**
records were dropped against `xnu_entry_kv_written=0x1fea` = 8170 bytes — the 8192-byte buffer holds the
**first 280** records of the boot and everything after them, including the terminal wrapper's two, was
refused. 269 made the refusal *visible* with a counter, and the counter is doing its job here; what
nothing made is the terminal record **matter more** than the 280 that got in. The instrument's most
important record is written last, into a collector that is full by construction.

## What the 280 records that did survive say

They are the boot's earliest allocations. Resolved against the image the device ran:

| `t268_*_caller` | records | symbol + offset |
| --- | --- | --- |
| `t268_lckgrp_caller` | 6 x 1 | `kalloc_init+0x1e8`, `cs_init+0x34`, `stackshot_init+0x28`, `mac_policy_init+0x70`, `clock_config+0x34`, `ntp_init+0x4c` |
| `t268_kalloc_caller` | 6 / 5 / 5 / 4 / 3 / 2 | `lck_grp_alloc_init+0x2c`, `lck_grp_attr_alloc_init+0x24`, `ledger_entry_add+0x88`, `lck_attr_alloc_init+0x24`, `ivac_alloc+0x54`, `ledger_set_callback+0x60` |
| `t268_kma_caller` | 16 / 2 / 1 / 1 / 1 | `zalloc_internal+0x3cc`, `ltable_init+0x6c`, `zone_init+0xec`, `vm_page_more_fictitious+0x80`, `waitq_bootstrap+0x114` |

Sizes: `kalloc` requests 4, 8, 12, 0x24, 0x68, 0x100, 0x108, 0x3000; the answers 8, 16, 0x28, 0x70, 0x100,
0x120, 0x1b8, 0x3000 — so the zone ladder is doing exactly what 268 read, 264-byte requests served as
288. **Every `kernel_memory_allocate` in the window returned 0** (`KERN_SUCCESS`): 13 x 4096, 5 x 8192,
and one each of 12288, 16384, 20480.

Two things follow.

**First, the frontier has moved on from 439.** 439 stopped at `stub_hit=bpf_init`; this image writes no
`stub_hit=` line at all before it blocks, so the boot passed that point and reached a block after **at
least 16269 further allocation records**.

**Second, a correction to 444.** 444's doc says the free region was "for practical purposes, gone" and
made memory exhaustion the likely shape of the silence. The device says otherwise in this window:
`vm_page_free_count` read **0x7aa = 1962 pages** at all thirty samples, and `t268_vm_page_wait_caller`
does not appear at all — `vm_page_wait` was never called while the buffer was live. 1962 free pages is
≈ 8 MB, where 444's arithmetic gives `avail_end - avail_start = 0x81000000 - 0x8070A000 = 0x1F6000` =
502 pages. **The two numbers disagree by a factor of four, and the one taken from the device is the one
to keep.** The derivation is not obviously wrong (`arm_vm_init.c:399-400` does compute `avail_start =
cpu_ttep + ARM_PGBYTES*6` and `avail_end = gPhysBase + mem_size`), so the discrepancy is its own open
reading — the candidates being that `vm_page_free_count` at this point counts pages from a region that
is not `[avail_start, avail_end)`, or that the region was extended after `arm_vm_init` computed it. What
this run *does* settle is the direction: **"the allocator had nothing left" is not supported by the only
number taken from the machine**, and the constant value across thirty samples is 268's reading again (it
read 0x3ba in all six), which is itself the reason to be careful with it rather than to lean on it.

## The frontier this leaves: a block, at an unknown site, after a long boot

With the terminal record dropped, the run cannot say *where* the block is. What it bounds is the
interval: the boot allocates at least 16269 further records after the 280 that fit, so the block is a
long way past `waitq_bootstrap` and `ipc_table_init` — which is consistent with the boot thread's own
path through `bsd_init`, and says nothing at all about whether the block is a *wait that should never
have happened* or a *wait with nothing to wake it*.

That distinction is the whole question now, and it is one record away. `entry_stubs.c` already has the
pattern for a record that must survive the collector: the abort handler's `g_abort_entries` and
`g_first_abort_*` live in `.bss` and are written by the epilogue **outside** the buffer
(`entry_stubs.c:1067-1080`). The same three lines answer this: a `g_block_*` slot pair written by the
terminal wrapper, reported beside the abort keys. It is a small change to `entry_trace.c` and
`entry_stubs.c`, and its run is 446.

**The falsifiable claim 446 is written against**: the report will carry a `t268_block_caller` that
resolves, against the image the device ran, to a symbol with a `bl thread_block` at that site. Which
symbol decides the shape, and the two branches are named in advance — if it is `vm_page_wait`'s path
(`zalloc_internal`, `vm_page_wait+…`), the block is an allocator with nothing to give and 444's
hypothesis survives in a form this run did not support; if it is the boot thread's own wait path
(`kernel_bootstrap_thread`, `bsd_init`'s tail, `assert_wait`/`thread_block` in a real service), then the
machine is waiting for something that no device in this image can ever deliver, and the missing piece is
the one the standing list has owed since the timer was first named — `ml_init_timebase` and an MSM8974
`tbd_ops_t` over the GPT at `0xf9020000`.

Safety, unchanged and measured: `fastboot boot` only, never flash; both gates; 25 x
`persistent_write_attempted=0x00000000`, 87 x `failure_mask=0x00000000`, 0 x `stub_hit=`, 0 x
`exception:`; the hardware watchdog armed and counting before the jump, and the device back in Android
on its own, log ending `No errors detected`.
