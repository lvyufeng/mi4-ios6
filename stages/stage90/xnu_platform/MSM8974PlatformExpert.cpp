/*
 * The stage-owned platform expert (experiment 363).
 *
 * `iokit/Kernel/IOStartIOKit.cpp:155` is `rootNub = new IOPlatformExpertDevice;` and `:165` is
 * `rootNub->registerService()`, which matches the root nub against the kernel's catalogue of
 * personalities. The open-source tree's catalogue has exactly one entry -
 * `iokit/KernelConfigTables.cpp:35`, `'IOClass' = IOPanicPlatform; 'IOProviderClass' =
 * IOPlatformExpertDevice; 'IOProbeScore' = 0:32` - and `IOPanicPlatform::start`
 * (`IOPlatformExpert.cpp:1725-1733`) is Apple's designed fallback, whose class comment is "If no
 * legitimate IOPlatformDevice matches, this one does and panics the kernel with a suitable
 * message". Experiment 362's run is that fallback firing: `Unable to find driver for this
 * platform: "..."`, trapped at `DebuggerTrapWithState`'s `udf`.
 *
 * The tree contains no concrete platform expert to put in front of the fallback, and both
 * candidates were read rather than guessed:
 *
 *   - `IODTPlatformExpert` is Apple's whole device-tree platform expert - `probe`, `configure`,
 *     `createNub`, `createNubs`, `processTopLevel`, `getModelName`, `getMachineName`,
 *     `getNubResources`, `haltRestart`, all of it real in this image since experiment 355 - but
 *     its metaclass is abstract: `IOPlatformExpert.cpp:1243` is
 *     `OSDefineMetaClassAndAbstractStructors(IODTPlatformExpert, IOPlatformExpert)`, so
 *     `OSMetaClass::allocClassWithName` (which is how matching instantiates a driver,
 *     `IOService.cpp:3296-3301`) refuses it.
 *   - `ApplePlatformExpert` (`iokit/IOKit/platform/ApplePlatformExpert.h:61`) is
 *     `OSDeclareAbstractStructors` as well, and has no implementation in the tree at all
 *     (`iokit/Kernel/` holds only `IOPlatformExpert.cpp`).
 *
 * So this class is the missing piece and nothing else: a concrete subclass of `IODTPlatformExpert`
 * whose entire content is what the abstraction barrier requires - the metaclass, and the two pure
 * virtuals `IODTPlatformExpert` leaves for its subclasses (`deleteList` and `excludeList`,
 * `IOPlatformExpert.h:229-230`). Everything the machine then does is Apple's own code, already in
 * the link. **Both of those virtuals were wrong until 462** - they answered NULL, which in
 * `IODTFindMatchingEntries` means "every entry" rather than "no names" and made `processTopLevel`
 * detach the device tree's top-level nodes from the IODT plane; the section above the two
 * definitions carries the reading, and `start` below carries the count that measures it.
 *
 * `start` is overridden to call `super::start` and for one structural reason besides: this walk's
 * rule 358 is that an object which owns a vtable is not accounted for by its reference list, and a
 * class with no virtual of its own gets no vtable emitted in its translation unit under the
 * standard key-function rule. A vtable that nothing defines would arrive in the link as a
 * *storage stand-in*, and the first virtual dispatch through it would be a jump through zero.
 * Defining the three virtuals here is what gives `_ZTV22MSM8974PlatformExpert` a home.
 *
 * `start` is deliberately the whole of the behaviour: `IOPlatformExpert::start`
 * (`IOPlatformExpert.cpp:108-185`) is real and already linked, and it is where `gIOPlatform = this`
 * is set, where the interrupt-controller dictionary, the physical range allocator and the power
 * domains are created, and where the provider's `serial-number` is published - then it ends in
 * `configure(provider)`, which for this class is `IODTPlatformExpert::configure` and the device-tree
 * nub plane.
 *
 * **One requirement this class puts on its personality, and it is not optional.**
 * `IODTPlatformExpert::probe` (`IOPlatformExpert.cpp:1256-1268`) is three lines: chain to
 * `super::probe`, which is `IOService::probe` and returns `this` (`IOService.cpp:507-511`), then
 *
 *     if( !provider->compareNames( getProperty( gIONameMatchKey ) )) return( 0 );
 *
 * So the driver matches only if its own `IONameMatch` property names the provider. With no such
 * property `getProperty` returns `0`, `IORegistryEntry::compareNames(0)` falls through both casts to
 * a null `string` and returns **false** (`IORegistryEntry.cpp:903-928`), and the probe fails - which
 * would put the machine back on Apple's fallback and the panic experiment 362 measured. The
 * personality in `stage90_platform_config_tables.c` therefore carries
 * `'IONameMatch' = "qcom,msm8974-xnu-stage90"`, and that value is the root node's `compatible` string
 * as this machine's device tree writes it (`stage90_main.c:80`); the comparison is
 * `IOPlatformExpertDevice::compareName` -> `IODTCompareNubName` -> `CompareKey` over the node's
 * `name`, `compatible`, `device_type` and `model` (`IODeviceTreeSupport.cpp:799-865`), so an exact
 * match on any of those four is a match. The root node's `name` is `"/"` and its `model` is
 * `"Xiaomi Mi 4 cancro Stage84"` (`stage90_main.c:79-82`) - the other two candidates.
 */
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <arm/machine_routines.h>

/* The entry instrument's own entry point, declared here rather than included from it because this
 * file is compiled by the tree's own build (`PLATFORM_SOURCES` in `tools/build_xnu_arm_kernel.sh`),
 * the same way `MSM8974RootResource.cpp` declares it for the same reason. The symbol exists in every
 * configuration this project builds (the canonical entry build sets `STAGE90_ENTRY_TRACE=1`); a
 * build that left it out would be an undefined symbol and a loud link failure, not a silent zero. */
extern "C" void entry_live_write(const char *key, uint32_t value);

class MSM8974PlatformExpert : public IODTPlatformExpert
{
    OSDeclareDefaultStructors(MSM8974PlatformExpert);

public:
    virtual bool         start( IOService * provider ) APPLE_KEXT_OVERRIDE;
    virtual const char * deleteList( void ) APPLE_KEXT_OVERRIDE;
    virtual const char * excludeList( void ) APPLE_KEXT_OVERRIDE;
};

/* `super` is a per-file macro in this tree rather than a keyword: `IOPlatformExpert.cpp:72` is
 * `#define super IOService` and its own `IODTPlatformExpert` block redefines it at `:1241`
 * (`#define super IOPlatformExpert`). Without this line `super::start(provider)` is
 * "use of undeclared identifier 'super'", which is how the first compile of this file failed. */
#define super IODTPlatformExpert

OSDefineMetaClassAndStructors(MSM8974PlatformExpert, IODTPlatformExpert);

static uint32_t g_pexpert_starts;
static uint32_t g_pexpert_registers;
static uint32_t g_delete_list_calls;
static uint32_t g_exclude_list_calls;

/* `deleteList`/`excludeList` are asked for a C string in the OSUnserialize list grammar, and this
 * is its length - read from the string itself, one byte at a time and bounded, so that the record
 * says what was *answered* rather than what this file meant to answer. `strlen` would do the same
 * thing by name; a loop cannot be the wrong function. */
static uint32_t
MSM8974_list_len( const char * list )
{
    uint32_t n = 0;

    if( list != 0 ) {
        while( n < 64u && list[n] != '\0' ) n++;
    }
    return n;
}

bool
MSM8974PlatformExpert::start( IOService * provider )
{
    uint32_t kids_before = 0u;
    uint32_t kids_after  = 0u;
    bool     ok;

    /* Experiment 462. `processTopLevel` runs inside `super::start` (`IOPlatformExpert::start` ends
     * in `configure(provider)`, `IOPlatformExpert.cpp:184`, which for this class is
     * `IODTPlatformExpert::configure` -> `processTopLevel`, `:1269-1276`), and its first statement
     * detaches `IODTFindMatchingEntries(provider, 0, deleteList())` from the IODT plane. `provider`
     * is the root nub, and in the IODT plane its children are the device tree's top-level nodes -
     * `IORegistryEntry::init(old, plane)` moves them there from the tree's own root when
     * `IOPlatformExpertDevice::initWithArgs` initializes from it (`IORegistryEntry.cpp:356-380`,
     * `IOPlatformExpert.cpp:1557-1575`), which is why the walk to `/chosen` goes through it.
     *
     * So the count on either side of `super::start` is the infanticide measured rather than argued:
     * non-zero before and the same value after is a plane that kept its nodes, and zero after is the
     * `(const char *)0` this file answered with until 462. `_prov` is recorded beside it because the
     * tracer's own walk reading carries the pointer its walk starts from - the two are the same
     * object if the reading above is of the right entry, which is a check rather than a comment. */
    g_pexpert_starts++;
    if( provider != 0 && gIODTPlane != 0 )
        kids_before = provider->getChildCount( gIODTPlane );
    entry_live_write( "xnu_live_pexpert_seq", g_pexpert_starts );
    entry_live_write( "xnu_live_pexpert_prov", (uint32_t)(uintptr_t) provider );
    entry_live_write( "xnu_live_pexpert_kids_before", kids_before );

    ok = super::start( provider );

    if( provider != 0 && gIODTPlane != 0 )
        kids_after = provider->getChildCount( gIODTPlane );
    entry_live_write( "xnu_live_pexpert_kids_after", kids_after );

    if( !ok ) return( false );

    /* Experiment 420. `bsd_init` calls `IOKitInitializeTime()` between `bsd_bufferinit()` and
     * `ubc_init()` (`bsd/kern/bsd_init.c:727-728`, and the disassembly agrees: `bl IOKitInitializeTime`
     * at `0x8003B1B0`, `bl ubc_init` at `0x8003B1B4`), and that function is a wait, not a read
     * (`iokit/Kernel/IOStartIOKit.cpp:67-77`):
     *
     *     mach_timespec_t t; t.tv_sec = 30; t.tv_nsec = 0;
     *     IOService::waitForService( IOService::resourceMatching("IORTC"), &t );
     *     clock_initialize_calendar();
     *
     * Experiment 419 measured what its absence costs: `waitForMatchingService` finds nothing, takes the
     * notify lock and sleeps on a deadline, and the run reports *nothing at all* - the log stops at
     * `jumping to XNU's _start`, with no `stub_hit`, no `abort_entries`, no `exception:` and no `panic`,
     * while the link is demonstrably the new one. Both ways out of that sleep are closed on this
     * machine: the 30 s deadline cannot fire, because nothing here arms a hardware timer
     * (`ml_set_decrementer` takes the `!__ARM_TIME__` branch and only stores to per-CPU memory, since
     * `cpu_set_decrementer_func` is NULL - `ml_init_timebase`'s only caller is `pe_identify_machine`'s
     * Apple-SoC `strcmp` chain, which `return 0`s for MSM8974), and the net cannot outlive it either
     * (the bark register is 20 bits at 32765 Hz: 29 s is the largest bark it can hold).
     *
     * **`IORTC` is a closed-source driver's resource, and this machine's RTC is a platform device.**
     * Nothing in the open-source tree publishes it: the only `publishResource` calls in the tree are
     * `"IONVRAM"` and `kIOPlatformUUIDKey` (`iokit/Kernel/IOPlatformExpert.cpp:1204-1209`, in
     * `registerNVRAMController`), `"IOKit"` (`IOService.cpp:1212`, in `setPlatform`), `"IOBSD"`
     * (`iokit/bsddev/IOKitBSDInit.cpp:95`) and `"boot-uuid"`. `IORTCController`
     * (`iokit/IOKit/rtc/IORTCController.h:37`) is an abstract `IOService` with no implementation in the
     * tree at all - so the publisher is Apple's closed-source RTC driver, and on this SoC the clock it
     * would drive is the platform's own: the GPT at `0xf9020000` (19.2 MHz), which the payload's own
     * interrupt handler already counts. Stating it from the platform expert is the same idiom XNU uses
     * for the resources the platform owns, not a shortcut around one.
     *
     * The match is a property test, not a class test: `resourceMatching("IORTC")` sets the table's
     * `IOResourceMatch` key to `"IORTC"`, and `IOResources::matchPropertyTable`
     * (`IOService.cpp:5083-5091`) answers `0 != getProperty("IORTC")` on the resource nub.
     * `IOService::publishResource(key, 0)` sets that property and `registerService()`s the nub
     * (`IOService.cpp:3532-3544`), which is exactly what `publishResource("IONVRAM")` does two
     * functions earlier in Apple's own code. With it, `waitForService` returns the resource
     * immediately - the wait needs no timer, which is why this step is worth taking before the timer
     * work rather than after it.
     *
     * **Prediction, written before the build: `stub_hit=ubc_init`, caller key `0x8003B1B8`** - the
     * return address of the `bl <ubc_init>` at `0x8003B1B4`, `bsd_init + 0x7C4`. `bsd_init`'s own
     * addresses cannot move: the statement this step adds is in an object linked after `bsd_init.c`'s.
     * Between this call and that stub: `clock_initialize_calendar` (real, 0x1E0 bytes, walked clean in
     * both directions - its only indirect is `PEGetUTCTimeOfDay`'s `if (gIOPlatform)`-guarded virtual
     * call, `IOPlatformExpert.cpp:1096-1105`).
     *
     * Falsifiers, named in advance: **the same silence** - which would mean the match still failed or
     * something faulted inside the publish or the waiter, and is the outcome that would say this
     * analysis is wrong; a stop inside `waitForService`, `resourceMatching`, `copyExistingServices`,
     * `IOService::setNotification` or `IORecursiveLockSleepDeadline`; a stop at `ast_taken_kernel` (a
     * stub three guards deeper in `clock_initialize_calendar`'s guarded list, reachable only if an AST
     * is pending when interrupts are enabled); a stop at `CURSIG` (419's name, for a signalled thread);
     * a `panic`; a stop at `vfsinit` (`+0x7C8`, key `0x8003B1BC`), which would mean `ubc_init` had
     * already been retired. Note also that this step adds **no symbols** - the stub set is untouched
     * and the payload's marker set may be byte-identical to 419's (defect 130); what will prove the new
     * image ran is the stop. */
    IOService::publishResource( "IORTC" );

    /* Experiment 405. `ml_get_max_cpus` (`osfmk/arm/machine_routines.c:184`) is a wait, not a read:
     * `if (max_cpus_initialized != MAX_CPUS_SET) { max_cpus_initialized = MAX_CPUS_WAIT;
     * assert_wait(&max_cpus_initialized, THREAD_UNINT); thread_block(THREAD_CONTINUE_NULL); }`, and
     * `commpage_populate` calls it with the comment `// NB: this call can block`
     * (`osfmk/arm/commpage/commpage.c:222`). The only writer of that flag is `ml_init_max_cpus`
     * (`machine_routines.c:163`), whose only caller in this image is
     * `IOCPUInterruptController::initCPUInterruptController(int, int)+0x94` (`iokit/Kernel/IOCPU.cpp:765`)
     * - and the platform-expert hook that would construct that controller,
     * `createCPUInterruptController`, is not in the open-source tree at all: only Apple's closed-source
     * platform experts implement it. Experiments 403 and 404 measured the consequence: the boot thread
     * reaches the `bl ml_get_max_cpus` at `commpage_populate + 0x70` (key 0x800E6C98) and never returns,
     * because nothing on this machine can announce a CPU count.
     *
     * This call is the smallest thing that can set it, and it is deliberately the *probe* rather than
     * the faithful fix: Apple's own platforms set this fact from the CPU interrupt controller's
     * constructor, which also allocates one lock per interrupt source and - through
     * `enableCPUInterrupt` - installs the per-CPU interrupt handler the timer needs. This image
     * instantiates no CPU driver at all, so the number announced here is what this payload actually
     * brings up: one processor, the boot CPU, with no per-CPU interrupt handler installed. Nothing in
     * the image can start a second CPU, and announcing four (the SoC's real core count) would size
     * `cpu_data`, the scheduler's per-CPU arrays and the shared page's CPU count for processors that
     * never register.
     *
     * Where the boot stops next is the measurement this step exists for: `ml_get_max_cpus` returns,
     * `commpage_populate` completes its remaining 100+ instructions, and the walk continues in
     * `kernel_bootstrap_thread`'s ladder - which, if every remaining frame is real, ends at the stub
     * `throttle_init` (`bsd_init + 0x8`, key 0x8003A9FC). */
    ml_init_max_cpus( 1 );

    /* Experiment 463. **This object's own identity and its state word, read at the end of `start`** -
     * the two numbers the instance walk in the tracer compares against, and the reason that walk can
     * name an instance without ever naming a class: `IOService::getState()`'s one implementation
     * (`IOService.h:499`) returns `__state[0]`, and a walk over the `IOPlatformExpert` metaclass's
     * instances that shows *this* pointer is a walk that found this object.
     *
     * The value is a **moment** and not a verdict, and the difference is the whole point of taking it
     * here rather than anywhere else: `kIOServiceMatchedState` (0x4, `IOService.h:74`) is written by
     * `copyNotifiers( gIOMatchedNotification, kIOServiceMatchedState, 0xffffffff )`
     * (`IOService.cpp:3754`, whose body ends `__state[0] = (__state[0] | orNewState) & andNewState`),
     * i.e. in `doServiceMatch` *after* `start` returns - so this reading is expected to *lack* bit 2
     * even on a boot where the wait succeeds, and it is `kIOServiceRegisteredState` (0x2) that is the
     * discriminating bit here: it is set inside `registerService` (`IOService.cpp:3702`), in the same
     * two statements as `getMetaClass()->addInstance(this)` (`:3694`) - so **bit 1 set here would
     * prove registration happened before this object's own start returned, and bit 1 clear with the
     * walk's `seen` = 0 is the third candidate 462 did not name**: nothing in this tree registers an
     * `IOPlatformExpert` (the only `registerService` callers are `IOStartIOKit.cpp:167`'s root nub -
     * an `IOPlatformExpertDevice`, whose superclass is `IOService`, not `IOPlatformExpert` - the
     * platform nubs at `IOPlatformExpert.cpp:205`/`:1306`, the NVRAM controller at `:1340`, and
     * `gIOResources` at `IOService.cpp:3543`). */
    entry_live_write( "xnu_live_pexpert_self", (uint32_t)(uintptr_t) this );
    entry_live_write( "xnu_live_pexpert_state", (uint32_t) this->getState() );

    /* Experiment 464. **The registration.** 463 measured that `IOSecureBSDRoot`'s
     * `waitForMatchingService(serviceMatching("IOPlatformExpert"), 30 s)` cannot be satisfied on this
     * boot: `OSMetaClass::applyToInstancesOfClassName` resolves the name to the metaclass
     * (`0x8058b4f0`) and then walks its `instances` set, which holds nothing - `wcls_seen = 0` - so
     * the two mechanisms 462 named (the `__state[0]` bit test in `instanceMatch` and the
     * `matchInternal` clause of `matchPassive`) are filters over an *empty* set rather than failing
     * tests. The one caller of `OSMetaClass::addInstance` in the whole tree is
     * `IOService::doServiceMatch` (`IOService.cpp:3694`), which is reached from `registerService` ->
     * `startMatching` - and **nothing in this tree registers an `IOPlatformExpert`**. The
     * `registerService()` callers the tree does have are `IOStartIOKit.cpp:167`'s root nub (an
     * `IOPlatformExpertDevice`, i.e. a *sibling* of this class, `IOPlatformExpert.h:290`), the
     * platform nubs (`IOPlatformExpert.cpp:205`, `:1306`), the NVRAM controller (`:1340`) and
     * `gIOResources` (`IOService.cpp:3543`) - so this object is the missing one, and this statement
     * is what Apple's closed-source platform experts do at the end of their own `start`: it is the
     * documented idiom, not a shortcut (`IOService.h:510-515`).
     *
     * **What the call does, and the reason it is not the same thing as "the registration happens
     * here".** `registerService` (`IOService.cpp:751`) is a registry-membership check, then
     * `gIOPlatform->platformAdjustService` (this object, which answers true -
     * `IOPlatformExpert.cpp:413`), then `IOInstallServicePlatformActions(this)`, then
     * `startMatching(options)` with `options = 0`. `startMatching` (`:805`) computes
     * `sync = (options & kIOServiceSynchronous) || (provider && (provider->__state[1] &
     * kIOServiceSynchronousState))`; `getProvider()` here is the root nub, whose own
     * `registerService()` was called with no options (`IOStartIOKit.cpp:167`), so its synchronous
     * bit was *cleared* - `sync` is false and the call takes the branch
     * `!sync || (kIOServiceAsynchronous & options)`:
     *
     *     ok = (0 != _IOServiceJob::startJob( this, kMatchNubJob, options ));      IOService.cpp:857
     *
     * so the match is **enqueued as a job**, not run inline: `pingConfig` (`:4175`) queues it on
     * `gJobs` and signals `gJobsSemaphore`, a config thread picks it up and calls
     * `nub->doServiceMatch(job->options)` (`:4103`). The boot thread returns from
     * `registerService()` with nothing set yet.
     *
     * That is why the two records below are a *pair* and not one: `xnu_live_pexpert_state` (written
     * immediately above, at the same moment as 463's) is the state **before**, and
     * `xnu_live_pexpert_state_reg` is the state **immediately after the call**. The prediction,
     * written before the build: `_reg_seq = 1` and `_state_reg = 0` - the async path - and the
     * registration itself visible later, at wait 2's `wcls` record, as `wcls_seen = 1` with
     * `winst_inst = 0xc04bf800` and both bit 1 (`kIOServiceRegisteredState`, set at `:3702`) and bit
     * 2 (`kIOServiceMatchedState`, set by `copyNotifiers(gIOMatchedNotification,
     * kIOServiceMatchedState, 0xffffffff)` at `:3753`) set, i.e. `0x1e` - the same reading the
     * resource root carries (`addInstance` also sets bit 3 at `:3695` and bit 4 at `:3756`, so
     * "0x1e" rather than "0x06" is the full expectation for a first, successful `doServiceMatch`).
     *
     * **Why no driver will be instantiated by this registration.** `doServiceMatch` opens with
     * `matches = gIOCatalogue->findDrivers(this, &catalogGeneration)` (`:3687`), and `findDrivers`
     * (`IOCatalogue.cpp:198-222`) walks `service->getMetaClass()` up the chain - here
     * `MSM8974PlatformExpert`, `IODTPlatformExpert`, `IOPlatformExpert`, `IOService` - collecting
     * personalities filed under each of those names, where filing is by **`IOProviderClass`**
     * (`arrayForPersonality`, `:124-132`, called from `addPersonality`, `:134-149`). This image's
     * three personalities are filed under `IOPlatformExpertDevice` (two: this class's and
     * `IOPanicPlatform`'s) and `IOResources` (one: `MSM8974RootResource`'s), and none of those names
     * is on this object's class chain - `IOPlatformExpertDevice` is a subclass of `IOService`, not of
     * `IOPlatformExpert` - so `matches` is an empty set. `probeCandidates` is therefore skipped
     * (`matches->getCount()` is 0 at `:3720`), which is also what keeps 362's panic out of reach: the
     * catalogue's `IOPanicPlatform` entry would match an `IOPlatformExpertDevice` provider, and this
     * object is not one.
     *
     * Falsifiers, named in advance. (a) `_state_reg` **non-zero** would mean `sync` was true and the
     * inline `doServiceMatch` branch ran (`:859-884`), whose `waitAgain = (prevBusy <
     * (__state[1] & kIOServiceBusyStateMask))` would then `assert_wait` + `thread_block` with no
     * timer on this machine to wake it: a non-zero `_state_reg` followed by silence in `start` is the
     * signature, and the fix would be `kIOServiceAsynchronous`. (b) `_reg_seq = 1` with `_state_reg =
     * 0` and *no* `wcls_seen = 1` at wait 2 is the same third sleep 463 measured, now with the
     * enqueue known to have happened - which would put the frontier in the job machinery
     * (`pingConfig`'s `create` decision, the config thread's existence under `kernel_thread_start`,
     * or `doServiceMatch` itself). (c) `wcls_seen = 1` with `wsvc_p4 = 0` is 463's own falsifier and
     * would move the cause to the filter; (d) a *new* console line, `"MSM8974PlatformExpert: not
     * registry member at registerService()"` (`IOService.cpp:769-772`), would mean the walk to the
     * registry root through `gIOServicePlane` failed and the call returned before `startMatching`
     * (`state_reg` would be 0 in that case too, so the console is the distinguishing reading); and
     * (e) a stop at `IOLog`, `IOMalloc`/`IOFree` (the `kIOLogRegister` block is compiled in but
     * `gIOKitDebug` does not have that bit, so it should not run), `_adjustBusy`, `IOLockLock` or
     * `semaphore_signal`. */
    g_pexpert_registers++;
    entry_live_write( "xnu_live_pexpert_reg_seq", g_pexpert_registers );
    this->registerService();
    entry_live_write( "xnu_live_pexpert_state_reg", (uint32_t) this->getState() );

    return( true );
}

/*
 * The two lists IOKit's platform bring-up asks a platform expert for, and neither of them is NULL
 * (experiment 462).
 *
 * Both answered `(const char *)0` from 363 to 461, on the reading that NULL means "no names". It does
 * not mean that to the one caller that matters:
 *
 *     IODTFindMatchingEntries( from, options, keys )             IODeviceTreeSupport.cpp:886
 *         ...
 *         if( keys) { cmp = IODTMatchNubWithKeys( next, keys ); ... }
 *         else result->setObject( next);
 *
 * A NULL key list is not "match nothing" - it is the *else* branch, "collect every entry" - and
 * `processTopLevel`'s call does not set `kIODTExclusive` either:
 *
 *     kids = IODTFindMatchingEntries( rootEntry, 0, deleteList() );      IOPlatformExpert.cpp:1322
 *     while( (next = kids->getNextObject())) next->detachAll( gIODTPlane);
 *
 * so with `deleteList()` answering NULL, that loop detaches **every child the root entry has in the
 * IODT plane**, `chosen` among them - and `/chosen` is the node `IOFindBSDRoot` roots this machine
 * from (`iokit/bsddev/IOKitBSDInit.cpp:404`, `:440`). Apple's own subclasses answer with the few
 * nodes that deserve deleting, as quoted names in the OSUnserialize list grammar:
 *
 *     AppleMacIO.cpp:108            "('sd', 'st', 'disk', 'tape', 'pram', 'rtc', 'mouse')"
 *     ApplePlatformExpert.cpp:86    "('packages', 'psuedo-usb', 'psuedo-hid', 'multiboot', 'rtas')"
 *
 * `"()"` is that answer with the set empty. `OSUnserialize.y:168` is `array: '(' ')' { $$ = NULL; }`,
 * so it parses to an empty `OSArray` - non-NULL, which is the only thing `IODTMatchNubWithKeys`
 * checks - and comparing each entry's name against nothing answers false, so the loop detaches
 * nothing. For `excludeList()` it is the same answer NULL gave - `kIODTExclusive` *is* set there, so
 * "false != cmp" holds for every entry and every child is still published - which is the point: one
 * spelling for both, and the spelling is the one the grammar defines.
 *
 * **The records below are what says whether this was the frontier.** `_seq` counts the asks (zero
 * means `processTopLevel` never reached them, and 461's run is the state to compare against), `_ptr`
 * and `_len` are the answer as string, and `_len` is read from the bytes rather than from this
 * comment - an empty list is two bytes and a NUL, a NULL is zero bytes and no pointer.
 */
const char *
MSM8974PlatformExpert::deleteList( void )
{
    static const char kEmptyList[] = "()";

    g_delete_list_calls++;
    entry_live_write( "xnu_live_deletelist_seq", g_delete_list_calls );
    entry_live_write( "xnu_live_deletelist_ptr", (uint32_t)(uintptr_t) kEmptyList );
    entry_live_write( "xnu_live_deletelist_len", MSM8974_list_len( kEmptyList ) );
    return( kEmptyList );
}

const char *
MSM8974PlatformExpert::excludeList( void )
{
    static const char kEmptyList[] = "()";

    g_exclude_list_calls++;
    entry_live_write( "xnu_live_excludelist_seq", g_exclude_list_calls );
    entry_live_write( "xnu_live_excludelist_ptr", (uint32_t)(uintptr_t) kEmptyList );
    entry_live_write( "xnu_live_excludelist_len", MSM8974_list_len( kEmptyList ) );
    return( kEmptyList );
}
