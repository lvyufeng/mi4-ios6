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
BSD_INIT_CPP = os.path.join(IOKIT, "bsddev", "IOKitBSDInit.cpp")

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


def node(tree_text, name):
    """A `apple_dt_node_begin(...)` block of the payload's tree, by the `name` it publishes.

    The payload writes the tree with `apple_dt_*` calls in one function; a node ends at the next
    `apple_dt_node_begin(`, and its properties are the `apple_dt_prop_*` calls in between. The text is
    passed in rather than a path because the selftest mutates it.
    """
    text = strip_comments(tree_text)
    blocks = text.split("apple_dt_node_begin")
    for block in blocks[1:]:
        props = {}
        for kind, key, value in re.findall(
                r'apple_dt_prop_(\w+)\s*\(\s*\w+\s*,\s*"([^"]+)"\s*,\s*(.*?)\);', block):
            if kind == "u32" or kind == "u32_array":
                props[key] = value.strip()
            else:
                m = re.fullmatch(r'"([^"]*)"', value.strip())
                props[key] = m.group(1) if m else value.strip()
        if props.get("name") == name:
            return props
    return None


def read_int(text):
    """A C integer literal of this tree's style, `19200000u` included."""
    m = re.match(r"\s*(\d+)", text)
    return int(m.group(1)) if m else None


def gather(args):
    facts = {
        "table": read(args.table),
        "tree": read(args.tree_source),
        "timer": read(os.path.join(REPO_ROOT, "stages", "stage90", "xnu_platform",
                                   "MSM8974Timer.cpp")),
    }
    facts["sources"] = platform_sources(args.build_script)
    facts["entries"] = table_entries(args.table)
    facts["classes"] = {}
    for path in (facts["sources"] or []):
        facts["classes"][path] = defined_classes(path)
    facts["nub_class"] = create_nub_class()
    facts["resource"] = published_resource()
    facts["root"] = node(facts["tree"], "/")
    facts["timer_node"] = node(facts["tree"], "timer")
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
    """4. The device entry's names are the node's names and the driver's own."""
    entries = entries_of(facts, failures)
    nub = facts["nub_class"]
    node_props = facts["timer_node"]
    if node_props is None:
        failures.append("the payload's tree has no node named `timer`: the personality's "
                        "`IONameMatch` names would be names of nothing")
        return
    node_names = [node_props.get("name"), node_props.get("compatible")]

    device = [e for e in entries if e.get("IOProviderClass") == nub]
    if not device:
        return                      # claim 3 has already said this
    for entry in device:
        if "IONameMatch" not in entry:
            failures.append("`%s` has no `IONameMatch`: without it the probe cannot name the "
                            "provider, so the personality would match nothing even in the right "
                            "bucket" % entry.get("IOClass"))
            continue
        got = names_of(entry["IONameMatch"])
        if sorted(got) != sorted(n for n in node_names if n):
            failures.append("`%s`'s `IONameMatch` is %s and the `/timer` node's `name`/`compatible` "
                            "are %s: the candidate test is `IODTCompareNubName` over the provider's "
                            "own properties, so a name the node does not carry is a probe that "
                            "answers false" % (entry.get("IOClass"), got, node_names))

        # The driver's own list, which is the one that decides whether a match on a name this driver
        # does not know reads as a match at all (`IONameMatched`).
        compared = re.findall(r'isEqualTo\(\s*"([^"]+)"\s*\)', facts["timer"])
        if sorted(compared) != sorted(got):
            failures.append("`MSM8974Timer.cpp` compares `IONameMatched` against %s while the "
                            "personality lists %s: one of the two lists moved, and a match on a name "
                            "the driver does not know is worse than no match - it starts the driver "
                            "on the wrong fact" % (compared, got))
        else:
            notes.append("`%s`'s names are the node's and the driver's: %s"
                         % (entry.get("IOClass"), ", ".join(got)))

        # And the properties the driver reads have to be properties the node has: a driver reading a
        # property nothing writes records a zero, which reads the same as a zero the device has.
        read_props = sorted(set(re.findall(r'provider->getProperty\(\s*"([^"]+)"\s*\)',
                                          facts["timer"])))
        missing = [p for p in read_props if p not in node_props]
        if missing:
            failures.append("`MSM8974Timer.cpp` reads %s from its provider and the `/timer` node "
                            "publishes no such property(ies): the driver would record a zero and "
                            "count it as a reading" % ", ".join(missing))
        else:
            notes.append("every property the driver reads is published by the node: %s"
                         % ", ".join(read_props) if read_props else "the driver reads no property")

        # The frequency is the one value the device tree and the driver both name, and the driver's
        # whole point is comparing it with the machine's own answer - so the node has to carry it.
        if "frequency" in read_props and read_int(node_props.get("frequency", "")) is None:
            failures.append("the `/timer` node's `frequency` is not an integer literal of the "
                            "payload's own style, so the comparison the driver makes has no left "
                            "side")


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
    """6. No personality carries a `CFBundleIdentifier`."""
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
    """5. A device-tree property is read as the kind the device-tree plane produces.

    Every property of a device-tree node becomes an **`OSData`** when the node is turned into a
    registry entry - `data = OSData::withBytes( prop, propSize )` (`IODeviceTreeSupport.cpp:379`), for
    the node's `name` as much as for its `frequency`. So a driver that reaches for one with
    `OSDynamicCast( OSNumber, provider->getProperty( name ))` finds nothing, and - the reason this is a
    check and not a comment - the record it leaves reads "the tree has no such property", which is a
    statement about the tree. 492's first device run is exactly that: `_have = 2`, `_freq = 0`, with
    `reg` read fine beside it, and nothing in the log saying the cast was the wrong kind.
    """
    entries = entries_of(facts, failures)
    node_props = facts["timer_node"]
    if node_props is None:
        return
    for prop in sorted(set(re.findall(r'provider->getProperty\(\s*"([^"]+)"\s*\)', facts["timer"]))):
        if prop not in node_props:
            continue                    # claim 4 has said this
        if not re.search(r'OSDynamicCast\(\s*OSData\s*,\s*provider->getProperty\(\s*"%s"\s*\)'
                         % re.escape(prop), facts["timer"]):
            failures.append("`MSM8974Timer.cpp` reads the node's `%s` without ever casting it to "
                            "`OSData`, the kind a device-tree node's property has "
                            "(`IODeviceTreeSupport.cpp:379`): the record would say the tree has no "
                            "`%s`, which is a claim about the tree made by a reader with the wrong "
                            "type" % (prop, prop))
    if entries:
        notes.append("every device-tree property this driver reads is read as `OSData`, the kind the "
                     "plane produces")


CLAIMS = (claim_shape, claim_classes, claim_provider, claim_names, claim_root_names,
          claim_property_kinds, claim_bundle_id)


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


def mutate_facts(facts, mutate):
    facts = dict(facts)
    facts["entries"] = [dict(e) for e in (facts["entries"] or [])]

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
    elif mutate == "the_driver_names_a_node_that_is_not_there":
        for e in facts["entries"]:
            if e.get("IOClass") == "MSM8974Timer":
                e["IONameMatch"] = '(timer, "qcom,msm-timerx")'
    elif mutate == "the_driver_stops_comparing_one_name":
        facts["timer"] = _bump(facts["timer"], 'isEqualTo( "qcom,msm-timer" )',
                               'isEqualTo( "qcom,msm-timer!" )')
    elif mutate == "the_node_loses_the_property_the_driver_reads":
        facts["timer"] = _bump(facts["timer"], 'provider->getProperty( "frequency" )',
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
        facts["timer"] = _bump(facts["timer"],
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
    else:
        raise AssertionError("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "an_entry_loses_its_provider_class",
    "a_class_the_table_names_is_not_compiled",
    "a_compiled_class_is_not_named",
    "the_device_entry_is_filed_under_the_root_nub",
    "the_driver_names_a_node_that_is_not_there",
    "the_driver_stops_comparing_one_name",
    "the_node_loses_the_property_the_driver_reads",
    "a_node_property_is_read_as_a_number",
    "the_root_compatible_moves",
    "the_resource_personality_asks_for_another_resource",
    "a_personality_gains_a_bundle_id",
    "the_nub_class_moves_in_apple_s_file",
    "a_class_is_defined_twice",
    "the_fallback_entry_is_removed",
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
