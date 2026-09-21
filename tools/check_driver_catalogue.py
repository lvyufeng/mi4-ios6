#!/usr/bin/env python3
"""
Check that every personality in this machine's `gIOKernelConfigTables` can actually match something.

    python3 tools/check_driver_catalogue.py --verbose
    python3 tools/check_driver_catalogue.py --selftest

Why this exists. `IOService::doServiceMatch` decides everything on

    matches = gIOCatalogue->findDrivers( this, &catalogGeneration );        // IOService.cpp:3688

and `IOCatalogue::findDrivers(IOService *, SInt32 *)` (`IOCatalogue.cpp:199-231`) looks personalities up
by **the service's own class chain**, because `addPersonality` (`:124-132`) files each one under its
`IOProviderClass` value:

    meta = service->getMetaClass();
    while (meta) { array = personalities->getObject(meta->getClassNameSymbol()); ... }

Two silent failures come out of that one fact, and both were measured here rather than imagined:

  * **A personality filed under a class no service has is a personality nothing can match.** 491's
    census is what this looks like from the other side: 26 nubs under the platform expert, every one
    registered and matched, and not one service-plane child under any of them. This table's three
    entries were under `IOPlatformExpertDevice` and `IOResources`; every nub is `IOPlatformDevice`
    (`IODTPlatformExpert::createNub`, `IOPlatformExpert.cpp:1283`), so `findDrivers` answered each nub
    with an empty set and `probeCandidates` was never called for one.
  * **A personality naming a class no object defines is a match that fails at
    `OSMetaClass::allocClassWithName`** (`IOService.cpp:3296-3301`), which returns nothing, prints
    nothing and starts nothing. The class name in the table and the class the source defines are one
    value with two definitions, and until this check they were compared only by eye.

So the check is over the *sources the build compiles*, and it holds the table to the three files that
would have to agree for a driver to start: the personality's provider class against the class Apple's
own `createNub` builds, the personality's class name against the `OSDefineMetaClassAndStructors` in
`PLATFORM_SOURCES` (read from the build script, not repeated here), and the personality's `IONameMatch`
names against the device-tree node the payload writes and the names the driver itself compares.

**493's fourth part: the two readings of one device's address.** Between 492 and 493 the drivers grew
a comparison between *their* reading of a node's `reg` and the **OS's** reading of the same property -
the `IODeviceMemory` array `IODTResolveAddressing` filed on the nub before `start` was called - and
that comparison only means something if both sides are about one property, counted in one unit, and
published. So the check also holds:

  * the tree's root to the cell counts its own `reg` arrays are written for, against Apple's defaults
    for a node that declares none (`#address-cells` 2 / `#size-cells` 1, `IODTGetCellCounts`,
    `IODeviceTreeSupport.cpp:1034-1041`): under the defaults `num = length / (4 * cells)`, and a tree
    of `{address, size}` pairs that declares nothing resolves **zero** entries per node - the defect
    493 names, and the one a driver reading `getDeviceMemory()` can see and nothing else can, because
    `getNubResources` returns success on the path that resolved nothing;
  * the drivers to that declaration: the node's entry count has to be derived from the node's own `reg`
    length with the divisor the declaration implies, so the two definitions of the device's shape
    cannot move apart;
  * Apple's own file, whose `getNubResources` -> `IODTResolveAddressing` -> `gIODeviceMemoryKey` chain
    and identity address resolution (no `ranges` in this tree, `offset` still 0) are the mechanism the
    drivers' headers derive and the comparison rests on;
  * and the class the array's objects actually are. `withRange` is a blind cast of
    `IOMemoryDescriptor::withAddressRange`, which builds an `IOGeneralMemoryDescriptor` - a *sibling*
    of `IODeviceMemory`, not a subclass - so a driver that reads an entry as an `IODeviceMemory` reads
    nothing at all. 493's first device run did exactly that and reported the OS as having resolved
    nothing while its `_devcount` said three entries: the second occurrence of "the reader's type is a
    reading", in the same file, one step after 492's `_freqkind`.

**494's fifth part: the access, and the one value in it that has two definitions.** 493 left each
driver holding a number - `_phys0`, the address the kernel made of the node's `reg[0]` - and 494 turns
it into an access: `map( kIOMapAnywhere )` on the very object the OS's array holds, then a device word
read through `getVirtualAddress()`. Two more claims hold that:

  * the mapping to be the OS's - the mapped object is the object the entry was cast into (not a
    descriptor the driver built from the node's own words), the address comes from that map's own
    `getVirtualAddress()`, the length from its `getLength()`, all three are published, every word is
    read at an offset **named in the driver's own file** and resolved by this check, and both guards
    (`map != 0`, `vaddr != 0`) are present *and precede* the read - because a dereference of zero is a
    data abort whose `far` says nothing about the mapping;
  * the compared value to be two reads. `GICD_TYPER` is read-only and constant, so the driver can hold
    its mapped read against the payload's own read of the same register - and this check follows the
    chain from `gicd_read( STAGE90_GICD_TYPER )` through the variable to the non-`static` global the
    payload defines and its header declares, requires the driver's `extern` to name that symbol and its
    comparison to have that symbol on one side and the mapped read on the other, and compares the two
    definitions of the *offset*. Every link replaced by a constant is a refusal, because a comparison
    with a typed-in number is one no run can contradict. The driver that has no payload counterpart
    (`/timer`) is noted rather than failed: nothing here has read a word out of the GPT, and the check
    says so instead of inventing an expected value for it.

It reads sources and not the linked image on purpose: the image cannot say which provider class a
personality *would* have matched, only which services ended up matched, and 491's census is exactly the
case where the image looked correct and the driver tree was empty.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

BUILD_SCRIPT = os.path.join(REPO_ROOT, "tools", "build_xnu_arm_kernel.sh")
TABLE = os.path.join(REPO_ROOT, "stages", "stage90", "xnu_platform",
                     "stage90_platform_config_tables.c")
TREE_SOURCE = os.path.join(REPO_ROOT, "stages", "stage90", "stage90_main.c")
IOKIT = os.path.join(REPO_ROOT, "external", "xnu-4570.1.46", "iokit")

PLATFORM_EXPERT_CPP = os.path.join(IOKIT, "Kernel", "IOPlatformExpert.cpp")
PLATFORM_EXPERT_TREE_CPP = os.path.join(IOKIT, "Kernel", "IODeviceTreeSupport.cpp")
BSD_INIT_CPP = os.path.join(IOKIT, "bsddev", "IOKitBSDInit.cpp")
DEVICE_MEMORY_CPP = os.path.join(IOKIT, "Kernel", "IODeviceMemory.cpp")
MEMORY_DESCRIPTOR_CPP = os.path.join(IOKIT, "Kernel", "IOMemoryDescriptor.cpp")
DEVICE_MEMORY_H = os.path.join(IOKIT, "IOKit", "IODeviceMemory.h")
MEMORY_DESCRIPTOR_H = os.path.join(IOKIT, "IOKit", "IOMemoryDescriptor.h")

# 494: the payload's own GIC header and probe. The check reads them for one reason - the driver that
# compares its mapped read against the payload's read has to be held to the *payload's* definition of
# the register's offset and of the symbol that carries the value, and neither may be a transcription
# into the driver.
PAYLOAD_GIC_C = os.path.join(REPO_ROOT, "stages", "stage90", "xnu_arm_boot", "entry_gic.c")
PAYLOAD_GIC_H = os.path.join(REPO_ROOT, "stages", "stage90", "xnu_arm_boot", "entry_gic.h")

# 500: the payload file that *defines* the arming call, and the driver's side of the same ABI is a
# declaration in `entry_gic.h`. Three places hold one signature - the header, the definition, the
# driver's own `extern "C"` line - and the two files cannot see each other, so the comparison is this
# check's. That is claim 15's reason for the registry's two prototypes, applied to the call that makes a
# line arrive rather than the one that makes it serviceable.
PAYLOAD_IRQ_C = os.path.join(REPO_ROOT, "stages", "stage90", "xnu_arm_boot", "entry_irq.c")

# The two provider classes the kernel itself creates, and so the two a personality may name without any
# device-tree fact behind it: `IOPlatformExpertDevice` is the root nub `StartIOKit` builds
# (`IOPlatformExpert.cpp:1290-1300` via `new IOPlatformExpertDevice`) and `IOResources` is built by
# `IOService::setPlatform` and registered before the catalogue is first consulted.
KERNEL_CREATED = ("IOPlatformExpertDevice", "IOResources")

# Apple's designed fallback, the one entry of the stock table (`iokit/KernelConfigTables.cpp:35`) this
# project's table reproduces verbatim and last: it names a class this project does not define.
FALLBACK_CLASS = "IOPanicPlatform"

# 496: the six numbers the interrupt-controller driver writes its device with, paired with the header's
# name for the same number. One value with two definitions is this project's oldest defect class, and a
# *write* is where it costs the most: a read at the wrong offset returns a wrong number, a store at the
# wrong offset programs a different register - `GICD_ICPENDR0` is `GICD_ISPENDR0` + 0x80, so a sign or
# a digit wrong in one of these turns "clear my line's pending bit" into a write into the register that
# *sets* it. The two files cannot see each other (the driver cannot include the header, which is why it
# declares its own `extern "C"` prototypes), so the comparison is this check's.
DRIVER_GIC_REGISTER_PAIRS = (
    ("MSM8974_GICD_ISENABLER0_OFF", "STAGE90_GICD_ISENABLER0"),
    ("MSM8974_GICD_ISPENDR0_OFF", "STAGE90_GICD_ISPENDR0"),
    ("MSM8974_GICD_ICPENDR0_OFF", "STAGE90_GICD_ICPENDR0"),
    ("MSM8974_GICD_SGIR_OFF", "STAGE90_GICD_SGIR"),
    ("MSM8974_GICD_SGIR_SELF", "STAGE90_GICD_SGIR_TARGET_SELF"),
    ("MSM8974_GIC_OWN_INTID", "STAGE90_GIC_SGI0_ID"),
)

# The switch that decides whether any of those writes happen at all, as a `#define` in the driver's own
# source - the driver-side twin of `entry_irq.c`'s `STAGE90_IRQ_ENABLE_LINE`. Its value is published to
# the live buffer, so a run's log says which machine it was.
DRIVER_GIC_SWITCH = "MSM8974_GIC_DRIVE_SGI"


def read(path):
    with open(path, "r", errors="replace") as fh:
        return fh.read()


def strip_comments(text):
    """C and C++ comments out, so a class name in a comment is not a class this file defines.

    361's lesson, kept: a doc comment here quotes the table's own entries twice, and a `#if 0` block
    quoting a personality would otherwise read as one the build has.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def platform_sources(script):
    """The `PLATFORM_SOURCES` array of the build script, as absolute paths.

    The list is read rather than repeated because it *is* the definition of what this image links: a
    check with its own copy of it would keep passing after the build stopped compiling one of the files.
    """
    text = read(script)
    m = re.search(r"PLATFORM_SOURCES=\((.*?)\)", text, re.S)
    if not m:
        return None
    out = []
    for token in re.findall(r'"([^"]+)"', m.group(1)):
        token = token.replace("$REPO_ROOT", REPO_ROOT)
        out.append(os.path.normpath(token))
    return out


def c_strings(text):
    """Every C string literal in `text`, unescaped.

    The escapes matter here: the table's own `IOPlatformExpertDevice` entry quotes a name with a comma
    in it - `'IONameMatch' = \\"qcom,msm8974-xnu-stage90\\"` - because the plist lexer's unquoted rule
    accepts `[A-Za-z0-9-]` only (`OSUnserialize.y:298-317`). A reader that took the file's bytes for
    the plist text would compare `\\"qcom,...\\"` with the node's `compatible`, which is a different
    string, and the first version of this check did exactly that (defect 276).
    """
    out = []
    for literal in re.findall(r'"((?:[^"\\]|\\.)*)"', text, re.S):
        out.append(re.sub(r"\\(.)", r"\1", literal))
    return out


def node(tree_text, name):
    """One `apple_dt_node_begin(...)` block of the payload's tree, by the `name` it publishes.

    The payload writes the tree with `apple_dt_*` calls in one function; a node's properties are the
    `apple_dt_prop_*` calls between its `apple_dt_node_begin(` and the next one. The text is passed in
    rather than a path because the selftest mutates it.
    """
    return tree_nodes(tree_text).get(name)


def cell_count_defaults():
    """`IODTGetCellCounts`'s two defaults, read out of Apple's own file.

    This is the number that decides what a `reg` entry means to the kernel: the function answers `1`
    for `#size-cells` and `2` for `#address-cells` when the node declares neither
    (`IODeviceTreeSupport.cpp:1034-1041`). It is read rather than repeated because the whole of 493
    rests on it being Apple's default and not this tree's shape (defect 277's rule - a check that
    carries a copy of the value it checks is a check with an expiry date).
    """
    text = strip_comments(read(PLATFORM_EXPERT_TREE_CPP))
    m = re.search(r"void IODTGetCellCounts\s*\(.*?\n\{(.*?)\n\}", text, re.S)
    if not m:
        return None
    body = m.group(1)
    size = re.search(r"gIODTSizeCellKey\s*,\s*sizeCount\s*\)\s*\)\s*\n?\s*\*\s*sizeCount\s*=\s*(\d+)",
                     body)
    addr = re.search(r"gIODTAddressCellKey\s*,\s*addressCount\s*\)\s*\)\s*\n?\s*\*\s*addressCount\s*=\s*(\d+)",
                     body)
    if not size or not addr:
        return None
    return {"size": int(size.group(1)), "address": int(addr.group(1))}


def resolve_is_passthrough_without_ranges():
    """Whether `IODTResolveAddressCell` returns the entry's own address when nothing has `ranges`.

    The arm that matters is the one taken *before* any range search:
    `prop = regEntry->getProperty( gIODTRangeKey ); if( 0 == prop) { *phys = CellsValue(...); break; }`
    (`IODeviceTreeSupport.cpp:1090-1094`). With it, a tree that writes **absolute** addresses resolves
    them as absolute and needs no `ranges` anywhere above the node; without it, every device address in
    this tree would be a `ranges` the tree does not carry. The returned string is the assignment the
    function makes, so a check can assert it is the identity and not an offset.
    """
    # The raw text, not `strip_comments`: the arm is *identified* by its own comment
    # (`/* end of the road */`), so a reader that removed comments first could not find it.
    text = read(PLATFORM_EXPERT_TREE_CPP)
    m = re.search(r"bool IODTResolveAddressCell\s*\(.*?\n\{(.*?)\n\}", text, re.S)
    if not m:
        return None
    body = m.group(1)
    arm = re.search(r"0\s*==\s*prop\s*\)\s*\{\s*/\*\s*end of the road\s*\*/(.*?)break;", body, re.S)
    if not arm:
        return None
    # Comments inside the arm are removed, so the returned text is the statements it runs; whitespace
    # is collapsed so the claim below compares the statements and not the indentation they sit at.
    return re.sub(r"\s+", " ", strip_comments(arm.group(1))).strip()


def resolve_offset_initialiser():
    """What `IODTResolveAddressCell`'s `offset` starts at, read out of Apple's own file.

    The identity arm adds `offset` to the address it just decoded (`*phys += offset;`,
    `IODeviceTreeSupport.cpp:1094`), so the arm is only the identity if `offset` is still its
    initialiser when it runs. The only assignment to it is `offset += diff;` (`:1181`), inside the
    range-search branch the arm `break`s before - but "the only assignment is later in the function"
    is a fact about the file, so it is read rather than asserted in prose.
    """
    text = read(PLATFORM_EXPERT_TREE_CPP)
    m = re.search(r"UInt64\s+offset\s*=\s*(\d+)\s*;", text)
    return int(m.group(1)) if m else None


def table_entries(table):
    """The personalities of `gIOKernelConfigTables`, as `[{key: value}]` in the order they are written.

    The table is an old-style plist string parsed at run time by `OSUnserialize`; this reads the same
    text with the same shape - `{ 'Key' = value; ... }` - and keeps each value as its literal text, so
    `1616:32` stays one value rather than becoming arithmetic here. **Comments are stripped first**,
    because this file's own header reproduces the stock table inside a doc comment and the pattern
    below matches that copy just as well (defect 277).
    """
    text = strip_comments(read(table))
    m = re.search(r"gIOKernelConfigTables\s*=\s*(.*?);\s*$", text, re.S | re.M)
    if not m:
        return None
    # Every `"` string of the concatenated C literal, joined, is the plist text.
    body = "".join(c_strings(m.group(1)))
    entries = []
    for block in re.findall(r"\{(.*?)\}", body, re.S):
        entry = {}
        for key, value in re.findall(r"'(\w+)'\s*=\s*([^;]+);", block):
            entry[key] = value.strip()
        entries.append(entry)
    return entries


def names_of(value):
    """An `IONameMatch` value as a list of strings: `"qcom,msm-timer"` or `(timer, "qcom,msm-timer")`."""
    value = value.strip()
    if value.startswith("(") and value.endswith(")"):
        inner = value[1:-1]
    else:
        inner = value
    out = []
    for token in re.findall(r'"(.*?)"|([^,\s][^,]*)', inner):
        token = (token[0] or token[1]).strip()
        if token:
            out.append(token)
    return out


def defined_classes(source):
    """The classes a source defines, from `OSDefineMetaClassAndStructors(<class>, <super>)`."""
    text = strip_comments(read(source))
    return re.findall(r"OSDefineMetaClassAndStructors\s*\(\s*(\w+)\s*,", text)


def create_nub_class():
    """The class `IODTPlatformExpert::createNub` builds, read out of Apple's own file.

    This is the class every nub of this machine's device tree has, and so the only provider class a
    personality can name if it is to be found by a nub's lookup - `IOPlatformDevice` -> `IOService`.
    """
    text = strip_comments(read(PLATFORM_EXPERT_CPP))
    m = re.search(r"IOPlatformExpert::createNub\s*\(.*?\n\{(.*?)\n\}", text, re.S)
    if not m:
        return None
    body = m.group(1)
    found = re.findall(r"new\s+(\w+)", body)
    return found[0] if found else None


def published_resource():
    """The name `IOKitBSDInit` publishes before it waits, read out of Apple's own file."""
    text = strip_comments(read(BSD_INIT_CPP))
    m = re.search(r"publishResource\s*\(\s*\"([^\"]+)\"", text)
    return m.group(1) if m else None


def nub_resource_property():
    """The property `IODTPlatformExpert::getNubResources` resolves a nub's device memory from.

    493's drivers read the same property the OS did, and "the same" is a fact about Apple's file:
    `IODTResolveAddressing( nub, "reg", 0 )` (`IOPlatformExpert.cpp:1358-1366`). If that call ever
    resolved a different property, the drivers would be comparing their reading of one property with
    the OS's reading of another - the one-value-two-definitions shape, one layer down.
    """
    text = strip_comments(read(PLATFORM_EXPERT_CPP))
    m = re.search(r"IODTPlatformExpert::getNubResources\s*\(.*?\n\{(.*?)\n\}", text, re.S)
    if not m:
        return None
    m2 = re.search(r'IODTResolveAddressing\s*\(\s*\w+\s*,\s*"([^"]+)"', m.group(1))
    return m2.group(1) if m2 else None


def nub_resources_can_fail():
    """Whether any path of `getNubResources` returns something other than success.

    `IODTPlatformExpert::getNubResources` is

        if( nub->getDeviceMemory()) return( kIOReturnSuccess );
        IODTResolveAddressing( nub, "reg", 0);
        return( kIOReturnSuccess );

    - **success on the path where it resolved nothing**. So `doServiceMatch`'s
    `kIOReturnSuccess == getResources()` guard (`IOService.cpp:3724`) cannot be the thing that reports
    an empty resolution, and an empty `IODeviceMemory` array is invisible to the whole boot unless a
    driver reads it. That is why 493's keys exist rather than a status code being checked somewhere.
    """
    text = strip_comments(read(PLATFORM_EXPERT_CPP))
    m = re.search(r"IODTPlatformExpert::getNubResources\s*\(.*?\n\{(.*?)\n\}", text, re.S)
    if not m:
        return None
    return [r for r in re.findall(r"return\(\s*(kIOReturn\w+)", m.group(1))
            if r != "kIOReturnSuccess"]


def files_under_device_memory_key():
    """Whether `IODTResolveAddressing` is still the function that files the array the drivers read.

    The chain the drivers' headers state is `getDeviceMemory()` -> the array `setProperty(
    gIODeviceMemoryKey, array )` put on the nub (`IODeviceTreeSupport.cpp:1251`). This reads the
    filing out of Apple's file so the chain is checked at one end rather than assumed at both.
    """
    text = re.sub(r"\s+", " ", strip_comments(read(PLATFORM_EXPERT_TREE_CPP)))
    m = re.search(r"IODTResolveAddressing\( (.*?)return \(array\);", text, re.S)
    if not m:
        return None
    return "setProperty( gIODeviceMemoryKey, array)" in m.group(1)


def method_body(text, qualified):
    """The body of a definition of `<qualified>`, and not of a call to it.

    The distinction matters because `IOMemoryDescriptor::withAddressRange` occurs twice in its own
    file: once as the definition, and once as a call inside `withPhysicalAddress`'s body - where it is
    preceded by `(`. Two things separate them: a call's arguments end in `);`, so the `[^;]*?` before
    the opening brace cannot cross a call, and a call is preceded by `(`, `::`, `.` or `->` while a
    definition has its return type (`IOMemoryDescriptor *`, on the line above) before it.
    """
    for m in re.finditer(re.escape(qualified) + r"\s*\([^;]*?\n\{(.*?)\n\}", text, re.S):
        before = text[:m.start()].rstrip()
        if before.endswith(("(", "::", ".", "->")):
            continue
        return strip_comments(m.group(1))
    return None


def device_memory_factory():
    """`IODeviceMemory::withRange`'s cast and its factory, or None if it cannot be read.

    493's first device run read a `_devcount` of 3 beside a `_phys0` of 0, and this function is the
    first half of why: the array `IODTResolveAddressing` files holds objects built by the factory the
    blind cast below names, not by `IODeviceMemory`.
    """
    body = method_body(strip_comments(read(DEVICE_MEMORY_CPP)), "IODeviceMemory::withRange")
    if body is None:
        return None
    m = re.search(r"\(\s*(\w+)\s*\*\s*\)\s*(\w+)::(\w+)\s*\(", body)
    if not m:
        return None
    return {"cast_to": m.group(1), "factory_class": m.group(2), "factory_method": m.group(3)}


def built_class(factory_class, factory_method, depth=4):
    """The class a factory method ends up building, by following `new <class>` through its helpers.

    `withAddressRange` delegates to `withAddressRanges`, which does `new IOGeneralMemoryDescriptor`.
    Following the chain rather than naming the class here is what makes the claim below about *this*
    tree: if the factory ever built an `IODeviceMemory` after all, the drivers' casts would still be
    right and the check would say so instead of failing.
    """
    text = strip_comments(read(MEMORY_DESCRIPTOR_CPP))
    cls, method = factory_class, factory_method
    for _ in range(depth):
        body = method_body(text, "%s::%s" % (cls, method))
        if body is None:
            return None
        built = re.search(r"\bnew\s+(\w+)", body)
        if built:
            return built.group(1)
        nxt = re.search(r"(\w+)::(\w+)\s*\(", body)
        if not nxt:
            return None
        cls, method = nxt.group(1), nxt.group(2)
    return None


def class_chain(name):
    """`name` and its ancestors, out of the two headers that declare the classes in question."""
    parents = {}
    for header in (DEVICE_MEMORY_H, MEMORY_DESCRIPTOR_H):
        for derived, base in re.findall(r"(?m)^class\s+(\w+)\s*:\s*public\s+(\w+)",
                                        strip_comments(read(header))):
            parents[derived] = base
    chain, seen = [name], set()
    while chain[-1] in parents and chain[-1] not in seen:
        seen.add(chain[-1])
        chain.append(parents[chain[-1]])
    return chain


def reading_cast(source):
    """The class a driver reads the OS's memory-descriptor entry as.

    Not the driver's *declared* entry type - the class of the variable it actually calls
    `getPhysicalSegment` on, followed back to the `OSDynamicCast` that produced it. A driver may cast
    the same entry to other classes for other questions (the kind key does), and those casts are not
    the reading this claim is about.
    """
    m = re.search(r"(\w+)\s*->\s*getPhysicalSegment\s*\(", source)
    if not m:
        return None
    var = m.group(1)
    # Two spellings, because 494's drivers declare the entry's slot once at the top of `start` - the
    # map needs the same object the segment walk used, so it cannot be an inner declaration - and the
    # class of the cast is the reading either way. The declaration's type is reported when the source
    # names one, so a mutation that moves the cast *type* is still visible as a moved type.
    m2 = re.search(r"(\w+)\s*\*\s*%s\s*=\s*OSDynamicCast\(\s*(\w+)\s*," % re.escape(var), source)
    if m2:
        return {"var": var, "declared": m2.group(1), "cast_to": m2.group(2)}
    m2 = re.search(r"(?:^|\s)%s\s*=\s*OSDynamicCast\(\s*(\w+)\s*," % re.escape(var), source, re.M)
    if m2:
        return {"var": var, "declared": "", "cast_to": m2.group(1)}
    return None


def live_keys(source):
    """A driver's `entry_live_write` keys, as `{prefix: {suffix, ...}}`.

    The prefix is what makes two drivers' records comparable: 493's two drivers publish the same
    suffixes under different prefixes, so a key-by-key comparison of the two rows is a comparison of
    the same reading on two nodes rather than of two differently-shaped records.

    **The split is a property of the key format, not a guess about how wide a suffix is**, and this is
    the reading 495 had to correct: the first version cut every key at its *last* underscore, so
    `xnu_live_timerdrv_arm_lo` was filed under a prefix of its own and the driver appeared to write its
    record under seven prefixes the moment its keys grew a suffix of two words. The convention the
    live channel actually has is `xnu_live_<name>_<key>`, where `<name>` names the thing being
    recorded (a driver, the entry instrument, the trap) and `<key>` is the rest - which may contain
    underscores. A key that does not have that shape is not silently cut into one that does; it is
    filed under a prefix of its own, so a stray key is a *different* bucket and the caller refuses it.
    """
    out = {}
    for key in re.findall(r'entry_live_write\(\s*"([^"]+)"', source):
        m = re.match(r"^(xnu_live_\w+?)_(.+)$", key)
        if m:
            out.setdefault(m.group(1) + "_", set()).add(m.group(2))
        else:
            out.setdefault("<not of the form xnu_live_<name>_>", set()).add(key)
    return out


def resolve_condition(source):
    """The condition under which a driver sets `_resolve = 1`, as its expression text.

    That expression *is* the comparison this step is about, so reading it is how the claim below can
    require every number it names to be published: a driver that compares `len0` with the node's first
    size and does not publish either is a record whose `_resolve` = 2 cannot be explained by its own
    keys.
    """
    m = re.search(r"else\s+if\s*\(([^;{}]*)\)\s*resolve\s*=\s*1u\s*;", source, re.S)
    return m.group(1).strip() if m else None


def driver_defines(source):
    """A driver's `#define <NAME> <integer>` table, as `{name: value}`.

    The offsets a driver reads a device register at are named in its own file, and this is what turns
    those names into the numbers the claim compares. A `#define` whose value is not an integer (the
    drivers' own `#define super IOService`) is not in the table and cannot be resolved - which is the
    point of resolving at all: an offset written straight into the read expression has no name for a
    claim to look up, and a name that resolves to nothing is reported as such instead of passing.

    **A parenthesised shift of two literals counts as an integer**, and 496 is why: the register a
    driver raises an interrupt with is written as `MSM8974_GICD_SGIR_SELF (2u << 24)`, and leaving it
    unresolvable would have meant the claim that the driver's filter equals the payload's could not be
    made at all. `check_gic_routing.py`'s `defines` grew the same form in the same step, for the same
    value - the one place where two definitions of it are two parsers is a cost this project pays
    twice rather than a place to invent a third spelling.
    """
    out = {}
    for match in re.finditer(r"^#define[ \t]+(\w+)[ \t]+(.*)$", source, re.M):
        name = match.group(1)
        text = re.sub(r"[uU]\b", "", match.group(2).strip())
        if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", text):
            out[name] = int(text, 0)
            continue
        shift = re.fullmatch(r"\(\s*(0[xX][0-9a-fA-F]+|\d+)\s*<<\s*(0[xX][0-9a-fA-F]+|\d+)\s*\)", text)
        if shift:
            out[name] = int(shift.group(1), 0) << int(shift.group(2), 0)
    return out


def driver_mapping(source):
    """One driver's use of the OS's answer as a *device mapping*, as the pieces of that expression.

    Five things, and every one of them is a way the access could be something other than what the
    record claims:

      * `call` - `map( kIOMapAnywhere )` and the object it is called on. The object must be the one the
        driver cast the OS's array entry into: a driver that mapped a descriptor it built from the
        node's `reg` words would be reading an address it computed itself, which is the thing 493's
        driver comment rejects in as many words.
      * `mapvar`, `vaddr`, `vlen` - the three names, so the guards and the published keys can be
        checked against the expression that produced them rather than against a name this check
        assumes.
      * `reads` - the device words read through the *mapped* address, each as
        `(offset_symbol, whole_expression)`. The symbol is what the claim resolves against the file's
        own `#define`s, so an offset cannot be a number typed into the read and nothing else.
      * `writes` - the same for the device words **stored**: the registers a driver programs at, split
        from `reads` by whether the expression is followed by `=`. 496 is the first step where a driver
        writes its device at all, and a write is a stronger claim than a read - a wrong offset stores
        into an unrelated register instead of returning a wrong number - so it is the same *resolved
        symbol* requirement and a separate list, because the note a claim emits should say which of the
        two it is looking at.
    """
    out = {"call": None, "mapvar": None, "srcvar": None, "vaddr": None, "vaddrsrc": None,
           "vlen": None, "reads": [], "writes": [], "map_guards": []}
    m = re.search(r"(\w+)\s*=\s*(\w+)\s*->\s*map\(\s*kIOMapAnywhere\s*\)", source)
    if m:
        out["call"] = m.group(0)
        out["mapvar"] = m.group(1)
        out["srcvar"] = m.group(2)
    m = re.search(r"(\w+)\s*=\s*\(uint32_t\)\s*\(uintptr_t\)\s*(\w+)\s*->\s*getVirtualAddress\(\)", source)
    if m:
        out["vaddr"] = m.group(1)
        out["vaddrsrc"] = m.group(2)
    m = re.search(r"(\w+)\s*=\s*\(uint32_t\)\s*\w+\s*->\s*getLength\(\)", source)
    if m:
        out["vlen"] = m.group(1)
    if out["mapvar"]:
        out["map_guards"].append(re.search(r"if\(\s*%s\s*!=\s*0\)" % re.escape(out["mapvar"]), source))
    if out["vaddr"]:
        out["map_guards"].append(re.search(r"if\(\s*%s\s*!=\s*0u\)" % re.escape(out["vaddr"]), source))
    if out["vaddr"]:
        pattern = (r"\*\s*\(\s*volatile\s+uint32_t\s*\*\s*\)\s*\(\s*uintptr_t\s*\)\s*"
                   r"\(\s*%s\s*(?:\+\s*(\w+)\s*)?\)" % re.escape(out["vaddr"]))
        for m in re.finditer(pattern, source):
            stored = re.match(r"\s*=", source[m.end():])
            out["writes" if stored else "reads"].append((m.group(1), m.group(0)))
    return out


def payload_defines(header_text, prefix):
    """`{NAME: value}` for the payload header's `#define <NAME> <integer>` whose name starts `prefix`.

    The third form is `(2u << 24)`, added in 496 with `check_gic_routing.py`'s `defines` for the same
    reason and in the same shape: `GICD_SGIR`'s target-self filter is a *derivation*, both files spell
    it as one, and leaving it unparsed would have made the comparison a spelling rather than a value -
    which passes for `(2u << 24)` and `(2u << 23)` alike until either file is reformatted.
    """
    out = {}
    for name, value in re.findall(r"#define\s+(%s\w*)\s+"
                                  r"(\(?\s*(?:0[xX][0-9a-fA-F]+|\d+)[uU]?\s*<<\s*"
                                  r"(?:0[xX][0-9a-fA-F]+|\d+)[uU]?\s*\)?|0[xX][0-9a-fA-F]+|\d+)[uU]?"
                                  % re.escape(prefix), header_text):
        text = re.sub(r"[uU]\b", "", value).strip()
        shift = re.fullmatch(r"\(\s*(0[xX][0-9a-fA-F]+|\d+)\s*<<\s*(0[xX][0-9a-fA-F]+|\d+)\s*\)", text)
        if shift:
            out[name] = int(shift.group(1), 0) << int(shift.group(2), 0)
        else:
            out[name] = int(text, 0)
    return out


def payload_typer_channel(c_text, h_text):
    """The payload's own reading of the register the GIC driver compares against, as a chain.

    Four links, and the claim below fails on the first one that is a constant instead of a read: the
    symbol must be **defined** in the payload's C (a `static` one would not link, so the driver's
    `extern` would be an undefined symbol and the build would say so), **declared** in the payload's
    header (one definition, one name), assigned from the variable the probe read the register into, and
    that variable must itself be assigned from `gicd_read( <macro> )` where `<macro>` is a name the
    header defines as an integer. A chain that ends in a number makes the driver's `_hwok` a comparison
    with a value someone typed, which no run could falsify.
    """
    out = {"symbol": None, "rhs": None, "macro": None, "offset": None, "declared": False}
    m = re.search(r"^uint32_t\s+(g_\w+)\s*;", c_text, re.M)
    if not m:
        return out
    out["symbol"] = m.group(1)
    m = re.search(r"^\s*%s\s*=\s*(\w+)\s*;" % re.escape(out["symbol"]), c_text, re.M)
    if not m:
        return out
    out["rhs"] = m.group(1)
    m = re.search(r"^\s*%s\s*=\s*gicd_read\(\s*(\w+)\s*\)\s*;" % re.escape(out["rhs"]), c_text, re.M)
    if not m:
        return out
    out["macro"] = m.group(1)
    out["offset"] = payload_defines(h_text, "STAGE90_GICD_").get(out["macro"])
    out["declared"] = bool(re.search(r"extern\s+uint32_t\s+%s\s*;" % re.escape(out["symbol"]), h_text))
    return out


def driver_payload_symbol(source):
    """The payload symbol one driver declares `extern`, and the comparison it makes against it.

    The driver that has a second definition is found by *that declaration* and not by its class name,
    so a third device driver that gains a payload-side counterpart is checked by adding the declaration
    and nothing else - the same rule 493's `facts["drivers"]` follows for the personalities.
    """
    m = re.search(r'extern\s+"C"\s+uint32_t\s+(\w+)\s*;', source)
    if not m:
        return None
    symbol = m.group(1)
    out = {"symbol": symbol, "lhs": None, "rhs": None, "verdict": None}
    m = re.search(r"(\w+)\s*=\s*\(\s*(\w+)\s*==\s*(\w+)\s*\)\s*\?\s*1u\s*:\s*0u\s*;", source)
    if m:
        out["verdict"] = m.group(1)
        out["lhs"] = m.group(2)
        out["rhs"] = m.group(3)
    return out


def driver_timeout(source):
    """One driver's use of the OS's own timeout machinery, as the pieces of the chain.

    The step's subject: the kernel calls a driver back. Six links, and each one is a way the record
    could still say "the driver armed a timeout" while no timeout exists:

      * `workloop` - the variable `IOWorkLoop::workLoop()` was assigned to. `IOWorkLoop::init()`
        (`IOWorkLoop.cpp:165-172`) is where `kernel_thread_start` happens, so a non-NULL one here is
        the reason a kernel thread exists at all.
      * `timer` and `action` - the event source and **the function handed to it**. The action is read
        out of the `timerEventSource(...)` call rather than assumed by name, so a driver whose
        callback is wired to something else is refused - and the function's own definition is then
        read, because a name that is never defined is a callback that cannot run.
      * `added_loop` - the object `addEventSource` was called on, which must be the work loop above.
        `IOTimerEventSource::wakeAtTime` arms nothing unless the source has a `workLoop`
        (`IOTimerEventSource.cpp:476`), which `addEventSource` is what gives it.
      * `enabled_at` - the source offset of the `enable()` call and of the first `setTimeout`, in that
        order. A disabled source stores the time and arms nothing.
      * `armed` - the interval *name* passed to `setTimeout`, resolved against the driver's own
        `#define`s, and `deadline` - the interval name passed to `clock_interval_to_deadline`. The two
        must be the same name: the moment the callback should run and the moment it was asked to run
        are one decision.
      * `within_action` - the body of the callback, with the interval name it re-arms with and the keys
        it publishes, so "the callback says it ran" and "the callback keeps the timer alive" are read
        out of the function that actually runs rather than out of the file at large.
    """
    out = {"workloop": None, "timer": None, "action": None, "owner": None, "added_loop": None,
           "enabled_at": None, "armed_at": None, "armed": None, "deadline": None, "deadline_var": None,
           "action_def": None, "rearm": None, "action_keys": [], "file_keys": [],
           "fires_zero_at": None, "arming_at": None}
    m = re.search(r"(\w+)\s*=\s*IOWorkLoop::workLoop\(\)\s*;", source)
    if m:
        out["workloop"] = m.group(1)
        out["arming_at"] = m.start()
    m = re.search(r"(\w+)\s*=\s*IOTimerEventSource::timerEventSource\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                  source)
    if m:
        out["timer"], out["owner"], out["action"] = m.group(1), m.group(2), m.group(3)
    m = re.search(r"(\w+)\s*->\s*addEventSource\(\s*(\w+)\s*\)", source)
    if m:
        out["added_loop"] = m.group(1)
    # `enable()` and the arming `setTimeout` are searched **from the work loop forward**, and the
    # offsets are kept absolute: the callback is defined above `start` and re-arms from its own body, so
    # a whole-file search for the first `setTimeout` finds the *re-arm* and reports an order that is
    # about two different regions of the file (the reading 495's first run of this claim got wrong).
    if out["arming_at"] is not None:
        region = source[out["arming_at"]:]
        base = out["arming_at"]
        m = re.search(r"(\w+)\s*->\s*enable\(\)\s*;", region)
        if m:
            out["enabled_at"] = base + m.start()
        m = re.search(r"(\w+)\s*->\s*setTimeout\(\s*(\w+)\s*,\s*kMillisecondScale\s*\)", region)
        if m:
            out["armed"], out["armed_at"] = m.group(2), base + m.start()
        m = re.search(r"clock_interval_to_deadline\(\s*(\w+)\s*,\s*kMillisecondScale\s*,\s*&(\w+)\s*\)",
                      region)
        if m:
            out["deadline"], out["deadline_var"] = m.group(1), m.group(2)
    if out["action"]:
        m = re.search(r"(?:static\s+void\s+)?\b%s\s*\(\s*OSObject\s*\*\s*owner\s*,\s*"
                      r"IOTimerEventSource\s*\*\s*sender\s*\)\s*\{" % re.escape(out["action"]), source)
        if m:
            body = _braced(source, m.end() - 1)
            out["action_def"] = body
            rearm = re.search(r"(\w+)\s*->\s*setTimeout\(\s*(\w+)\s*,\s*kMillisecondScale\s*\)", body)
            if rearm:
                out["rearm"] = rearm.group(2)
            out["action_keys"] = re.findall(r'"(xnu_live_\w+)"', body)
    out["file_keys"] = re.findall(r'"(xnu_live_\w+)"', source)
    m = re.search(r'entry_live_write\(\s*"(xnu_live_\w*_fires)"\s*,\s*0u\s*\)', source)
    if m:
        out["fires_zero_at"] = m.start()
    return out


def driver_irq_client(source):
    """One driver's registration for an interrupt line, as the pieces of the call it makes.

    496's subject, and the call is read rather than assumed: the intid (a *name*, which the claim
    resolves against the driver's own `#define`s and against the payload's header), the function whose
    address is handed over, and the `refCon`. The address is an `&`-expression and not a bare name, so
    a driver that registered the *value* of a pointer variable - or a function it calls at
    registration time - is a different shape and is refused rather than silently accepted.

    `rc` is the variable the return is stored in, and it is here because the request that follows must
    be guarded on it: a driver that raises an interrupt on a line no client is filed for is poking the
    one dispatcher path this project has measured to stop the run.

    **500 widened one group of this reader, and why is a finding in its own right.** The `refCon` was
    read as `(uint32_t)(uintptr_t)<name>` - the shape Apple's own route produces and the shape the
    interrupt-controller driver writes - so `MSM8974Timer.cpp`'s
    `entry_irq_register_client( line, ..., 0u )` did not match at all. 498's registration was real,
    was measured (`_line_cli_rc = 1`), and was *invisible to the claim that reads registrations*, whose
    note then said about that file the opposite of what 498 had just done to it ("owns no interrupt
    line"). The cast is now optional and the argument's text is what is captured, so a driver whose
    `refCon` is its own choice is read; whether *this* driver's `refCon` is the right one is a separate
    rule, and it is claim 15's, because it is a fact about Apple's second-level route rather than about
    the call's shape.
    """
    out = {"intid": None, "handler": None, "refcon": None, "rc": None, "at": None}
    m = re.search(r"(\w+)\s*=\s*entry_irq_register_client\(\s*(\w+)\s*,\s*"
                  r"\(uint32_t\)\s*\(uintptr_t\)\s*&\s*(\w+)\s*,\s*"
                  r"(?:\(\s*uint32_t\s*\)\s*\(\s*uintptr_t\s*\)\s*)?(\w+)\s*\)\s*;", source)
    if not m:
        return out
    out["rc"], out["intid"] = m.group(1), m.group(2)
    out["handler"], out["refcon"] = m.group(3), m.group(4)
    out["at"] = m.start()
    return out


def driver_arming_calls(source):
    """Every `entry_irq_enable_line( <intid>, <target> )` *call* in a driver's source.

    500's call, and both arguments are read as *text* rather than resolved here: the claim below holds
    the intid against the variable the driver registered with and the target against the driver's own
    `#define`s, and a reader that resolved either of them would be a second definition of the question.

    **A declaration is not a call, and that test is `method_body`'s.** The driver's own `extern "C"`
    prototype has the same text as the call with the same two names inside it, and the first version of
    this reader took it for the first arming - which made every order claim below subtract two source
    offsets in the wrong direction and report the driver as arming the line before the registration that
    precedes it. What separates them is the return type before the name, which a call does not have.
    """
    out = []
    for m in re.finditer(r"entry_irq_enable_line\(\s*([^,()]+?)\s*,\s*([^,()]+?)\s*\)\s*;", source):
        before = source[:m.start()].rstrip()
        if re.search(r'(?:extern\s+"C"\s+)?\b(?:uint32_t|uint64_t|int|void)\s*$', before):
            continue
        out.append((m.group(1).strip(), m.group(2).strip(), m.start()))
    return out


def key_writes(source, suffix):
    """Every write of the live key ending in `_<suffix>`, as `(value_text, index)`, in source order.

    The value is read as text for the same reason 493's reader reads the resolution *condition* as text:
    the question the claims ask is which of several writes is the one where the value is known, and that
    is a question about the file rather than about the number. `[^;]*?` and not `.*?` because a value
    can contain parentheses (`(uint32_t)(uintptr_t) &handler`) and cannot contain a `;`.
    """
    return [(m.group(2).strip(), m.start()) for m in re.finditer(
        r'entry_live_write\(\s*"(xnu_live_\w+_%s)"\s*,\s*([^;]*?)\s*\)\s*;' % re.escape(suffix),
        source, re.S)]


def value_expression(source, name):
    """The expression of the **last** statement that gives `name` a value, or None if there is none.

    A deadline that does not fit the register is computed in two steps on purpose - the product of a
    64-bit frequency and a millisecond count is not `CNTP_TVAL`'s width - so a claim that only read the
    published name's right-hand side would see `want` and could not tell a derivation from a literal
    that happens to be spelled like one. Reading the definition of the name is the same move the rest of
    this file makes when a value's *source* is what matters.

    **"The last" and not "the first, unless it is a declaration" - and both halves of that are
    findings.** The first version of this reader skipped anything a return type preceded, which is the
    test that separates a declaration from a call; here it is the *wrong* test twice over. `want`'s only
    occurrence is its declaration, so skipping it left the chain unreadable and the claim reported a
    file that derives its deadline from the frame's frequency as deriving it from something else. And
    `arm_ticks` has both a declaration (with a zero initialiser, which is what a record looks like
    before the work) and an assignment, so taking the first would have read the zero. The last write in
    source order is the value the name holds when the code that follows it runs, which is the question a
    device store after it is asking. **A name written on two branches** is the case this reader cannot
    answer, and it says so rather than picking one: the stores would have to be read with their guards.

    **`=(?!=)`, and the first version of this reader read a comparison as an assignment.** `arm_ticks ==
    want` matched `\s*=\s*` on the first of its two `=`, and what the class then captured ran through a
    comment - no `;` in it - to the `;` of the next statement, so the "deadline" answered for was a
    paragraph of prose. The claim that reads it reported a file whose deadline *is* written down as
    deriving it from the frame's frequency: the mutation this clause exists to refuse was accepted, for
    the third time in this step by a reader that had to learn the difference between a thing and
    something spelled nearly like it.
    """
    found = None
    for m in re.finditer(r"\b%s\s*=(?!=)\s*([^;]+);" % re.escape(name), source):
        found = m.group(1).strip()
    return found


def driver_device_stores(source, base):
    """Every store to `base + <named offset>`, as `(offset_symbol, expression, index)`.

    The other half of `driver_mapping`'s `reads`, and here it is the *whole* file rather than `start`'s
    body: 496's driver programs the distributor from two contexts - the enable and the first request
    from process context, and the next request from **inside its own handler** - so the claim has to be
    able to see a store that is in a different function. The offsets are symbols so that each one can
    be resolved and held against the payload's header; `index` is the source offset, which is what the
    order claims are made of.
    """
    out = []
    if not base:
        return out
    pattern = (r"\*\s*\(\s*volatile\s+uint32_t\s*\*\s*\)\s*\(\s*uintptr_t\s*\)\s*"
               r"\(\s*%s\s*\+\s*(\w+)\s*\)\s*=\s*([^;]+);" % re.escape(base))
    for m in re.finditer(pattern, source):
        out.append((m.group(1), m.group(2).strip(), m.start()))
    return out


def preprocessor_arm(text, macro):
    """What follows `#if <macro>` up to its `#else`/`#endif`, or None if there is no such block.

    The same helper `check_irq_routing.py` has, for the same reason: a switch that says which machine
    was built is only worth reading if the code it switches is *inside* it, and "is it inside" is a
    question about the preprocessor and not about the source's shape.
    """
    m = re.search(r"^#if\s+%s\s*$(.*?)^#(?:else|endif)" % re.escape(macro), text, re.M | re.S)
    return m.group(1) if m else None


def _braced(text, start):
    """The `{...}` block that begins at `text[start]`, matched by nesting (and over strings loosely)."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return text[start:]


def tree_nodes(tree_text):
    """Every `apple_dt_node_begin(...)` block, as `{name: {property: value}}`."""
    out = {}
    for block in strip_comments(tree_text).split("apple_dt_node_begin")[1:]:
        props = {}
        for kind, key, value in re.findall(
                r'apple_dt_prop_(\w+)\s*\(\s*\w+\s*,\s*"([^"]+)"\s*,\s*(.*?)\);', block):
            if kind in ("u32", "u32_array"):
                props[key] = value.strip()
            else:
                m = re.fullmatch(r'"([^"]*)"', value.strip())
                props[key] = m.group(1) if m else value.strip()
        if "name" in props:
            out[props["name"]] = props
    return out


def read_int(text):
    """A C integer literal of this tree's style, `19200000u` included."""
    m = re.match(r"\s*(\d+)", text)
    return int(m.group(1)) if m else None


def c_int_array(tree_text, prop_name):
    """The `apple_dt_prop_u32_array(b, "<prop_name>", <array>, ...)` initialisers, as lists of ints.

    The array the call passes is resolved against the same file's own `static const uint32_t <name>[]`
    declarations, so the check reads the *values the tree writes* and not a transcription of them.
    """
    text = strip_comments(tree_text)
    arrays = {}
    for name, body in re.findall(r"static const uint32_t (\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        arrays[name] = [int(t.rstrip("uU"), 0) for t in re.findall(r"0x[0-9a-fA-F]+u?|\d+u?", body)]
    out = []
    for passed in re.findall(r'apple_dt_prop_u32_array\(\s*\w+\s*,\s*"%s"\s*,\s*(\w+)' % prop_name,
                             text):
        out.append((passed, arrays.get(passed, [])))
    return out


def gather(args):
    facts = {
        "table": read(args.table),
        "tree": read(args.tree_source),
        "sources_text": {},
    }
    facts["sources"] = platform_sources(args.build_script)
    facts["entries"] = table_entries(args.table)
    facts["classes"] = {}
    for path in (facts["sources"] or []):
        facts["classes"][path] = defined_classes(path)
        facts["sources_text"][os.path.basename(path)] = read(path)
    facts["nub_class"] = create_nub_class()
    facts["resource"] = published_resource()
    facts["nub_resource_prop"] = nub_resource_property()
    facts["nub_resources_failures"] = nub_resources_can_fail()
    facts["files_devmem_key"] = files_under_device_memory_key()
    facts["nodes"] = tree_nodes(facts["tree"])
    facts["root"] = facts["nodes"].get("/")
    facts["cell_defaults"] = cell_count_defaults()
    facts["resolve_passthrough"] = resolve_is_passthrough_without_ranges()
    facts["resolve_offset"] = resolve_offset_initialiser()
    facts["devmem_factory"] = device_memory_factory()
    facts["devmem_built"] = (built_class(facts["devmem_factory"]["factory_class"],
                                         facts["devmem_factory"]["factory_method"])
                             if facts["devmem_factory"] else None)
    facts["devmem_chain"] = class_chain(facts["devmem_built"]) if facts["devmem_built"] else None
    facts["payload_gic_c"] = read(PAYLOAD_GIC_C)
    facts["payload_gic_h"] = read(PAYLOAD_GIC_H)
    facts["payload_irq_c"] = read(PAYLOAD_IRQ_C)
    facts["drivers"] = []

    # 493: one record per personality that names a *device node*, with the file that defines its class
    # and the tree node its names point at. The pairing is what the claims below iterate over, so a
    # third device personality is checked by adding it to the table and nothing else.
    owner = {}
    for path, classes in facts["classes"].items():
        for name in classes:
            owner[name] = path
    for entry in (facts["entries"] or []):
        if entry.get("IOProviderClass") != facts["nub_class"]:
            continue
        klass = entry.get("IOClass")
        path = owner.get(klass)
        if path is None:
            continue                      # claim_classes reports this
        names = names_of(entry.get("IONameMatch", ""))
        matched = None
        for node_name, props in facts["nodes"].items():
            if props.get("name") in names or props.get("compatible") in names:
                matched = node_name
                break
        facts["drivers"].append({
            "class": klass,
            "file": os.path.basename(path),
            "path": path,
            "source": facts["sources_text"].get(os.path.basename(path), ""),
            "names": names,
            "node_name": matched,
            "node": facts["nodes"].get(matched) if matched else None,
        })
    return facts


def entries_of(facts, failures):
    if facts["entries"] is None:
        failures.append("`gIOKernelConfigTables` is no longer a string this check can read: the "
                        "kernel would call `IOCatalogue::initialize` with no personalities at all")
        return []
    if not facts["entries"]:
        failures.append("`gIOKernelConfigTables` parses to no entries")
    return facts["entries"]


def claim_shape(facts, failures, notes):
    """1. Every entry names a class and a provider class."""
    entries = entries_of(facts, failures)
    for i, entry in enumerate(entries):
        for key in ("IOClass", "IOProviderClass"):
            if key not in entry:
                failures.append("entry %d of the table has no `%s`: a personality without it is filed "
                                "under nothing and instantiates nothing" % (i, key))
    if entries and not any(e.get("IOClass") == FALLBACK_CLASS for e in entries):
        failures.append("Apple's fallback (`%s` under `IOPlatformExpertDevice`) is gone from the "
                        "table: a machine whose platform expert does not match would then boot with "
                        "no platform expert instead of panicking with its name (experiment 362)"
                        % FALLBACK_CLASS)
    if entries:
        notes.append("the table has %d entries: %s" % (len(entries), ", ".join(
            "%s<- %s" % (e.get("IOClass", "?"), e.get("IOProviderClass", "?")) for e in entries)))


def claim_classes(facts, failures, notes):
    """2. Every class the table names is defined by exactly one file of `PLATFORM_SOURCES`."""
    entries = entries_of(facts, failures)
    sources = facts["sources"]
    if sources is None:
        failures.append("`PLATFORM_SOURCES` is gone from `tools/build_xnu_arm_kernel.sh`, so the list "
                        "of files this image compiles cannot be read; a class name and the class it "
                        "names would then be compared by eye")
        return
    if not sources:
        failures.append("`PLATFORM_SOURCES` is empty: no class this table names could be defined")
        return

    owners = {}
    for path, classes in facts["classes"].items():
        for name in classes:
            owners.setdefault(name, []).append(os.path.basename(path))
    facts["owners"] = owners

    for entry in entries:
        name = entry.get("IOClass")
        if name is None or name == FALLBACK_CLASS:
            continue
        if name not in owners:
            failures.append("the table names `%s` and no file in `PLATFORM_SOURCES` defines it: "
                            "`OSMetaClass::allocClassWithName` would fail at match time, print "
                            "nothing and start nothing" % name)
        elif len(owners[name]) > 1:
            failures.append("`%s` is defined by %d files (%s): one class name is one class, and two "
                            "definitions of it is the shape this project keeps meeting"
                            % (name, len(owners[name]), ", ".join(owners[name])))

    # And the other direction. A class compiled into the image that no personality names is
    # unreachable - nothing can ever supply it as `IOClass` - which is the defect 491's census
    # measured from the service side, stated here from the catalogue side.
    named = set(e.get("IOClass") for e in entries)
    for name in sorted(owners):
        if name not in named:
            failures.append("`%s` is compiled into this image by `PLATFORM_SOURCES` and no "
                            "personality names it: nothing can match it, so it is an object linked "
                            "for nothing" % name)


def claim_provider(facts, failures, notes):
    """3. The device-tree entry's provider class is the class `createNub` builds."""
    entries = entries_of(facts, failures)
    nub = facts["nub_class"]
    if nub is None:
        failures.append("`IODTPlatformExpert::createNub` no longer builds a class this check can "
                        "read out of `IOPlatformExpert.cpp`: the nub class the personalities have to "
                        "name is then a guess")
        return
    notes.append("Apple's `createNub` builds `%s`, so that is the class a nub's lookup chain is "
                 "searched under" % nub)

    device = [e for e in entries if e.get("IOProviderClass") not in KERNEL_CREATED]
    if not device:
        failures.append("no personality's `IOProviderClass` is the class `createNub` builds (`%s`): "
                        "every nub this machine's device tree produces is that class, so with the "
                        "table as it is `findDrivers` answers every one of them with an empty set "
                        "and no device driver can start - the state experiment 491 measured" % nub)
        return
    for entry in device:
        got = entry.get("IOProviderClass")
        if got != nub:
            failures.append("the table files `%s` under `IOProviderClass = %s`, which is neither a "
                            "class the kernel creates (%s) nor the class `createNub` builds (`%s`): "
                            "no service's class chain reaches that bucket"
                            % (entry.get("IOClass"), got, "/".join(KERNEL_CREATED), nub))
    reached = [e for e in entries if e.get("IOProviderClass") == nub]
    notes.append("%d personality(ies) filed under `%s`: %s"
                 % (len(reached), nub, ", ".join(e.get("IOClass", "?") for e in reached)))


def claim_names(facts, failures, notes):
    """4. Every device personality's names are its node's names and its driver's own.

    493 made this a loop over `facts["drivers"]` rather than a check of one personality: the claim is
    about the *layer*, and a second driver added to the table is then checked by being added and
    nothing else. The three comparisons inside are each one value with two definitions - the
    personality's `IONameMatch` against the tree node's `name`/`compatible`, the same list against the
    names the driver's own `IONameMatched` reader knows, and the properties the driver reads against
    the properties the node publishes.
    """
    entries = entries_of(facts, failures)
    nub = facts["nub_class"]
    if nub is None or not facts["drivers"]:
        return                      # claim 3 has already said why
    by_class = dict((e.get("IOClass"), e) for e in entries)

    for d in facts["drivers"]:
        klass = d["class"]
        entry = by_class.get(klass, {})
        node_props = d["node"]
        if node_props is None:
            failures.append("no node of the payload's tree carries a `name` or `compatible` in "
                            "`%s`'s `IONameMatch` %s: the candidate test is `IODTCompareNubName` over "
                            "the provider nub's own properties, so this personality could be probed "
                            "against every nub and match none of them"
                            % (klass, d["names"]))
            continue
        if "IONameMatch" not in entry:
            failures.append("`%s` has no `IONameMatch`: without it the probe cannot name the "
                            "provider, so the personality would match nothing even in the right "
                            "bucket" % klass)
            continue
        # The personality's own value as the table has it now, not the record's copy of it: the
        # selftest's mutations move the table, and a claim that read `d["names"]` would then be
        # comparing the record with itself (the shape 493's own driver pair exists to rule out).
        got = names_of(entry["IONameMatch"])
        node_names = [node_props.get("name"), node_props.get("compatible")]
        if sorted(got) != sorted(n for n in node_names if n):
            failures.append("`%s`'s `IONameMatch` is %s and the `/%s` node's `name`/`compatible` "
                            "are %s: the candidate test reads the provider's own properties, so a "
                            "name the node does not carry is a probe that answers false"
                            % (klass, got, d["node_name"], node_names))

        # The driver's own list, which is the one that decides whether a match on a name this driver
        # does not know reads as a match at all (`IONameMatched`).
        compared = re.findall(r'isEqualTo\(\s*"([^"]+)"\s*\)', d["source"])
        if sorted(compared) != sorted(got):
            failures.append("`%s` compares `IONameMatched` against %s while the personality lists "
                            "%s: one of the two lists moved, and a match on a name the driver does "
                            "not know is worse than no match - it starts the driver on the wrong fact"
                            % (d["file"], compared, got))
        else:
            notes.append("`%s`'s names are the `/%s` node's and the driver's: %s"
                         % (klass, d["node_name"], ", ".join(got)))

        # And the properties the driver reads have to be properties the node has: a driver reading a
        # property nothing writes records a zero, which reads the same as a zero the device has.
        read_props = sorted(set(re.findall(r'provider->getProperty\(\s*"([^"]+)"\s*\)',
                                          d["source"])))
        missing = [p for p in read_props if p not in node_props]
        if missing:
            failures.append("`%s` reads %s from its provider and the `/%s` node publishes no such "
                            "property(ies): the driver would record a zero and count it as a reading"
                            % (d["file"], ", ".join(missing), d["node_name"]))
        else:
            if read_props:
                notes.append("every property `%s` reads is published by `/%s`: %s"
                             % (d["file"], d["node_name"], ", ".join(read_props)))
            else:
                notes.append("`%s` reads no property of its provider" % d["file"])

        # A frequency the driver compares with the machine's own has to have a left side that is an
        # integer of the payload's own style, or the comparison is against a reader's zero.
        if "frequency" in read_props and read_int(node_props.get("frequency", "")) is None:
            failures.append("the `/%s` node's `frequency` is not an integer literal of the "
                            "payload's own style, so the comparison the driver makes has no left "
                            "side" % d["node_name"])


def claim_root_names(facts, failures, notes):
    """5. The platform expert's `IONameMatch` is the root node's `compatible`."""
    entries = entries_of(facts, failures)
    root = facts["root"]
    if root is None:
        failures.append("the payload's tree has no node named `/`: the root's `compatible` is what "
                        "`IODTPlatformExpert::probe` compares this table's first entry against")
        return
    for entry in entries:
        if entry.get("IOClass") == FALLBACK_CLASS:
            continue
        if entry.get("IOProviderClass") != "IOPlatformExpertDevice":
            continue
        got = names_of(entry.get("IONameMatch", ""))
        if root.get("compatible") not in got:
            failures.append("the table's `%s` entry matches %s and the tree's root is "
                            "`compatible = %s`: `IODTPlatformExpert::probe` requires the driver's "
                            "`IONameMatch` to name the provider, so this entry would fall through to "
                            "`%s` (experiment 362's panic)"
                            % (entry.get("IOClass"), got, root.get("compatible"), FALLBACK_CLASS))
        else:
            notes.append("`%s` matches the root's `compatible`" % entry.get("IOClass"))

    # A resource personality names a resource the OS publishes rather than a node in the tree, so its
    # name is checked against the file that publishes it - the other end of the same wait.
    published = facts["resource"]
    for entry in entries:
        if entry.get("IOProviderClass") != "IOResources":
            continue
        want = entry.get("IOResourceMatch")
        if want is None:
            failures.append("the `IOResources` personality `%s` has no `IOResourceMatch`: its own "
                            "candidate test is what asks for the resource, so without the key the "
                            "match is decided by the score alone" % entry.get("IOClass"))
        elif want != published:
            failures.append("the `IOResources` personality asks for `%s` and the OS publishes `%s` "
                            "(`IOKitBSDInit.cpp`): a resource nobody publishes is a driver that "
                            "never starts and a wait that times out" % (want, published))
        else:
            notes.append("the `IOResources` personality asks for the resource the OS publishes: `%s`"
                         % published)


def claim_bundle_id(facts, failures, notes):
    """7. No personality carries a `CFBundleIdentifier`."""
    entries = entries_of(facts, failures)
    for entry in entries:
        if "CFBundleIdentifier" in entry:
            failures.append("`%s` carries a `CFBundleIdentifier`: `IOService::probeCandidates` stalls "
                            "on `gIOCatalogue->isModuleLoaded(match)` and that answers *true* for "
                            "exactly the personalities with no bundle id - 'assumed to be an "
                            "in-kernel driver' (`IOCatalogue.cpp:475-496`) - so a bundle id on a "
                            "driver linked into this image is a match that never happens, silently"
                            % entry.get("IOClass"))
    if entries:
        notes.append("no personality carries a `CFBundleIdentifier`, so every one of them is assumed "
                     "to be an in-kernel driver")


def claim_property_kinds(facts, failures, notes):
    """6. A device-tree property is read as the kind the device-tree plane produces.

    Every property of a device-tree node becomes an **`OSData`** when the node is turned into a
    registry entry - `data = OSData::withBytes( prop, propSize )` (`IODeviceTreeSupport.cpp:379`), for
    the node's `name` as much as for its `frequency`. So a driver that reaches for one with
    `OSDynamicCast( OSNumber, provider->getProperty( name ))` finds nothing, and - the reason this is a
    check and not a comment - the record it leaves reads "the tree has no such property", which is a
    statement about the tree. 492's first device run is exactly that: `_have = 2`, `_freq = 0`, with
    `reg` read fine beside it, and nothing in the log saying the cast was the wrong kind.
    """
    checked = 0
    for d in facts["drivers"]:
        node_props = d["node"]
        if node_props is None:
            continue                    # claim 4 has said this
        for prop in sorted(set(re.findall(r'provider->getProperty\(\s*"([^"]+)"\s*\)', d["source"]))):
            if prop not in node_props:
                continue                # claim 4 has said this
            checked += 1
            if not re.search(r'OSDynamicCast\(\s*OSData\s*,\s*provider->getProperty\(\s*"%s"\s*\)'
                             % re.escape(prop), d["source"]):
                failures.append("`%s` reads the node's `%s` without ever casting it to `OSData`, the "
                                "kind a device-tree node's property has "
                                "(`IODeviceTreeSupport.cpp:379`): the record would say the tree has "
                                "no `%s`, which is a claim about the tree made by a reader with the "
                                "wrong type" % (d["file"], prop, prop))
    if checked:
        notes.append("%d device-tree property reads, in %d driver(s), are all cast to `OSData` - the "
                     "kind the plane produces" % (checked, len(facts["drivers"])))


def claim_cell_counts(facts, failures, notes):
    """8. The root declares the cell counts every `reg` array in this tree is written for.

    493's defect, and why the declaration is on the *root*: `IODTResolveAddressing` decides how wide
    one `reg` entry is from the **parent's** `#address-cells`/`#size-cells`
    (`IODeviceTreeSupport.cpp:1227`) and computes

        cells = sizeCells + addressCells;  num = addressProperty->getLength() / (4 * cells);

    so for a nub whose parent is the root, Apple's defaults `1`/`2` (`IODTGetCellCounts`,
    `:1034-1041`) make `cells = 3` and a 8-byte entry `8 / (3 * 4) = 0` entries: **the OS resolves no
    `IODeviceMemory` for any node of a tree that writes `{address, size}` pairs and declares nothing**.
    One declaration on the root reaches every nub at once, because every nub's parent is the root.

    The numbers below are all read: Apple's defaults out of Apple's file, the declaration and the
    arrays out of the payload's tree. The claim is the arithmetic identity between them - the declared
    width has to make the OS's entry count equal the number of pairs the node's own array writes, and
    it must not be the width Apple's defaults would have produced, or the declaration is a no-op.
    """
    tree = facts["tree"]
    defaults = facts["cell_defaults"]
    if defaults is None:
        failures.append("`IODTGetCellCounts`'s defaults can no longer be read out of "
                        "`IODeviceTreeSupport.cpp`: the numbers the whole of this step rests on would "
                        "then be a transcription")
        return
    if defaults["size"] != 1 or defaults["address"] != 2:
        failures.append("`IODTGetCellCounts` now defaults to `#size-cells` %d / `#address-cells` %d "
                        "instead of Apple's 1/2: the defect 493 names - an 8-byte `reg` entry "
                        "resolving to zero entries - was derived from 1 and 2, so the derivation "
                        "would have to be redone rather than re-stated"
                        % (defaults["size"], defaults["address"]))
        return
    default_cells = defaults["address"] + defaults["size"]

    nodes = tree_nodes(tree)
    root = nodes.get("/")
    if root is None:
        return                      # claim 5 has already said this
    declared = {}
    for key in ("#address-cells", "#size-cells"):
        if key not in root:
            failures.append("the tree's root node declares no `%s`, and every `reg` array in this "
                            "tree is written as `{address, size}` pairs: `IODTResolveAddressing` "
                            "reads the **parent's** counts (`IODeviceTreeSupport.cpp:1227`) and "
                            "`IODTGetCellCounts` answers Apple's default %d/%d for a node that "
                            "declares neither (`:1034-1041`), so `cells = %d` and an 8-byte entry "
                            "resolves to `8 / (%d * 4) = 0` entries - the OS then files an empty "
                            "`IODeviceMemory` array on every nub, and only a driver that reads "
                            "`getDeviceMemory()` can see it"
                            % (key, defaults["address"], defaults["size"], default_cells,
                               default_cells))
            continue
        declared[key] = read_int(root[key])
    if len(declared) != 2 or None in declared.values():
        failures.append("the root's cell-count declaration is not two integer literals this check "
                        "can read: %s" % declared)
        return
    if declared["#size-cells"] == 0:
        failures.append("the root declares `#size-cells = 0`, and `IODTResolveAddressing` "
                        "`break`s out before resolving anything when `0 == sizeCells` "
                        "(`IODeviceTreeSupport.cpp:1229-1230`): the declaration would be worse than "
                        "none, because it would also stop the entry's length from being read")
        return
    cells = declared["#address-cells"] + declared["#size-cells"]
    if cells == default_cells:
        failures.append("the root declares `#address-cells = %d` and `#size-cells = %d`, which is "
                        "the same width as Apple's defaults (%d + %d): the declaration resolves the "
                        "entry width to the number the defaults already produced, so the fix is a "
                        "no-op and the arrays below would still resolve to the wrong count"
                        % (declared["#address-cells"], declared["#size-cells"],
                           defaults["address"], defaults["size"]))
        return

    arrays = dict(c_int_array(tree, "reg"))
    for d in facts["drivers"]:
        node = nodes.get(d["node_name"])
        if node is None or "reg" not in node:
            continue                    # claim 4 has said this
        m = re.match(r"\s*(\w+)", node["reg"])
        name = m.group(1) if m else None
        words = arrays.get(name)
        if not words:
            failures.append("the `/%s` node's `reg` is the array `%s` and this check cannot read its "
                            "initialiser out of the payload's own file: the number of entries the "
                            "node describes - the number the OS's `_devcount` is compared with - "
                            "would be a transcription" % (d["node_name"], name))
            continue
        if len(words) % cells:
            failures.append("`/%s`'s `reg` is %d words and the root declares %d cells per entry, so "
                            "the last entry is cut in half and the OS's `num = length / (4 * %d)` "
                            "silently drops it: %d %% %d = %d"
                            % (d["node_name"], len(words), cells, cells, len(words), cells,
                               len(words) % cells))
            continue
        entries = len(words) // cells
        pairs = len(words) // 2
        if entries != pairs:
            failures.append("`/%s`'s `reg` is %d words, i.e. %d `{address, size}` pairs, and the "
                            "root's declaration makes the OS resolve %d entries from it: the "
                            "declaration and the array disagree, and the OS's count is exactly what "
                            "`%s`'s `_devcount` will be compared against"
                            % (d["node_name"], len(words), pairs, entries, d["file"]))
            continue
        notes.append("`/%s`: `reg` is %d words = %d pairs, and the root's %d cells per entry make "
                     "the OS resolve %d entries; Apple's defaults (%d cells) would have resolved %d"
                     % (d["node_name"], len(words), pairs, cells, entries, default_cells,
                        len(words) // default_cells))


def claim_resolution_read(facts, failures, notes):
    """9. Each driver's resolution record is the OS's reading, and every number in it is published.

    Three properties of the driver, all of them about one value having two definitions:

      * the record is the **OS's** answer and not the driver's arithmetic - `getDeviceMemory()` and
        `getDeviceMemoryCount()`, entry 0 read with `getPhysicalSegment` (which class it is read *as* is
        claim 11's business, and 493's first run is why it is a claim and not a comment);
      * the entry count the OS's answer is compared against is derived from the node's own `reg`
        length with the divisor the root's declaration implies (`regwords / 2u` for 1 + 1 cells), so
        changing the declaration without changing the drivers - or the reverse - is a mismatch this
        check refuses;
      * every number the comparison names is published under that driver's own key prefix, so a
        `_resolve = 2` is explainable from the record instead of only from the source.

    The last one is why 493 added `_reg1` to both drivers: the comparison's left sides are `phys0` and
    `len0` and its right sides `reg0` and `reg1`, and before this a reader of the log could see three
    of those four.
    """
    if facts["nub_resource_prop"] is None:
        failures.append("`IODTPlatformExpert::getNubResources` can no longer be read out of "
                        "`IOPlatformExpert.cpp`: the property the OS resolves a nub's memory from - "
                        "which the drivers have to read for their comparison to be about one value - "
                        "would then be a guess")
        return
    resolved_prop = facts["nub_resource_prop"]
    for d in facts["drivers"]:
        src = d["source"]
        # The three calls the OS's answer is read with. Each one is named in its own message rather
        # than in a shared one: which half of the reading is missing decides what the record proves.
        os_reads = (
            ("getDeviceMemory()",
             "`%s` never calls `getDeviceMemory()`: a driver that reports where its device is without "
             "reading the array the OS already filed would be comparing its own arithmetic with "
             "itself (`IOService.cpp:6046-6049`, filed by `IODTResolveAddressing`, "
             "`IODeviceTreeSupport.cpp:1251`)"),
            ("getDeviceMemoryCount()",
             "`%s` never calls `getDeviceMemoryCount()`: the OS's answer is an array of ranges, and a "
             "driver that reads entry 0 without asking how many entries there are cannot tell one "
             "range from the first range of several"),
            ("getPhysicalSegment(",
             "`%s` never calls `getPhysicalSegment(`: without it the OS's entry is read as a pointer "
             "and the number the node's own `reg` is compared against is never taken out of it"),
        )
        for call, message in os_reads:
            if call not in src:
                failures.append(message % d["file"])

        cond = resolve_condition(src)
        if cond is None:
            failures.append("`%s` has no `resolve = 1u` condition this check can read: the "
                            "comparison between the OS's resolution and the node's own `reg` is the "
                            "whole of what the driver measures" % d["file"])
            continue
        divisor = re.search(r"regwords\s*/\s*(\d+)u?", cond)
        cells = None
        root = facts["nodes"].get("/")
        if root is not None:
            addr, size = read_int(root.get("#address-cells", "")), read_int(root.get("#size-cells", ""))
            if addr is not None and size is not None:
                cells = addr + size
        if divisor is None:
            failures.append("`%s`'s comparison does not derive the node's entry count from its own "
                            "`regwords`: without it the count the OS's answer is checked against is "
                            "a constant in the driver, which is a third definition of the node's "
                            "shape" % d["file"])
        elif cells is not None and int(divisor.group(1)) != cells:
            failures.append("`%s` divides the node's `reg` length by %s while the root's declaration "
                            "makes one entry %d cells: one of the two moved, and a comparison whose "
                            "two sides are counted in different units is a comparison that can "
                            "answer `_resolve = 2` for the wrong reason"
                            % (d["file"], divisor.group(1), cells))
        if resolved_prop not in re.findall(r'provider->getProperty\(\s*"([^"]+)"\s*\)', src):
            failures.append("`%s` compares its reading of one property with the OS's resolution of "
                            "`%s` (`IOPlatformExpert.cpp:1363`): the two sides of the comparison "
                            "have to describe the same property" % (d["file"], resolved_prop))

        prefixes = live_keys(src)
        if "entry_write_kv(" in src:
            failures.append("`%s` writes a report key (`entry_write_kv`): the report epilogue has "
                            "not run since the boot reached `vm_pageout` (490 measured it), so a "
                            "driver's reading written there is a number no run reads - and it would "
                            "be a second restatement of one the live channel already carries, which "
                            "is the shape every key since 454 avoids" % d["file"])
        stray = [k for k in re.findall(r'entry_live_write\(\s*"([^"]+)"', src)
                 if not k.startswith("xnu_live_")]
        if stray:
            failures.append("`%s` writes the live key(s) %s under a name this project's convention "
                            "does not use (`xnu_live_*`): the live channel is read by name, and a "
                            "key outside the convention is a line the reader does not look for"
                            % (d["file"], ", ".join(stray)))
        if len(prefixes) != 1:
            failures.append("`%s` writes live keys under %d prefixes (%s): the driver's record is one "
                            "row of one census and is meant to be read key by key"
                            % (d["file"], len(prefixes), ", ".join(sorted(prefixes))))
            continue
        prefix, suffixes = next(iter(prefixes.items()))
        # The lookbehind is what keeps the `u` of `2u` out of the set: an integer suffix is part of a
        # literal and not a number the comparison compares, and requiring a key for it would make the
        # claim refuse every driver that writes its divisor the way the tree's style does.
        named = set(re.findall(r"(?<![\w.])[A-Za-z_]\w*", cond))
        unpublished = sorted(n for n in named if n not in suffixes)
        if unpublished:
            failures.append("`%s`'s comparison names %s and publishes no `%s_%s`: a `_resolve` of 2 "
                            "would then be a number whose cause is not in the record"
                            % (d["file"], ", ".join(unpublished), prefix, unpublished[0]))
        else:
            notes.append("`%s`'s resolution record reads the OS's answer, compares it with the "
                         "node's own `reg` in the units the root declares, and publishes every "
                         "number the comparison names (%s)"
                         % (d["file"], ", ".join(sorted(named))))


def claim_mechanism(facts, failures, notes):
    """10. Apple's file still does what the two drivers' headers derive from it.

    The headers of `MSM8974Timer.cpp` and `MSM8974GIC.cpp` state a mechanism with four parts, and each
    one is a fact about code this project does not own. Reading them here means a change in the
    open-source tree is a red check rather than a header comment that has quietly stopped being true:

      * the nub's `getResources` resolves `"reg"` and files it under `IODeviceMemoryKey` - the two
        ends of the chain `getDeviceMemory()` returns;
      * that same function returns **success on the path where it resolved nothing**, which is why an
        empty resolution is invisible to the boot and had to be read by a driver at all;
      * the address resolution ends at the child's own address when nothing above carries `ranges`,
        with `offset` still at its initialiser - so this tree's absolute addresses are absolute and
        need no `ranges` between `/` and `/timer`.
    """
    if facts["nub_resources_failures"] is None:
        failures.append("`IODTPlatformExpert::getNubResources` can no longer be read out of Apple's "
                        "file: whether an empty resolution can be reported at all would then be a "
                        "guess, and the reason this step's record is a driver's rather than a status "
                        "code would rest on nothing")
    elif facts["nub_resources_failures"]:
        failures.append("`getNubResources` now returns `%s` on a path - `IOService::doServiceMatch` "
                        "checks `kIOReturnSuccess == getResources()` before `probeCandidates` "
                        "(`IOService.cpp:3724`), so a nub whose `reg` resolved to nothing would then "
                        "stop being probed: the empty-resolution case 493's drivers were written to "
                        "measure would be a *different* failure, before any driver runs"
                        % ", ".join(sorted(set(facts["nub_resources_failures"]))))
    else:
        notes.append("`getNubResources` returns success on every path, including the one that "
                     "resolved nothing: `doServiceMatch`'s `kIOReturnSuccess == getResources()` "
                     "cannot report an empty resolution")

    if facts["files_devmem_key"] is None:
        failures.append("`IODTResolveAddressing` can no longer be read out of "
                        "`IODeviceTreeSupport.cpp`: whether it is the function that files the array "
                        "under `IODeviceMemoryKey` - the chain both drivers' headers state - would "
                        "then be assumed at both ends")
    elif not facts["files_devmem_key"]:
        failures.append("`IODTResolveAddressing` no longer sets `gIODeviceMemoryKey` on the entry it "
                        "resolves: `IOService::getDeviceMemory()` would return something other than "
                        "this step's resolution, and both drivers' `_devmem`/`_devcount` keys would "
                        "be readings of a different array")
    else:
        notes.append("`IODTResolveAddressing` files its array under `gIODeviceMemoryKey`, which is "
                     "the array `getDeviceMemory()` returns")

    arm = facts["resolve_passthrough"]
    if arm is None:
        failures.append("`IODTResolveAddressCell`'s `end of the road` arm can no longer be read out "
                        "of Apple's file: whether this tree's absolute addresses resolve as absolute "
                        "would then be a guess")
    elif arm != ("*phys = CellsValue( childAddressCells, cell ); *phys += offset; "
                 "if (regEntry != startEntry) regEntry->release();"):
        failures.append("`IODTResolveAddressCell`'s arm for an entry with no `ranges` is now %r: this "
                        "tree has no `ranges` between `/` and `/timer`, so if that arm searches or "
                        "offsets rather than taking the child's own address, every `phys0` the "
                        "drivers read is a number the tree never wrote" % arm)
    elif facts["resolve_offset"] != 0:
        failures.append("`IODTResolveAddressCell`'s `offset` starts at %r rather than 0: the arm adds "
                        "it to the address it just decoded (`:1094`), so the arm is only the "
                        "identity - absolute addresses for a tree with no `ranges` - if the "
                        "initialiser is zero" % (facts["resolve_offset"],))
    else:
        notes.append("the arm for an entry with no `ranges` is the identity (the child's own "
                     "address, `offset` still 0), so this tree's absolute addresses are absolute")


def claim_entry_class(facts, failures, notes):
    """11. The class the OS's array holds is the class the driver reads it as.

    `IODTResolveAddressing` files its ranges as `IODeviceMemory::withRange( phys, len )`
    (`IODeviceTreeSupport.cpp:1244-1248`), and that function is a blind cast:

        return( (IODeviceMemory *) IOMemoryDescriptor::withAddressRange( start, length, ... ));
        // iokit/Kernel/IODeviceMemory.cpp:34-39

    `withAddressRange` goes through `withAddressRanges`, which builds an `IOGeneralMemoryDescriptor`
    (`IOMemoryDescriptor.cpp:1162-1181`). That class and `IODeviceMemory` are **siblings** - both
    declare `: public IOMemoryDescriptor` (`IOMemoryDescriptor.h:986`, `IODeviceMemory.h:45`) - so
    `OSDynamicCast( IODeviceMemory, entry )` answers 0 for every entry the OS files, and a driver that
    reads it that way reads nothing while its `_devcount` still says how many entries there are.

    493's first run is exactly that reading: `_devcount` 3 for the `/timer` node's three
    `{address, size}` pairs and 2 for `/interrupt-controller`'s two - the declaration had arrived and
    the OS had resolved both nodes - beside `_phys0` 0, `_len0` 0 and `_resolve` 2. The reading was
    the reader's, and the record could not say so, which is the same defect as 492's `_freqkind` one
    step earlier and the reason `_objkind`/`_objlen` exist.

    The claim is stated as a *derivation* and not as "the drivers must cast to `IOGeneralMemoryDescriptor`":
    the class the array holds is read out of Apple's files, the drivers' reading cast is read out of
    the driver, and what is required is that the second be the first or one of its base classes - so
    this check would still pass on the day Apple's `withRange` built a real `IODeviceMemory`.
    """
    factory = facts["devmem_factory"]
    built = facts["devmem_built"]
    chain = facts["devmem_chain"]
    if factory is None:
        failures.append("`IODeviceMemory::withRange` can no longer be read out of Apple's file: which "
                        "class the OS's `IODeviceMemory` array holds would then be a guess, and the "
                        "drivers' casts could not be checked against it")
        return
    if built is None or not chain:
        failures.append("the class `%s::%s` builds cannot be read out of `IOMemoryDescriptor.cpp`: "
                        "the objects in the array the drivers read are of that class, and with it "
                        "unknown a cast that answers 0 is indistinguishable from an OS that resolved "
                        "nothing" % (factory["factory_class"], factory["factory_method"]))
        return
    if factory["cast_to"] not in chain:
        notes.append("`withRange` blind-casts `%s::%s` to `%s`, which builds an `%s`: the array's "
                     "objects are not `%s` (a sibling class), so a driver's cast to `%s` answers 0 - "
                     "the reading 493's first run measured"
                     % (factory["factory_class"], factory["factory_method"], factory["cast_to"],
                        built, factory["cast_to"], factory["cast_to"]))
    else:
        notes.append("the class `withRange` builds is a `%s`, which its `%s` cast is also a "
                     "subclass of, so a cast to either name succeeds" % (built, factory["cast_to"]))

    for d in facts["drivers"]:
        casting = reading_cast(d["source"])
        if casting is None:
            failures.append("`%s` reads no entry of the OS's array through a cast this check can find: "
                            "the entry's class is the one thing that decides whether the read "
                            "happens at all (`withAddressRange` builds an `%s`, and a cast to a "
                            "sibling class answers 0 - 493's first run)" % (d["file"], built))
        elif casting["cast_to"] not in chain:
            failures.append("`%s` reads the OS's entry as a `%s` (declared `%s *%s`) and the array "
                            "holds `%s` objects: `%s` is not a `%s` and not a base of one, so the "
                            "cast answers 0, the read is skipped, and the record says the OS resolved "
                            "nothing while `_devcount` says how many entries there were - the defect "
                            "493's first run measured with a cast to `%s`"
                            % (d["file"], casting["cast_to"], casting["declared"], casting["var"],
                               built, casting["cast_to"], built, factory["cast_to"]))
        else:
            notes.append("`%s` reads the OS's entry as a `%s`, and the array holds `%s` (whose chain "
                         "is %s)" % (d["file"], casting["cast_to"], built, " -> ".join(chain)))
        # **The two keys below are matched by suffix against the driver's own key set, and 498 is the
        # step that made the difference matter.** The clause was a regex - `xnu_live_\w+_objkind` -
        # which `\w+` lets a *longer* key satisfy: 498's new `xnu_live_timerdrv_frame_objkind` is the
        # kind of a *different* entry (the timer frame the driver mapped, not the array entry it cast),
        # so removing the publication of the entry's kind stopped refusing
        # `the_driver_stops_publishing_the_entry_kind` while the check went on printing `ok`. That is
        # 269/272/297/496's family - a needle that is a claim about the file's bytes - in its third
        # shape: here the needle matched, and matched something else. `live_keys` splits a key at the
        # first underscore after its name, so a suffix is exact and a key that merely ends in the same
        # word is a different suffix.
        suffixes = set()
        for _suffixes in live_keys(d["source"]).values():
            suffixes |= _suffixes
        if "objkind" not in suffixes:
            failures.append("`%s` does not publish the kind of object it found in the OS's array: "
                            "'the OS resolved nothing' and 'this reader's cast answered 0' are the two "
                            "different findings that produced the same zero before 493's second run"
                            % d["file"])
        if "objlen" not in suffixes:
            failures.append("`%s` does not publish the entry's own `getLength()`: it is the reading "
                            "that separates a well-formed descriptor (`_objlen` = the node's first "
                            "size) from a segment walk that returned nothing (both `_phys0` and "
                            "`_len0` 0 with `_objlen` right)" % d["file"])


def claim_device_mapping(facts, failures, notes):
    """12. A driver reaches its device through the OS's own mapping, and only after both guards.

    The step's whole subject: 493 left each driver holding a *number* - `_phys0`, the address the
    kernel made of the node's `reg[0]` - and this is the access. The route is the one IOKit gives a
    kext, and the claim is that it is the route the drivers actually take:

      * `map( kIOMapAnywhere )` is called, and it is called **on the object the driver cast the OS's
        array entry into** (`reading_cast`) - not on a descriptor the driver built from the node's own
        `reg` words. A driver that mapped its own arithmetic would be measuring itself: 493's claim 9
        rejects exactly that for the *value*, and this claim rejects it for the *access*. That the call
        does what the files say (`device_pager_setup` + a physically-typed entry, `IOMemoryDescriptor.cpp
        :641-678`) is claim 11's neighbourhood and `claim_mechanism`'s subject, not something a source
        can be asked to prove; what a source *can* be asked is that the address read through is the one
        this call returned.
      * The address comes from `getVirtualAddress()` **on that map** and the length from its
        `getLength()`, and *both* are published (`_map`, `_mapvaddr`, `_mapvlen`). A record that
        published only the word it read could not tell "the OS mapped nothing" from "the driver read
        nothing through a map it had" - the two findings this step exists to separate, and the same
        shape as 493's `_objkind`.
      * Every device word is read through **`_mapvaddr`** and at an offset **named in the driver's own
        file**, which this check resolves. A bare `0` in the expression is an offset nothing can hold
        against anything, and an unresolvable name is reported rather than skipped.
      * Both guards are present, and the read is *after* them: `if( <map> != 0 )` and
        `if( <vaddr> != 0u )`. A dereference of zero on this machine is a data abort, and an abort whose
        `far` is 0 would say nothing about the mapping - so the guard is not defensive style, it is what
        keeps a failed map from being recorded as a failed *read*.
    """
    for d in facts["drivers"]:
        mapping = driver_mapping(d["source"])
        casting = reading_cast(d["source"]) or {}
        if mapping["call"] is None:
            failures.append("`%s` never calls `map( kIOMapAnywhere )`: the OS's resolution stays a "
                            "number, so nothing on this machine has asked the kernel's own mapping "
                            "machinery to reach a device, and the claim that a driver touches its "
                            "device is not made by this file" % d["file"])
        elif mapping["srcvar"] != casting.get("var"):
            failures.append("`%s` maps `%s`, and the OS's entry was cast into `%s`: the mapped object "
                            "is not the one the OS's array holds, so the address read through is not "
                            "the OS's answer to where this device is"
                            % (d["file"], mapping["srcvar"], casting.get("var")))
        if mapping["vaddr"] is None or mapping["vaddrsrc"] != mapping["mapvar"]:
            failures.append("`%s` does not take the address to read through from "
                            "`getVirtualAddress()` of the map it just made (map `%s`, address from "
                            "`%s`): a device read through anything else is not a reading of the OS's "
                            "mapping" % (d["file"], mapping["mapvar"],
                                         mapping["vaddrsrc"] or "nothing"))
        if mapping["vlen"] is None:
            failures.append("`%s` does not publish the mapping's own `getLength()`: without it a "
                            "`_mapvaddr` that landed on the wrong page cannot be told from one that "
                            "landed on the right one" % d["file"])
        for suffix, what in (("map", "the `IOMemoryMap *` the map call returned"),
                             ("mapvaddr", "the mapping's `getVirtualAddress()`"),
                             ("mapvlen", "the mapping's `getLength()`")):
            if not re.search(r'xnu_live_\w+_%s\b' % suffix, d["source"]):
                failures.append("`%s` does not publish `_%s` (%s): a guard that fails silently is a "
                                "reading that cannot be told from a read that never happened"
                                % (d["file"], suffix, what))
        if not mapping["reads"]:
            failures.append("`%s` reads no device word through the address its mapping returned: the "
                            "mapping would then be the only thing this driver did with the OS's answer"
                            % d["file"])
        for offset_symbol, expr, stored in ([(s, e, False) for s, e in mapping["reads"]] +
                                            [(s, e, True) for s, e in mapping["writes"]]):
            if offset_symbol is None:
                failures.append("`%s` %s a device word at a bare offset (%s): an offset nothing "
                                "names is a constant no claim can hold against the header the payload "
                                "%s the same register at"
                                % (d["file"], "stores" if stored else "reads",
                                   re.sub(r"\s+", " ", expr), "writes" if stored else "reads"))
                continue
            resolved = driver_defines(d["source"]).get(offset_symbol)
            if resolved is None:
                failures.append("`%s` %s at `%s`, which its own file does not define as an integer: "
                                "the offset cannot be resolved, so a %s of the wrong register would "
                                "look exactly like this one"
                                % (d["file"], "stores" if stored else "reads", offset_symbol,
                                   "write" if stored else "read"))
            else:
                notes.append("`%s` %s `%s` at offset 0x%03x through `%s`"
                             % (d["file"], "stores to" if stored else "reads", offset_symbol, resolved,
                                mapping["vaddr"]))
        for guard in mapping["map_guards"]:
            if guard is None:
                failures.append("`%s` is missing one of the two guards - `if( <map> != 0 )` and "
                                "`if( <vaddr> != 0u )` - that keep a failed mapping from being "
                                "recorded as a failed read" % d["file"])
        if len(mapping["map_guards"]) == 2 and all(mapping["map_guards"]):
            first_read = min(d["source"].find(expr)
                             for _sym, expr in mapping["reads"] + mapping["writes"])
            last_guard = max(g.end() for g in mapping["map_guards"])
            if first_read < last_guard:
                failures.append("`%s` reaches the device before both guards have passed: the access is "
                                "outside the branch the mapping's success is tested in, so a map that "
                                "returned 0 would still be dereferenced" % d["file"])


def claim_device_value(facts, failures, notes):
    """13. The value read through the mapping has two definitions, and the comparison is between reads.

    `GICD_TYPER` is the distributor's identification register: read-only, constant, and read by two
    pieces of code that share nothing but the address the device tree declares - the payload's probe,
    through a 1 MB section it installed itself (`entry_gic.c`'s `entry_mmio_section`), and the driver,
    through the mapping the OS's own resolution produced. That is what makes it the one reading in this
    step that can be falsified by a run: a mapping that reached the wrong page, or a `reg` whose
    declaration resolved to the wrong pair, returns a value that is not the payload's.

    Every link of the chain is required to be a *read* rather than a transcription, and the chain is
    followed in one direction from the payload's register read to the driver's comparison:

      * `payload_typer_channel` walks `gicd_read( <macro> )` -> variable -> the named global, requires
        the global to be defined (not `static`) in the payload's C and declared in the payload's header,
        and resolves `<macro>` against the header's own `#define`. Any of those links replaced by a
        number makes the driver's verdict a comparison with a constant no run can contradict.
      * the driver's `extern "C"` declaration must name that same symbol - found by the declaration and
        not by the driver's class, so a fourth driver with a payload-side counterpart is checked by
        adding the declaration;
      * the driver's comparison's right-hand side must be that symbol, and its left-hand side must be
        the variable the device read was assigned to - so the verdict compares the *mapped read* with
        the *payload's read* and not two names that happen to be in the file;
      * and the offset of that read must be the header's offset: the two definitions of "which register
        is `GICD_TYPER`" are compared here, which is the same discipline 493 applied to the
        `#address-cells` declaration.

    The last requirement is a *note* rather than a failure for the driver that declares no payload
    symbol: `/timer`'s `_rd0` has no second definition on this machine - nothing here has ever read a
    word out of the GPT - and the check says so out loud rather than inventing one for it.
    """
    channel = payload_typer_channel(facts["payload_gic_c"], facts["payload_gic_h"])
    if channel["symbol"] is None:
        failures.append("the payload defines no non-`static` `uint32_t g_...` for a driver to compare "
                        "against: a `static` one would not link, so the driver's `extern` would be an "
                        "undefined symbol rather than a reading")
        return
    if channel["rhs"] is None or channel["macro"] is None:
        failures.append("`%s` is not assigned from the variable the payload's probe read the register "
                        "into (`%s`): if the symbol is not the probe's own read, the driver's verdict "
                        "compares its mapping against a value nothing measured"
                        % (channel["symbol"], channel["rhs"] or "nothing"))
        return
    if channel["offset"] is None:
        failures.append("`entry_gic.h` does not define `%s` as an integer offset, so the register the "
                        "driver reads at cannot be held against the register the payload read: the "
                        "offset would be one definition and one claim" % channel["macro"])
    if not channel["declared"]:
        failures.append("`entry_gic.h` does not declare `%s`: the symbol the driver declares `extern` "
                        "would then have one definition and no header, which is how a name and a value "
                        "drift apart" % channel["symbol"])
    notes.append("the payload's `%s` is `gicd_read( %s )` = 0x%03x, the register the driver's mapped "
                 "read must match" % (channel["symbol"], channel["macro"], channel["offset"] or 0))

    compared = 0
    for d in facts["drivers"]:
        declared = driver_payload_symbol(d["source"])
        if declared is None:
            notes.append("`%s` declares no payload symbol: its read has one definition on this machine "
                         "and the step says so rather than inventing an expected value for it"
                         % d["file"])
            continue
        compared += 1
        if declared["symbol"] != channel["symbol"]:
            failures.append("`%s` compares against `%s` and the payload publishes `%s`: the two names "
                            "are two symbols, and the comparison is with whichever one nothing wrote"
                            % (d["file"], declared["symbol"], channel["symbol"]))
            continue
        mapping = driver_mapping(d["source"])
        if mapping["vaddr"] is None:
            failures.append("`%s` reads no mapped address, so the comparison has no left-hand side"
                            % d["file"])
            continue
        reads_by_var = {}
        for m in re.finditer(r"(\w+)\s*=\s*\*\s*\(\s*volatile\s+uint32_t\s*\*\s*\)\s*"
                             r"\(\s*uintptr_t\s*\)\s*\(\s*%s\s*\+\s*(\w+)\s*\)\s*;"
                             % re.escape(mapping["vaddr"]), d["source"]):
            reads_by_var[m.group(1)] = m.group(2)
        # Which side of the comparison is which is the source's business; what is required is that one
        # side is a word read through the mapping and the other is the payload's symbol. A comparison
        # between two names neither of which is a device read is the shape this step must refuse.
        sides = [declared["lhs"], declared["rhs"]]
        mapped_side = next((s for s in sides if s in reads_by_var), None)
        payload_side = sides[0] if mapped_side == sides[1] else sides[1]
        if mapped_side is None:
            failures.append("`%s` compares `%s` with `%s`, and neither is assigned from a word read "
                            "through the mapping: the verdict is not about the OS's mapping at all"
                            % (d["file"], declared["lhs"], declared["rhs"]))
        elif not re.search(r"^\s*%s\s*=\s*%s\s*;" % (re.escape(payload_side), re.escape(channel["symbol"])),
                           d["source"], re.M):
            failures.append("`%s` compares its mapped read with `%s`, which is not assigned from `%s`: "
                            "the verdict's other side is a name the payload's read does not reach"
                            % (d["file"], payload_side, channel["symbol"]))
        else:
            offset_symbol = reads_by_var[mapped_side]
            resolved = driver_defines(d["source"]).get(offset_symbol)
            if resolved != channel["offset"]:
                failures.append("`%s` reads `%s` at 0x%03x and the payload reads `%s` at 0x%03x: the "
                                "two definitions of which register this is disagree, so `_hwok` could "
                                "be comparing two different registers"
                                % (d["file"], offset_symbol, resolved or 0, channel["macro"],
                                   channel["offset"] or 0))
            else:
                notes.append("`%s` compares its read of `%s` (0x%03x) with `%s`, the payload's read of "
                             "the same register" % (d["file"], offset_symbol, resolved,
                                                    channel["symbol"]))
        if not re.search(r'xnu_live_\w+_%s\b' % declared["verdict"], d["source"]):
            failures.append("`%s` publishes no `_%s`: the comparison's outcome is the reading this step "
                            "is for, and a comparison whose result is discarded measures nothing"
                            % (d["file"], declared["verdict"]))
        for suffix in ("maptyper", "probetyper"):
            if not re.search(r'xnu_live_\w+_%s\b' % suffix, d["source"]):
                failures.append("`%s` publishes no `_%s`: a verdict of 0 with only one of its operands "
                                "in the log is a number whose cause is not in the record"
                                % (d["file"], suffix))
    if compared == 0:
        notes.append("no driver compares against the payload's read: the register this step chose for "
                     "its second definition is read by nothing on the OS side")


def claim_timer_callback(facts, failures, notes):
    """14. The driver asks the OS for a timeout, and the callback the kernel will call is its own.

    493 and 494 made a driver *read* its device; neither made the boot need the driver for anything.
    This claim is about the step where the kernel calls the driver back: a work loop, a timer event
    source on it, `enable()`, and `setTimeout` - after which the callback runs on the thread-call
    daemon, with the work loop's gate held, once the timer queue is expired from the payload's
    interrupt handler. Nothing in the chain is this image's own code, which is exactly why it has to
    be read out of the source rather than assumed to work:

      * **the event source is on the work loop that was created.** `addEventSource` is what gives the
        source its `workLoop`, and `IOTimerEventSource::wakeAtTime` arms nothing without one
        (`IOTimerEventSource.cpp:476`), so a chain wired to two different objects is a timeout that
        never becomes a thread call - and the run would show it only as `_fires = 0`.
      * **the function handed to `timerEventSource` is defined in this file.** The action is read out of
        the *call*, not looked up by name, so a driver whose callback is wired to another function is
        refused; and the function must have a body here, because a name with no definition is a
        callback the kernel cannot reach.
      * **`enable()` is called before `setTimeout`.** A disabled source stores the time and arms
        nothing, so the order of two adjacent statements is the difference between a deadline and a
        recorded intention.
      * **the interval has one definition.** The name passed to `setTimeout` and the name passed to
        `clock_interval_to_deadline` must be the same name, and it must be a `#define` in this file with
        an integer value: the moment the callback should run and the moment it was asked to run are one
        decision, and the callback's own re-arm must use that same name. A `100u` in one of the three
        places is a second definition of the interval, which is the defect this project counts.
      * **the callback says it ran, and keeps itself alive.** The action body must publish the fire
        count and the OS's own conversion of the elapsed time, and must re-arm. A callback that fires
        and reports nothing is a reading the log cannot show; one that fires once and never re-arms
        cannot distinguish a working timer from one that fired by accident.
      * **the record separates "never armed" from "armed and never called".** The fire count (and the
        keys the callback owns) must be published *before* the arming, and the arming's own five results
        (`_wl`, `_ts`, `_add`, `_en`, `_to`) plus the two clock readings (`_arm_*`, `_due_*`) must be in
        the record. Without the zero, a callback that never ran leaves no key at all and reads exactly
        like a driver that never asked.

    A driver that arms no timeout is *noted* rather than failed - `/interrupt-controller`'s driver does
    not need one - but at least one driver in this image must, or the reading this step is for would be
    absent from the record altogether.
    """
    armed_drivers = 0
    for d in facts["drivers"]:
        t = driver_timeout(d["source"])
        if t["workloop"] is None and t["timer"] is None:
            notes.append("`%s` arms no OS timeout: the driver that does is the one whose subject is "
                         "time, and this file's reading is the device's" % d["file"])
            continue
        armed_drivers += 1

        def has(key):
            return bool(re.search(r'xnu_live_\w+_%s\b' % re.escape(key), d["source"]))

        if t["workloop"] is None:
            failures.append("`%s` never calls `IOWorkLoop::workLoop()`: the event source would have no "
                            "work loop, and `wakeAtTime` arms nothing without one"
                            % d["file"])
        if t["timer"] is None:
            failures.append("`%s` never creates an `IOTimerEventSource`: there is no timeout for the "
                            "kernel to call back" % d["file"])
        elif t["added_loop"] != t["workloop"]:
            failures.append("`%s` adds the timer event source to `%s` and created `%s`: the source's "
                            "own `workLoop` is the one it is added to, so this timeout would never "
                            "reach the thread-call machinery"
                            % (d["file"], t["added_loop"] or "nothing", t["workloop"] or "nothing"))
        if t["timer"] is not None:
            if t["action_def"] is None:
                failures.append("`%s` hands `%s` to `timerEventSource` and defines no such function "
                                "here: the callback the kernel is told to call is not in this file, so "
                                "the timeout could only ever call whatever that name resolves to"
                                % (d["file"], t["action"]))
            else:
                action = t["action_def"]
                if not re.search(r'xnu_live_\w+_fires\b', action):
                    failures.append("`%s`'s callback `%s` publishes no `_fires`: a callback that runs "
                                    "and reports nothing leaves the one reading this step is for out "
                                    "of the log" % (d["file"], t["action"]))
                if not re.search(r'xnu_live_\w+_fire_lat_ns\b', action):
                    failures.append("`%s`'s callback `%s` publishes no `_fire_lat_ns`: how long the OS "
                                    "took to call the driver back is the number the interval it asked "
                                    "for is held against" % (d["file"], t["action"]))
                if t["rearm"] is None:
                    failures.append("`%s`'s callback `%s` never calls `setTimeout` again: a timer that "
                                    "fires once and stops cannot distinguish an armed deadline from a "
                                    "single accident" % (d["file"], t["action"]))
        if t["enabled_at"] is None or t["armed_at"] is None:
            failures.append("`%s` does not both `enable()` the source and call `setTimeout` on it: a "
                            "disabled source stores the time and arms no thread call"
                            % d["file"])
        elif t["enabled_at"] > t["armed_at"]:
            failures.append("`%s` arms the timeout before enabling the source: the deadline is stored "
                            "and no thread call is entered, so the record would say 'armed' about a "
                            "timeout that can never fire" % d["file"])
        interval = t["armed"]
        if interval is None:
            failures.append("`%s` never calls `setTimeout( <interval>, kMillisecondScale )`"
                            % d["file"])
        else:
            resolved = driver_defines(d["source"]).get(interval)
            if resolved is None:
                failures.append("`%s` asks for `%s`, which its own file does not define as an integer: "
                                "the interval has no name to hold against the deadline the callback is "
                                "compared with" % (d["file"], interval))
            elif t["deadline"] != interval:
                failures.append("`%s` computes its deadline from `%s` and arms `%s`: two definitions of "
                                "one interval, so the moment the callback was asked to run is not the "
                                "moment the record compares it with"
                                % (d["file"], t["deadline"] or "nothing", interval))
            elif t["rearm"] is not None and t["rearm"] != interval:
                failures.append("`%s`'s callback re-arms with `%s` and the driver armed `%s`: the "
                                "second and later intervals would be a decision nothing else in the "
                                "file makes" % (d["file"], t["rearm"], interval))
            else:
                notes.append("`%s` asks for `%s` = %d ms, from one `#define` used by the arming, the "
                             "deadline and the re-arm" % (d["file"], interval, resolved))
        if t["fires_zero_at"] is None:
            failures.append("`%s` publishes no zero `_fires` before arming: a callback that never ran "
                            "would then leave no key, and 'armed and never called' would read exactly "
                            "like 'never armed'" % d["file"])
        elif t["arming_at"] is not None and t["fires_zero_at"] > t["arming_at"]:
            failures.append("`%s` publishes its `_fires` zero *after* creating the work loop: a "
                            "timeout whose callback ran between the two lines would be overwritten by "
                            "the zero" % d["file"])
        for key in ("ms", "wl", "ts", "add", "en", "to", "arm_lo", "arm_hi", "due_lo", "due_hi"):
            if not has(key):
                failures.append("`%s` publishes no `_%s`: the chain of five calls that arms this "
                                "timeout can each fail silently, and a guard that is not in the log is "
                                "a reading that cannot be told from a timeout that never existed"
                                % (d["file"], key))
    if armed_drivers == 0:
        failures.append("no driver in `PLATFORM_SOURCES` arms an OS timeout: the reading this step is "
                        "for - the kernel calling a driver back - would not exist in any run")


def claim_driver_line(facts, failures, notes):
    """15. A driver owns an interrupt line and writes its device, in one order and under one switch.

    492-495 made a driver *read* its device and made the OS call it back on a timeout; none of them
    made a driver own anything of the machine. 496 does, and every part of the claim is a way the step
    could be a file that reads like it without being it:

      * **there is exactly one switch, it is a `#define` in the driver's own source, and its value is
        read back.** `MSM8974_GIC_DRIVE_SGI` decides whether this file writes its device at all. It is
        checked the way `check_irq_routing.py` checks `entry_irq.c`'s switch - its *value* as well as
        its presence - because a value the check cannot read is a claim it cannot make about the
        artifact that ran, and a third value is a build that differs from both measured ones. The
        value is published to the live buffer, so two builds that differ only here are two machines a
        log can tell apart.
      * **every device store in the file is inside a `#if MSM8974_GIC_DRIVE_SGI` block.** The switch
        is not a switch if a store lives outside it. The claim is about *all* the stores and not only
        about the switch's own block, because the failure it guards is a later store added next to the
        handler it belongs to - which would be reachable in a build that turned the feature off.
      * **the registration comes before the request, and the request is guarded on it.** An intid that
        reaches the dispatcher with no client is the one path this image documents as *stopping the
        run*, so a driver that pends a line before filing a client for it ends the boot at the exact
        place the step exists to keep it out of. The guard on the return value is the second half of
        the same statement, because a registration can be refused - a full table, a duplicate, the
        dispatcher's own timer line.
      * **the handler that is registered is a function this file defines, and the same address is
        published.** The registration is read out of the call - the intid, the `&`-expression, the
        `refCon` - and the address must also appear in the record, so the log carries the function the
        dispatcher will call rather than a name the claim resolved. The `refCon` must be `this`: the
        kernel's own second-level ABI hands the *controller object* back
        (`IOCPUInterruptController::handleInterrupt`), so a client that filed anything else is called
        with an argument that is not the device it owns. And the two functions it calls across the
        image boundary must each have an `extern "C"` prototype in this file, because this file cannot
        include the header that defines them - the declaration *is* the ABI.
      * **the numbers it writes with have one definition.** Six of them - four register offsets, the
        target filter and the intid - are paired with the header's names for the same registers, both
        resolved as integers. A store at the wrong offset programs a *different register* rather than
        returning a wrong number: `GICD_ICPENDR0` is `GICD_ISPENDR0` + 0x80, so one wrong digit turns
        "clear my line" into "set my line".
      * **the driver's own handler clears its device and asks for the next interrupt itself.** The
        handler body must read the pending word, publish it, and be the place at least one further
        request comes from - which is what makes the call count a *sequence* rather than one event,
        and it is the same shape 495's callback used to keep its own timer alive. The count it stops
        at is a named constant, published, and at least 2 so that interrupt context is really where a
        request is made.
      * **it gives the line back only when its own device says the line is quiet.** The unregister is
        the client's decision and it is guarded by the driver's own read of the pending word - a
        registration withdrawn while a delivery is in flight is exactly the unregistered-intid stop -
        and the guard's value is published, so the run says which branch was taken.

    A driver that owns no line is *noted* rather than failed - `/timer`'s subject is time - and at
    least one in the image must, for claim 14's reason: the reading this step is for would otherwise
    be absent from every run.
    """
    header = payload_defines(facts["payload_gic_h"], "STAGE90_GIC")
    registered = 0

    for d in facts["drivers"]:
        source = d["source"]
        defines_here = driver_defines(source)
        client = driver_irq_client(source)
        switch = defines_here.get(DRIVER_GIC_SWITCH)
        # **This claim's subject is the driver whose *own device* is the distributor** - the one that
        # raises its own line and whose writes to that device are what the switch gates - and 500 is why
        # the gate says so instead of "registers a client or defines the switch". The reader above could
        # not see `MSM8974Timer.cpp`'s registration until this step widened it, and the note the old gate
        # printed for that file - "owns no interrupt line" - was then a claim about a different file
        # altogether, printed about the one file in the image that had just been given a line. The two
        # claims partition the drivers that own something: a driver that raises its own line is read
        # here, a driver that owns a line the machine asserts is read by claim 16, and the note names
        # which half a file is in rather than asserting that it owns nothing.
        asks_sgir = (header.get("STAGE90_GICD_SGIR") is not None
                     and any(v == header["STAGE90_GICD_SGIR"] for v in defines_here.values()))
        if switch is None and not asks_sgir:
            owns = driver_irq_client(source)
            notes.append("`%s` arms no distributor register of its own, so this claim's question - "
                         "whether a driver that writes the interrupt controller keeps every write inside "
                         "one switch - is not about this file%s"
                         % (d["file"],
                            (": it registers a client for `%s` and the line it owns is the machine's, "
                             "which claim 16 reads" % owns["intid"]) if owns["at"] is not None
                            else "; it registers no client for any line"))
            continue
        registered += 1

        # -- the switch ---------------------------------------------------------------------------
        blocks = [(m.start(1), m.end(1)) for m in
                  re.finditer(r"^#if\s+%s\s*$(.*?)^#(?:else|endif)"
                              % re.escape(DRIVER_GIC_SWITCH), source, re.M | re.S)]
        if switch is None:
            failures.append("`%s` does not define `%s` as an integer: whether this driver writes its "
                            "device at all would then be unstated, and no reading of the artifact "
                            "could say which machine was built" % (d["file"], DRIVER_GIC_SWITCH))
        elif switch not in (0, 1):
            failures.append("`%s`'s `%s` is %d: it is a two-state switch, and a third value is a "
                            "build that differs from both measured ones"
                            % (d["file"], DRIVER_GIC_SWITCH, switch))
        if not blocks:
            failures.append("`%s` has no `#if %s` block: the switch would be a number nothing reads, "
                            "and the code it is supposed to switch would run either way"
                            % (d["file"], DRIVER_GIC_SWITCH))
        if not re.search(r'entry_live_write\(\s*"xnu_live_\w+_drive_compiled"\s*,\s*%s\s*\)'
                         % re.escape(DRIVER_GIC_SWITCH), source):
            failures.append("`%s` never publishes `%s` to the live buffer: a run's record could not "
                            "say whether the image that produced it wrote the distributor"
                            % (d["file"], DRIVER_GIC_SWITCH))

        # -- every device store is inside a switch block --------------------------------------------
        mapping = driver_mapping(source)
        # Two bases, and the second one is not decoration: the handler runs long after `start` has
        # returned, so it writes through the *saved* address rather than through `start`'s local (the
        # comment on that global says so). A claim that only looked at `mapping["vaddr"]` would see
        # the stores in `start` and be blind to the one in the handler - which is exactly the store
        # that must not escape the switch, because the handler is what runs in a build that leaves the
        # request out.
        handler_base = None
        handler_body = (method_body(strip_comments(source), client["handler"])
                        if client["at"] is not None else None)
        if handler_body:
            found = re.findall(r"\*\s*\(\s*volatile\s+uint32_t\s*\*\s*\)\s*\(\s*uintptr_t\s*\)\s*"
                               r"\(\s*(\w+)\s*\+", handler_body)
            handler_base = found[0] if found else None
        stores = []
        for base in dict.fromkeys(b for b in (mapping["vaddr"], handler_base) if b):
            stores.extend(driver_device_stores(source, base))
        stores.sort(key=lambda item: item[2])
        for symbol, expr, at in stores:
            if not any(lo <= at < hi for lo, hi in blocks):
                failures.append("`%s` stores to `%s` at offset %d (outside every `#if %s` block): the "
                                "switch is then not a switch - with it at 0 the device would still be "
                                "written" % (d["file"], symbol, at, DRIVER_GIC_SWITCH))

        if client["at"] is None:
            failures.append("`%s` writes its device and registers no client for a line: the "
                            "interrupts its own handler would raise have no dispatcher entry to go "
                            "through, and the payload's stop path is what would answer them"
                            % d["file"])
            continue

        # -- the registration ----------------------------------------------------------------------
        for fn in ("entry_irq_register_client", "entry_irq_unregister_client"):
            if not re.search(r'extern\s+"C"\s+uint32_t\s+%s\s*\(' % re.escape(fn), source):
                failures.append("`%s` calls `%s` and declares no `extern \"C\"` prototype for it: "
                                "this file cannot include the payload's header, so that declaration "
                                "is the whole ABI between the two and nothing else compares them"
                                % (d["file"], fn))
        if client["refcon"] != "this":
            failures.append("`%s` registers `%s` as its `refCon`: Apple's second-level route hands the "
                            "*controller object* back to the handler "
                            "(`IOCPUInterruptController::handleInterrupt`), so a client that filed "
                            "anything else is called with an argument that is not the device it owns"
                            % (d["file"], client["refcon"]))
        if defines_here.get(client["intid"]) is None:
            failures.append("`%s` registers for `%s`, which its own file does not define as an "
                            "integer: the line it claims would be a number no claim can hold against "
                            "the payload's own SGI" % (d["file"], client["intid"]))
        if handler_body is None:
            failures.append("`%s` registers the address of `%s` and defines no such function here: "
                            "the kernel would call whatever that name resolves to at link time, which "
                            "is a handler no claim in this file is about"
                            % (d["file"], client["handler"]))
        if not re.search(r'entry_live_write\(\s*"xnu_live_\w+_isr"\s*,\s*\(uint32_t\)\s*'
                         r"\(uintptr_t\)\s*&\s*%s\s*\)" % re.escape(client["handler"]), source):
            failures.append("`%s` registers `&%s` and publishes no key holding that same address: the "
                            "record would not carry the function the dispatcher was told to call, so a "
                            "run could not show that the line went to *this* driver"
                            % (d["file"], client["handler"]))
        if not re.search(r'entry_live_write\(\s*"xnu_live_\w+_cli_rc"\s*,\s*%s\s*\)'
                         % re.escape(client["rc"]), source):
            failures.append("`%s` does not publish the registration's return (`%s`): a refused "
                            "registration is a line the driver does not own, and it is the one outcome "
                            "the record has to be able to tell from an accepted one"
                            % (d["file"], client["rc"]))

        # -- the order: registration, then the request, guarded on the return -----------------------
        sgir_names = {name for name, value in defines_here.items()
                      if header.get("STAGE90_GICD_SGIR") is not None
                      and value == header["STAGE90_GICD_SGIR"]}
        asks = [(sym, at) for sym, _expr, at in stores if sym in sgir_names]
        if not asks:
            failures.append("`%s` registers a client and never requests the line: nothing this driver "
                            "owns would ever be delivered, and the handler it filed would be a "
                            "function the kernel never calls" % d["file"])
        elif not any(at > client["at"] for _sym, at in asks):
            failures.append("`%s` requests the line before it registers a client for it: an intid "
                            "that reaches the dispatcher with no client is the path this image "
                            "documents as stopping the run, so the order is not style - it is the "
                            "difference between a delivery and a stop" % d["file"])
        else:
            first_ask = min(at for _sym, at in asks if at > client["at"])
            if not re.search(r"\b%s\s*==\s*1u" % re.escape(client["rc"]),
                             source[client["at"]:first_ask]):
                failures.append("`%s` requests the line without testing `%s == 1u` first: a "
                                "registration can be refused - a full table, a duplicate, or the "
                                "dispatcher's own timer line - and the request would then be an "
                                "interrupt nobody is filed for" % (d["file"], client["rc"]))

        # -- the six numbers, against the header ----------------------------------------------------
        for driver_name, header_name in DRIVER_GIC_REGISTER_PAIRS:
            if driver_name not in defines_here:
                failures.append("`%s` no longer defines `%s`: one of the six numbers this driver "
                                "writes its device with has lost its name"
                                % (d["file"], driver_name))
            elif header_name not in header:
                failures.append("`entry_gic.h` no longer defines `%s`, so the driver's `%s` has one "
                                "definition and no counterpart" % (header_name, driver_name))
            elif defines_here[driver_name] != header[header_name]:
                failures.append("`%s` is 0x%x in `%s` and `%s` is 0x%x in `entry_gic.h`: the two "
                                "definitions of which register this is disagree, and for a *store* "
                                "that means the driver programs a different register"
                                % (driver_name, defines_here[driver_name], d["file"], header_name,
                                   header[header_name]))
            else:
                notes.append("`%s`'s `%s` is 0x%03x, the header's `%s`"
                             % (d["file"], driver_name, defines_here[driver_name], header_name))

        # -- the enable's read-back -----------------------------------------------------------------
        for suffix, what in (("en_before", "the enable word before the driver wrote it"),
                             ("en_after", "the enable word after the driver wrote it"),
                             ("pend_before", "the pending word before anything was asked of the line")):
            if not re.search(r'xnu_live_\w+_%s\b' % suffix, source):
                failures.append("`%s` publishes no `_%s` (%s): a device write whose effect is not read "
                                "back is an assumption about the part, and the readings beside it are "
                                "what make it a measurement" % (d["file"], suffix, what))

        if handler_body is None:
            continue

        # -- the handler's own half -----------------------------------------------------------------
        for suffix, what in (("isr_calls", "how many times the kernel called it"),
                             ("isr_last", "the intid it was called with"),
                             ("isr_refcon", "the object it was handed"),
                             ("isr_pends", "the requests it made from interrupt context"),
                             ("isr_guard", "whether its own device said the line was quiet")):
            if not re.search(r'xnu_live_\w+_%s\b' % suffix, handler_body):
                failures.append("`%s`'s handler publishes no `_%s` (%s): the reading this step is for "
                                "is the kernel calling a *driver's* function, and a call whose count, "
                                "argument and effect are not in the log is a call that cannot be told "
                                "from one that never happened" % (d["file"], suffix, what))
        if handler_base is None:
            failures.append("`%s`'s handler touches no device address at all: it would be a function "
                            "the kernel calls that does nothing to the interrupt source - the line's "
                            "pending state would then never be cleared by its owner, and the "
                            "dispatcher's cap would be the thing that ends the run" % d["file"])
            continue
        if handler_base != mapping["vaddr"]:
            # The handler cannot see `start`'s locals, so it reaches the device through a saved
            # address. That is a *second name* for one address, and the property that keeps it from
            # being a second address is that the name is assigned from the one that was published.
            origin = re.search(r"^\s*%s\s*=\s*(\w+)\s*;" % re.escape(handler_base), source, re.M)
            if origin is None:
                failures.append("`%s`'s handler writes through `%s`, which no statement in this file "
                                "assigns: the handler's base would be a variable nothing set, so the "
                                "address it programs is not the one the driver mapped"
                                % (d["file"], handler_base))
            elif not re.search(r'entry_live_write\(\s*"xnu_live_\w+"\s*,\s*%s\s*\)'
                               % re.escape(origin.group(1)), source):
                failures.append("`%s`'s handler writes through `%s`, assigned from `%s`, and `%s` is "
                                "never published: the record could not say which address the handler's "
                                "stores went to, which is the one thing that separates a write to the "
                                "driver's mapping from a write to one of the two other ways this "
                                "machine reaches that device"
                                % (d["file"], handler_base, origin.group(1), origin.group(1)))
            else:
                notes.append("`%s`'s handler reaches the device through `%s`, the saved copy of the "
                             "published `%s`" % (d["file"], handler_base, origin.group(1)))
        pend_reads = [(m.group(1), m.group(2), m.end()) for m in re.finditer(
            r"(\w+)\s*=\s*\*\s*\(\s*volatile\s+uint32_t\s*\*\s*\)\s*\(\s*uintptr_t\s*\)\s*"
            r"\(\s*%s\s*\+\s*(\w+)\s*\)" % re.escape(handler_base), handler_body)]
        pend_var = None
        for var, symbol, _end in pend_reads:
            if header.get("STAGE90_GICD_ISPENDR0") is not None \
                    and defines_here.get(symbol) == header["STAGE90_GICD_ISPENDR0"]:
                pend_var = var
        if pend_var is None:
            failures.append("`%s`'s handler never reads its own line's pending bit: the "
                            "acknowledgement's effect would be assumed rather than measured, and the "
                            "guard on giving the line back would have nothing to be a guard on"
                            % d["file"])
        in_handler = [m.group(1) for m in re.finditer(
            r"\*\s*\(\s*volatile\s+uint32_t\s*\*\s*\)\s*\(\s*uintptr_t\s*\)\s*"
            r"\(\s*%s\s*\+\s*(\w+)\s*\)\s*=\s*[^;]+;" % re.escape(handler_base), handler_body)]
        if not (sgir_names & set(in_handler)):
            failures.append("`%s`'s handler never asks for the line again: the delivery would then be "
                            "a single event, with no way to tell an armed line from one that fired "
                            "once, and the call count would be a constant rather than a sequence"
                            % d["file"])
        ask_define = re.search(r'entry_live_write\(\s*"xnu_live_\w+_isr_ask"\s*,\s*(\w+)\s*\)', source)
        if ask_define is None:
            failures.append("`%s` publishes no `_isr_ask`: the number of deliveries it will answer "
                            "for is a decision no reading of the artifact can recover" % d["file"])
        else:
            ask = defines_here.get(ask_define.group(1))
            if ask is None:
                failures.append("`%s` publishes `%s` as the number of asks and its own file does not "
                                "define that as an integer" % (d["file"], ask_define.group(1)))
            elif ask < 2:
                failures.append("`%s` publishes an ask count of %d: below 2 the handler never asks for "
                                "the line itself, so the second delivery - the one that makes "
                                "interrupt context part of the record - would not exist"
                                % (d["file"], ask))
            elif not re.search(r"<\s*%s\b" % re.escape(ask_define.group(1)), handler_body):
                failures.append("`%s`'s handler does not stop asking at `%s`: the line would be asked "
                                "for until the dispatcher's own cap stops the run"
                                % (d["file"], ask_define.group(1)))
        unreg = handler_body.find("entry_irq_unregister_client")
        if unreg < 0:
            failures.append("`%s`'s handler never withdraws the registration: the line would be owned "
                            "for the rest of the boot, and the dispatcher's stop for an unregistered "
                            "line would be unreachable from the driver's side" % d["file"])
        elif pend_var is not None:
            guard_region = handler_body[max(end for _v, _s, end in pend_reads):unreg]
            if not re.search(r"\b%s\b\s*&" % re.escape(pend_var), guard_region):
                failures.append("`%s`'s handler withdraws the registration without testing the "
                                "pending bit it just read (`%s & ...`): a line withdrawn while a "
                                "delivery is in flight is the unregistered-intid stop, which is the "
                                "one state a driver must not create" % (d["file"], pend_var))
        if not re.search(r'xnu_live_\w+_isr_unregs\b', handler_body):
            failures.append("`%s`'s handler publishes no `_isr_unregs`, so whether it gave the line "
                            "back at all is not in the record" % d["file"])

    if registered == 0:
        failures.append("no driver in `PLATFORM_SOURCES` registers a client for an interrupt line: "
                        "the reading this step is for - a driver owning a line and writing its device "
                        "- would not exist in any run")


def claim_driver_arming(facts, failures, notes):
    """16. The driver that owns a machine line arms it, and programs its device before it does.

    496 made a driver own a line and raise it itself; 498 made one own a line the *machine* asserts.
    Neither made a line arrive: a registration makes an intid serviceable, and only the distributor
    delivers it. 500 arms the line through `entry_irq_enable_line` **and** puts the deadline into the
    frame, and this claim is about the chain between the registration and the delivery.

    **Which of the two halves was missing is a measurement, and this claim was written believing the
    wrong one.** The first version of its docstring said an SPI is dropped by the distributor because
    `ITARGETSR` reads 0 at reset. That is what a GIC at reset does, and it is not the state this machine
    was in: the run reads the line already enabled, already targeted at CPU 0 and already in Group 0
    (Android's own kernel takes this frame's deadline on this SPI), so every distributor write this step
    makes was a write of a value that was already there, and what had been missing was the deadline in
    the device. The claim's checks are unchanged by that - the writes are the architecture's requirement
    for any machine whose predecessor left the line unconfigured, and the *before* keys are what keep the
    two cases apart - but the reason given for them, here and in the sources, is now the run's.

    Each clause is a way the step could read like an armed line without being one:

      * **the subject is a driver whose line the device tree gave it**, identified by the record it
        publishes for that line - the same variable the registration is made with. A driver that raises
        its own line is not a subject: an SGI does not go through `ITARGETSR` at all, which is why claim
        15 reads that driver, and a file in neither half is *noted* rather than passed over.
      * **every arming call arms the line the driver registered for**, read out of the call's own text -
        an intid armed but not registered reaches the dispatcher with no client, which is the image's
        documented *stop*.
      * **the target is a name and not a number, and its value is checked.** `ITARGETSR`'s byte for an
        SPI is a CPU mask; 0 is the value that means *no CPU*, and a literal in the call is one decision
        written twice with nothing comparing the two.
      * **both halves that can fail are tested before the line is enabled**: the registration's return
        (a full table, a duplicate, or the dispatcher's own timer line can refuse one) and the driver's
        own read-back of the device's `ENABLE` (a store to a part with no clock behind it can vanish).
        Enabling a line for a frame nothing started is what a deadline-less delivery would come from.
      * **the device is programmed before the line is enabled, and the deadline before the enable.** The
        frame has nothing to assert until its control word is written. What makes the arming safe on this
        machine is the *mask* - a disabled or masked frame asserts nothing, and the run shows the frame
        masked before the driver ran and masked again after the last delivery - so the order is the
        image's discipline rather than the thing that saved it, and it is checked as a discipline.
      * **the tick count is derived from the frequency the frame reports** and is never written down: a
        literal 192000 is a fourth definition of 19.2 MHz in an image whose device layer has paid for
        that defect class repeatedly, and the register being programmed counts that clock.
      * **the two outcomes publish, and each publishes a zero before the registration.** `_arm_rc` and
        `_arm_line_rc` are the pair that says *which* half failed - they fail independently - and the
        zero written where the value is not yet known is what makes "the arming refused" a reading
        rather than a missing key (495's rule, and the reason 498's `_isr_*` block is shaped that way).
        The distributor's own before-values are the same rule applied to a *device*: they are what turned
        "the distributor had to be configured" into a question this step could answer.
      * **the handler clears its device on every path, and re-arms from the file-scope record.** The
        line is level-sensitive: a handler that returned with the frame's output still asserted would be
        re-entered immediately and forever. So one path reloads `CNTP_TVAL` - and the reload *is* the
        acknowledgement, because loading a down-counter clears the expired condition - while the other
        masks, which is the end state 498 measured. The reload's number is read from the record the
        *other* call wrote (497's rule made structural: a deadline recomputed here would be a second
        definition of the interval), the zero deadline is refused, and the counts of the two paths are
        published so the last delivery's shape is in the record.
    """
    header = payload_defines(facts["payload_gic_h"], "STAGE90_GIC")
    irq_c = facts.get("payload_irq_c", "") or ""
    armed = 0

    for d in facts["drivers"]:
        source = d["source"]
        code = strip_comments(source)
        defines_here = driver_defines(source)

        line_writes = key_writes(source, "line_intid")
        if not line_writes:
            notes.append("`%s` publishes no `_line_intid`: it owns no line the device tree gave it, so "
                         "the question this claim asks - how a driver arms a machine line - is not about "
                         "this file (a driver that raises its own line is claim 15's subject)"
                         % d["file"])
            continue
        armed += 1
        line_var = line_writes[-1][0]

        own = driver_irq_client(source)
        if own["at"] is None:
            failures.append("`%s` publishes `%s` as the line it owns and registers no client for it: an "
                            "intid nothing is filed for is the dispatcher's stop, so the line this file "
                            "arms is a line the image would end the run on" % (d["file"], line_var))
            continue
        if own["intid"] != line_var:
            failures.append("`%s` registers for `%s` and publishes `%s` as the line the tree gave it: "
                            "two names for one decision, and the arming below is read against the "
                            "published one" % (d["file"], own["intid"], line_var))

        # -- the ABI, in the three places it is written -------------------------------------------------
        if not re.search(r'extern\s+"C"\s+uint32_t\s+entry_irq_enable_line\s*\(', source):
            failures.append("`%s` arms a line and declares no `extern \"C\"` prototype for "
                            "`entry_irq_enable_line`: this file cannot include the payload's header, so "
                            "that declaration is the whole ABI between the two and nothing else compares "
                            "them" % d["file"])
        if "uint32_t entry_irq_enable_line(uint32_t intid, uint32_t target);" not in facts["payload_gic_h"]:
            failures.append("`entry_gic.h` no longer declares "
                            "`entry_irq_enable_line(uint32_t, uint32_t)`: the driver's own `extern \"C\"` "
                            "line would then be the only definition of the ABI on this side of it")
        if "uint32_t entry_irq_enable_line(uint32_t intid, uint32_t target)" not in irq_c:
            failures.append("`entry_irq.c` no longer defines "
                            "`entry_irq_enable_line(uint32_t, uint32_t)`: the header and the driver would "
                            "both be declaring a function the payload does not have, and the link would "
                            "be the only thing that noticed")

        # -- the arming calls --------------------------------------------------------------------------
        calls = driver_arming_calls(source)
        if not calls:
            failures.append("`%s` owns the line `%s` and arms nothing at the distributor: a "
                            "registration is not a delivery, and an SPI whose `ITARGETSR` byte is 0 with "
                            "its enable bit clear is dropped by the distributor whatever handler is "
                            "filed - `_isr_calls` of 0 is what that looks like"
                            % (d["file"], line_var))
            continue
        for intid_text, target_text, _at in calls:
            if intid_text != line_var:
                failures.append("`%s` arms `%s` and owns `%s`: an intid armed that this driver did not "
                                "register for reaches the dispatcher with no client, which is the "
                                "documented stop" % (d["file"], intid_text, line_var))
            if not re.fullmatch(r"[A-Za-z_]\w*", target_text):
                failures.append("`%s` arms `%s` with the target `%s`: the CPU mask has to be a name this "
                                "file defines, because a number in the call is the same decision written "
                                "a second time with nothing comparing the two"
                                % (d["file"], line_var, target_text))
            elif defines_here.get(target_text) is None:
                failures.append("`%s` arms `%s` with the target `%s`, which its own file does not define "
                                "as an integer: the mask would be a number no claim can hold against the "
                                "`ITARGETSR` byte it becomes" % (d["file"], line_var, target_text))
            elif not 1 <= defines_here[target_text] <= 0xff:
                failures.append("`%s`'s `%s` is %d: `GICD_ITARGETSR`'s byte for an SPI is a CPU mask, "
                                "and 0 means *no CPU is targeted* - a line in that state is enabled, "
                                "pending and never delivered, which is one silence this step's record "
                                "could not tell from 'the device never asserted'"
                                % (d["file"], target_text, defines_here[target_text]))
            else:
                notes.append("`%s` arms `%s` with `%s` = 0x%02x, a CPU mask"
                             % (d["file"], line_var, target_text, defines_here[target_text]))

        # -- the device, through the address the driver publishes as its mapping -------------------------
        frame_writes = key_writes(source, "frame_va")
        if not frame_writes:
            failures.append("`%s` arms `%s` and publishes no address for the device it arms: the run "
                            "could not say which device the deadline went into, and the stores below "
                            "could be through any name at all"
                            % (d["file"], line_var))
            continue
        frame_base = frame_writes[-1][0]
        stores = driver_device_stores(source, frame_base)
        ctrl_at = [at for sym, expr, at in stores
                   if sym.endswith("_CTRL_OFF") and "ENABLE" in expr]
        tval_at = [at for sym, expr, at in stores if sym.endswith("_TVAL_OFF")]
        if not ctrl_at:
            failures.append("`%s` programs no `CTRL` through `%s`, the address it publishes as the "
                            "frame's mapping: either the device is never started or the store goes "
                            "through a name the record does not carry, and from a log those are one "
                            "finding - the deadline did not reach the device"
                            % (d["file"], frame_base))
        else:
            first_ctrl = min(ctrl_at)
            early = [at for _i, _t, at in calls if at < first_ctrl]
            if early:
                failures.append("`%s` enables the line at the distributor before it programs the frame: "
                                "an enabled line with a device nothing has started is the one state that "
                                "could deliver an interrupt with no deadline behind it, and closing "
                                "that window is the order's whole reason" % d["file"])
            else:
                notes.append("`%s` programs the frame through `%s` before it enables `%s`"
                             % (d["file"], frame_base, line_var))
            if not tval_at:
                failures.append("`%s` starts its device without ever writing a deadline to a "
                                "`_TVAL_OFF` register: a countdown started at the reset value is not the "
                                "interval this driver says it arms" % d["file"])
            elif min(tval_at) > first_ctrl:
                failures.append("`%s` writes `CTRL` before it writes the deadline: the frame would count "
                                "from whatever value it held, so the interval the record publishes is "
                                "not the interval the device ran" % d["file"])

        # -- the gates ---------------------------------------------------------------------------------
        first_call = min(at for _i, _t, at in calls)
        gate_region = source[own["at"]:first_call]
        if not re.search(r"\b%s\s*!=\s*0u|\b%s\s*==\s*1u"
                         % (re.escape(own["rc"]), re.escape(own["rc"])), gate_region):
            failures.append("`%s` arms the line without testing the registration's return (`%s`): a "
                            "registration can be refused - a full table, a duplicate, or the "
                            "dispatcher's own timer line - and the arming would then be an enabled line "
                            "nobody is filed for" % (d["file"], own["rc"]))
        rc_writes = key_writes(source, "arm_rc")
        line_rc_writes = key_writes(source, "arm_line_rc")
        rc_vars = []
        for writes, key, what in ((rc_writes, "_arm_rc", "the device's read-back of `ENABLE`"),
                                  (line_rc_writes, "_arm_line_rc", "the enabler's own return")):
            if not writes:
                failures.append("`%s` publishes no `%s` (%s): the two halves of an arming fail "
                                "independently, so one key cannot say which of them did"
                                % (d["file"], key, what))
                continue
            if not any(v == "0u" and at < own["at"] for v, at in writes):
                failures.append("`%s` publishes `%s` only where its value is already known: a key "
                                "written after the work it describes cannot distinguish 'the arming "
                                "refused' from 'the arming never ran'" % (d["file"], key))
            computed = [v for v, at in writes if at > own["at"] and v != "0u"]
            if not computed:
                failures.append("`%s` writes `%s` and never publishes what it came out as (%s): the "
                                "record would carry the zero it held before the work and nothing else, "
                                "so an arming that refused and one that never ran would read the same"
                                % (d["file"], key, what))
            elif key == "_arm_rc":
                rc_vars = computed
        if ctrl_at and rc_vars:
            guarded = source[min(ctrl_at):first_call]
            if not re.search(r"\b%s\s*!=\s*0u" % re.escape(rc_vars[-1]), guarded):
                failures.append("`%s` enables the line without testing its own read-back of the device's "
                                "`ENABLE` (`%s != 0u`): enabling a line for a frame that did not start "
                                "is the delivery-with-no-deadline state, and the read-back is the only "
                                "thing that can see it" % (d["file"], rc_vars[-1]))
        elif ctrl_at:
            failures.append("`%s` enables the line and never publishes a computed `_arm_rc`: whether the "
                            "frame took the control write would be a number no run carries, so the gate "
                            "on it is a gate on a value the log does not hold" % d["file"])

        # -- the deadline is derived, not written down ---------------------------------------------------
        ticks_writes = key_writes(source, "arm_ticks")
        handler_sources = []
        if not ticks_writes:
            failures.append("`%s` publishes no `_arm_ticks`: the deadline it programs is a number no "
                            "reading of the artifact can recover" % d["file"])
            ticks_var = None
        else:
            ticks_var = ticks_writes[-1][0]
            # The handler cannot see `start`'s locals, so the value may be reached through a file-scope
            # copy: `g_timer_arm_ticks = arm_ticks` is that copy and the claim reads both names as the
            # one value. A handler that worked the interval out again would have neither name.
            handler_sources = [ticks_var] + [m.group(1) for m in
                                             re.finditer(r"\b(\w+)\s*=\s*%s\s*;"
                                                         % re.escape(ticks_var), source)]
            expr = value_expression(source, ticks_var)
            freq_writes = key_writes(source, "frame_freq")
            freq_var = freq_writes[-1][0] if freq_writes else None
            if expr is None:
                failures.append("`%s` publishes `%s` as its deadline and no statement in this file "
                                "assigns it: the record would carry a value whose source is not here"
                                % (d["file"], ticks_var))
            else:
                # One level down, because the product does not fit the register: the expression is read
                # and so is the definition of each bare name it is made of.
                text = expr
                for name in re.findall(r"\b[A-Za-z_]\w*\b", expr):
                    text += " " + (value_expression(source, name) or "")
                if freq_var is None or not re.search(r"\b%s\b" % re.escape(freq_var), text):
                    failures.append("`%s` derives its deadline from something other than the frequency "
                                    "the frame reports (`%s`): the register it programs counts that "
                                    "clock, so a number here is a second definition of a rate this "
                                    "machine already has three of" % (d["file"], freq_var))
                else:
                    notes.append("`%s`'s deadline is derived from `%s`, the frame's own frequency"
                                 % (d["file"], freq_var))

        # -- the handler's half --------------------------------------------------------------------------
        handler_body = method_body(code, own["handler"]) if own["handler"] else None
        if handler_body is None:
            failures.append("`%s` arms `%s` and defines no `%s` here: the line would be enabled for a "
                            "handler that is not in this file"
                            % (d["file"], line_var, own["handler"]))
            continue
        hb = re.search(r"\*\s*\(\s*volatile\s+uint32_t\s*\*\s*\)\s*\(\s*uintptr_t\s*\)\s*"
                       r"\(\s*(\w+)\s*\+", handler_body)
        hstores = driver_device_stores(handler_body, hb.group(1)) if hb else []
        if not hstores:
            failures.append("`%s`'s handler touches no device address at all: the line's source would "
                            "then never be cleared by its owner, and the dispatcher's cap would be what "
                            "ends the run" % d["file"])
            continue
        h_ticks_name = None
        for sym, expr, _at in hstores:
            if not sym.endswith("_TVAL_OFF"):
                continue
            named = [n for n in handler_sources if re.search(r"\b%s\b" % re.escape(n), expr)]
            if named:
                h_ticks_name = named[0]
                break
        h_tval = [at for sym, _e, at in hstores if sym.endswith("_TVAL_OFF")]
        h_en = [at for sym, expr, at in hstores
                if sym.endswith("_CTRL_OFF") and "ENABLE" in expr]
        h_mask = [at for sym, expr, at in hstores
                  if sym.endswith("_CTRL_OFF") and "IT_MASK" in expr]
        if h_ticks_name is None:
            failures.append("`%s`'s handler never reloads the deadline from `%s`: the frame's line is "
                            "level-sensitive, so a handler that only silenced it would deliver exactly "
                            "once - and a deadline computed here from anything else would be a second "
                            "definition of the interval" % (d["file"], ticks_var or "_arm_ticks"))
        if not h_en:
            failures.append("`%s`'s handler writes no `CTRL` that carries `ENABLE`: the reload of the "
                            "countdown is the acknowledgement, so a path that writes the deadline "
                            "without unmasking reloads a value the device ignores" % d["file"])
        if not h_mask:
            failures.append("`%s`'s handler has no path that masks the frame: a level-sensitive line "
                            "whose last write leaves the output asserted is re-entered immediately and "
                            "forever, and the dispatcher's cap is what would end the run" % d["file"])
        if h_tval and h_en and min(h_tval) > min(h_en):
            failures.append("`%s`'s handler unmasks before it reloads the deadline: between the two "
                            "writes the frame is enabled with an expired countdown, which is the "
                            "condition the reload exists to clear" % d["file"])
        if h_ticks_name and not re.search(r"\b%s\s*!=\s*0u" % re.escape(h_ticks_name), handler_body):
            failures.append("`%s`'s handler re-arms without testing `%s` for zero: a deadline of 0 is "
                            "`CNTP_TVAL`'s 'already expired', so a record nothing wrote would turn the "
                            "next delivery into a storm" % (d["file"], h_ticks_name))
        for suffix, what in (("isr_rearmed", "how many deliveries reloaded the deadline"),
                             ("isr_masked", "how many silenced the frame instead")):
            if not re.search(r'xnu_live_\w+_%s\b' % suffix, handler_body):
                failures.append("`%s`'s handler publishes no `_%s` (%s): which of the two paths the "
                                "deliveries took would not be in the record, so an armed line and a "
                                "line that fired once would read the same"
                                % (d["file"], suffix, what))

    if armed == 0:
        failures.append("no driver in `PLATFORM_SOURCES` owns a line the device tree gave it: the "
                        "reading this step is for - a driver making an SPI arrive - would not exist in "
                        "any run")


def claim_driver_routes(facts, failures, notes):
    """17. A driver that reads a device reads every way the device can be read, and holds them together.

    500 proved the frame's first view against itself - the rate against `mach_absolute_time`, the control
    word's three bits - and against nothing outside it. The device has more than one way to be read, and
    the *evidence* for our reading of it has had the same shape all along: the device's own kernel holds
    **two routes to every counter** - `counter_get_cntpct_mem`/`_cp15` and
    `counter_get_cntvct_mem`/`_cp15` (`arch/arm/kernel/arch_timer.c:297`, `:310`, `:318`, `:331`) - and
    chooses one of the two at boot by `has_cp15` (`:607`), so on the machine that has both the loser is
    never read again by anything. This claim is about the comparison the kernel can make and does not,
    plus the frame's *second* view, which no boot in this project has ever read.

    Each clause is a way a step could read like a proof that the offsets are right while being a
    restatement of them:

      * **the second view is the entry the tree's second `reg` region names**, and it is not the frame.
        The frame's `reg` is two regions (`msm8974.dtsi:161-167`, and the binding at
        `arch_timer.txt:43-44` calls them "the first and second view base addresses"), our tree flattens
        them onto the matched node, and the device's kernel maps only the first. A second view read at
        the frame's own entry would be the *same* device read twice, which is the one comparison that
        cannot fail - so the index is a name in the driver's own defines and the two indices are checked
        to be different, exactly as claim 16 holds the arming's target to a name rather than a number.
      * **both views are compared, and the comparison is bounded by a slack measured on this machine.**
        A counter read twice through two windows differs by however long the reads took, so `==` is a
        claim about how fast the CPU is; `_frame_slack`'s rule (498) is that the tolerance is measured
        by the code that uses it, and the same rule is checked here for both of this step's slacks.
      * **the high words are compared exactly.** A 32-bit difference says nothing if the two readings
        are a `2^32` boundary apart, and the frame's own reader in the device's kernel re-reads the high
        word for that reason (`:318`'s loop) - so a claim that only compared low words would accept a
        comparison that cannot fail for the right reason.
      * **every counter is read both ways, and the coprocessor side is the architected encoding.**
        `mrrc p15, 0`/`mrrc p15, 1` for the two 64-bit counters and `c14, c3, 0` for the virtual
        countdown - the encodings the device's kernel and this image's own `entry_timebase.c` use. A
        "comparison" whose two sides were both the frame would be one reading published twice.
      * **the frame side of each comparison is the same offset symbol the certified block uses.** The
        offsets have one definition in this file and `tools/check_timer_line.py` holds them against the
        device's kernel line by line; a second spelling of `0x038` in the comparison would be this
        project's oldest defect class inside the code written to expose it.
      * **the outcome of each comparison is published with what it was made of** - both low words, both
        high words, the difference, and the bit - because a bit alone cannot say whether the two
        readings disagreed or the block never ran, and the inputs are what a later step would need to
        re-derive it (this file's standing rule: the keys that make an outcome readable are part of the
        outcome).
      * **and the block writes nothing.** The step's whole safety argument is that it cannot disturb the
        OS, because every register it touches is read and never written - the virtual countdown it reads
        is XNU's own decrementer. An argument of that shape has to be structural: the region between the
        second view's mapping and the last coprocessor read is searched for a *store* through either
        mapped base, and a build with one is refused.

    **The run answered both questions, and neither answer is one this claim could have made.** The frame's
    second `reg` region maps and reads **zero everywhere** (`_v2_freq` 0, `_v2_cntv_lo` 0, `_v2_cntp_lo`
    0), so it is not a window onto this frame - the comparison's `_v2_*_ok` of 0 is a fact about the
    machine and not a defect in the driver, and the binding's word for that region is *optional*. And the
    coprocessor agrees with the frame on both counters (`_rt_cntp_d` 7, `_rt_cntv_d` 71, inside a measured
    `_rt_slack` of 104) and on the control word exactly (`_rt_ctl_agree` 1), while the **virtual countdown
    disagrees** (`_rt_tval_fr` 0xf912a3cc against `_rt_tval_cpu` 0x0002d8cb, `_rt_tval_ok` 0) - which is
    the one offset the device's kernel declares and never reads. So the clause above that reads a
    comparison as *nothing but the presence of a comparison* would have been satisfied by a step that
    never looked at the answer: what this claim insists on is that both readings, their difference and
    the verdict are all in the record, so that a disagreement of 0x06f034ff ticks cannot be mistaken for
    a block that never ran. **A claim can require that a question was asked; it cannot require the answer
    it wants** - and the two answers this one got are the reason the step exists.
    """
    subjects = 0
    for d in facts["drivers"]:
        source = d["source"]
        # The two architected encodings and the timebase are also *quoted* in this file's comments -
        # the 501 block names the device's kernel's own `mrrc p15, 1` in prose a few lines above the
        # code that uses it - so the searches below read the source with its comments removed, or a
        # file that had lost the real encoding would still answer (a needle that is a claim about the
        # file's bytes, in its fourth shape: 269/272/297/496/500's family).
        code = strip_comments(source)
        defines_here = driver_defines(source)

        if not key_writes(source, "frame_va") or not key_writes(source, "frame_freq"):
            notes.append("`%s` maps no device that reports a frequency: the two-route question this "
                         "claim asks is about a counter, and this file's device is not one" % d["file"])
            continue
        subjects += 1

        # -- the second view is the tree's second region, named once ------------------------------------
        if not key_writes(source, "v2_va"):
            failures.append("`%s` reads a frame with two `reg` regions and publishes no `_v2_va`: the "
                            "second view is a reading this project has never taken, and a claim about "
                            "the frame's offsets that never leaves the first view is the frame held "
                            "against itself" % d["file"])
            continue
        entries = re.findall(r"getObject\(\s*([A-Za-z_]\w*)\s*\)", source)
        view2 = "MSM8974_TIMER_VIEW2_ENTRY"
        if view2 not in entries:
            failures.append("`%s` maps a second view without naming `%s` as its `reg` entry: the index "
                            "would be a number in the call, which is one decision written twice with "
                            "nothing comparing the two" % (d["file"], view2))
        elif defines_here.get(view2) is None:
            failures.append("`%s` names `%s` and its own file does not define it as an integer: the "
                            "entry the second view is read from would not be in the file that reads it"
                            % (d["file"], view2))
        elif defines_here.get(view2) == defines_here.get("MSM8974_TIMER_FRAME_ENTRY"):
            failures.append("`%s` reads its second view from `%s` = %d, the *frame's* own `reg` entry: "
                            "that is the same device read twice, and the comparison it feeds is one "
                            "that cannot fail"
                            % (d["file"], view2, defines_here[view2]))
        else:
            notes.append("`%s` reads the frame's second view from `%s` = %d, one region past the frame's"
                         % (d["file"], view2, defines_here[view2]))

        # -- the two views are compared, with a measured slack and an exact high word -------------------
        for suffix, what in (("v2_cntv_ok", "the virtual counter"),
                             ("v2_cntp_ok", "the physical counter"),
                             ("v2_cntv_d", "the virtual counter's difference"),
                             ("v2_cntp_d", "the physical counter's difference")):
            if not key_writes(source, suffix):
                failures.append("`%s` publishes no `_%s` (%s): the two views would be read and not "
                                "compared, which is the state this step exists to leave"
                                % (d["file"], suffix, what))
        for base in ("v2_slack", "rt_slack"):
            writes = key_writes(source, base)
            if not writes:
                failures.append("`%s` publishes no `_%s`: the tolerance it compares against would be a "
                                "number no reading of the artifact can recover" % (d["file"], base))
                continue
            literal = [v for v, _at in writes if re.fullmatch(r"0*[0-9]+u?", v or "")]
            if literal:
                failures.append("`%s` publishes `_%s` as the constant `%s`: a slack chosen rather than "
                                "measured is a claim about how fast this CPU is, and the two readings "
                                "it bounds then differ by exactly however much the sign of that claim "
                                "was wrong" % (d["file"], base, literal[0]))
        if not re.search(r"mach_absolute_time\(\)", code):
            failures.append("`%s` slacks off a counter with no timebase behind it: the only thing that "
                            "can say what a counter read costs on this machine is this machine's clock"
                            % d["file"])
        for lo, hi in (("v2_cntv_lo", "v2_cntv_hi"), ("v2_cntp_lo", "v2_cntp_hi"),
                       ("rt_cntv_lo_fr", "rt_cntv_hi_fr"), ("rt_cntp_lo_fr", "rt_cntp_hi_fr"),
                       ("rt_cntv_lo_cpu", "rt_cntv_hi_cpu"), ("rt_cntp_lo_cpu", "rt_cntp_hi_cpu")):
            if not key_writes(source, lo) or not key_writes(source, hi):
                failures.append("`%s` publishes no `_%s`/`_%s` pair: a 32-bit difference between two "
                                "readings of a 64-bit counter means nothing unless the high words are in "
                                "the record too" % (d["file"], lo, hi))
        if not re.search(r"\b\w*_hi\w*\s*==\s*\w*_hi\w*", source):
            failures.append("`%s` compares two 64-bit counters without holding their high words against "
                            "each other: the same comparison across a `2^32` boundary would pass"
                            % d["file"])

        # -- both routes, the architected encodings, and one definition per offset -----------------------
        for enc, what in ((r"mrrc\s+p15\s*,\s*0", "`CNTPCT`'s `mrrc p15, 0`"),
                          (r"mrrc\s+p15\s*,\s*1", "`CNTVCT`'s `mrrc p15, 1`"),
                          (r"mrc\s+p15\s*,\s*0\s*,\s*%0\s*,\s*c14\s*,\s*c3\s*,\s*0",
                           "`CNTV_TVAL`'s `c14, c3, 0`")):
            if not re.search(enc, code):
                failures.append("`%s` never reads %s: the frame's counters are reachable that way on "
                                "this machine, and the device's own kernel reads them both ways in one "
                                "file - a driver that reads one route is holding the frame against "
                                "itself" % (d["file"], what))
        for suffix in ("rt_cntv_ok", "rt_cntp_ok", "rt_tval_ok", "rt_ctl_agree"):
            if not key_writes(source, suffix):
                failures.append("`%s` publishes no `_%s`: the two readings of one value would be in the "
                                "record with no statement about whether they agree"
                                % (d["file"], suffix))
        # The frame side's offsets have to be the symbols the certified block defines, never a literal:
        # `tools/check_timer_line.py` holds those symbols against the device's kernel, and a second
        # spelling in the comparison would be outside that check.
        for m in re.finditer(r"\(\s*uintptr_t\s*\)\s*\(\s*\w+\s*\+\s*"
                             r"(0[xX][0-9a-fA-F]+|\d+)[uU]?\s*\)", code):
            failures.append("`%s` addresses a register at the literal `%s` instead of an offset symbol: "
                            "the device's kernel's line numbers are held against the *symbols* (claim 1 "
                            "of `check_timer_line.py`), and a literal here is a reading that check "
                            "cannot see" % (d["file"], m.group(1)))
        if not re.search(r'\b(?:rt_tval_fr|rt_tval_cpu)\b', source):
            failures.append("`%s` compares no virtual countdown: `QTIMER_CNTV_TVAL_REG` (`arch_timer.c:67`) "
                            "is the one offset the device's kernel declares and never reads, and the "
                            "coprocessor route to it is what this image's own decrementer uses"
                            % d["file"])
        else:
            # Both sides of that one comparison, and each side's *source* rather than its name: the
            # offset symbol on the frame's side (so the certified table stays the one definition) and
            # the coprocessor accessor on the other. A comparison whose two sides read the same
            # register is the shape this whole claim is about.
            tval_expr = value_expression(source, "rt_tval_fr")
            if tval_expr is None or "CNTV_TVAL_OFF" not in tval_expr:
                failures.append("`%s`'s frame-side reading of the virtual countdown is not from "
                                "`MSM8974_FRAME_CNTV_TVAL_OFF` (`%s`): the comparison would be between "
                                "two different countdowns, and the offset that makes it a comparison "
                                "would be outside `check_timer_line.py`'s table" % (d["file"], tval_expr))
            if not re.search(r"\b\w*cpu_cntv_tval\s*\(", source):
                failures.append("`%s` holds the frame's virtual countdown against nothing from the "
                                "coprocessor: `c14, c3, 0` is the other route to that register and the "
                                "one `entry_timebase.c` writes for XNU's own decrementer, so a driver "
                                "that reads only the frame is holding the frame against itself"
                                % d["file"])

        # -- the block writes nothing --------------------------------------------------------------------
        # The region is the two blocks and only them: it begins at the call that takes the *second
        # view's* entry and ends at the last coprocessor control read. The handler's own stores to the
        # frame (500) are before it and the arming block is after it, and a region bounded any wider
        # would refuse the driver for doing the thing the previous step measured - which is how the
        # first version of this clause failed this file, by starting at the *define* whose comment
        # names the entry and so swallowing the handler.
        pick = re.search(r"getObject\(\s*%s\s*\)" % view2, source)
        ctl_call_at = source.rfind("msm8974_cpu_cntv_ctl(")
        if pick and ctl_call_at > pick.start():
            region = source[pick.start():ctl_call_at]
            stores = re.findall(r"\*\s*\(\s*volatile\s+uint32_t\s*\*\s*\)\s*\(\s*uintptr_t\s*\)\s*"
                                r"\(\s*(?:frame_va|v2_va|\w*_va)\s*\+[^;]*?\)\s*=", region)
            if stores:
                failures.append("`%s` writes to a mapped device in the region that reads the second view "
                                "and the second route (%d store(s)): the step's safety argument is that "
                                "every register it touches is read, and the virtual countdown it reads is "
                                "the kernel's own decrementer - which is exactly the argument that cannot "
                                "be left to intent" % (d["file"], len(stores)))
            else:
                notes.append("`%s`'s second-view and two-route region stores to nothing" % d["file"])

    if subjects == 0:
        failures.append("no driver in `PLATFORM_SOURCES` reads a device that reports a frequency: the "
                        "two-route comparison this step is for would not exist in any run")


CLAIMS = (claim_shape, claim_classes, claim_provider, claim_names, claim_root_names,
          claim_property_kinds, claim_bundle_id, claim_cell_counts, claim_resolution_read,
          claim_mechanism, claim_entry_class, claim_device_mapping, claim_device_value,
          claim_timer_callback, claim_driver_line, claim_driver_arming, claim_driver_routes)


def compare(facts, mutate=None):
    if mutate is not None:
        facts = mutate_facts(facts, mutate)
    failures, notes = [], []
    for claim in CLAIMS:
        claim(facts, failures, notes)
    return failures, notes


# ------------------------------------------------------------------------------------------------
# The selftest
# ------------------------------------------------------------------------------------------------

def _bump(text, needle, replacement):
    assert text.count(needle) >= 1, needle
    return text.replace(needle, replacement, 1)


def _bump_driver(facts, klass, needle, replacement):
    """Move one line of one driver's source, as the selftest's mutations have to.

    Only the driver *record*'s copy of the source is moved: the claims read the record, which is built
    from the same file `PLATFORM_SOURCES` compiles, so a mutation here is a mutation of the source as
    far as every claim is concerned.
    """
    facts["drivers"] = [dict(d) for d in facts["drivers"]]
    for d in facts["drivers"]:
        if d["class"] == klass:
            d["source"] = _bump(d["source"], needle, replacement)
            return facts
    raise AssertionError("no driver record for %s" % klass)


def mutate_facts(facts, mutate):
    facts = dict(facts)
    facts["entries"] = [dict(e) for e in (facts["entries"] or [])]
    facts["drivers"] = [dict(d) for d in facts["drivers"]]

    if mutate == "an_entry_loses_its_provider_class":
        for e in facts["entries"]:
            if "IOProviderClass" in e:
                del e["IOProviderClass"]
                break
    elif mutate == "a_class_the_table_names_is_not_compiled":
        for e in facts["entries"]:
            if e.get("IOClass") == "MSM8974Timer":
                e["IOClass"] = "MSM8974GhostTimer"
    elif mutate == "a_compiled_class_is_not_named":
        for i, e in enumerate(facts["entries"]):
            if e.get("IOClass") == "MSM8974Timer":
                del facts["entries"][i]
                break
    elif mutate == "the_device_entry_is_filed_under_the_root_nub":
        for e in facts["entries"]:
            if e.get("IOClass") == "MSM8974Timer":
                e["IOProviderClass"] = "IOPlatformExpertDevice"
    elif mutate == "the_personality_names_a_node_that_is_not_there":
        for e in facts["entries"]:
            if e.get("IOClass") == "MSM8974Timer":
                e["IONameMatch"] = '(timer, "qcom,msm-timerx")'
    elif mutate == "the_second_personality_names_the_first_node":
        for e in facts["entries"]:
            if e.get("IOClass") == "MSM8974GIC":
                e["IONameMatch"] = '(timer, "qcom,msm-timer")'
    elif mutate == "the_driver_stops_comparing_one_name":
        facts = _bump_driver(facts, "MSM8974Timer", 'isEqualTo( "qcom,msm-timer" )',
                             'isEqualTo( "qcom,msm-timer!" )')
    elif mutate == "the_node_loses_the_property_the_driver_reads":
        facts = _bump_driver(facts, "MSM8974Timer", 'provider->getProperty( "frequency" )',
                             'provider->getProperty( "clock-frequency" )')
    elif mutate == "the_root_compatible_moves":
        facts["tree"] = _bump(facts["tree"], '"qcom,msm8974-xnu-stage90"',
                              '"qcom,msm8974-xnu-stage91"')
        facts["root"] = node(facts["tree"], "/")
    elif mutate == "the_resource_personality_asks_for_another_resource":
        for e in facts["entries"]:
            if e.get("IOProviderClass") == "IOResources":
                e["IOResourceMatch"] = "IOBSD2"
    elif mutate == "a_personality_gains_a_bundle_id":
        for e in facts["entries"]:
            if e.get("IOClass") == "MSM8974Timer":
                e["CFBundleIdentifier"] = "com.example.timer"
    elif mutate == "a_node_property_is_read_as_a_number":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'OSDynamicCast( OSData, provider->getProperty( "frequency" ))',
                             'OSDynamicCast( OSString, provider->getProperty( "frequency" ))')
    elif mutate == "the_nub_class_moves_in_apple_s_file":
        facts["nub_class"] = "IOPlatformExpertDevice"
    elif mutate == "a_class_is_defined_twice":
        facts["classes"] = dict(facts["classes"])
        first = sorted(facts["classes"])[0]
        facts["classes"][first] = list(facts["classes"][first]) + ["MSM8974Timer"]
    elif mutate == "the_fallback_entry_is_removed":
        facts["entries"] = [e for e in facts["entries"] if e.get("IOClass") != FALLBACK_CLASS]

    # -- 493's mutations: the declaration on the root, and the reading the drivers make of the OS's
    # answer. Each one is a way the pair of records could still look like a measurement while the two
    # sides of the comparison described different things.
    elif mutate == "the_root_declares_no_cell_counts":
        facts["tree"] = _bump(facts["tree"], '    apple_dt_prop_u32(b, "#address-cells", 1);\n'
                                             '    apple_dt_prop_u32(b, "#size-cells", 1);\n', "")
    elif mutate == "the_declaration_is_the_width_apple_defaults_to":
        facts["tree"] = _bump(facts["tree"], 'apple_dt_prop_u32(b, "#address-cells", 1)',
                              'apple_dt_prop_u32(b, "#address-cells", 2)')
    elif mutate == "the_declaration_makes_the_arrays_the_wrong_count":
        facts["tree"] = _bump(facts["tree"], 'apple_dt_prop_u32(b, "#address-cells", 1)',
                              'apple_dt_prop_u32(b, "#address-cells", 2)')
        facts["tree"] = _bump(facts["tree"], 'apple_dt_prop_u32(b, "#size-cells", 1)',
                              'apple_dt_prop_u32(b, "#size-cells", 2)')
    elif mutate == "the_declaration_says_zero_cells_of_size":
        facts["tree"] = _bump(facts["tree"], 'apple_dt_prop_u32(b, "#size-cells", 1)',
                              'apple_dt_prop_u32(b, "#size-cells", 0)')
    elif mutate == "apple_defaults_a_third_cell":
        facts["cell_defaults"] = {"size": 1, "address": 3}
    elif mutate == "the_driver_divides_by_the_wrong_width":
        facts = _bump_driver(facts, "MSM8974Timer", "regwords / 2u", "regwords / 3u")
    elif mutate == "the_driver_computes_the_count_itself":
        facts = _bump_driver(facts, "MSM8974Timer", "provider->getDeviceMemoryCount()",
                             "3u")
    elif mutate == "the_driver_stops_publishing_a_number_it_compares":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'entry_live_write( "xnu_live_timerdrv_len0", len0 );\n', "")
    elif mutate == "the_driver_writes_live_keys_under_two_prefixes":
        facts = _bump_driver(facts, "MSM8974Timer", '"xnu_live_timerdrv_resolve"',
                             '"xnu_entry_timerdrv_resolve"')
    elif mutate == "a_driver_writes_its_reading_into_the_report":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'entry_live_write( "xnu_live_timerdrv_resolve", resolve );',
                             'entry_live_write( "xnu_live_timerdrv_resolve", resolve );\n'
                             '    entry_write_kv( "xnu_entry_timerdrv_resolve", resolve );')
    elif mutate == "a_driver_writes_a_live_key_the_reader_does_not_look_for":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'entry_live_write( "xnu_live_timerdrv_agree"',
                             'entry_live_write( "timerdrv_agree"')
    elif mutate == "the_nub_resolution_names_another_property":
        facts["nub_resource_prop"] = "assigned-addresses"
    elif mutate == "apple_stops_filing_the_device_memory_array":
        facts["files_devmem_key"] = False
    elif mutate == "resolving_nothing_becomes_an_error":
        facts["nub_resources_failures"] = ["kIOReturnNotReady"]
    elif mutate == "the_passthrough_arm_gains_an_offset":
        facts["resolve_passthrough"] = ("*phys = CellsValue( childAddressCells, cell ); "
                                        "*phys += offset + 0x1000; "
                                        "if (regEntry != startEntry) regEntry->release();")
    elif mutate == "the_offset_starts_at_something_else":
        facts["resolve_offset"] = 4

    # -- 494's mutations: the access, and the one value in it that has two definitions. Each is a way
    # the record could still read like an access while the address, the guard or the compared value
    # came from somewhere other than the OS's own answer.
    elif mutate == "the_driver_maps_a_descriptor_it_built_itself":
        facts = _bump_driver(facts, "MSM8974GIC", "themap = range->map( kIOMapAnywhere );",
                             "themap = regobj->map( kIOMapAnywhere );")
    elif mutate == "the_driver_reads_the_node_s_own_address":
        facts = _bump_driver(facts, "MSM8974GIC",
                             "maptyper = *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_GICD_TYPER_OFF );",
                             "maptyper = *(volatile uint32_t *)(uintptr_t)( reg0 + MSM8974_GICD_TYPER_OFF );")
    elif mutate == "the_driver_drops_the_guard_on_the_address":
        facts = _bump_driver(facts, "MSM8974GIC", "if( mapvaddr != 0u) {", "if( mapvaddr != 1u) {")
    elif mutate == "the_driver_publishes_no_mapping":
        facts = _bump_driver(facts, "MSM8974GIC",
                             'entry_live_write( "xnu_live_gicdrv_map", (uint32_t)(uintptr_t) themap );\n',
                             "")
    elif mutate == "the_driver_stops_publishing_the_mapped_value":
        facts = _bump_driver(facts, "MSM8974GIC",
                             'entry_live_write( "xnu_live_gicdrv_maptyper", maptyper );\n', "")
    elif mutate == "the_driver_reads_at_another_offset":
        facts = _bump_driver(facts, "MSM8974GIC", "#define MSM8974_GICD_TYPER_OFF  0x004u",
                             "#define MSM8974_GICD_TYPER_OFF  0x008u")
    elif mutate == "the_comparison_is_against_a_typed_constant":
        facts = _bump_driver(facts, "MSM8974GIC", "hwok = (maptyper == probetyper) ? 1u : 0u;",
                             "hwok = (maptyper == 0x468u) ? 1u : 0u;")
    elif mutate == "the_header_moves_the_offset":
        facts["payload_gic_h"] = _bump(facts["payload_gic_h"], "#define STAGE90_GICD_TYPER      0x004u",
                                       "#define STAGE90_GICD_TYPER      0x008u")
    elif mutate == "the_payload_publishes_a_constant":
        facts["payload_gic_c"] = _bump(facts["payload_gic_c"],
                                       "g_stage90_gic_dist_typer = dist_typer;",
                                       "g_stage90_gic_dist_typer = 0x468u;")
    elif mutate == "the_payload_stops_publishing_the_value":
        facts["payload_gic_h"] = _bump(facts["payload_gic_h"],
                                       "extern uint32_t g_stage90_gic_dist_typer;\n", "")

    # -- 493's second run's finding: the class the OS's array holds, and the class the driver reads it
    # as. The first of these re-introduces the defect the run measured, so the check is shown to be
    # able to see the thing that actually happened rather than a hypothesis about it.
    elif mutate == "the_driver_reads_the_entry_as_an_io_device_memory":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "range = OSDynamicCast( IOMemoryDescriptor, entry )",
                             "range = OSDynamicCast( IODeviceMemory, entry )")
    elif mutate == "the_driver_stops_publishing_the_entry_kind":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'entry_live_write( "xnu_live_timerdrv_objkind", objkind );\n', "")
    elif mutate == "the_driver_stops_publishing_the_entry_length":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'entry_live_write( "xnu_live_timerdrv_objlen", objlen );\n', "")
    elif mutate == "the_array_holds_something_that_is_not_a_memory_descriptor":
        facts["devmem_built"] = "OSArray"
        facts["devmem_chain"] = class_chain("OSArray")
    elif mutate == "the_factory_cannot_be_read":
        facts["devmem_factory"] = None

    # -- 495's subject: the OS calls a driver back. Every one of these leaves a file that still reads
    # like a driver asking for a timeout, and each is refused for a different link of the chain - which
    # is the point, because the failure the run can see is `_fires = 0` and the cause of a `_fires = 0`
    # is one of five silent returns unless the record separates them.
    elif mutate == "the_driver_creates_no_work_loop":
        facts = _bump_driver(facts, "MSM8974Timer", "workloop = IOWorkLoop::workLoop();\n", "")
    elif mutate == "the_event_source_is_added_to_another_object":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "addrc = (uint32_t) workloop->addEventSource( timer );",
                             "addrc = (uint32_t) timer->addEventSource( timer );")
    elif mutate == "the_callback_is_another_function":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "IOTimerEventSource::timerEventSource( this, msm8974_timer_timeout )",
                             "IOTimerEventSource::timerEventSource( this, msm8974_timer_tick )")
    elif mutate == "the_source_is_never_enabled":
        facts = _bump_driver(facts, "MSM8974Timer", "            timer->enable();\n", "")
    elif mutate == "the_timeout_is_armed_before_the_source_is_enabled":
        facts = _bump_driver(facts, "MSM8974Timer", "            timer->enable();\n", "")
        facts = _bump_driver(
            facts, "MSM8974Timer",
            "            tormc = (uint32_t) timer->setTimeout( MSM8974_TIMER_ASK_MS, kMillisecondScale );\n",
            "            tormc = (uint32_t) timer->setTimeout( MSM8974_TIMER_ASK_MS, kMillisecondScale );\n"
            "            timer->enable();\n")
    elif mutate == "the_deadline_has_a_second_definition_of_the_interval":
        # The needle carries the statement's `;`: the same call is also *quoted in prose* above the
        # code, and the first cut of this mutation replaced the sentence rather than the call - a
        # mutation that ran on the comment and left the code alone, which is the shape of defect this
        # file counts (a claim about a comment is not a claim about the code).
        facts = _bump_driver(facts, "MSM8974Timer",
                             "clock_interval_to_deadline( MSM8974_TIMER_ASK_MS, kMillisecondScale, "
                             "&due );",
                             "clock_interval_to_deadline( 100u, kMillisecondScale, &due );")
    elif mutate == "the_callback_rearms_with_another_interval":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "sender->setTimeout( MSM8974_TIMER_ASK_MS, kMillisecondScale )",
                             "sender->setTimeout( MSM8974_TIMER_REARM_MS, kMillisecondScale )")
    elif mutate == "the_callback_publishes_no_fire_count":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'entry_live_write( "xnu_live_timerdrv_fires", fires );\n', "")
    elif mutate == "the_callback_publishes_no_fire_latency":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'entry_live_write( "xnu_live_timerdrv_fire_lat_ns", (uint32_t) since_arm );\n',
                             "")
    elif mutate == "the_callback_never_rearms":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "g_timer_rearm_rc = (uint32_t) sender->setTimeout( "
                             "MSM8974_TIMER_ASK_MS, kMillisecondScale );",
                             "g_timer_rearm_rc = 0u;")
    elif mutate == "the_fire_count_zero_is_published_after_the_work_loop":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'entry_live_write( "xnu_live_timerdrv_fires", 0u );\n', "")
        facts = _bump_driver(facts, "MSM8974Timer", "workloop = IOWorkLoop::workLoop();\n",
                             "workloop = IOWorkLoop::workLoop();\n"
                             '    entry_live_write( "xnu_live_timerdrv_fires", 0u );\n')
    # -- 496: a driver owns a line and writes its device. Twenty-four ways for the file to read like
    # one without being one, one per link: the switch, the order, the registration, the numbers, and
    # the three things the handler does with its own device.
    elif mutate == "the_driver_has_no_switch":
        facts = _bump_driver(facts, "MSM8974GIC", "#define MSM8974_GIC_DRIVE_SGI 1\n", "")
    elif mutate == "the_switch_is_a_third_value":
        facts = _bump_driver(facts, "MSM8974GIC", "#define MSM8974_GIC_DRIVE_SGI 1",
                             "#define MSM8974_GIC_DRIVE_SGI 2")
    elif mutate == "the_switch_is_never_published":
        facts = _bump_driver(facts, "MSM8974GIC",
                             '    entry_live_write( "xnu_live_gicdrv_drive_compiled", '
                             'MSM8974_GIC_DRIVE_SGI );\n', "")
    elif mutate == "a_store_outside_the_switch":
        # The first `#if` is the one around the handler; the store the claim must still see is the
        # one in *that* block, because the handler is the code a build with the request left out
        # still runs.
        facts = _bump_driver(facts, "MSM8974GIC", "#if MSM8974_GIC_DRIVE_SGI\n", "#if 1\n")
    elif mutate == "the_request_is_not_guarded_on_the_registration":
        facts = _bump_driver(facts, "MSM8974GIC", "if( cli_rc == 1u && mapvaddr != 0u) {",
                             "if( 1 && mapvaddr != 0u) {")
    elif mutate == "the_request_comes_before_the_registration":
        ask = ('    if( cli_rc == 1u && mapvaddr != 0u) {\n'
               '        *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_GICD_SGIR_OFF )\n'
               '            = MSM8974_GICD_SGIR_SELF | MSM8974_GIC_OWN_INTID;\n'
               '        __asm__ volatile ("dsb sy" ::: "memory");\n'
               '        ++g_gic_pends;\n'
               '        entry_live_write( "xnu_live_gicdrv_pends", g_gic_pends );\n'
               '        entry_live_write( "xnu_live_gicdrv_sgir", MSM8974_GICD_SGIR_SELF | '
               'MSM8974_GIC_OWN_INTID );\n'
               '    }\n')
        facts = _bump_driver(facts, "MSM8974GIC", ask, "")
        facts = _bump_driver(facts, "MSM8974GIC",
                             "    cli_rc = entry_irq_register_client(", ask +
                             "    cli_rc = entry_irq_register_client(")
    elif mutate == "the_registration_is_for_another_line":
        facts = _bump_driver(facts, "MSM8974GIC",
                             "entry_irq_register_client( MSM8974_GIC_OWN_INTID,",
                             "entry_irq_register_client( 1u,")
    elif mutate == "the_registration_files_something_that_is_not_the_driver":
        facts = _bump_driver(facts, "MSM8974GIC",
                             "                                        (uint32_t)(uintptr_t) this );",
                             "                                        (uint32_t)(uintptr_t) provider );")
    elif mutate == "the_registered_function_is_not_in_this_file":
        facts = _bump_driver(facts, "MSM8974GIC",
                             "                                        (uint32_t)(uintptr_t) "
                             "&msm8974_gic_isr,",
                             "                                        (uint32_t)(uintptr_t) "
                             "&msm8974_gic_ghost,")
    elif mutate == "the_handler_address_is_not_published":
        facts = _bump_driver(facts, "MSM8974GIC",
                             '    entry_live_write( "xnu_live_gicdrv_isr", '
                             '(uint32_t)(uintptr_t) &msm8974_gic_isr );\n', "")
    elif mutate == "the_registration_result_is_not_published":
        facts = _bump_driver(facts, "MSM8974GIC",
                             '    entry_live_write( "xnu_live_gicdrv_cli_rc", cli_rc );\n', "")
    elif mutate == "the_driver_declares_no_prototype_for_the_registry":
        facts = _bump_driver(facts, "MSM8974GIC",
                             'extern "C" uint32_t entry_irq_register_client(uint32_t intid, '
                             'uint32_t handler, uint32_t refCon);\n', "")
    elif mutate == "an_offset_disagrees_with_the_header":
        facts = _bump_driver(facts, "MSM8974GIC", "#define MSM8974_GICD_SGIR_OFF       0xf00u",
                             "#define MSM8974_GICD_SGIR_OFF       0xf04u")
    elif mutate == "the_self_test_filter_disagrees_with_the_header":
        facts = _bump_driver(facts, "MSM8974GIC", "(2u << 24)", "(1u << 24)")
    elif mutate == "the_driver_stops_reading_the_enable_back":
        facts = _bump_driver(facts, "MSM8974GIC",
                             '    entry_live_write( "xnu_live_gicdrv_en_after", en_after );\n', "")
    elif mutate == "the_header_moves_the_sgir":
        facts = dict(facts)
        facts["payload_gic_h"] = _bump(facts["payload_gic_h"],
                                       "#define STAGE90_GICD_SGIR       0xf00u",
                                       "#define STAGE90_GICD_SGIR       0xf04u")
    elif mutate == "the_handler_does_not_read_the_pending_bit":
        facts = _bump_driver(facts, "MSM8974GIC",
                             "        pend = *(volatile uint32_t *)(uintptr_t)( g_gic_mapvaddr + "
                             "MSM8974_GICD_ISPENDR0_OFF );",
                             "        pend = 0u;")
        facts = _bump_driver(facts, "MSM8974GIC",
                             "        g_gic_isr_pend = *(volatile uint32_t *)(uintptr_t)( "
                             "g_gic_mapvaddr + MSM8974_GICD_ISPENDR0_OFF );",
                             "        g_gic_isr_pend = 0u;")
    elif mutate == "the_handler_does_not_ask_again":
        facts = _bump_driver(facts, "MSM8974GIC",
                             "        *(volatile uint32_t *)(uintptr_t)( g_gic_mapvaddr + "
                             "MSM8974_GICD_SGIR_OFF )",
                             "        *(volatile uint32_t *)(uintptr_t)( g_gic_mapvaddr + "
                             "MSM8974_GICD_ISENABLER0_OFF )")
    elif mutate == "the_handler_stops_asking":
        facts = _bump_driver(facts, "MSM8974GIC", "calls < MSM8974_GIC_ASK_PENDS", "calls < 1u")
    elif mutate == "the_driver_answers_the_line_forever":
        facts = _bump_driver(facts, "MSM8974GIC", "#define MSM8974_GIC_ASK_PENDS       3u",
                             "#define MSM8974_GIC_ASK_PENDS       1u")
    elif mutate == "the_handler_withdraws_without_testing_the_bit":
        facts = _bump_driver(facts, "MSM8974GIC", "( g_gic_isr_pend & bit ) == 0u",
                             "( 0u & bit ) == 0u")
    elif mutate == "the_handler_never_withdraws":
        facts = _bump_driver(facts, "MSM8974GIC",
                             "        g_gic_isr_unreg_rc = entry_irq_unregister_client( "
                             "MSM8974_GIC_OWN_INTID );",
                             "        g_gic_isr_unreg_rc = 0u;")
    elif mutate == "the_handler_publishes_no_call_count":
        facts = _bump_driver(facts, "MSM8974GIC",
                             '    entry_live_write( "xnu_live_gicdrv_isr_calls", calls );\n', "")
    elif mutate == "the_handler_writes_through_an_unpublished_address":
        facts = _bump_driver(facts, "MSM8974GIC", "    g_gic_mapvaddr = mapvaddr;",
                             "    g_gic_mapvaddr = 0xf9000000u;")

    # -- 500's mutations: the driver that owns a machine line arms it, and the device comes first. Each
    # one is a way the file could still read like an armed line while the line never arrives, or arrives
    # before there is a deadline behind it. The first two are the *device* half of the GICv2 blind spot:
    # a line with no CPU targeted is dropped however the handler is filed.
    elif mutate == "the_target_is_zero":
        facts = _bump_driver(facts, "MSM8974Timer", "#define MSM8974_TIMER_LINE_TARGET 1u",
                             "#define MSM8974_TIMER_LINE_TARGET 0u")
    elif mutate == "the_line_is_armed_with_a_literal_target":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "entry_irq_enable_line( line, MSM8974_TIMER_LINE_TARGET )",
                             "entry_irq_enable_line( line, 1u )")
    elif mutate == "the_line_armed_is_not_the_line_registered":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "entry_irq_enable_line( line, MSM8974_TIMER_LINE_TARGET )",
                             "entry_irq_enable_line( line + 1u, MSM8974_TIMER_LINE_TARGET )")
    elif mutate == "the_line_is_armed_before_the_device":
        # The arming hoisted above the gate that programs the frame: the failure this is for is an
        # enabled line with a device nothing has started, which is a delivery with no deadline behind it.
        facts = _bump_driver(facts, "MSM8974Timer",
                             "    if( line != 0u && line_cli_rc != 0u && frame_va != 0u && "
                             "frame_freq != 0u) {",
                             "    arm_line_rc = entry_irq_enable_line( line, "
                             "MSM8974_TIMER_LINE_TARGET );\n"
                             "    if( line != 0u && line_cli_rc != 0u && frame_va != 0u && "
                             "frame_freq != 0u) {")
    elif mutate == "the_enable_is_written_before_the_deadline":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "            *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CNTP_TVAL_OFF ) = arm_ticks;\n"
                             "            *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CTRL_OFF ) =\n"
                             "                MSM8974_FRAME_CTRL_ENABLE;",
                             "            *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CTRL_OFF ) =\n"
                             "                MSM8974_FRAME_CTRL_ENABLE;\n"
                             "            *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CNTP_TVAL_OFF ) = arm_ticks;")
    elif mutate == "the_frame_is_never_enabled":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "            *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CNTP_TVAL_OFF ) = arm_ticks;\n"
                             "            *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CTRL_OFF ) =\n"
                             "                MSM8974_FRAME_CTRL_ENABLE;",
                             "            *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CNTP_TVAL_OFF ) = arm_ticks;\n"
                             "            *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CTRL_OFF ) =\n"
                             "                MSM8974_FRAME_CTRL_IT_MASK;")
    elif mutate == "the_arming_is_not_guarded_on_the_registration":
        facts = _bump_driver(facts, "MSM8974Timer", "line_cli_rc != 0u && ", "")
    elif mutate == "the_arming_is_not_guarded_on_the_device":
        facts = _bump_driver(facts, "MSM8974Timer", "            if( arm_rc != 0u) {",
                             "            if( 1u) {")
    elif mutate == "the_arming_return_is_not_published":
        facts = _bump_driver(facts, "MSM8974Timer",
                             '    entry_live_write( "xnu_live_timerdrv_arm_line_rc",   '
                             'arm_line_rc );\n', "")
    elif mutate == "the_device_read_back_is_not_published":
        facts = _bump_driver(facts, "MSM8974Timer",
                             '    entry_live_write( "xnu_live_timerdrv_arm_rc",        arm_rc );\n', "")
    elif mutate == "the_records_are_not_published_before_the_registration":
        facts = _bump_driver(facts, "MSM8974Timer",
                             '    entry_live_write( "xnu_live_timerdrv_arm_rc", 0u );\n'
                             '    entry_live_write( "xnu_live_timerdrv_arm_line_rc", 0u );\n', "")
    elif mutate == "the_driver_declares_no_prototype_for_the_arming":
        facts = _bump_driver(facts, "MSM8974Timer",
                             'extern "C" uint32_t entry_irq_enable_line(uint32_t intid, '
                             'uint32_t target);\n', "")
    elif mutate == "the_tick_count_is_written_down":
        facts = _bump_driver(facts, "MSM8974Timer", "arm_ticks = (uint32_t) want;",
                             "arm_ticks = 192000u;")
    elif mutate == "the_deadline_is_a_second_definition_of_the_rate":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "((uint64_t) frame_freq * (uint64_t) MSM8974_FRAME_ARM_MS) / 1000u",
                             "(uint64_t) 192000u")
    elif mutate == "the_handler_re_arms_from_another_number":
        facts = _bump_driver(facts, "MSM8974Timer", "                g_timer_arm_ticks;",
                             "                192000u;")
    elif mutate == "the_handler_never_masks":
        facts = _bump_driver(facts, "MSM8974Timer", "ctl | MSM8974_FRAME_CTRL_IT_MASK;", "ctl;")
    elif mutate == "the_handler_re_arms_with_a_zero_deadline":
        facts = _bump_driver(facts, "MSM8974Timer", "g_timer_arm_ticks != 0u && ", "")
    elif mutate == "the_handler_publishes_no_rearm_count":
        facts = _bump_driver(facts, "MSM8974Timer",
                             '    entry_live_write( "xnu_live_timerdrv_isr_rearmed", '
                             'g_timer_isr_rearmed );\n', "")

    # -- 501's mutations: the frame's second view, and the frame's second route to the same counters.
    # Eleven ways the step could read like a comparison of two readings while being one reading
    # published twice, or a comparison whose bound is a claim rather than a measurement - and the last
    # one is the read-only property the step's safety rests on.
    elif mutate == "the_second_view_is_the_frame":
        facts = _bump_driver(facts, "MSM8974Timer", "#define MSM8974_TIMER_VIEW2_ENTRY  2u",
                             "#define MSM8974_TIMER_VIEW2_ENTRY  1u")
    elif mutate == "the_second_view_is_a_number_in_the_call":
        facts = _bump_driver(facts, "MSM8974Timer", "getObject( MSM8974_TIMER_VIEW2_ENTRY )",
                             "getObject( 2u )")
    elif mutate == "the_second_view_has_no_name":
        facts = _bump_driver(facts, "MSM8974Timer", "#define MSM8974_TIMER_VIEW2_ENTRY  2u\n", "")
    elif mutate == "the_views_are_not_compared":
        facts = _bump_driver(facts, "MSM8974Timer",
                             '    entry_live_write( "xnu_live_timerdrv_v2_cntv_ok",    v2_cntv_ok );\n',
                             "")
    elif mutate == "the_view_slack_is_a_literal":
        facts = _bump_driver(facts, "MSM8974Timer",
                             '    entry_live_write( "xnu_live_timerdrv_v2_slack",      v2_slack );',
                             '    entry_live_write( "xnu_live_timerdrv_v2_slack",      64u );')
    elif mutate == "the_high_words_are_not_published":
        facts = _bump_driver(facts, "MSM8974Timer",
                             '    entry_live_write( "xnu_live_timerdrv_v2_cntv_hi",    v2_cntv_hi );\n',
                             "")
    elif mutate == "the_coprocessor_read_is_not_the_architected_one":
        facts = _bump_driver(facts, "MSM8974Timer",
                             '__asm__ volatile ("mrrc p15, 1, %0, %1, c14" : "=r"(lo), "=r"(hi));',
                             'lo = 0u; hi = 0u;')
    elif mutate == "the_virtual_countdown_is_read_from_another_register":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "        rt_tval_fr  = *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CNTV_TVAL_OFF );",
                             "        rt_tval_fr  = *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CNTP_TVAL_OFF );")
    elif mutate == "the_route_offsets_are_written_again":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "            rt_cntp_lo_fr = *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "MSM8974_FRAME_CNTP_LOW_OFF );",
                             "            rt_cntp_lo_fr = *(volatile uint32_t *)(uintptr_t)( frame_va + "
                             "0x000u );")
    elif mutate == "the_route_words_are_not_published":
        facts = _bump_driver(facts, "MSM8974Timer",
                             '    entry_live_write( "xnu_live_timerdrv_rt_cntv_lo_cpu", rt_cntv_lo_cpu );\n',
                             "")
    elif mutate == "the_second_view_is_written_to":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "                        v2_cntv_lo = *(volatile uint32_t *)(uintptr_t)"
                             "( v2_va + MSM8974_FRAME_CNTV_LOW_OFF );",
                             "                        *(volatile uint32_t *)(uintptr_t)( v2_va + "
                             "MSM8974_FRAME_CNTV_LOW_OFF ) = v2_cntv_lo;")
    else:
        raise AssertionError("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "an_entry_loses_its_provider_class",
    "a_class_the_table_names_is_not_compiled",
    "a_compiled_class_is_not_named",
    "the_device_entry_is_filed_under_the_root_nub",
    "the_personality_names_a_node_that_is_not_there",
    "the_second_personality_names_the_first_node",
    "the_driver_stops_comparing_one_name",
    "the_node_loses_the_property_the_driver_reads",
    "a_node_property_is_read_as_a_number",
    "the_root_compatible_moves",
    "the_resource_personality_asks_for_another_resource",
    "a_personality_gains_a_bundle_id",
    "the_nub_class_moves_in_apple_s_file",
    "a_class_is_defined_twice",
    "the_fallback_entry_is_removed",
    "the_root_declares_no_cell_counts",
    "the_declaration_is_the_width_apple_defaults_to",
    "the_declaration_makes_the_arrays_the_wrong_count",
    "the_declaration_says_zero_cells_of_size",
    "apple_defaults_a_third_cell",
    "the_driver_divides_by_the_wrong_width",
    "the_driver_computes_the_count_itself",
    "the_driver_stops_publishing_a_number_it_compares",
    "the_driver_writes_live_keys_under_two_prefixes",
    "a_driver_writes_its_reading_into_the_report",
    "a_driver_writes_a_live_key_the_reader_does_not_look_for",
    "the_nub_resolution_names_another_property",
    "apple_stops_filing_the_device_memory_array",
    "resolving_nothing_becomes_an_error",
    "the_passthrough_arm_gains_an_offset",
    "the_offset_starts_at_something_else",
    "the_driver_reads_the_entry_as_an_io_device_memory",
    "the_driver_stops_publishing_the_entry_kind",
    "the_driver_stops_publishing_the_entry_length",
    "the_array_holds_something_that_is_not_a_memory_descriptor",
    "the_factory_cannot_be_read",
    "the_driver_maps_a_descriptor_it_built_itself",
    "the_driver_reads_the_node_s_own_address",
    "the_driver_drops_the_guard_on_the_address",
    "the_driver_publishes_no_mapping",
    "the_driver_stops_publishing_the_mapped_value",
    "the_driver_reads_at_another_offset",
    "the_comparison_is_against_a_typed_constant",
    "the_header_moves_the_offset",
    "the_payload_publishes_a_constant",
    "the_payload_stops_publishing_the_value",
    # 495: the OS calls a driver back. Eleven ways for a file to read like an armed timeout with no
    # timeout in it, one per link of the chain.
    "the_driver_creates_no_work_loop",
    "the_event_source_is_added_to_another_object",
    "the_callback_is_another_function",
    "the_source_is_never_enabled",
    "the_timeout_is_armed_before_the_source_is_enabled",
    "the_deadline_has_a_second_definition_of_the_interval",
    "the_callback_rearms_with_another_interval",
    "the_callback_publishes_no_fire_count",
    "the_callback_publishes_no_fire_latency",
    "the_callback_never_rearms",
    "the_fire_count_zero_is_published_after_the_work_loop",
    # 496: a driver owns a line and writes its device.
    "the_driver_has_no_switch",
    "the_switch_is_a_third_value",
    "the_switch_is_never_published",
    "a_store_outside_the_switch",
    "the_request_is_not_guarded_on_the_registration",
    "the_request_comes_before_the_registration",
    "the_registration_is_for_another_line",
    "the_registration_files_something_that_is_not_the_driver",
    "the_registered_function_is_not_in_this_file",
    "the_handler_address_is_not_published",
    "the_registration_result_is_not_published",
    "the_driver_declares_no_prototype_for_the_registry",
    "an_offset_disagrees_with_the_header",
    "the_self_test_filter_disagrees_with_the_header",
    "the_driver_stops_reading_the_enable_back",
    "the_header_moves_the_sgir",
    "the_handler_does_not_read_the_pending_bit",
    "the_handler_does_not_ask_again",
    "the_handler_stops_asking",
    "the_driver_answers_the_line_forever",
    "the_handler_withdraws_without_testing_the_bit",
    "the_handler_never_withdraws",
    "the_handler_publishes_no_call_count",
    "the_handler_writes_through_an_unpublished_address",
    # 500: a driver owning a line the machine asserts, and arming it. Eighteen ways for the arming to be
    # a file that reads like one - the target, the line, the order, both gates, the two records, the
    # derived count, and the handler's own two paths.
    "the_target_is_zero",
    "the_line_is_armed_with_a_literal_target",
    "the_line_armed_is_not_the_line_registered",
    "the_line_is_armed_before_the_device",
    "the_enable_is_written_before_the_deadline",
    "the_frame_is_never_enabled",
    "the_arming_is_not_guarded_on_the_registration",
    "the_arming_is_not_guarded_on_the_device",
    "the_arming_return_is_not_published",
    "the_device_read_back_is_not_published",
    "the_records_are_not_published_before_the_registration",
    "the_driver_declares_no_prototype_for_the_arming",
    "the_tick_count_is_written_down",
    "the_deadline_is_a_second_definition_of_the_rate",
    "the_handler_re_arms_from_another_number",
    "the_handler_never_masks",
    "the_handler_re_arms_with_a_zero_deadline",
    "the_handler_publishes_no_rearm_count",
    # 501: the frame's second view and its second route to the same counters. Eleven ways the step could
    # read like a comparison of two readings while being one reading published twice, a comparison whose
    # bound is a claim, or a comparison with a write in it.
    "the_second_view_is_the_frame",
    "the_second_view_is_a_number_in_the_call",
    "the_second_view_has_no_name",
    "the_views_are_not_compared",
    "the_view_slack_is_a_literal",
    "the_high_words_are_not_published",
    "the_coprocessor_read_is_not_the_architected_one",
    "the_virtual_countdown_is_read_from_another_register",
    "the_route_offsets_are_written_again",
    "the_route_words_are_not_published",
    "the_second_view_is_written_to",
)


def selftest(facts):
    """The baseline first, then every mutation, which has to be refused."""
    baseline, _notes = compare(facts)
    if baseline:
        print("FAIL: the selftest's own baseline fails %d claim(s), so 'every mutation was refused' "
              "would be true for the wrong reason. Fix these first:" % len(baseline), file=sys.stderr)
        for failure in baseline:
            print("      " + failure, file=sys.stderr)
        return 1
    accepted = []
    for name in MUTATIONS:
        failures, _notes = compare(facts, mutate=name)
        if not failures:
            accepted.append(name)
            print("      ACCEPTED: %s" % name, file=sys.stderr)
    if accepted:
        print("FAIL: %d of %d mutations were not refused: %s"
              % (len(accepted), len(MUTATIONS), ", ".join(accepted)), file=sys.stderr)
        return 1
    print("  --selftest: all %d mutations were refused" % len(MUTATIONS))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--build-script", default=BUILD_SCRIPT)
    ap.add_argument("--table", default=TABLE)
    ap.add_argument("--tree-source", default=TREE_SOURCE)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    for path in (args.build_script, args.table, args.tree_source, PLATFORM_EXPERT_CPP, BSD_INIT_CPP,
                 PAYLOAD_GIC_C, PAYLOAD_GIC_H):
        if not os.path.exists(path):
            print("missing %s" % path, file=sys.stderr)
            return 2

    facts = gather(args)
    if args.selftest:
        return selftest(facts)

    failures, notes = compare(facts)
    if args.verbose:
        for note in notes:
            print("    " + note)
    if failures:
        print("FAIL: the personality table and the drivers it names disagree:", file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    print("ok: every personality is filed under a class a service can have, names a class one object "
          "defines, and asks for names the tree and the driver both carry")
    return 0


if __name__ == "__main__":
    sys.exit(main())
