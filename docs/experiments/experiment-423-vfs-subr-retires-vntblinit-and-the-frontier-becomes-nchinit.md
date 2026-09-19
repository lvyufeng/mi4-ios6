# Experiment 423 — `bsd/vfs/vfs_subr.c`: `vntblinit` retires, and the frontier becomes `nchinit` — the second of 422's four consecutive new stubs

**Step:** one object linked — `bsd_vfs_vfs_subr.o`, the pool's only definer of `vntblinit`
(`bsd/vfs/vfs_subr.c:327`) — appended to `LINK_OBJS` after `bsd_vfs_vfs_init.o`.

**Prediction:** `stub_hit=nchinit`, caller key = the linked address of `vfsinit + 0x238`.
**Measured: exactly that** — `xnu_entry_stub_caller_v=0x801b3ab8`, with `vfsinit` at `0x801B3880`.

    line 3938:  xnu_entry_stub_caller_v=0x801b3ab8
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=nchinit
    line 3974:  xnu_entry_stub_caller=0x801b3ab8
    line 3978: No errors detected

`xnu_entry_stub_caller_w0=0x62313038` / `w1=0x38626133` are the same address as ASCII — `801b` and
`3ab8`. The image on disk agrees: `bl <nchinit>` is at `0x801B3AB4`, `vfsinit + 0x234`, so its return
address is `vfsinit + 0x238` to the byte.

## The step's effect

     35 resolved (33 function, 2 storage) / 64 added (60 function, 4 storage)
      resolved  vntblinit, vfs_event_init, vnode_authorize_init, vfs_sysctl_node, vfs_mountroot,
                vcount, check_mountedon, lock_vnode_and_post, vfs_mountedon, vn_getcdhash, vn_getpath,
                vn_getpath_fsenter, vn_path_package_check, vnode_authorize, vnode_close,
                vnode_getwithref, vnode_getwithvid, vnode_iterate, vnode_lock, vnode_lock_spin,
                vnode_lookup, vnode_mtime, vnode_open, vnode_put, vnode_ref, vnode_ref_ext,
                vnode_rele, vnode_setsize, vnode_size, vnode_startwrite, vnode_unlock,
                vnode_waitforwrites, vnode_writedone, and the **storage** stand-ins `fs_filtops`
                (`R 0x28`) and `mountlist` (`B 0x8`)

    796 -> 825 stub names, 668 -> 695 function, 128 -> 130 storage

All three counts are exactly as `tools/entry_object_effect.py` predicted — **and this is by far the
largest step in this run of steps, the biggest since 406.** Thirty-five resolutions of a single object
at once: the whole `vnode_*` face that 418, 420, 421 and 422 had been creating piecemeal is now real.
The 64 added names are this object's own downward references — the `vnode_*` predicates and label hooks,
the `vfs_context_*` family, the `VNOP_*` operation slots, and `namei` / `vn_open` / `dounmount` /
`lookup_validate_creation_path` / `safedounmount` — machinery a vnode calls that lives in objects not
yet linked (`vfs_syscalls.c`, `vfs_lookup.c`, `vfs_cache.c`, `vfs_vnops.c`, the MAC vnode hooks).

## The reading: `vntblinit`'s calls are all real, and so are the calls of the thread it starts

`vntblinit` is five `TAILQ_INIT`s, a `microuptime`, a division, and two thread calls:

    TAILQ_INIT(&vnode_free_list); TAILQ_INIT(&vnode_rage_list);
    TAILQ_INIT(&vnode_dead_list); TAILQ_INIT(&vnode_async_work_list); TAILQ_INIT(&mountlist);

    microuptime(&rage_tv);                             // real since 417
    rage_limit = desiredvnodes / 100;                  // `desiredvnodes` is real data (conf/param.c)
    if (rage_limit < RAGE_LIMIT_MIN) rage_limit = RAGE_LIMIT_MIN;

    kernel_thread_start((thread_continue_t)async_work_continue, NULL, &thread);
    thread_deallocate(thread);

Its three calls — `microuptime` (object `+0x60`), `kernel_thread_start` (`+0xA8`), `thread_deallocate`
(`+0xB0`) — are every one satisfied. **But `kernel_thread_start` is the call that made 418 a miss**, so
this step names the thread it starts and walks that too, rather than assuming the boot thread carries
on. `async_work_continue` (`vfs_subr.c:4062`) is an empty-queue blocking loop, and on a fresh boot the
queue *is* empty, because `vntblinit` itself just ran `TAILQ_INIT(&vnode_async_work_list)`:

    for (;;) {
        vnode_list_lock();
        if ( TAILQ_EMPTY(q) ) {
            assert_wait(q, THREAD_UNINT);
            vnode_list_unlock();
            thread_block((thread_continue_t)async_work_continue);
            continue;
        }
        ...
    }

Four calls on that path — `vnode_list_lock`, `assert_wait`, `vnode_list_unlock`, `thread_block` — and
all four are satisfied. `vnode_list_lock` and `vnode_list_unlock` are **defined by
`bsd_vfs_vfs_init.o`, the object linked at 422**; `assert_wait` and `thread_block` have been real since
long before. The thread parks, and the only other call the function makes, `process_vp`, is behind a
non-empty queue — with its `panic("found VBAD vp ...")` behind the same condition. **So 418's failure
mode is checked here rather than assumed away.**

Next on `vfsinit`'s line is `vfs_event_init` (`+0x230`), which this object also retires, and its body is
three calls, all real:

    klist_init(&fs_klist);                                    // real — bsd_kern_kern_event.o has been
    fs_klist_lck_grp = lck_grp_alloc_init("fs_klist", NULL);   //   in LINK_OBJS since 409
    fs_klist_lock = lck_mtx_alloc_init(fs_klist_lck_grp, NULL);

Then `bl <nchinit>` at `+0x234`, and **`nchinit` is not touched by this step**: it is defined by
`bsd_vfs_vfs_cache.o`, and its neighbour `nspace_handler_init` by `bsd_vfs_vfs_syscalls.o`. Neither is
linked. 422 created both, and both are still stand-ins.

**The forward readings, taken against the new image, agree and are worth recording as a pair:**
`walk from vfsinit` answers `nchinit` on the straight-line path, while `walk from vntblinit` and
`walk from vfs_event_init` both answer **no stub on the straight-line path**. The two functions this
step makes real are clean, and the stop is the one it does not touch.

**Falsifiers, named in advance and all silent:** a stop inside `vntblinit` or its thread —
`microuptime`, `kernel_thread_start`, `thread_deallocate`, or from the thread `vnode_list_lock`,
`assert_wait`, `vnode_list_unlock`, `thread_block`, `process_vp` — which by 418's rule was the outcome
that would have said this reading is wrong; a stop at `klist_init`, `lck_grp_alloc_init` or
`lck_mtx_alloc_init` inside `vfs_event_init`; a stop at `nspace_handler_init` (`+0x23C`, key
`vfsinit + 0x23C`), which would have meant `nchinit` is real; a stop at `vfs_opv_init` (`+0x2C4`), real
and defined by `vfs_init.o`; a stop on one of the 64 added names; a stop at `proc_uuid_policy_init`
(`+0x7CC`, key `0x8003B1C0`); a `panic`, and the two candidates were nameable in advance
(`async_work_continue`'s VBAD check behind a non-empty queue, and the reap-side paths this line does not
reach).

## Layout

    stubnames    825 records (695 function, 130 storage)
    text size    2019520 (0x1ED0C0) — up 0x4DC0 from 422's 0x1E1300
    data         0x801F0000 .. 0x8020B0B8 (0x1B0B8), sysctl_set 0x8020B0B8 (0x1C8),
                 init_array 0x8020B280 (0x90)
    image bytes  2142992 (0x20B310)   — the image ends at `.init_array`'s end
    bss          0x8020B340 .. 0x80248D58 (252440 bytes)
    layout       args +2400256 (0x8024A000), topOfKernelData +4194304 (0x80400000),
                 tree +6291456 (0x80600000, len 0x7358), window 8388608
    headroom     1798824 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 02d2c9fd8c5741f49de4b5e820b6e8b4fe4ca438af7d833c4eeefbf3b70493bf
    frontier     nchinit linked at 0x801C0F70, vntblinit at 0x801B4290, vfs_event_init at 0x801B88E4

The run's markers agree to the byte: `image_bytes=0x0020B310`, `bss_start=0x8020B340`,
`bss_end=0x80248D58`, `args_pa=0x8024A000`, `checksum=0x90402D4D`, `checks=5`, `failures=0` — every one
different from 422's, so the image's own identity is visible from the report.

## Where the frontier is now

**`nchinit`** (linked at `0x801C0F70`), reached from `vfsinit + 0x238`, key `0x801B3AB8` — the name
cache's initialisation, and the second of the four stubs 422 created four bytes apart. Its pool definer
is `bsd_vfs_vfs_cache.o`, the next object. `nspace_handler_init` (`0x801C1108`) is one call behind it.

**Still owed and unchanged: the timer.** Nothing on 423's path takes a deadline either — the two
functions this step makes real are pure setup, and the thread it starts parks on `thread_block`, which
is a scheduler primitive and not a deadline. But `vfs_cache.c` and the mount machinery behind it are
where `vfs_mountroot` lives, and `vfs_mountroot` is the code that will call into drivers. That is the
step where the absent timer starts to matter directly.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301801 bytes / 3978 lines, ending `No errors detected`. The
only net across the jump armed and running (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bark_after=0x000c7fb5`, `hw_watchdog_bite_after=0x000dffac`,
`hw_watchdog_bite_truncated=0x00000000`) and it is what ended the run; the dead-man PPI was deliberately
disarmed before the jump (`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Device returned to Android on
its own and was confirmed there (`adb devices` shows `4a2fe00b`).
