# Experiment 406 — `bsd/miscfs/specfs/spec_vnops.c`: the step that retires `throttle_init`, and the frontier leaves the kernel's start-up machinery for the BSD subsystem's own init

**Step:** one object linked — `bsd_miscfs_specfs_spec_vnops.o`, the pool's only definer of
`throttle_init` (`bsd/miscfs/specfs/spec_vnops.c:1287`) — appended to `LINK_OBJS` after
`osfmk_ipc_ipc_object.o` and before the platform expert, exactly where every step since 381 has gone.
405's stop was `throttle_init`, the **first statement of `bsd_init`** (key `0x8003A9FC`), and this step is
the one that retires it.

**Prediction:** `stub_hit=kauth_init`, caller key **`0x8003ABB4`** (`bsd_init + 0x1C4`).
**Measured:** `stub_hit=kauth_init` at `xnu_entry_stub_caller_v=0x8003ABB4` — name and key exactly as predicted,
`abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

## The step's effect, and why its prediction is not a "the step's object" answer

`tools/entry_object_effect.py` measured the object before the build:

    132 definitions, 170 references
    resolved (4: 3 function, 1 storage)
      rethrottle_thread       object T, stand-in was func T
      spec_filtops            object R, stand-in was data R 0x28
      throttle_init           object T, stand-in was func T      <- 405's stop
      throttle_lowpri_io      object T, stand-in was func T
    added (91: 55 function, 36 storage)

and the build confirms all three columns to the record: **740 → 827** stub names, **644 → 696** function
(−3 retired, +55 added) and **96 → 131** storage (−1 retired, +36 added). `throttle_init` is gone from the
list. **91 added names is the largest `added` column of any step since 342**, and that is what makes the
prediction a question about the *path* rather than about the object: this step's stop is nowhere near
anything it defines.

## The reading that produced the prediction

Three questions, each answered in the image the step would start from:

1. **Does `throttle_init` itself stop anywhere?** Its body is 140 instructions in the object and calls eight
   names — `lck_grp_attr_alloc_init`, `lck_grp_alloc_init`, `lck_attr_alloc_init`, `lck_mtx_init`,
   `PE_get_default` (×3), `PE_parse_boot_argn` (×4), `thread_call_allocate`, `vm_io_reprioritize_init` — and
   **every one is real in this image and reaches no stub on its own straight line** (`xnu_entry_callwalk.py`
   on each of the eight). `throttle_init_throttle_window` is inlined, which is where four of the
   `PE_parse_boot_argn` calls come from. So `throttle_init` runs to its end and returns.
2. **Does anything between its return and the next stub stop?** `bsd_init`'s own line after the call:
   `_consume_printf_args` (+0x10), `kmeminit` (+0x14), four `PE_parse_boot_argn` (+0x28/+0x54/+0x78/+0x9C) —
   all reach no stub either, and `bsd_init` makes **no other call between +0x9C and +0x1C0**.
3. **Is the next stub still a stub after this link?** `kauth_init` is record **175** of 740 before the link
   and record **215** of 827 after it, and it is *not* in this step's `added` column — so the link neither
   retires it nor creates it. The call site is unconditional:

       8003abb0: bl 80189dc4 <kauth_init>      ; bsd_init + 0x1C0, caller key 0x8003ABB4
       8003abb4: bl 8018afdc <procinit>        ; +0x1C4 — the next stub, if kauth_init were skipped
       8003abb8: bl 8018bcfc <tty_init>        ; +0x1C8

**And the trap this step had to check for, in the form 342 taught:** a name the step *creates* can be a name
the walk reaches. Here the check is an intersection, and it comes out empty: **none of the 91 added names is
a `bl` target anywhere inside `bsd_init`'s 696 instructions.** They are the BSD VFS/buf face (`buf_*`,
`err_*`, `nop_*`, `spec_*`, `VNOP_*`, `vnop_*_desc`, `iskmemdev`, `check_mountedon`, `chrtoblk`,
`bpfkqfilter`, `session_lock`, `vn_default_error` and their kin), and they are reached from `spec_vnops`'s
own functions, which nothing on this path calls yet.

**Falsifiers, named in advance and all silent:** a stop inside `throttle_init`'s body (its eight callees
measured stub-free); a stop between its return and `kauth_init` (the frames above measured stub-free); a
`data abort` in `throttle_init` (it allocates two lock groups and a lock attribute and writes
`_throttle_io_info[i]`, all real `.bss`); a stop on one of the 91 new stubs (measured not to be called from
`bsd_init`).

## Layout

    counts       740 -> 827 records (696 function, 131 storage)
    text size    1786464 (0x1B42E0)   — 405's 1768544 + 0x44E0
    image bytes  1911120 (0x1D2950)   — 405's 1877400 + 0x8DA8
    bss          0x801D2980 .. 0x8020FD98 (250904 bytes)  — 405's 0x801CA5C0 .. 0x802035D8, moved *and* grown
    layout       args +2166784 (0x80211000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     2032232 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, 4929536 bytes, sha256
                 636b3cd00c6d27041e7bc42f7856fcb9d51be0f82aa92e6949d68150d56ae309

The run's own markers agree to the byte: `bss_bytes=0x0003D418`, `image_bytes=0x001D2950`,
`bss_start=0x801D2980`, `bss_end=0x8020FD98`, `args_pa=0x80211000`, `top_of_kernel_data=0x80400000`.

**`.bss` is the section this step moves, and it is the stub object that moves it.** 91 new names add 91
bodies to `realstubs.o`'s `.text` and 91 name slots to its `.rodata.str1.4`, and the **36 new storage
stand-ins land inside `.bss`** — each costing `align64(size)` rather than a 0x40 slot (354's rule) — so for
the first time in a long while the *start* of `.bss` itself steps (0x801CA5C0 → 0x801D2980) instead of only
its contents. The `.text` growth is likewise dominated by the stub pool rather than by the object: 91 × 0x18
of new bodies against 4 × 0x18 retired, plus the name slots, plus this object's own code.

## Where the frontier is now

The walk is inside the BSD kernel's own init sequence, and the next stop is two calls after this one:
`kauth_init` (record 215) → `procinit` (+0x1C4) → `tty_init` (+0x1C8), all stubs, all in `bsd_init`'s
straight line. `kauth_init` is defined by `bsd/kern/kern_authorization.c:143`, and that object measures
**1 resolved / 11 added** (10 function + 1 storage) — the next step, with the same shape of prediction as
this one.

## Safety

A non-persistent `fastboot boot` of the image above through `preflight_boot_check.sh --allow-xnu-entry` and
`run_and_capture.sh --allow-xnu-entry`; nothing flashed. Preflight green. **25 records of
`persistent_write_attempted=0x00000000`** and **87 of `failure_mask=0x00000000`**, none non-zero;
`abort_entries=0`; `checks=5` / `failures=0`; `kv_written=0x5B`, `kv_in_dram=0x7F`; no `panic`, no
`exception:`; 301622 bytes, log ends `No errors detected`. The device returned to Android on its own and was
confirmed there afterwards (`adb devices` shows `4a2fe00b`).
