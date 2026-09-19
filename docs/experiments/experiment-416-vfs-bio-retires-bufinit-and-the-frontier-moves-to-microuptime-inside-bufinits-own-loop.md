# Experiment 416 — `bsd/vfs/vfs_bio.c`: `bufinit` retires, and the frontier moves to `microuptime` inside `bufinit`'s own header loop

**Step:** one object linked — `bsd_vfs_vfs_bio.o`, the pool's only definer of `bufinit`
(`bsd/vfs/vfs_bio.c:1962`) and therefore of 415's stop — appended to `LINK_OBJS` after
`bsd_kern_kern_descrip.o`.

**Prediction:** `stub_hit=microuptime`, caller key = the linked address of `bufinit + 0x158`.
**Measured:** exactly that — `xnu_entry_stub_caller_v=0x801A2A78`, and `bufinit` linked at `0x801A2920`,
so `0x801A2A78 = bufinit + 0x158` to the byte. `abort_entries=0`, `checks=5` / `failures=0`, no
`exception:`, no `panic`.

    line 3938: xnu_entry_stub_caller_v=0x801a2a78
    line 3942: xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=microuptime
    (no `exception:`, no `panic`; log ends `No errors detected`)

## The step's effect

    21 resolved (21 function, 0 storage) / 15 added (15 function, 0 storage)
      resolved  bufinit and the buffer cache's public face — buf_bawrite, buf_bdwrite, buf_blkno,
                buf_bread, buf_breadn, buf_brelse, buf_bwrite, buf_count, buf_dataptr, buf_device,
                buf_error, buf_flushdirtyblks, buf_getblk, buf_invalidateblks,
                buf_kernel_addrperm_addr, buf_markaged, buf_resid, buf_vnode,
                bufattr_markquickcomplete, count_busy_buffers
      added     VNOP_BWRITE, VNOP_STRATEGY, cluster_bp, cluster_init, cpx_synthetic_offset_for_iv,
                cpx_use_offset_for_iv, disk_conditioner_delay, ubc_blktooff, ubc_create_upl_kernel,
                ubc_upl_abort, ubc_upl_commit_range, ubc_upl_map, ubc_upl_unmap, vnode_waitforwrites,
                vnode_writedone

    799 -> 793 stub names, 677 -> 671 function, 122 -> 122 storage

All three counts are exactly as the tool predicted.

## The reading: the first stub is not the one this step creates — and it turns on one loop

`bufinit` is 0x46C bytes, and its call sites in address order come out of the object as `hashinit`
(+0x7C, real since 410), `bzero` (+0x118), **`microuptime` (+0x154, a stub, record 340)**, `panic` ×6,
the four `lck_*_init` calls, **`cluster_init` (+0x398, the stub *this step creates*)**, `_consume_printf_args`,
`zinit` ×2, `zone_change` ×2, `kernel_thread_start`, `thread_deallocate`,
`vm_set_buffer_cleanup_callout`.

So this step has 342/407's shape — **a stop it creates** — and the two candidate stops are 0x244 bytes
apart in the same function. Which one the run reaches is decided by whether this loop executes:

    for (i = 0; i < max_nbuf_headers; i++) { ... bp->b_timestamp = buf_timestamp(); ... }

`buf_timestamp()` is a `static inline` in vfs_bio.c, so its `microuptime` call lands **inside** `bufinit`;
that is the `bl` at object `+0x154`. The disassembly guards the loop with `cmp r0, #1 / blt +0x1E0` on
`max_nbuf_headers`, so a zero would skip past it and leave `cluster_init` as the stop instead.

**It is not zero, and the evidence was already on the device.** `bsd_startupearly()` runs at the head of
`bsd_bufferinit`, *before* the tail call into `bufinit`, and its body sets
`max_nbuf_headers = atop_kernel(sane_size / 50)` (clamped to 16384, floored at `CONFIG_MIN_NBUF`). The run
reached `bufinit` with no panic, and both `bsd_startupearly`'s and `bsd_bufferinit`'s `kmem_suballoc`
failure paths panic — so the whole of `bsd_startupearly` ran, which is the same thing as saying
`max_nbuf_headers` is nonzero. **A reading of the previous run's success is what decided which of two
candidate stops this one would hit.**

**Falsifiers, named in advance and all silent:** a stop at `cluster_init` (above — the shape this step
creates, and the one that would have meant the loop was skipped); a stop inside `hashinit` or
`bsd_startupearly`; a `panic` — `bufinit` has six, the reachable ones being the `iobuffer_mtxp`/`buf_mtxp`
NULL checks after `lck_mtx_alloc_init`; a stop at `ubc_init` (`0x8003B1B8`), which would mean `bufinit` had
returned cleanly, past every stub in it.

Both lists of the walk were read after the link, as a standing rule since 413: the straight-line answer
was `microuptime`, and the guarded list's entries are panic paths (`hashinit`'s, `__MALLOC`'s,
`bufinit+0x1a4`, `bufinit+0x1d8`) — none of them a stop.

## Layout

    stubnames    793 records (671 function, 122 storage)
    text size    1909600 (0x1D23C0)   — 415's 1884768 + 0x2610
    image bytes  2026804 (0x1EED34)   — 415's 2010252 + 0x16528
    bss          0x801EED40 .. 0x8022C218 (251096 bytes)
    layout       args +2285568 (0x8022E000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     1916392 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 df0b53a1bb15749d08615702d51bfecb9f2d9846ea16e0476891bc88106a003e

The run's markers agree to the byte: `image_bytes=0x001EED34`, `bss_start=0x801EED40`,
`bss_end=0x8022C218`, `args_pa=0x8022E000`, `checksum=0x90402229`.

## Where the frontier is now

`microuptime`, record 340 of 793, defined by **`bsd/kern/kern_time.c`**
(`out/xnu_kernel_obj/bsd_kern_kern_time.o`) — the BSD timekeeping face, and the first stub the walk has
reached inside a function *this* step made real rather than in `bsd_init`'s own statement list. Behind it in
the same function: `cluster_init` (created here), then `bufinit`'s zone and thread setup. On `bsd_init`'s
own line, still ahead: `ubc_init`, `vfsinit`, `proc_uuid_policy_init`, `mcache_init`, `mbinit`,
`net_str_id_init`, `eventhandler_init`, `aio_init`, `pipeinit`, … and eventually `vfs_mountroot`.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301805 bytes, ending `No errors detected`. Device returned to
Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
