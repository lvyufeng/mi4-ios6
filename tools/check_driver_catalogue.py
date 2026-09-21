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

# The two provider classes the kernel itself creates, and so the two a personality may name without any
# device-tree fact behind it: `IOPlatformExpertDevice` is the root nub `StartIOKit` builds
# (`IOPlatformExpert.cpp:1290-1300` via `new IOPlatformExpertDevice`) and `IOResources` is built by
# `IOService::setPlatform` and registered before the catalogue is first consulted.
KERNEL_CREATED = ("IOPlatformExpertDevice", "IOResources")

# Apple's designed fallback, the one entry of the stock table (`iokit/KernelConfigTables.cpp:35`) this
# project's table reproduces verbatim and last: it names a class this project does not define.
FALLBACK_CLASS = "IOPanicPlatform"


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
    m2 = re.search(r"(\w+)\s*\*\s*%s\s*=\s*OSDynamicCast\(\s*(\w+)\s*," % re.escape(var), source)
    if not m2:
        return None
    return {"var": var, "declared": m2.group(1), "cast_to": m2.group(2)}


def live_keys(source):
    """A driver's `entry_live_write` keys, as `{common_prefix: {suffix, ...}}`.

    The prefix is what makes two drivers' records comparable: 493's two drivers publish the same
    suffixes under different prefixes, so a key-by-key comparison of the two rows is a comparison of
    the same reading on two nodes rather than of two differently-shaped records.
    """
    out = {}
    for key in re.findall(r'entry_live_write\(\s*"([^"]+)"', source):
        head, _, tail = key.rpartition("_")
        out.setdefault(head, set()).add(tail)
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
        if not re.search(r'xnu_live_\w+_objkind', d["source"]):
            failures.append("`%s` does not publish the kind of object it found in the OS's array: "
                            "'the OS resolved nothing' and 'this reader's cast answered 0' are the two "
                            "different findings that produced the same zero before 493's second run"
                            % d["file"])
        if not re.search(r'xnu_live_\w+_objlen', d["source"]):
            failures.append("`%s` does not publish the entry's own `getLength()`: it is the reading "
                            "that separates a well-formed descriptor (`_objlen` = the node's first "
                            "size) from a segment walk that returned nothing (both `_phys0` and "
                            "`_len0` 0 with `_objlen` right)" % d["file"])


CLAIMS = (claim_shape, claim_classes, claim_provider, claim_names, claim_root_names,
          claim_property_kinds, claim_bundle_id, claim_cell_counts, claim_resolution_read,
          claim_mechanism, claim_entry_class)


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

    # -- 493's second run's finding: the class the OS's array holds, and the class the driver reads it
    # as. The first of these re-introduces the defect the run measured, so the check is shown to be
    # able to see the thing that actually happened rather than a hypothesis about it.
    elif mutate == "the_driver_reads_the_entry_as_an_io_device_memory":
        facts = _bump_driver(facts, "MSM8974Timer",
                             "IOMemoryDescriptor * range = OSDynamicCast( IOMemoryDescriptor, entry )",
                             "IODeviceMemory * range = OSDynamicCast( IODeviceMemory, entry )")
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

    for path in (args.build_script, args.table, args.tree_source, PLATFORM_EXPERT_CPP, BSD_INIT_CPP):
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
