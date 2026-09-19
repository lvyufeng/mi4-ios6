# Experiment 411 — `bsd/kern/tty.c`: `tty_init` retires and the frontier jumps 0x1E8 bytes down `bsd_init`'s statement list

**Step:** one object linked — `bsd_kern_tty.o`, the pool's only definer of `tty_init` (`bsd/kern/tty.c`)
and therefore of 410's stop — appended to `LINK_OBJS` after `bsd_kern_kern_subr.o`.

**Prediction:** `stub_hit=init_system_override`, caller key `0x8003ADA4`. **Measured:** exactly that,
`abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

    line 3935: xnu_entry_stub_caller_v=0x8003ada4
    line 3939: xnu_entry_abort_entries=0x00000000
    line 3970: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=init_system_override
    line 3975: No errors detected

## The step's effect

    8 resolved (7 function, 1 storage) / 21 added (18 function, 3 storage)
      resolved  tty_init, tty_filtops, tty_lock, tty_unlock, ttycheckoutq, ttyclrpgrphup,
                ttyfree, ttypchar
      added     averunnable, b_to_q, calcru, catq, clalloc, clfree, firstc, getc, linesw,
                msleep0, ndflush, nextc, nlinesw, pgsignal, putc, q_to_b, scanc, ttcompat,
                tty_pgsignal, unputc, vfs_context_thread

    762 -> 775 stub names, 643 -> 654 function, 119 -> 121 storage

The three storage entries are the line-discipline table itself (`linesw`, `nlinesw`) and the load-average
state `averunnable` — all sized stand-ins, none read on this path.

## The reading that produced the prediction — and why the stop moves 22 calls, not one

`tty_init`'s whole body is three calls — `lck_grp_attr_alloc_init`, `lck_grp_alloc_init`,
`lck_attr_alloc_init` — every one of them real since the lock objects went in, so the frame returns
immediately and `bsd_init` resumes at `+0x1CC`.

**But the next stub on that line is not the next call.** `ulock_initialize` sits at `+0x1D8`, and
`init_system_override` sits at `+0x1B0` — *before* the pointer `bsd_init` is already past — because the
compiler ordered the call sites by its own schedule, not by source order. So from `tty_init`'s return the
walk runs **22 calls** before it reaches a stub, and the whole reading depends on none of those 22 being
one or containing one.

Each was taken from the built image's disassembly and then walked:

    non-trivial callees between tty_init's return and +0x1B0
      kalloc_canblock, mac_policy_initbsd, get_bsdthread_info, set_bsdtask_info, __bzero,
      _consume_printf_args, strlcpy, current_thread, current_task, lck_mtx_init,
      lck_spin_init, lck_mtx_alloc_init, lck_grp_alloc_init, lck_attr_alloc_init,
      lck_grp_attr_alloc_init

`xnu_entry_callwalk.py --root <fn>` on each returns "reached no stub on the straight-line path". That is
374/378's rule applied properly — **"not a stub" is not "does not contain a stub"**, and the per-callee
listing would have shown all fifteen as real without saying anything about what is underneath them.

And the second half of 342's rule holds too: the intersection of this step's 21 added names with
`bsd_init`'s call set is empty. A stop cannot arrive from the caller as one of this step's own creations.

**Falsifiers, named in advance and all silent:** a stop on one of the 21 added names; a stop inside
`tty_init` (its three callees are real and stub-free); a stop at `ulock_initialize` (`+0x1D8`), which
would mean `init_system_override` had stopped being a stub without this step touching it; a `panic` or
`data abort` from the three new lock groups.

## Layout

    stubnames    775 records (654 function, 121 storage)
    text size    1844544 (0x1C2540)   — 410's 1824288 + 0x2020
    image bytes  1960760 (0x1DEB38)   — 410's 1944328 + 0x1C30
    bss          0x801DEB40 .. 0x8021BE18 (250584 bytes)
    layout       args +2203648 (0x8021D000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     1982952 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 de3e22d88c7404d72ad50fddf8009daca4666753fa8e631f09d9e78b4f1046a9

The run's markers agree to the byte: `image_bytes=0x001DEB38`, `bss_start=0x801DEB40`,
`bss_end=0x8021BE18`, `args_pa=0x8021D000`, `checksum=0x90406E25`.

## Where the frontier is now

`init_system_override`, defined by `bsd/kern/kern_overrides.c` — the non-Mac variant of the
`mac_policy_initbsd` pair, called from `bsd_init + 0x1B0`. Behind it four of `bsd_init`'s early
statements are real and its line is being retired one object per step; ahead of it, still on this same
line, are `ulock_initialize` (`+0x1D8`), `file_lock_init`, `ubc_init`, `vfsinit`, `mcache_init`,
`mbinit`, `aio_init`, `socketinit`, `domaininit`, `inittodr`, `IOFindBSDRoot`, `vfs_mountroot` — the
root filesystem mount and everything that has to be real before it.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301632 bytes, ending `No errors detected`. Device returned to
Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
