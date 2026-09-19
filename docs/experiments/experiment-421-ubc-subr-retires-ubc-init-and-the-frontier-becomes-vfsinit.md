# Experiment 421 — `bsd/kern/ubc_subr.c`: `ubc_init` retires, and the frontier becomes `vfsinit`

**Step:** one object linked — `bsd_kern_ubc_subr.o`, the pool's only definer of `ubc_init`
(`bsd/kern/ubc_subr.c:761`) and therefore of 420's stop — appended to `LINK_OBJS` after
`bsd_kern_kern_synch.o`.

**Prediction:** `stub_hit=vfsinit`, caller key `0x8003B1BC` (`bsd_init + 0x7C8`).
**Measured: exactly that.**

    line 3930: MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
    line 3938:  xnu_entry_stub_caller_v=0x8003b1bc
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vfsinit
    line 3974:  xnu_entry_stub_caller=0x8003b1bc
    line 3978: No errors detected

`xnu_entry_stub_caller_w0=0x33303038` / `w1=0x63623162` are the same address as ASCII — `8003` and
`b1bc`. The image on disk agrees: `bl <vfsinit>` is at `0x8003B1B8` and `vfsinit` links at `0x801B6488`.

## The step's effect

     26 resolved (26 function, 0 storage) / 20 added (19 function, 1 storage)
      resolved  ubc_init with the whole `ubc_*` face around it: UBCINFOEXISTS, cs_hash_type,
                cs_validate_range, csblob_find_blob, csblob_get_entitlements, mach_to_bsd_errno,
                ubc_blktooff, ubc_create_upl_kernel, ubc_cs_blob_add, ubc_cs_blob_allocate,
                ubc_cs_blob_deallocate, ubc_cs_blob_get, ubc_cs_blob_revalidate,
                ubc_cs_validation_bitmap_allocate, ubc_getobject, ubc_getsize, ubc_init,
                ubc_page_op, ubc_range_op, ubc_strict_uncached_IO, ubc_upl_abort,
                ubc_upl_abort_range, ubc_upl_commit_range, ubc_upl_map, ubc_upl_pageinfo,
                ubc_upl_unmap
      added     SHA384_Final, SHA384_Init, SHA384_Update, VNOP_MMAP, VNOP_MNOMAP, add_fsevent,
                lock_vnode_and_post, mac_vnode_check_signature, memchr, need_fsevent,
                vnode_getname_printable, vnode_iterate, vnode_lock_spin, vnode_mtime,
                vnode_pager_deallocate, vnode_pager_setup, vnode_putname_printable, vnode_ref_ext,
                vnode_size — plus the **storage** stand-in `root_fs_upgrade_try` (`B 0x4`)

    787 -> 781 stub names, 667 -> 660 function, 120 -> 121 storage

All three counts are exactly as `tools/entry_object_effect.py` predicted, and the storage column rises
by one because of that one `B` stand-in.

## The reading: `ubc_init` has four instructions of substance, and both of its callees have already run

The object's own disassembly is the whole body:

    mov  r0, #64                    ; sizeof(struct ubc_info)
    movw r1, #50176 ; movt r1, #9   ; 640000 = 10000 * 64
    mov  r2, #8192                  ; the alloc quantum
    bl   zinit                      ; 0x8006DE00
    str  r0, [ubc_info_zone]        ; 0x80239814, in this image
    pop  {fp, lr}
    b    zone_change                ; 0x800709A0 - a TAIL CALL

The last instruction is a tail call, so **415's rule applies to the prediction's shape**: had
`zone_change` been a stub, it would have been entered holding `ubc_init`'s caller's `lr` — a site in
`bsd_init` — and reported that, not anything inside `ubc_init`. It is not a stub, so the question is
only whether `ubc_init` returns.

**Both callees are real, and both have already run to completion on this machine** — which is stronger
evidence than any static reading, and it is what the step turns on. `zinit`'s callers in this image are
in address order:

    thread_init         0x80009270, 0x80009298
    vm_page_module_init
    pmap_init
    vm_compressor_init
    vm_map_init

and `zone_change`'s are `thread_init` ×4 and `vm_page_module_init` ×6 — every one of them early boot,
every one of them already executed by the time the walk reached `bsd_init`.

That matters because `zinit`'s **guarded list is not empty**, and it reads alarming on its own:

    zinit+0x12c -> strcmp   (leads nowhere this image can name)
    zinit+0x2e4 -> lck_attr_setdefault   (leads nowhere this image can name)
    zinit+0x2fc -> lck_mtx_init_ext   (a stub 2 guards further)
    zinit+0x308 -> lck_spin_unlock   (a stub 2 guards further)
    zinit+0x324 -> panic   (a stub 1 guard further)
    zinit+0x5e4 -> kmem_alloc_kobject   (a stub 1 guard further)
    zinit+0x5f8 -> panic   (a stub 1 guard further)
    zinit+0x604 -> __bzero / +0x618 -> strlcpy   (leads nowhere this image can name)
    ... and 13 more

**The answer to "which guard matters" is none of them, and it does not come from the walk at all.** The
guarded list is a *static bound*: it says some branch inside `zinit` can reach a stub, not that any
branch is taken. A function that has already returned six times in this environment, with the zone map
live and `ZONE_MAX_SIZE` unchecked against a 64-byte element, is a function that returns. The
experiment is a reminder that the walk's guarded column is a place to *look*, and the image's own
execution history is the place to *decide*.

`zone_change(ubc_info_zone, 6, TRUE)` is `Z_NOENCRYPT` (`osfmk/kern/zalloc.h:262`), so the switch takes
its first case and sets `zone->noencrypt` and returns; its one guarded site (`zone_change+0x54 ->
panic`) is the `default:` arm, which item 6 does not reach. The two nameable panics on the path are
`zinit`'s `size > ZONE_MAX_SIZE` check (64 bytes, so it does not hold) and its allocation-failure check.
And `ubc_info_zone` is **defined by this object** — `B`, 0x30 into its `.bss`, linked at `0x80239814` —
so it is not a stand-in. It had no symbol in the image at all before this step, because nothing
referenced it while `ubc_init` was a stub.

**The twenty added names are not on this path.** They are the vnode and code-signing face reached from
`ubc_info_init*`, `ubc_cs_blob_*`, `ubc_upl_*` and `ubc_page_op` — functions nothing in this image
calls yet. 342's trap is checked and empty.

**Falsifiers, named in advance and all silent:** a stop at `zinit` or `zone_change`, which would have
meant one of them is not the real body this image has been running; a stop on one of the twenty added
names; a stop at `proc_uuid_policy_init` (`+0x7CC`, key `0x8003B1C0`), which would have meant `vfsinit`
had already been retired — it is still a stub (`0x801B5810`); a `panic`, and the three candidates were
nameable in advance (the two `zinit` checks and `zone_change`'s `default:` arm), none of which holds.

**And this step is the first time since 415 that the frontier has moved on `bsd_init`'s own statement
list.** 416, 417, 418 and 419 all stopped either inside a function the previous step had made real or
nowhere at all; 420 was not a stub stop either, it was the removal of an unbounded wait. `ubc_init` →
`vfsinit` is two statements one instruction apart in `bsd_init`, and the walk is reading that list
again.

## Layout

    stubnames    781 records (660 function, 121 storage)
    text size    1967232 (0x1E0480) — up 0x3780 from 420's 0x1DCD00
    data         0x801E4000 .. 0x801FECF0 (0x1ACF0), sysctl_set 0x801FECF0 (0x19C),
                 init_array 0x801FEE8C (0x90)
    image bytes  2092828 (0x1FEF1C)   — the image ends at `.init_array`'s end
    bss          0x801FEF40 .. 0x8023C558 (251416 bytes)
    layout       args +2351104 (0x8023E000), topOfKernelData +4194304 (0x80400000),
                 tree +6291456 (0x80600000, len 0x7358), window 8388608
    headroom     1850024 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 9d64c77052ab39e7406a8932a23b43990909a9bd515fa61235b3a3d560478c87

The run's markers agree to the byte: `image_bytes=0x001FEF1C`, `bss_start=0x801FEF40`,
`bss_end=0x8023C558`, `args_pa=0x8023E000`, `checksum=0x90402541`, `checks=5`, `failures=0`. **Every one
of them differs from 420's** — this link moved `.bss` (`0x801FAE00` → `0x801FEF40`) and the checksum with
it, so defect 130 does not arise here: the image's own identity is visible again.

## Where the frontier is now

**`vfsinit`** (`0x801B6488`), reached from `bsd_init + 0x7C8`, key `0x8003B1BC` — the filesystem
subsystem's own init, and the next stub on `bsd_init`'s statement list. Behind it, one statement apart
each: `proc_uuid_policy_init` (`+0x7CC`, `0x801B5810`), `mcache_init` (0x801B5240), `mbinit`
(0x801B51F8), `net_str_id_init` (0x801B54E0), `knote_init` — and on toward `vfs_mountroot`.

**Still owed, and unchanged by this step: the timer.** `vfsinit` and everything behind it is filesystem
initialization, and the filesystem layer is where deadlines start to matter — `IOKitBSDInit`'s
`waitForService`s, `IOFindBSDRoot`, the workloop's event sources all take deadlines, and every one of
them is unbounded while `cpu_set_decrementer_func` is NULL. The environment acting as the platform's
timer driver (`ml_init_timebase` with an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`) and 405's
`IOCPUInterruptController` remain the two pieces of that. What this step does buy is that the walk is
back on a *stub* frontier, where a prediction is a call-graph reading rather than a probability about a
wait.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301801 bytes / 3978 lines, ending `No errors detected`. The
only net across the jump armed and running (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bark_after=0x000c7fb5`, `hw_watchdog_bite_after=0x000dffac`,
`hw_watchdog_bite_truncated=0x00000000`) and it is what ended the run; the dead-man PPI was deliberately
disarmed before the jump (`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`, with
`disarm_ispendr0 0x20480000 -> 0x20400000` and `disarm_cntp_ctl 0x00000005 -> 0x00000002`). Device
returned to Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
