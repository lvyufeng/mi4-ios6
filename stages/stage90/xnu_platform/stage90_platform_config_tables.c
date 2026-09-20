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
 * The entry is placed **before** Apple's fallback, and after the platform expert, so that the file's
 * own reading order is the boot's: the platform expert that names this machine, the driver that makes
 * its resource root match, and Apple's designed fallback last as it was.
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
