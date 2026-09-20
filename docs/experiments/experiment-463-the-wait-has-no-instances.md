# Experiment 463 — the wait has no instances

**Status: on the device, two runs, 450561 bytes of log, exit 0, device back on Android by itself.
One new frontier, and it is a named mechanism rather than a bracket: `IOSecureBSDRoot`'s wait for an
`IOPlatformExpert` resolves its name to the metaclass `0x8058b4f0` and then finds **no instances at
all**, so no `inState` and no `options` can ever match and the boot thread sleeps until the payload's
watchdog resets the device, ~21 s before the wait's own deadline. The instrument is `--wrap`ped on
`IOService::waitForMatchingService` and asks the wait's own three questions at each call - the
caller, the dictionary and the return; `copyExistingServices` with the wait's own arguments; and the
instance set the dictionary's `IOProviderClass` names, walked with XNU's own
`applyToInstancesOfClassName` and an applier that is `instanceMatch` with its filters removed.**

**The control is in the same log.** The boot makes two `waitForMatchingService` calls, and the first
one - `IOFindBSDRoot`'s, for a service that has matched the `IOBSD` resource - **returns**
`0xc04bcd10`, which is `IOService::getResourceService()`'s own answer, i.e. the resource root. So
"the wait did not return" in 462 was the second wait and only the second, and the instrument's
records can read a success.

**The other two mechanisms 462 left open are retired by measurement, and the reason is `seen = 0`:
the metaclass exists and its instance set is empty.** 462 named (a) `instanceMatch`'s
`inState == (__state[0] & inState)` test and (b) the `matchInternal` clause, both of which are
*filters* - and a filter cannot be the cause when the set it filters was never entered: 463's applier
**is** `instanceMatch` with the filters removed (same walk, same `OSMetaClass` walk order), and it
counted zero instances of `IOPlatformExpert`. `OSMetaClass::addInstance` (`OSMetaClass.cpp:817`) has
exactly one caller, `IOService::registerService` (`IOService.cpp:3694`, which links a subclass's set
into its superclass's), and `MSM8974PlatformExpert::start`'s last two records say this object's
`__state[0]` is `0` - so **nothing registers an `IOPlatformExpert` instance in this tree**, and the
registration is the next step's object.

## The two waits, from the source

```
IOFindBSDRoot       IOKitBSDInit.cpp:349   matching = serviceMatching(gIOResourcesKey);
                   :377                    matching->setObject(gIOResourceMatchedKey, gIOBSDKey);
                   :379                    service = waitForMatchingService(matching, 30 s);
IOSecureBSDRoot     IOKitBSDInit.cpp:664   matching = serviceMatching("IOPlatformExpert");
                   :673                    pe = waitForMatchingService(matching, 30 s);
```

Two dictionaries, two questions, both with a 30 s timeout, both reached in this order by the same
thread - `IOSecureBSDRoot` is the first statement after `bsd_init`'s mount loop breaks
(`bsd_init.c:966`), so *reaching* it is the measurement that `vfs_mountroot()` returned 0.

And one function, so one instrument covers both (`IOService.cpp:4674`):

```c
    LOCKWRITENOTIFY();
    do
    {
        result = (IOService *) copyExistingServices( matching,
                            kIOServiceMatchedState, kIONotifyOnce );
	if (result) break;
        notify = IOService::setNotification( gIOMatchedNotification, matching, ... &result ... );
	if (!notify) break;
	SLEEPNOTIFYTO(&result, deadline);          /* the sleep the iolock records see */
    }
    while( false );
```

`do { } while (false)`: one query, then one sleep, and the answer after the sleep is whatever a
notification handler wrote into `&result` (`syncNotificationHandler`, `:4610`) - which is why the
first call's answer and the wait's answer are *different questions* and 463 records both.

## The instrument: three readings per call, and a control

`__wrap__...waitForMatchingService` records `ent = 0` before the real call and `ent = 1` after it,
with the caller (`lr`, so the `bl` is at `caller - 4`), the dictionary, the timeout and the return.
Two records and not one, for 454's reason: a boot that never returns from the wait still says so -
the last record in this run for that call is an `ent = 0`.

Between the two, when the dictionary's `IOProviderClass` is `IOResources` or `IOPlatformExpert`,
`entry_probe_wait` reads the wait's own predicate through XNU's own functions, called by mangled
name (virtual and static members cannot be `--wrap`ped, 455):

| reading | call | what it answers |
| --- | --- | --- |
| `wsvc_sym` | `OSDictionary::getObject("IOProviderClass")` | the name, as the dictionary holds it |
| `wsvc_p4` | `copyExistingServices(dict, kIOServiceMatchedState, kIONotifyOnce)` | **the wait's own first call**, same three arguments |
| `wsvc_p0` | `copyExistingServices(dict, 0, kIONotifyOnce)` | the same walk with the state filter off |
| `wcls_meta` | `OSMetaClass::getMetaClassWithName(sym)` | whether the name resolves at all |
| `wcls_name0/1` | `OSMetaClass::getClassName(meta)` | the metaclass's own name, 8 bytes, stopping at the terminator |
| `winst_inst/state` | `applyToInstancesOfClassName(sym, applier, 0)` | **the set, and each instance's `__state[0]`** |
| `wcls_seen/shown` | the same walk | how many instances, and how many were recorded (first 4) |

The applier is the whole argument, and it is not an analogy:

```c
	if ((str = OSDynamicCast(OSString, obj))) {
	    const OSSymbol * sym = OSSymbol::withString(str);
	    OSMetaClass::applyToInstancesOfClassName(sym, instanceMatch, &ctx);   /* IOService.cpp:4307 */
```

is what `copyExistingServices` does with a non-`IOResources` `IOProviderClass`. 463 calls the *same
function with the same argument* and puts `entry_wcls_applier` where `instanceMatch` sits, so
`wcls_seen = 0` means `instanceMatch` would be entered zero times: not "the state test failed" and
not "`matchInternal` said no", but "there is nothing to test". `entry_str8` reads the class name two
words at a time and stops at the terminator, because an interned symbol's string is not a sized
buffer - without those two words the log could name a metaclass pointer and not a class.

The platform expert's own file becomes a recorder too (`MSM8974PlatformExpert.cpp`, the idiom 462
added there): at the end of `start`, `entry_live_write("xnu_live_pexpert_self", this)` and
`…_pexpert_state`, `this->getState()`. The comment in the source names why *that* bit and not the
matched bit: `kIOServiceMatchedState` is written by `copyNotifiers( gIOMatchedNotification,
kIOServiceMatchedState, … )` **after** `start` returns, so bit 2 is expected to be clear even on a
boot that succeeds; `kIOServiceRegisteredState` is set inside `registerService` in the same two
statements as `getMetaClass()->addInstance(this)`, so **bit 1 clear here is the third candidate 462
did not name** — and it is the one the run measured.

### One build input that is not compiled by the script that links it

463's first run had no `xnu_live_pexpert_self` at all, and read as "the end of `start` was never
reached". It was not: `entry_live_write` goes to the live console, the edit had simply never been
compiled. `build_entry.sh` **links** `out/xnu_platform_obj/MSM8974PlatformExpert.o`, which is
compiled by `tools/build_xnu_arm_kernel.sh` - a different script with its own two required
environment variables - and 463's first build ran only the entry script. The run's own addresses say
so in hindsight: in run a `IOSecureBSDRoot+0x30` is `0x802079f0` and in run b it is `0x80207a50`, the
0x60 by which the platform object grew (24060 -> 24252 bytes) and with it everything linked after it.

The fix is structural, not a note. **`platform_obj_fresh()`** in `build_entry.sh` checks that each of
the three `out/xnu_platform_obj/*.o` inputs is newer than the sources that describe them, and exits 2
with the exact rebuild command in the message:

```
  FAIL: out/xnu_platform_obj/MSM8974PlatformExpert.o is older than stages/stage90/xnu_platform/MSM8974PlatformExpert.cpp
  the platform objects are compiled by tools/build_xnu_arm_kernel.sh, not by this script:
    XNU_KERNEL_CONFIG=STAGE90_XNU XNU_MASTER_LOCAL=$PWD/tools/xnu_config/boot/STAGE90_XNU.local \
        ./tools/build_xnu_arm_kernel.sh --platform-only
```

It was tested in both directions (touch the source -> exit 2 with that message; rebuild -> exit 0),
and its own message states what it does *not* cover: the two `xnu_supply/*.c` sources and the
generated `stage90_pseudo_inits.o`.

## The build

Both stages rebuilt in the canonical order (entry, then the payload that embeds it). The entry
image's `.text` grew 5099072 -> 5102144 bytes (+3072) and **nothing else moved**: image bytes
5307400 (unchanged), `.bss` 0x80510000..0x805a5f38 (614200 bytes) and `headroom` 1417416 bytes below
`topOfKernelData`, both byte-identical to 462's - the new globals are in `.bss` at
0x805111f8..0x80511268, packed after 462's group. The payload is 8327168 bytes, md5
`0913ed254e4c933019beeff2fcb12b7e`.

40 wraps: **37 reached by a branch, 1 same-object-only** (`_ZN9IOService12matchPassiveEP12OSDictionaryj`),
**1 never called here** (`sleep`), **1 by address only** (`vcputc`), none dead. Pass 1 is unchanged
at 27 undefined. Two of 463's edits changed the wrap accounting: `copyExistingServices` left
`same_object` (the instrument's own call makes the symbol undefined, so it is reachable now - it had
been exempt, hence unchecked, since 455), and six new mangled names joined the undefined-set check
because a misspelled name links as a generated stub and reads as a finding about the device.

**The link that booted is the tree that is committed.** After the run, `build_entry.sh` was re-run
from the working tree and its `xnu_arm_entry.bin` (md5 `96661f098cb86cedecc09cad7c865657`) is
**byte-identical** to the blob the device booted - `cmp` clean - and the build printed
`xnu_entry_463: the three out/xnu_platform_obj inputs this script links are newer than the sources
that describe them`. Run b's records resolve to the expected symbols against that ELF
(`IOFindBSDRoot+0x6c`, `IOSecureBSDRoot+0x30`/`+0x44`, `waitForMatchingService+0xe0`,
`doServiceMatch+0x3c`), which is the check that the layout in the log is the layout that was built.

## The run

One `fastboot boot` through the two gates, log 450561 bytes, exit 0, device back on Android by itself.
The console block ends with the two lines 462's frontier was measured at - `Added memory device
md0/rmd0 (01000000/0B000000) at <ptr> for 0000000000040000` and `BSD root: md0, major 1, minor 0` -
and contains neither `cannot mount root` nor `mount(…) failed`, so the mount loop broke on
`vfs_mountroot() == 0`: the root filesystem is this payload's own **mockfs** image in its 256 KB RAM
disk (the build's own check reads `vfstbllist` and says so).

### The first wait returns, and the boot's own records say why

```
xnu_live_wmatch_seq=0x1 ent=0x0 caller=0x80206e0c dict=0xc04eb600 to=0x6fc23ac00 ret=0x0
xnu_live_wsvc_seq=0x1 dict=0xc04eb600 sym=0xc0490430 p4=0x0 p0=0x0
xnu_live_wcls_seq=0x1 sym=0xc0490430 meta=0x8058ae18 rsvc=0xc04bcd10 name0=0x65524f49 name1=0x72756f73
xnu_live_winst_seq=0x0 inst=0xc04bcd10 state=0x0000001e
xnu_live_wcls_seen=0x1 shown=0x1
xnu_live_wmatch_seq=0x2 ent=0x1 caller=0x80206e0c dict=0xc04eb600 to=0x6fc23ac00 ret=0xc04bcd10
```

`to = 0x6fc23ac00` is 30 000 000 000 ns, the `30ULL * kSecondScale` of the source. `sym` is not a
literal: `IOFindBSDRoot` passes `gIOResourcesKey`, an `OSSymbol` on the heap, so `0xc0490430` is the
dictionary's own `IOProviderClass` object - and the walk's `name0/name1` are the bytes
`I O R e` / `s o u r`, i.e. **"IOResources"**, the class the dictionary names. `rsvc` and `inst` are
the same pointer, so the instance the walk found *is* `getResourceService()`'s answer, and
`state = 0x1e` has bits 1, 2, 3 and 4 set - registered, published, matched.

**And the wait's own first call returned 0.** `p4` is `copyExistingServices(dict,
kIOServiceMatchedState, kIONotifyOnce)` on the same dictionary the wait was handed, so the wait's own
first call cannot have returned anything else; the answer it *did* return came through the sleep.
That is not a defect in the wait - `IOFindBSDRoot`'s dictionary asks for a service that has matched
the `IOBSD` resource, and at that moment the resource root's `IOResourceMatched` array holds two
entries and `IOBSD` is not one of them (`reg_seq=2`, `count=0x2`, `idx=0xffffffff`).

The log then says what changed, in three records:

```
xnu_live_pub2_key=0x8047b10f                    <- "IOBSD", IOService::publishResource, from IOKitBSDInit()
xnu_live_finddrv_seq=0x2 site=0x801359d0 svc=0xc04bcd10 set=0xc04c6990 count=0x1
xnu_live_reg_seq=0x3 state0=0x1e state1=0x1 rm=0xc04eb5e0 count=0x5 idx=0x4
```

The first is `IOKitBSDInit()` (`IOKitBSDInit.cpp:95: IOService::publishResource("IOBSD")`), which
`setconf` calls on its way into `IOFindBSDRoot`. The second is `IOCatalogue::findDrivers` called from `IOService::doServiceMatch+0x3c`, on the
**resource root**, and `registerService` is its only caller (`IOService.cpp:3690`); its wrapper
records *after* the call, so its position is when the call returned. Two statements above its
`return`, `registerService` ends

```c
        if (resourceKeys) setProperty(gIOResourceMatchedKey, resourceKeys);          /* :3751 */
        notifiers[0] = copyNotifiers(gIOMatchedNotification,
                                     kIOServiceMatchedState, 0xffffffff);           /* :3754 */
```

- so a registration of the resource root is exactly a service that sets `IOResourceMatched` **and**
delivers the matched notification the sleeping wait is registered for. The next reading is the
`IOResourceMatched` array itself: **five entries with `gIOBSDKey` at index 4** by the time of the
second wait, against two and no `IOBSD` at the first. `IOService::publishResource`'s tail is
`gIOResources->setProperty(key, value); … gIOResources->registerService()` (`:3532-3544`), and the
`pub2` record above is that publish - so the boot's own reading of the resource root is consistent
and complete: **the wait for `IOBSD` was satisfied by the `IOBSD` resource being published and the
resource root being registered with it.** What is *not* measured is the thread that performed the
registration and its timing relative to the publish's own record; see "What is not measured" below.

### The second wait, and the frontier

```
xnu_live_dict_site=0x80207a50 _name=0x80480c0c _out=0xc04eba00     <- IOSecureBSDRoot+0x30, "IOPlatformExpert"
xnu_live_wmatch_seq=0x3 ent=0x0 caller=0x80207a64 dict=0xc04eba00 to=0x6fc23ac00 ret=0x0
xnu_live_wsvc_seq=0x2 sym=0xc04ce8a0 p4=0x0 p0=0x0
xnu_live_wcls_seq=0x2 sym=0xc04ce8a0 meta=0x8058b4f0 rsvc=0xc04bcd10 name0=0x6c504f49 name1=0x6f667461
xnu_live_wcls_seen=0x0 shown=0x0
```

`name0/name1` are `I O P l` / `a t f o` - **"IOPlatformExpert"** - so `getMetaClassWithName`
resolves the name the dictionary carries (`0xc04ce8a0`, the interned symbol; the literal
`serviceMatching` was called with is `0x80480c0c`, and the two differ, which is a reading of the
dictionary rather than of the argument) and the class is really there.

Then the walk finds nothing: **`wcls_seen = 0`**, and `ent = 1` never appears - the run's last record
for that call is the `ent = 0` above. Both `inState` readings are 0 as well, which is the same fact
twice: with an empty set, the state filter and the `matchInternal` clause are unreachable code.

```
xnu_live_pexpert_self=0xc04bf800
xnu_live_pexpert_state=0x00000000
```

is the third reading, and it is the one that names the cause: the object exists (its `start` ran to
its own last statement, one line above) and its `__state[0]` is **0** - no `kIOServiceRegisteredState`
and no `kIOServiceFirstPublishState`, i.e. `registerService` has never run on it, and
`getMetaClass()->addInstance(this)` sets bit 0 of that same word in the statement before the
registration bit. An `IOPlatformExpert` with a metaclass and no instances is not a naming problem,
not a state problem and not a matching problem; it is an object that was never published.

**That record is a moment, and the reading that spans the rest of the boot is the walk.** `state = 0`
is read at the end of `start`; a registration performed *after* `start` returned would not show
there. `wcls_seen = 0` is read five milliseconds later, at the second wait - after `start` returned,
after `probeCandidates`, after the mount - and it is the same answer, which is what makes "nothing
registers it" a measured claim rather than an inference from one moment. The path that creates the
object registers nothing either, on the source's evidence: `IOService::startCandidate`
(`:3479-3517`) attaches the candidate, checks its resources and calls `start`, and returns - the
platform experts that do register are Apple's own, and this tree has none.

### Three sleeps, one deadline, and the watchdog

```
xnu_live_iolock_site=0x801377c8 lock=0xc04bb780 event=0xc2013e34 dl-now=575999988 now=4.0597 s   <- IOKitInitializeTime's IORTC wait
xnu_live_iolock_site=0x801377c8 lock=0xc04bb780 event=0xc2013dd4 dl-now=575999988 now=4.0725 s   <- IOFindBSDRoot's IOBSD wait
xnu_live_iolock_site=0x801377c8 lock=0xc04bb780 event=0xc2013dfc dl-now=575999988 now=4.0736 s   <- IOSecureBSDRoot's IOPlatformExpert wait
```

All three sites are `IOService::waitForMatchingService+0xe0` (the `IOLockSleepDeadline` the
`SLEEPNOTIFYTO` expands to), all three locks are `gNotificationLock`, and all three
`dl_lo - now = 575999988` - 30.000 s minus 12 ticks of the 19.2 MHz GPT. **An address is only
meaningful against the build that produced the log**: this same call site is `0x801372c8` in 462's
build and `0x801377c8` here, and `IOSecureBSDRoot+0x30` is `0x80207550` there against `0x80207a50`
here - XNU's own objects moved 0x500 because the entry image's objects before them grew, which is
the defect 179 records: an address from one log resolved against another build's ELF lands in a
neighbouring function and reads as a call site that does not exist.
**The first wait is not in
the `wmatch` records and the log says why**: it is `IOKitInitializeTime`'s
`waitForService( resourceMatching("IORTC"), &t )`, and `IOService::waitForService` calls
`waitForMatchingService` **from inside the same object**, where `--wrap` cannot reach (455) - the
sleep is recorded (the iolock wrapper is on a libkern function) and the call is not.

The two waits' sleeps are 20773 ticks (1.081 ms) apart, so the whole of `IOFindBSDRoot` after the
first wait - `mdevadd`, the two console lines, `mdevlookup` returning `0x01000000`, the mount, and
`bsd_init` up to `IOSecureBSDRoot` - fits inside about a millisecond of GPT. The second wait's
deadline falls at 34.074 s; the payload armed its watchdog for 25 s before the jump, and no timer in
this image can serve a deadline, so the device is reset about 21 s before the wait would have
expired. The last live record is at 4.0737 s - the boot thread's own `block` right after the sleep
above - and the log ends `No errors detected`.

### The console block's position is not its time

The captured console text (`[os-console-459]`, `entry_stubs.c`'s 458 instrument) is a **reserved
region**, not a stream: on the first character after the live channel is installed,
`entry_os_reserve()` reads the console's size field, adds `ENTRY_OS_BLOCK` (0x20000) to it, fills the
whole block with spaces, and then appends text *inside* that block while the live records append
*above* it. So the block's position in the log is the moment it was **reserved**, while its content
keeps growing for the rest of the boot - which is why `Added memory device md0…` and `BSD root: md0…`,
printed after the first wait and after the mount, sit at log lines 4183-4184, *before* that wait's
records. Its content is evidence; its position is not evidence about when the text was printed.

### The record order is a total order over all threads, not one thread's sequence

The records carry no thread id unless their own fields do (`wmatch_caller`, `dict_site`,
`maxcpus_caller`, `block_thr`, …), and 463's readings include two whose interleaving shows more than
one writer:

* the platform expert's `start` records bracket the boot thread's own `bsd_init` path -
  `xnu_live_pexpert_kids_after` (the line after `super::start`) is at 4150 and
  `xnu_live_pexpert_self` (the line after `ml_init_max_cpus(1)`) at 4378, while
  `xnu_live_dict_site=0x8011eb94` (`IOKitInitializeTime+0x28`, whose only caller is `bsd_init:728`)
  and its sleep are at 4189-4225, *between* them;
* `ml_get_max_cpus`'s callers are four different sites (`commpage_populate+0x70` at 3972,
  `mcache_init+0x20`, `mbinit+0x684`, `sysctl_mib_init+0xc0`), and the first of them **blocks** until
  `ml_init_max_cpus(1)` sets the flag - which is the platform expert's `start`, whose records are at
  4378-4379. The thread that called it at 3972 resumed after 4379; the thread that set the flag was
  not that thread.

The structure the source gives for this is that IOKit's start runs from `StartIOKit`
(`pexpert/arm/pe_init.c:279`, called by `PE_init`) while `bsd_init` runs in
`kernel_bootstrap_thread`'s ladder, so on this image the platform expert's `start` and
`IOFindBSDRoot` are on different threads and their records interleave. **Every reading above is a
pair taken inside one call or one wrapper** - `ent = 0`/`ent = 1` around the real call, `wsvc`/`wcls`
inside the probe, `dl_lo`/`now` read in the sleep's own entry - so none of them depends on attributing
a neighbour record to a thread. A reading that did would be a defect (see 179).

## What is not measured

1. **Which thread registers the resource root, and when relative to its publish.** The log gives the
   fact (a registration of `0xc04bcd10` returning between the sleeping wait's entry and its wake, and
   the `IOResourceMatched` array growing to include `IOBSD` between the two waits) and the source
   gives the only candidate (`publishResource`'s tail). The *time* between the `pub2` record and the
   `finddrv` record is not the time between the publish and the registration: `findDrivers`' wrapper
   records after the call, and the call is what the wait depends on.
2. **The metaclass's `reserved->instances` being NULL rather than empty.** `seen = 0` and the
   platform expert's bit 1 = 0 both say no instance was registered; whether the set exists with zero
   members or does not exist is not distinguished by this instrument (both make
   `applyToInstancesOfClassName` return early).
3. **Whether `matchInternal` would pass for this instance.** With an empty set the filter is never
   consulted, so 463 cannot say that a registered `MSM8974PlatformExpert` would satisfy
   `serviceMatching("IOPlatformExpert")` - only that nothing else can. That is a prediction 464 has
   to test, and the mechanism it relies on is `addInstance` propagating a subclass's set up to its
   superclass (`OSMetaClass.cpp:817-823`).
4. Still owed from 461: 448's inverted `_bad` slots (three of four hold allocation *pointers*:
   `IORecursiveLockAlloc` 0xc060b798, `IOSimpleLockAlloc` 0xc05caae0, `IOCommandGate::commandGate`
   0xc05d6708; `kernel_thread_start`'s is a `kern_return_t`) as a pair of keys naming each one's
   sense.

## Next: 464, the registration

1. **The object of the step is the missing `registerService`, not another reading.** The three
   candidates are `MSM8974PlatformExpert::start`'s own tail (before its `return true`), whatever code
   instantiates the class - the object is named by `IOClass = MSM8974PlatformExpert`
   (`stage90_platform_config_tables.c:75`, and the device tree carries the same property), and the
   run records two `allocClassWithName` calls just before the object's `start`
   (`xnu_live_alloc_name`, heap `OSSymbol`s whose *text* the record does not carry, so the two are
   pointers and not names) - and Apple's own platform path, which this tree does not contain. Only
   the first is this project's file, so it is where the statement goes.
2. **Predicted records, written before the build**: `xnu_live_wcls_seen = 1` for
   `IOPlatformExpert`, with `winst_inst = 0xc04bf800` (this platform expert) and
   `winst_state = 0x6` (registered) or `0x7` at the second wait, and `xnu_live_wmatch_seq`'s `ent = 1`
   with a non-zero `ret` - three readings, each of which independently fails if the registration is
   the wrong one.
3. **The falsifier is the third wait's sleep**: if the registration happens but
   `copyExistingServices` still answers 0 (a `matchInternal` class test that does not accept the
   subclass, or `addInstance`'s propagation not reaching this set), the instrument says so by
   recording `wcls_seen = 1` with `wsvc_p4 = 0` - the two mechanisms 462 named, now with the empty-set
   explanation removed and a real instance to test them on.
4. **The step after that is the boot's next statement**, whatever it is: `pe->callPlatformFunction(
   "SecureRootName", …)` on the returned object, then `bsd_init`'s `VFS_ROOT`/`filedesc0` and the
   ladder on. The concurrency finding above matters for it: a *timer* is still absent, so anything
   that needs a deadline to fire is still unreachable, and only paths with a real call site between
   them can be measured this way.
