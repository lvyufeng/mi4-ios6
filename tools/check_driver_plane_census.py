#!/usr/bin/env python3
"""
Check the things 487's census of the *second plane* - and 491's census of the level below it -
depends on, before the device is asked.

485 measured the driver layer's table - 21 children of the registry's IODT child, every one
`Registered | Matched` - and 486 named the two entries at the top of it. Neither asked what the 21
*are*, and the class of each of them is the one reading that says whether Apple's own nub pass ran:
a device-tree entry is `new IOService` (`IODeviceTreeSupport.cpp:359`), and the object the platform
expert's driver puts in its place is `new IOPlatformDevice` (`IOPlatformExpert.cpp:1283`). The
second half of this project's goal is "the basic drivers run", so the question this step asks is not
whether the nodes are known to the registry any more - they are - but whether anything was
*attached* to a provider, which is a link in `gIOServicePlane` and therefore a child set any accessor
can read.

The claims below are what keeps that reading from being a comment.

  1. **the two planes are this kernel's, and the service plane's root is the object the IODT walk
     starts from.** `gIODTPlane` and `gIOServicePlane` are two globals in the linked image at two
     addresses, and `IOService::attach( 0 )` is where the second one's root comes from: the
     zero-provider branch is `gIOServiceRoot = this;` followed by
     `attachToParent( getRegistryRoot(), gIOServicePlane )` (`IOService.cpp:663-666`), and
     `StartIOKit` calls exactly that (`rootNub->attach( 0 )`, `IOStartIOKit.cpp:157`). So the census's
     `same` - `getServiceRoot()` against the object the IODT walk starts from - is a prediction with
     a basis in Apple's source rather than a coincidence, and the claim is that the basis is still
     there.

  2. **the machinery the prediction names is in this image.** `IOPlatformDevice` has a metaclass
     object, a vtable and constructors here, and `IODTPlatformExpert`'s `configure`, `processTopLevel`,
     `createNubs` and `createNub` are defined here - so a run whose children read `IOService` says
     the missing link is an *instance* (a concrete `IODTPlatformExpert` subclass, which this kernel
     has none of: `deleteList` and `excludeList` are pure virtual) and not a missing function. The
     chain is read out of Apple's own file in the same claim: `configure` calls `processTopLevel`,
     `processTopLevel` calls `createNubs`, `createNubs` calls `createNub` and then
     `nub->attach( parent )` / `nub->registerService()`, and `createNub` is `new IOPlatformDevice`.

  3. **the two censuses are one shape.** The same six accessors (`getChildSetReference`,
     `getChildCount`, `OSArray::getCount`, `OSArray::getObject`, `getName`, `getState`), the same
     `__state[1]` offset expression, the same loop guard, and a published pair of counts. Two numbers
     about two planes are only comparable if they were taken the same way, and this claim is a
     comparison of the two bodies rather than a list of what each contains.

  4. **the caps agree.** `STAGE90_SVC_MAX` equals `STAGE90_DTK_MAX`, and each is within the array it
     indexes (`ENTRY_DTK_SHOWN`, `ENTRY_SVC_SHOWN`) - because a census that stops at a different place
     than the other one is a census whose count cannot be compared with it, and a cap larger than the
     array is a write past the end of a `.bss` array.

  5. **the class-read sites are five and distinct.** `STAGE90_CLS_CHILD` is used once, in the IODT
     census's child loop, and `STAGE90_CLS_SVCCHILD` once, in the service census's - so a class word in
     the log can be attributed to one of the two loops without counting, which is 486's reason for
     having sites at all.

  6. **every key this step adds has a live writer and a report writer.** This boot never returns from
     `vm_pageout`, so a report-only record is a record that is never taken (459) - and a key that is
     not written reads as zero, which is what "the child had no class" also prints.

  7. **the wrapper runs both censuses, in order, before the call that never returns.** The IODT
     census's return value is what the service census compares itself with, so the call has to be
     between the two: a service census called first would compare `getServiceRoot()` with nothing, and
     one called after `__real_vm_pageout()` would never run at all.

  8. **the third census walks the level the nubs are attached at.** `createNubs( this, ... )` passes the
     *platform expert instance*, not the root, so `nub->attach( parent )` puts the 21 nubs one level
     below the only place 487 read - and a run whose service census reads two rows while its tree
     census reads twenty-one cannot say whether the nub pass ran at all. The claim is that the walk
     descends by taking each **row's** child set and not the root's again, that it publishes each row's
     own child count so the table names the object the nubs hang from rather than the instrument
     choosing it, that it says how many rows it descended into, and that it is called between the
     service census and the call that never returns.

  9. **the Matched bit is not a match, and the key says so.** `_matched` is gone from every door this
     step publishes - report and live channel alike - and `_matchpass` with `_inactive` are in its
     place, because `IOService::doServiceMatch` sets `kIOServiceMatchedState` for every service that
     registers and is not Inactive. `check_boot_completion.py` claim 6 reads that out of Apple's file;
     this claim is only that no writer here still spells the old name.

    ./tools/check_driver_plane_census.py --image out/stage90/xnu_arm_entry.elf
    ./tools/check_driver_plane_census.py --image out/stage90/xnu_arm_entry.elf --selftest

What this check cannot say
--------------------------
Which class the run prints for the 21 children, and how many services are attached to the platform
expert. 487 predicted `IOService` for the children and an empty service plane, on the ground that no
`IODTPlatformExpert` subclass exists in this kernel for `configure` to run in - and **490's run
falsified both halves**: the children's class reads `IOPlatform*` at every one of the 21 rows, and the
service plane holds two entries, `MSM8974P` and `IOResources`, from the live channel of a run that
never reached the report. So a subclass does exist - the fixture supplies one - and the reading 487
could not take is the one this step's third census walks. What this file guarantees is that the run is
*able* to answer: three planes' worth of state is read, every count is published, every row is named
and classed, and every key has a live writer as well as a report one.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.path.join(REPO_ROOT, "external/xnu-4570.1.46")
BOOT_DIR = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot")

ENTRY_TRACE_C = os.path.join(BOOT_DIR, "entry_trace.c")
ENTRY_STUBS_C = os.path.join(BOOT_DIR, "entry_stubs.c")
BUILD_ENTRY_SH = os.path.join(BOOT_DIR, "build_entry.sh")
PLATFORM_EXPERT_CPP = os.path.join(XNU, "iokit/Kernel/IOPlatformExpert.cpp")
PLATFORM_EXPERT_H = os.path.join(XNU, "iokit/IOKit/IOPlatformExpert.h")
SERVICE_CPP = os.path.join(XNU, "iokit/Kernel/IOService.cpp")
START_IOKIT_CPP = os.path.join(XNU, "iokit/Kernel/IOStartIOKit.cpp")
DEVICE_TREE_CPP = os.path.join(XNU, "iokit/Kernel/IODeviceTreeSupport.cpp")

NM = "arm-none-eabi-nm"
OBJDUMP = "arm-none-eabi-objdump"

IODT_PLANE = "gIODTPlane"
ISVC_PLANE = "gIOServicePlane"
SERVICE_ROOT = "_ZN9IOService14getServiceRootEv"
SVC_GETTER = "entry_xnu_service_root"

# The two censuses, the two class-read sites, and the keys each one publishes.
CENSUS = "entry_probe_dt_children"
SVC_CENSUS = "entry_probe_service_plane"
# 491's third census, which is the one the goal's "basic drivers" half turns on: `createNubs(this,...)`
# attaches the nubs to the platform expert *instance*, one level below the root the service census
# walks, so the services that were attached for matching have never been in a table.
PEX_CENSUS = "entry_probe_service_tree"
PEX_WRAPPER = "__wrap_vm_pageout"
DTCHILD_CLS = "entry_note_dtchildcls"
SVC_CHILD = "entry_note_svcchild"
PEX_CHILD = "entry_note_pexchild"
REPORT_GROUP = "entry_write_485_kv"

DT_SITE = "STAGE90_CLS_CHILD"
SVC_SITE = "STAGE90_CLS_SVCCHILD"
PEX_SITE = "STAGE90_CLS_PEXCHILD"
DT_CAP = "STAGE90_DTK_MAX"
SVC_CAP = "STAGE90_SVC_MAX"
# The third census has a cap per level and a table between them: the outer loop walks at most
# `PEX_ROOT_CAP` of the root's children and the inner one at most the rows the table has left.
PEX_ROOT_CAP = "STAGE90_PEX_ROOT"
PEX_DEEP_CAP = "STAGE90_PEX_DEEP"
DT_ARRAY = "ENTRY_DTK_SHOWN"
SVC_ARRAY = "ENTRY_SVC_SHOWN"
PEX_ARRAY = "ENTRY_PEX_DEEP"
PEX_ROOT_ARRAY = "ENTRY_PEX_ROOT"

# The six accessors every census of this shape reads, and the offset expression both use for
# `__state[1]`. A census missing one of them is not the same measurement as the other one.
ACCESSORS = (
    "entry_xnu_child_set(",
    "entry_xnu_child_count(",
    "entry_xnu_array_count(",
    "entry_xnu_array_object(",
    "entry_xnu_entry_name(",
    "entry_xnu_entry_state(",
)
STATE1 = "STAGE90_DTK_STATE0_OFF + 4u"

# 487's live keys, one per number the two censuses publish.
LIVE_KEYS = (
    "xnu_live_dtcc_seq", "xnu_live_dtcc_class0", "xnu_live_dtcc_class1",
    "xnu_live_svc_calls", "xnu_live_svc_plane", "xnu_live_svc_root", "xnu_live_svc_same",
    "xnu_live_svc_set", "xnu_live_svc_kids", "xnu_live_svc_count", "xnu_live_svc_none",
    "xnu_live_svc_seq", "xnu_live_svc_child", "xnu_live_svc_name0", "xnu_live_svc_name1",
    "xnu_live_svc_class0", "xnu_live_svc_class1", "xnu_live_svc_state0", "xnu_live_svc_state1",
    # 491's: the tallies 487 published only in the report - which the run that takes the census never
    # reaches - and the whole of the third census.
    "xnu_live_svc_matchpass", "xnu_live_svc_inactive", "xnu_live_svc_registered",
    "xnu_live_svc_named", "xnu_live_svc_shown",
    "xnu_live_dtk_matchpass", "xnu_live_dtk_inactive", "xnu_live_dtk_registered",
    "xnu_live_dtk_named", "xnu_live_dtk_shown",
    "xnu_live_pex_calls", "xnu_live_pex_plane", "xnu_live_pex_root", "xnu_live_pex_set",
    "xnu_live_pex_kids", "xnu_live_pex_none", "xnu_live_pex_deep_l1", "xnu_live_pex_seen",
    "xnu_live_pex_shown", "xnu_live_pex_named", "xnu_live_pex_matchpass", "xnu_live_pex_inactive",
    "xnu_live_pex_registered", "xnu_live_pex_seq", "xnu_live_pex_depth", "xnu_live_pex_child",
    "xnu_live_pex_name0", "xnu_live_pex_name1", "xnu_live_pex_class0", "xnu_live_pex_class1",
    "xnu_live_pex_state0", "xnu_live_pex_state1", "xnu_live_pex_kids_of",
    # 492's: the catalogue's own answer, which is the reading the third census's `kids_of` column is
    # about. `_gen` is the generation the call was made with and `_res` says whether the service asked
    # about was `gIOResources` - `findDrivers` is asked about two populations and a zero means
    # something different in each.
    "xnu_live_finddrv_seq", "xnu_live_finddrv_site", "xnu_live_finddrv_svc",
    "xnu_live_finddrv_set", "xnu_live_finddrv_count", "xnu_live_finddrv_gen", "xnu_live_finddrv_res",
    "xnu_live_finddrv_some", "xnu_live_finddrv_none", "xnu_live_finddrv_null",
    "xnu_live_finddrv_max",
)
# And the report keys, which are written in 485's group beside the rest of the census.
REPORT_KEYS = (
    "xnu_entry_dtcc_calls", "xnu_entry_dtcc_c0", "xnu_entry_dtcc_c1", "xnu_entry_dtcc_named",
    "xnu_entry_svc_calls", "xnu_entry_svc_plane", "xnu_entry_svc_root", "xnu_entry_svc_same",
    "xnu_entry_svc_set", "xnu_entry_svc_kids", "xnu_entry_svc_count", "xnu_entry_svc_shown",
    "xnu_entry_svc_none", "xnu_entry_svc_named", "xnu_entry_svc_matchpass",
    "xnu_entry_svc_registered", "xnu_entry_svc_child0", "xnu_entry_svc_name00",
    "xnu_entry_svc_name01", "xnu_entry_svc_class00", "xnu_entry_svc_class01",
    "xnu_entry_svc_state00", "xnu_entry_svc_state01",
    "xnu_entry_svc_inactive",
    "xnu_entry_pex_calls", "xnu_entry_pex_plane", "xnu_entry_pex_root", "xnu_entry_pex_set",
    "xnu_entry_pex_kids", "xnu_entry_pex_deep_l1", "xnu_entry_pex_none", "xnu_entry_pex_seen",
    "xnu_entry_pex_shown", "xnu_entry_pex_named", "xnu_entry_pex_matchpass",
    "xnu_entry_pex_inactive", "xnu_entry_pex_registered",
    "xnu_entry_pex_depth0", "xnu_entry_pex_child0", "xnu_entry_pex_name00", "xnu_entry_pex_name01",
    "xnu_entry_pex_class00", "xnu_entry_pex_class01", "xnu_entry_pex_state00",
    "xnu_entry_pex_state01", "xnu_entry_pex_kids_of0",
    "xnu_entry_pex_depth1", "xnu_entry_pex_child1", "xnu_entry_pex_name10", "xnu_entry_pex_name11",
    "xnu_entry_pex_class10", "xnu_entry_pex_class11", "xnu_entry_pex_state10",
    "xnu_entry_pex_state11", "xnu_entry_pex_kids_of1",
    # 492's, written in the same group and in the same order as their live twins.
    "xnu_entry_finddrv_calls", "xnu_entry_finddrv_some", "xnu_entry_finddrv_none",
    "xnu_entry_finddrv_null", "xnu_entry_finddrv_max", "xnu_entry_finddrv_site",
    "xnu_entry_finddrv_svc", "xnu_entry_finddrv_set", "xnu_entry_finddrv_count",
    "xnu_entry_finddrv_gen", "xnu_entry_finddrv_res",
)

# The names the prediction is written in, and which the run's reading will confirm or falsify.
PREDICTED_TREE = "IOService"
PREDICTED_NUB = "IOPlatformDevice"

# `IOPlatformDevice` is 16 characters, so its vtable and its metaclass are these.
NUB_VTABLE = "_ZTV16IOPlatformDevice"
NUB_META = "_ZN16IOPlatformDevice10gMetaClassE"
CREATE_NUB = "_ZN18IODTPlatformExpert9createNubEP15IORegistryEntry"
CREATE_NUBS = "_ZN18IODTPlatformExpert10createNubsEP9IOServiceP10OSIterator"


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


def run(command):
    return subprocess.run(command, capture_output=True, text=True).stdout


def nm(path):
    """`name -> kind`, for the question "is it there"."""
    symbols = {}
    for line in run([NM, path]).splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols[parts[2]] = parts[1]
        elif len(parts) == 2:
            symbols[parts[1]] = "U"
    return symbols


def nm_addresses(path):
    """`name -> address`, which is a different question from `nm`'s kind: this check needs both, and
    reading the kind where an address was meant is a defect this project has already paid for."""
    addresses = {}
    for line in run([NM, path]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                addresses[parts[2]] = int(parts[0], 16)
            except ValueError:
                continue
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
    """The body of a *definition* of `name`.

    `parameters` disambiguates overloads the way 486's check does - `IORegistryEntry::init` has two
    definitions and only one performs the handover - and a declaration or a call is skipped because
    what follows the closing parenthesis is not a `{`.
    """
    for match in re.finditer(r"(?<![\w])%s\s*\(" % re.escape(name), text):
        open_paren = text.index("(", match.start())
        close_paren = _matching(text, open_paren, "(", ")")
        if close_paren < 0:
            continue
        if parameters is not None and parameters not in text[open_paren + 1:close_paren]:
            continue
        after = text[close_paren + 1:]
        qualifiers = re.match(r"[A-Za-z_0-9\s*]*\{", after)
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
    found = []
    for line in lines:
        match = re.search(r"\b(bl|blx|b)\s+([0-9a-f]+)\s+<([^>]+)>", line)
        if match:
            found.append((match.group(2), match.group(3)))
    return found


def reaches(targets, name):
    """Whether a transfer list contains `name` - or the partial GCC emitted for it.

    A `static` function with two callers is still one function, but the `bl` names the split
    (`name.part.0.constprop.0`), and comparing a transfer target with a bare name reports a call that
    is there as a call that is not. 486's defect 242, in this check's one use.
    """
    return any(target == name or target.startswith(name + ".") for target in targets)


def find_function(functions, name):
    if name in functions:
        return functions[name]
    for candidate, lines in functions.items():
        if candidate.startswith(name + "."):
            return lines
    return None


def define(text, name):
    """The integer a `#define name <n>u` holds, or None."""
    match = re.search(r"#define\s+%s\s+(\d+)u" % re.escape(name), text)
    return int(match.group(1)) if match else None


def gather(image):
    service_text = strip_comments(read(SERVICE_CPP))
    pexpert_text = strip_comments(read(PLATFORM_EXPERT_CPP))
    devtree_text = strip_comments(read(DEVICE_TREE_CPP))
    startiokit_text = strip_comments(read(START_IOKIT_CPP))
    facts = {
        "service": service_text,
        "pexpert": pexpert_text,
        "pexpert_h": strip_comments(read(PLATFORM_EXPERT_H)),
        "devtree": devtree_text,
        "startiokit": startiokit_text,
        "trace_text": read(ENTRY_TRACE_C),
        "stubs_text": read(ENTRY_STUBS_C),
        "build_text": read(BUILD_ENTRY_SH),
        "image": image,
        "symbols": nm(image) if image else {},
        "addresses": nm_addresses(image) if image else {},
        "functions": disassembly_functions(run([OBJDUMP, "-d", image])) if image else {},
    }
    facts["trace"] = strip_comments(facts["trace_text"])
    facts["stubs"] = strip_comments(facts["stubs_text"])
    # The class-read instrument is 486's, and this step reads it rather than re-declaring it: the
    # census calls `entry_class_words` with its own site, which is 240's rule one step on.
    for key, source in (("census", ("trace", CENSUS, None)),
                        ("svc_census", ("trace", SVC_CENSUS, None)),
                        ("pex_census", ("trace", PEX_CENSUS, None)),
                        ("wrapper", ("trace", "__wrap_vm_pageout", None)),
                        ("dtchild_cls", ("stubs", DTCHILD_CLS, None)),
                        ("svc_child", ("stubs", SVC_CHILD, None)),
                        ("pex_child", ("stubs", PEX_CHILD, None)),
                        ("svc_begin", ("stubs", "entry_note_svcbegin", None)),
                        ("pex_begin", ("stubs", "entry_note_pexbegin", None)),
                        ("pex_end", ("stubs", "entry_note_pexend", None)),
                        ("report_group", ("stubs", REPORT_GROUP, None)),
                        ("attach", ("service", "IOService::attach", "IOService * provider")),
                        ("configure", ("pexpert", "IODTPlatformExpert::configure", None)),
                        ("top_level", ("pexpert", "IODTPlatformExpert::processTopLevel", None)),
                        ("create_nubs", ("pexpert", "IODTPlatformExpert::createNubs", None)),
                        ("create_nub", ("pexpert", "IODTPlatformExpert::createNub",
                                        "IORegistryEntry * from"))):
        source_key, name, parameters = source
        facts[key] = function_body(facts[source_key], name, parameters)
    return facts


# ------------------------------------------------------------------------------------------------
# The claims
# ------------------------------------------------------------------------------------------------

def claim_planes(facts, failures, notes):
    """1. Two planes, two globals, and the service plane's root is the object the IODT walk starts
    from - Apple's own statement, read here rather than remembered."""
    addresses = facts["addresses"]
    if not addresses:
        failures.append("no image was read, so the two planes cannot be compared in it")
        return

    for name in (IODT_PLANE, ISVC_PLANE):
        if name not in addresses:
            failures.append("`%s` is not a symbol in the linked image; one of the two censuses is "
                            "reading a plane that is not there" % name)
    if IODT_PLANE in addresses and ISVC_PLANE in addresses:
        if addresses[IODT_PLANE] == addresses[ISVC_PLANE]:
            failures.append("`%s` and `%s` are the same address in the linked image, so the two "
                            "censuses would be two readings of one plane"
                            % (IODT_PLANE, ISVC_PLANE))
        else:
            notes.append("the two planes are two globals: `%s` = 0x%08x, `%s` = 0x%08x"
                         % (IODT_PLANE, addresses[IODT_PLANE], ISVC_PLANE, addresses[ISVC_PLANE]))

    if SERVICE_ROOT not in facts["symbols"]:
        failures.append("`IOService::getServiceRoot` (`%s`) is not in the linked image, so the second "
                        "plane has no root to census" % SERVICE_ROOT)

    # The census must read the *service* plane, and the sentence that says so is in the source.
    census = facts["census"] or ""
    svc = facts["svc_census"] or ""
    if "= %s;" % IODT_PLANE not in census:
        failures.append("the IODT census no longer reads `%s` into its plane" % IODT_PLANE)
    if "= %s;" % ISVC_PLANE not in svc:
        failures.append("the service census does not read `%s` into its plane; a census of the device "
                        "tree called twice is not a census of the driver layer" % ISVC_PLANE)
    if IODT_PLANE in svc:
        failures.append("the service census mentions `%s`, and the plane it must read is `%s`: the two "
                        "planes are two registries over the same objects and reading the wrong one "
                        "answers a question this step is not asking" % (IODT_PLANE, ISVC_PLANE))

    # And the root of the service plane is the IODT walk's root *because Apple says so*, not because
    # the two pointers happened to be equal once.
    attach = facts["attach"]
    if attach is None:
        failures.append("`IOService::attach` was not found in Apple's own file, so the claim about "
                        "where the service plane's root comes from has nothing to read")
    else:
        for needle in ("gIOServiceRoot = this;",
                       "attachToParent( getRegistryRoot(), gIOServicePlane)"):
            if needle not in attach:
                failures.append("`IOService::attach`'s zero-provider branch no longer contains `%s`, "
                                "so `getServiceRoot()` is no longer the object `attach(0)` made the "
                                "root of the service plane" % needle)
    start = facts["startiokit"]
    if start is None or "attach( 0 )" not in start:
        failures.append("`StartIOKit` no longer calls `attach( 0 )`, so this boot's service plane may "
                        "have no root at all")
    if "root == iokit_root" not in svc:
        failures.append("the service census no longer compares `getServiceRoot()` with the object the "
                        "IODT census walked, so the two roots are read without being compared - and "
                        "`same` is the whole of the cross-check between the two planes")
    begin = facts["svc_begin"] or ""
    if 'entry_live_write("xnu_live_svc_same", same)' not in begin:
        failures.append("`entry_note_svcbegin` no longer publishes the comparison it is given, so the "
                        "cross-check is computed and thrown away")


def claim_machinery(facts, failures, notes):
    """2. The machinery the prediction names is in this image, and the chain that reaches it is in
    Apple's own file."""
    addresses = facts["addresses"]
    if addresses:
        for name, what in ((NUB_VTABLE, "`IOPlatformDevice`'s vtable"),
                           (NUB_META, "`IOPlatformDevice`'s metaclass object")):
            if name not in addresses:
                failures.append("%s (`%s`) is not in the linked image, so the class this step predicts "
                                "for a nub cannot be what any child reads" % (what, name))
        for name in (CREATE_NUB, CREATE_NUBS):
            if name not in addresses:
                failures.append("`%s` is not in the linked image; the prediction's basis is that the "
                                "nub pass is *here* and has not run, and a missing function would make "
                                "that basis wrong" % name)

    chain = (("configure", "processTopLevel", "`configure` no longer calls `processTopLevel`"),
             ("top_level", "createNubs", "`processTopLevel` no longer calls `createNubs`"),
             # Both of `processTopLevel`'s nub sets, because the function publishes two - the cpus and
             # the top level minus the exclude list - and a claim that only asked for the name would
             # accept a body that had dropped one of them (this check's own selftest found that).
             ("top_level", "createNubs( this, IODTFindMatchingEntries( cpus, kIODTExclusive, 0))",
              "`processTopLevel` no longer creates the nubs under `/cpus`"),
             ("top_level", "createNubs( this, IODTFindMatchingEntries( rootEntry, kIODTExclusive, "
                           "excludeList()))",
              "`processTopLevel` no longer creates the top level's nubs"),
             ("create_nubs", "createNub", "`createNubs` no longer calls `createNub`"),
             ("create_nubs", "registerService", "`createNubs` no longer registers the nub it made"),
             ("create_nub", "new IOPlatformDevice", "`createNub` no longer makes an `IOPlatformDevice`"))
    for key, needle, complaint in chain:
        body = facts[key]
        if body is None:
            failures.append("`%s` was not found in Apple's own file, so the chain that makes "
                            "`IOPlatformDevice` the predicted class cannot be read" % key)
        elif needle not in body:
            failures.append("%s: the chain `configure` -> `processTopLevel` -> `createNubs` -> "
                            "`createNub` -> `new IOPlatformDevice` is what the prediction rests on"
                            % complaint)
    # The two pure virtuals are what make `IODTPlatformExpert` abstract, which is why this kernel
    # missing an *instance* is a different finding from a missing function. They are declared in the
    # header and never defined, so the header is what this claim reads.
    header = facts["pexpert_h"]
    for name in ("deleteList", "excludeList"):
        if not re.search(r"%s\s*\(\s*void\s*\)\s*=\s*0\s*;" % name, header):
            failures.append("`IODTPlatformExpert::%s` is no longer declared pure virtual in Apple's "
                            "header; without it the class is concrete and 'this kernel has no "
                            "instance' would not be the frontier" % name)
    if all(facts[key] is not None for key in ("configure", "top_level", "create_nubs", "create_nub")):
        notes.append("the nub pass is in the image and has a caller chain: `configure` -> "
                     "`processTopLevel` -> `createNubs` -> `createNub`, whose class is "
                     "`IOPlatformDevice` in the same image")


def claim_shape(facts, failures, notes):
    """3. The two censuses are one shape: the same accessors, the same state offset, the same guard."""
    census = facts["census"]
    svc = facts["svc_census"]
    if census is None or svc is None:
        failures.append("one of the two censuses was not found in `entry_trace.c`, so their shapes "
                        "cannot be compared - and a comparison of two numbers taken differently is "
                        "not a comparison")
        return

    missing = [name for name in ACCESSORS if name not in census]
    if missing:
        failures.append("the IODT census no longer calls %s" % ", ".join(missing))
    missing = [name for name in ACCESSORS if name not in svc]
    if missing:
        failures.append("the service census does not call %s, so it is not the same measurement as "
                        "the census it is compared with" % ", ".join(missing))

    if STATE1 not in census or STATE1 not in svc:
        failures.append("both censuses must read `__state[1]` at `%s` - the offset 455 read out of "
                        "`getState`'s own load; a second offset would be a second definition of one "
                        "word" % STATE1)

    for body, name in ((census, CENSUS), (svc, SVC_CENSUS)):
        if "entry_xnu_array_count(" not in body or "entry_xnu_child_count(" not in body:
            failures.append("`%s` no longer publishes two counts of one child set, so the number it "
                            "prints has nothing to be compared with" % name)
        if "getChildSetReference" in body:
            failures.append("`%s` spells the child set's accessor by name rather than calling "
                            "`entry_xnu_child_set`; the two roads would agree until one was edited" % name)

    if "entry_probe_service_plane" in (facts["trace"] or "") and "STAGE90_SVC_MAX" not in svc:
        failures.append("the service census's loop no longer stops at `%s`, so a tree bigger than the "
                        "arrays would write past them" % SVC_CAP)


def claim_caps(facts, failures, notes):
    """4. The caps agree with each other and fit the arrays they index."""
    dt_cap = define(facts["trace"], DT_CAP)
    svc_cap = define(facts["trace"], SVC_CAP)
    dt_array = define(facts["stubs"], DT_ARRAY)
    svc_array = define(facts["stubs"], SVC_ARRAY)

    for value, name in ((dt_cap, DT_CAP), (svc_cap, SVC_CAP), (dt_array, DT_ARRAY),
                        (svc_array, SVC_ARRAY)):
        if value is None:
            failures.append("`%s` is no longer defined as a `#define <n>u`, so the cap it holds "
                            "cannot be compared" % name)
    if dt_cap is None or svc_cap is None or dt_array is None or svc_array is None:
        return

    if dt_cap != svc_cap:
        failures.append("`%s` = %d and `%s` = %d: two censuses of one shape that stop at different "
                        "places have counts that cannot be compared with each other"
                        % (DT_CAP, dt_cap, SVC_CAP, svc_cap))
    root_cap = define(facts["trace"], PEX_ROOT_CAP)
    deep_cap = define(facts["trace"], PEX_DEEP_CAP)
    pex_array = define(facts["stubs"], PEX_ARRAY)
    root_array = define(facts["stubs"], PEX_ROOT_ARRAY)
    for value, name in ((root_cap, PEX_ROOT_CAP), (deep_cap, PEX_DEEP_CAP),
                        (pex_array, PEX_ARRAY), (root_array, PEX_ROOT_ARRAY)):
        if value is None:
            failures.append("`%s` is no longer defined as a `#define <n>u`, so the third census's cap "
                            "cannot be compared with the table it indexes" % name)
    if None not in (root_cap, deep_cap, pex_array, root_array):
        if root_cap > root_array:
            failures.append("`%s` = %d descends into more of the root's children than `%s[%d]` has room "
                            "for" % (PEX_ROOT_CAP, root_cap, PEX_ROOT_ARRAY, root_array))
        if deep_cap > pex_array:
            failures.append("`%s` = %d writes past `%s[%d]`: the third census's rows share one table "
                            "between both levels, so the cap that bounds the rows is the array's own "
                            "size" % (PEX_DEEP_CAP, deep_cap, PEX_ARRAY, pex_array))
        if root_cap >= deep_cap:
            failures.append("`%s` = %d is not smaller than `%s` = %d, so the inner loop's bound "
                            "(`%s - %s`) is zero or negative and the second level - the one the nubs "
                            "are at - would never be walked"
                            % (PEX_ROOT_CAP, root_cap, PEX_DEEP_CAP, deep_cap, PEX_DEEP_CAP,
                               PEX_ROOT_CAP))
        census = facts["pex_census"] or ""
        if ("j < (%s - %s)" % (PEX_DEEP_CAP, PEX_ROOT_CAP)) not in census:
            failures.append("the service-tree census's inner loop no longer stops at `%s - %s`, so its "
                            "two levels can write past the one table they share"
                            % (PEX_DEEP_CAP, PEX_ROOT_CAP))

    if dt_cap > dt_array:
        failures.append("`%s` = %d writes past `%s[%d]`" % (DT_CAP, dt_cap, DT_ARRAY, dt_array))
    if svc_cap > svc_array:
        failures.append("`%s` = %d writes past `%s[%d]`" % (SVC_CAP, svc_cap, SVC_ARRAY, svc_array))
    if dt_cap <= dt_array and svc_cap <= svc_array and dt_cap == svc_cap:
        notes.append("both censuses stop at %d, inside arrays of %d and %d"
                     % (dt_cap, dt_array, svc_array))


def claim_sites(facts, failures, notes):
    """5. Five class-read sites, five distinct numbers, one use each in the two loops."""
    trace = facts["trace"]
    sites = {}
    for name in ("STAGE90_CLS_WALK", "STAGE90_CLS_ROOT", "STAGE90_CLS_RECORDED", DT_SITE, SVC_SITE,
                 PEX_SITE):
        value = define(trace, name)
        if value is None:
            failures.append("`%s` is no longer a `#define <n>u`; a site that cannot be read is a "
                            "site that cannot be attributed" % name)
        sites[name] = value
    known = [value for value in sites.values() if value is not None]
    if len(set(known)) != len(known):
        failures.append("two class-read sites share a number (%s), so a class word in the log cannot "
                        "be attributed to one of them" % sites)
    if 0 in known:
        failures.append("a class-read site is 0, which is the value a record that was never written "
                        "reads as")

    census = facts["census"] or ""
    svc = facts["svc_census"] or ""
    if census.count("%s," % DT_SITE) != 1:
        failures.append("`%s` is used %d times in the IODT census's child loop; one use per child is "
                        "what makes the record's `seq` the child's `seq`"
                        % (DT_SITE, census.count("%s," % DT_SITE)))
    if svc.count("%s," % SVC_SITE) != 1:
        failures.append("`%s` is used %d times in the service census" % (SVC_SITE,
                                                                        svc.count("%s," % SVC_SITE)))
    if SVC_SITE in census or DT_SITE in svc:
        failures.append("the two censuses use each other's class-read site, so the sites no longer "
                        "separate the tree's children from the attached services")
    # 491: the third census's site, used in both of its loops - one site and not two, because the two
    # levels answer one question and the record already carries `depth`.
    pex = facts["pex_census"] or ""
    uses = pex.count("%s," % PEX_SITE)
    if uses != 2:
        failures.append("`%s` is used %d times in the service-tree census and not twice - once per "
                        "level, because both levels' rows are the same question and the record's "
                        "`depth` is what separates them" % (PEX_SITE, uses))
    for other in (census, svc):
        if PEX_SITE in other:
            failures.append("one of the first two censuses uses the service-tree census's class-read "
                            "site, so a class word in the log can no longer be attributed to the "
                            "level it came from")


def claim_keys(facts, failures, notes):
    """6. Every key has a live writer and a report writer, and the census calls the writers."""
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

    group = stubs.find("void %s" % REPORT_GROUP)
    if group < 0:
        failures.append("`%s` is gone, so the report keys of this step have no group to be written "
                        "in" % REPORT_GROUP)
    else:
        body = stubs[group:]
        end = body.find("\n}")
        body = body[:end]
        for key in REPORT_KEYS:
            if '"%s"' % key not in body:
                failures.append("the report key `%s` is written outside `%s`; the epilogue's constant "
                                "pool is at the PC-relative edge (455) and a key in the wrong function "
                                "is a key that may not link" % (key, REPORT_GROUP))

    # 9: the old name is gone from every door, live and report alike. A tally named `_matched` states
    # an outcome - "the kernel matched a driver to this service" - that `doServiceMatch` does not test
    # for; `check_boot_completion.py` claim 6 reads the setter out of Apple's file, and this is the
    # consequence on this step's own keys.
    for stale in ("g_svc_matched", "g_dtk_matched", "xnu_entry_svc_matched", "xnu_live_svc_matched",
                  "xnu_entry_dtk_matched", "xnu_live_dtk_matched", "xnu_entry_svc_matched"):
        if stale in stubs:
            failures.append("`entry_stubs.c` still spells `%s`: the bit it counts is set by "
                            "`IOService::doServiceMatch` for every service that registers and is not "
                            "Inactive, so the name states an outcome the kernel never tests" % stale)

    census = facts["census"] or ""
    svc = facts["svc_census"] or ""
    if "entry_note_dtchildcls(" not in census:
        failures.append("the IODT census no longer calls `%s`, so no child's class is recorded" %
                        DTCHILD_CLS)
    if "entry_note_svcchild(" not in svc:
        failures.append("the service census no longer calls `%s`, so no attached service is named" %
                        SVC_CHILD)
    if "entry_note_svcbegin(" not in svc or "entry_note_svcnone(" not in svc:
        failures.append("the service census no longer publishes which object it read and the reason "
                        "it took no reading; 'there is no service plane' and 'nothing is attached' "
                        "would print the same record")
    note_count = len([key for key in LIVE_KEYS if key.startswith("xnu_live_svc_")])
    notes.append("every key this step adds has a live writer and a report writer (%d live, %d report, "
                 "%d of them the second plane's)" % (len(LIVE_KEYS), len(REPORT_KEYS), note_count))


def claim_service_tree(facts, failures, notes):
    """8. The third census walks the level the nubs are actually attached at, and it is called.

    487's owed reading was "the platform expert instance's own service children". `createNubs( this,
    ... )` (`IOPlatformExpert.cpp:1360`, `:1363`) passes the **expert**, not the root the service
    census walks, and `createNubs` does `nub->attach( parent )` on each nub it makes (`:1310-1311`) -
    so the services that were attached for matching hang one level below the only place this project
    has read, and a run whose service census reads two rows and whose tree census reads twenty-one
    cannot say whether the nub pass ran at all. This claim is what makes that reading a measurement
    rather than a second walk that might be of the same level again.
    """
    pex = facts["pex_census"]
    stubs = facts["stubs"]
    if pex is None:
        failures.append("`entry_probe_service_tree` is gone, so the level `createNubs` attaches at is "
                        "not walked and the driver layer's own services have no table")
        return

    missing = [name for name in ACCESSORS if name not in pex]
    if missing:
        failures.append("the service-tree census no longer calls %s, so it is not the same "
                        "measurement as the two censuses it sits under" % ", ".join(missing))
    if STATE1 not in pex:
        failures.append("the service-tree census does not read `__state[1]` at `%s`, so its rows carry "
                        "one state word where the other two censuses carry two" % STATE1)

    # The second level, which is the whole reason this census exists: it descends by taking each row's
    # *own* child set - not the root's again, which is what a copy of the service census would do.
    if "entry_xnu_child_set(child, plane)" not in pex:
        failures.append("the service-tree census never takes a *row's* child set, so it is a walk of "
                        "the root's children and not of the level below them - and the nubs, which "
                        "`createNubs` attaches to the platform expert instance, would still be "
                        "unread")
    if "entry_xnu_child_count(child, plane)" not in pex:
        failures.append("the service-tree census no longer publishes each row's own child count, which "
                        "is what names the object the nubs hang from inside the table instead of the "
                        "instrument choosing it before the fact")
    # The descended count is the *variable*, not the name: `entry_note_pexdeep(0u)` is also written on
    # the path where there is no child set at all, so a claim stated over the name alone would be
    # satisfied by the early return (this check's own selftest is what said so).
    if "entry_note_pexdeep(l1)" not in pex:
        failures.append("the service-tree census no longer publishes how many rows it descended into, "
                        "so a walk that stopped after the first of them reads as a census of the whole "
                        "subtree")
    # Two rows per level and two levels: the outer row and the inner one. One call would be a table
    # that only ever holds the root's own children, which is the census this one exists to go below.
    if pex.count("entry_note_pexchild(") != 2:
        failures.append("the service-tree census calls `entry_note_pexchild(` %d times and not twice - "
                        "once per level - so one of the two levels has no rows in the table"
                        % pex.count("entry_note_pexchild("))
    for call in ("entry_note_pexbegin(", "entry_note_pexnone(", "entry_note_pexend("):
        if call not in pex:
            failures.append("the service-tree census no longer calls `%s`" % call)
    for why in ("entry_note_pexnone(0u)", "entry_note_pexnone(1u)", "entry_note_pexnone(2u)"):
        if why not in pex:
            failures.append("the service-tree census has lost the `%s` reason: no plane, no service "
                            "root and no child set are three findings and they must not print as one "
                            "record" % why)

    # And it has to be *called*, at the one moment downstream of everything the boot does.
    wrapper = facts["wrapper"]
    if wrapper is None:
        failures.append("`__wrap_vm_pageout` is gone, so nothing takes the census")
    else:
        if "entry_probe_service_tree()" not in wrapper:
            failures.append("`__wrap_vm_pageout` no longer calls the service-tree census, so the walk "
                            "exists and is never run")
        elif "entry_probe_service_plane(" not in wrapper:
            failures.append("`__wrap_vm_pageout` no longer calls the service census, so the census "
                            "below it has no root to be below - and the order this claim is about "
                            "cannot be read")
        else:
            after = wrapper.index("entry_probe_service_plane(")
            here = wrapper.index("entry_probe_service_tree()")
            last = wrapper.rindex("__real_vm_pageout();")
            if here < after:
                failures.append("the service-tree census runs before the service census: the level it "
                                "reads is reached through the root the other one reads, so a run in "
                                "which the root moved would be walked against a stale one")
            if here > last:
                failures.append("the service-tree census runs after `__real_vm_pageout()`, which is "
                                "followed by Apple's own NOTREACHED marker (`startup.c:647`) - so it "
                                "would never run at all")

    for key in ("xnu_live_pex_matchpass", "xnu_live_pex_kids_of", "xnu_live_pex_depth"):
        if 'entry_live_write("%s"' % key not in stubs:
            failures.append("the service-tree census does not publish `%s` on the live channel; the "
                            "report is written after `vm_pageout`'s wrapper returns, which the kernel "
                            "says cannot happen, so a report-only record is a record never taken"
                            % key)


def claim_prediction(facts, failures, notes):
    """7. The prediction is stated in the source with both candidate classes, and the run can
    distinguish them: the two classes' names are the two the reading would print."""
    trace = facts["trace_text"]
    stubs = facts["stubs_text"]

    # The prediction is prose, so it is read as prose - but it has to name *both* possible answers,
    # because a comment that names one is a comment that cannot be falsified by the run.
    block = trace[trace.find("487") - 2000 if trace.find("487") > 2000 else 0:]
    for name in (PREDICTED_TREE, PREDICTED_NUB):
        if name not in block:
            failures.append("`entry_trace.c`'s 487 block no longer names `%s`, so the prediction the "
                            "run will confirm or falsify is no longer written down" % name)
    if PREDICTED_NUB not in stubs:
        failures.append("`entry_stubs.c` no longer names `%s` beside its globals, so the key the run "
                        "would have to be read against is not in the writer" % PREDICTED_NUB)

    addresses = facts["addresses"]
    if addresses:
        if NUB_VTABLE not in addresses:
            failures.append("`%s` is not in the linked image: the run could not print `%s` for a "
                            "child even if the nub pass ran" % (NUB_VTABLE, PREDICTED_NUB))
        else:
            notes.append("both classes the prediction names are resolvable in this image: `%s` and "
                         "`%s` (0x%08x)" % (PREDICTED_TREE, PREDICTED_NUB, addresses[NUB_VTABLE]))


def claim_wrapper(facts, failures, notes):
    """8. Both censuses run inside `vm_pageout`'s wrapper, in order, before the call that never
    returns."""
    body = facts["wrapper"]
    if body is None:
        failures.append("`__wrap_vm_pageout` was not found in `entry_trace.c`, so neither census "
                        "runs")
        return
    note = body.find("entry_note_boot_tail(")
    dt = body.find("%s()" % CENSUS)
    svc = body.find("%s(" % SVC_CENSUS)
    real = body.find("__real_vm_pageout(")
    for position, what in ((dt, CENSUS), (svc, SVC_CENSUS)):
        if position < 0:
            failures.append("`__wrap_vm_pageout` no longer calls `%s`" % what)
    if note < 0 or real < 0:
        failures.append("`__wrap_vm_pageout` no longer records its position before the call it wraps")
        return
    if dt >= 0 and svc >= 0 and note >= 0 and real >= 0:
        if not note < dt < svc < real:
            failures.append("the wrapper's order is record(%d) census(%d) service(%d) call(%d): the "
                            "service census takes the first one's return value, so it has to run "
                            "after it, and both have to run before a call that never returns"
                            % (note, dt, svc, real))
    if "= %s()" % CENSUS not in body:
        failures.append("the IODT census's return value is no longer kept, so the service census has "
                        "nothing to compare `getServiceRoot()` with")
    if re.search(r"%s\(\s*0\s*\)" % SVC_CENSUS, body):
        failures.append("the service census is called with a literal `0` as the root to compare "
                        "with, which is not a reading of the tree")
    if svc >= 0 and real >= 0 and svc < real:
        notes.append("both planes are read between the wrapper's own record and the call that never "
                     "returns, the IODT census first")


CLAIMS = (claim_planes, claim_machinery, claim_shape, claim_caps, claim_sites, claim_keys,
          claim_service_tree, claim_prediction, claim_wrapper)


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


def _bump_all(text, needle, replacement):
    """Every occurrence, for a property stated twice in one file."""
    assert text.count(needle) >= 1, needle
    return text.replace(needle, replacement)


def mutate_facts(facts, mutate):
    facts = dict(facts)
    facts["addresses"] = dict(facts["addresses"])
    facts["symbols"] = dict(facts["symbols"])
    facts["functions"] = {name: list(lines) for name, lines in facts["functions"].items()}

    def src(key, needle, replacement):
        facts[key] = _bump(facts[key], needle, replacement)

    def body(key, needle, replacement):
        assert needle in facts[key], needle
        facts[key] = facts[key].replace(needle, replacement, 1)

    if mutate == "the_service_census_reads_the_dt_plane":
        src("svc_census", "const void *plane = gIOServicePlane;", "const void *plane = gIODTPlane;")
    elif mutate == "the_dt_census_reads_the_service_plane":
        src("census", "const void *plane = gIODTPlane;", "const void *plane = gIOServicePlane;")
    elif mutate == "the_two_planes_are_one_global":
        facts["addresses"][ISVC_PLANE] = facts["addresses"][IODT_PLANE]
    elif mutate == "the_service_plane_is_missing_from_the_image":
        del facts["addresses"][ISVC_PLANE]
    elif mutate == "get_service_root_is_missing_from_the_image":
        del facts["addresses"][SERVICE_ROOT]
        facts["symbols"].pop(SERVICE_ROOT, None)
    elif mutate == "attach_no_longer_sets_the_service_root":
        body("attach", "gIOServiceRoot = this;", ";")
    elif mutate == "attach_links_the_root_to_another_plane":
        body("attach", "attachToParent( getRegistryRoot(), gIOServicePlane)",
             "attachToParent( getRegistryRoot(), gIODTPlane)")
    elif mutate == "start_iokit_attaches_to_a_provider":
        src("startiokit", "rootNub->attach( 0 );", "rootNub->attach( this );")
    elif mutate == "the_nub_vtable_is_missing_from_the_image":
        del facts["addresses"][NUB_VTABLE]
    elif mutate == "the_nub_metaclass_is_missing_from_the_image":
        del facts["addresses"][NUB_META]
    elif mutate == "create_nub_is_missing_from_the_image":
        del facts["addresses"][CREATE_NUB]
    elif mutate == "configure_stops_calling_process_top_level":
        body("configure", "processTopLevel( provider );", ";")
    elif mutate == "process_top_level_stops_creating_nubs":
        body("top_level", "createNubs( this, IODTFindMatchingEntries( cpus, kIODTExclusive, 0))",
             "createNubs( this, 0)")
    elif mutate == "create_nubs_stops_registering":
        body("create_nubs", "nub->registerService();", ";")
    elif mutate == "create_nub_makes_an_ioservice":
        body("create_nub", "new IOPlatformDevice", "new IOService")
    elif mutate == "the_pure_virtuals_are_gone":
        src("pexpert_h", "deleteList( void ) = 0;", "deleteList( void );")
    elif mutate == "the_service_census_drops_an_accessor":
        src("svc_census", "kids = entry_xnu_child_count(root, plane);", "")
    elif mutate == "the_service_census_stops_counting_the_set":
        src("svc_census", "n = entry_xnu_array_count(set);", "n = kids;")
    elif mutate == "the_service_census_reads_another_state_word":
        src("svc_census", "STAGE90_DTK_STATE0_OFF + 4u", "STAGE90_DTK_STATE0_OFF")
    elif mutate == "the_dt_census_reads_another_state_word":
        src("census", "STAGE90_DTK_STATE0_OFF + 4u", "STAGE90_DTK_STATE0_OFF")
    elif mutate == "the_service_census_spells_the_accessor_directly":
        src("svc_census", "set = entry_xnu_child_set(root, plane);",
            "set = getChildSetReference(root, plane);")
    elif mutate == "the_two_caps_disagree":
        src("trace", "#define STAGE90_SVC_MAX 24u", "#define STAGE90_SVC_MAX 25u")
    elif mutate == "the_service_array_is_smaller_than_its_cap":
        src("stubs", "#define ENTRY_SVC_SHOWN 24u", "#define ENTRY_SVC_SHOWN 23u")
    elif mutate == "the_dt_array_is_smaller_than_its_cap":
        src("stubs", "#define ENTRY_DTK_SHOWN 24u", "#define ENTRY_DTK_SHOWN 23u")
    elif mutate == "a_cap_is_a_literal":
        src("trace", "#define STAGE90_SVC_MAX 24u", "#define STAGE90_SVC_MAX (24u)")
    elif mutate == "two_sites_share_a_number":
        src("trace", "#define STAGE90_CLS_SVCCHILD 5u", "#define STAGE90_CLS_SVCCHILD 4u")
    elif mutate == "a_site_is_zero":
        src("trace", "#define STAGE90_CLS_CHILD    4u", "#define STAGE90_CLS_CHILD    0u")
    elif mutate == "the_two_loops_swap_sites":
        src("svc_census", "STAGE90_CLS_SVCCHILD, child", "STAGE90_CLS_CHILD, child")
    elif mutate == "the_dt_loop_forgets_the_class":
        src("census", "entry_class_words(STAGE90_CLS_CHILD, child, &cclass0, &cclass1);", "")
    elif mutate == "the_service_loop_forgets_the_class":
        src("svc_census", "entry_class_words(STAGE90_CLS_SVCCHILD, child, &class0, &class1);", "")
    # The key claims read the *file*, so these mutations edit the file and not the function body:
    # a mutation that edited the body would leave the text claim true and be accepted (this check's
    # own selftest is what found that, and it is 239's defect one file over).
    elif mutate == "a_live_key_is_dropped":
        src("stubs", 'entry_live_write("xnu_live_svc_class0", class0);', "")
    elif mutate == "a_live_key_is_renamed":
        src("stubs", 'entry_live_write("xnu_live_svc_count", count);',
            'entry_live_write("xnu_live_svc_count_v2", count);')
    elif mutate == "the_service_root_key_is_not_written":
        src("stubs", 'entry_live_write("xnu_live_svc_root", root);', "")
    elif mutate == "a_report_key_is_dropped":
        src("stubs", 'entry_write_kv("xnu_entry_svc_same", g_svc_same);', "")
    elif mutate == "a_report_key_leaves_the_group":
        src("stubs", 'entry_write_kv("xnu_entry_svc_none", g_svc_none);', "")
        facts["stubs"] += '\nvoid entry_write_487_stray(void) { entry_write_kv("xnu_entry_svc_none", 0); }\n'
    elif mutate == "the_census_stops_recording_the_class":
        src("census", "entry_note_dtchildcls(i, cclass0, cclass1);", "")
    elif mutate == "the_census_stops_recording_the_child":
        src("svc_census", "entry_note_svcchild(", "entry_note_nothing(")
    elif mutate == "the_census_stops_saying_why":
        # Both of the census's "no reading" records, not the first: a mutation that removed one of the
        # two would leave the claim's needle standing and be accepted (this check's selftest found it).
        facts["svc_census"] = facts["svc_census"].replace("entry_note_svcnone(", "entry_note_nothing(")
    elif mutate == "the_census_stops_naming_its_root":
        src("svc_census", "entry_note_svcbegin(", "entry_note_svcnothing(")
    elif mutate == "the_prediction_names_one_answer":
        assert facts["trace_text"].count(PREDICTED_NUB) >= 1
        facts["trace_text"] = facts["trace_text"].replace(PREDICTED_NUB, PREDICTED_TREE)
    elif mutate == "the_prediction_is_gone_from_the_writers":
        assert facts["stubs_text"].count(PREDICTED_NUB) >= 1
        facts["stubs_text"] = facts["stubs_text"].replace(PREDICTED_NUB, PREDICTED_TREE)
    elif mutate == "the_wrapper_does_not_census_the_service_plane":
        src("wrapper", "entry_probe_service_plane(iokit_root);", ";")
    elif mutate == "the_wrapper_censuses_the_service_plane_first":
        text = facts["wrapper"]
        dt = "iokit_root = entry_probe_dt_children();"
        svc = "entry_probe_service_plane(iokit_root);"
        assert dt in text and svc in text
        facts["wrapper"] = _bump(text, dt + "\n    " + svc, svc + "\n    " + dt)
    elif mutate == "the_service_census_compares_with_nothing":
        src("wrapper", "entry_probe_service_plane(iokit_root);", "entry_probe_service_plane(0);")
    elif mutate == "the_wrapper_forgets_the_census_return":
        src("wrapper", "iokit_root = entry_probe_dt_children();", "entry_probe_dt_children();")
    elif mutate == "the_census_runs_after_the_call_that_never_returns":
        src("wrapper", "__real_vm_pageout();", "__real_vm_pageout(); entry_probe_service_plane(0);")
    # 491's, over the third census. Each is a way the walk could stop being the level `createNubs`
    # attaches at while still producing a table and a count.
    elif mutate == "the_service_tree_census_walks_the_root_again":
        src("pex_census", "cset = entry_xnu_child_set(child, plane);",
            "cset = entry_xnu_child_set(root, plane);")
    elif mutate == "the_service_tree_census_forgets_its_depth":
        src("pex_census", "entry_note_pexdeep(l1);", ";")
    elif mutate == "the_service_tree_rows_are_not_recorded":
        src("pex_census", "entry_note_pexchild(", "entry_note_nothing(")
    elif mutate == "a_service_tree_live_key_is_dropped":
        src("stubs", 'entry_live_write("xnu_live_pex_depth", depth);', "")
    elif mutate == "the_service_tree_census_is_not_called":
        src("wrapper", "entry_probe_service_tree();", ";")
    elif mutate == "the_service_tree_census_runs_before_the_service_census":
        text = facts["wrapper"]
        dt = "    iokit_root = entry_probe_dt_children();\n"
        pex = "    entry_probe_service_tree();\n"
        assert dt in text and pex in text
        lines = [l for l in text.splitlines(keepends=True) if l != pex]
        assert len(lines) == len(text.splitlines(keepends=True)) - 1
        facts["wrapper"] = _bump("".join(lines), dt, pex + dt)
    elif mutate == "a_service_tree_reason_is_dropped":
        src("pex_census", "entry_note_pexnone(2u);", "entry_note_pexnone(1u);")
    elif mutate == "the_pex_caps_disagree":
        # Both needles are read out of the file rather than spelled here. They used to be the literal
        # `24u` of 491's census, and 492 raised the cap to 40: a mutation whose needle is a *value*
        # stops mutating the day that value moves, and `_bump`'s assertion is the only thing that
        # says so - the selftest would have gone from "refused" to "crashed", which is a different
        # statement about the check. The replacement is derived too: the point of this mutation is
        # that the deep cap and the root cap must not cross, so it puts the deep cap one below the
        # root cap whatever the two currently are (defect 275).
        deep = define(facts["trace"], PEX_DEEP_CAP)
        root = define(facts["trace"], PEX_ROOT_CAP)
        assert deep is not None and root is not None
        src("trace", "#define %s %du" % (PEX_DEEP_CAP, deep),
            "#define %s %du" % (PEX_DEEP_CAP, root - 1))
    elif mutate == "the_pex_array_is_smaller_than_its_cap":
        array = define(facts["stubs"], PEX_ARRAY)
        assert array is not None
        src("stubs", "#define %s %du" % (PEX_ARRAY, array),
            "#define %s %du" % (PEX_ARRAY, array - 1))
    elif mutate == "the_inner_loop_bound_is_a_literal":
        src("pex_census", "j < (STAGE90_PEX_DEEP - STAGE90_PEX_ROOT)", "j < 24u")
    elif mutate == "the_old_matched_name_comes_back":
        src("stubs", 'entry_live_write("xnu_live_svc_matchpass", g_svc_matchpass);',
            'entry_live_write("xnu_live_svc_matched", g_svc_matchpass);')
    elif mutate == "the_service_tree_site_is_the_service_census_s":
        src("trace", "#define STAGE90_CLS_PEXCHILD 6u", "#define STAGE90_CLS_PEXCHILD 5u")
    else:
        raise AssertionError("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "the_service_census_reads_the_dt_plane", "the_dt_census_reads_the_service_plane",
    "the_two_planes_are_one_global", "the_service_plane_is_missing_from_the_image",
    "get_service_root_is_missing_from_the_image", "attach_no_longer_sets_the_service_root",
    "attach_links_the_root_to_another_plane", "start_iokit_attaches_to_a_provider",
    "the_nub_vtable_is_missing_from_the_image", "the_nub_metaclass_is_missing_from_the_image",
    "create_nub_is_missing_from_the_image", "configure_stops_calling_process_top_level",
    "process_top_level_stops_creating_nubs", "create_nubs_stops_registering",
    "create_nub_makes_an_ioservice", "the_pure_virtuals_are_gone",
    "the_service_census_drops_an_accessor", "the_service_census_stops_counting_the_set",
    "the_service_census_reads_another_state_word", "the_dt_census_reads_another_state_word",
    "the_service_census_spells_the_accessor_directly", "the_two_caps_disagree",
    "the_service_array_is_smaller_than_its_cap", "the_dt_array_is_smaller_than_its_cap",
    "a_cap_is_a_literal", "two_sites_share_a_number", "a_site_is_zero", "the_two_loops_swap_sites",
    "the_dt_loop_forgets_the_class", "the_service_loop_forgets_the_class", "a_live_key_is_dropped",
    "a_live_key_is_renamed", "the_service_root_key_is_not_written", "a_report_key_is_dropped",
    "a_report_key_leaves_the_group", "the_census_stops_recording_the_class",
    "the_census_stops_recording_the_child", "the_census_stops_saying_why",
    "the_census_stops_naming_its_root", "the_prediction_names_one_answer",
    "the_prediction_is_gone_from_the_writers", "the_wrapper_does_not_census_the_service_plane",
    "the_wrapper_censuses_the_service_plane_first", "the_service_census_compares_with_nothing",
    "the_wrapper_forgets_the_census_return", "the_census_runs_after_the_call_that_never_returns",
    "the_service_tree_census_walks_the_root_again", "the_service_tree_census_forgets_its_depth",
    "the_service_tree_rows_are_not_recorded", "a_service_tree_live_key_is_dropped",
    "the_service_tree_census_is_not_called",
    "the_service_tree_census_runs_before_the_service_census",
    "a_service_tree_reason_is_dropped", "the_pex_caps_disagree",
    "the_pex_array_is_smaller_than_its_cap", "the_inner_loop_bound_is_a_literal",
    "the_service_tree_site_is_the_service_census_s", "the_old_matched_name_comes_back",
)


def selftest(facts):
    # **The baseline first.** Every mutation below is "the check must still report a failure after
    # this edit", so on a baseline that already fails - sources on a machine where the tree was never
    # assembled, say - every mutation is refused for the baseline's reason and the run says nothing.
    # 484's selftest is where this project learned that.
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
        print("FAIL: --image is required: claim 1 is about the two planes in the linked image, claim 2 "
              "about the nub pass's functions and the class they build, and claim 5 about the sites the "
              "class read is made at - three of this file's eight claims, and there is no default that "
              "can stand in for it", file=sys.stderr)
        return 1
    facts = gather(args.image)
    if args.selftest:
        return selftest(facts)

    failures, notes = compare(facts)
    if args.verbose:
        for note in notes:
            say("    " + note)
    if failures:
        print("FAIL: the driver layer's second plane is not the reading this step measures:",
              file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    say("  xnu_entry_487: the tree's children are named by class and the plane they would be attached "
        "in is censused - two planes read by one shape of census, the IODT census's root passed to the "
        "service census as its cross-check, every child's class read through the object's own vtable, "
        "and every number of both censuses in the live channel as well as the report")
    return 0


if __name__ == "__main__":
    sys.exit(main())
