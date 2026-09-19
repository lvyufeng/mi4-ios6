# Experiment 422 — `bsd/vfs/vfs_init.c`: `vfsinit` retires, and the step creates its own stop — `vntblinit`, the first of four consecutive new stubs

**Step:** one object linked — `bsd_vfs_vfs_init.o`, the pool's only definer of `vfsinit`
(`bsd/vfs/vfs_init.c:326`) and therefore of 421's stop — appended to `LINK_OBJS` after
`bsd_kern_ubc_subr.o`.

**Prediction:** `stub_hit=vntblinit`, caller key = the linked address of `vfsinit + 0x230`.
**Measured: exactly that** — `xnu_entry_stub_caller_v=0x801b3ab0`, with `vfsinit` linked at
`0x801B3880`.

    line 3930: MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
    line 3938:  xnu_entry_stub_caller_v=0x801b3ab0
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vntblinit
    line 3974:  xnu_entry_stub_caller=0x801b3ab0
    line 3978: No errors detected

`xnu_entry_stub_caller_w0=0x62313038` / `w1=0x30626133` are the same address as ASCII — `801b` and
`3ab0`.

## The step's effect

     3 resolved (2 function, 1 storage) / 18 added (10 function, 8 storage)
      resolved  vfsinit, vn_default_error, and the **storage** stand-in `dead_mountp` (`B 0x4`)
      added     decmpfs_init, dqinit, mac_mount_label_associate, mac_mount_label_init, nchinit,
                nspace_handler_init, vfs_event_init, vfs_sysctl_node, vnode_authorize_init,
                vntblinit — and eight **storage** stand-ins: maxvfsslots (`R 4`), maxvfstypenum
                (`D 4`), numregistered_fses (`B 4`), numused_vfsslots (`B 4`),
                sysctl__vfs_children (`B 4`), vfs_op_descs (`D f8`), vfs_opv_descs (`D 18`),
                vfsconf (`D 4`)

    781 -> 796 stub names, 660 -> 668 function, 121 -> 128 storage

All three counts are exactly as `tools/entry_object_effect.py` predicted; the storage column rises by
eight because of the eight `B`/`D`/`R` stand-ins.

## The reading: twenty lock allocations, all real, then four new stubs four bytes apart

`vfsinit` begins with the whole of XNU's VFS lock furniture — `lck_grp_attr_alloc_init`,
`lck_grp_alloc_init`, `lck_attr_alloc_init`, `lck_spin_alloc_init` and `lck_mtx_alloc_init`, repeated for
`vnode_list`, `vnode`, `trigger_vnode`, `fd_vnode_data`, `fs conf`, `mount list`, `mount` and
`sync thread` (the `lck_mtx_init`/`lck_rw_init` family comes later in the body, behind the stop).
**Every one of those names is already real in this image** — they are the calls 415's `file_lock_init`
and 419's `msleep0` went through — so the first thing the walk meets that this image does not provide is
`vntblinit`, one call after the last `lck_mtx_alloc_init`:

    +0x21c: bl lck_mtx_alloc_init   (sync_mtx_lck)
    +0x22c: bl vntblinit            <- the stop, and this step creates it
    +0x230: bl vfs_event_init       <- also created here
    +0x234: bl nchinit              <- also created here
    +0x238: bl nspace_handler_init  <- also created here
    +0x2c4: bl vfs_opv_init         (real — defined by this object)
    +0x324: blx  r1                 (the indirect (*vfs_init)(&vfsc), behind the vfsconf loop)

**These are 342/407's shape — a step that creates the stub it stops on — and this is the sharpest
version of it in this run: the four new stubs are consecutive calls, four bytes apart.** "Which one is
the stop" is therefore not a lookup but a statement about which of four names the image does not
provide, and the answer is the first, because the three behind it are only reachable if `vntblinit`
returns — and `vntblinit` is a stand-in this build creates, so it does not return.

**The forward reading, taken after the link now that `vfsinit` is real, agrees:** the straight-line
answer is `vntblinit` and nothing else. Its guarded column is long and dominated by `zalloc_internal`
(the path the `lck_*` allocations take) — `lck_mtx_lock_spin_always` (2 guards further),
`thread_wakeup_prim` (2), `lck_mtx_unlock` (2), `assert_wait_timeout` (2), `thread_block` (2),
`try_alloc_from_zone` (1), `lck_mtx_sleep` (2), `panic` ×3, and 55 more. **By 421's rule, none of them
is the stop, and this run is the second confirmation of that rule in two steps:** every name on that
column belongs to a lock path this image has been running since 410, and the run stopped exactly where
the straight-line answer said it would, with `abort_entries=0`, `checks=5`/`failures=0`, no `exception:`
and no `panic`. A guarded column that names `thread_block` and three `panic`s is a place to look, not a
prediction.

**One stand-in this step creates is inert, and it is worth naming rather than discovering.** `maxvfsslots`
is a zeroed `R 4`, and it is the loop bound of the `for (vfsp = vfsconf, i = 0; i < maxvfsslots; i++,
vfsp++)` that would initialise each filesystem type and call each one's `vfs_init` — so with that
stand-in in place the loop does not run and `vfsconf` (also a stand-in, `D 4`) stays empty. That is
*behind* the stop, not in front of it, and it is recorded here so that a future frontier arriving at
`vfs_opv_init` or at the `blx r1` does not treat it as a surprise.

**A defect this step caught on itself, before the device ran.** The first draft of the `build_entry.sh`
comment quoted the offsets exactly as `objdump` printed them for the *object* — `+0x49C` for
`bl vntblinit` — and those are offsets from the start of the object's `.text`, not from the start of
`vfsinit`, which begins at object offset `0x270`. The function-relative site is `+0x22C`, and the linked
image confirms it to the byte (`0x801B3AAC`, with `vfsinit` at `0x801B3880`). The correction was made
before the run; had it not been, the published key would have been wrong by `0x270` and the run would
have looked like a miss on a step that was in fact a hit. **An object disassembly's offsets are relative
to the object, and a function's offsets are relative to the function — the difference is the function's
own start offset in that object.**

**Falsifiers, named in advance and all silent:** a stop at `vfs_event_init`, `nchinit` or
`nspace_handler_init`, which would have meant `vntblinit` is real — it cannot be, it is the stand-in this
build creates; a stop inside one of the `lck_*` calls; a stop at `vfs_opv_init` (`+0x2C4`), which is real
and defined by this object; a stop at `vfs_context_kernel`, `sysctl_register_oid` or `__MALLOC`, all real
and all behind the loop; a stop at `proc_uuid_policy_init` (`+0x7CC`, key `0x8003B1C0`), which would have
meant `vfsinit` had returned; a `panic`.

## Layout

    stubnames    796 records (668 function, 128 storage)
    text size    1970944 (0x1E1300) — up 0xE80 from 421's 0x1E0480
    data         0x801E4000 .. 0x801FED68 (0x1AD68), sysctl_set 0x801FED68 (0x19C),
                 init_array 0x801FEF04 (0x90)
    image bytes  2092948 (0x1FEF94)   — the image ends at `.init_array`'s end
    bss          0x801FEFC0 .. 0x8023C898 (252120 bytes)
    layout       args +2351104 (0x8023E000), topOfKernelData +4194304 (0x80400000),
                 tree +6291456 (0x80600000, len 0x7358), window 8388608
    headroom     1849192 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 bebb880951fe234e249e55a86f0fd1800fb3564f76b4dee8f11ac8a09a4753c87

The run's markers agree to the byte: `image_bytes=0x001FEF94`, `bss_start=0x801FEFC0`,
`bss_end=0x8023C898`, `args_pa=0x8023E000`, `checksum=0x90402889`, `checks=5`, `failures=0` — every one
of them different from 421's, so the image's own identity is visible from the report.

## Where the frontier is now

**`vntblinit`** (`bsd/vfs/vfs_subr.c`, linked at `0x801B7F18`), reached from `vfsinit + 0x230`, key
`0x801B3AB0` — the vnode table's own initialisation, and a stub **this step created**. Its pool definer
is `bsd_vfs_vfs_subr.o`, which is the next object: one of the largest in the pool (80 KB), and the
object that carries `vnode_*`, the vnode hash and the vnode freelist machinery. The three names behind
it on `vfsinit`'s line are `vfs_event_init` (`0x801B7180`), `nchinit` (`0x801B60E8`) and
`nspace_handler_init` (`0x801B6280`), then `vfs_opv_init` (real) and the `vfsconf` loop.

**Still owed and unchanged: the timer.** Nothing in this step's path takes a deadline — the `lck_*`
allocations are zone allocations and the stop is a stub — but `vntblinit` and everything behind it is
where the deadline-taking code starts to be reachable (`IOKitBSDInit`'s `waitForService`s,
`IOFindBSDRoot`, the workloop's event sources), and every one of them is unbounded while
`cpu_set_decrementer_func` is NULL. The two pieces remain `ml_init_timebase` with an MSM8974 `tbd_ops_t`
over the GPT at `0xf9020000`, and 405's `IOCPUInterruptController`.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301803 bytes / 3978 lines, ending `No errors detected`. The
only net across the jump armed and running (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bark_after=0x000c7fb5`, `hw_watchdog_bite_after=0x000dffac`,
`hw_watchdog_bite_truncated=0x00000000`) and it is what ended the run; the dead-man PPI was deliberately
disarmed before the jump (`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`, with
`disarm_ispendr0 0x20480000 -> 0x20400000` and `disarm_cntp_ctl 0x00000005 -> 0x00000002`). Device
returned to Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
