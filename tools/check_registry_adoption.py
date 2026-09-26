#!/usr/bin/env python3
"""
Check the things 486's reading of the device tree's *ownership* depends on, before the device is asked.

485 measured that the registry slot holds one entry inside the `IODeviceTreeAlloc` wrapper and a
different entry at the first `fromPath` of `bsd_init`, both with the same 21 children and the same
child-set pointer, and left the question open. The answer is in Apple's sources, in four statements, and
this check is what keeps that answer from being a comment: every one of them is read out of Apple's own
file here, and a step that changed the mechanism - or that changed the instrument so the run can no
longer see which object it is reading - fails the build rather than printing a table that looks the same.

  1. **the call chain is this kernel's.** `StartIOKit` makes the adopting entry (`new
     IOPlatformExpertDevice`), `IOPlatformExpertDevice::initWithArgs` builds the tree
     (`IODeviceTreeAlloc(dtTop)`) *before* it initializes from it (`super::init(dt, gIODTPlane)`), and
     `IOService::init(from, inPlane)` hands those two arguments straight to
     `IORegistryEntry::init(old, plane)`. The order matters and is claimed: an adoption before the tree
     exists adopts nothing, and the class names the run prints are the two `new`s - `IOService` for the
     tree root (`MakeReferenceTable`), `IOPlatformExpertDevice` for the adopter.

  2. **the handover is those four statements, and the key indices are the ones they name.**
     `IORegistryEntry::init` copies `old`'s property table, removes the child-set key from `old`'s own
     table, moves every parent link from `old` to `this`, and moves every child link from `old` to
     `this`. Which key is the child set is a *value* in an enum beside the plane class, and a swap there
     would move the wrong links while every name in the code still read correctly - so the enum is read
     as a number and compared with the roles the four statements use.

  3. **the copy is shallow, which is why one OSArray is both objects' child set.** `dictionaryWithProperties`
     is `OSDictionary::withDictionary`, and `initWithDictionary` retains the source's *values* rather than
     copying them (both of its branches). This is the load-bearing step for 485's `walk_set` reading: with
     a deep copy the adopter's child set would be a second array, and the log's "the same set pointer" would
     have to be a coincidence instead of the mechanism.

  4. **the class comes from the object's own vtable, at the slot the image keeps it in.** `getMetaClass`
     is virtual, and this step's first run is why that is a claim: a virtual reached by its mangled name
     calls the *base* implementation, so run A printed `OSObject` for the tree root and for the entry
     that adopted it - a correct answer to a question nobody asked. The call is through the object's
     first word, and the slot is read out of the linked image twice: `_ZTV8OSObject`[9] is
     `OSObject::getMetaClass` and `_ZTV9IOService`[9] is `IOService::getMetaClass`. Two classes, one
     slot, and the check requires the code's `#define` to equal it - and requires the *base* name not to
     be called directly anywhere in the instrument.

  4b. **the object is read before it is called, and called only when it looks like an object of a class
     in this image.** Run B called the slot on an entry the walk was holding, the call returned 1, and
     `getClassName(1)` took an unserviceable data abort that cost the run every record after it. So the
     read publishes the object, its first word, the two words before that word, the slot and the metaclass,
     *before* and after the call - and makes the call only when those two words are zero (the vtable's
     offset-to-top and its absent RTTI pointer) and the slot is inside `[0x80000000, __bss_start)`.
     `xnu_live_cls_took` is 1 exactly when the call was made, so "the registry handed this instrument a
     pointer whose first word is not a vtable" is a reading rather than a stop. Run C is why the two words
     are read *before* the vptr: read at it, they are the class's own destructors, and the guard refused
     every object - correctly, since that is what a wrong offset looks like from the device.

  4c. **the object's table index is the vtable's index less the preamble, and both are read.** The
     object's first word is the ABI's vptr, which points past the vtable's two-word preamble; `getMetaClass`
     is at index 9 of the *vtable* and index 7 of the object's table. This check reads the preamble's
     length out of the image (its leading zero words), requires `STAGE90_VTABLE_PREAMBLE` to equal it, and
     requires the load to subtract it. That is the claim run B and run C were both missing: they compared
     the code's index against the *vtable's* index while the code indexed the *object's* table, so the
     check passed a program that called `OSObject::taggedRetain`.

  5. **the instrument names the objects and cannot confuse them.** The census asks the entry the OS's walk
     starts from for its class and its children, and asks the entry `IODeviceTreeAlloc` returned for its
     class and its children *now* - two records under two key sets, because one key set would let the
     second reading overwrite the first. The walk's own record carries the class of the entry it started
     from at every moment, so the sequence is in one log. Each of the three call sites names itself
     (`STAGE90_CLS_WALK` / `_ROOT` / `_RECORDED`), so a record can be attributed without counting.

  6. **every key this step adds has a live writer and a report writer.** This boot never returns from
     `vm_pageout`, so a report-only record is a record that is never taken (459) - and a key that is not
     written reads as zero, which is what "no class could be read" also prints.

  7. **the names the instrument calls are in the linked image, and the instrument is where they are
     called.** `OSMetaClass::getClassName` must be *defined* in the image, the census and the walk must
     transfer to `entry_class_words`, and `entry_class_words` - the one function that zeroes the two
     words, reads the vtable under the guard and calls `getClassName` - must be where the image transfers
     to it and to the guard's own record. A name that stopped resolving would print as a zero and read as
     "no class"; a second road to it would read as the same name until one of the two roads was edited.

    ./tools/check_registry_adoption.py --image out/stage90/xnu_arm_entry.elf
    ./tools/check_registry_adoption.py --image out/stage90/xnu_arm_entry.elf --selftest

What this check cannot say
--------------------------
Which class the run actually prints, and whether the object passes the guard. Every claim here is about
the program that will print it and about the sources that predict it; the predicted answers are
`IOService` for the tree root and `IOPlatformExpertDevice` for the entry that adopts it, and the run's
readings are what decide. Run B is the case: the guard's precondition held in the source and did not hold
on the object the device handed the walk, which is a fact about the device and not about this file. A run
that answers something else, or that reports `cls_took = 0`, falsifies a prediction rather than failing
this check.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.path.join(REPO_ROOT, "external/xnu-4570.1.46")
BOOT_DIR = os.path.join(REPO_ROOT, "src/entry")

ENTRY_TRACE_C = os.path.join(BOOT_DIR, "entry_trace.c")
ENTRY_STUBS_C = os.path.join(BOOT_DIR, "entry_stubs.c")
BUILD_ENTRY_SH = os.path.join(BOOT_DIR, "build_entry.sh")
IOREGISTRY_CPP = os.path.join(XNU, "iokit/Kernel/IORegistryEntry.cpp")
OSDICTIONARY_CPP = os.path.join(XNU, "libkern/c++/OSDictionary.cpp")
PLATFORM_EXPERT_CPP = os.path.join(XNU, "iokit/Kernel/IOPlatformExpert.cpp")
START_IOKIT_CPP = os.path.join(XNU, "iokit/Kernel/IOStartIOKit.cpp")
SERVICE_CPP = os.path.join(XNU, "iokit/Kernel/IOService.cpp")
DEVICE_TREE_CPP = os.path.join(XNU, "iokit/Kernel/IODeviceTreeSupport.cpp")

NM = "arm-none-eabi-nm"
OBJDUMP = "arm-none-eabi-objdump"

META_CLASS = "_ZNK8OSObject12getMetaClassEv"
CLASS_NAME = "_ZNK11OSMetaClass12getClassNameEv"
VOBJECT_VTABLE = "_ZTV8OSObject"
VSERVICE_VTABLE = "_ZTV9IOService"
SERVICE_META_CLASS = "_ZNK9IOService12getMetaClassEv"

# The vtable slot every class keeps its `getMetaClass` in, and the preamble a primary vtable has.
# It is a *reading*: `claim_slot` finds this index in the linked image for two different classes rather
# than taking the number from this file.
SLOT = 9
VTABLE_WORDS = 20

# The functions each claim reads, by the spelling that finds them as definitions.
INIT = "IORegistryEntry::init"
WITH_PROPS = "IORegistryEntry::dictionaryWithProperties"
WITH_DICT = "OSDictionary::withDictionary"
INIT_WITH_DICT = "OSDictionary::initWithDictionary"
INIT_WITH_ARGS = "IOPlatformExpertDevice::initWithArgs"
START_IOKIT = "StartIOKit"
SERVICE_INIT = "IOService::init"
DT_ALLOC = "IODeviceTreeAlloc"
MAKE_TABLE = "MakeReferenceTable"
CENSUS = "entry_probe_dt_children"
WALK = "entry_probe_walk"
CLASS_WORDS = "entry_class_words"
CLASS_META = "entry_object_meta"
CLASS_NOTE = "entry_note_class"
CLASS_SITES = ("STAGE90_CLS_WALK", "STAGE90_CLS_ROOT", "STAGE90_CLS_RECORDED")

# Every key 486 adds, and the writer each one has to appear in. The live keys are the reading - this
# boot never returns from `vm_pageout`, so the report path is never reached - and the report keys are
# what makes the same numbers readable when it is. A key in one and not the other is the defect this
# project has hit most often: one value with two definitions, neither compared.
LIVE_KEYS = (
    "xnu_live_dtc_calls", "xnu_live_dtc_obj", "xnu_live_dtc_class0", "xnu_live_dtc_class1",
    "xnu_live_dtc_kids", "xnu_live_dtc_set",
    "xnu_live_dtrec_calls", "xnu_live_dtrec_obj", "xnu_live_dtrec_class0", "xnu_live_dtrec_class1",
    "xnu_live_dtrec_kids", "xnu_live_dtrec_set",
    "xnu_live_walk_class0", "xnu_live_walk_class1",
    "xnu_live_cls_calls", "xnu_live_cls_site", "xnu_live_cls_obj", "xnu_live_cls_vptr",
    "xnu_live_cls_pre0", "xnu_live_cls_pre1", "xnu_live_cls_fn", "xnu_live_cls_meta",
    "xnu_live_cls_took",
)
REPORT_KEYS = (
    "xnu_entry_dtc_obj", "xnu_entry_dtc_class0", "xnu_entry_dtc_class1", "xnu_entry_dtc_kids",
    "xnu_entry_dtc_set", "xnu_entry_dtc_calls",
    "xnu_entry_dtrec_obj", "xnu_entry_dtrec_class0", "xnu_entry_dtrec_class1", "xnu_entry_dtrec_kids",
    "xnu_entry_dtrec_set", "xnu_entry_dtrec_calls",
    "xnu_entry_walk_t1_class0", "xnu_entry_walk_t1_class1",
    "xnu_entry_walk_t2_class0", "xnu_entry_walk_t2_class1",
    "xnu_entry_cls_calls", "xnu_entry_cls_site", "xnu_entry_cls_obj", "xnu_entry_cls_vptr",
    "xnu_entry_cls_pre0", "xnu_entry_cls_pre1", "xnu_entry_cls_fn", "xnu_entry_cls_meta",
    "xnu_entry_cls_took",
)


def say(message):
    print(message)


def read(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read()


def strip_comments(text):
    """Remove C comments, and C++ `//` comments outside of strings."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                if text[i] == "\\":
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append("\n")
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        out.append(c)
        i += 1
    return "".join(out)


def strip_shell_comments(text):
    lines = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("#"):
            continue
        lines.append(line)
    return "\n".join(lines)


def run(command):
    return subprocess.run(command, capture_output=True, text=True).stdout


def nm(path):
    symbols = {}
    for line in run([NM, path]).splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols[parts[2]] = parts[1]
        elif len(parts) == 2:
            symbols[parts[1]] = "U"
    return symbols


def nm_addresses(path):
    """`name -> address` for the symbols that have one, which is a different question from `nm`'s
    `name -> kind`: this check needs both, and reading the *kind* where an address was meant is the
    defect it would find in someone else's code."""
    addresses = {}
    for line in run([NM, path]).splitlines():
        parts = line.split()
        if len(parts) >= 3 and re.fullmatch(r"[0-9a-f]{8}", parts[0]):
            addresses[parts[2]] = int(parts[0], 16)
    return addresses


def _matching(text, start, opener, closer):
    depth = 0
    i = start
    while i < len(text):
        if text[i] == opener:
            depth += 1
        elif text[i] == closer:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def function_body(text, name, parameters=None):
    """The body of a *definition* of `name`: the first `name(` whose argument list is followed by a
    body, taking the declaration's qualifiers into account.

    A call or a forward declaration is skipped, because what follows the closing parenthesis is not a
    `{` (Apple writes `const` after `dictionaryWithProperties( void )` and `APPLE_KEXT_OVERRIDE` after
    the platform expert's overrides). `parameters` disambiguates overloads: `IORegistryEntry::init` has
    two definitions in that file and only the two-argument one performs the handover, so the caller says
    which one it means instead of this function guessing.
    """
    for match in re.finditer(re.escape(name) + r"\s*\(", text):
        open_paren = text.index("(", match.start())
        close_paren = _matching(text, open_paren, "(", ")")
        if close_paren < 0:
            continue
        if parameters is not None and parameters not in text[open_paren + 1:close_paren]:
            continue
        after = text[close_paren + 1:]
        qualifiers = re.match(r"[A-Za-z_0-9\s]*\{", after)
        if qualifiers is None:
            continue
        brace = close_paren + 1 + qualifiers.end() - 1
        end = _matching(text, brace, "{", "}")
        if end < 0:
            continue
        return text[brace:end + 1]
    return None


def disassembly_functions(text):
    functions = {}
    current = None
    for line in text.splitlines():
        match = re.match(r"^([0-9a-f]+) <(.+)>:$", line.strip())
        if match:
            current = match.group(2)
            functions[current] = []
            continue
        if current is not None:
            functions[current].append(line)
    return functions


def transfers(lines):
    """Every `bl`/`blx`/`b` in a function, as (address, target)."""
    found = []
    for line in lines:
        match = re.search(r"\b(bl|blx|b)\s+([0-9a-f]+)\s+<([^>]+)>", line)
        if match:
            found.append((match.group(2), match.group(3)))
    return found


def reaches(targets, name):
    """Whether a transfer list contains `name` - the symbol itself, or the partial GCC emitted for it
    (`name.part.0.constprop.0`, which is what `entry_class_words` became under `-O2`).

    `find_function`'s sibling: the same reason applies, in the other direction. A `static` function two
    callers share is still one function, but the `bl` in the text names the split, and comparing a
    transfer target to a bare name would report a call that is there as a call that is not.
    """
    return any(target == name or target.startswith(name + ".") for target in targets)


def find_function(functions, name):
    """The function `name`, or the partial GCC emitted for it (`name.part.0`, `name.constprop.1`).

    A `static` function with one caller is frequently split or inlined, so looking for the exact name
    would make a claim about the *linker's* naming rather than about the program: what this check wants
    is the text that came from that function, and the suffix is how GCC says which one it is.
    """
    if name in functions:
        return functions[name]
    for candidate, lines in functions.items():
        if candidate.startswith(name + "."):
            return lines
    return None


def index_of(body, needle, label, failures):
    if body is None:
        failures.append("%s: the function was not found in its own source, so nothing about it is "
                        "claimed" % label)
        return -1
    position = body.find(needle)
    if position < 0:
        failures.append("%s: the source no longer contains `%s`, which is the statement this reading "
                        "depends on" % (label, needle))
    return position


def vtable_words(image, address, count=VTABLE_WORDS):
    """`count` little-endian words of the linked image at `address`.

    Read with `objdump -s` rather than by parsing the ELF: the words are the *image's*, and the point of
    claim 4 is that the slot index and the preamble are read out of the file that will be booted rather
    than out of a header - so the reading has to be of the same bytes the device gets.
    """
    out = run([OBJDUMP, "-s", "--start-address=%d" % address,
               "--stop-address=%d" % (address + 4 * count), image])
    words = []
    for line in out.splitlines():
        if not re.match(r"^\s*[0-9a-f]{4,}\s", line):
            continue
        parts = line.split()
        if len(parts) < 5:
            continue
        for word in parts[1:5]:
            if re.fullmatch(r"[0-9a-f]{8}", word):
                words.append(int.from_bytes(bytes.fromhex(word), "little"))
    return words


def gather(image):
    ioregistry_text = read(IOREGISTRY_CPP)
    osdict_text = read(OSDICTIONARY_CPP)
    pexpert_text = read(PLATFORM_EXPERT_CPP)
    startiokit_text = read(START_IOKIT_CPP)
    service_text = read(SERVICE_CPP)
    devtree_text = read(DEVICE_TREE_CPP)
    facts = {
        "ioregistry": strip_comments(ioregistry_text),
        "osdict": strip_comments(osdict_text),
        "pexpert": strip_comments(pexpert_text),
        "startiokit": strip_comments(startiokit_text),
        "service": strip_comments(service_text),
        "devtree": strip_comments(devtree_text),
        "trace_text": read(ENTRY_TRACE_C),
        "stubs_text": read(ENTRY_STUBS_C),
        "build_text": read(BUILD_ENTRY_SH),
        "image": image,
        "symbols": nm(image) if image else {},
        "addresses": nm_addresses(image) if image else {},
        "functions": disassembly_functions(run([OBJDUMP, "-d", image])) if image else {},
        "vtables": {},
    }
    addresses = facts["addresses"]
    for name in (VOBJECT_VTABLE, VSERVICE_VTABLE):
        if name in addresses:
            facts["vtables"][name] = vtable_words(image, addresses[name])
    facts["trace"] = strip_comments(facts["trace_text"])
    facts["stubs"] = strip_comments(facts["stubs_text"])
    for key, source in (("init", ("ioregistry", INIT, "IORegistryEntry * old")),
                        ("with_props", ("ioregistry", WITH_PROPS, None)),
                        ("with_dict", ("osdict", WITH_DICT, None)),
                        ("init_with_dict", ("osdict", INIT_WITH_DICT, None)),
                        ("init_with_args", ("pexpert", INIT_WITH_ARGS, None)),
                        ("start_iokit", ("startiokit", START_IOKIT, None)),
                        ("service_init", ("service", SERVICE_INIT, "from")),
                        ("dt_alloc", ("devtree", DT_ALLOC, None)),
                        ("make_table", ("devtree", MAKE_TABLE, None)),
                        ("census", ("trace", CENSUS, None)),
                        ("walk", ("trace", WALK, None)),
                        ("dtclass", ("stubs", "entry_note_dtclass", None)),
                        ("dtrec", ("stubs", "entry_note_dtrec", None)),
                        ("dtwalk", ("stubs", "entry_note_dtwalk", None)),
                        ("class_words", ("trace", "entry_class_words", None)),
                        ("class_meta", ("trace", CLASS_META, None))):
        source_key, name, parameters = source
        facts[key] = function_body(facts[source_key], name, parameters)
    return facts


# ------------------------------------------------------------------------------------------------
# The claims
# ------------------------------------------------------------------------------------------------

def claim_chain(facts, failures, notes):
    """1. The adoption is this kernel's call chain, with the tree built before it is adopted, and the
    two classes the run is expected to print are the two `new`s in that chain."""
    start = facts["start_iokit"]
    if start is None:
        failures.append("IOStartIOKit.cpp no longer defines StartIOKit, so the entry that adopts the "
                        "tree is not made by any code this check can read")
    else:
        made = start.find("new IOPlatformExpertDevice")
        used = start.find("initWithArgs")
        if made < 0:
            failures.append("StartIOKit no longer makes its root nub with `new IOPlatformExpertDevice`; "
                            "the class name this step's run is expected to print for the entry the OS "
                            "walks would then be a different class, and nothing here would say so")
        if used < 0:
            failures.append("StartIOKit no longer calls `initWithArgs`, so the device tree is never "
                            "adopted and the object 485's census reads is not explained by this chain")
        if made >= 0 and used >= 0 and made > used:
            failures.append("StartIOKit calls `initWithArgs` before it allocates the entry, which "
                            "cannot be the adoption 485 measured")

    args = facts["init_with_args"]
    if args is None:
        failures.append("IOPlatformExpertDevice::initWithArgs is gone; the function that adopts the "
                        "tree is the whole subject of this claim")
    else:
        alloc = index_of(args, "IODeviceTreeAlloc", "initWithArgs", failures)
        adopt = index_of(args, "super::init( dt, gIODTPlane )", "initWithArgs", failures)
        if alloc >= 0 and not re.search(r"IODeviceTreeAlloc\s*\(\s*dtTop\s*\)", args):
            failures.append("initWithArgs no longer builds the tree from the pointer it was handed "
                            "(`IODeviceTreeAlloc(dtTop)`); whatever it builds from, the object the "
                            "census reads is not the tree this boot was given")
        if alloc >= 0 and adopt >= 0 and alloc > adopt:
            failures.append("initWithArgs initializes from the tree before it builds it "
                            "(`super::init(dt, gIODTPlane)` precedes `IODeviceTreeAlloc(dtTop)`), so "
                            "the object it adopts is not a tree and 485's pair has another cause")
        if adopt >= 0 and "IOPlatformExpertDevice::initWithArgs" not in facts["pexpert"]:
            failures.append("initWithArgs' body was found but not under its own name")
        elif adopt < 0:
            failures.append("initWithArgs no longer initializes *from* the tree root "
                            "(`super::init(dt, gIODTPlane)`), which is the call that makes the second "
                            "registry object")

    service = facts["service_init"]
    if service is None:
        failures.append("IOService::init(from, inPlane) is gone; the two-argument constructor that "
                        "reaches IORegistryEntry::init is the link between the chain above and the "
                        "handover below")
    elif not re.search(r"super::init\s*\(\s*from\s*,\s*inPlane\s*\)", service):
        failures.append("IOService::init(from, inPlane) no longer passes both arguments to "
                        "`super::init`, so the plane the tree was adopted in would not reach "
                        "IORegistryEntry::init and the adoption would not be in gIODTPlane")

    table = facts["make_table"]
    if table is None:
        failures.append("MakeReferenceTable is gone; it is the function whose `new` decides the class "
                        "name the run prints for the entry the walk starts from")
    elif not re.search(r"new\s+IOService\s*;", table):
        failures.append("MakeReferenceTable no longer makes the tree's nodes with `new IOService` "
                        "(`new IOServicePM;` is a different class whose name the run would print), so "
                        "the class name this step predicts for the tree root is no longer the source's")

    alloc_body = facts["dt_alloc"]
    if alloc_body is None:
        failures.append("IODeviceTreeAlloc is gone, so the attachment that the adoption later rewrites "
                        "is not in the source this check reads")
    elif "attachToParent( IORegistryEntry::getRegistryRoot(), gIODTPlane)" not in alloc_body:
        failures.append("IODeviceTreeAlloc no longer attaches the tree to the registry root in the "
                        "IODT plane, so the slot the adoption moves is not the slot 485 measured")

    notes.append("the chain: StartIOKit's `new IOPlatformExpertDevice` -> initWithArgs' "
                 "`IODeviceTreeAlloc(dtTop)` then `super::init(dt, gIODTPlane)` -> IOService::init -> "
                 "IORegistryEntry::init; MakeReferenceTable's `new IOService` is the class the walk "
                 "should print first")


def claim_handover(facts, failures, notes):
    """2. The handover is the four statements that move the tree, in the order that makes them work,
    with the child-set key and the parent-set key in the roles the code names."""
    ioregistry = facts["ioregistry"]
    parent_index = re.search(r"kParentSetIndex\s*=\s*(\d+)", ioregistry)
    child_index = re.search(r"kChildSetIndex\s*=\s*(\d+)", ioregistry)
    if parent_index is None or child_index is None:
        failures.append("the plane's key indices are no longer written as numbers "
                        "(`kParentSetIndex = n`, `kChildSetIndex = n`), so the roles the four "
                        "statements use cannot be compared with the values the plane is built with")
    else:
        if parent_index.group(1) != "0" or child_index.group(1) != "1":
            failures.append("the plane's key indices moved (kParentSetIndex=%s, kChildSetIndex=%s); "
                            "the handover below names them by symbol, so a changed value would move "
                            "the wrong links while every statement still read correctly - which is "
                            "this project's oldest defect class, one value with two definitions"
                            % (parent_index.group(1), child_index.group(1)))

    body = facts["init"]
    if body is None:
        failures.append("IORegistryEntry::init(old, plane) is gone; it is the function that performs "
                        "the handover and the only place this reading can come from")
        return

    copy = index_of(body, "fPropertyTable = old->dictionaryWithProperties()", "IORegistryEntry::init",
                    failures)
    remove_parent = body.find("old->registryTable()->removeObject( plane->keys[ kParentSetIndex ] )")
    remove_child = body.find("old->registryTable()->removeObject( plane->keys[ kChildSetIndex ] )")
    parent_link = body.find("next->makeLink( this, kChildSetIndex, plane )")
    parent_break = body.find("next->breakLink( old, kChildSetIndex, plane )")
    child_link = body.find("next->makeLink( this, kParentSetIndex, plane )")
    child_break = body.find("next->breakLink( old, kParentSetIndex, plane )")

    if remove_parent < 0 or remove_child < 0:
        failures.append("the two `removeObject( plane->keys[ ... ] )` calls are no longer both in "
                        "IORegistryEntry::init; without the child-set removal the tree root would "
                        "still answer its 21 children after the adoption, which is the observation "
                        "485's first run made and this step explains")
    if parent_link < 0 or parent_break < 0:
        failures.append("the parents' child links no longer move from `old` to `this` "
                        "(`makeLink(this, kChildSetIndex)` beside `breakLink(old, kChildSetIndex)`): "
                        "the registry root would then hold two children in the IODT plane instead of "
                        "one, and 485's `walk_count = 1` would be unexplained")
    if child_link < 0 or child_break < 0:
        failures.append("the childrens' parent links no longer move from `old` to `this` "
                        "(`makeLink(this, kParentSetIndex)` beside `breakLink(old, kParentSetIndex)`): "
                        "the tree's nodes would keep a parent that is no longer in the plane, and "
                        "`fromPath` would still find them through it")
    if copy < 0:
        failures.append("IORegistryEntry::init no longer copies the old entry's property table, so the "
                        "adopting entry would have no child set at all and the census would read zero")
    if copy >= 0 and remove_child >= 0 and copy > remove_child:
        failures.append("the property table is copied *after* the child-set key is removed from the "
                        "old table, so the adopting entry would inherit a table with no child set - "
                        "the copy has to come first, which is the order in this source")
    order = [p for p in (copy, remove_parent, remove_child, parent_link, child_link) if p >= 0]
    if order != sorted(order) and all(p >= 0 for p in (copy, remove_parent, remove_child, parent_link,
                                                      child_link)):
        failures.append("the handover's statements are out of order: the copy must precede the "
                        "removals, and the parent links must be moved before the child links for the "
                        "iteration to see a set that still holds every child")
    if body.count("(next = (IORegistryEntry *) all->getObject(index))") != 2:
        failures.append("the handover's two loops no longer both stop at the first NULL entry "
                        "(`(next = (IORegistryEntry *) all->getObject(index))`, one per loop); a "
                        "loop that read index 0 would walk a half-full set, and a loop whose guard "
                        "moved would stop reading after the first child")
    if re.search(r"all->getObject\(\s*0\s*\)", body):
        failures.append("one of the handover's loops reads index 0 instead of the loop variable, so "
                        "only the first parent link or the first child link would be moved")

    notes.append("the handover: the shallow copy first, then the two key removals, then the parents' "
                 "child sets and the childrens' parent sets, moved one link pair at a time")


def claim_shallow(facts, failures, notes):
    """3. The copy is shallow, so the adopting entry's child set is the *same* OSArray the tree root
    had - which is what 485's `walk_set` reading shows and this claim explains."""
    props = facts["with_props"]
    if props is None:
        failures.append("dictionaryWithProperties is gone; nothing then says what the adopting entry's "
                        "property table is a copy of")
    elif not re.search(r"OSDictionary::withDictionary\s*\(\s*getPropertyTable\(\)\s*,", props):
        failures.append("dictionaryWithProperties no longer builds its copy with "
                        "`OSDictionary::withDictionary(getPropertyTable(), ...)`, so the child set the "
                        "adopting entry inherits is not the one this reading reasons about")

    with_dict = facts["with_dict"]
    if with_dict is None:
        failures.append("OSDictionary::withDictionary is gone, so the shallow-copy claim has no "
                        "subject")
    else:
        delegated = re.search(r"initWithDictionary\s*\(\s*dict\s*,\s*capacity\s*\)", with_dict)
        if delegated is None:
            failures.append("OSDictionary::withDictionary no longer delegates to "
                            "`initWithDictionary(dict, capacity)`, so the statement that decides "
                            "whether the values are shared is somewhere else")
        copied = re.search(r"copyCollection|->copy\(\)|withArray\s*\(\s*dict", with_dict)
        if copied:
            failures.append("OSDictionary::withDictionary copies its values (`%s`); with a deep copy "
                            "the adopting entry's child set would be a second OSArray and 485's "
                            "`walk_set` reading - the same pointer at every moment - could only be a "
                            "coincidence" % copied.group(0))

    init_dict = facts["init_with_dict"]
    if init_dict is None:
        failures.append("OSDictionary::initWithDictionary is gone; it is where the source's values are "
                        "either retained or copied, which is the whole of claim 3")
        return
    retained = re.search(r"dictionary\[i\]\.value->taggedRetain\s*\(", init_dict)
    sorted_branch = re.search(r"setObject\s*\(\s*dict->dictionary\[i\]\.key\s*,\s*"
                              r"dict->dictionary\[i\]\.value\s*\)", init_dict)
    deep = re.search(r"copyCollection|->copy\(\)|OSData::withBytes|withObject\s*\(\s*dict->dictionary"
                     r"\[i\]\.value\s*\)", init_dict)
    if deep:
        failures.append("OSDictionary::initWithDictionary deep-copies a value (`%s`); the child set "
                        "would then be a different object for the adopting entry" % deep.group(0))
    if retained is None and sorted_branch is None:
        failures.append("OSDictionary::initWithDictionary neither retains the source's values nor "
                        "passes them to `setObject` unchanged - so whatever it does now, this check "
                        "can no longer say the copy is shallow")
    if retained is not None and not re.search(r"bcopy\s*\(\s*dict->dictionary\s*,\s*dictionary\s*,"
                                              r"\s*count\s*\*\s*sizeof\(dictEntry\)\s*\)", init_dict):
        failures.append("initWithDictionary retains the values but no longer copies the entries with "
                        "`bcopy(dict->dictionary, dictionary, count * sizeof(dictEntry))`; the "
                        "retained values are the same objects either way, but the claim says by which "
                        "road and a different road is a different claim")
    notes.append("the copy is shallow: `bcopy` of the entries plus `taggedRetain` on each value, so "
                 "the same OSArray is both entries' child set")


def claim_records(facts, failures, notes):
    """4. The instrument names both objects, in two records under two key sets, and the walk's own
    record carries the class at every moment."""
    stubs = facts["stubs"]
    trace = facts["trace"]

    dtclass = facts["dtclass"]
    dtrec = facts["dtrec"]
    if dtclass is None or dtrec is None:
        failures.append("`entry_note_dtclass`/`entry_note_dtrec` are not both defined in "
                        "`entry_stubs.c`; two subjects need two records and this step's whole reading "
                        "is the pair")
    else:
        for name, body in (("entry_note_dtclass", dtclass), ("entry_note_dtrec", dtrec)):
            params = re.search(re.escape(name) + r"\s*\(([^)]*)\)", stubs)
            count = len([p for p in params.group(1).split(",") if p.strip()]) if params else 0
            if count != 5:
                failures.append("%s takes %d parameters, not five (object, class0, class1, kids, set) "
                                "- a record that cannot carry all five cannot be read as the pair this "
                                "step publishes" % (name, count))
            for key in ("obj", "class0", "class1", "kids", "set"):
                if "xnu_live_%s_%s" % ("dtc" if name.endswith("dtclass") else "dtrec", key) not in body:
                    failures.append("%s no longer writes its `%s` key in the live channel; this boot "
                                    "never returns from `vm_pageout`, so a report-only record is a "
                                    "record that is never taken (459)" % (name, key))

    dtc_keys = set(re.findall(r"xnu_live_(dtc_\w+)", dtclass or ""))
    dtrec_keys = set(re.findall(r"xnu_live_(dtrec_\w+)", dtrec or ""))
    shared = dtc_keys & dtrec_keys
    if shared:
        failures.append("the two records share live key(s) %s; one would overwrite the other and the "
                        "pair 485 could not read would be unreadable again - the two subjects are the "
                        "walked entry and the returned entry, and they need two key sets"
                        % ", ".join(sorted(shared)))
    if len(dtc_keys) != 6 or len(dtrec_keys) != 6:
        failures.append("the two records write %d and %d live keys; each names six things (the call "
                        "count, the object, two class words, the children, the child set) and a missing "
                        "one reads as zero rather than as 'not taken'" % (len(dtc_keys), len(dtrec_keys)))

    census = facts["census"]
    if census is None:
        failures.append("the census function is gone from `entry_trace.c`")
    else:
        if "entry_note_dtclass(" not in census:
            failures.append("the census no longer publishes the walked entry's class")
        if "entry_note_dtrec(" not in census:
            failures.append("the census no longer publishes the returned entry's class and its children "
                            "*now*, which is the half that says the tree root was emptied")
        class_arg = re.search(r"entry_note_dtclass\s*\(\s*\(uint32_t\)\(uintptr_t\)(\w+)", census)
        if class_arg is None or class_arg.group(1) != "root":
            failures.append("the class record is no longer taken from `root` (the entry the OS's own "
                            "walk starts from)%s"
                            % ("" if class_arg is None else ", it is taken from `%s`" % class_arg.group(1)))
        rec_arg = re.search(r"entry_note_dtrec\s*\(\s*\(uint32_t\)\(uintptr_t\)(\w+)", census)
        if rec_arg is None or rec_arg.group(1) != "recorded":
            failures.append("the second class record is no longer taken from `recorded` (461's "
                            "`g_dtplane_root`)%s"
                            % ("" if rec_arg is None else ", it is taken from `%s`" % rec_arg.group(1)))
        if re.search(r"entry_note_dtrec\s*\(\s*\(uint32_t\)\(uintptr_t\)recorded", census) and \
                "if (recorded != 0)" not in census:
            failures.append("the record about the returned entry is taken without checking that there "
                            "is one; a NULL `g_dtplane_root` would then be read as an entry")

    walk = facts["walk"]
    if walk is None:
        failures.append("`entry_probe_walk` is gone from `entry_trace.c`")
    else:
        if "entry_class_words(STAGE90_CLS_WALK, first" not in walk:
            failures.append("the walk no longer takes the class of the entry it started from - or no "
                            "longer names the site it is - so the sequence 485 measured, one object "
                            "inside the `IODeviceTreeAlloc` wrapper and another at the first `fromPath` "
                            "of `bsd_init`, would be two pointers the reader cannot tell apart")
        if not re.search(r"entry_note_dtwalk\([^;]*class0,\s*class1", walk):
            failures.append("the walk no longer passes the class words to its own record")

    census = facts["census"]
    if census is not None:
        for site in ("STAGE90_CLS_ROOT", "STAGE90_CLS_RECORDED"):
            if "entry_class_words(%s," % site not in census:
                failures.append("the census's call for `%s` is gone or no longer names its site; the "
                                "two objects it reads are the pair this step exists to separate" % site)

    dtwalk = facts["dtwalk"]
    if dtwalk is None:
        failures.append("`entry_note_dtwalk` is gone from `entry_stubs.c`")
    else:
        for slot in ("t1", "t2"):
            for word in ("class0", "class1"):
                if "g_walk_%s_%s = " % (slot, word) not in dtwalk:
                    failures.append("`entry_note_dtwalk` no longer stores `%s` for the %s slot; the "
                                    "record's report half would read a stale or zero class"
                                    % (word, slot))
        if "xnu_live_walk_class0" not in dtwalk or "xnu_live_walk_class1" not in dtwalk:
            failures.append("`entry_note_dtwalk` no longer publishes the class in the live channel, "
                            "which is the only channel this boot's report path reaches")

    words = facts["class_words"]
    if words is None:
        failures.append("`entry_class_words` is gone from `entry_trace.c`; it is the one place a class "
                        "name is turned into two words")
    else:
        if not re.search(r"\*w0 = 0u;\s*\n\s*\*w1 = 0u;", words):
            failures.append("`entry_class_words` no longer zeroes both words before reading; a class "
                            "that could not be read would then publish the *previous* reading's name")
        if CLASS_META not in words or "entry_xnu_metaclass_name" not in words:
            failures.append("`entry_class_words` no longer goes through `%s` - the one place the object "
                            "is read - and this file's `entry_xnu_metaclass_name`" % CLASS_META)
        if "obj != 0" not in words or "meta != 0" not in words or "name != 0" not in words:
            failures.append("`entry_class_words` no longer guards each of the three dereferences "
                            "(object, meta class, name); the census reads entries that are live and "
                            "dereferences the OS itself will dereference on the next line")

    if CLASS_NAME not in trace:
        failures.append("`entry_trace.c` no longer reaches `OSMetaClass::getClassName` by its mangled "
                        "name `%s`; with the vtable call it is the only *direct* call on this road and "
                        "a typo in it is a link error or a wrong function" % CLASS_NAME)
    elif trace.count(CLASS_NAME) != 1:
        failures.append("`OSMetaClass::getClassName` is declared %d times in `entry_trace.c`; two "
                        "declarations of one call are this project's oldest defect class - and the one "
                        "this step's first version wrote, because the file already had `getClassName` "
                        "declared for the metaclass walk (240). A selftest mutation that edits one of "
                        "the two would leave the other standing and report a blind spot that is not "
                        "there" % trace.count(CLASS_NAME))
    if META_CLASS in trace:
        failures.append("`entry_trace.c` calls `%s` *by name* again: run A did exactly that and printed "
                        "`OSObject` for both entries, because a virtual called by its mangled name is "
                        "the *base* implementation (`{ return &gMetaClass; }`, `OSObject.cpp:57-58`) - "
                        "the class has to come from the object's own vtable" % META_CLASS)

    notes.append("the instrument: two records under two key sets, the walk's class at every moment, "
                 "and `entry_class_words` returning zero rather than a stale name when it cannot read")


def claim_slot(facts, failures, notes):
    """4b. The class comes from the object's own vtable, at the slot the *image* keeps it in for two
    different classes - because a virtual called by name is the base implementation (run A).

    **And the two tables the index can be counted in are both spelled out, because run C was decided by
    exactly that mistake.** An object's first word is the vptr, and in this ABI the vptr points *past* the
    vtable's two-word preamble to its first function slot; `getMetaClass` sits at index 9 of the vtable,
    i.e. index 9 - 2 of the object's table. 486's first two runs read index 9 of the object's table, which
    is `OSObject::taggedRetain`, found each class's own `getMetaClass` at index 9 of the *label*, and both
    reads were of the same image - so this check passed a program that called the wrong slot, which is the
    "one value, two definitions" defect in its purest form. The claim below reads the preamble length from
    the image (the label's two zero words), requires the code's `#define` to name the vtable index, and
    requires the load to subtract the preamble rather than to index the vtable directly.
    """
    trace = facts["trace"]
    symbols = facts["symbols"]
    vtables = facts["vtables"]

    define = re.search(r"#define\s+STAGE90_VTABLE_META_CLASS\s+(\d+)u", trace)
    if define is None:
        failures.append("`STAGE90_VTABLE_META_CLASS` is not a `#define` in `entry_trace.c` any more; "
                        "the slot would then be a literal in the load and this claim would have "
                        "nothing to compare with the image")
        slot = None
    else:
        slot = int(define.group(1))

    preamble_define = re.search(r"#define\s+STAGE90_VTABLE_PREAMBLE\s+(\d+)u", trace)
    if preamble_define is None:
        failures.append("`STAGE90_VTABLE_PREAMBLE` is not a `#define` in `entry_trace.c`; the distance "
                        "between the object's vptr and the vtable's first word would then be a literal in "
                        "the arithmetic, and run C is what a hard-coded one costs")
        preamble = None
    else:
        preamble = int(preamble_define.group(1))

    if "STAGE90_VTABLE_META_CLASS - STAGE90_VTABLE_PREAMBLE" not in trace:
        failures.append("the slot is no longer loaded as `STAGE90_VTABLE_META_CLASS - "
                        "STAGE90_VTABLE_PREAMBLE`. The object's first word is the vptr, which points "
                        "past the vtable's two-word preamble: indexing it with the vtable's own index "
                        "calls the wrong virtual - run B called `OSObject::taggedRetain` and passed its "
                        "leftover `r0` to `getClassName`")

    if "[-(int)STAGE90_VTABLE_PREAMBLE]" not in trace or "[1 - (int)STAGE90_VTABLE_PREAMBLE]" not in trace:
        failures.append("the two preamble words are no longer read *before* the vptr "
                        "(`vptr[-PREAMBLE]`, `vptr[1 - PREAMBLE]`); reading them at the vptr is what run "
                        "C did, and the two words it got were the vtable's first two function slots - the "
                        "class's destructors - which the guard then read as a non-zero preamble")

    for name in (VOBJECT_VTABLE, VSERVICE_VTABLE):
        if name not in facts["addresses"]:
            failures.append("the image does not contain `%s`, so the slot this claim reads cannot be "
                            "read at all" % name)
        elif name not in vtables or len(vtables.get(name, [])) < VTABLE_WORDS:
            failures.append("`%s`'s words could not be read out of the image" % name)

    if VOBJECT_VTABLE in vtables and VSERVICE_VTABLE in vtables and slot is not None:
        for vtable, own in ((VOBJECT_VTABLE, META_CLASS), (VSERVICE_VTABLE, SERVICE_META_CLASS)):
            words = vtables[vtable]
            if words[0] != 0 or words[1] != 0:
                failures.append("`%s`'s preamble is %d/%d and not 0/0; a *primary* vtable begins with a "
                                "zero offset-to-top and this kernel carries no RTTI pointer, and a "
                                "different preamble would mean the vtable and the object do not start "
                                "at the same place" % (vtable, words[0], words[1]))
            if own not in facts["addresses"]:
                failures.append("the image does not contain `%s`, the class's own `getMetaClass`"
                                % own)
                continue
            target = facts["addresses"][own]
            found = [index for index, word in enumerate(words) if word == target]
            if found != [slot]:
                failures.append("`%s` holds `%s` at index %s, and the code uses slot %d; two classes "
                                "keeping their own `getMetaClass` at the same index is what makes the "
                                "index the ABI's rather than this file's, and a disagreement here is a "
                                "call into whatever else lives in that slot"
                                % (vtable, own, found, slot))
        notes.append("the slot is the image's: `_ZTV8OSObject`[%d] = OSObject::getMetaClass and "
                     "`_ZTV9IOService`[%d] = IOService::getMetaClass, preamble 0/0 in both" % (slot, slot))

    # The preamble's *length* is a reading of the image as well: the leading words that are zero before
    # the first word that is a code address. That is what an object's vptr points past, so it is the
    # number the load has to subtract - and reading it here is the whole repair, because run C's mistake
    # was to have this number (2) in one place and the code's index in another.
    if VOBJECT_VTABLE in vtables and preamble is not None:
        words = vtables[VOBJECT_VTABLE]
        length = 0
        while length < len(words) and words[length] == 0:
            length += 1
        if length != preamble:
            failures.append("the image's primary vtable has %d zero word(s) before its first code "
                            "address and the code subtracts `STAGE90_VTABLE_PREAMBLE` = %d; the "
                            "object's vptr points past that run, so the two have to agree or the load "
                            "indexes a slot that is not `getMetaClass` - which is run B's fault and the "
                            "check that missed it" % (length, preamble))
        else:
            notes.append("the preamble is %d words, read from the image's own leading zero words, and "
                         "the object's table index is %d - 2 = %d" % (length, slot, slot - length))


def claim_guard(facts, failures, notes):
    """4c. The object is read *before* it is called, and it is called only when it looks like an object
    of a class in this image - which is what run B's fault bought."""
    meta = facts["class_meta"]
    trace = facts["trace"]
    symbols = facts["symbols"]
    functions = facts["functions"]

    if meta is None:
        failures.append("`entry_object_meta` is gone from `entry_trace.c`; the class read would then be "
                        "an unguarded call through whatever the registry handed it, which is the fault "
                        "that ended run B")
        return

    if not re.search(r"\*\s*\(\s*const\s+void\s*\*\s*const\s*\*\s*\)\s*obj", meta):
        failures.append("the vtable pointer is no longer read from the object's *first word* "
                        "(`*(const void *const *)obj`); an object of an `OSObject`-derived class begins "
                        "with its vptr, and any other offset would read a member as a table")

    if "[STAGE90_VTABLE_META_CLASS - STAGE90_VTABLE_PREAMBLE]" not in meta:
        failures.append("the slot is no longer loaded as the vtable's index less the preamble; the "
                        "object's first word points past the vtable's preamble, so indexing it with the "
                        "vtable's own index calls `OSObject::taggedRetain` - run B's fault")

    if "[-(int)STAGE90_VTABLE_PREAMBLE]" not in meta:
        failures.append("the preamble words are no longer read *before* the vptr; the two words at the "
                        "vptr are the vtable's first two function slots - the class's destructors - so "
                        "the guard would refuse every real object, which is what run C measured")

    if "pre0 == 0u && pre1 == 0u" not in meta:
        failures.append("the precondition no longer requires the two preamble words to be zero. Every "
                        "one of the 218 vtables in the linked image begins 0/0 - a primary vtable's zero "
                        "offset-to-top and no RTTI - so this is the test that separates a vtable from "
                        "arbitrary data, and without it the guard is only a range check")

    if "STAGE90_IMAGE_BASE" not in meta or "__bss_start" not in meta:
        failures.append("the precondition no longer bounds the slot by this image's own extent "
                        "(`STAGE90_IMAGE_BASE` .. `&__bss_start`); a slot outside the image is a call "
                        "into whatever else the address space holds, which is where run B's fault came "
                        "from")

    if "took = 1u;" not in meta:
        failures.append("nothing sets `took` inside the guard, so the record could not say whether the "
                        "call was made - and `took = 0` beside a non-zero metaclass is a combination a "
                        "reader could not explain")

    call = meta.find("fn(obj)")
    first = meta.find("entry_note_class(")
    second = meta.find("entry_note_class(", first + 1) if first >= 0 else -1
    if call < 0:
        failures.append("`entry_object_meta` no longer calls the slot function at all, so no class can "
                        "be read and every name this step publishes would be zero")
    elif first < 0 or second < 0:
        failures.append("`entry_object_meta` no longer publishes its numbers *twice*, before and after "
                        "the call; a record written only afterwards is a record a fault inside the call "
                        "never reaches, which is what run B left in the log")
    elif not first < call < second:
        failures.append("the two `entry_note_class` calls no longer straddle the call (`fn(obj)`); the "
                        "point of publishing first is that the object, its first word and slot 9 are in "
                        "the log when the call does not return")

    if "extern uint32_t __bss_start;" not in trace:
        failures.append("`__bss_start` is no longer declared in `entry_trace.c` in the form `entry.ld` "
                        "defines it; the upper bound of the range check would be a different number or "
                        "a link error")

    if symbols:
        words = find_function(functions, CLASS_WORDS)
        if words is None:
            failures.append("`%s` is not in the linked image, so the guard cannot be claimed on what "
                            "the device ran" % CLASS_WORDS)
        elif not reaches([target for _addr, target in transfers(words)], CLASS_NOTE):
            failures.append("the text of `%s` contains no transfer to `%s`; the numbers of the guard "
                            "would then not be in the log, whatever the source says"
                            % (CLASS_WORDS, CLASS_NOTE))

    notes.append("the guard: the vtable is read from the object's first word, the two words *before* it "
                 "must be 0/0, the vtable's own slot 9 (the object's table index 7) must be inside this "
                 "image, and the numbers are published before and after the call")


def claim_report(facts, failures, notes):
    """5. Every key this step adds is written by the writer the report path calls, in the same group
    the rest of the census is written in."""
    stubs = facts["stubs"]
    for key in LIVE_KEYS:
        if 'entry_live_write("%s"' % key not in stubs:
            failures.append("the live key `%s` is not written by any `entry_live_write` in "
                            "`entry_stubs.c`" % key)
    for key in REPORT_KEYS:
        if not re.search(r'entry_write_kv\(\s*"%s"\s*,' % re.escape(key), stubs):
            failures.append("the report key `%s` is not written in `entry_stubs.c`; a report that "
                            "omits it reads as a zero, which is what 'the class could not be read' "
                            "also prints" % key)
    group = facts["stubs"].find("void entry_write_485_kv")
    if group >= 0:
        body = facts["stubs"][group:]
        end = body.find("\n}")
        body = body[:end]
        for key in REPORT_KEYS:
            if "walk_" in key:
                continue
            if '"%s"' % key not in body:
                failures.append("the report key `%s` is written outside the group the rest of this "
                                "census is written in; the epilogue's constant pool is at the "
                                "PC-relative edge (455) and a key in the wrong function is a key that "
                                "may not link" % key)
    note_count = len([k for k in REPORT_KEYS if "walk_" in k])
    notes.append("every key this step adds has a live writer and a report writer (%d live, %d report, "
                 "%d of them the walk's)" % (len(LIVE_KEYS), len(REPORT_KEYS), note_count))


def claim_image(facts, failures, notes):
    """6. The road the class name travels in the linked image: the census and the walk transfer to
    `entry_class_words`, that function is where the image transfers to `OSMetaClass::getClassName`, and
    neither caller reaches the class name directly.

    The indirectness is the point rather than a style. `entry_class_words` is where both words are
    zeroed before the read and where the class comes from the object's own vtable, and it is the only
    transfer this step adds to `getClassName`; a direct call in either caller would be a second reading
    that agrees with the first only until one of them is edited. (The image has twenty other callers of
    `getClassName` - Apple's own - so the claim is about *these two*, not about a count over the file.)
    """
    symbols = facts["symbols"]
    functions = facts["functions"]
    if not symbols:
        failures.append("no image was read, so nothing about the linked instrument is claimed")
        return
    kind = symbols.get(CLASS_NAME)
    if kind is None:
        failures.append("`OSMetaClass::getClassName` (`%s`) is not a symbol in the linked image at all"
                        % CLASS_NAME)
    elif kind == "U":
        failures.append("`OSMetaClass::getClassName` (`%s`) is undefined in the linked image, so the "
                        "census's call to it would not link - or would link to nothing, which is worse"
                        % CLASS_NAME)

    census = find_function(functions, CENSUS)
    if census is None:
        failures.append("`%s` is not a function in the linked image; the claim that the census takes "
                        "the class is then about source that was not linked" % CENSUS)
    else:
        targets = [target for _addr, target in transfers(census)]
        for name in ("entry_note_dtclass", "entry_note_dtrec"):
            if name not in targets:
                failures.append("the census's text contains no call to `%s`; the record this step "
                                "publishes would never be written" % name)
        if not reaches(targets, CLASS_WORDS):
            failures.append("the census's text contains no transfer to `%s`; whatever the vtable load "
                            "returns, the census never turns it into a name" % CLASS_WORDS)
        if CLASS_NAME in targets:
            failures.append("the census's text transfers to `getClassName` *directly* as well as "
                            "through `%s`: two roads to one name, and the direct one skips the zeroing "
                            "that keeps a failed read from publishing the previous entry's class"
                            % CLASS_WORDS)

    walk = find_function(functions, WALK)
    if walk is None:
        failures.append("no function in the linked image came from `%s`, so the walk's class reading "
                        "is not claimed" % WALK)
    else:
        targets = [target for _addr, target in transfers(walk)]
        if "entry_note_dtwalk" not in targets:
            failures.append("the walk's text contains no call to its own record")
        if not reaches(targets, CLASS_WORDS):
            failures.append("the walk's text contains no transfer to `%s`; the class it publishes "
                            "would be the zero of an untouched local" % CLASS_WORDS)
        if CLASS_NAME in targets:
            failures.append("the walk's text transfers to `getClassName` *directly* as well as through "
                            "`%s`: the name in the walk's record would then not be the one "
                            "`entry_class_words` produced" % CLASS_WORDS)

    words = find_function(functions, CLASS_WORDS)
    if words is None:
        failures.append("`%s` is not a function in the linked image, so the class name is read "
                        "nowhere in the linked instrument" % CLASS_WORDS)
    else:
        wtargets = [target for _addr, target in transfers(words)]
        if CLASS_NAME not in wtargets:
            failures.append("the text of `%s` contains no transfer to `getClassName`; no function in "
                            "the linked image that this step owns would then produce a class name"
                            % CLASS_WORDS)
        if not reaches(wtargets, "entry_str8"):
            failures.append("the text of `%s` contains no transfer to `entry_str8`, the read that stops "
                            "at the terminator; the name would be written out of the string's storage "
                            "without one" % CLASS_WORDS)

    notes.append("the image: `getClassName` is defined, the census and the walk reach it only through "
                 "`%s`, and `%s` is where the image transfers to it and to the terminating read"
                 % (CLASS_WORDS, CLASS_WORDS))


CLAIMS = (claim_chain, claim_handover, claim_shallow, claim_records, claim_slot, claim_guard,
          claim_report, claim_image)

# The facts that come from the linked image and from nowhere else, and which claims read them.
#
# `--image` has no default because these claims have nothing to read without one, and the message that
# says so used to carry the *count* as prose: "three of the seven claims", written when this file had
# seven claims and one image reader. It was wrong twice - the file has eight claims and two of them read
# the image - and nothing compared either number with anything, which is the "one value, two definitions"
# defect this repository counts twenty-four cases of. The tuple below is therefore the *measured* set and
# `compare` requires the measurement to agree with it, so the message and the file cannot drift apart.
IMAGE_FACTS = ("symbols", "functions", "addresses", "vtables")
IMAGE_CLAIMS = ("claim_slot", "claim_image")


def image_claims(facts):
    """Which claims refuse a run with no linked image - measured by emptying the image's fact groups and
    asking each claim, rather than listed from memory."""
    blank = dict(facts)
    for key in IMAGE_FACTS:
        blank[key] = {}
    measured = []
    for claim in CLAIMS:
        failures, notes = [], []
        try:
            claim(blank, failures, notes)
        except Exception:
            # A claim that *raises* without the image is as image-dependent as one that reports: either
            # way there is no reading to be had, which is the whole content of the `--image` message.
            failures = ["raised"]
        if failures:
            measured.append(claim.__name__)
    return tuple(measured)


def compare(facts, mutate=None):
    if mutate is not None:
        facts = mutate_facts(facts, mutate)
    failures, notes = [], []
    if mutate is None:
        measured = image_claims(facts)
        if measured != IMAGE_CLAIMS:
            failures.append("the claims that read the linked image measure out as %s and the `--image` "
                            "message names %s; the message is what a user without an image reads first, "
                            "so it has to be the measurement rather than a remembered list"
                            % (", ".join(measured) or "none", ", ".join(IMAGE_CLAIMS) or "none"))
    for claim in CLAIMS:
        claim(facts, failures, notes)
    return failures, notes


# ------------------------------------------------------------------------------------------------
# The selftest
# ------------------------------------------------------------------------------------------------

def _bump(text, needle, replacement):
    assert text.count(needle) >= 1, needle
    return text.replace(needle, replacement, 1)


def _bump_all(text, needle, replacement):
    """Every occurrence, for a property that is stated *twice* in the source.

    `IORegistryEntry::init`'s two loops are textually identical, so a mutation that replaced the first
    occurrence only would leave the second one saying the property still holds - and the mutation would
    be a claim about one loop wearing the name of a claim about both. That is 239's defect one file
    over, and the reason this helper exists.
    """
    assert text.count(needle) >= 1, needle
    return text.replace(needle, replacement)


def mutate_facts(facts, mutate):
    facts = dict(facts)
    facts["symbols"] = dict(facts["symbols"])
    facts["addresses"] = dict(facts["addresses"])
    facts["vtables"] = {name: list(words) for name, words in facts["vtables"].items()}
    facts["functions"] = {name: list(lines) for name, lines in facts["functions"].items()}

    def src(key, needle, replacement):
        facts[key] = _bump(facts[key], needle, replacement)

    def src_all(key, needle, replacement):
        facts[key] = _bump_all(facts[key], needle, replacement)

    def function_key(name):
        """The key the image facts hold a function under, including GCC's partial suffix.

        `entry_class_words` is `static` and two callers share it, so the linked name is
        `entry_class_words.part.0.constprop.0`; a mutation written against the bare name would raise
        `KeyError` rather than mutating anything, and a mutation that mutates nothing is the defect
        this selftest exists to catch (239).
        """
        if name in facts["functions"]:
            return name
        for candidate in facts["functions"]:
            if candidate.startswith(name + "."):
                return candidate
        raise AssertionError("no function named %s in the image facts" % name)

    if mutate == "start_iokit_makes_another_class":
        src("start_iokit", "new IOPlatformExpertDevice", "new IOService")
    elif mutate == "start_iokit_stops_initializing":
        src("start_iokit", "initWithArgs", "initWithProperties")
    elif mutate == "the_adoption_precedes_the_build":
        body = facts["init_with_args"]
        alloc = "if( dtTop && (dt = IODeviceTreeAlloc( dtTop )))"
        adopt = "ok = super::init( dt, gIODTPlane );"
        assert alloc in body and adopt in body
        facts["init_with_args"] = body.replace(alloc, "@@ALLOC@@").replace(adopt, alloc).replace(
            "@@ALLOC@@", adopt)
    elif mutate == "init_with_args_initializes_from_nothing":
        src("init_with_args", "super::init( dt, gIODTPlane )", "super::init()")
    elif mutate == "the_tree_is_built_and_dropped":
        src("init_with_args", "IODeviceTreeAlloc( dtTop )", "IODeviceTreeAlloc( 0 )")
    elif mutate == "service_init_drops_the_plane":
        src("service_init", "super::init(from, inPlane)", "super::init()")
    elif mutate == "the_root_is_not_an_ioservice":
        src("make_table", "new IOService", "new IOServicePM")
    elif mutate == "the_tree_is_not_attached_to_the_registry_root":
        src("dt_alloc", "attachToParent( IORegistryEntry::getRegistryRoot(), gIODTPlane)",
            "attachToParent( 0, gIODTPlane)")
    elif mutate == "the_plane_indices_swap":
        src("ioregistry", "kChildSetIndex\t= 1,", "kChildSetIndex\t= 0,")
    elif mutate == "the_child_set_key_is_not_removed":
        src("init", "old->registryTable()->removeObject( plane->keys[ kChildSetIndex ] );", ";")
    elif mutate == "the_parent_set_key_is_not_removed":
        src("init", "old->registryTable()->removeObject( plane->keys[ kParentSetIndex ] );", ";")
    elif mutate == "the_table_is_copied_after_the_removals":
        body = facts["init"]
        copy = "fPropertyTable = old->dictionaryWithProperties();"
        remove = "old->registryTable()->removeObject( plane->keys[ kChildSetIndex ] );"
        assert copy in body and remove in body
        facts["init"] = body.replace(copy, "@@COPY@@").replace(remove, copy).replace("@@COPY@@", remove)
    elif mutate == "the_parents_child_links_are_not_moved":
        src("init", "next->makeLink( this, kChildSetIndex, plane );",
            "next->makeLink( old, kChildSetIndex, plane );")
    elif mutate == "the_parents_links_are_not_broken":
        src("init", "next->breakLink( old, kChildSetIndex, plane );",
            "next->breakLink( this, kChildSetIndex, plane );")
    elif mutate == "the_childrens_parent_links_are_not_moved":
        src("init", "next->makeLink( this, kParentSetIndex, plane );",
            "next->makeLink( old, kParentSetIndex, plane );")
    elif mutate == "the_childrens_links_are_not_broken":
        src("init", "next->breakLink( old, kParentSetIndex, plane );",
            "next->breakLink( this, kParentSetIndex, plane );")
    elif mutate == "the_loops_stop_being_guarded":
        src_all("init", "(next = (IORegistryEntry *) all->getObject(index))",
                "(next = (IORegistryEntry *) all->getObject(0))")
    elif mutate == "the_handover_copies_the_table_deeply":
        src("with_props", "withDictionary( getPropertyTable(),",
            "withDictionary( getPropertyTable()->copyCollection(),")
    elif mutate == "the_dictionary_copy_is_not_shallow":
        src("init_with_dict", "dictionary[i].value->taggedRetain(OSTypeID(OSCollection));",
            "dictionary[i].value = dict->dictionary[i].value->copyCollection();")
    elif mutate == "with_dictionary_stops_delegating":
        src("with_dict", "initWithDictionary(dict, capacity)", "asDictionary(dict, capacity)")
    elif mutate == "the_copy_comes_from_another_table":
        src("with_props", "withDictionary( getPropertyTable(),",
            "withDictionary( gIORegistryPlanes,")
    elif mutate == "a_class_record_loses_its_key":
        src("dtclass", 'entry_live_write("xnu_live_dtc_class1", class1);', ";")
    elif mutate == "the_two_records_share_a_key":
        src("dtrec", 'entry_live_write("xnu_live_dtrec_obj", obj);',
            'entry_live_write("xnu_live_dtc_obj", obj);')
    elif mutate == "a_class_record_takes_four_things":
        src("stubs", "uint32_t obj, uint32_t class0, uint32_t class1, uint32_t kids, uint32_t set)\n{\n    g_dtc_calls++;",
            "uint32_t obj, uint32_t class0, uint32_t class1, uint32_t kids)\n{\n    g_dtc_calls++;")
    elif mutate == "the_class_record_is_the_returned_entry":
        src("census", "entry_note_dtclass((uint32_t)(uintptr_t)root,",
            "entry_note_dtclass((uint32_t)(uintptr_t)recorded,")
    elif mutate == "the_returned_entry_is_not_named":
        src("census", "entry_note_dtrec(", "entry_note_dtclass2(")
    elif mutate == "the_returned_entry_is_read_unguarded":
        src("census", "if (recorded != 0) {", "{")
    elif mutate == "the_walk_stops_naming_its_object":
        src("walk", "entry_class_words(STAGE90_CLS_WALK, first, &class0, &class1);", ";")
    elif mutate == "the_walk_forgets_its_class":
        src("walk", "entry_note_dtwalk(t1, (uint32_t)(uintptr_t)root, (uint32_t)count, "
                    "(uint32_t)(uintptr_t)first,\n                      (uint32_t)(uintptr_t)set, "
                    "(uint32_t)kids, class0, class1,\n                      (uint32_t)(uintptr_t)control);",
            "entry_note_dtwalk(t1, (uint32_t)(uintptr_t)root, (uint32_t)count, "
            "(uint32_t)(uintptr_t)first,\n                      (uint32_t)(uintptr_t)set, "
            "(uint32_t)kids, 0u, 0u,\n                      (uint32_t)(uintptr_t)control);")
    elif mutate == "the_walk_record_drops_a_class_word":
        src("dtwalk", 'entry_live_write("xnu_live_walk_class1", class1);', ";")
    elif mutate == "the_walk_record_stores_one_slot":
        src("dtwalk", "g_walk_t2_class0 = class0;", ";")
    elif mutate == "the_class_words_skip_the_vtable":
        src("class_words", "entry_object_meta(site, obj)", "(const void *)obj")
    elif mutate == "the_guard_calls_the_object_itself":
        src("class_meta", "*(const void *const *)obj", "*(const void *const *)&obj")
    elif mutate == "the_guard_skips_the_preamble_test":
        src("class_meta", "pre0 == 0u && pre1 == 0u && fnw >= STAGE90_IMAGE_BASE &&",
            "fnw >= STAGE90_IMAGE_BASE &&")
    elif mutate == "the_guard_skips_the_image_bound":
        src("class_meta", "fnw >= STAGE90_IMAGE_BASE &&\n            fnw < (uint32_t)(uintptr_t)&__bss_start",
            "fnw != 0u")
    elif mutate == "the_guard_slots_a_literal":
        src("class_meta", ")[STAGE90_VTABLE_META_CLASS - STAGE90_VTABLE_PREAMBLE]",
            ")[STAGE90_VTABLE_META_CLASS]")
    elif mutate == "the_guard_forgets_the_preamble":
        src("class_meta", "STAGE90_VTABLE_META_CLASS - STAGE90_VTABLE_PREAMBLE",
            "STAGE90_VTABLE_META_CLASS - 0u")
    elif mutate == "the_preamble_is_read_at_the_vptr":
        src("class_meta", "pre0 = ((const uint32_t *)vptr)[-(int)STAGE90_VTABLE_PREAMBLE];",
            "pre0 = ((const uint32_t *)vptr)[0];")
    elif mutate == "the_preamble_define_moves":
        src("trace", "#define STAGE90_VTABLE_PREAMBLE 2u", "#define STAGE90_VTABLE_PREAMBLE 1u")
    elif mutate == "the_guard_publishes_only_after_the_call":
        src("class_meta", "    if (obj == 0) {\n        entry_note_class(site, 0u, 0u, 0u, 0u, 0u, 0u, 0u);\n"
                          "        return 0;\n    }\n\n", "")
    elif mutate == "the_guard_does_not_say_whether_it_called":
        src("class_meta", "took = 1u;", ";")
    elif mutate == "the_site_is_not_named":
        src("census", "entry_class_words(STAGE90_CLS_ROOT, root", "entry_class_words(0u, root")
    elif mutate == "the_guard_record_is_not_in_the_image":
        lines = facts["functions"][function_key(CLASS_WORDS)]
        facts["functions"][function_key(CLASS_WORDS)] = [
            re.sub(r"\s*<" + re.escape(CLASS_NOTE) + r"(\.[^>]*)?>", " <local>", line) for line in lines]
    elif mutate == "the_class_words_are_not_zeroed":
        src("class_words", "*w0 = 0u;\n    *w1 = 0u;", "*w1 = 0u;")
    elif mutate == "the_class_words_skip_the_object":
        src("class_words", "if (obj != 0) {", "{")
    elif mutate == "the_class_is_read_from_the_meta_class":
        src("trace", 'asm__("_ZNK11OSMetaClass12getClassNameEv")',
            'asm__("_ZNK11OSMetaClass18getClassNameSymbolEv")')
    elif mutate == "the_class_name_is_declared_twice":
        src("trace", "extern const char *entry_xnu_metaclass_name(const void *meta);",
            "extern const char *entry_xnu_metaclass_name(const void *meta);\n"
            "extern const char *entry_xnu_class_name2(const void *meta)\n"
            '    __asm__("_ZNK11OSMetaClass12getClassNameEv");')
    elif mutate == "the_base_class_is_called_by_name":
        src("trace", "typedef const void *(*entry_meta_class_fn)(const void *);",
            "extern const void *entry_xnu_get_meta_class(const void *self)\n"
            '    __asm__("_ZNK8OSObject12getMetaClassEv");\n'
            "typedef const void *(*entry_meta_class_fn)(const void *);")
    elif mutate == "the_slot_define_moves":
        src("trace", "#define STAGE90_VTABLE_META_CLASS 9u", "#define STAGE90_VTABLE_META_CLASS 8u")
    elif mutate == "the_slot_is_a_literal_in_the_load":
        src("class_meta", "STAGE90_VTABLE_META_CLASS - STAGE90_VTABLE_PREAMBLE]", "9]")
    elif mutate == "the_vptr_comes_from_a_member":
        src("class_meta", "*(const void *const *)obj",
            "*((const void *const *)obj + 1)")
    elif mutate == "a_vtable_is_missing_from_the_image":
        facts["vtables"].pop(VOBJECT_VTABLE, None)
        facts["symbols"].pop(VOBJECT_VTABLE, None)
    elif mutate == "the_slot_moves_in_one_vtable":
        facts["vtables"][VOBJECT_VTABLE] = list(facts["vtables"][VOBJECT_VTABLE])
        facts["vtables"][VOBJECT_VTABLE][SLOT] = 0
    elif mutate == "the_vtable_preamble_is_not_zero":
        facts["vtables"][VOBJECT_VTABLE] = list(facts["vtables"][VOBJECT_VTABLE])
        facts["vtables"][VOBJECT_VTABLE][0] = 1
    elif mutate == "the_other_class_keeps_its_name_elsewhere":
        facts["vtables"][VSERVICE_VTABLE] = list(facts["vtables"][VSERVICE_VTABLE])
        facts["vtables"][VSERVICE_VTABLE][SLOT] = 0
    elif mutate == "a_report_key_is_dropped":
        src("stubs", 'entry_write_kv("xnu_entry_dtc_class0", g_dtc_class0);', ";")
    elif mutate == "a_live_key_is_dropped":
        src("stubs", 'entry_live_write("xnu_live_dtrec_kids", kids);', ";")
    elif mutate == "the_returned_entrys_children_are_not_published":
        src("stubs", 'entry_write_kv("xnu_entry_dtrec_set", g_dtrec_set);', ";")
    elif mutate == "the_class_name_is_undefined_in_the_image":
        facts["symbols"][CLASS_NAME] = "U"
    elif mutate == "the_class_name_is_not_in_the_image":
        facts["symbols"].pop(CLASS_NAME, None)
    elif mutate == "the_census_does_not_read_the_class":
        lines = facts["functions"][CENSUS]
        facts["functions"][CENSUS] = [re.sub(r"\s*<" + re.escape(CLASS_WORDS) + r"(\.[^>]*)?>",
                                             " <local>", line) for line in lines]
    elif mutate == "the_census_reads_the_class_itself":
        lines = facts["functions"][CENSUS]
        facts["functions"][CENSUS] = [re.sub(r"(bl\s+[0-9a-f]+\s+<)" + re.escape(CLASS_WORDS),
                                             r"\1" + CLASS_NAME, line) for line in lines]
    elif mutate == "the_census_does_not_write_the_record":
        lines = facts["functions"][CENSUS]
        facts["functions"][CENSUS] = [re.sub(r"\s*<entry_note_dtclass>", " <local>", line)
                                      for line in lines]
    elif mutate == "the_walk_does_not_read_the_class":
        lines = facts["functions"][WALK]
        facts["functions"][WALK] = [re.sub(r"\s*<" + re.escape(CLASS_WORDS) + r"(\.[^>]*)?>",
                                           " <local>", line) for line in lines]
    elif mutate == "the_class_words_lose_the_name":
        key = function_key(CLASS_WORDS)
        facts["functions"][key] = [re.sub(r"\s*<" + re.escape(CLASS_NAME) + r">", " <local>", line)
                                   for line in facts["functions"][key]]
    elif mutate == "the_class_words_write_past_the_terminator":
        key = function_key(CLASS_WORDS)
        facts["functions"][key] = [re.sub(r"\s*<entry_str8\.[^>]*>|\s*<entry_str8>", " <local>", line)
                                   for line in facts["functions"][key]]
    else:
        raise AssertionError("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "start_iokit_makes_another_class", "start_iokit_stops_initializing",
    "the_adoption_precedes_the_build", "init_with_args_initializes_from_nothing",
    "the_tree_is_built_and_dropped", "service_init_drops_the_plane", "the_root_is_not_an_ioservice",
    "the_tree_is_not_attached_to_the_registry_root",
    "the_plane_indices_swap", "the_child_set_key_is_not_removed", "the_parent_set_key_is_not_removed",
    "the_table_is_copied_after_the_removals", "the_parents_child_links_are_not_moved",
    "the_parents_links_are_not_broken", "the_childrens_parent_links_are_not_moved",
    "the_childrens_links_are_not_broken", "the_loops_stop_being_guarded",
    "the_handover_copies_the_table_deeply", "the_dictionary_copy_is_not_shallow",
    "with_dictionary_stops_delegating", "the_copy_comes_from_another_table",
    "a_class_record_loses_its_key", "the_two_records_share_a_key", "a_class_record_takes_four_things",
    "the_class_record_is_the_returned_entry", "the_returned_entry_is_not_named",
    "the_returned_entry_is_read_unguarded", "the_walk_stops_naming_its_object",
    "the_walk_forgets_its_class", "the_walk_record_drops_a_class_word",
    "the_walk_record_stores_one_slot", "the_class_words_are_not_zeroed",
    "the_class_words_skip_the_object", "the_class_words_skip_the_vtable",
    "the_guard_calls_the_object_itself", "the_guard_skips_the_preamble_test",
    "the_guard_skips_the_image_bound", "the_guard_slots_a_literal",
    "the_guard_forgets_the_preamble", "the_preamble_is_read_at_the_vptr",
    "the_preamble_define_moves",
    "the_guard_publishes_only_after_the_call", "the_guard_does_not_say_whether_it_called",
    "the_site_is_not_named", "the_guard_record_is_not_in_the_image",
    "the_class_is_read_from_the_meta_class", "the_class_name_is_declared_twice",
    "the_base_class_is_called_by_name", "the_slot_define_moves",
    "the_slot_is_a_literal_in_the_load", "the_vptr_comes_from_a_member",
    "a_vtable_is_missing_from_the_image", "the_slot_moves_in_one_vtable",
    "the_vtable_preamble_is_not_zero", "the_other_class_keeps_its_name_elsewhere",
    "a_report_key_is_dropped", "a_live_key_is_dropped",
    "the_returned_entrys_children_are_not_published",
    "the_class_name_is_undefined_in_the_image", "the_class_name_is_not_in_the_image",
    "the_census_does_not_read_the_class", "the_census_reads_the_class_itself",
    "the_census_does_not_write_the_record", "the_class_words_lose_the_name",
    "the_class_words_write_past_the_terminator",
    "the_walk_does_not_read_the_class",
)


def selftest(facts):
    # **The baseline first, and it is not a formality**: every mutation below is "the check must still
    # report a failure after this edit", so on a baseline that already fails - the sources on a machine
    # where the tree was never assembled, say - every mutation is refused for the baseline's reason and
    # the run says nothing. 484's selftest is where this project learned that.
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
    say("  --selftest: all %d mutations were refused" % len(MUTATIONS))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--image", default=None, help="the linked entry image")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    if not args.image:
        print("FAIL: --image is required: %d of the %d claims (%s) are about the linked image - the "
              "vtable slot the class is read at - two classes' worth of it - the accessor's presence, "
              "and which functions' text transfers to it - and there is no default that can stand in "
              "for it. `image_claims` measures that set and `compare` requires it to be this one."
              % (len(IMAGE_CLAIMS), len(CLAIMS), ", ".join(IMAGE_CLAIMS)), file=sys.stderr)
        return 1
    facts = gather(args.image)
    if args.selftest:
        return selftest(facts)

    failures, notes = compare(facts)
    if args.verbose:
        for note in notes:
            say("    " + note)
    if failures:
        print("FAIL: the device tree's ownership is not the handover this step measures:", file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    say("  xnu_entry_486: the tree root's children are adopted by the entry `StartIOKit` makes - the "
        "shallow copy of the property table first, the child-set key removed from the tree root's own "
        "table second, then every parent link and every child link moved - and the instrument names both "
        "entries by class, through the object's own vtable at the slot the image keeps `getMetaClass` "
        "in, in two records under two key sets, with the object published before it is called and the "
        "call made only when its first word looks like a vtable of this image")
    return 0


if __name__ == "__main__":
    sys.exit(main())
