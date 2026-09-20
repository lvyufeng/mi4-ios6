# Experiment 492 — the node has a driver, and the catalogue was why it had none

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured twice
Artifacts: `stages/stage90/xnu_platform/{MSM8974Timer.cpp,stage90_platform_config_tables.c}`,
`stages/stage90/xnu_arm_boot/{entry_trace.c,entry_stubs.c,build_entry.sh}`,
`tools/{build_xnu_arm_kernel.sh,check_driver_catalogue.py,check_driver_plane_census.py}`

**Result: the driver layer now has a device driver in it, and the reason it had none was one line of
`IOCatalogue`.** 491 measured the symptom - 26 nubs under the platform expert, every one registered
and matched, not one service-plane child under any of them - and the cause is that `findDrivers` looks
personalities up **by the service's own class chain** (`IOCatalogue.cpp:199-231`), because
`addPersonality` files each one under its `IOProviderClass` (`:124-132`). Every personality in this
kernel's table was filed under `IOPlatformExpertDevice` or `IOResources`; every nub of this machine's
device tree is `IOPlatformDevice` (`IODTPlatformExpert::createNub`, `IOPlatformExpert.cpp:1283`), so
every nub's lookup came back empty and `probeCandidates` never ran for one. With a personality filed
under `IOPlatformDevice` and a driver for the `/timer` node, the run says:

    xnu_live_timerdrv_seq=0x00000001   the driver started once
    xnu_live_timerdrv_prov=0xc05d19c8  on the nub that is 491's third census row 26
    xnu_live_timerdrv_match=0x00000001 matched on the node's `name`
    xnu_live_timerdrv_have=0x00000003   the node published both `frequency` and `reg`
    xnu_live_timerdrv_freqkind=0x2      as a 4-byte `OSData`, the kind the tree plane produces
    xnu_live_timerdrv_freq=0x0124f800   the node's 19,200,000 Hz
    xnu_live_timerdrv_reg0=0xf9020000   the node's `reg[0]`
    xnu_live_timerdrv_cntfrq=0x0124f800 the machine's own CNTFRQ, read from the CPU
    xnu_live_timerdrv_agree=0x00000001  they agree

and 491's third census, under a cap that now fits the level it walks, records **29 of 29 rows** with
`kids_of = 1` on exactly that row. So the goal's second half moved by one device node: the OS is
entered, IOKit's nub pass builds the whole tree, a driver of this machine's own is matched to a node
of that tree and started on it, and the driver's first act is to read its device's description and
compare it with the hardware. The boot's own console frontier did **not** move - it is still
`load_init_program: attempting to load /sbin/launchd` - and nothing in this step claims otherwise.

Two device runs, both through the gate, both exit 0 with the device back on Android on its own. The
second run is the one whose readings are published; the first is kept because it is what found the
defect in the driver's own reader, which is the more useful half of this experiment.

## Why the driver tree was empty: the bucket, not the matching code

490 and 491 read the matching code to the bottom and it is not where the failure was. `doServiceMatch`
decides everything on

    IOService.cpp:3688   matches = gIOCatalogue->findDrivers( this, &catalogGeneration );
    IOService.cpp:3724   if (keepGuessing && matches->getCount() && (kIOReturnSuccess == getResources()))

and `findDrivers` is a lookup by class name, not a walk over anything:

    IOCatalogue.cpp:199-231
        meta = service->getMetaClass();
        while (meta) {
            array = personalities->getObject( meta->getClassNameSymbol() );
            ... if (meta == &IOService::gMetaClass) break;
            meta = meta->getSuperClass();
        }

with `addPersonality` (`:124-132`) putting each personality in the array its `IOProviderClass` names.
A nub's chain is `IOPlatformDevice` then `IOService` (`createNub` is `nub = new IOPlatformDevice`,
`IOPlatformExpert.cpp:1283`), so **no personality filed under `IOPlatformExpertDevice` can ever be
found by a nub's lookup** - the class of the *root* nub is not the class of any nub below it. The
table's three entries before this step were `MSM8974PlatformExpert` and `IOPanicPlatform` under
`IOPlatformExpertDevice` and `MSM8974RootResource` under `IOResources`, and that is the whole of the
explanation for an empty driver tree on a tree whose every node registered and matched.

The step's own error, stated because it was made: before building, this experiment predicted that a
driver attached to a *nub* would abort, on the reasoning that `IOPlatformDevice::getResources` is
`((IOPlatformExpert *)getProvider())->getNubResources( this )` (`IOPlatformExpert.cpp:1700-1703`) and
that `getNubResources` was an unresolved symbol in this image. It is not:
`_ZN18IODTPlatformExpert15getNubResourcesEP9IOService` is at `0x80175fec` and
`_ZN16IOPlatformDevice12getResourcesEv` at `0x80176998`, both defined, both in the 26-entry undefined
list's complement. The run is what settled it, and the driver started on a nub with nothing special
arranged. (The `gIOResources` exemption at `IOService.cpp:3489` is real and remains the reason the
resource root starts without a provider resource walk; it is not the reason a nub could not.)

## The personality, and the three files that have to agree

    stage90_platform_config_tables.c
      { 'IOClass' = MSM8974Timer; 'IOProviderClass' = IOPlatformDevice;
        'IONameMatch' = (timer, "qcom,msm-timer"); 'IOProbeScore' = 1616:32; }

Three separate facts have to line up for that entry to start anything, and each is in a different file:

  * **the bucket** - `IOPlatformDevice` - is the class `createNub` builds, in Apple's
    `IOPlatformExpert.cpp:1283`;
  * **the class name** `MSM8974Timer` is defined by exactly one object this image links, and that
    object is in `tools/build_xnu_arm_kernel.sh`'s `PLATFORM_SOURCES` (one class, one definition);
  * **the names** `timer` and `qcom,msm-timer` are the `/timer` node's `name` and `compatible` in the
    payload's tree (`stage90_main.c`), which is what the candidate test compares them against -
    `IOPlatformDevice::compareName` is `compareNubName` (`IOPlatformExpert.cpp:1688-1693`) over the
    provider's own properties - and they are also the two strings the driver compares `IONameMatched`
    with, so a match made on a name this driver does not know reads as `_match = 3` instead of being
    silently counted as a start.

`tools/check_driver_catalogue.py` is the new check over all of it, and it reads `PLATFORM_SOURCES` out
of the build script rather than repeating it. It is a source-level check on purpose: an image cannot
say which provider class a personality *would* have matched - only which services ended up matched -
and 491's run is precisely the case where the image looked right and the driver tree was empty. Eight
claims, 14 mutations, all refused. Two of the mutations are the two defects this check had in itself:
it first parsed the **stock table quoted inside the file's own doc comment** (the pattern matches a
comment as well as code - the defect class this project keeps a memory for), and it first read the
C string literal `\"qcom,msm8974-xnu-stage90\"` with its backslashes intact and compared that with the
node's `compatible`. Both were found by running the check, not by reading it.

## Run 1: the driver started, and its reader was what was wrong

The first run's record is

    xnu_live_timerdrv_match=0x00000001  xnu_live_timerdrv_have=0x00000002
    xnu_live_timerdrv_freq=0x00000000   xnu_live_timerdrv_reg0=0xf9020000
    xnu_live_timerdrv_cntfrq=0x0124f800 xnu_live_timerdrv_agree=0x00000000

i.e. the driver matched, read the node's `reg`, and found no `frequency` - in a node that has one. The
reader was an `OSDynamicCast( OSNumber, provider->getProperty( "frequency" ))`, and the kind is the
bug: every property of a device-tree node becomes an **`OSData`** when the node is turned into a
registry entry - `data = OSData::withBytes( prop, propSize )`, `IODeviceTreeSupport.cpp:379`, for the
node's `name` as much as for its `frequency`. `reg` was read with the same byte-wise reader in both
runs, which is why it worked while its neighbour did not: the failure was never in the tree.

That is the shape this project has a memory for - a reported number that is an artifact of how it was
taken - and the record would have read "the tree has no `frequency`", which is a claim about the tree.
So the driver now reads both kinds, publishes which kind it found (`_freqkind`: 1 = `OSNumber`, 2 = a
4-byte `OSData`, 3 = an `OSData` of another length, 5 = an `OSData` whose bytes could not be read),
and `tools/check_driver_catalogue.py` refuses a build in which a device-tree property is read without
the `OSData` cast. Run 1 is kept in this doc as the measurement it was, and run 2 is the corrected
reader over the same tree.

## Run 2: the tree's 19,200,000 and the machine's own CNTFRQ agree

    _have=0x3  _freqkind=0x2  _freq=0x0124f800  _reg0=0xf9020000
    _cntfrq=0x0124f800  _agree=0x1

Both numbers are read live at run time from two different sources - the node's property out of the
registry, and `CNTFRQ` out of the CPU (`mrc p15, 0, r, c14, c0, 0`, the register the payload's
`timebase.c` reads at boot) - and until this step nothing compared them: the tree's value is checked
host-side by the tree's own selftests and the register's by the payload, in two different programs.
`_agree = 1` is that comparison, made inside the OS, on this machine.

**The driver does not yet touch the device.** `_reg0 = 0xf9020000` is recorded and not dereferenced:
the address is inside the `io_ranges` window the payload maps, but "the window is in the kernel's page
tables" is a property of `mmu.c`'s mapping rather than of the device tree, and a first driver that
faulted on its own device's registers would be the instrument's failure and not the device's. The
first register read belongs to the step that proves the mapping first.

## The catalogue's answers, all thirty of them

The `findDrivers` wrapper was opened to **every** service for this step (491 restricted it, because
its question was the nub pass), and it now publishes the generation the call was made with and whether
the service asked about was `gIOResources` - `findDrivers` is asked about two populations, and a count
of zero means something different for each. Run 2's thirty calls:

| what | count |
| --- | --- |
| services answered with exactly one candidate | 27 |
| the root nub (`IOPlatformExpertDevice`'s bucket: the platform expert and the fallback) | 1 call, 2 candidates |
| services answered with nothing | 2 |
| `gIOResources` calls (`_res = 1`) | 2, one candidate each |

The 25 nub lookups are `calls 4..28`, every one answered with one, and call 28 is the `/timer` nub -
the same pointer the driver's own `_prov` publishes, and census row 26. Call 3 comes before them and
answers nothing, and call 29 (after the nubs) answers nothing too: two services whose class chain holds
no personality at all. Those two are the honest remainder of this table: the record publishes the
service's *pointer* and not its class, so "which two services the catalogue cannot answer for" is an
owed reading, not a reading.

## The census now covers the level it walks

491's cap made the third census record part of the level: `STAGE90_PEX_DEEP` was 24 with
`STAGE90_PEX_ROOT` 8, so the root's children took rows and the inner loop had 16 left for a level that
has 29 rows in it. It is 40 now (inner room 40 - 8 = 32) and both runs record:

    pex_seen=0x1d  pex_shown=0x1d  pex_named=0x1d  pex_registered=0x1c
    pex_matchpass=0x1c  pex_inactive=0x0  pex_kids=0x2  pex_deep_l1=0x2

29 rows seen, 29 shown. Two are the root's children (`MSM8974P…` with 26 children, `IOResour…` with 1),
27 are one level below them, and **row 26 - the `timer` nub - has `kids_of = 1`**, which is the driver.
The `pex_*` rows are otherwise what 491's were: every nub `Registered|Matched|FirstPublish|FirstMatch`
(`state0 = 0x1e`), `pex_inactive` zero, and `MSM8974R…` (`MSM8974RootResource`) still reading
`state0 = 0` at census time - an owed reading since 491, unchanged by this step.

The library change this forced is in `tools/check_driver_plane_census.py`, and it is the same defect
one step out: two of its mutations spelled the cap as the literal `24u` in their **needles**. A
needle that is a *value* stops mutating the day the value moves, and the failure mode is worse than a
stale test - `_bump`'s assertion turns "refused the mutation" into a crash, which is a different
statement about the check. Both needles are now read out of the files, and the 492 keys (11 live, 11
report) are held to a writer on each channel like every other key of this step's family.

## The two checks, extended

  * `tools/check_driver_catalogue.py` (new, wired into `build_entry.sh` beside the others): the table
    parses; every `IOClass` is either Apple's fallback or defined by exactly one file of
    `PLATFORM_SOURCES`; every class so compiled is named by a personality (an object linked with no
    personality is unreachable); the entry that is not under a kernel-created provider class names the
    class `createNub` builds; its `IONameMatch` names are the `/timer` node's `name` and `compatible`
    **and** the two the driver compares `IONameMatched` with; the properties the driver reads are the
    node's, read as the kind the plane produces; the platform expert's name is the tree root's
    `compatible`; the resource personality's `IOResourceMatch` is the resource `IOKitBSDInit.cpp`
    publishes; and no personality carries a `CFBundleIdentifier` (without which `probeCandidates`
    stalls on `isModuleLoaded`, `IOService.cpp:3253`).
  * `tools/check_driver_plane_census.py`: the cap-derived needles above, the 22 new keys, and the
    keys' two writers per channel.

And one defect in a build tool, found by using it: **`tools/build_xnu_arm_kernel.sh --platform-only`
wrote the kernel pool's `config.stamp` for a pool it did not build.** The mode skips the kernel loop,
so a `--platform-only` run without `XNU_KERNEL_CONFIG` stamped `config RELEASE` over a `STAGE90_XNU`
pool whose 708 objects were untouched - a recorded fact about a build that did not happen, in the file
459 created precisely so that this class of mistake could not pass silently. `build_entry.sh`'s
`verify_root_device` refused the next link for it, which is the guard working and the *writer* being
wrong. The mode now reads the stamp instead of writing it, refuses when the pool is not the
configuration it would compile the platform blocks with, and says which configuration to set. Both
directions were measured: without the environment it exits 3 with the message; with it, it prints
`platform-only: the pool in /mnt/data/mi4-ios6/out/xnu_kernel_obj is the 'STAGE90_XNU' kernel, and the
stamp is left alone` and builds.

## What is owed

  * **Which two services the catalogue answers with nothing** (the 2 zero-count calls): the record
    publishes the service pointer and not its class, so this needs the class reader the next key
    should carry.
  * **The node's registers**: `_reg0` is recorded and not read; the mapping has to be proved first.
  * **`MSM8974RootResource`'s `state0 = 0`** at census time, owed since 491.
  * **`IOPMrootDomain`'s start**, recorded through `startCandidate`'s vtable slot rather than inferred
    from an attachment (491's).
  * **A name/class reader wider than eight characters**: `IOPlatfo…` cannot separate
    `IOPlatformDevice` from `IOPlatformExpert`, and every row's name and class in both censuses are
    printed at most eight characters wide (491's, still the sharpest limit on what this census can say).
  * **The other device nodes**: `/interrupt-controller`, `/arm-io`, `/cpus` and the rest have the same
    shape as `/timer` and no driver; the timer is the first because its two numbers could be checked
    against the hardware.
  * `xnu_live_dec_same` (0x2 in this run) remains unexplained.

## The build

Canonical four steps, all green: the pool (`--platform-only` fixed as above), `gen_assym.sh` (266
defines), `assemble_arm_layer.sh` (17 ok, 0 failed), `build_entry.sh` (image 5470840 bytes, 26
undefined symbols, 59 wraps), `stages/stage90/build.sh` (payload 5966104-byte `.bin`,
8491008-byte `.img`). One trap is worth recording here because it cost this session two failed builds
and it is the same defect as the stamp: **step 2's two scripts must both carry `XNU_KERNEL_CONFIG`.**
The documented command line is `XNU_KERNEL_CONFIG=STAGE90_XNU ./tools/gen_assym.sh` *and*
`./tools/assemble_arm_layer.sh`, and the second one takes its `CONFIG` from the environment too
(`CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}`); assembling without it gives a `cswitch.o` built against
*RELEASE*'s `TH_CTH_SELF` (1480) while `check_assym_cswitch.py` compares it with `STAGE90_XNU`'s
(1496) - which is exactly the failure the check exists to catch, and it caught it. **Both step 2
commands need the environment variable, not just the first.**

## Safety

Two runs, each through `preflight_boot_check.sh --allow-xnu-entry` then
`run_and_capture.sh --allow-xnu-entry`, non-persistent `fastboot boot` only. Both exit 0, both
captured (`/tmp/stage90-492-run1.log` 550303 bytes, `/tmp/stage90-492-run2.log` 550461 bytes), both
ending `No errors detected`, and both returning to Android on their own - the hardware watchdog and
the software dead-man armed for the run and neither spent. No persistent write of any kind: the image
is only ever `fastboot boot`ed.
