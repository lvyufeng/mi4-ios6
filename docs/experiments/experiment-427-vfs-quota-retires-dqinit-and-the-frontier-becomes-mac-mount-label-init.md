# Experiment 427 — `bsd/vfs/vfs_quota.c`: `dqinit` retires, and the frontier becomes `mac_mount_label_init` — the first one-object step after the batch

**Step:** one object linked — `bsd_vfs_vfs_quota.o`, the pool's only definer of `dqinit` and of
`munge_dqblk` — appended to `LINK_OBJS` after `bsd_kern_sys_generic.o`, so that every object linked
from 422 onward keeps its address.

**Effect:** **2 resolved (2 function, 0 storage) / 2 added (2 function, 0 storage)** — `dqinit` and
`munge_dqblk` out, `VNOP_READ` and `VNOP_WRITE` in as *new* function stubs that this object's own
vnode-op functions reference. Counts **906 -> 906** undefined, **777 -> 777** function, **129 -> 129**
storage: **the net is zero for the first time in this run of steps**, because 425's batch stubs are now
being retired at the rate they arrive. All four counts are exactly what
`tools/entry_object_effect.py` predicted.

    bsd_vfs_vfs_quota.o   2 resolved (dqinit, munge_dqblk) / 2 added (VNOP_READ, VNOP_WRITE)

**Prediction, written before the build:** `stub_hit=mac_mount_label_init`, caller key = the linked
address of `vfsinit + 0x59C` — the return address of the `bl <mac_mount_label_init>` at
`vfsinit + 0x598`. **Measured: exactly that** — `xnu_entry_stub_caller_v=0x801b3e1c`, with `vfsinit`
still at `0x801B3880` and `mac_mount_label_init` now a real stand-in at `0x801F4948`.

    line 3894: MI4IOS6_STAGE90_XNU kernel_entry ok
    line 3917: MI4IOS6_STAGE90_XNU xnu_entry_checks=0x00000005
    line 3918: MI4IOS6_STAGE90_XNU xnu_entry_failures=0x00000000
    line 3938:  xnu_entry_stub_caller_v=0x801b3e1c
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=mac_mount_label_init
    line 3978: No errors detected

`dqinit` and `munge_dqblk` are **real symbols in the image that ran** (`0x801F1384`, `0x801F2C28`) —
they were function stand-ins in 425's link — and the stand-in that moved is the *next* one, which is
why the prediction is keyed on the caller and not on the callee's address.

## The reading: a function with no branches is the one shape the walk models exactly

425 left the tool in a particular state: `tools/xnu_entry_callwalk.py` still answers
`nwk_wq_init` on the straight-line path (rule 426's loop-exit blind spot is not statically fixable),
and what it offers instead is the list of guarded sites whose callee is itself a stub. The right way
to read a one-object step out of that is to find the case where the walk's model is *exact*, and
`dqinit` is it: **0x98 bytes, seven calls, and no branches at all** — every control transfer in the
function is a `bl` that returns.

    lck_grp_attr_alloc_init   object +0x04, +0x58    real, the family 415-426 have gone through
    lck_grp_alloc_init        +0x20, +0x74           real
    lck_attr_alloc_init       +0x30, +0x84           real
    lck_mtx_alloc_init        +0x48                  real

The rest of the body is the stores that file those four structures into `quota_list_lck_grp_attr`,
`quota_list_lck_grp`, `quota_list_lck_attr` and `quota_list_mtx_lock` — four `.bss` slots the object
defines itself — so when `dqinit` becomes real there is nothing inside it that can stop.
`munge_dqblk` is smaller still: two `bcopy` calls and a `cmp`. **So this step's stop is decided
entirely by the next call on `vfsinit`'s line**, which is what the prediction is made of.

**And the sixty-odd instructions between that call and the next one contain no branch either.**
Between the `bl <dqinit>` at `+0x49C` and the `bl <mac_mount_label_init>` at `+0x598`, the only
control transfers are `bl <__MALLOC_ZONE>`, `bl <__bzero>`, three `bl <lck_mtx_init>` and
`bl <lck_rw_init>` — all real, all returning. That is the whole argument for the key `0x801b3e1c`,
and it is checked against the linked image rather than assumed: this is a *conditional*-branch-free
region, which is exactly what rule 426 says the walk cannot see through when a loop exit is involved
and can see through completely when it is not.

**The object's two created stubs are not reachable before the frontier** — 342's trap, checked by
asking which function owns each reference rather than by assuming the answer: `VNOP_READ` and
`VNOP_WRITE` are referenced by `dqfileclose`, `dqfileopen` and `dqget`, the quota layer's vnode
operations, none of which is on `vfsinit`'s path. The object's other 25 references are already
satisfied in the 425 image.

## Two readings in the report that can be misread, and the second reading of the same number

* `xnu_entry_why=0x801f7f20` with `xnu_entry_why_byte=0x00000061` is the **reason** string — "a symbol
  this image does not provide was called", whose first byte is `a` — and not the stub's name. The
  name is the `stub_hit=` line. The two are printed a few lines apart and look alike.
* `xnu_entry_stub_caller_digits=0x00000038` with `_w0=0x62313038` and `_w1=0x63316533` is the caller
  address spelled back as ASCII (`80b13e1c`), produced by a different path from the hex value on the
  line above it. It is the same number twice, which is the useful part: an address and its digits
  come from one write, so a mismatch there would mean the record itself was corrupted.

## Layout

    entry text   2250592 (0x225760)
    entry image  2375332 (0x243EA4)
    bss          0x80243EC0 .. 0x80285418 (267608 bytes, zeroed by the payload)
    layout       args 0x80287000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1551336 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 fbd87231258881db7f54f766bd74e4225715371fa5ef88fe6c1a94057a749e31 (5394432 bytes)
    entry bin    2358924 -> 2375332 bytes; sha256 fe9b2a8195bcd66b2a7a699cfb299a567170139f909facd6e5df2a32af982dd4

The run's markers agree with the link: `xnu_entry_image_bytes=0x00243EA4`,
`xnu_entry_bss_start=0x80243EC0`, `xnu_entry_bss_end=0x80285418`, `xnu_entry_args_pa=0x80287000`,
`xnu_entry_top_of_kernel_data=0x80400000`, `xnu_entry_checks=0x00000005`,
`xnu_entry_failures=0x00000000`, `xnu_entry_checksum=0x90402439`, `xnu_entry_entering_at=0x80000074`.
Every marker differs from 425's, which is the check that the image that ran is the image that was
built.

**And the image is reproducible from the committed sources:** rebuilding the entry image after the
425/427 comment blocks were written produced a byte-identical `xnu_arm_entry.bin`
(`fe9b2a81…`, unchanged), so the payload that ran is the one the tree builds. The 425 step's own
rebuild check is the same statement one step earlier.

## Where the frontier is now

**`mac_mount_label_init`** (`security/mac_vfs.c`, defined by `security_mac_vfs.o`), reached from
`vfsinit + 0x598`, key `0x801b3e1c`. The next three sites on the same line, in order, are
`vfs_context_kernel` (`bsd_vfs_kpi_vfs.o`, `+0x59C`), `mac_mount_label_associate`
(`security_mac_vfs.o` — the *same object* as this step's stop, so one object retires two of the three)
and `decmpfs_init` (`bsd_kern_decmpfs.o`, `+0x5B4`). After those, the line leaves `vfsinit`: the
remaining distance to `vfs_mountroot`'s machinery is on `bsd_init`'s own statement list, where 425's
batch already retired the mbuf and pipe layers.

**Still owed and unchanged: the timer.** Nothing on this step's path takes a deadline — `dqinit` is
four lock allocations and `munge_dqblk` is two `bcopy`s — but the layer behind these three objects is
where the mount machinery starts, and `IOFindBSDRoot` and the mount loop's waits are unbounded while
`cpu_set_decrementer_func` is NULL. The two pieces remain `ml_init_timebase` with an MSM8974
`tbd_ops_t` over the GPT at `0xf9020000`, and 405's `IOCPUInterruptController`.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed, nothing written to
storage. 25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`,
`abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`; 301817 bytes / 3978 lines,
ending `No errors detected`. The only net armed across the jump was the hardware watchdog
(`hw_watchdog_counter_running=0x00000001`, `hw_watchdog_bark_after=0x000c7fb5`,
`hw_watchdog_bite_after=0x000dffac`, `hw_watchdog_bite_truncated=0x00000000`) and it is what ended
the run; the dead-man PPI was deliberately disarmed before the jump
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Device returned to Android on its own and was
confirmed there (`adb devices` shows `4a2fe00b`).

The 425 run's capture was overwritten by the two runs after it; before this run it was copied to
`/tmp/run425_kmsg.txt`, and this run's log is `/tmp/run427_kmsg.txt`, so neither can overwrite the
other.
