# Experiment 413 — `bsd/kern/sys_ulock.c`: `ulock_initialize` retires, and the run stops one level down at a guarded site the walk had already named

**Step:** one object linked — `bsd_kern_sys_ulock.o`, the pool's only definer of `ulock_initialize`
(`bsd/kern/sys_ulock.c:185`) and therefore of 412's stop — appended to `LINK_OBJS` after
`bsd_kern_kern_overrides.o`.

**Prediction:** `stub_hit=file_lock_init`, caller key `0x8003B024`. **Measured: `stub_hit=mac_cred_label_init`
at `xnu_entry_stub_caller_v=0x8018ADB4` — the prediction was wrong**, and the reason it was wrong is the
point of this experiment. `abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

    line 3935: xnu_entry_stub_caller_v=0x8018adb4
    line 3939: xnu_entry_abort_entries=0x00000000
    line 3970: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=mac_cred_label_init
    line 3975: No errors detected

## The step's effect — exactly as predicted

    2 resolved (2 function, 0 storage) / 2 added (2 function, 0 storage)
      resolved  ulock_initialize, kdp_ulock_find_owner
      added     port_name_to_thread_for_ulock, workqueue_get_sched_callback

    774 -> 774 stub names, 653 -> 653 function, 121 -> 121 storage

The three counts are unmoved because two go out and two come in, as the tool predicted before the build.
`ulock_initialize` is gone from the list and both added names are present, measured after the link.

## Why the prediction missed — the walk's first line and its second list

The reading was the one this project has used since 226: `xnu_entry_callwalk.py --root <fn>` on each
non-trivial callee between `ulock_initialize`'s return and `file_lock_init`. Twelve of them returned
**"reached no stub on the straight-line path"**, including `kauth_cred_create`. The device stopped inside
exactly that function:

    walk from kauth_cred_create reached no stub on the straight-line path.
      ...
      if a conditional branch on that path is taken instead, the stop is
      the first of these whose condition holds, in execution order:
        kauth_cred_create+0x5c -> mac_cred_label_init   STUB
        kauth_cred_find+0x5c -> mac_cred_label_compare   STUB
        kauth_cred_create+0xcc -> mac_cred_label_init   STUB
        kauth_cred_create+0x144 -> mac_cred_label_destroy   STUB

**The tool named the stop, in the first entry of its own second list, and the prediction read only the
first line.** The two lines say different things: the first is about the *straight line*, the second is
about the *guarded sites on that path in execution order* — and the tool's own header says a run can stop
earlier than the straight-line answer on a conditional branch that is taken, "and no static walk can know
that". The run took one, at `+0xCC`.

The site is the inlined copy of `kauth_cred_alloc`. Its body is in this image twice:

    8018ac80 <kauth_cred_alloc>:      8018acd8  bl mac_cred_label_init     (+0x58)
    8018ace4 <kauth_cred_create>:     8018ad40  bl mac_cred_label_init     (+0x5c)
                                      8018ad74  bl __MALLOC_ZONE
                                      8018ad8c  bl __bzero
                                      8018adb0  bl mac_cred_label_init     (+0xcc)  <- the stop
                                      8018adf4  bl bcopy
                                      8018ae0c  bl kauth_cred_add
                                      8018ae28  bl mac_cred_label_destroy

`kauth_cred_alloc`'s source is `MALLOC_ZONE` → `if (newcred != 0) { … fields … }` →
`#if CONFIG_MACF mac_cred_label_init(newcred)`. GCC emitted it once out of line and once inlined into
its only caller, and the run reached the inlined copy's site at `+0xCC` — not the `+0x5C` copy, which sits
on a path the run did not take.

**This is measurement defect 128**, and its tell is written in the tool's own output: *a negative from a
straight-line walk is not a negative for the function* — read the guarded list before predicting a step
whose path runs through a function with branches.

## The step itself is sound

Nothing about the miss touches what the step was for. The object's two created names are real stubs but
**neither is reachable from `ulock_initialize` or from `bsd_init`'s line** (they belong to
`ulock_wait`/`ulock_wake` and the workqueue callback, which only user space reaches), so 342's trap is
checked and empty. And the two `assert`s that would have panicked the body were checked against a
*measured* value, not an assumed one:

    assert(thread_max > 16);
    ull_hash_buckets = (1 << (bit_ceiling(thread_max) - 2));
    assert(ull_hash_buckets >= thread_max/4);
    ull_bucket = kalloc(sizeof(queue_head_t) * ull_hash_buckets); assert(ull_bucket != NULL);

`thread_max` is real data (`thread.o`: `int thread_max = CONFIG_THREAD_MAX`), and `startup.c:803`'s
`thread_max = task_max * 5` sits inside `#if defined(__LP64__)`, which this ARMv7 build does not take.
Read out of the linked image's `.data` at `0x801D4378`: **0x00000600 = 1536**. So `bit_ceiling = 11`,
`ull_hash_buckets = 512`, `512 >= 384` holds, and the `kalloc` is 2048 bytes. No panic fired.

## Layout

    stubnames    774 records (653 function, 121 storage)
    text size    1848224 (0x1C33E0)   — 412's 1845312 + 0xB60
    image bytes  1960836 (0x1DEB84)   — 412's 1960812 + 0x18
    bss          0x801DEBC0 .. 0x8021BED8 (250648 bytes)
    layout       args +2215936 (0x8021D000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     1982760 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 2a3379bf71524bb556288509fd6204d7614f6bcc2540bfd67fd01a04a6751675

The run's markers agree to the byte: `image_bytes=0x001DEB84`, `bss_start=0x801DEBC0`,
`bss_end=0x8021BED8`, `args_pa=0x8021D000`, `checksum=0x90406ED9`.

## Where the frontier is now

`mac_cred_label_init`, defined by **`security/mac_process.c`** (`out/xnu_kernel_obj/security_mac_process.o`),
which measures **17 resolved (17 function, 0 storage) / 0 added**, with all 19 of its references already
satisfied — the `mac_cred_label_*` family (`init`, `destroy`, `compare`, `associate`,
`associate_kernel`, `associate_user`, `associate_fork`, `alloc`, `free`, `externalize`, `internalize`,
`update`, `check_label_update`) and four `mac_proc_check_*`. And every call target of those seventeen was
checked against the stub list: `mac_labelzone_alloc`, `mac_policy_list_conditional_busy`,
`mac_policy_list_unbusy`, `mac_error_select`, `mac_externalize`, `posix_cred_get`, `bcmp` — **all real**.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301631 bytes, ending `No errors detected`. Device returned to
Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
