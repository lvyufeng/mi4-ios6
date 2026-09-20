# Experiment 491 — the bit registration sets, and the level the nubs are at

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_arm_boot/{entry_trace.c,entry_stubs.c}`, `tools/check_boot_completion.py`,
`tools/check_driver_plane_census.py`

**Result: the number this project has been reading as "matched" is set by registration, and the level
where the drivers are attached had never been walked - and both are now answered from one run.**
`IOService::doServiceMatch` writes `kIOServiceMatchedState` for every service that registers and is not
`Inactive`; nothing on that path tests a match result. The run says so empirically in the same log:
`chosen`, `defaults`, `memory` and `cpus` are nodes no driver can match, and every one of them carries
the bit. And the census's third walk - the one 487's owed list named - finds **26 services attached to
the platform expert *instance***, one level below the only place this project had looked, of which the
first sixteen are recorded: **`IOPMroot…` (Apple's `IOPMrootDomain`) and fifteen nubs of the class the
source names `IOPlatformDevice`** (the record's eight characters read `IOPlatfo…`, a prefix that class
shares with `IOPlatformExpert`, so the class here is the source's reading and the prefix is the
record's). So the driver
layer ran: the platform expert's nub pass attached its nubs, and a real Apple driver class attached
under it as a started service. The goal's second half is therefore no longer unmeasured - and it reads
*partly*: the OS is entered, IOKit's nub pass ran over the whole device tree, one Apple driver started,
and nothing that describes a device of this machine has a driver of its own.

One device run, through the gate, exit 0, device back on Android on its own. The OS console block is
byte-identical to 490's (`7ecfdceb…`, 1307 bytes over 26 lines), the abort record is the same eight
aborts at the same sites, and `getpid_count` still doubles to `0x800000` - so nothing about the OS's own
behaviour moved. The image did move, and by a measured amount: see "the image moved by a page".

## The bit, and what sets it

Two censuses have published a `Matched` tally - 485's over the tree's children and 487's over the
service plane - and both read `state0 & 0x4` with `0x4` derived from `IOService.h:74`. The header says
what the bit is *called*; it does not say what sets it, and 491 read the setter:

    IOService.cpp:3754   notifiers[0] = copyNotifiers(gIOMatchedNotification,
                                                      kIOServiceMatchedState, 0xffffffff);
    IOService.cpp:3748   if( (0 == (__state[0] & kIOServiceInactiveState))
                          && (0 == (__state[1] & kIOServiceModuleStallState)) ) {
    IOService.cpp:4782       __state[0] = (__state[0] | orNewState) & andNewState;   /* inside copyNotifiers */

The write at `:3754` is inside the block that begins at `:3748`, and that block sits **after** the
`while( keepGuessing )` loop whose only consumer of `findDrivers`'s answer is
`if (keepGuessing && matches->getCount() && …)` (`:3730`) with `matches->release()` as its else-arm
(`:3736`). So a service no driver matched reaches the write by the same path as one that is matched: the
condition is "this service is not Inactive and not ModuleStalled", and the bit means *its match pass
ran*. `copyNotifiers` itself only writes the bits when the same condition holds (`:4760`).

The chain from a service to that write is longer than a reader would guess, and the check is what said
so - twice:

    registerService (:751) -> startMatching( options ) (:803)
    startMatching        -> doServiceMatch( options )          (:862, synchronous)
                         -> _IOServiceJob::startJob           (:838, asynchronous)
    startJob             -> pingConfig( job )                  (:956)
    _IOConfigThread::main-> nub->doServiceMatch( job->options )(:4103)

Two call sites in the whole file, and my first two drafts of the claim named the wrong callers -
`registerService` first, then `pingConfig`. Both were refused by the check, which is the reason the
claim is four measured links here and a sentence in the doc rather than a sentence in the check.

## The run's own control: the nodes no driver can match

Measured, this run, `xnu_live_dtk_*` on the live channel:

| child | name (read, eight characters) | `state0` |
| --- | --- | --- |
| 13 | `chosen` | `0x0000001e` |
| 14 | `defaults` | `0x0000001e` |
| 15 | `memory` | `0x0000001e` |
| 16 | `cpus` | `0x0000001e` |
| 19 | `interrup…` (`interrupt-controller`) | `0x0000001e` |

`0x1e` = `Registered|Matched|FirstPublish|FirstMatch`, Inactive clear. `chosen` and `defaults` are
OSDictionary nodes the platform expert publishes; `memory` and `cpus` are containers. No driver
matches any of them, and every one of them carries the bit - and the four that fit in eight characters
carry it with no expansion at all. So the run falsifies the key's old name
from its own table, and it does it on four rows rather than by an argument. `xnu_live_dtk_matchpass` =
`xnu_live_dtk_registered` = `0x15` = 21 of 21, and `xnu_live_dtk_inactive` = 0.

The key is renamed for that reason: `_matched` is gone from the report and the live channel alike, and
`_matchpass` with `_inactive` are in its place. Runs before 491 print `xnu_entry_dtk_matched` /
`xnu_entry_svc_matched`, which are the same numbers under a name that stated an outcome the kernel
never tests. This is 490's `_recovered` recurring one layer up: a key named for what the reader hopes
the number means.

## The level the nubs are at: 26 services under the platform expert

`entry_probe_service_plane` walks `getServiceRoot()`'s children and found **two** in 490's run. Two is
not what `createNubs` produces, and the source says why: `IODTPlatformExpert::processTopLevel` calls
`createNubs( this, … )` (`IOPlatformExpert.cpp:1360`, `:1363`) with `this` being the platform expert
**instance**, and `createNubs` does `nub->attach( parent )` on each nub it makes (`:1310`). So the nubs
are one level *below* the root 487 read, and until this step nothing in this project had looked there.

The third census walks two levels and the run fills it. **Every name and class here is what `entry_str8`
publishes, and that reader is exactly eight characters wide** (`entry_trace.c:1870-1885`: two words, four
bytes each), so a cell ending in `…` is a *prefix* and the characters after it are the reader's:

| `seq` | `depth` | name (read) | class (read) | `state0` | `kids_of` |
| --- | --- | --- | --- | --- | --- |
| 0 | 1 | `MSM8974P` | `MSM8974P` | `0x1e` | **26** |
| 1 | 2 | `IOPMroot…` | `IOPMroot…` | `0x1e` | 1 |
| 2-5 | 2 | `cpu@0` … `cpu@3` | `IOPlatfo…` | `0x1e` | 0 |
| 6-16 | 2 | `device-t…`, `iokit-pl…`, 5 × `msm8974-…`, `iokit-ca…`, 3 × `stage90-…` | `IOPlatfo…` | `0x1e` | 0 |
| 17 | 1 | `IOResour…` | `IOResour…` | `0x1e` | 1 |
| 18 | 2 | `MSM8974R` | `MSM8974R` | **`0x00000000`** | 0 |

Four of those prefixes are expanded from the fixture's own tree rather than from the record -
`IOPMroot…` = `IOPMrootDomain`, `IOResour…` = `IOResources`, `device-t…` = `device-tree`,
`iokit-pl…` = `iokit-platform`, `iokit-ca…` = `iokit-catalog` - and the three `stage90-…` rows are the
fixture's own `stage90-*` nodes, whose distinguishing suffixes the eight bytes do not carry at all. **One
prefix does not identify its class at all: `IOPlatfo…` is the first eight characters of
`IOPlatformDevice` *and* of `IOPlatformExpert`.** The first draft of this table printed the expansions as
if they were readings and concluded `IOPlatformDevice` from them; the instrument publishes eight
characters and the claim is the source's, not the record's.

Read as a reading rather than as a list:

- **`MSM8974P` is the platform expert, and it has 26 service children.** `kids_of` is that row's own
  `getChildCount(gIOServicePlane)`, and it is what names the object the nubs hang from: the walk does
  not decide which child is the expert, it records both and lets the count say. 26 is
  `processTopLevel`'s two `createNubs` calls and `processTopLevel`'s own `dtNVRAM->attach( this )`: four
  `cpu@N` nubs, twenty-one top-level nubs, and `IODTNVRAM`.
- **The first sixteen of them are named in the table and the cap is visible.** `ENTRY_PEX_DEEP` is 24
  and the inner loop takes `STAGE90_PEX_DEEP - STAGE90_PEX_ROOT` = 16 of the root's children, so
  `seen` = 19 = 2 + 16 + 1 while `MSM8974P`'s own count is 26. That gap is the reading the `count`/`shown`
  pair exists for: a table that stopped at the cap and a small subtree must not print the same. The ten
  rows that were not recorded are the *rest* of the top-level nubs.
- **`IOPlatfo…` at fifteen of the sixteen**, which is 487's prediction answered - by the source and by the
  prefix together, because the eight characters alone cannot carry it. `createNub` is
  `new IOPlatformDevice` (`IOPlatformExpert.cpp:1283`), so the nub pass ran and replaced the tree's raw
  `IOService` entries with `IOPlatformDevice`s; the record says `IOPlatfo…`, the prefix that class shares
  with `IOPlatformExpert`, and the one instance in this tree that *is* a platform expert reads `MSM8974P`
  - its own class name, not the base's. 487 predicted `IOService` on the ground that no
  `IODTPlatformExpert` subclass existed in this kernel for `configure` to run in - **and the run falsifies
  that**: `MSM8974P`, the fixture's own subclass, is the instance, `configure` ran, and `createNubs`
  attached.
- **`IOPMroot…` is a driver that started.** It is one of Apple's own classes (`iokit/Kernel/
  IOPMrootDomain.cpp`, in this build - the record's eight characters are its prefix), it is not a nub, and
  the only two routes to being attached in
  `gIOServicePlane` are `createNubs`'s `nub->attach( parent )` and `startCandidate`'s
  `ok = service->attach( this )` (`IOService.cpp:3483`). `startCandidate` calls `service->detach( this )`
  when `service->start(this)` returns false (`:3505-3506`), so an attachment that survives is a start
  that succeeded. `IOPMrootDomain`'s presence is therefore a *started driver* by construction and not
  by inference from the console.
- **`MSM8974R` is the one row with `state0 = 0`** - attached, never registered, so it never reached
  `doServiceMatch` at all. One row of the driver layer's table is an object that exists and does
  nothing, and it is recorded rather than explained: this step has no reading of what creates it.
- **`xnu_live_pex_none` is absent**, which is the third census's own "no reading" key: no service
  plane, no service root, no child set are three reasons and none of them fired.

## What the run cost, and what it did not touch

The same eight aborts, in the same order, on the same two threads as 486's, 487's, 488's and 490's
runs, with kernel return addresses `8028cccc`, `8028e680`, `800434a0`, `8028e03c`, `800a9d34` and the
fixture's own `1118`/`1124` unmoved; `xnu_live_sleh_seen` = `0x20`, `_armed` = `0x1c`, `_redirected` =
`0x1a`, `_storm` = 9; `getpid_count` = `0x800000` with `last = 1` and `error = 0`;
`irq_timer_count` = `tmr_qexp_over` = `0x800`; the five tail calls all present with `idx = 4` =
`vm_pageout`; zero `undef` records; and the console block byte-identical to 490's. The census runs
inside `vm_pageout`'s wrapper at line 6237 of 7211, so it is taken *before* the clock cuts the log - and
for the first time the tallies it computes are on the live channel, because the run that takes them
never reaches the report (490 measured that: its log ends on `sleh_seen = 0x20` with no report at all).

## The image moved by a page, and the arithmetic says why

Every address in this run is higher than 490's, and not by one amount:

| | 490 run 2 | 491 run 1 | delta |
| --- | --- | --- | --- |
| `copyin` (`nm`) | `80014944` | `80015084` | **+0x740** |
| `copypv` (`nm`) | `8004241c` | `8004341c` | **+0x1000** |
| `load_init_program_at_path` (`nm`) | `8028bc9c` | `8028cc9c` | **+0x1000** |
| `telemetry_take_sample` (`nm`) | `800a8bdc` | `800a9bdc` | **+0x1000** |
| `Lcopyout_wordwise_loop` (log) | `80014ac0` | `80015200` | **+0x740** |

The two deltas differ by `0x8C0`, and `0x740 + 0x8C0 = 0x1000`: the symbol at that boundary is
**`locore_ExceptionLowVectorsBase`, at `0x80016000`** and 4 KB-aligned in this image - the vector page
must sit on a page boundary (Phase 1's requirement, and the reason `stage85` could run code there at
all). So this step's 6368 bytes of added `.text` moved everything before the vector block by `0x740`,
and the vector block's own alignment absorbed the remaining `0x8C0`, moving everything after it by
exactly one page. `.text` 5254728 -> 5261096 (+6368), `.bss` 361032 -> 361928 (+896), `bss
0x80537a80 .. 0x80590248`, headroom 1508152 -> 1507256 (-896). Nothing about the *kernel's* code moved
in the sense of being recompiled: the deltas are all layout, and the vector page being page-aligned
after the move is the invariant that mattered.

## The two checks, extended

`tools/check_boot_completion.py` gained claim 6 and `tools/check_driver_plane_census.py` gained claims 8
and 9.

- **Claim 6 (boot completion)**: the setter is `copyNotifiers`, whose whole write is one statement; the
  matched write is in `doServiceMatch`, is the **only** occurrence of
  `copyNotifiers(gIOMatchedNotification` in that body, sits **outside** the balanced
  `while( keepGuessing )` loop, and has no `getCount`/`findDrivers`/`matches->` between the loop and
  itself; the guard names `kIOServiceInactiveState` and `kIOServiceModuleStallState`; the four links
  from `registerService` to the write are present; `doServiceMatch` has exactly **two** call sites in
  the file; and no writer in `entry_stubs.c` still spells `_matched`.
- **Claims 8 and 9 (driver plane)**: the third census reads the same six accessors and the same
  `__state[1]` offset, descends by taking each **row's** child set rather than the root's again - which
  is the difference between walking the level below and walking the same level twice - publishes each
  row's own child count and how many rows it descended into (`entry_note_pexdeep(l1)`, the variable and
  not the name, because the early return also calls it with `0u`), calls `entry_note_pexchild(` twice
  and not once, keeps its three "no reading" reasons apart, is called between the service census and
  the call that never returns, and uses its own class-read site; and the old `_matched` name is refused
  wherever it reappears.

Both selftests green: **62 mutations refused** (boot completion, from 55) and **58** (driver plane, from
45). Four of the new mutations found real holes in my first drafts rather than confirming them - the
`_matched` rename had to be stated over the *file* rather than over a function body; the
`entry_note_pexdeep(l1)` claim was satisfied by the early return's `0u` call; the
`entry_note_pexchild(` claim was satisfied because `_bump` replaces only the first of two calls; and
the wrapper-order mutation raised instead of mutating when the line it needed was removed. A mutation
that cannot run is not a mutation, and a claim a second call site satisfies is not a claim about the
call site.

## What is owed

- **The third census's array count is not published.** The other two censuses publish *two* counts of
  one child set - the entry's own `getChildCount` and the array's `OSArray::getCount` on its
  `getChildSetReference` - and the third publishes the entry's answer (`kids_of` per row) but not the
  array's for the root. So `26` and `shown = 19` are separated by the cap by arithmetic rather than by
  a second reading, which is the same "one value, two accessors" pair this project keeps asking for.
- **The name and class readers are eight characters wide, and one claim needs the ninth.**
  `entry_str8` publishes two words and stops (`entry_trace.c:1876`), so `IOPlatfo…` cannot separate
  `IOPlatformDevice` from `IOPlatformExpert` - the distinction this step's central sentence rests on,
  and one it had to take from the source instead of from the record. A third word is four more
  characters and would carry `IOPlatformDev`, which is unique in this tree; the same widening fixes the
  three `stage90-…` rows, whose suffixes are exactly what the cap cut.
- **`IOPMrootDomain`'s start is an inference from an attachment**, and the inference is sound because
  `startCandidate` detaches on failure - but the *call* is still not recorded. `startCandidate` is
  virtual (`APPLE_KEXT_COMPATIBILITY_VIRTUAL` expands to `virtual` on 32-bit: `OSMetaClass.h:93-99`,
  confirmed by `IOKitDiagnosticsClient`'s vtable holding `IOService::startCandidate` at slot 153), so
  `--wrap` cannot catch it - 455's `getProperty` rule recurring. What *would* catch it is the
  instrument reading the vtable slot and comparing, which is a measurement this step did not take.
- **Unchanged and still owed:** 490's distinct-`(pc, lr)` frames band; the release as a reading;
  `vm_fault` as a caller-side record; 488's deferred flag-list derivation; `MSM8974R`'s creator; the
  ten nub rows the cap cut; and `xnu_live_dec_same`, which is absent from this run as it was from 488's
  and 490's and reached `0x800`, and in 485's run 2 `0x20`, and still needs its own explanation.

## The build

Step 3 (`build_entry.sh`, `STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1`) green with
`--selftest: all 62 mutations were refused` for 485's check and `58` for 487's, beside 490's 31, 489's
30 and 488's 21. Step 4 (`stages/stage90/build.sh`, `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'`)
green with `kernel_size = 5966100`, `dt_size = 2521088`. 26 undefined symbols, entry base `0x80000000`,
entry point `0x80000074`.

`xnu_arm_entry.bin` 5470836 bytes sha256 `3053c7759c1794d42a0456c574f2077205b88b72f5b6fc41360a5df68c07be3e`,
`.elf` 6639252 sha256 `4a2a615f3dd7f2f031c5a2271eca6031167964f83dfc83370415c5e309fb1863`, `stage90.bin` 5966100
`4c95c483db5d3b158ae6bdf016a08bbd37d38f9519ae62148b8a0adaedb4a30f`, `stage90-qcdt.img` 8491008
`ee0056cb373bac3abcb31652e13f8f69883856a994fb169994856c0a48f50fe9`. The checks were edited before the
build, so the run is of the shipped image.

## Safety

One non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of
either, `xnu_entry_checks = 5` / `xnu_entry_failures = 0` in the pre-jump report, no `exception:` line,
no `panic_entered`, no `stub_hit=` line, and the device back on Android on its own (`MI 4LTE`, release
10) after the run: 533032 bytes captured.
