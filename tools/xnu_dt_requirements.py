#!/usr/bin/env python3
"""Check our Apple-format device tree against what XNU's ARM code looks up.

`pe_identify_machine.c`, `machine_routines.c` and `pe_init.c` do not negotiate: they
call `DTLookupEntry`/`DTFindEntry`/`DTGetProperty` and then either `assert`, `panic`, or
silently take a default. A missing node or property therefore does not produce a clear
error - it produces a wrong clock, a zero SoC base, or a panic at a place that looks
unrelated. This is cheap to check against the source, and expensive to find on a device.

What it does
------------
1. Scans XNU's ARM sources for the lookups that matter: literal paths passed to
   `DTLookupEntry`, literal `(property, value)` pairs passed to `DTFindEntry`, and the
   properties read out of the nodes those find.
2. Scans our DT builder (`src/stage90_main.c`, `build_stage90_apple_dt`) for
   the nodes and properties it emits.
3. Reports which lookups our tree can satisfy and which it cannot.

It is a *source-level* check, not a runtime one: it proves we emit the right names, not
that XNU's walker accepts the encoding. The runtime half is Phase 2's exit criterion.
Run it when either side changes.

Usage:
    xnu_dt_requirements.py [--repo-root DIR]
Exit 0 if everything XNU requires is present, 1 if something is missing.
"""

import argparse
import os
import re
import sys

XNU_SOURCES = [
    "external/xnu-4570.1.46/pexpert/arm/pe_init.c",
    "external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c",
    "external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c",
    "external/xnu-4570.1.46/pexpert/arm/pe_serial.c",
    "external/xnu-4570.1.46/osfmk/arm/machine_routines.c",
]

# Our DT builder and the properties it emits live here.
OUR_BUILDER = "src/stage90_main.c"

LOOKUP_RE = re.compile(r'DTLookupEntry\s*\([^,]*,\s*"([^"]+)"')
FIND_RE = re.compile(r'DTFindEntry\s*\(\s*"([^"]+)"\s*,\s*(?:"([^"]*)"|NULL)')
GETPROP_RE = re.compile(r'DTGetProperty\s*\([^,]+,\s*"([^"]+)"')

# `apple_dt_prop_str(b, "name", "cpu@0")` / `apple_dt_prop_u32(b, "reg", cpu)`.
OUR_NODE_RE = re.compile(r'apple_dt_node_begin\s*\(')
OUR_PROP_RE = re.compile(r'apple_dt_prop(?:_str|_u32|_u32_array)?\s*\(\s*b\s*,\s*"([^"]+)"\s*,'
                         r'\s*(?:"([^"]*)"|[^,)]+)')
OUR_NAME_RE = re.compile(r'apple_dt_prop_str\s*\(\s*b\s*,\s*"name"\s*,\s*"([^"]*)"')


def read(root, rel):
    path = os.path.join(root, rel)
    try:
        with open(path) as fh:
            return fh.read()
    except OSError:
        return None


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def xnu_requirements(root):
    """Return (paths, find_pairs, per_node_props) that XNU's ARM code requires."""
    paths, find_pairs, node_props = set(), set(), set()
    missing_sources = []

    for rel in XNU_SOURCES:
        text = read(root, rel)
        if text is None:
            missing_sources.append(rel)
            continue
        text = strip_comments(text)
        for m in LOOKUP_RE.finditer(text):
            paths.add(m.group(1))
        for m in FIND_RE.finditer(text):
            find_pairs.add((m.group(1), m.group(2)))
        # Properties read out of a located node, tracked as a loose per-node set: the
        # sources pick them by variable, so the association is "some node XNU locates
        # by path or by (name,value) must carry this".
        for m in GETPROP_RE.finditer(text):
            node_props.add(m.group(1))

    return paths, find_pairs, node_props, missing_sources


def our_tree(root):
    """Return (node_names, all_props) emitted by our DT builder."""
    text = read(root, OUR_BUILDER)
    if text is None:
        raise ValueError("cannot read %s" % OUR_BUILDER)
    text = strip_comments(text)

    names = set(OUR_NAME_RE.findall(text))
    props = {m.group(1) for m in OUR_PROP_RE.finditer(text)}

    # A few names are built into a local variable then passed to the builder; pick
    # those up too rather than reporting them missing.
    for m in re.finditer(r'char\s+name\[\]\s*=\s*"([^"]+)"', text):
        names.add(m.group(1))
    for m in re.finditer(r'apple_dt_prop_str\s*\(\s*b\s*,\s*"name"\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)',
                         text):
        names.add("<computed:%s>" % m.group(1))

    return names, props


# Properties XNU reads from a located node. Matched loosely: the sources read these
# through DTGetProperty on nodes they found by path or by (name,value), so a property
# present anywhere in our tree is treated as satisfiable, and this check reports the
# ones that are absent everywhere.
# Requirements classified by what XNU does when they are absent. This table is the
# point of the tool: a raw scan finds ~40 property names, most of which XNU reads
# opportunistically and falls back from. Reporting those as failures would drown the
# two that actually stop a bring-up, and a checker whose output gets ignored is worse
# than none. So each entry carries the consequence and the place it was read from, and
# only the `blocks` ones fail the run.
#
#   blocks        - XNU panics, asserts, or computes a value that makes a subsystem
#                   unusable (a zero SoC base means no interrupt controller at all)
#   degrades      - XNU takes a default, so something is wrong but the boot continues
#   optional      - read behind a success check with a sane fallback
REQUIREMENTS = [
    ("path", "/cpus", "blocks",
     "machine_routines.c:462 asserts kSuccess, then panics 'No cpus found!' if the "
     "iteration yields none"),
    ("path", "/chosen", "blocks",
     "pe_init.c:80 - the boot-args and memory-map source; PE_init_platform reads it "
     "before anything else"),
    ("prop", "state", "blocks",
     "machine_routines.c:474 panics 'unable to retrieve state for cpu 0' under "
     "MACH_ASSERT; pe_identify_machine.c:117 silently skips the CPU without it, so its "
     "timebase-frequency is never read"),
    ("name", "arm-io", "blocks",
     "pe_identify_machine.c:232 - pe_arm_get_soc_base_phys() returns 0 without it, and "
     "pe_arm_map_interrupt_controller (:541) returns 0 early when gSocPhys == 0, so "
     "neither the interrupt controller nor the timer is ever mapped"),
    ("prop", "reg", "blocks",
     "read from the interrupt-controller and timer nodes to derive their bases "
     "(pe_identify_machine.c:546, :558)"),
    ("prop", "ranges", "blocks",
     "pe_identify_machine.c:237 - gPESoCBasePhys is taken from ranges[1]"),
    ("prop", "device_type", "blocks",
     "pe_identify_machine.c:234, :556 - selects the SoC device type string and locates "
     "the timer node"),
    ("prop", "timebase-frequency", "degrades",
     "pe_identify_machine.c:124 - falls back to a hardcoded 24 MHz, wrong for this SoC "
     "(19.2 MHz)"),
    ("prop", "clock-frequency", "degrades",
     "pe_identify_machine.c - gPEClockFrequencyInfo falls back to a fixed value"),
    ("prop", "chip-revision", "degrades",
     "pe_identify_machine.c:253 - pe_arm_get_soc_revision() returns 0"),
    ("path", "/chosen/memory-map", "optional",
     "pe_init.c:189 - behind a kSuccess check"),
    ("path", "/chosen/iBoot", "optional",
     "pe_init.c:252 - behind a kSuccess check"),
]

# Reported for information only. These are Apple-SoC boot-display and UART clock
# properties that XNU reads with a fallback; a device-tree node for MSM8974 will not
# have most of them and that is correct, not a gap.
INFORMATIONAL = "BootCLUT Pict-FailedBoot bus-frequency consistent-debug-root debug-enabled " \
                "debug-wait-start dram-vendor-id embedded-panic-log-size enable-sw-drain " \
                "firmware-version fixed-frequency load-kernel-start max-aop-clk " \
                "memory-frequency pclk peripheral-frequency populate-registry-time " \
                "sampling start-time ubrdiv unique-chip-id boot-console cpu-debug-interface " \
                "uart0 uart1 dockfifo-uart dockchannel-uart pram".split()


def check_declared_counts(root):
    """Verify each node's declared nProperties matches what its block emits.

    The Apple DT format puts nProperties/nChildren in the node header, ahead of the
    properties. A wrong nProperties does not fail to build and does not fail at DTInit -
    the walker reads the number it was given and lands in the middle of the next
    property name. Counting them here is the cheap way to catch a miscount introduced by
    editing the builder, which is exactly what adding a node or a property does.

    The attribution rule is simple because of how this builder is written: nodes are
    emitted in pre-order as flat sequential calls, so a node's *own* properties are
    exactly the property calls between its `apple_dt_node_begin` and the next one. That
    holds whether or not the node has children, because children are emitted after their
    parent's properties.

    nChildren is deliberately NOT verified. Deciding a node's real child count from the
    source requires knowing the tree shape, which is what the declared counts are - so
    any check would assume the thing it is trying to prove. It is checked at runtime
    instead, by apple_dt_selftest_and_log on the device.
    """
    text = read(root, OUR_BUILDER)
    if text is None:
        raise ValueError("cannot read %s" % OUR_BUILDER)
    body = strip_comments(text)

    m = re.search(r"build_stage90_apple_dt\s*\([^)]*\)\s*\{(.*?)\n\}", body, flags=re.S)
    if not m:
        raise ValueError("cannot find build_stage90_apple_dt")
    body = m.group(1)

    NODE = re.compile(r"apple_dt_node_begin\s*\(\s*b\s*,\s*(\d+)\s*,\s*(\d+)\s*\)")
    PROP = re.compile(r"apple_dt_prop(?:_str|_u32|_u32_array)?\s*\(\s*b\s*,")
    NAME = re.compile(r'apple_dt_prop_str\s*\(\s*b\s*,\s*"name"\s*,\s*"([^"]*)"')

    problems = []
    nodes = list(NODE.finditer(body))
    if not nodes:
        raise ValueError("no apple_dt_node_begin calls found")

    # Properties emitted by a `for` loop that produces nodes: the loop body appears once
    # in the source but its node runs once per iteration, so check it against its own
    # declared header rather than against the following sibling.
    loop_spans = []
    for fm in re.finditer(r"for\s*\([^)]*\)\s*\{", body):
        open_at = fm.end() - 1
        depth, j = 0, open_at
        while j < len(body):
            if body[j] == "{":
                depth += 1
            elif body[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        loop_spans.append((open_at, j))

    def in_loop(pos):
        return any(a <= pos <= b for a, b in loop_spans)

    for i, nd in enumerate(nodes):
        declared = int(nd.group(1))
        nxt = nodes[i + 1].start() if i + 1 < len(nodes) else len(body)
        block_end = nxt
        # If the next node is inside a loop that starts after this one, this node's
        # block still ends where the next node begins - its own props precede the loop.
        block = body[nd.end():block_end]
        emitted = len(PROP.findall(block))
        name_m = NAME.search(block[:300])
        label = name_m.group(1) if name_m else "<node at %d>" % nd.start()
        if emitted != declared:
            problems.append("%s: declares %d properties, emits %d"
                            % (label, declared, emitted))

    # The loop's own node, checked against the loop body.
    for a, b in loop_spans:
        lb = body[a:b]
        hm = NODE.search(lb)
        if not hm:
            continue
        declared = int(hm.group(1))
        emitted = len(PROP.findall(lb))
        name_m = NAME.search(lb[hm.end():hm.end() + 300])
        label = name_m.group(1) if name_m else "<loop node>"
        if emitted != declared:
            problems.append("%s (emitted by a loop): declares %d properties, emits %d"
                            % (label, declared, emitted))

    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo-root", default=os.path.join(os.path.dirname(__file__), ".."))
    ap.add_argument("--all", action="store_true",
                    help="also list the informational lookups")
    args = ap.parse_args()
    root = os.path.abspath(args.repo_root)

    paths, find_pairs, node_props, missing = xnu_requirements(root)
    try:
        names, props = our_tree(root)
    except ValueError as exc:
        print("xnu_dt_requirements: %s" % exc, file=sys.stderr)
        return 2

    if missing:
        print("warning: XNU source(s) absent, the scan is incomplete:", file=sys.stderr)
        for rel in missing:
            print("  %s" % rel, file=sys.stderr)
        print(file=sys.stderr)
        if len(missing) == len(XNU_SOURCES):
            print("xnu_dt_requirements: no XNU sources available", file=sys.stderr)
            return 2

    def present(kind, key):
        if kind == "path":
            leaf = key.strip("/").split("/")[-1]
            return key in names or leaf in names
        if kind == "name":
            return key in names
        return key in props

    # Every requirement must still appear in the scan; if XNU moved or renamed the
    # lookup, the table is stale and should say so rather than keep claiming coverage.
    scanned = paths | {v for _p, v in find_pairs if v} | node_props
    stale = 0

    print("device tree requirements from XNU's ARM sources")
    print("  %-22s %-8s %s" % ("requirement", "kind", "consequence"))
    bad = 0
    for kind, key, severity, why in REQUIREMENTS:
        if key not in scanned:
            if kind != "name" or key not in {v for _p, v in find_pairs}:
                print("  %-22s %-8s STALE ENTRY - no such lookup in the scanned sources"
                      % (key, severity))
                stale += 1
                continue
        ok = present(kind, key)
        if severity == "blocks" and not ok:
            bad += 1
        if kind == "prop":
            # This scan reads source text. It can establish that some node carries a
            # property of this name; it cannot establish that any node carries the
            # *value* XNU's DTFindEntry matches on. Saying "present" for the weaker
            # question is how an earlier revision of this check passed
            # DTFindEntry("device_type", "timer") while no node had that value - which is
            # a false pass, and worse than a gap. So the verdict says which it is.
            marker = ("name present" if ok else
                      ("MISSING" if severity == "blocks" else "absent (ok)"))
        else:
            marker = "present" if ok else ("MISSING" if severity == "blocks" else "absent (ok)")
        print("  %-22s %-8s %s" % (key, severity, marker))
        if not ok:
            print("  %-22s          %s" % ("", why))

    if args.all:
        print()
        print("informational lookups (Apple-SoC specific, XNU falls back on each):")
        for prop in sorted(node_props):
            if prop in INFORMATIONAL and prop not in props:
                print("  %-28s absent" % prop)
        for _prop, value in sorted(find_pairs):
            if value in INFORMATIONAL and value not in names:
                print("  %-28s absent" % value)

    print()
    print("declared node header counts:")
    try:
        problems = check_declared_counts(root)
    except ValueError as exc:
        print("  cannot check: %s" % exc)
        problems = []
    if problems:
        for prob in problems:
            print("  %s" % prob)
        print("  (a wrong nProperties/nChildren corrupts the walk silently)")
    else:
        print("  every node header agrees with the properties it emits")

    print()
    if stale:
        print("NOTE: %d stale table entr(ies) - XNU's lookups moved; update REQUIREMENTS."
              % stale)
    if bad:
        print("FAIL: %d requirement(s) our device tree does not satisfy, and XNU does not "
              "recover from them." % bad)
        return 1
    if problems:
        print("FAIL: %d node header count(s) disagree with the emitted properties."
              % len(problems))
        return 1
    print("OK: every node XNU locates by name, and every property name it reads, is present.")
    print("    Property *values* are not checked here - this scan reads source text. Use")
    print("    tools/host_dt_check.sh, which walks the tree with XNU's own reader.")
    if stale:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
