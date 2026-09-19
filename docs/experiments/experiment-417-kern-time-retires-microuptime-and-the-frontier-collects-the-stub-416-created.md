# Experiment 417 — `bsd/kern/kern_time.c`: `microuptime` retires, and the frontier collects the stub 416 created

**Step:** one object linked — `bsd_kern_kern_time.o`, the pool's only definer of `microuptime`
(`bsd/kern/kern_time.c`) and therefore of 416's stop — appended to `LINK_OBJS` after `bsd_vfs_vfs_bio.o`.

**Prediction:** `stub_hit=cluster_init`, caller key = the linked address of `bufinit + 0x39C`.
**Measured:** exactly that — `xnu_entry_stub_caller_v=0x801A2CBC`, with `bufinit` linked at `0x801A2920`,
so `0x801A2CBC = bufinit + 0x39C` to the byte. `abort_entries=0`, `checks=5` / `failures=0`, no
`exception:`, no `panic`.

    line 3938: xnu_entry_stub_caller_v=0x801a2cbc
    line 3942: xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=cluster_init

## The step's effect

    10 resolved (10 function, 0 storage) / 1 added (1 function, 0 storage)
      resolved  microuptime, microtime, microtime_with_abstime, nanotime, inittodr, itimerfix,
                realitexpire, time_zone_slock_init, timevaladd, timevalsub
      added     net_uptime2timeval

    793 -> 784 stub names, 671 -> 662 function, 122 -> 122 storage

All three counts are exactly as the tool predicted.

## The reading

`microuptime` is 0x2C bytes and makes **exactly one call** — `clock_get_system_microtime`, which is real
(`osfmk/arm/rtclock.o`, already linked) and walks clean; the rest of the body is `push`, `sub sp`,
`stm r4, {r0, r1}`, `pop`. So the frame returns into `bufinit`'s header loop with nothing to stop on. The
one name this step creates, `net_uptime2timeval`, is reached from `nanotime`/`microtime` — not from
`microuptime` and not from `bsd_init`'s line — so 342's trap is checked and empty.

**This step collects what 416 predicted and created.** Asked precisely which of `bufinit`'s calls is a
stub, the answer in this image is one: `cluster_init`, at object `+0x398` — the stub 416 introduced. Every
other call in the function is real: the loop body's `bzero` and two inlined `binsheadfree`/`binshash`, the
four `lck_*_init` calls at `+0x2BC..+0x338`, and the six `panic`s (the reachable ones being the
`iobuffer_mtxp`/`buf_mtxp` NULL checks). The second loop sets no timestamp, so it does not call
`microuptime` again.

**Falsifiers, named in advance and all silent:** a stop inside `microuptime` or
`clock_get_system_microtime`; a stop at `net_uptime2timeval`; **a `panic`** — the two at object
`+0x1A4`/`+0x1D8` sit inside the header loop this run iterates `max_nbuf_headers` times, so a bad header
would surface here rather than as a stub; a stop at `ubc_init` (`0x8003B1B8`), which would mean `bufinit`
had run to its end past `cluster_init`.

## A note on the run markers: they are a layout fact, not image evidence

This run's `xnu_entry_image_bytes` (`0x001EED34`), `xnu_entry_bss_start`/`_end`
(`0x801EED40`/`0x8022C218`) and `xnu_entry_checksum` (`0x90402229`) are **byte-identical to 416's** — and
that is not a mistake at the hands of the reader, it is what those markers are. `image_bytes` is the
number reported for where the copied image ends, which the payload derives from where `.bss` starts, and
that moves in the linker script's `.data` alignment steps (`2**14`), not with the code: the 417 link grew
`.text` by 3940 bytes and the `.bss` start stayed inside the same 0x4000 window. The checksum is an XOR of
the *result record*'s fields and carries no image bytes at all.

**What told the truth about which image ran is the stub name and its key** — `cluster_init` at
`bufinit + 0x39C`, a site 416's run never reached, because 416's run was still stopped on `microuptime`.
The general rule, recorded as defect 130: **a marker set that is stable across a link change cannot
confirm which image ran; only a reading that the link changed can.**

## Layout

    stubnames    784 records (662 function, 122 storage)
    text size    1913540 (0x1D31C4)   — 416's 1909600 + 0xE74
    image bytes  2026804 (0x1EED34)   — unchanged, and explained above
    bss          0x801EED40 .. 0x8022C218 (251096 bytes)  — unchanged, same reason
    layout       args +2285568 (0x8022E000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     1916392 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 43db0c5a681fa7282175d3159eb34790f6464c6e32311363902855db9066b1d8

## Where the frontier is now

`cluster_init`, defined by **`bsd/vfs/vfs_cluster.c`** (`out/xnu_kernel_obj/bsd_vfs_vfs_cluster.o`) — the
clustering layer's init, called from `bufinit` at `+0x398`. Its sibling `cluster_bp` was created as a stub
by 416 and is still one.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301806 bytes, ending `No errors detected`. Device returned to
Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
