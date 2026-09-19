# Experiment 412 — `bsd/kern/kern_overrides.c`: `init_system_override` retires, and the frontier takes one step to `ulock_initialize`

**Step:** one object linked — `bsd_kern_kern_overrides.o`, the pool's only definer of
`init_system_override` (`bsd/kern/kern_overrides.c`) and therefore of 411's stop — appended to
`LINK_OBJS` after `bsd_kern_tty.o`.

**Prediction:** `stub_hit=ulock_initialize`, caller key `0x8003ADA8`. **Measured:** exactly that,
`abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

    line 3935: xnu_entry_stub_caller_v=0x8003ada8
    line 3939: xnu_entry_abort_entries=0x00000000
    line 3970: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ulock_initialize
    line 3975: No errors detected

## The step's effect — the cleanest step shape there is

    1 resolved (1 function, 0 storage) / 0 added (0 function, 0 storage)
      resolved  init_system_override
      16 references, all 16 already satisfied

    775 -> 774 stub names, 654 -> 653 function, 121 -> 121 storage

**An object that can retire a stub and cannot create one.** The build confirms all three counts to the
record. The other six names it defines — `system_override`, `sys_override_enabled`,
`sysctl__debug_sys_override_enabled`, the four `.bss` locks — are names *nothing in the link references*,
which is why the tool counts one resolved and not seven: stubness is a property of what the link
undefineds, never of what an object happens to define.

## The reading that produced the prediction

`init_system_override`'s body makes exactly four calls, and the disassembly has four `bl`s and nothing
else:

    +0x04  bl lck_grp_attr_alloc_init   real
    +0x20  bl lck_grp_alloc_init        real
    +0x30  bl lck_attr_alloc_init       real
    +0x50  bl lck_mtx_init              real

each walked with `xnu_entry_callwalk.py --root <fn>` to "reached no stub on the straight-line path" —
374/378's rule, because "not a stub" is not "does not contain one". The rest of the function is stores
into its own `.bss` statics, two zeroed counters, and `sys_override_enabled = 1`.

**And the next stop is the very next instruction.** The 411 image's own disassembly puts the three call
sites adjacent:

    8003ad9c  bl mac_policy_initbsd
    8003ada0  bl init_system_override     <- 411's stop, bsd_init + 0x3B0
    8003ada4  bl ulock_initialize         <- this step's stop, bsd_init + 0x3B4
    8003ada8  bl proc_list_lock

so the frontier moves 4 bytes. **This corrects 411's write-up, which placed `ulock_initialize` at
`+0x1D8`** — the jump of 0x1E8 bytes was right, the offset quoted for the *next* stub was not; it is
`+0x3B4`. Nothing in 411's conclusion depends on it (the prediction was name and key, both measured),
but the number was wrong and is fixed in that document too.

**Falsifiers, named in advance and all silent:** a stop inside `init_system_override` (its four callees
are real and stub-free); a `data abort` from `lck_mtx_init` if `lck_grp_alloc_init` returned NULL — but
those same four calls already ran for real inside `bsd_init`'s own body at `+0x220`, `+0x23C`, `+0x258`,
`+0x274` and `+0x284`, before 411's stop, so a NULL here would have fired long before; a stop at
`proc_list_lock` (`+0x3B8`, already real); a stop at `file_lock_init` (`+0x630`, a stub, but reachable
only if `ulock_initialize` were already real).

## Layout

    stubnames    774 records (653 function, 121 storage)
    text size    1845312 (0x1C2720)   — 411's 1844544 + 0x300
    image bytes  1960812 (0x1DEB6C)   — 411's 1960760 + 0x34
    bss          0x801DEB80 .. 0x8021BE98 (250648 bytes)
    layout       args +2215936 (0x8021D000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     1982824 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 80aa88db6538bdc8a1a063dbcf01a49c612079c96f9ea522a01adcdb0ce6ce2b

The run's markers agree to the byte: `image_bytes=0x001DEB6C`, `bss_start=0x801DEB80`,
`bss_end=0x8021BE98`, `args_pa=0x8021D000`, `checksum=0x90406E31`.

## A build-order note that cost a device run

The first build of this step produced a payload **bit-identical to 411's** (`de3e22d8…`): the canonical
sequence is two builds, and running only `stages/stage90/build.sh` does not relink the entry image —
`stages/stage90/xnu_arm_boot/build_entry.sh` with `STAGE90_ENTRY_REAL_ARM_INIT=1` is what consumes
`LINK_OBJS`. The unchanged sha256 was the tell, and checking the stub-name count (775 records, unchanged)
before touching the device is what turned it into a rebuild rather than a wasted run.

## Where the frontier is now

`ulock_initialize`, defined by `bsd/kern/sys/ulock.c` — the call immediately after `init_system_override`
in `bsd_init`'s compiled line, and the object that defines it is `bsd_kern_sys_ulock.o`.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` /
`failures=0`, no `exception:`, no `panic`; 301628 bytes, ending `No errors detected`. Device returned to
Android on its own and was confirmed there (`adb devices` shows `4a2fe00b`).
