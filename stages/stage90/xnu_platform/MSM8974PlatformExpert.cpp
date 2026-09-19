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
    return( super::start( provider ));
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
