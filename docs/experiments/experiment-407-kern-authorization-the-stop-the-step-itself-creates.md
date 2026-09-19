# Experiment 407 — `bsd/kern/kern_authorization.c`: the step whose stop is a name the step itself creates, one call into the function it makes real

**Step:** one object linked — `bsd_kern_kern_authorization.o`, the pool's only definer of `kauth_init`
(`bsd/kern/kern_authorization.c:143`) and therefore of 406's stop — appended to `LINK_OBJS` after
`bsd_miscfs_specfs_spec_vnops.o`.

**Prediction:** `stub_hit=kauth_cred_init`, caller key = the linked address of `kauth_init + 0x58`.
**Measured:** `stub_hit=kauth_cred_init` at `xnu_entry_stub_caller_v=0x80188A38` — name and key exactly as
predicted (`0x80188A38` is the return address of the `bl 8018b160 <kauth_cred_init>` at `0x80188A34`),
`abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

## The step's effect

    1 resolved (1 function, 0 storage) / 11 added (10 function, 1 storage)
      resolved  kauth_init
      added     cantrace, get_pathbuff, kauth_cred_getguid, kauth_cred_init,
                kauth_cred_ismember_guid, kauth_guid_equal, kauth_null_guid (storage, B 0x10),
                kauth_wellknown_guid, release_pathbuff, vfs_authopaque, vnode_mount

and the build confirms it to the record: **827 → 837** stub names, **696 → 705** function (−1, +10) and
**131 → 132** storage (+1). `kauth_init` is gone from the list.

## Why the stop is a name the step creates — 342/348's shape, and this time it is unavoidable

`kauth_init`'s body makes seven calls, and the first three are real:

    +0x28  bl lck_grp_attr_alloc_init   real (osfmk_kern_locks.o) — walked: no stub beneath it
    +0x3C  bl lck_grp_alloc_init        real (osfmk_kern_locks.o) — walked: no stub beneath it
    +0x50  bl lck_grp_attr_free         real (osfmk_kernel_locks.o) — walked: no stub beneath it
    +0x54  bl kauth_cred_init           **the stub this step creates**
    +0x60  bl lck_mtx_alloc_init        real
    +0x84/.a8/.c8  bl kauth_register_scope x3   real (defined in this same object, linked here)
    +0xd4  bl lck_grp_free              real

so the frontier cannot be *outside* the object: the object's own body is what the walk stands in, and the
hole in it is a name no linked object defines yet. `kauth_cred_init` is the fourth call and the first that is
not real. The three before it are the reason the stop is not one call earlier, and each was walked rather
than assumed.

**And 342's trap is checked and comes out empty:** of the 11 names this step creates, **`bsd_init` calls
none** — the intersection of the added column with `bsd_init`'s 696-instruction call set is empty, so no
stop can be reached from the caller instead of from `kauth_init`.

## Layout

    counts       827 -> 837 records (705 function, 132 storage)
    text size    1792096 (0x1B58C0)   — 406's 1786464 + 0x15C0
    image bytes  1911216 (0x1D29B0)   — 406's 1911120 + 0x60
    bss          0x801D29C0 .. 0x8020FE18 (250968 bytes)
    layout       args +2166784 (0x80211000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     2032104 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 ee2bcada9b45a8b0006445c43a7e925fc265ea00189aaa1d0c139787ab66fba9

The run's own markers agree: `bss_bytes=0x0003D458`, `image_bytes=0x001D29B0`, `bss_start=0x801D29C0`,
`bss_end=0x8020FE18`, `args_pa=0x80211000`.

## The frontier

`kauth_cred_init`, defined by `bsd/kern/kern_credential.c:3499` — an object measuring **23 resolved (22
function, 1 storage) / 7 added**, which is what turns 407's newly created credential face real in one step.

## Safety

A non-persistent `fastboot boot` of the image above through both gated scripts; nothing flashed. Preflight
green. 25 records of `persistent_write_attempted=0x00000000`, 87 of `failure_mask=0x00000000`, none non-zero;
`abort_entries=0`; `checks=5` / `failures=0`; `xnu_entry_checksum=0x9041EE2D`; no `panic`, no `exception:`;
301627 bytes, log ends `No errors detected`. Device returned to Android on its own and was confirmed there
(`adb devices` shows `4a2fe00b`).
