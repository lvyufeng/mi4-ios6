/*
 * The kernel's personality table, with this machine's platform expert in front of Apple's fallback
 * (experiment 363).
 *
 * The open-source tree's table is one entry - `iokit/KernelConfigTables.cpp:35`:
 *
 *   const char * gIOKernelConfigTables =
 *   "( { 'IOClass' = IOPanicPlatform; 'IOProviderClass' = IOPlatformExpertDevice;"
 *   " 'IOProbeScore' = 0:32; } )";
 *
 * and `IOCatalogue::initialize` (iokit/Kernel/IOCatalogue.cpp:98-102) parses whatever this symbol
 * points at with `OSUnserialize`. Matching walks the personalities and instantiates each by *class
 * name* (`IOService.cpp:3296-3301`, `OSMetaClass::allocClassWithName`), so the first entry whose
 * class exists and whose probe finds the nub is the one that starts.
 *
 * This object *replaces* the stock `iokit_KernelConfigTables.o` in the entry link - one object for
 * one object, so the link's own accounting is unchanged - and the table it replaces is reproduced
 * below, verbatim and last, so that Apple's designed fallback is still there behind ours: with this
 * class absent or its name wrong, the machine must still behave exactly as experiment 362 measured
 * it, and panic with the platform name rather than boot silently with no platform expert at all.
 *
 * The class name is the one the Stage64..75 plane already used for this machine
 * (`/msm8974-platform-driver`'s `IOClass` in `stages/stage90/xnu_iokit_platform_scaffold_contract.c`,
 * built by `stages/stage85/stage85_main.c`, with `IOProbeScore = 0x00000650`), and 1616 is that
 * score written the way Apple's own entry writes its zero: `<value>:<bits>`. It is greater than
 * every other score in the table, which is what "ahead of the fallback" means here - the fallback's
 * 0 is still a legal score and would still match if this one did not.
 *
 * **`IONameMatch` is what makes the match happen at all, and its value is a device-tree fact.**
 * `IODTPlatformExpert::probe` (`IOPlatformExpert.cpp:1256-1268`) requires the driver's
 * `getProperty("IONameMatch")` to name the provider - `provider->compareNames(...)`, false for a
 * missing property - so without this key the probe fails and the table falls through to
 * `IOPanicPlatform`, which is exactly the panic experiment 362 measured. The comparison is
 * `IODTCompareNubName`/`CompareKey` over the provider nub's `name`, `compatible`, `device_type` and
 * `model` (`IODeviceTreeSupport.cpp:799-865`), and the provider is `IOPlatformExpertDevice` built
 * from the root of this machine's device tree, whose properties are written at
 * `stages/stage90/stage90_main.c:79-82`: `name = "/"`, `compatible = "qcom,msm8974-xnu-stage90"`,
 * `model = "Xiaomi Mi 4 cancro Stage84"`. The value below is the `compatible` one, which is the
 * device tree's own name for this machine.
 *
 * It is **quoted** on purpose: the old-style plist lexer's unquoted-string rule accepts
 * `[A-Za-z0-9-]` only (`OSUnserialize.y:298-317`), and this value has a comma in it.
 *
 * **And a second entry (457), which is the machine's first driver rather than its platform expert.**
 * Experiment 456 derived, and `MSM8974RootResource.cpp`'s header re-derives from the source, that
 * `IOFindBSDRoot`'s 30-second wait cannot end until `gIOResources` carries an `IOResourceMatched`
 * array containing `"IOBSD"` - and that the array is written only when `doServiceMatch`'s
 * `matches->getCount()` is non-empty, where `matches` is `gIOCatalogue->findDrivers(gIOResources)`.
 * `findDrivers` looks personalities up by **`IOProviderClass`** along the service's class chain
 * (`IOCatalogue.cpp:199-231`) - the key `addDrivers` files them under
 * (`IOCatalogue.cpp:124-132`, `arrayForPersonality`) - and the resource root's chain is
 * `IOResources` then `IOService`. So this entry's provider class is the whole of its effect on the
 * boot, and `IOResourceMatch` is what its own candidate test asks for: the resource the driver that
 * will own this machine's root device needs, which is why `IOKitBSDInit` publishes `"IOBSD"` before
 * it waits.
 *
 * Unlike the platform expert's, this entry carries **no `IOProbeScore`**, and that is a statement
 * rather than an omission: a score ranks *siblings in one provider-class array* (`IOServiceOrdering`
 * over `gIOProbeScoreKey`), and this array has exactly one member. It is also the only entry here
 * whose provider class is not a nub this image creates at boot - `IOResources` is built by
 * `IOService::setPlatform` and is already registered when the catalogue is first consulted, which is
 * what makes the match a property of the *catalogue* rather than of the device tree.
 *
 * The entries are placed **before** Apple's fallback, and in the boot's own order after that: the
 * platform expert that names this machine, the drivers for the device nodes it publishes (492's timer
 * and 493's interrupt controller), the driver that makes its resource root match, and Apple's designed
 * fallback last as it was. The
 * order is not what makes the matching work - `addPersonality` buckets each entry by its
 * `IOProviderClass`, so the array's order only decides the *scores* of entries that share one bucket -
 * but it is what makes this file readable as the boot it describes.
 *
 * **And a third entry (492), the driver for this machine's first device node.** 491's census
 * measured what the driver layer looks like without it: 26 nubs under the platform expert, every one
 * of them registered, and **not one service-plane child under any of them** - no driver attached to a
 * device node at all. The reason is in the catalogue rather than in the matching code:
 * `IOService::doServiceMatch` decides on `matches = gIOCatalogue->findDrivers(this, &generation)`
 * (`IOService.cpp:3688`) and `findDrivers(IOService *, SInt32 *)` looks personalities up **by the
 * service's own class chain** (`IOCatalogue.cpp:199-231`), because `addPersonality` files them under
 * their `IOProviderClass` value (`:124-132`). The nubs this machine's tree produces are
 * `IOPlatformDevice` - `IODTPlatformExpert::createNub` is `nub = new IOPlatformDevice`
 * (`IOPlatformExpert.cpp:1283`) - and no entry here named that class, so every nub was answered with
 * an empty set and `probeCandidates` never ran for one.
 *
 * So the third entry's `IOProviderClass` is `IOPlatformDevice`, read out of Apple's own `createNub`
 * rather than guessed: the *root* nub the platform expert's entry names is
 * `IOPlatformExpertDevice`, a different class, and a personality filed under it can never be found by
 * the chain `IOPlatformDevice` -> `IOService` that a nub's lookup walks.
 *
 * `IONameMatch` is what the candidate test asks for afterwards, and its value comes from the device
 * tree the payload builds (`stage90_main.c`'s `/timer` node): an `IOPlatformDevice`'s `compareName` is
 * `((IOPlatformExpert *)getProvider())->compareNubName(this, ...)` (`IOPlatformExpert.cpp:1688-1693`),
 * which is `IODTCompareNubName` over the provider nub's `name`, `compatible`, `device_type` and
 * `model` (`IODeviceTreeSupport.cpp:799-865`). The two names below are that node's `name` and its
 * `compatible`, listed as an OSUnserialize array so the driver's own record can say *which* of them
 * matched (`kIONameMatchedKey`, `IOService.cpp:5443-5448`) instead of only that something did.
 * `MSM8974Timer.cpp` holds both names as well, and `tools/check_driver_catalogue.py` compares the two
 * lists - one value, two definitions, and this time compared.
 *
 * **And a fourth entry (493), which is the second driver in the same bucket.** 492's mechanism had
 * three parts that could each have been a property of that one node - the bucket (`IOPlatformDevice`),
 * the probe score, and the name bridge - and one driver cannot tell "the table matches nodes" from
 * "this node happened to work". So the same three parts are stated again for `/interrupt-controller`
 * (`compatible = "qcom,msm-qgic2"`), whose names `MSM8974Timer`'s cannot match: `probeCandidates`
 * walks the whole bucket for every nub, so **both** personalities are probed against **both** nodes
 * and each run's record says which one started and on which node (`MSM8974GIC.cpp`'s `_match` = 3 or a
 * start whose `_prov` is the other node would be the falsification). The node is picked for a second
 * reason as well: 482 and 483 already measured this machine's GIC from the payload side, so the
 * addresses this entry's driver resolves are addresses this project holds an independent reading of.
 *
 * The two entries share a bucket and an `IOProbeScore`, which is correct rather than sloppy: a score
 * ranks **siblings in one provider-class array** (`IOServiceOrdering` over `gIOProbeScoreKey`) and is
 * only consulted when more than one entry could claim the same provider, which two entries naming
 * disjoint node names cannot. What the pair measures is the thing a single driver cannot - that the
 * bucket is searched for every nub and the *candidate test* is what selects, not the bucket's size.
 *
 * No `CFBundleIdentifier`, deliberately: `probeCandidates` stalls on
 * `gIOCatalogue->isModuleLoaded(match)` (`IOService.cpp:3253`) and that function's answer is "true" for
 * exactly the personalities that carry no bundle id, "assumed to be an in-kernel driver"
 * (`IOCatalogue.cpp:475-496`) - which this is, linked into the entry image by
 * `tools/build_xnu_arm_kernel.sh`'s `PLATFORM_SOURCES`.
 *
 * Compiled as C by this stage's own toolchain and linked as data. The symbol is a modifiable
 * pointer in C, so it lands in `.data` exactly where the stock object's does; the string is
 * read-only data.
 */
const char * gIOKernelConfigTables =
    "("
    "   {"
    "     'IOClass'         = MSM8974PlatformExpert;"
    "     'IOProviderClass' = IOPlatformExpertDevice;"
    "     'IONameMatch'     = \"qcom,msm8974-xnu-stage90\";"
    "     'IOProbeScore'    = 1616:32;"
    "   },"
    "   {"
    "     'IOClass'         = MSM8974Timer;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = (timer, \"qcom,msm-timer\");"
    "     'IOProbeScore'    = 1616:32;"
    "   },"
    "   {"
    "     'IOClass'         = MSM8974GIC;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = (interrupt-controller, \"qcom,msm-qgic2\");"
    "     'IOProbeScore'    = 1616:32;"
    "   },"
    "   {"
    "     'IOClass'         = MSM8974RootResource;"
    "     'IOProviderClass' = IOResources;"
    "     'IOResourceMatch' = IOBSD;"
    "   },"
    "   {"
    "     'IOClass'         = IOPanicPlatform;"
    "     'IOProviderClass' = IOPlatformExpertDevice;"
    "     'IOProbeScore'    = 0:32;"
    "   }"
    ")";
