# Experiment 459 — the root device is memory, and the OS names it

**Status: built, gated, run on hardware — the two reads that were crispest hold, and the frontier
moved back one step inside the same function.** The 60-second wait for an `IOMedia` is gone from the
log and so is the `IOLog` that would have announced it, so the `rd=md0` branch was taken and the
media path was never entered; the copyright banner is in the log, so the string restoration took.
But the OS never named a memory device: no line of `mdevadd`'s five printfs is in a capture that
demonstrably carries `printf`, the `rd` branch's own `mdevlookup` then returned -1, and
`IOFindBSDRoot` panicked on `:490` **0.6 ms after its own IOBSD wait returned**. The host check walks
this same tree with XNU's own reader and finds `/chosen/memory-map` and its exact two words, so the
gap is between the *DT blob* and the *IODT registry plane* the OS looks the property up in — and the
run could not name the trap that proves it, because the instrument that records the trap writes into
the one buffer the kalloc tracer had already filled (`8170` of `8192` bytes, `26299` calls dropped).
Both are 460's first two pieces of work.

457 and 458 left the boot inside
one loop: `IOFindBSDRoot`'s `do { service = IOService::waitForService(matching, &t); } while
(!service)` for an `IOMedia` whose `Content` is `Apple_HFS` (`iokit/bsddev/IOKitBSDInit.cpp:552-569`)
— a device this machine cannot produce, because nothing in this image drives the eMMC. This step
takes the *other* exit from the same function, twenty lines above it: the `/chosen/memory-map`
`RAMDisk` property that `mdevadd` turns into a memory device, and the `rd=md0` boot-arg that makes
`IOFindBSDRoot` name it and `goto iofrootx` (`:436-490`). It is the first step in this walk whose
product is not an instrument but a *device*: the first time the OS is handed a root filesystem.

## What the step adds, and where it came from

| piece | file | why it is that and not something else |
| --- | --- | --- |
| the disk | `entry_stubs.c`'s `g_stage90_ramdisk` | 256 KB of `.bss` in the entry image. It has to be *this* image's memory: `mdevadd` is called with `phys = 0`, so the device reads its contents through `mdBase` as a virtual address in this kernel's address space, and the payload's `.bss` is not in these page tables |
| the property | `stage90_main.c`'s `/chosen/memory-map` | `IOFindBSDRoot` looks that path up in the DT plane and takes `RAMDisk` as two machine words. Two properties (`name`, `RAMDisk`), zero children, and the `/chosen` node's own child count goes 0 → 1 |
| the two words | `build_entry.sh` → `out/stage90/xnu_arm_entry.h` → `stage90_main.c` | the address and size of `g_stage90_ramdisk` **in the linked entry image**, read out of the link by `nm`/`nm -S` and injected into the generated header. The payload compiles against that header, so the property and the symbol cannot disagree — this project's oldest defect class, and the reason the value is not a constant anywhere |
| the boot-arg | `stages/stage90/boot_args.c` and the DT's `/chosen` `boot-args` | `rd=md0`, in *both* copies. `PE_parse_boot_argn` reads `PE_boot_args()` = the `boot_args` struct's `CommandLine` (`pexpert/arm/pe_bootargs.c:11`), and the DT property is not what it parses — which is why the `serial=0x1` that sat in the property did nothing for a hundred experiments. The two copies are kept in agreement anyway, and the host walker checks that the tree's copy names md0 |
| the filesystem | `tools/xnu_config/boot/STAGE90_XNU.local` | `STAGE90_XNU = [ RELEASE mockfs ]` — Apple's RELEASE kernel with `mockfs`, the only filesystem in the tree whose `vfc_mountroot` is non-NULL. `vfs_mountroot` calls the first such entry and nothing else; devfs and routefs are NULL and NFSCLIENT is off. Composed rather than copied, because a second copy of RELEASE's attribute list would be one value with two definitions |
| the messages | `tools/build_xnu_arm_kernel.sh` | `-UCONFIG_NO_PRINTF_STRINGS`, after the configuration's own `-D` list. RELEASE inherits `no_printf_str` (`config/MASTER:302` through `BSD_RELEASE`, `MASTER.arm:24`), which makes `printf` a macro that drops its first argument — so the string is *absent from the object*, not merely unprinted. 458 measured that (`bsd_init`'s `bl _consume_printf_args` and the missing literal) and named this step as where it has to change. `no_kprintf_str` stays on: a panic in this configuration is still silent, written down here rather than discovered |
| the stamp | `tools/build_xnu_arm_kernel.sh`'s `config.stamp` | the entry link globs **every** object in `out/xnu_kernel_obj`, so whichever build ran last decides what this image's kernel *is*, and nothing recorded which. Six lines: configuration, fragment, object count, defines, `pool_printf_shim`, `pool_mockfs_objects` |
| the checks | `build_entry.sh`'s `verify_root_device` | five groups, all `layout_fail`: the stamp says `STAGE90_XNU` with 0 printf shims and 3 mockfs objects; eleven names of the root-device path are defined in the image and absent from the undefined list; four `printf` literals are in the image; the `vfstbllist` entries are read back and mockfs is the one with a `vfc_mountroot` while devfs and routefs have none; and the disk is 0x40000 bytes, page-aligned, inside `.bss`, below the boot_args page and below `topOfKernelData` |

## The prediction, written before the build and before the run

1. **The 60-second wait is not entered at all.** 457's third `xnu_live_iolock` record — same site
   (`waitForMatchingService+0xE0`), `ent = 1`, `deadline − now = 1151999988` ticks = **60.0000 s** —
   is absent from this run. `IOFindBSDRoot` reaches the `rd=md0` branch and `goto iofrootx` *before*
   it builds the `IOMedia` table, so the record's own dictionary (`serviceMatching("IOMedia")` +
   `Content = "Apple_HFS"`) is never created. The run's iolock records are the two 30-second ones
   (`IORTC`'s and the `IOBSD` resource's), both of which 457 measured ending in 0.18 ms and 0.32 ms.
   This is the crispest single reading of the step and it is where a false `rd` or a missing property
   shows up as *the same log as 458*.
2. **The OS names the root device, in its own words, twice.** The console block contains
   `Added memory device md0/rmd0 (...)` — `printf` at `bsd/dev/memdev.c:629`, the only proof
   `mdevadd` ran at all — followed by `BSD root: md0, major M, minor 0`, `IOLog` at
   `IOKitBSDInit.cpp:472`. `M` is whatever `bdevsw_add` handed out and is not predicted; the claim is
   that both lines are there, in that order, and that the second one is the IOLog the `rd` branch
   exists to print.
3. **The copyright banner is in the log**: `Copyright (c) 1982, 1986, 1989, 1991, 1993` / `The
   Regents of the University of California. All rights reserved.` — `printf` at
   `bsd/kern/bsd_init.c:452`, the first statement of `bsd_init`, and text that has never appeared in
   any run of this project because it was compiled out. Its presence is this step's *build* reading,
   independent of the root device; its absence with everything else present would mean the string
   restoration did not take.
4. **The frontier is `load_init_program`.** The block's last two lines are
   `load_init_program: attempting to load /sbin/launchd` and then
   `load_init_program: failed loading /sbin/launchd: errno -1`. The prediction's most falsifiable
   part is the value: `exec_mach_imgact` sets `error = -1` — not an errno — for a Mach-O whose
   `magic` is neither `MH_MAGIC` nor `MH_MAGIC_64` (`bsd/kern/kern_exec.c:855-858`) and returns it
   unmodified, so `printf`'s `%d` prints the raw -1. The disk is 256 KB of zeros, so the header's
   magic is 0, which is exactly that branch. Readings that would say something else, named in
   advance: **`errno 88`** (EBADMACHO) means exec's own check passed and the failure came from
   `mach_loader.c`, a different path; **`errno 2`** (ENOENT) means `/sbin/launchd` never resolved —
   the mount happened and mockfs's lookup or its file node's mapping did not; **`errno 5`** (EIO)
   or `cannot mount root` means mockfs refused the device, which is the mount and not the exec.
5. **The mount itself is quiet.** `cannot mount root, errno = N` (`bsd_init.c:962`),
   `We are hanging here...` (`:956`) and `vfs_mountroot: can't setup bdevvp` (`vfs_subr.c:1061`) are
   all in the image now — the build reads three of them back out of it — so their *absence* is the
   statement that the root mount succeeded on its first attempt, and their presence localises a
   failure that would otherwise be "nothing happened". `IOSecureBSDRoot` (`bsd_init.c:966`) is
   between the mount and the vnode lookup and waits 30 s for an `IOPlatformExpert` service; the
   prediction is that it *does not* wait, because `MSM8974PlatformExpert` started (457's `IORTC`
   wait is the proof) and a started service is a registered one, and because `callPlatformFunction
   ("SecureRootName")` is unimplemented and returns.
6. **The panic is silent and the trap is visible.** `panic()` prints through `kprintf`, which
   `disable_serial_output` gates off in this configuration (458's `xnu_live_console_noserial = 1`),
   so the panic's own text is not in the block — the reason 440 could only recover the message from
   `r9`. What the log shows instead is the entry's own record of the trap: a non-zero
   `xnu_entry_undef_pc` and `why = "exception: undefined instruction"` from the `udf #0xfdee` inside
   `DebuggerTrapWithState` (`osfmk/kern/debug.c:371`, reached from `panic` at `:645`), the shape 440
   measured. The device returns on the hardware watchdog at ~28 s, as in every run since 457.
7. **Timing: the run's records all land in the first ~7 seconds.** 457 measured the boot reaching
   `IOFindBSDRoot` at 6.0876 s. The root device is accepted immediately after, and everything from
   there to `load_init_program` — mockfs's three nodes, the vfs mount, `devfs_kernel_mount`, the
   clone of process 1, the exec attempt — is arithmetic and memory in an image that already has its
   stubs. So the last record's `now` should be under 8 s against a 28-second watchdog, and if it is
   not, the step's own claim that this path is short is wrong.
8. **The counters this step republishes say what the text weighs**:
   `xnu_entry_ostext_chars`, `_total`, `_lines`, `_heals` and `_limited` are written by the
   epilogue now (and every kilobyte of text, by the heal). Predicted values: `_limited = 0` (the
   128 KB block is nowhere near full), `_heals` = 0 or 1 depending on whether the text reaches
   1024 characters, and `_chars` in the **hundreds** — the 458-era 176 bytes plus the banner, the
   memory device's line, the `BSD root` line and the two `launchd` lines. A `_chars` in the
   thousands would mean this step prints far more than this document thinks it does.

Falsifiers, named in advance: (a) the run's log is 458's log — the 60-second record present and no
`md0` anywhere — which means `rd` was not parsed, or the memory-map node was not found, and the
`RAMDisk`-less case is distinguished by `panic`'s message slot (`xnu_entry_panic_str`); (b)
`Added memory device` present but no `BSD root:` line — `mdevadd` ran and `mdevlookup` failed, i.e.
the device index in `mdevadd`'s own call is not 0; (c) `BSD root:` present and `cannot mount root`
after it — the device was accepted and *mockfs* refused it, which would put the frontier in
`mockfs_mountroot`'s first allocation rather than in the exec; (d) a new 30-second iolock record
after `BSD root:` — `IOSecureBSDRoot`'s `IOPlatformExpert` wait, i.e. the platform expert is not
registered and prediction 5's second reason is the wrong one; (e) `errno 88` at the exec — the
zeroed disk passed exec's check, which would be a statement about `kern_exec.c` and not about this
step; (f) a `stub_hit=` or `abort_entries != 0` on the root-device path — a name this step needs is
resolving to a stand-in after all, and `verify_root_device`'s eleven names are the list to re-read.

## The build, and the numbers it made

The kernel is the composed configuration, and `out/xnu_kernel_obj/config.stamp` is the record this
step added:

    config STAGE90_XNU
    master_local /mnt/data/mi4-ios6/tools/xnu_config/boot/STAGE90_XNU.local
    objects 706
    defines 129
    pool_printf_shim 0
    pool_mockfs_objects 3

706 objects of which exactly three are `bsd_miscfs_mockfs_mockfs_{fsnode,vfsops,vnops}.o`, and no
object in the pool calls `_consume_printf_args` — that pair of numbers is the whole point of the
stamp: the entry link globs `out/xnu_kernel_obj/*.o`, so "which kernel is in this image" is decided
by which build ran last, and nothing in the image says which. `failed.txt` still names one file
(`osfmk/kperf/kperfbsd.c`), unchanged.

The entry build's own checks, all `layout_fail`, and its layout, from `build_entry.sh`:

    __bss_start 0x8050c000 is the .bss output section's first byte (606008 bytes to 0x8059ff38)
    xnu_entry_459: the pool is the STAGE90_XNU kernel (706 objects), no object in it calls
        _consume_printf_args, and mockfs is in it
    xnu_entry_459: the root-device path's eleven names are defined in this image, none of them stubbed
    xnu_entry_459: the four printf strings this step reads are in the image (the md device's own
        line, load_init_program's two, and vfs_mountroot's bdevvp failure)
    xnu_entry_459: vfstbllist names mockfs with ops at 0x804fc2dc and mountroot 802a6c0c at word +8,
        and devfs and routefs have no mountroot at that word - so vfs_mountroot mounts mockfs
    xnu_entry_459: the RAM disk is 0x8050e000 +0x40000 - page aligned, inside .bss, and below both
        the boot_args page and topOfKernelData

    entry base   0x80000000          entry point  0x80000074
    text size    5092896 bytes (.text)
    image bytes  5291016
    bss          0x8050c000 .. 0x8059ff38 (606008 bytes, zeroed by the payload)
    layout       args +5902336, topOfKernelData +7340032, tree +7208960, window 0x01000000
    headroom     1441992 bytes below topOfKernelData

and the host side reads the same two numbers out of the generated header, walks the tree with XNU's
own reader, and reads the property back the way `IOFindBSDRoot` will:

    RAM disk, from out/stage90/xnu_arm_entry.h: STAGE90_XNU_RAMDISK_VA 0x8050e000 / _SIZE 0x00040000
    tree built: 29632 bytes of 32768 (90.4% used, 3136 free)
      ok  0x8050e000 +0x40000, the entry image's g_stage90_ramdisk
      ok  /chosen:boot-args carries rd=md0

`host_dt_harness.c`'s `check_memory_map()` is what turns the second line into a claim: it reads
`/chosen/memory-map`, refuses a property whose length is not `sizeof(uintptr_t[2])`, compares both
words against the header's two values, and its failure text names the outcome this run then produced
— "without this property IOFindBSDRoot never calls mdevadd, mdevlookup(0) returns -1, and the same
function panics 'specified root memory device, md0, has not been configured'". Every one of those
checks passes; which is why the run's answer is a statement about the *registry plane* and not about
the tree.

**Two defects this step put in its own build, both found by reading the build's output, both fixed in
it.** The generated header is one unquoted heredoc so that the layout values expand, and its new 459
comment names symbols in backticks — so `` `verify_root_device` `` **ran that function a second time
and substituted its output into the file being written**, which is why the first build's header held
this script's own say-lines inside a C comment and printed five `command not found` lines while still
exiting 0. The fix is structural: the body is a *quoted* heredoc, every value is an `@NAME@`
placeholder, they are substituted by name afterwards, and the build refuses to continue if any
placeholder is left or if the header does not carry all 14 definitions. The second defect is the
first draft of that very check: `grep` exits 1 when it matches nothing, this script runs under
`set -e` and `pipefail`, and the *success* case — no placeholder left — ended the build with status 1
and nothing printed. That is 455's defect class a second time ("a check that dies silently on
success"), and it is why both greps now carry `|| true` and a `say` line reports the check when it
passes. After the fix the image is byte-identical to the one the device ran (`md5
ab253a424a12f5433f94eb5cee7b4659`), so both defects were confined to the header's comment text and
to what the build told its reader.

## The run

    LOGFILE=/tmp/cancro-459-last_kmsg.txt ./run_and_capture.sh --allow-xnu-entry
    wrote 457959 bytes, exit 0, device back on its own

**Prediction 1 holds, twice over.** The log has exactly two `xnu_live_iolock` records, both at
`waitForMatchingService+0xE0` (`0x80136908`) on the boot thread (`0x805540f0`), entered at
`now` 0x06c74aa1 and 0x06ca89b7 — 5.9231 s and 5.9342 s at this clock's 19.2 MHz — each with a
deadline 29.99 s out. Those are the two 30-second waits this path has: `IOKitInitializeTime`'s
`IORTC` (`iokit/Kernel/IOStartIOKit.cpp:65-72`) and `IOFindBSDRoot`'s own `IOBSD` resource
(`iokit/bsddev/IOKitBSDInit.cpp:379`, the function's first statement). **The 60-second record is
absent**, and the console text says the same thing a second way: 458's block ended with
`IOLog("Waiting on %s\n")` and the `IOMedia`/`Apple_HFS` dictionary in XNU's own words
(`:547`), and 459's text has neither, because the matching is built *after* the `rd` branch
(`:492-552`) and `rd=md0` leaves the function twenty lines above it.

**And prediction 3 holds — the string restoration took.** The console block now holds **921 bytes and
19 newlines** of XNU's own text, against 458's 176 bytes:

    \n[os-console-459]\n
    Darwin Kernel Version ###not-built-by-apple###\n\r
    vm_page_bootstrap: 2218 free pages and 1878 wired pages\n\r
    zalloc: allocating memory for zone names buffer\n\r
    "vm_compressor_mode" is 0\n\r
    multiq scheduler config: deep-drain 0, ceiling 47, depth limit 4, band limit 127, sanity check 0\n\r
    standard timeslicing quantum is 10000 us\n\r
    standard background quantum is 2500 us\n\r
    WQ[wql_init]: init linktable with max:262144 elements (8388608 bytes)\n\r
    WQ[wqp_init]: init prepost table with max:262144 elements (8388608 bytes)\n\r
    mig_table_max_displ = 1\n\r
    debug_log_init: Error!! gPanicBase is still not initialized\n\r
    iBoot version: \n\r
    Copyright (c) 1982, 1986, 1989, 1991, 1993\n\r
    \tThe Regents of the University of California. All rights reserved.\n\r
    MAC Framework successfully initialized\n\r
    using 80 buffer headers and 80 cluster IO buffer headers\n\r
    mcache: 1 CPU(s), 128 bytes CPU cache line size\n\r
    mbinit: done [0 MB total pool size, (0/0) split]\n\r

The banner is `printf(copyright)` at `bsd/kern/bsd_init.c:452`, the first statement of `bsd_init`,
and 458's run could not print it: `CONFIG_NO_PRINTF_STRINGS` made `printf` a call that discards its
first argument. It is here now, with `mcache`, `mbinit` and `MAC Framework` — one macro's worth of
difference, visible in the OS's own words.

**The frontier is `IOFindBSDRoot`'s `rd` branch, and the branch that follows it instead is the one
the host check named.** The evidence, in the order the source runs:

- `IOFindBSDRoot` **was entered**: its first statement is the IOBSD wait, and that record is there.
- The media matching was **not** reached: no 60-second record, and no `IOLog("Waiting on %s\n")`.
- So the function was in the `rd=md0` branch (`:445-490`) when it stopped. Every other statement in
  that window is a call that cannot panic on this tree: `IOMalloc` returning NULL returns
  `kIOReturnNoMemory` (`setconf` would have printed "IOFindBSDRoot returned an error" — absent),
  `PE_parse_boot_argn` cannot panic, `fromPath("/chosen", gIODTPlane)` and `di_root_ramfile` return
  early because the tree has no `boot-ramdmg-size`, `OSUnserializeXML` is not reached, and the two
  panics inside `mdevadd` (overlapping device, no free slot) are unreachable on a first call with
  `mdev[i].mdFlags & mdInited` clear in every slot.
- **`mdevadd` was never called**, and this is a print's absence doing real work: `mdevadd`
  (`bsd/dev/memdev.c:560-635`) prints on **all five** of its outcomes — the success line
  `Added memory device md%x/rmd%x (%08X/%08X) at %016llX for %016llX` and four early-return errors
  (`bdevsw_add`, `cdevsw_add_with_bdev`, and `devfs_make_node` twice). None is in the block, and the
  capture demonstrably carries `printf` — the copyright banner is a `printf` from the same class.
- So `mdevlookup(0)` returned -1, and the same function panicked on `:490`:
  `panic("IOFindBSDRoot: specified root memory device, %s, has not been configured\n")`.
- The trap follows in the same millisecond: the last live record is at 5.9348 s — **0.6 ms after the
  IOBSD wait was entered** — and the epilogue reports `exception: undefined instruction`, which is
  `DebuggerTrapWithState`'s `udf` (`osfmk/kern/debug.c:371`), i.e. a panic. It is silent for 458's
  reason, re-measured here: `xnu_live_console_noserial = 0x00000001`.

The run cannot name the trapping instruction, and that is the second thing worth carrying out of it.
`fleh_undef` writes its report — `xnu_entry_undef_lr`/`_pc`/`_spsr`, `xnu_entry_panic_str`,
`_panic_caller`, `_panic_message` — through `entry_kv` into the 8 KB `g_kv_buf`, and that buffer was
**full before the trap happened**: `xnu_entry_kv_written = xnu_entry_kv_in_dram = 0x1fea` (8170 of
8192 bytes) with `xnu_entry_kv_dropped = 0x66bb` (**26299 calls refused**). The buffer's first record
is a `t268_*` one — the kalloc tracer, which logs every allocation from `kernel_memory_allocate`
onwards, and which is why the payload's dump of the buffer is a run of `t268_kalloc_*` lines. So
prediction 6's instrument failed exactly where the doc's falsifier (a) had put its weight:
`xnu_entry_panic_str` is the key that would have named the panic, and it was written and dropped.

**Prediction errors.** 2 and 4 are wrong: there is no `Added memory device` and no `BSD root:` line,
and the frontier is not `load_init_program` — it is 20 lines earlier, in the same function that was
supposed to hand the OS a device. 5 is the interesting one: its *observation* holds (no `cannot mount
root`, no `We are hanging here...`, no `vfs_mountroot: can't setup bdevvp`) but its *reading* does
not — those three are the text of a mount that failed, and this run has them absent because the mount
never started: `setconf` panicked before `vfs_mountroot` was called. Absence of a failure message is
only evidence when the code that prints it ran. 6 splits: the trap's *shape* is as predicted
(`exception: undefined instruction`, `DebuggerTrapWithState`'s `udf`, a silent panic), and the
device came back on its own, but the entry **returned to the payload** — 454, 457 and 458 have no post-entry
report at all, because the boot was still inside the entry when the watchdog reset the device, and this
run's report is written before the watchdog fired.
7 holds: every record lands by 5.935 s, seven seconds inside the prediction's 8 and well inside the
watchdog. 8 holds: `ostext_chars = ostext_total = 0x399` (921, "in the hundreds"), `ostext_lines =
0x13`, `ostext_heals = 0`, `ostext_limited = 0`, and `xnu_live_ostext_tank = 0x262` (610 characters
held in the 8 KB `.bss` tank before the live channel's tables were installed — the tank earning its
place a second run in a row).

**Next**, and it is two instruments and one question. (0) The cheapest thing to do first is host-side
and needs no device: `tools/xnu_dt_walk.py` already replays XNU's own walk over the blob, and what it
has never replayed is `IODeviceTreeAlloc`'s *stack* loop (`IODeviceTreeSupport.cpp:167-186`, the
`OSArray` stack with `DTIterateEntries`/`DTEnterEntry`/`DTExitEntry`), which is the layer that decides
which entry is whose parent in the plane. A node that XNU's reader finds by path can still be attached
to the wrong parent, and `fromPath` walks parents. (1) The question is where the property went
between the DT blob and the IODT plane. Two names can be wrapped and would answer it without leaning
on the OS's print path at all: `IORegistryEntry::fromPath` (the lookup itself, with its path
argument and its return value) and `mdevadd`/`mdevlookup` (plain C, defined in `memdev.c`, so both
are ordinary undefined references a `--wrap` rewrites — 455's rule), which together say whether the
path resolved, whether the property was read, and what the device lookup returned. The `getProperty`
call in between is a *virtual* call through a vtable and cannot be wrapped; it does not need to be,
because its only producer is the property table `IODeviceTreeAlloc` built. (2) The trap path must
stop writing into the tracer's buffer: `fleh_undef`'s six keys are the whole diagnosis of a silent
panic, and they need either the live channel (`entry_write_kv`) or a buffer the kalloc tracer cannot
fill. (3) The macOS-side reasoning that has to be re-read first is the one that says the tree and the
plane are the same thing: the tree is verified by XNU's own reader on the host and the plane is built
from it by `IODeviceTreeAlloc` (`iokit/Kernel/IODeviceTreeSupport.cpp:96-215`, `attachToParent(…,
gIODTPlane)` per node), but the run says the plane is where the lookup failed, so one of the steps
between `DTInitEntryIterator` and `getChildEntry` is not doing what the code says.

## Safety, unchanged and re-stated for a step that changes what the kernel is built from

`fastboot boot` only, never flash; every touch through `stages/stage90/preflight_boot_check.sh
--allow-xnu-entry` and `stages/stage90/run_and_capture.sh --allow-xnu-entry`; the hardware watchdog
is armed by the payload before the jump and XNU never touches it, and it is the net that returns the
device (the software dead-man is disarmed before the entry by design, 308). Two new things this step
does are worth stating: the kernel this image runs is now built from a *different configuration*
(`STAGE90_XNU`), which changes no address and no boot path — it adds three objects and removes a
macro; and the memory the OS is told is its root device is 256 KB of this image's own `.bss`, which
`mdevadd` and then `mockfs` read and *write* through the UBC. The payload zeroes that range as part
of `.bss` before the jump, the build refuses the link unless the array is inside `.bss` and below
the boot_args page, and nothing the boot has run so far writes to a `.bss` address it was not
compiled to write — but it is the first time this project hands the kernel a region it may write,
and it is recorded here as the changed assumption rather than left in the diff.
