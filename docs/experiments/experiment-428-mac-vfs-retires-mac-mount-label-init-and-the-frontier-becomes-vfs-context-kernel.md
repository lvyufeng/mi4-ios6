# Experiment 428 — `security/mac_vfs.c`: `mac_mount_label_init` retires with 54 more names, and the frontier becomes `vfs_context_kernel`

**Step:** one object linked — `security_mac_vfs.o`, the pool's only definer of `mac_mount_label_init`
and of fifty-four other names this walk has been stepping past — appended to `LINK_OBJS` after
`bsd_vfs_vfs_quota.o`.

**Effect:** **55 resolved (55 function, 0 storage) / 2 added (2 function, 0 storage)** — the widest
single object in this run of steps, and the largest step since 423. Every `mac_vnode_*`,
`mac_mount_*`, `mac_vnode_notify_*` and `vn_setlabel` stand-in goes real at once. Counts
**906 -> 853** undefined, **777 -> 724** function, **129 -> 129** storage, all three exact per
`tools/entry_object_effect.py`.

    security_mac_vfs.o   55 resolved / 2 added (VNOP_SETLABEL, exec_spawnattr_getmacpolicyinfo)

**Prediction, written before the build:** `stub_hit=vfs_context_kernel`, caller key = the linked
address of `vfsinit + 0x5A0` — the return address of the `bl <vfs_context_kernel>` at
`vfsinit + 0x59C`. **Measured: exactly that** — `xnu_entry_stub_caller_v=0x801b3e20`.

    line 3894: MI4IOS6_STAGE90_XNU kernel_entry ok
    line 3917: MI4IOS6_STAGE90_XNU xnu_entry_checks=0x00000005
    line 3918: MI4IOS6_STAGE90_XNU xnu_entry_failures=0x00000000
    line 3938:  xnu_entry_stub_caller_v=0x801b3e20
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vfs_context_kernel
    line 3978: No errors detected

## The step's reading: an argument that is a real allocation, and a `blx` closed by storage rather than by the graph

`mac_mount_label_init` is called from `bsd/vfs/vfs_init.c:505` with the `mp` that line 479 allocated:

    479   MALLOC_ZONE(mp, struct mount *, sizeof(struct mount), M_MOUNT, M_WAITOK);
    480   bzero((char *)mp, sizeof(struct mount));
    ...
    505   mac_mount_label_init(mp);
    506   mac_mount_label_associate(vfs_context_kernel(), mp);

so the `[r4, #2364]` store that ends the function lands inside a real `struct mount` — **the first
body in this run of steps whose argument is a real allocation rather than a stand-in**, which is why
it is worth stating rather than assuming. Its body is three direct calls and two indirect ones, and
the three direct ones are all real:

    mac_labelzone_alloc                security_mac_label.o, in LINK_OBJS since 274
    mac_policy_list_conditional_busy   security_mac_base.o, in LINK_OBJS since 274
    mac_policy_list_unbusy             the same object
    blx r2  x2                         the policy vtable's hook at +380

**The `blx` is the interesting one, because it is the one call shape rule 426 cannot model at all.**
A `blx` through a vtable is not a call site the walk can name — there is no `bl` for the stub reporter
to catch either — so it has to be closed by the *storage* rather than by the graph. Here that is
available and checkable: both indirect calls are entered only from a non-empty entry of
`mac_policy_list`, and `mac_policy_list` is real and zeroed — defined by `security_mac_base.o` as a
`0x1C`-byte `.bss` object, which `nm -S` on the entry image agrees with (`0x80277220`, `0x1C` at the
time of the prediction; it has moved since, as every `.bss` object does when the image grows). The
count field the function tests first is the word at `+12`, and with no policy module linked the two
loops are skipped and the function falls to `mac_policy_list_conditional_busy` and returns.

**The object's two created stubs are not reachable before the frontier** — 342's trap, checked by
ownership: `VNOP_SETLABEL` and `exec_spawnattr_getmacpolicyinfo` are referenced only by
`mac_cred_check_label_update_execve`, `mac_cred_label_update_execve`, `mac_vnode_check_exec` and
`vn_setlabel`, the exec and label-setting paths, none of which is on `vfsinit`'s line.

**And the frontier is not the name this object is linked for.** `mac_mount_label_associate` is also
retired here, and its call sits four bytes *after* `vfs_context_kernel` on the same line (`+0x5A4`):
`mac_mount_label_associate(vfs_context_kernel(), mp)` is **one source statement compiled as two calls
in argument order**, so the object that supplies the second argument's callee is not the one that
supplies the first. The honest way to say the step's name is therefore "the object that retires
`mac_mount_label_init`" and the honest way to say its frontier is "the first stub *after* that call
site", which is a name the object does not define. **The run is what settles it, and the falsifier
was written for exactly this case**: a stop at `mac_mount_label_associate` (`+0x5A4`, key
`0x801b3e28`) would have meant the argument-order reading was wrong. It did not happen.

## Layout

    entry text   2278240 (0x22C360)
    entry image  2408124 (0x24BEBC)
    bss          0x8024BEC0 .. 0x8028D418 (267608 bytes, zeroed by the payload)
    layout       args 0x8028F000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1518568 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 accc9c0bfbaf44d027f8a24657df1a8a6750131d34340ded976eca0d7b22ff1c (5427200 bytes)
    entry bin    2375332 -> 2408124 bytes; sha256 940107659f2ef3057099fb47d5b2626191b57ffbe550a7ad5c051a5f9e905873

The run's markers agree with the link: `xnu_entry_image_bytes=0x0024BEBC`,
`xnu_entry_bss_start=0x8024BEC0`, `xnu_entry_bss_end=0x8028D418`, `xnu_entry_args_pa=0x8028F000`,
`xnu_entry_checks=0x00000005`, `xnu_entry_failures=0x00000000`, `xnu_entry_checksum=0x90402421`,
`xnu_entry_entering_at=0x80000074`. Every marker differs from 427's. The stub count **853** matches
the effect tool's prediction to the record.

## Where the frontier is now

**`vfs_context_kernel`** (`bsd/vfs/kpi_vfs.c`, defined by `bsd_vfs_kpi_vfs.o`), reached from
`vfsinit + 0x59C`, key `0x801b3e20`. It is one call further along the same line from 427's stop, and
the two remaining sites after it are `decmpfs_init` (`bsd_kern_decmpfs.o`, `+0x5B4`, from
`vfs_init.c:511`'s `#if FS_COMPRESSION` block) — `mac_mount_label_associate` is now real, so the line
is down to two stubs between here and `vfsinit`'s return.

**Still owed and unchanged: the timer.** Nothing on this step's path takes a deadline: the object's
bodies are label allocation, vtable walks over an empty policy list and label copies. But once
`vfsinit` returns, `bsd_init`'s next layer is the root mount, and `IOFindBSDRoot` and the mount loop's
waits are unbounded while `cpu_set_decrementer_func` is NULL. The two pieces remain `ml_init_timebase`
with an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`, and 405's `IOCPUInterruptController`.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure, so both gates ran for this step); nothing
flashed, nothing written to storage. 25 × `persistent_write_attempted=0x00000000`, 87 ×
`failure_mask=0x00000000`, `abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`;
301815 bytes / 3978 lines, ending `No errors detected`. The only net armed across the jump was the
hardware watchdog (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bite_truncated=0x00000000`) and it is what ended the run; the dead-man PPI was disarmed
before the jump. Device returned to Android on its own and was confirmed there (`adb devices` shows
`4a2fe00b`).

Per-run logs are now kept apart: 425's capture is `/tmp/run425_kmsg.txt`, 427's
`/tmp/run427_kmsg.txt`, 428's `/tmp/run428_kmsg.txt`.
