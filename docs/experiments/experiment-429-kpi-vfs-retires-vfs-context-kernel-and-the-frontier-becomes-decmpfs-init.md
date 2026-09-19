# Experiment 429 — `bsd/vfs/kpi_vfs.c`: `vfs_context_kernel` retires with 108 more names, and the frontier becomes `decmpfs_init` — the last stub call left in `vfsinit`

**Step:** one object linked — `bsd_vfs_kpi_vfs.o`, the pool's definer of `vfs_context_kernel` and of
108 more names — appended to `LINK_OBJS` after `security_mac_vfs.o`.

**Effect:** **109 resolved (109 function, 0 storage) / 27 added (4 function, 23 storage)** — the widest
step of the whole walk. Counts **853 -> 771** undefined, **724 -> 619** function, **129 -> 152**
storage. The reason one file is this wide is that `bsd/vfs/kpi_vfs.c` defines the `VFS_*` and `VNOP_*`
wrappers *by hand* — 98 `VNOP_` occurrences, each a small function that fills an `args` struct from
`&vnop_*_desc` and calls through `vp->v_op[...vdesc_offset]` — so one object turns the whole vnode-op
front door real at once.

    bsd_vfs_kpi_vfs.o   109 resolved / 27 added (4 function, 23 storage)

**Prediction, written before the build:** `stub_hit=decmpfs_init`, caller key = the linked address of
`vfsinit + 0x5B8` — the return address of the `bl <decmpfs_init>` at `vfsinit + 0x5B4`
(`bsd/vfs/vfs_init.c:511`, inside `#if FS_COMPRESSION`). **Measured: exactly that** —
`xnu_entry_stub_caller_v=0x801b3e38`.

    line 3894: MI4IOS6_STAGE90_XNU kernel_entry ok
    line 3917: MI4IOS6_STAGE90_XNU xnu_entry_checks=0x00000005
    line 3918: MI4IOS6_STAGE90_XNU xnu_entry_failures=0x00000000
    line 3938:  xnu_entry_stub_caller_v=0x801b3e38
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=decmpfs_init
    line 3978: No errors detected

## The 23 created storage stand-ins — a new kind of exposure in this walk

Every step so far has created *function* stubs, and a function stub is safe by construction: it halts
the run and names itself, which is exactly what makes this walk work. **This step creates 23 storage
stand-ins, and those are not stops — they are wrong values** (the standing rule: the call graph says
which symbols are missing, never which pointers are null). They are the `vnop_*_desc` tables
(`D 28`, 0x28 bytes each in the pool), and the effect tool's note applies: a created storage stand-in
costs `0x40` of `.bss` rather than its own size, so this step adds `23 × 0x40 = 0x5C0` bytes of zeroed
storage for 23 tables that stay uninitialized until their defining object is linked. They are not
initialized by this step and nothing in it writes them.

So 342's trap has to be asked of each one — who reads it, and does that reader run before the frontier:

    the 23 desc tables are referenced only by the VNOP_*/VFS_* wrappers this same object defines,
    which are reached from the syscall and namei layers — none on vfsinit's line.
    the four created function stubs are create_fsevent_from_kevent, lf_advlock,
    lookup_compound_vnop_post_hook and vnode_getnamedstream, referenced by vfs_fsadd, ADVLOCK's
    callers, vn_open and the named-stream wrappers — again none on this line.

The run confirms it in the only way that counts here: no stop on any of the four, and no VOP
misbehaving through a zeroed table (a wrong `vdesc_offset` would have called through a garbage vtable
slot and shown up as an abort, not as a clean stop).

## The reading: `vfs_context_kernel`'s safety rests on `bsd_init`'s statement order

`bsd/vfs/kpi_vfs.c:1343`:

    if (kerncontext.vc_ucred == NOCRED) kerncontext.vc_ucred = kernproc->p_ucred;
    if (kerncontext.vc_thread == NULL)  kerncontext.vc_thread = proc_thread(kernproc);
    return &kerncontext;

so the function dereferences `kernproc`. That is not a stand-in: `kernproc` is real
(`bsd_kern_bsd_init.o`, a 4-byte pointer, `0x80257DD8` in the 428 image) and points at `proc0`, a real
`0x2B8`-byte `struct proc` in the same object. The read is `kernproc->p_ucred` at `+0x90`, and the
question that decides whether this step faults is not "is the symbol real" but **"has that field been
written by the time `vfsinit` runs"**:

    bsd/kern/bsd_init.c:638   kernproc->p_ucred = kauth_cred_create(&temp_cred);
    bsd/kern/bsd_init.c:735   vfsinit();

97 lines earlier and on a path the walk has already traversed, so yes. Its only call is
`proc_thread(kernproc)` (real, `bsd_kern_kern_proc.o`), and `kerncontext` is an 8-byte `.bss` pair this
object defines itself. **This is the first body in the run whose safety is an argument about boot
ordering rather than about storage being zeroed**, and it is closed by reading two line numbers in the
caller's source rather than by assuming that a real symbol means a valid value.

## This is the last stub call in `vfsinit`

The walk's guarded list for the new image has exactly one `vfsinit` entry left —
`vfsinit+0x5b4 -> decmpfs_init` — and nothing after it: `vfsinit` returns. So the **next step is the
first one that leaves `vfsinit` for `bsd_init`'s own statement list**, which is where 425 predicted
`nwk_wq_init` (`bsd_init + 0x808`, key `0x8003B1FC`) and where the walk's headline answer has been
pointing since rule 426. If `decmpfs_init`'s body is clean, the next run's stop is the first name the
walk has been answering all along, one function after this one.

## Layout

    entry text   2295872 (0x230840)
    entry image  2424580 (0x24FF04)
    bss          0x8024FF40 .. 0x80291A58 (269080 bytes; 0x5C0 of it the 23 desc tables)
    layout       args 0x80293000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1500584 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 55371fe865006084c27df4ecfa3c01a42ed5004823531292215d693db770d3b2 (5443584 bytes)

Run markers agree with the link: `xnu_entry_image_bytes=0x0024FF04`,
`xnu_entry_bss_start=0x8024FF40`, `xnu_entry_args_pa=0x80293000`,
`xnu_entry_checks=0x00000005`, `xnu_entry_failures=0x00000000`, `xnu_entry_checksum=0x90402A59`,
`xnu_entry_entering_at=0x80000074`. The stub counts **771 / 619 / 152** are exactly the effect tool's
prediction, including the 23 new storage records. Every marker differs from 428's.

## Where the frontier is now

**`decmpfs_init`** (`bsd/kern/decmpfs.c`, defined by `bsd_kern_decmpfs.o`), reached from
`vfsinit + 0x5B4`, key `0x801b3e38`. It is the **last** stub call in `vfsinit` — the function returns
from here — so the step that retires it is the one that hands the walk back to `bsd_init`'s own
statement list, one frame up. Everything 425's batch retired (`mbinit`, the pipes, the POSIX
semaphores and the rest) is on the far side of that return.

**Still owed and unchanged: the timer.** This step's bodies take no deadline — `vfs_context_kernel` is
two field initializations, and the wrappers it brings in are called from the syscall layer, not from
here. But the layer the next step enters is `bsd_init`'s mountmachinery, and `IOFindBSDRoot` and the
mount loop's waits are unbounded while `cpu_set_decrementer_func` is NULL. The two pieces remain
`ml_init_timebase` with an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`, and 405's
`IOCPUInterruptController`.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure); nothing flashed, nothing written to storage.
25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`,
`checks=5` / `failures=0`, no `exception:`, no `panic`; 301809 bytes / 3978 lines, ending
`No errors detected`. The hardware watchdog was the only net armed across the jump and is what ended
the run. Device returned to Android on its own and was confirmed there (`adb devices` shows
`4a2fe00b`). Per-run logs stay apart: `/tmp/run425_kmsg.txt`, `/tmp/run427_kmsg.txt`,
`/tmp/run428_kmsg.txt`, `/tmp/run429_kmsg.txt`.
