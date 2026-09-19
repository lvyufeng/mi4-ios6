# Experiment 432 — `stage90_pthread_functions.c`: the image supplies the kernel's pthread function table, and the frontier becomes its first slot — the first step whose object changes no count at all

**Step:** one object linked — `stage90_pthread_functions.o`, which is **this project's own source**
(`stages/stage90/xnu_supply/stage90_pthread_functions.c`) compiled by
`tools/build_xnu_arm_kernel.sh`'s platform block and appended to `LINK_OBJS` after
`iokit_bsddev_IOKitBSDInit.o` and before `STAGE90_PLATFORM_EXPERT_OBJ`. It is the first step in this
walk whose object is a **table** rather than a missing symbol.

**Effect:** **0 resolved / 0 added** — the three counts do not move at all: **768 undefined, 616
function, 152 storage**, exactly as 431 left them, and exactly what `tools/entry_object_effect.py`
predicted:

    stage90_pthread_functions.o   82 definitions, 2 references
    resolved (0: 0 function, 0 storage)   added (0: 0 function, 0 storage)
    of the 2 references, 2 are already satisfied
    predicted counts: 768 -> 768 undefined, 616 -> 616 function, 152 -> 152 storage

Its two references are `pthread_kext_register` (real since 425, `bsd_kern_pthread_shims.o`) and
`entry_stub_hit` — **defined by this image's own `entry_stubs.o`**, which makes this the first pool
object in the walk with a reference back into the entry image. It is resolved in pass 1 rather than
stubbed, because `entry_stubs.o` is in `LINK_OBJS` before pass 1 runs (the same fact that lets
`panic` be deliberately ungenerated).

## Why this step exists, and why no link could have done it

431's run left `vfsinit`, went ten calls down `bsd_init`'s statement list, and stopped on a `panic`
at `bsd_init + 0x7F4`:

    void pthread_init(void)                                    pthread_shims.c:271
    {
        if (!pthread_functions)                                :275
            panic("pthread kernel extension not loaded (function table is NULL).");
        pthread_functions->pthread_init();                     :277
    }

`pthread_functions` (`pthread_shims.c:703`) is a real 4-byte `.bss` pointer whose only writer is
`pthread_kext_register` (`:712`), and that function's only caller in the whole tree is `pthread.kext`
— an Apple binary the tarball does not contain and that no object in the 695-object pool supplies.
So the guard is on a **value** nothing in this image writes, and the walk's whole method (link the
definer of the previous stop) has nothing to link. The only way past it is to write the value.

**So 432 is the shape a kext would have, minus the kext:** a table built from Apple's own header,
registered by an `.init_array` constructor during `kernel_bootstrap`, with every slot this project
does not implement pointing at a stand-in that stops the run and names itself.

## The layout comes from Apple's header, and the kernel's own code confirms it

The source includes `<sys/pthread_shims.h>` and is compiled by the kernel build script with the
`bsd` component's defines, so the table's size and the offsets of its slots are **the ones the kernel
was compiled against**, not a copy of them — one definition, the rule this project has a memory about
(`mi4-one-value-two-definitions`). A second copy of the layout would not fail loudly: it would call a
function through the wrong offset, with the wrong arguments, and the run would look like something
else entirely.

The layout was measured before the file was written, and checked a second time against the kernel's
own compiled code:

    sizeof(struct pthread_functions_s)      508 = 0x1FC = (1 + 39 + 87) words
    offsetof(version)                       0
    offsetof(pthread_init)                  4
    offsetof(_pad)                          160, i.e. 40 words of named members
    PTHREAD_FUNCTIONS_TABLE_VERSION         1   (taken from the header, not written as a literal)

    bsd_kern_pthread_shims.o, pthread_init:       28:  ldr r0, [r0, #4]    <- the slot
                                                  2c:  pop {r4, lr}
                                                  30:  bx  r0             a tail branch, so the
                                                                         callee's lr is pthread_init's
                                                                         caller

and Apple's own static assert in that same file (`pthread_shims.c:71`) checks the tail the same way:
`sizeof(...) - offsetof(..., psynch_rw_yieldwrlock) - sizeof(void *)` is 100 pointers, and
508 − 104 − 4 is 400.

**One property of the source is load-bearing and not obvious.** `<sys/eventvar.h>` must be included
*before* `<sys/pthread_shims.h>`. The tree has a circular include there — `pthread_shims.h:40` →
`user.h:89` → `eventvar.h:71` → `pthread_shims.h` again, a no-op because its guard is already set —
and then `eventvar.h:175` uses `workq_threadreq_t`, which only `pthread_shims.h:56` defines and which
therefore does not exist yet. Measured both ways: the other order fails with "field has incomplete
type `struct workq_threadreq_s`" at `eventvar.h:175`.

## The two design rules, and the check the file adds

**Every named slot — all 39, including Apple's three `__unused*` reservations — points at a stand-in
that stops the run and names itself:**

    stub_hit=stage90_pthread_functions.<slot>

A **NULL** slot would be a fault rather than a stop (a branch to 0 shows up as `abort_entries != 0`
and a `first_dfar`, which says something went wrong but not *what was expected to be there*), and a
**silent no-op** would tell the kernel that work nobody did had been done. The stand-in shape is the
generated stubs' own, byte for byte: 16 bytes, `mov r1, lr` ahead of a tail call to `entry_stub_hit`,
so the caller field is the stand-in's `lr` and the call site is `caller - 4`.

**And before registering, the constructor walks the table's 40 named words and stops on any NULL,
reporting the word index in the caller field.** That is the check for the one mistake this file can
make but cannot see at compile time: a slot **forgotten in the list** is a missing initializer, which
C silently zeroes. With the scan such a slot is a stop at registration that names its index, before
anything can be called through it, rather than a branch to zero at whatever point the kernel first
uses that slot.

Registration is from an `.init_array` constructor, which is how Apple's own kernel runs this class of
work: `kernel_bootstrap` → `PE_init_iokit` → `StartIOKit` → `OSlibkernInit` → `OSRuntimeInitializeCPP`
walks `.init_array` in link order, long before `bsd_init` calls `pthread_init`. The object is placed
before `ENTRY_LAST_KERNEL_CONSTRUCTOR_OBJ`, and the map confirms the order: its `.init_array` word is
at `0x802542A4`, the platform expert's at `0x802542A8`, `last_kernel_constructor`'s at `0x802542AC`.

## The prediction, and the measurement

**Prediction, written before the build:** `stub_hit=stage90_pthread_functions.pthread_init`, caller
key `0x8003B1E8` — `bsd_init` at `0x8003A9F0` calls `pthread_init` at `bsd_init + 0x7F4`, and
`pthread_init` reaches the slot through a tail branch (`pop {r4, lr}; bx r0`), so the stand-in is
entered with `pthread_init`'s own caller in `lr`: the return address of the `bl`, `bsd_init + 0x7F8`.

**Measured on hardware: exactly that, to the byte, and no panic.**

    line 3926: MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
    line 3932: MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
    line 3936:  xnu_entry_stub_caller_v=0x8003b1e8
    line 3937:  xnu_entry_stub_caller_digits=0x0000004a
    line 3938:  xnu_entry_stub_caller_w0=0x33303038      <- "8003"
    line 3939:  xnu_entry_stub_caller_w1=0x38653162      <- "b1e8"
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=stage90_pthread_functions.pthread_init
    line 3974:  xnu_entry_stub_caller=0x8003b1e8
    line 3975:  xnu_entry_stub_caller_a=0x8003b1e8
    line 3976:  xnu_entry_stub_caller_e=0x8003b1e8
    line 3978: No errors detected

Three records of the caller, all identical, and `_w0`/`_w1` spell the same key back as ASCII
"8003b1e8" — the second path to the same number that every step since 271 has taken. The exit is
`entry_stub_hit`, so the report line is the ordinary one: no `exception:`, no `panic`, no
`abort_entries`.

**And the call site is confirmed against the linked image rather than inferred:**

    0x8003b1e8  bsd_init+0x7f8
    caller-4 = 0x8003b1e4  bsd_init+0x7f4   <- the `bl`, if the call was one

    $ arm-none-eabi-objdump -d --start-address=0x8003b1d0 out/stage90/xnu_arm_entry.elf
    8003b1e0:  bl 801eb908 <psem_lock_init>
    8003b1e4:  bl 801ecb38 <pthread_init>      <- the call
    8003b1e8:  bl 801ea474 <pshm_cache_init>   <- the key, the next instruction
    8003b1f8:  bl 80206278 <nwk_wq_init>       <- and the next stop, at +0x808

`bsd_init + 0x7F4` is the eleventh of the fifteen calls between `vfsinit` and 431's predicted stop;
ten of them ran clean in 431 and the eleventh panicked, and here the eleventh is the stop.

**What the key does and does not name.** It names the *call site* — any slot called from `pthread_init`
would report the same `0x8003B1E8` — which is why the stub's name carries both the table's name and
the slot's. This is 431's tail-branch trap read the other way round: there the same key under a
different name was the discriminator, and here the same key under the same call site is the slot.

## The reading: the frontier moved because a value was written, not because a symbol was added

Every step since 425 has moved the frontier by retiring a name. This one retires nothing and creates
nothing — the stub set is unchanged, the three counts are unchanged, and the walk's own tool would
say this object changes nothing at all. It changed the boot anyway, and the reason is worth stating
plainly because the rest of this walk now depends on it:

**a `.bss` pointer's guard is not a symbol and cannot be satisfied by a link, only by a write.**

431 met that as a `panic` with the message in `r9` — a stop the walk's two lists cannot represent,
because both are built from the stub set. This step answers it in the idiom the walk uses for
everything else: **the value is supplied, and the first thing that consumes it stops and names
itself**, so the run is once again an ordinary `stub_hit=` report with a caller key. The panic is
gone; in its place is a stop that says which slot of which table was called from where.

## Layout

    entry text   0x235FA0 (2318240)     <- was 0x2353E0
    entry image  0x2542B0 (2441904)     <- was 0x2542AC: four bytes, this object's .init_array word
    .init_array  0x8025421C .. 0x802542B0 (0x94)      <- was 0x90
    bss          0x802542C0 .. 0x802961D8 (270104 bytes)   <- start, end and size all unmoved
    layout       args 0x80298000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1482280 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 4ee629f47520b7d82416dc3194ecfc8d2744e89d4283987ea3799b570913989e (5462016 bytes)
    entry bin    2441904 bytes; sha256
                 b43b7c4de7ec96ba6a1ccd4f5a901c8f02a85ba22d225a1f737a6f046757ac1c

Run markers agree with the link: `xnu_entry_image_bytes=0x002542B0`,
`xnu_entry_bss_start=0x802542C0`, `xnu_entry_bss_end=0x802961D8`, `xnu_entry_args_pa=0x80298000`,
`xnu_entry_top_of_kernel_data=0x80400000`, `xnu_entry_checks=0x00000005`,
`xnu_entry_failures=0x00000000`, `xnu_entry_checksum=0x9040E1ED` (431: `0x9040E1F1`),
`xnu_entry_entering_at=0x80000074`. Every marker differs from 431's, and `xnu_entry_kv_words` — the
word-for-word dump of the vector table's first 2644 bytes — differs too, which is what rules out
defect 130's case: the image that ran is the image that was linked.

**The `.data` boundary swallowed the whole of the text growth — for the third step running, and this
time the arithmetic is exact.** `.text` grew by the object's own three inputs — `.text` 0x2CC +
`.rodata` 0x1FC + `.rodata.str1.1` 0x6F1 = **0xBB9** — and the measured growth is **0xBC0**: the fill
term is **7 bytes**, the same rule that made earlier steps miss by 0x12 and 0x55 and 0x11. `.text`
now ends at 0x80235FA0 while `.data` is placed at 0x80238000 in both images, so the gap between them
shrank from 0x2C20 to 0x2060, and **not one byte of the growth reached the image.** What the image
did grow is the `.init_array` word; what it did not grow is `.bss` — the object's 4 bytes
(`stage90_pthread_callbacks`, where `pthread_kext_register` writes `&pthread_callbacks`) landed at
`0x80292C60`, inside the run's own alignment padding, and `__bss_start`/`__bss_end` came out identical
to the byte.

**And the object's `.text` landed on `0x80203D48`** — `0x80202A0C + 0x133C`, the fifth-time-checked
contiguous chain of `entry.ld:45` — the sixth consecutive check of that reading, and the first time
it has placed an object that is not Apple's.

Both stages rebuild byte-identically from the committed sources: the entry image above, and the
payload above, after the comment block was finished.

## Where the frontier is now, and what 433 has to be

**`pthread_functions->pthread_init`** — a slot of the table this step supplies, supplied by a stand-in
that stops. The *value* frontier that stopped 431 is gone: `pthread_functions` is no longer NULL, and
the kernel reached the table it expects a kext to provide.

**The next stop is the one 425 predicted and 431 never reached**: `nwk_wq_init` (`bsd_net_nwk_wq.o`,
`bsd_init + 0x808`, key `0x8003B1FC`), one call after `pshm_cache_init` in the disassembly above. It
is still a plain missing symbol, and 433 is shaped like every step from 425 to 431: link the object
that defines it.

**But the table's slots are now a second frontier, and they are not moved by linking.** `pthread_init`
itself, `fill_procworkqueue` (called from `fill_procworkqueue_shim`), the `psynch_*` family and the
workqueue entry points are all reachable from code this image already contains — `pthread_shims.c`'s
shims call them — and each is a stop until this project implements it. Nothing in the boot so far
calls any of them except `pthread_init`, so the ordering question is only "which comes first", and
the answer is measured rather than assumed: 433 links `nwk_wq_init`, and the table's other slots stay
where they are until a run names one.

**Still owed and unchanged: the timer.** `ml_init_timebase` with an MSM8974 `tbd_ops_t` over the GPT
at `0xf9020000`, plus 405's `IOCPUInterruptController`. Nothing on this step's path takes a deadline —
the constructor writes one pointer and the run stops at the next call — but the object 433 links is
`nwk_wq_init`'s, and the mount machinery (`IOFindBSDRoot`, `vfs_mountroot`) that waits unboundedly is
what follows it.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure); nothing flashed, nothing written to storage.
25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`,
`checks=5` / `failures=0`, no `exception:` and no `panic:` line. The only net armed across the jump
was the hardware watchdog (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bite_truncated=0x00000000`), with the dead-man PPI disarmed before it
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Device returned to Android on its own and was
confirmed there (`adb devices` shows `4a2fe00b`, `ro.build.version.release` 10, `ro.product.device`
cancro). 301835 bytes / 3978 lines, ending `No errors detected`.

Per-run logs stay apart: `/tmp/run425_kmsg.txt` … `/tmp/run431_kmsg.txt`, `run432_kmsg.txt`.
