# Experiment 462 — the delete list was every entry, and the boot mounts its root filesystem

**Status: on the device, one run, 447772 bytes of log, exit 0, device back on Android by itself.
Three results and one new frontier. `MSM8974PlatformExpert`'s `deleteList()` answered
`(const char *)0`, and to `IODTFindMatchingEntries` that is not "no names" - it is the *else*
branch, "collect every entry" - so `IODTPlatformExpert::processTopLevel` detached the device tree's
entire top level from the IODT plane, `chosen` included. Answering `"()"` instead, the tree survives
its own platform-expert start; the whole root-device chain then runs: **`mdevadd` is called,
`mdevlookup(0)` returns a device number, XNU prints `Added memory device md0/rmd0 (01000000/0B000000)
… for 0000000000040000` and `BSD root: md0, major 1, minor 0`, and `vfs_mountroot()` succeeds** -
which is what `IOSecureBSDRoot`, the statement right after the mount loop's `break`, is reached for.
The frontier is then one line further on: `IOSecureBSDRoot`'s
`waitForMatchingService(serviceMatching("IOPlatformExpert"), 30 s)` blocks the boot thread for a
deadline the payload's 25 s watchdog beats, and nothing is recorded after it.**

## The defect, and why it took the whole top level

`IOPlatformExpert.h:229-230` makes both lists pure virtuals and `IODTPlatformExpert::processTopLevel`
(`IOPlatformExpert.cpp:1314`) opens with:

```c
    kids = IODTFindMatchingEntries( rootEntry, 0, deleteList() );
    while( (next = (IORegistryEntry *)kids->getNextObject())) next->detachAll( gIODTPlane);
```

`IODTFindMatchingEntries` (`IODeviceTreeSupport.cpp:886`) is where NULL stops meaning "nothing":

```c
        if( keys) { cmp = IODTMatchNubWithKeys( next, keys ); ... }
        else result->setObject( next);
```

so a NULL key list takes the else branch and collects *every* entry the iterator yields, and
`options` here is `0` - `kIODTExclusive` is not set, so there is no inversion either. The iterator is
`iterateOver(from, gIODTPlane, 0)`, non-recursive, so the victims are `rootEntry`'s direct IODT
children: the tree's top-level nodes, one of which is `chosen`. `detachAll(gIODTPlane)` removes each
from the plane, and after that `IORegistryEntry::fromPath("/chosen", gIODTPlane)` cannot find the
first component - which is exactly and only what 461 measured.

Apple's own subclasses answer with the few names that deserve deleting, as quoted names in the
OSUnserialize list grammar:

| file | answer |
| --- | --- |
| `iokit/Drivers/platform/drvAppleMacIO/AppleMacIO.cpp:108` | `"('sd', 'st', 'disk', 'tape', 'pram', 'rtc', 'mouse')"` |
| `iokit/Drivers/platform/drvApplePlatformExpert/ApplePlatformExpert.cpp:86` | `"('packages', 'psuedo-usb', 'psuedo-hid', 'multiboot', 'rtas')"` |

`"()"` is that answer with the set empty: `OSUnserialize.y:168` is `array: '(' ')' { $$ = NULL; }`, so
it parses to an empty `OSArray` - non-NULL, which is the only thing `IODTMatchNubWithKeys` tests - and
comparing each entry's name against nothing answers false. For `excludeList()` it is what NULL always
meant, since `kIODTExclusive` *is* set there: every entry still matches and every child is still
published. One spelling for both methods, and it is the one the grammar defines.

**This was a claim in a comment.** The two methods carried "This kernel loads no kexts and has none to
exclude, so both answers are the empty one" from experiment 363 to 461, and the claim was about the
*value* while the caller reads the *list*. `tools/check_platform_lists.py` (new) is the structural
form: it reads the two definitions and fails the build unless each answers a string in the
OSUnserialize name-list grammar, refusing NULL, the empty C string, and any non-literal - and refusing
a file where only one of the two is defined, because the stub generator would supply the other and the
link would succeed. `tools/build_xnu_arm_kernel.sh` runs it against `PLATFORM_SOURCES` itself rather
than a path repeated, so there is one definition of what was checked.

## The instrument: four readings of the walk, at two moments

461 left the failure at `fromPath`'s first component without saying *what changed* between the moment
its own probe worked and the moment the OS's calls did not. `entry_probe_walk` (`entry_trace.c`) reads
the four objects that first component passes through, by XNU's own functions - `getRegistryRoot`,
`getChildEntry`, `getChildSetReference`, `getChildCount` - plus the instrument's own
`fromPath(path, plane, 0, 0, 0)` with the OS's own path as the control. It is called at two moments:

| moment | where | why |
| --- | --- | --- |
| t1 | `entry_probe_plane`, from the `IODeviceTreeAlloc` wrapper | the tree is complete and the plane exists - the moment 461's successful probe was taken |
| t2 | the top of `__wrap_…fromPath`, **before** the real call | the state the OS's own call is about to see, with the same call as the control |

The three new mangled names are calls, not wrappers, for 461's reason (`getChildEntry` and
`getRegistryRoot` are virtual or static, and `--wrap` renames only undefined references - 455's
negative result). `build_entry.sh`'s undefined-set check now covers all seven names the instrument
calls, because a misspelling links as a generated stub and looks like a finding about the device.

`MSM8974PlatformExpert.cpp` also became the first stage-owned platform file with live records -
`entry_live_write`, the idiom `MSM8974RootResource.cpp` already carries - because it logged nothing
and "did `processTopLevel` run" was not a question the log could answer. Its `start` counts the
provider's IODT children on either side of `super::start` (which is where `configure` ->
`processTopLevel` runs, `IOPlatformExpert.cpp:184`, `:1269`), and `deleteList`/`excludeList` publish
their answer as a pointer *and* a length read from the bytes.

## The build

Both stages rebuilt in the canonical order (entry, then the payload that embeds it). The entry image's
`.text` grew 5097184 -> 5099072 bytes (+1888) and **nothing else moved**: image bytes 5307400
(unchanged - the edited `.text` ends 1016 bytes below `__bss_start`, which is this image's slack), and
`.bss` 614200 bytes to 0x805a5f38, `headroom` 1417416 bytes below `topOfKernelData`, both byte-identical
to 461's. The thirteen new globals are in the image at 0x80511154..0x805111f4 (`nm`, `.bss`), packed
right after 461's `g_dtpath_*` group. The payload is 8327168 bytes, md5
`e0cc79991502cebb2227233176296f60` - the same size and a different hash from 461's, which is right:
the payload's own sources are unchanged and the entry blob it embeds is the same length.

39 wraps, unchanged: **35 reached by a branch, 2 same-object-only, 1 never called, 1 by address, none
dead.**

**One build invocation to write down, because its failure names the wrong subject.**
`XNU_KERNEL_CONFIG=STAGE90_XNU tools/build_xnu_arm_kernel.sh --platform-only` exits 2 with

```
  DISAGREEMENT config_sched_traditional: the build script defines it =1, absent from this table
  DISAGREEMENT kpc: ...
  DISAGREEMENT mach_bsd: ...
```

and none of those three is about the configuration that was asked for. `device_table.py` builds its
`opts` set by running `tools/xnu_config/make_defines.sh` with `XNU_KERNEL_CONFIG` *and*
`XNU_MASTER_LOCAL`; with the fragment unset, `expand.sh` expands **RELEASE**, so the STAGE90_XNU table
is compared against RELEASE's `-D` list and the three names RELEASE defines and `STAGE90_XNU` does not
are reported as disagreements of *this* build. The correct invocation carries both:

```
XNU_KERNEL_CONFIG=STAGE90_XNU XNU_MASTER_LOCAL=$PWD/tools/xnu_config/boot/STAGE90_XNU.local \
    ./tools/build_xnu_arm_kernel.sh --platform-only
```

which is exit 0 and compiles the two platform `.cpp` files, the C table, and nothing else - the fast
path used here to pick up a platform-expert edit without rebuilding the pool. Recorded because a
check whose message is about a *different* configuration is worse than no check: the three lines read
as facts about `STAGE90_XNU`.

## The run

One `fastboot boot` through the two gates, log 447772 bytes, exit 0, device back on Android by itself.
The device is on Android 10 afterwards (`ro.build.version.release`).

### The platform expert, and the tree it did not detach

```
xnu_live_pexpert_seq=0x00000001
xnu_live_pexpert_prov=0xc04ad0a0            <- the root nub, the provider whose children the infanticide walks
xnu_live_pexpert_kids_before=0x00000015     <- 21 IODT children, the device tree's whole top level
xnu_live_pexpert_kids_after=0x00000015      <- and 21 after super::start, i.e. after processTopLevel
xnu_live_deletelist_seq=0x00000001  xnu_live_deletelist_ptr=0x8049a728  xnu_live_deletelist_len=0x00000002
xnu_live_excludelist_seq=0x00000001 xnu_live_excludelist_ptr=0x8049a72b xnu_live_excludelist_len=0x00000002
```

`len = 2` is `"()"`, read out of the image at the pointer the method returned - and the device tree's
root node has 21 children in the blob (`apple_dt_root_children=0x15`), so 21 is the number the
infanticide would have emptied. Both methods were asked **once each**, in this order (the live records
are appended in order): `…deletelist…` at line 4010 and `…excludelist…` at line 4063, between
`pexpert_kids_before` (3997) and `pexpert_kids_after` (4150).

**Why this is the cause and not a coincidence.** 461 measured the two ends: the instrument's own
`fromPath("/chosen/memory-map", gIODTPlane)` returned the node from inside the `IODeviceTreeAlloc`
wrapper (the plane as built), and both of the OS's own `fromPath` calls in `IOFindBSDRoot` returned 0
in the same boot - a boot in which the only later statement that touches the IODT plane is
`rootNub->registerService()`, the matching that follows, and `IOService::start` ->
`configure` -> `processTopLevel`. `IORegistryEntry::init(old, plane)` (called from
`initWithArgs`, `IOPlatformExpert.cpp:1557`) *moves* the tree's nodes to a new parent and deletes
nothing; `processTopLevel` deletes. 462 changes nothing else on that path - the instrument only
*reads* - and the tree is intact at the same two moments. The falsifier was named in advance and did
not fire: if `deleteList()` had not been the cause, the OS's calls would still return 0 here.

### The chain, and the mount

```
xnu_live_walk_seq=1  t1: walk_root=0xc04e0750 count=1 first=0xc04e1f78 set=0xc04e9520 kids=0x15 control=0(none asked)
xnu_live_walk_seq=2  t2: same root, count=1, first=0xc04ad0a0 (=the root nub), set=0xc04e9520, kids=0x15, control=0xc04e28c0
xnu_live_walk_seq=3  t2: same root, count=1, first=0xc04ad0a0,                set=0xc04e9520, kids=0x15, control=0xc04e2188
xnu_live_path_caller=0x802069f4  "/cho"/"sen\0"                plane=0xc04ab8e8 ret=0xc04e28c0   <- /chosen, now non-zero
xnu_live_path_caller=0x80206b6c  "/cho"/"sen/"/"memo"          plane=0xc04ab8e8 ret=0xc04e2188   <- /chosen/memory-map
xnu_live_prop_entry=0xc04e2188  _obj=0xc04fb8e0  _w0=0x80512000  _w1=0x00040000
xnu_live_mdevadd_caller=0x80206bd8  devid=0xffffffff base=0x00080512 size=0x00000040 phys=0 ret=0x01000000
xnu_live_mdevlookup_caller=0x80206dc0 devid=0x00000000 ret=0x01000000
```

and XNU's own console, captured, now ends with two lines 461's block does not have:

```
Added memory device md0/rmd0 (01000000/0B000000) at <ptr> for 0000000000040000
BSD root: md0, major 1, minor 0
```

Every number in that pair is the instrument's, read independently: `mdevadd`'s `base << 12` is
`0x80512 << 12 = 0x80512000` (the payload's 256 KB RAM disk, and `printf` prints it as `<ptr>` because
`osfmk/kern/printf.c:584` hides kernel-range values when `doprnt_hide_pointers` is set - XNU's own
behaviour, not a redaction on this side), `size << 12 = 0x40 << 12 = 0x40000`, the `(%08X/%08X)` pair is
the `0x01000000/0x0B000000` that `mdevadd` returned and that `mdevlookup(0)` returns, and
`BSD root: md0, major 1, minor 0` is `makedev(1,0)` = `0x01000000`. 459's frontier - `mdevadd` never
called, `mdevlookup(0)` = -1, `panic("IOFindBSDRoot: specified root memory device, %s, has not been
configured\n")` - is retired by measurement, not by inference.

`walk_seq=1` also answers a question 461's design raised: `first` is the *tree's root node*
(0xc04e1f78) at t1 and the *root nub* (0xc04ad0a0 = `xnu_live_pexpert_prov`) at t2, `set` is the same
child-set array (0xc04e9520) both times and `kids` is 21 both times. That is `IORegistryEntry::init(old,
plane)` moving the nodes and `attachToParent` putting the meta root above them - the two moments 461
could only bracket.

### The frontier: a wait whose deadline the watchdog beats

`IOSecureBSDRoot` is the first statement after `bsd_init`'s mount loop breaks (`bsd_init.c:966`), so
reaching it *is* the measurement that `vfs_mountroot()` returned 0 - and the boot reached it. Under
`#if CONFIG_EMBEDDED` it opens with:

```c
    matching = IOService::serviceMatching("IOPlatformExpert");
    pe = (IOPlatformExpert *) IOService::waitForMatchingService(matching, 30ULL * kSecondScale);
```

Both are in the log, in this order and nowhere else in the boot:

```
xnu_live_dict_site=0x80207550  _name=0x80480064  _out=0xc0510a40  _seq=4     <- IOSecureBSDRoot+0x30, "IOPlatformExpert"
xnu_live_iolock_site=0x801372c8                                             <- IOService::waitForMatchingService+0xe0
xnu_live_iolock_thr=0x8055a0f0  _lock=0xc04e07c8  _dl_lo=0x290a3371  _now=0x06b5237d
```

`_now` and `_dl` are GPT ticks, and the three iolock records *say so by arithmetic*: their
`_dl_lo - _now` are 575999982, 575999988 and 575999988, and 576000000 / 19.2e6 = 30.000 s exactly -
the 30 s of `30ULL * kSecondScale` at the GPT's own 19.2 MHz, which is also what fixes the ticks-to-
seconds scale used everywhere below. All three sleeps are in the same function on the same thread, and
the first is the IORTC wait
(`dict_seq=2`, `IOKitInitializeTime+0x28`), the second the IOBSD wait (`dict_seq=3`, `IOFindBSDRoot+0x2c`,
whose `reg` reading of the *resource service* has `IOResourceMatch` 2 entries with `IOBSD` **not** among
them - the wait's own predicate, unsatisfied), and the third this one, whose `reg` reading has 5 entries
with `IOBSD` at index 4. The
boot thread's last block record is the sleep itself (`block_seq=0x3c`, `enter=0x800152fc` =
`lck_mtx_sleep_deadline+0x8c`, `now=0x06b52513`), it never returns, and **nothing else is recorded on
that thread**. The last live record in the whole log is at `now=0x06b52ea8` = 5.86 s; the payload armed
its watchdog for 25 s (`hw_watchdog_timeout_s=0x19`) before the jump, and no timer in this image can
serve that deadline, so the device is reset ~19 s before the wait would have expired.

What is *not* measured is why the wait does not match. Two mechanisms are consistent with the source
and neither is ruled out: `copyExistingServices` with `IOProviderClass` set runs
`OSMetaClass::applyToInstancesOfClassName(sym, instanceMatch, &ctx)`, which looks the name up in the
metaclass registry and applies to that metaclass's `instances` set - a set a subclass's `addInstance`
propagates *up* to its superclass (`OSMetaClass.cpp:817-823`), so `MSM8974PlatformExpert` should be
inside `IOPlatformExpert`'s set and `metaCast` should hold; and the same function requires
`inState == (service->__state[0] & inState)` with `inState = kIOServiceMatchedState`, a bit this image
never reads. The next step's instrument is the one that separates them, and it is cheap: wrap
`waitForMatchingService` (an ordinary undefined reference from `IOKitBSDInit.cpp`, so `--wrap` reaches
it) and, when the dictionary's `IOProviderClass` is `IOPlatformExpert`, ask the wait's own predicate -
`copyExistingServices(dict, kIOServiceMatchedState, kIONotifyOnce)`, called by mangled name - and read
the platform expert's `__state[0]` from its own `start`.

### What the console capture reads

`xnu_live_ostext_chars = xnu_live_ostext_total = 0x3ee` (1006), `_heals = 1`, `_tank = 0x262` (610) -
so one kilobyte of text, nothing dropped, and 610 characters were offered before the live channel was
installed. 461's counter read 921. **The counter and the block are different measurements and this
step does not reconcile them**: the block's bytes between its `[os-console-459]` marker and the space
fill are 1033, the two added lines are 109 characters as the block prints them, and 921 + 109 is not
1006. The block's *content* is not in doubt - both new lines are complete and in order - so what is
recorded is that a byte counter and the bytes it counts part company by a few dozen characters, and
that 1006 is the number to quote for a character count.

**And the report did not run at all.** This run reached neither `entry_epilogue` nor `fleh_undef`, so
not one of the report's own keys is in the log and every reading above is a *live* record. 455 said it
first and this run is the second demonstration: a boot that hangs at the frontier never prints its
report, and from here on a step's readings belong on the live channel first.

## Next: 463, the wait's own predicate

1. `--wrap` `IOService::waitForMatchingService`: the caller, the dictionary, the timeout and the
   return - so "which waits happened, and which one came back empty" is a record rather than a
   reading of the tick arithmetic.
2. Inside it, when the dictionary's `IOProviderClass` is the `IOPlatformExpert` symbol: call
   `copyExistingServices(dict, kIOServiceMatchedState, kIONotifyOnce)` by mangled name and record its
   answer - the wait's own predicate, which separates "the class test fails" from "the candidate is
   filtered by its state".
3. The platform expert's own `__state[0]` at the end of its `start`, and the number of *instances* of
   `IOPlatformExpert` the metaclass registry holds (`OSMetaClass::getMetaClassWithName` +
   `applyToInstances`' count, or the simplest proxy: `copyExistingServices` with `inState` = 0).
4. Still owed from 461: 448's `_bad` slots are inverted for three of their four functions
   (`IORecursiveLockAlloc` 0xc060b798, `IOSimpleLockAlloc` 0xc05caae0, `IOCommandGate::commandGate`
   0xc05d6708 hold allocation *pointers*) - the fix is a pair of keys that names each one's sense in
   the report rather than in a comment.
