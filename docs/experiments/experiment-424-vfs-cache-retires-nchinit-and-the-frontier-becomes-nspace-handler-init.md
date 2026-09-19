# Experiment 424 — `bsd/vfs/vfs_cache.c`: `nchinit` retires, and the frontier becomes `nspace_handler_init` — the third of 422's four consecutive new stubs

**Step:** one object linked — `bsd_vfs_vfs_cache.o`, the pool's only definer of `nchinit`
(`bsd/vfs/vfs_cache.c:2105`) and therefore of 423's stop — appended to `LINK_OBJS` after
`bsd_vfs_vfs_subr.o`.

**Prediction, written into `build_entry.sh` before the build:** `stub_hit=nspace_handler_init`,
caller key = the linked address of `vfsinit + 0x23C` — the return address of the
`bl <nspace_handler_init>` at `vfsinit + 0x238`.
**Measured: exactly that** — `xnu_entry_stub_caller_v=0x801b3abc`, with `vfsinit` linked at
`0x801B3880`.

    line 3894: MI4IOS6_STAGE90_XNU kernel_entry ok
    line 3932: MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
    line 3938:  xnu_entry_stub_caller_v=0x801b3abc
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=nspace_handler_init
    line 3978: No errors detected

The image on disk agrees to the byte: `vfsinit` links at `0x801B3880`, `nspace_handler_init` at
`0x801C432C`, and the `bl <nspace_handler_init>` is at `0x801B3AB8`, so the return address the stub
reported is `vfsinit + 0x23C`.

**One thing about the run this log describes.** The step's link was made before 426, but the device
run recorded here was made *after* it, because this entry's own capture was lost when the two runs
that followed it overwrote `/tmp/cancro-last_kmsg.txt` (the per-run logs under `/tmp` stop at
`run411`). The rebuild is deterministic — same sources, same toolchain, same counts as the comment
block recorded — and it was taken through both gated scripts like every other run. What is quoted
above is from that run, at `/tmp/run424_kmsg.txt`, not from the original one.

## The step's effect

     12 resolved (11 function, 1 storage) / 4 added (3 function, 1 storage)
      resolved  nchinit, build_path, build_path_with_parent, cache_enter_create, vfs_addname,
                vnode_cache_authorized_action, vnode_cache_is_authorized, vnode_getname_printable,
                vnode_getparent, vnode_putname_printable, vnode_update_identity, and the **storage**
                stand-in `nc_disabled` (`B 0x4`)
      added     VFS_VGET, VNOP_LOOKUP, mac_vnode_check_lookup, and the storage stand-in
                `mount_generation` (`B 4`)

    825 -> 817 stub names, 695 -> 687 function, 130 -> 130 storage

All three counts are exactly as `tools/entry_object_effect.py` predicted. The name count *falls*
because this object resolves more `cache_*`/`vnode_*` names than its own body references — the same
shape as every step in this run of steps that links a leaf of the VFS face.

## The reading: `nchinit` has no stub for its own body to reach, and the evidence is the object

The linked image still carried `nchinit` as a stand-in at the time of the prediction, so
`tools/xnu_entry_callwalk.py --root nchinit` could see nothing inside it: it answers `nchinit STUB`
and an *empty* guarded list, because a stand-in has no body to expand. Read from the object instead,
`nchinit` makes eight distinct calls and every one is satisfied:

    hashinit x2 (object +0x2CFC, +0x2D34)          real since 409
    lck_grp_attr_alloc_init, lck_grp_alloc_init, lck_attr_alloc_init, lck_rw_alloc_init x2,
    lck_mtx_init                                  all real - the same family 415-423 have gone through

and its two remaining callees, `init_crc32` (`vfs_cache.c:2074`) and `init_string_table` (`:2427`),
are **`static` and inlined into it** — their only trace in the object is the data they fill
(`crc32tab` and `string_table_mask`, both `.bss`), with no separate symbol and no relocation of their
own. So there is no fourth call site hiding behind them.

**The object's six remaining stub references are not on this path.** They are `vfs_context_issuser`,
`vfs_context_proc`, `vfs_context_ucred`, `vnode_getattr`, `vnode_isdir` and `vnode_vid`, and they
belong to the name-cache *lookup* functions this object also defines (`cache_lookup`,
`cache_enter_create`, `vnode_update_identity`, …) — none of which `nchinit` calls. That is 342's trap
(a new stub is only dangerous if it is reachable before the frontier) checked by intersecting the
object's undefined list with the stub set and asking which function owns each name.

Next on `vfsinit`'s line is `nspace_handler_init` at `+0x238`, and **this step does not touch it**: it
is defined by `bsd_vfs_vfs_syscalls.o`, which is not linked, so it is still the stand-in 422 created.
This is the third of 422's four consecutive stubs: `vntblinit` went at 423 (with `vfs_event_init`,
same object), `nchinit` goes here, and `nspace_handler_init` stays.

**Falsifiers, named in advance and all silent:** a stop inside `nchinit` — `hashinit` (twice), any of
the seven `lck_*` calls, or the inlined `init_crc32`/`init_string_table` — which would say this object
reading is wrong; a stop on one of the six name-cache lookup names above, which would mean the walk
had left `nchinit` and entered a function `bsd_init`'s line does not call; a stop at `vfs_opv_init`
(`+0x2C4`), which is real and defined by `vfs_init.o`; a stop at `proc_uuid_policy_init` (`+0x7CC`,
key `0x8003B1C0`), which would mean `vfsinit` had returned; a stop on one of the four added names; a
`panic`.

## Layout

    entry text   2032128 (0x1F0200)
    entry image  2159448 (0x0020F358)
    bss          0x8020F380 .. 0x8024F1D8 (261720 bytes, zeroed by the payload)
    layout       args 0x80251000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1773096 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 53cb94e75c29ec82469dd45c310ff53ba79ddc603881e4d42d0fd305a873f490

The run's markers agree with the link: `xnu_entry_image_bytes=0x0020f358`,
`xnu_entry_bss_start=0x8020f380`, `xnu_entry_bss_end=0x8024f1d8`, `xnu_entry_args_pa=0x80251000`,
`xnu_entry_top_of_kernel_data=0x80400000`, `xnu_entry_checks=0x00000005`,
`xnu_entry_failures=0x00000000`, `xnu_entry_checksum=0x9041e145`.

**And this image was already within 16 KB of the alias window that 425 then broke.** The high-alias
probe word sat at PA `0x002FC0B4`; the candidate table's window ended at `0x00300000`. Nobody could
have known that from the run — the check passed — and it is recorded here because it is the cheapest
possible statement of how close the two steps were: `0x300000 - 0x2FC0B4 = 0x3F4C` bytes, and the
thirteen-object batch that followed spent them.

## Where the frontier is now

**`nspace_handler_init`** (linked at `0x801C432C` in this image), reached from `vfsinit + 0x238`, key
`0x801B3ABC` — the fourth and last of 422's four consecutive stubs, and the pointer to the VFS
syscall surface. Its pool definer is `bsd_vfs_vfs_syscalls.o`, which is what 425's batch adds along
with twelve others. `nchinit` is at `0x801C1B60`, and `vfs_mountroot` — the function that will call
into drivers — is real and linked since 423, at `0x801B5050`.

**Still owed and unchanged: the timer.** Nothing on this step's path takes a deadline: `nchinit` is
eight allocations and two inlined table initialisers. But the boot's next deadline is not far — the
filesystem layer behind `nspace_handler_init` is where `IOFindBSDRoot` and the mount machinery live,
and every one of those waits is unbounded while `cpu_set_decrementer_func` is NULL. The two pieces
remain `ml_init_timebase` with an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`, and 405's
`IOCPUInterruptController`.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5`
/ `failures=0`, no `exception:`, no `panic`; 301816 bytes / 3978 lines, ending `No errors detected`.
The only net across the jump armed and running (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bark_after=0x000c7fb5`, `hw_watchdog_bite_after=0x000dffac`,
`hw_watchdog_bite_truncated=0x00000000`) and it is what ended the run; the dead-man PPI was
deliberately disarmed before the jump (`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Device returned
to Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
