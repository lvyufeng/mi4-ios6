# Experiment 408 — `bsd/kern/kern_credential.c`: the object that empties `kauth_init`'s frame, and the frontier steps one call further into `bsd_init`

**Step:** one object linked — `bsd_kern_kern_credential.o`, the pool's only definer of `kauth_cred_init`
(`bsd/kern/kern_credential.c:3499`) — appended to `LINK_OBJS` after `bsd_kern_kern_authorization.o`.

**Prediction:** `stub_hit=procinit`, caller key = the linked address of the instruction after
`bl kauth_init` in `bsd_init` (`bsd_init + 0x1C4`).
**Measured:** `stub_hit=procinit` at `xnu_entry_stub_caller_v=0x8003ABB8` — name and key exactly as
predicted (`0x8003ABB8` is the return address of the `bl 8018e9f0 <procinit>` at `0x8003ABB4`),
`abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

## The step's effect: the largest resolution since 381

    23 resolved (22 function, 1 storage) / 7 added (7 function, 0 storage)
      resolved  kauth_cred_create, kauth_cred_get, kauth_cred_get_with_ref, kauth_cred_getgid,
                kauth_cred_getguid, kauth_cred_getrgid, kauth_cred_getruid, kauth_cred_getsvgid,
                kauth_cred_getsvuid, kauth_cred_getuid, kauth_cred_init, kauth_cred_ismember_guid,
                kauth_cred_issuser, kauth_cred_proc_ref, kauth_cred_ref, kauth_cred_unref,
                kauth_getruid, kauth_getuid, kauth_guid_equal, kauth_null_guid (storage, B 0x10),
                kauth_proc_label_update, kauth_wellknown_guid, posix_cred_label
      added     mac_cred_label_associate, mac_cred_label_compare, mac_cred_label_destroy,
                mac_cred_label_init, mac_cred_label_update, mac_cred_label_update_execve, proc_ucred

The build confirms every column: **837 → 821** stub names, **705 → 690** function (−22, +7) and
**132 → 131** storage (−1). **This is the first step in the walk that retires more than twenty names at
once, and it is the step that closes 407's own creation**: five of the eleven names 407 created
(`kauth_cred_getguid`, `kauth_cred_ismember_guid`, `kauth_guid_equal`, `kauth_null_guid`,
`kauth_wellknown_guid`) are defined here, one step later.

## The reading that produced the prediction

Three parts, each measured in the object or the image the step would start from:

1. **`kauth_cred_init`'s own body has no stub.** It calls `lck_mtx_alloc_init` (real), `__MALLOC` (real
   since 377, and `xnu_entry_callwalk.py --root __MALLOC` reaches no stub on its straight line) and
   `panic` (real); everything else is a fully unrolled `TAILQ_INIT` loop over `KAUTH_CRED_TABLE_SIZE`
   with no calls at all.
2. **`kauth_init`'s frame empties.** After the call returns, the path is `lck_mtx_alloc_init` (real),
   `kauth_register_scope` ×3 (defined in `kern_authorization.o`, linked at 407) and `lck_grp_free`
   (real); `kauth_register_scope`'s own body calls `__MALLOC`, `lck_mtx_lock`, `strlen`, `strncmp`,
   `kauth_add_callback_to_scope` (same object) and `lck_mtx_unlock` — **all real**.
3. **So the stop is the caller's next statement.** `bsd_init`'s line after `bl kauth_init` is
   `bl procinit` at `+0x1C4`, a stub since 406.

**Falsifiers, named in advance:** the real `panic("startup: kauth_cred_init")` at object `+0x60`, taken iff
the 0xC800-byte `MALLOC` for the credential hash table returns NULL — **the one kind-6 risk on this path,
and the log tells a panic from a stub in one line**; a stop inside `kauth_cred_init`; a stop inside
`kauth_register_scope`; a stop on one of the 7 added names (the intersection of those 7 with `bsd_init`'s
call set is empty, and `kauth_cred_init` calls none of them). **None fired** — the run reported a stub, at
the predicted key.

## Layout

    counts       837 -> 821 records (690 function, 131 storage)
    text size    1802336 (0x1B7EE0)   — 407's 1792096 + 0x10240
    image bytes  1927656 (0x1D69E8)   — 407's 1911216 + 0x16438
    bss          0x801D6A00 .. 0x80213E58 (250968 bytes)  — moved by the `.bss`-resident stub slots
    layout       args +2183168 (0x80215000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     2015656 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, 4947968 bytes, sha256
                 4a1b4320cb42463a55f1e55c6e1c8d8712bed69eea613e786605cabca38b549c

The run's markers agree to the byte: `bss_bytes=0x0003D458`, `image_bytes=0x001D69E8`,
`bss_start=0x801D6A00`, `bss_end=0x80213E58`, `args_pa=0x80215000`.

## Where the frontier is now

`bsd_init`'s line, three calls in a row: `kauth_init` (407, real) → **`procinit`** → `tty_init`. `procinit`
is defined by `bsd/kern/kern_proc.c`, and the walk is now retiring one stub per object along `bsd_init`'s
own statement list — the BSD kernel's init sequence, with the credential and authorization subsystems real
behind it.

## Safety

A non-persistent `fastboot boot` of the image above through both gated scripts; nothing flashed. Preflight
green. 25 records of `persistent_write_attempted=0x00000000`, 87 of `failure_mask=0x00000000`, none non-zero;
`abort_entries=0`; `checks=5` / `failures=0`; `kv_written=0x59`, `kv_in_dram=0x7D`; no `panic`, no
`exception:`; 301620 bytes, log ends `No errors detected`. Device returned to Android on its own and was
confirmed there (`adb devices` shows `4a2fe00b`).
