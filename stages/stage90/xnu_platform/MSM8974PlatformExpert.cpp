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
 * the link.
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
#include <arm/machine_routines.h>

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

bool
MSM8974PlatformExpert::start( IOService * provider )
{
    if( !super::start( provider )) return( false );

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

    return( true );
}

/*
 * The two lists IOKit's kext loading asks a platform expert for. This kernel loads no kexts and
 * has none to exclude, so both answers are the empty one - the same `(const char *)0`
 * `IOPlatformExpert`'s own methods answer with when they are reached.
 */
const char *
MSM8974PlatformExpert::deleteList( void )
{
    return( (const char *)0 );
}

const char *
MSM8974PlatformExpert::excludeList( void )
{
    return( (const char *)0 );
}
