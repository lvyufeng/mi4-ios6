/*
 * The machine's root-device driver (experiment 457) - the first driver this project links in, and
 * the ingredient experiment 456 proved was missing.
 *
 * Why it exists
 * -------------
 * 456 measured the frontier inside `IOFindBSDRoot`'s 30-second wait (`iokit/bsddev/IOKitBSDInit.cpp:
 * 379`) and derived, from the resource root's own state word, that the wait cannot end on this
 * machine: the wait's matching dictionary is
 *
 *     matching = IOService::serviceMatching( gIOResourcesKey );          // { IOProviderClass: IOResources }
 *     matching->setObject( gIOResourceMatchedKey, gIOBSDKey );           // { IOProviderClass: IOResources,
 *                                                                        //   IOResourceMatched: "IOBSD" }
 *
 * (`IOKitBSDInit.cpp:376-378`), and `copyExistingServices`' fast path (`IOService.cpp:4282-4300`)
 * answers it by testing `gIOResources` itself: the state guard `0x4 == (__state[0] & 0x4)`, then
 * `service->matchPassive(matching, options)` - which for the resource root is
 * `IOResources::matchPropertyTable` (`IOService.cpp:5083-5120`), whose *third* branch is
 *
 *     keys = (OSArray *) copyProperty(gIOResourceMatchedKey);
 *     ok = (keys && ((-1U) != keys->getNextIndexOfObject(prop, 0)));
 *
 * So the wait returns only when `gIOResources` carries an `IOResourceMatched` array containing
 * `"IOBSD"`. That array is written in exactly one place - `doServiceMatch`'s tail,
 * `IOService.cpp:3751`:
 *
 *     if (resourceKeys) setProperty(gIOResourceMatchedKey, resourceKeys);
 *
 * - and `resourceKeys` is filled immediately above (`:3724-3730`) only when
 *
 *     if (keepGuessing && matches->getCount() && (kIOReturnSuccess == getResources()))
 *         if (this == gIOResources) resourceKeys = copyPropertyKeys();
 *
 * `IOResources` does not override `getResources` (`IOService/private` - `IOServicePrivate.h:183-198`
 * lists its overrides: `init`, `newUserClient`, `getWorkLoop`, `matchPropertyTable`, `setProperties`)
 * and `IOService::getResources` returns `kIOReturnSuccess` unconditionally (`IOService.cpp:982-985`),
 * so the *only* conjunct that can be false is `matches->getCount()`, and `matches` comes straight from
 *
 *     matches = gIOCatalogue->findDrivers( this, &catalogGeneration );   // IOService.cpp:3688
 *
 * **`findDrivers` looks personalities up by `IOProviderClass`, along the service's own class chain**
 * (`IOCatalogue.cpp:199-231`: `meta = service->getMetaClass(); ... personalities->getObject(meta->
 * getClassNameSymbol()) ... meta = meta->getSuperClass()`, stopping at `IOService`), because that is the
 * key `addDrivers` files them under (`IOCatalogue.cpp:124-132`, `arrayForPersonality`). For the resource
 * root the chain is `IOResources` then `IOService`, so a non-empty `matches` for `gIOResources` needs a
 * personality whose `IOProviderClass` is one of those two - and a kernel with no kexts has neither.
 *
 * That is this class: a concrete `IOService` subclass whose personality declares
 * `IOProviderClass = IOResources` and `IOResourceMatch = "IOBSD"`. The provider class is what puts it
 * in `findDrivers`' answer; the resource match is what its own candidate test asks for, and it is the
 * resource this driver - the one that will own the machine's root device - genuinely needs, which is
 * why `IOKitBSDInit` publishes it before it waits:
 *
 *     IOService::publishResource( "IOBSD" );                              // IOKitBSDInit.cpp:95, :1206
 *
 * `publishResource` sets the property and then `registerService()`s the resource root
 * (`IOService.cpp:3532-3544`), and `registerService` is what queues the `doServiceMatch` that this
 * personality makes non-empty - the same event 456 caught between its two readings (`__state[0]`
 * `0x00000000` at the first wait, `0x0000001e` at the second). With the array set, `copyNotifiers(
 * gIOMatchedNotification, kIOServiceMatchedState, 0xffffffff)` (`:3753`) also finds the waiting
 * thread's notifier, because the test that gates it is `matchPassive(notify->matching, 0)`
 * (`:4782-4784`) - the very same third branch. One missing array is both why the match failed and
 * why nothing ever woke the waiter.
 *
 * What this step is not
 * ---------------------
 * It is the *probe* and not the finished driver, in the sense 405's `ml_init_max_cpus(1)` and 420's
 * `publishResource("IORTC")` were: it makes the one thing happen that the boot is waiting for, and
 * its `start` states what it is rather than pretending to be the root media. The root device itself -
 * an `IOMedia` with a `Content` a filesystem can mount, or the ramdisk `IOFindBSDRoot` builds from
 * `/chosen/memory-map` - is the next step, and this class is where it will live.
 *
 * The record it leaves
 * --------------------
 * `start` writes three live keys, and they are the only direct evidence that a driver this project
 * authored was instantiated from the catalogue: `xnu_live_rootdrv_seq` (the ordinal), `_prov` (the
 * provider pointer, which must be `gIOResources`) and `_iobsd` (`provider->getProperty("IOBSD")`,
 * non-zero only if the property was published before the match - the ordering 456's bracket showed).
 * `entry_live_write` is the entry instrument's own entry point; it is declared here rather than
 * included from it because this file is compiled by the tree's own build (see PLATFORM_SOURCES in
 * `tools/build_xnu_arm_kernel.sh`), the same way `MSM8974PlatformExpert.cpp` is, and that symbol
 * exists in every configuration this project builds (the canonical entry build sets
 * `STAGE90_ENTRY_TRACE=1`); a build that left it out would be an undefined symbol and a loud link
 * failure, not a silent zero.
 */
#include <IOKit/IOService.h>

extern "C" void entry_live_write(const char *key, uint32_t value);

class MSM8974RootResource : public IOService
{
    OSDeclareDefaultStructors(MSM8974RootResource);

public:
    virtual bool start( IOService * provider ) APPLE_KEXT_OVERRIDE;
};

/* `super` is a per-file macro in this tree rather than a keyword (`IOService.cpp:72` is
 * `#define super IOService`), so a file that wants `super::start` has to say so - the same line
 * `MSM8974PlatformExpert.cpp` carries for the same reason. */
#define super IOService

OSDefineMetaClassAndStructors(MSM8974RootResource, IOService);

static uint32_t g_root_resource_starts;

bool
MSM8974RootResource::start( IOService * provider )
{
    if( !super::start( provider )) return( false );

    g_root_resource_starts++;

    entry_live_write( "xnu_live_rootdrv_seq", g_root_resource_starts );
    entry_live_write( "xnu_live_rootdrv_prov", (uint32_t)(uintptr_t) provider );
    entry_live_write( "xnu_live_rootdrv_iobsd",
                      (uint32_t)(uintptr_t)(provider ? provider->getProperty( "IOBSD" ) : 0) );

    return( true );
}
