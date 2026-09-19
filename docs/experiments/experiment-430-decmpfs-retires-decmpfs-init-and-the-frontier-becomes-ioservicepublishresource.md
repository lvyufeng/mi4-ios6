# Experiment 430 — `bsd/kern/decmpfs.c`: `decmpfs_init` retires, and the frontier becomes `IOServicePublishResource` — the first key that had to be derived rather than carried forward

**Step:** one object linked — `bsd_kern_decmpfs.o`, the pool's only definer of `decmpfs_init`, appended
to `LINK_OBJS` after `bsd_vfs_kpi_vfs.o` and before `STAGE90_PLATFORM_EXPERT_OBJ`.

**Effect:** **1 resolved (1 function, 0 storage) / 3 added (3 function, 0 storage)** — the narrowest
step since 423, because `vfsinit`'s line had exactly one stub left on it. Counts **771 -> 773**
undefined, **619 -> 621** function, **152 -> 152** storage: `decmpfs_init` out;
`IOServicePublishResource`, `IOServiceWaitForMatchingResource` and `IOCatalogueMatchingDriversPresent`
in, as new function stubs this object's own read path references. All three counts are exactly what
`tools/entry_object_effect.py` predicted.

    bsd_kern_decmpfs.o   1 resolved / 3 added (IOServicePublishResource, IOServiceWaitForMatchingResource,
                                              IOCatalogueMatchingDriversPresent)

## The step's real content: an address that had to be derived, not looked up

Every caller key since 422 has been `<function> + <offset>` where the function was linked long ago and
therefore did not move — `vfsinit` has been at `0x801B3880` since 425 and is at `0x801B3880` again
here, which is why 429's key (`0x801b3e38`) was computable from the 428 map. **This step's stop is
inside the object being linked**, so its address had to come from where the linker would put that
object's `.text`, and nothing in the previous image could be read off to get it.

The derivation is a chain the 425–429 maps already checked, and this step checks it a fifth time:
`*(.text .text.*)` in `entry.ld:45` places each object's `.text` in command-line order with no gap
beyond an input's own alignment, and the last four objects of `LINK_OBJS` ahead of the platform expert
are contiguous to the byte in the 429 map:

    bsd_kern_sys_generic.o   0x801ED620 + 0x3D64 = 0x801F1384   <- vfs_quota.o starts here
    bsd_vfs_vfs_quota.o      0x801F1384 + 0x1900 = 0x801F2C84   <- mac_vfs.o starts here
    security_mac_vfs.o       0x801F2C84 + 0x756C = 0x801FA1F0   <- kpi_vfs.o starts here
    bsd_vfs_kpi_vfs.o        0x801FA1F0 + 0x53A0 = 0x801FF590   <- the platform expert, before this step

So `bsd_kern_decmpfs.o` takes **0x801FF590** as the start of its `.text` (its own alignment is 16 and
0x801FF590 is 16-aligned, so the chain carries with no fill), and from the object's own `nm -S`:

    decmpfs_init                0x801FF590 + 0x3294 = 0x80202824
    its `bl <IOServicePublishResource>` is at object +0x3370, i.e. decmpfs_init + 0xDC,
    so the return address the stub reporter records is decmpfs_init + 0xE0
    caller key                  0x80202824 + 0xE0 = 0x80202904

**Prediction, written before the build:** `stub_hit=IOServicePublishResource`, caller key
`0x80202904`, with `register_decmpfs_decompressor` at `0x802006C0` and
`unregister_decmpfs_decompressor` at `0x80200760` as two extra `nm`-checkable addresses in the new
image. **Measured on hardware: exactly that** — `xnu_entry_stub_caller_v=0x80202904`.

    line 3894: MI4IOS6_STAGE90_XNU kernel_entry ok
    line 3917: MI4IOS6_STAGE90_XNU xnu_entry_checks=0x00000005
    line 3918: MI4IOS6_STAGE90_XNU xnu_entry_failures=0x00000000
    line 3938:  xnu_entry_stub_caller_v=0x80202904
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=IOServicePublishResource
    line 3978: No errors detected

and all three addresses are the predicted ones to the byte:

    80202824 T decmpfs_init
    802006c0 T register_decmpfs_decompressor
    80200760 T unregister_decmpfs_decompressor

The two names the block predicted only as a cross-check are the part worth noting: they are *not* on
the frontier and nothing would have stopped if they had been wrong, so they are a check on the
derivation rather than on the run — a second and third sample of the same claim, at +0x1130 and
+0x11D0 instead of +0x3294.

## The reading: the compiler folded the guards away, and what is left is two `.bss` reads

The source's `decmpfs_init` (`bsd/kern/decmpfs.c:1858`) ends in
`register_decmpfs_decompressor(CMP_Type1, &Type1Reg)`, and clang inlined that function *and both of
its tests*: `compression_type >= CMP_MAX` and `registration_valid(registration)` (`decmpfs.c:952`, a
`static inline`) are functions of compile-time constants, so neither survives in the code. The whole
function is 0xF8 bytes, eleven calls and **two** conditional branches, and both of them are guards on
a word this object's own `.bss` holds:

    decmpfs_init + 0x10   ldrb r0, [r5]        r5 = &decmpfs_init.done, `.bss +0x410`
    decmpfs_init + 0x18   cmp  r0, #0          the idempotence guard; `bne` leaves the function
    decmpfs_init + 0xA0   ldr  r1, [r0, #4]    r0 = decompressors, r1 = decompressors[CMP_Type1]
    decmpfs_init + 0xA4   cmp  r1, #0
    decmpfs_init + 0xA8   bne  +0xE0           skip the publish if one is already registered

`decompressors` is `SECURITY_READ_ONLY_EARLY(static decmpfs_registration *) decompressors[CMP_MAX]`
(`decmpfs.c:200`), which is what puts it in `.bss` — 0x3FC bytes, i.e. 255 × 4, matching
`CMP_MAX = 255` at `bsd/sys/decmpfs.h:68`. The payload zeroes `.bss`, so both guards read zero and
both paths are taken. Note also that the second branch lands on exactly `decmpfs_init + 0xE0`, the
instruction after the `bl` at `+0xDC`: the skipped path and the returned path rejoin at the same
address, which is why the caller key and the branch target are the same number.

**Both branches in this function are guards on storage, and the payload's `bzero` is what decides
them.** That is the reading this walk has used since 342, here in its cleanest form — and it is worth
saying plainly that this is *not* the safety argument of 428 or 429: those two rested on a real
allocation and on `bsd_init`'s statement order, whereas this one rests on nothing having written the
array yet, which is what `decmpfs_init` is for.

Every other call on the line is real, and 429 is what made them real: `vfs_context_kernel` (the
previous step's own name), `vfs_context_create`, the whole `lck_*` family
(`lck_grp_attr_alloc_init`, `lck_grp_alloc_init`, `lck_grp_attr_free`, `lck_rw_alloc_init`,
`lck_mtx_alloc_init`, `lck_rw_lock_exclusive`, `lck_rw_unlock_exclusive`) and `snprintf`.

## The two read-path names, and why a stop on them would have been a much bigger statement

Of the three names this step makes undefined, only `IOServicePublishResource` is on the line. The
other two are referenced only from `_decmp_get_func` (`decmpfs.c:241`, calls at 259, 267 and 271) —
the decompressor *lookup* that the read path runs, whose loop can wait for a kext to register. A stop
on either of them would have meant something opened a compressed file before the frontier, which is a
far larger claim than this step makes, so both are falsifiers rather than predictions.

## Layout, and the alignment boundary that ate the whole text growth

    entry text   0x233F20 (2309920)
    entry image  0x2502AC (2425516 bytes)
    bss          0x802502C0 .. 0x802921D8 (270104 bytes, zeroed by the payload)
    layout       args 0x80294000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1498664 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 83b8e814dd622b855cd414914f6efc18f22643b43900da80a9b5b1daa770d600 (5445632 bytes)
    entry bin    2425516 bytes; sha256 d23e4d004ff98e7b64b6e0d43284b694d5d651a0c16576343223df787b794ae7

Run markers agree with the link: `xnu_entry_image_bytes=0x002502AC`,
`xnu_entry_bss_start=0x802502C0`, `xnu_entry_bss_end=0x802921D8`, `xnu_entry_args_pa=0x80294000`,
`xnu_entry_top_of_kernel_data=0x80400000`, `xnu_entry_checks=0x00000005`,
`xnu_entry_failures=0x00000000`, `xnu_entry_checksum=0x904061F1`, `xnu_entry_entering_at=0x80000074`.
Every marker differs from 429's — which is the check that the image that ran is the image that was
built, and it is the reason 429's own key appearing here would have been a defect and not a success.

**`.text` grew 0x36E0 and the image grew only 0x3A8, and the difference is an alignment boundary
rather than a mistake.** `.data` is placed at 0x80234000 in both images because both `.text` ends
round up to that boundary: the gap between `.text` and `.data` was `0x80234000 - 0x80230840 = 0x37C0`
in 429 and is `0x80234000 - 0x80233F20 = 0xE0` here, so **0x36E0 of text growth was swallowed by the
gap and nothing below `.text` moved for it.** What the image did grow is `bsd_kern_decmpfs.o`'s own
`__DATA, __data`: 0x3A8 at 0x8024FC70, which is the image delta to the byte. `.bss`'s start moved by
0x380 — 0x3A8 minus the 0x28 by which the `.init_array`-to-`.bss` alignment gap shrank — and `.bss`'s
size grew 0x400 against the object's own 0x411, the 0x11 being the fill term: the same rule that made
`.text`'s earlier predictions miss by 0x12 and 0x55.

The image is reproducible from the committed sources: rebuilding both stages after the comment block
above was finished produced byte-identical `xnu_arm_entry.bin` (`d23e4d00…`) and
`stage90-qcdt.img` (`83b8e814…`), so the payload that ran is the one this tree builds.

## Where the frontier is now

**`IOServicePublishResource`** (`iokit/bsddev/IOKitBSDInit.cpp`), reached from `decmpfs_init + 0xDC`,
key `0x80202904`. **This is the first frontier in this walk whose definer is an I/O Kit object**, and
it is on the far side of a return: `decmpfs_init` returns to `vfsinit` at `+0x5B8` (429's own caller
key) and `vfsinit` then returns, so 431 is the first step whose run walks back out to `bsd_init`'s own
statement list — where 425 predicted `nwk_wq_init` (`bsd_init + 0x808`, key `0x8003B1FC`).

**431 is named in advance.** `IOServicePublishResource` is defined by exactly one object in the
695-object pool, **`iokit_bsddev_IOKitBSDInit.o`**, which also defines all three of this step's added
names plus `IOFindBSDRoot`, `IORamDiskBSDRoot`, `IOSecureBSDRoot`, `IOKitBSDInit`,
`IOBSDNameMatching`, `IOBSDGetPlatformUUID`, `IOBSDMountChange`, `IOTaskHasEntitlement` and
`IONetworkNamePrefixMatching`. Measured against the 429 image it is 6 resolved / 4 added
(`di_root_ramfile`, `mdevadd`, `mdevlookup`, `mdevremoveall`); against *this* image it will be
**9 resolved / 4 added**, 773 -> 768 undefined, because the three names this step creates are three of
the nine it retires. Its `.text` is 0x133C, 0x10 of `.bss`, 0x332 of mergeable `.rodata.str1.1`, and
no `__DATA, __data` at all.

**Still owed and unchanged: the timer.** Nothing on this step's path takes a deadline — `decmpfs_init`
allocates locks and publishes a property — but 431 is the object that holds `IOFindBSDRoot`, i.e. the
root-mount layer, and the mount loop's waits are unbounded while `cpu_set_decrementer_func` is NULL.
The two pieces remain `ml_init_timebase` with an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`, and
405's `IOCPUInterruptController`. **The timer stops being a footnote at this point in the walk.**

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure, so both gates ran for this step); nothing
flashed, nothing written to storage. 25 × `persistent_write_attempted=0x00000000`, 87 ×
`failure_mask=0x00000000`, `abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`;
301821 bytes / 3978 lines, ending `No errors detected`. The only net armed across the jump was the
hardware watchdog (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bite_truncated=0x00000000`) and it is what ended the run; the dead-man PPI was disarmed
before the jump (`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Device returned to Android on its own
and was confirmed there (`adb devices` shows `4a2fe00b`).

Per-run logs stay apart: `/tmp/run425_kmsg.txt`, `/tmp/run427_kmsg.txt`, `/tmp/run428_kmsg.txt`,
`/tmp/run429_kmsg.txt`, `/tmp/run430_kmsg.txt`.
