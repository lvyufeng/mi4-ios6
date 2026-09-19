# Experiment 418 — `bsd/vfs/vfs_cluster.c`: `cluster_init` retires, and the stop is the buffer-clean thread 415's own step started

**Step:** one object linked — `bsd_vfs_vfs_cluster.o`, the pool's only definer of `cluster_init`
(`bsd/vfs/vfs_cluster.c:305`) and therefore of 417's stop — appended to `LINK_OBJS` after
`bsd_kern_kern_time.o`.

**Prediction:** `stub_hit=ubc_init`, caller key `0x8003B1B8` (`bsd_init + 0x7C4`).
**Measured:** `stub_hit=msleep0`, caller key `xnu_entry_stub_caller_v=0x801A590C` — **not a site in
`bsd_init` at all**. `abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

    line 3938: xnu_entry_stub_caller_v=0x801a590c
    line 3942: xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=msleep0

## The step's effect

     3 resolved (2 function, 1 storage) / 12 added (12 function, 0 storage)
      resolved  cluster_init, cluster_bp, and the **storage** stand-in `speculative_reads_disabled`
                (a `B` record) — which is why the storage column falls for the first time in this
                run of steps
      added     memory_object_control_uiomove, ubc_getobject, ubc_getsize, ubc_page_op,
                ubc_range_op, ubc_strict_uncached_IO, ubc_upl_abort_range, ubc_upl_pageinfo,
                vfs_flags, vnode_isswap, vnode_pageout, vnode_startwrite

    784 -> 793 stub names, 662 -> 672 function, 122 -> 121 storage

The counts were predicted before the build and are confirmed **after** it by arithmetic on the next
link's measured figures: 419's post-link stub names are 787 / 667 / 120, and 419's own measured
effect is 6 function names resolved and 1 added; 787 + 6 − 1 = 793, 667 + 6 − 1 = 672, 120 + 1 = 121.
The 418 link's `.text` and image figures were not captured separately — the 419 build replaced the
artifacts in `out/` — and this doc does not invent them. The run's own `xnu_entry_image_bytes` was
`0x001FADC0` (419's is `0x001FADD0`).

## The reading: the prediction was wrong, and nothing on the path was a stub

`cluster_init` is 0xD4 bytes and makes six calls — `lck_grp_attr_alloc_init`, `lck_grp_alloc_init`,
`lck_attr_alloc_init`, `lck_mtx_alloc_init`, `lck_spin_init`, and `panic` behind the
`cl_transaction_mtxp == NULL` check — all real, all walked clean. So it returns, and **`bufinit` has
nothing left to stop on**: the calls after it in address order (`_consume_printf_args`, `zinit` ×2,
`zone_change` ×2, `kernel_thread_start` at `+0x438`, `thread_deallocate`,
`vm_set_buffer_cleanup_callout`) are every one real, and neither list of either walk names a stub.
The twelve names this step adds are the UBC/vnode face reached from `cluster_read`,
`cluster_write` and `cluster_pageout` — functions nothing in this image calls yet, so 342's trap is
checked and empty.

What the run actually stopped on is `msleep0`, and **the caller key says who called it**:
`0x801A590C` is `bcleanbuf_thread + 0x94`, and `bcleanbuf_thread` is linked at `0x801A5878`. That
thread does not exist because of this step; it is created one step earlier, by `bufinit` itself:

    bcleanbuf_thread_init():
        kernel_thread_start((thread_continue_t)bcleanbuf_thread, NULL, &thread);
        thread_deallocate(thread);

    bcleanbuf_thread():
        for (;;) {
            lck_mtx_lock_spin(buf_mtxp);
            while ((bp = TAILQ_FIRST(&bufqueues[BQ_LAUNDRY])) == NULL)
                (void) msleep0(&bufqueues[BQ_LAUNDRY], buf_mtxp, PRIBIO|PDROP, "blaundry", 0,
                               (bcleanbufcontinuation)bcleanbuf_thread);
            ...

`msleep0` is a stub in this image (416 and 417 did not retire it; 419 does), and the `bl` at
`+0x90` makes the site `bcleanbuf_thread + 0x94` — the key, to the byte. **The report therefore came
from a thread other than the boot thread**, and that is the finding: the walk had been reading
`bsd_init`'s compiled statement list as if it were the only thing executing, and `bufinit`'s
`kernel_thread_start` (a real call, retired at 416) put a second thread on the run queue. The first
stub the device reaches after that point is not on the boot thread's line at all.

**Falsifiers, named in advance and all silent:** a stop inside `cluster_init`, `kernel_thread_start`
or any of the other five real calls; a stop on one of the twelve added names; a `panic` — `cluster_init`'s
`cl_transaction_mtxp` check and `bufinit`'s own `kernel_thread_start` failure check, which is the live
one now that it runs for real; a stop at `vfsinit` (`+0x7C8`, key `0x8003B1BC`), which would have meant
`ubc_init` had already been retired.

**The rule this step adds, and 419 is what proved it:** the block's own reading listed
`IOKitInitializeTime` — the statement between `bufinit`'s return and `ubc_init`, at `0x8003B1B0` —
as "real" and treated it as a pass-through. **"Real" is not "returns promptly."** A call the walk
calls real can block the calling thread for thirty seconds, and then the caller's statement list is
not what executes next. Checking a *call target* against the stub list is not checking what the call
does.

## Where the frontier is now

Not `ubc_init`, and not any stub: the boot thread had **yielded** before it, and 419 identifies the
one place it can have yielded — `IOKitInitializeTime` → `IOService::waitForService(resourceMatching("IORTC"), 30 s)`
— and shows that the wait cannot end at all, because this environment has no timer and no `IORTC`
publisher. This step's contribution to that finding is the switch itself: the new thread got the CPU,
and its first stub reported.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301801 bytes, ending `No errors detected`. Device returned
to Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
