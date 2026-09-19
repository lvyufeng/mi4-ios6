# Experiment 415 — `bsd/kern/kern_descrip.c`: `file_lock_init` retires, and the frontier moves to `bufinit` by a tail call

**Step:** one object linked — `bsd_kern_kern_descrip.o`, the pool's only definer of `file_lock_init`
(`bsd/kern/kern_descrip.c`) and therefore of 414's stop — appended to `LINK_OBJS` after
`security_mac_process.o`.

**Prediction:** `stub_hit=bufinit`, caller key `0x8003B1B0`. **Measured:** exactly that,
`abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

    line 3938: xnu_entry_stub_caller_v=0x8003b1b0
    line 3942: xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=bufinit
    (no `exception:`, no `panic`; log ends `No errors detected`)

## The step's effect

    18 resolved (18 function, 0 storage) / 60 added (59 function, 1 storage)
      resolved  file_lock_init, falloc_withalloc, fdcopy, fdfree, fileproc_alloc_init,
                fo_kqfilter, fp_drop, fp_free, fp_getfkq, fp_getfpipe, fp_getfpsem, fp_getfpshm,
                fp_getfsock, fp_getfvpandvid, fp_lookup, proc_fdlock, proc_fdunlock,
                procfdtbl_releasefd
      added     the VFS/MAC file face — VNOP_* (8), mac_file_* (7), mac_vnode_* (3), vnode_* (12),
                vn_* (4), unlink1, open1, sock_getsockopt, sock_setsockopt, soo_stat, pipe_stat,
                pshm_stat, ubc_cs_* (5), waitevent_close, fileport_*, fg_vn_data_free, fulong, sulong,
                guarded_fileproc_free, fp_guard_exception, _aio_close, __pthread_testcancel,
                munge_user32_stat, munge_user32_stat64, select_conflict_queue (storage)

    757 -> 799 stub names, 636 -> 677 function, 121 -> 122 storage

All three counts are exactly as the tool predicted. This object both retires and creates, unlike 412's and
414's one-way steps, and its 60 added names are the largest creation since 406.

## The reading

`file_lock_init` is 0x5C bytes and makes exactly four calls — `lck_grp_attr_alloc_init`, `lck_grp_alloc_init`,
`lck_attr_alloc_init`, `lck_mtx_alloc_init` — all real, all walked clean, so the frame returns.

Past it, `bsd_init`'s line runs `mac_cred_label_associate_kernel` (+0x63C, real since 414), `lck_mtx_init`
×2, `chgproccnt`, `kmem_suballoc` — all real, each walked to "reached no stub on the straight-line path",
**and each one's guarded list read too** (413's defect 128 applied as a standing rule). Then `bsd_bufferinit`
(+0x7BC), which is real as well — and whose walk stops one level down:

    walk from bsd_bufferinit:
      bufinit   STUB

`bsd_bufferinit` is `bsd_startupearly()` (real), `kmem_suballoc(...)` (real, with a `panic` on failure), and
then its **last instruction is a tail call**:

    8003a9d0:  ea057bda  b  80199940 <bufinit>

which is why the stub's caller key is not a site inside `bsd_bufferinit`: the `pop {fp, lr}` two
instructions earlier restores the `lr` of *its* caller, so `bufinit` is entered holding `bsd_init`'s return
address. The predicted key `0x8003B1B0` is the instruction after the `bl <bsd_bufferinit>` at `0x8003B1AC`
— **a tail-called stub reports its grandparent's call site**, and this step is the first place that has
happened in the walk.

**Falsifiers, named in advance and all silent:** a stop inside `file_lock_init` or `bsd_bufferinit`; a stop
on one of the 60 added names — their intersection with `bsd_init`'s call set is empty, and the only
`bsd_init` callee this step defines is `file_lock_init`, so no reachable creation is possible; a `panic`
from `bsd_bufferinit`'s `kmem_suballoc` check; **a stop at `ubc_init` (`+0x7C4`, key `0x8003B1B8`)** — the
one reading the prediction turns on, which would have meant `bsd_bufferinit` returned instead of tail-calling.

## Layout

    stubnames    799 records (677 function, 122 storage)
    text size    1884768 (0x1CC260)   — 414's 1855296 + 0x2F60
    image bytes  2010252 (0x1EAC8C)   — 414's 1977244 + 0x33048
    bss          0x801EACC0 .. 0x80228058 (250776 bytes)
    layout       args +2269184 (0x8022A000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     1933224 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 5a04839c8e55dfc10283336b7fb606d5f492e96daafb89ae841d6638f4485e5d

The run's markers agree to the byte: `image_bytes=0x001EAC8C`, `bss_start=0x801EACC0`,
`bss_end=0x80228058`, `args_pa=0x8022A000`, `checksum=0x90402051`.

## Where the frontier is now

`bufinit`, defined by **`bsd/vfs/vfs_bio.c`** (`out/xnu_kernel_obj/bsd_vfs_vfs_bio.o`) — the buffer cache's
own initialisation, and the first stub the walk has reached that belongs to the filesystem layer rather
than to process credentials or the descriptor table. Behind it on `bsd_init`'s line are the stubs
`ubc_init` (+0x7C4), `vfsinit` (+0x7C8), `proc_uuid_policy_init`, `mcache_init`, `mbinit`, `net_str_id_init`,
`eventhandler_init`, `aio_init`, `pipeinit`, `pshm_lock_init`, `psem_lock_init`, `pthread_init`,
`pshm_cache_init`, `psem_cache_init`, `time_zone_slock_init`, `select_waitq_init`, `nwk_wq_init`,
`dlil_init` — fifteen of them in the next 0x50 bytes of compiled line, the density `bsd_init` has from here
on — and then, much further down, `vfs_mountroot`.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301801 bytes, ending `No errors detected`. Device returned to
Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
