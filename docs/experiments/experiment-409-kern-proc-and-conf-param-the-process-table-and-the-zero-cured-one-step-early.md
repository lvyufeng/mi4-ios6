# Experiment 409 — `bsd/kern/kern_proc.c` + `bsd/conf/param.c`: the process table goes real, and the zero that would have panicked it is cured one step before the symbol that reads it arrives

**Step:** **two objects**, and the second is not optional — `bsd_kern_kern_proc.o` (the pool's only
definer of `procinit`, `bsd/kern/kern_proc.c:221`) and `bsd_conf_param.o` (`bsd/conf/param.c`), appended
to `LINK_OBJS` after `bsd_kern_kern_credential.o`.

**Prediction:** `stub_hit=hashinit`, caller key = the linked address of `procinit + 0x6C`.
**Measured:** `stub_hit=hashinit` at `xnu_entry_stub_caller_v=0x8018C550` — name and key exactly as
predicted (the return address of the `bl 8018c54c <hashinit>` at `0x8018C54C`), `abort_entries=0`,
`checks=5` / `failures=0`, no `exception:`, no `panic`.

## The step's effect

    bsd_kern_kern_proc.o   55 resolved (49 function, 6 storage) / 7 added (6 function, 1 storage)
    bsd_conf_param.o        7 resolved (0 function, 7 storage) / 0 added, 0 references

    821 -> 773 -> **766** stub names, 690 -> **647** function, 131 -> **119** storage

The first is the largest single resolution of the walk: it retires `allproc`, `zombproc`, `pidhash`,
`pidhashtbl`, `pgrphashtbl`, `sesshashtbl` and the whole `proc_*` accessor family — `proc_find`,
`proc_pid`, `proc_name`, `proc_rele`, `proc_suser`, `proc_ucred` (408's own added name) and another thirty.
The second defines nothing the boot calls and references nothing at all.

## Why the second object is in this step, and why that is the whole point

`procinit`'s first statement that is a call is `hashinit(maxproc / 4, M_PROC, &pidhash)` — four times over.
`hashinit` is a stub (record 150), so **this run stops before that body runs**. But the step after this one
makes `hashinit` real, and its first statement is

    if (elements <= 0) panic("hashinit: bad cnt");

so a `maxproc` still reading zero would turn a stub stop into a **panic one step later**. And `maxproc` was
a zeroed data stand-in:

    data maxproc D 0x4        data maxprocperuid D 0x4     data maxfiles D 0x4
    B nprocs 0x4              B desiredvnodes 0x4           B nmbclusters 0x4
    B buf_headers 0x4

This is 341/343/377's rule — **the value's definer must arrive with the symbol's** — and this step is the
first place the read exists, so the cure belongs here. The value it brings is measured, not assumed: read
out of `bsd_conf_param.o`'s own `.data`, word 0 is **0x000003E8 = 1000** (`CONFIG_EMBEDDED`'s `NPROC`), so
the count passed is `1000 / 4 = 250`. `maxprocperuid` is 950 (`0x3B6`) at word 1, and `hard_maxproc` 1000
at word 2 — all three nonzero, all three now real.

## The reading that produced the prediction

`procinit`'s body makes **exactly four calls, all `hashinit`** — the two `LIST_INIT`s are inline stores, and
`personas_bootstrap` sits behind `CONFIG_PERSONAS`, which this build does not define. The disassembly has
four `bl`s and nothing else. So the first call is the stop, and the only question was whether something
earlier on the path could stop first; nothing can, because `procinit`'s entry is the whole of its preamble.

**Falsifiers, named in advance:** a stop inside `procinit` before the first `hashinit` (there is no other
call); a stop at `tty_init` (`bsd_init + 0x1C8`) instead, which would mean `procinit` was retired without
being entered; a stop on one of the 7 added names (`mac_proc_check_get_cs_info`,
`mac_proc_check_set_cs_info`, `microtime`, `proc_pendingsignals`, `proc_shutdown_exitcount`,
`pt_setrunnable`, `vn_getcdhash` — none is called from `bsd_init`'s line or from `procinit`); a `data abort`
from the newly real `allproc`/`zombproc` `LIST_INIT`s (inline stores into payload-zeroed `.bss`). **None
fired.**

## Layout

    text size    1819776 (0x1BC440)    — 408's 1802336 + 0x17440
    image bytes  1944256 (0x1DAAC0)    — 408's 1927656 + 0x165A0
    bss          0x801DAAC0 .. 0x80217C58 (250264 bytes)
    layout       args +2199552 (0x80219000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     1999784 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 87a6a368aceb3edeaa7ac8349313666ae92f383b64f33bc68a719cfd4936f2ff

The run's markers agree to the byte: `bss_bytes=0x0003D198`, `image_bytes=0x001DAAC0`,
`bss_start=0x801DAAC0`, `bss_end=0x80217C58`, `args_pa=0x80219000`.

## The frontier

`hashinit`, defined by `bsd/kern/kern_subr.c:307` — an object measuring **6 resolved / 2 added**, and the
step that must not be taken without 409's `bsd_conf_param.o` behind it.

## Safety

A non-persistent `fastboot boot` of the image above through both gated scripts; nothing flashed. Preflight
green. 25 records of `persistent_write_attempted=0x00000000`, 87 of `failure_mask=0x00000000`, none non-zero;
`abort_entries=0`; `checks=5` / `failures=0`; `xnu_entry_checksum=0x9040EC1D`; no `panic`, no `exception:`;
301620 bytes, log ends `No errors detected`. Device returned to Android on its own and was confirmed there
(`adb devices` shows `4a2fe00b`).
