# Experiment 414 — `security/mac_process.c`: the whole `mac_cred_label_*` family goes real, and the frontier moves to `file_lock_init`

**Step:** one object linked — `security_mac_process.o`, the pool's only definer of `mac_cred_label_init`
and therefore of 413's stop — appended to `LINK_OBJS` after `bsd_kern_sys_ulock.o`.

**Prediction:** `stub_hit=file_lock_init`, caller key `0x8003B024`. **Measured:** exactly that,
`abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

    line 3938: xnu_entry_stub_caller_v=0x8003b024
    line 3942: xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=file_lock_init
    (no `exception:`, no `panic`; log ends `No errors detected`)

This is the step 413's prediction was *for* — it lands on the same name and key one run later, with the
guarded site that pre-empted it in between now retired.

## The step's effect

    17 resolved (17 function, 0 storage) / 0 added (0 function, 0 storage)
      resolved  mac_cred_label_init, _destroy, _compare, _associate, _associate_kernel,
                _associate_user, _associate_fork, _alloc, _free, _externalize, _internalize,
                _update, _check_label_update, mac_proc_check_fork, mac_proc_check_get_cs_info,
                mac_proc_check_proc_info, mac_proc_check_set_cs_info
      19 references, all 19 already satisfied

    774 -> 757 stub names, 653 -> 636 function, 121 -> 121 storage

All three counts are exactly as the tool predicted before the build. Like 412's, this object can retire
stubs and cannot create one.

## The two readings that made the prediction, one of which 413 did not have

**1. The guarded lists, not just the straight lines.** 413's defect 128 was reading the walk's first line
and not its second list. This time every function on the path from the stop to `file_lock_init` had *both*
lists read — `kauth_cred_create`, `kauth_cred_alloc`, `kauth_cred_ref`, the six `kauth_cred_get*`,
`posix_cred_label`, `current_thread` — and the only stub they name in the guarded list is the four
`mac_cred_label_*` this step retires. `file_lock_init` is then `bsd_init`'s own next stub at `+0x630`.

**2. The `zalloc` path, which is new territory.** `mac_cred_label_init` calls `mac_cred_label_alloc`,
which calls `mac_labelzone_alloc` — and that is the first time this boot would run `zalloc` for real.
`mac_labelzone_alloc` is `if (flags & MAC_NOWAIT) zalloc_noblock(...) else zalloc(...)`, and
`mac_process.c:88` passes `MAC_WAITOK`, which `security/mac_policy.h:6960` defines as **0**, so the
`tst r1,#1 / bne` at `+0x18` falls through to `zalloc`. The tool's guarded list for that whole chain names
stubs — `OSBacktrace`, `btlog_add_entry`, `trace_backtrace` — each "a stub 1 guard further", and **the
stopping rule was resolved by reading the source, not by walking**: those three sit behind
`__improbable(DO_LOGGING(zone))` and the `#if ZONE_DEBUG` block, and a zone's `zlog_btlog` is NULL unless
`Z_LOGGING` is set, which `mac_label.c:43` does not set (it sets only `Z_EXPAND`, `Z_EXHAUST`,
`Z_CALLERACCT`). The walk's own header says it cannot know which conditions hold; a source reading can,
and that is what closed it.

**And the bound on the whole MAC machinery: there is no MAC policy loaded.** `mac_policy_init` sets
`mac_policy_list.numloaded = 0` and nothing in this image is a kext, so no `mpo_*` callback behind
`mac_policy_list_conditional_busy` is reachable and `mac_cred_label_alloc`'s policy loop never runs. The
step allocates one zone element and returns.

**Falsifiers, named in advance and all silent:** a stop inside any of the seventeen (every call target of
all seventeen was checked against the stub list — `mac_labelzone_alloc`, `mac_policy_list_conditional_busy`,
`mac_policy_list_unbusy`, `mac_error_select`, `mac_externalize`, `posix_cred_get`, `bcmp`, all real); a
stop at `OSBacktrace`, `btlog_add_entry` or `trace_backtrace` (the `zalloc` logging guards, off by the
source reading above); a stop at `proc_list_lock` or `posix_cred_label` (already real); a stop at
`mac_cred_label_associate_kernel`, `bsd_init + 0x63C`, which would mean this step had retired
`file_lock_init` too.

## Layout

    stubnames    757 records (636 function, 121 storage)
    text size    1855296 (0x1C5000)   — 413's 1848224 + 0x1DE0
    image bytes  1977244 (0x1E2B9C)   — 413's 1960836 + 0x16418
    bss          0x801E2BC0 .. 0x8021FED8 (250648 bytes)
    layout       args +2232320 (0x80221000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     1966376 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 f9a55c88b07b1842e48e90cae34a8dfc7146a69ce7153bec6e216b632f12d903

The run's markers agree to the byte: `image_bytes=0x001E2B9C`, `bss_start=0x801E2BC0`,
`bss_end=0x8021FED8`, `args_pa=0x80221000`, `checksum=0x9043EEC1`.

## Where the frontier is now

`file_lock_init`, defined by `bsd/kern/kern_descrip.c` (`out/xnu_kernel_obj/bsd_kern_kern_descrip.o`) —
`bsd_init`'s statement at `+0x630`, and the first stub on that line after the credential work. It is the
first of a run of stubs that were only three calls apart in the source but far apart in the compiled
line: `file_lock_init`, then `mac_cred_label_associate_kernel` (`+0x63C`), then `chgproccnt`,
`kmem_suballoc`, `bsd_bufferinit`, `IOKitInitializeTime`, `ubc_init`, `vfsinit`…

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301808 bytes, ending `No errors detected`. Device returned to
Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
