#!/usr/bin/env python3
"""
Replay XNU's own device-tree walk over the blob the payload builds, and name the first property
whose `length` cannot be a length.

    tools/xnu_dt_walk.py                       # walks out/apple_dt_host/apple_dt.bin
    tools/xnu_dt_walk.py --blob P              # a different blob
    tools/xnu_dt_walk.py --verbose             # per-node table
    tools/xnu_dt_walk.py --probe 0x8090cae0    # what XNU sees if `prop` points here
    tools/xnu_dt_walk.py --probe 0x8090cae0 --landings   # and is it reachable at all

Why this exists. Experiment 440's hardware run ended in a panic, and the message out of the trap's
`r9` resolved it to exactly one source line:

    pexpert/gen/device_tree.c:56
    panic("Device tree property overflow: prop %p, length 0x%x\\n", prop, prop->length);

inside `next_prop()`:

    next = (prop + prop->length + sizeof(DeviceTreeNodeProperty) + 3) & ~3;

`sizeof(DeviceTreeNodeProperty)` is 36 (`char name[32]` + `uint32_t length`; `kPropNameLength` is 32
at `pexpert/pexpert/device_tree.h:70`). So the rule is `next = prop + 36 + align4(length)` **for a
4-aligned `prop`**, and it can only panic when `prop->length` is enormous - which means the walk read
a *property header* somewhere there is not one. The walk drifted, and the thing that makes it drift
is a node whose `nProperties` does not match the properties actually written under it.

`DTIterateProperties` is the reader, and it is reached from exactly one place in the whole image
(`MakeReferenceTable+0x98`, via `IODeviceTreeAlloc` and `IOPlatformExpertDevice::initWithArgs`), on
the IOKit registry build. That is the first **exhaustive** property walk this tree has ever had:
`pe_identify_machine`'s and `PE_get_default`'s walks are targeted lookups, and the payload's own
`apple_dt_selftest_and_log` walks `skip_node()` over the whole tree but **never compares a node's
declared `nProperties` against the properties written under it** - it only checks that the total walk
lands on `end`, which a compensating pair of miscounts can satisfy.

What this tool does, and the three things it refuses to assume:

  1. It walks with **XNU's formula**, not the payload's, so a disagreement between the two writers is
     visible rather than averaged away. Where they differ, both positions are printed.
  2. It walks into **every** node, including grandchildren - `MakeReferenceTable` is called for the
     root and for every entry of every level, so a node at any depth is a candidate.
  3. It reports the **drift** case (a node whose property walk does not land on its first child) as a
     distinct finding from the **overflow** case (a `length` that is enormous). The first is the
     cause; the second is the panic. A tool that only reported the panic would name the symptom.

The blob is built by `tools/apple_dt_host_dump.sh`, which compiles the **real** `build_stage90_apple_dt`
and the real `apple_dt.c` for the host - so the tree here is the tree the device builds, not a
re-implementation of it.
"""

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
DEFAULT_BLOB = os.path.join(REPO_ROOT, "out", "apple_dt_host", "apple_dt.bin")

# `pexpert/pexpert/device_tree.h:70-77`. The sizeof is asserted below rather than computed, because
# this whole tool is about a header whose layout decides where the next header is.
PROP_NAME_LEN = 32
NODE_HDR = 8
PROP_HDR = PROP_NAME_LEN + 4
assert PROP_HDR == 36, PROP_HDR

U32 = 0xFFFFFFFF


def align4(v):
    return (v + 3) & ~3


def read_u32(blob, off):
    return struct.unpack_from("<I", blob, off)[0]


def prop_name(blob, off):
    raw = blob[off:off + PROP_NAME_LEN]
    nul = raw.find(b"\0")
    if nul < 0:
        return None, raw
    return raw[:nul].decode("latin-1"), raw


class Node:
    __slots__ = ("off", "nprops", "nchildren", "extent", "parent", "depth")

    def __init__(self, off, nprops, nchildren, parent, depth):
        self.off = off
        self.nprops = nprops
        self.nchildren = nchildren
        self.extent = None
        self.parent = parent
        self.depth = depth

    @property
    def hdr_end(self):
        return self.off + NODE_HDR


def walk(blob):
    """XNU's walk from the root: [(Node, findings)] plus the total length it consumed.

    The property advance is `next_prop`'s, and the overflow test is XNU's `os_add3_overflow` on
    32-bit `uintptr_t`. A node's extent is where the property loop ends - which is *the thing under
    test*, so it is reported and never used to bound the loop.
    """
    findings = []
    nodes = []

    def do(off, parent, depth, limit):
        if off + NODE_HDR > limit:
            findings.append((off, "node header past the end of the blob", None))
            return None, off
        nprops = read_u32(blob, off)
        nchildren = read_u32(blob, off + 4)
        node = Node(off, nprops, nchildren, parent, depth)
        p = off + NODE_HDR
        # XNU's DTInitPropertyIterator sets currentProperty = entry + 1 for the FIRST property and
        # only calls next_prop from the second on, so a node with nProperties <= 1 never calls it.
        for i in range(nprops):
            if p + PROP_HDR > limit:
                findings.append((off, f"property {i}: header at 0x{p:x} runs past the blob end "
                                      f"(0x{limit:x}) - nProperties={nprops} claims more than the "
                                      f"node holds", i))
                return node, p
            name, raw = prop_name(blob, p)
            length = read_u32(blob, p + PROP_NAME_LEN)
            if i > 0:
                # next_prop's own arithmetic, in 32 bits, exactly as os_add3_overflow sees it.
                total = (p + length) & U32
                total = (total + PROP_HDR + 3) & U32
                if total < p:
                    findings.append((off, f"property {i} ('{name}') at 0x{p:x}: length 0x{length:x} "
                                          f"overflows next_prop's addition - XNU panics HERE",
                                     i))
                    return node, p
            p = p + PROP_HDR + align4(length)
            if length > limit - (p - align4(length) - PROP_HDR) or p > limit:
                findings.append((off, f"property {i} ('{name}') at 0x{p - PROP_HDR - align4(length):x}: "
                                      f"length 0x{length:x} walks past the blob end", i))
                return node, p
        node.extent = p
        nodes.append(node)
        c = p
        for k in range(nchildren):
            child, c = do(c, node, depth + 1, limit)
            if child is None:
                return node, c
        return node, c

    root, end = do(0, None, 0, len(blob))
    return nodes, findings, end


def payload_walk(blob):
    """The payload's own `skip_node`, for the comparison. Returns None on its own failure."""
    def do(p, end):
        if p + NODE_HDR > end:
            return None
        nprops = read_u32(blob, p)
        nchildren = read_u32(blob, p + 4)
        p += NODE_HDR
        for _ in range(nprops):
            if p + PROP_HDR > end:
                return None
            length = read_u32(blob, p + PROP_NAME_LEN)
            p += PROP_HDR
            if p + length > end:
                return None
            p += align4(length)
        for _ in range(nchildren):
            p = do(p, end)
            if p is None:
                return None
        return p

    return do(0, len(blob))


def node_props(blob, node):
    """[(header_offset, name, length, value_offset)] for one node, by XNU's own formula."""
    out = []
    p = node.hdr_end
    for i in range(node.nprops):
        name, _raw = prop_name(blob, p)
        length = read_u32(blob, p + PROP_NAME_LEN)
        out.append((p, name, length, p + PROP_HDR))
        p += PROP_HDR + align4(length)
    return out


def tree_pa(repo_root):
    """Where `fastboot`'s payload copies the tree to, derived from the entry build's own header.

    The device's `prop` pointer is a physical address, because `xnu_entry_jump.c` sets
    `physBase == virtBase` - so the offset into the blob is `prop - ENTRY_DT_PA`, and
    `ENTRY_DT_PA` is `STAGE90_XNU_ENTRY_BASE + STAGE90_XNU_ENTRY_DT_OFFSET` in the header
    `xnu_arm_boot/build_entry.sh` writes. Reading it there rather than writing the number here is
    the point: it moves whenever the image does.
    """
    path = os.path.join(repo_root, "out", "stage90", "xnu_arm_entry.h")
    if not os.path.isfile(path):
        return None
    vals = {}
    for line in open(path):
        parts = line.split()
        if len(parts) == 3 and parts[0] == "#define" and parts[1].startswith("STAGE90_XNU_ENTRY"):
            try:
                vals[parts[1]] = int(parts[2], 0)
            except ValueError:
                pass
    if "STAGE90_XNU_ENTRY_BASE" not in vals or "STAGE90_XNU_ENTRY_DT_OFFSET" not in vals:
        return None
    return vals["STAGE90_XNU_ENTRY_BASE"] + vals["STAGE90_XNU_ENTRY_DT_OFFSET"]


def probe(blob, nodes, off, verbose):
    """What XNU's walk sees if `prop` points here. Returns the number of findings.

    The question this answers is the one the device's `panic` asks and cannot answer: `next_prop`
    panics because it read a *property header* somewhere there is not one, so the useful report is
    not "offset X" but "offset X is property 3 of /arm-io, named `reg`, of length 0x10" or "offset X
    is 4 bytes into the *value* of that property" or "offset X is not inside any node this walk
    reaches".

    And it always answers the second half too, which is the half that matters: if `prop` pointed
    here, **what would `prop->length` read as**. That value is what `next_prop` adds to the pointer,
    so a name-shaped header with a plausible length is a walk that survived, and a stretch of value
    bytes read as a header is the enormous length - reported here as what it is.
    """
    n = len(blob)
    print()
    if off > n:
        print(f"probe 0x{off:x} is past the end of the blob (0x{n:x}) - XNU would be reading "
              f"memory that is not this tree")
        return 1

    here = [(nd, rec) for nd in nodes for rec in node_props(blob, nd)]
    verdict = None
    for nd, (poff, name, length, voff) in here:
        if off == poff:
            verdict = (f"the property HEADER of node 0x{nd.off:x} (depth {nd.depth}), property "
                       f"'{name}', declared length 0x{length:x}")
            break
        if voff <= off < voff + length:
            verdict = (f"{off - voff} byte(s) into the VALUE of property '{name}' of node "
                       f"0x{nd.off:x} (depth {nd.depth}) - a walk here has drifted off the "
                       f"property list")
            break
    if verdict is None:
        for nd in nodes:
            if off == nd.off:
                verdict = (f"the NODE HEADER of node 0x{nd.off:x} (depth {nd.depth}, "
                           f"nProperties {nd.nprops}, nChildren {nd.nchildren}) - a node header is "
                           f"not a property header")
                break
    if verdict is None:
        verdict = "not the start of any node or property this walk reaches"

    print(f"probe 0x{off:x} is {verdict}")

    # What `next_prop` would compute if handed this address. This is the panic, or its absence.
    if off + PROP_HDR > n:
        print(f"  as a property header: the 36 bytes run past the blob end "
              f"({n - off} bytes left) - XNU cannot read a header here")
        return 1
    name, raw = prop_name(blob, off)
    length = read_u32(blob, off + PROP_NAME_LEN)
    printable = name is not None and name and all(0x20 <= ord(c) < 0x7f for c in name)
    total = off + PROP_HDR + align4(length)
    if printable:
        shown = "`" + name + "`"
    else:
        shown = "(not a printable C string: " + " ".join("%02x" % b for b in raw[:12]) + " ...)"
    print(f"  as a property header: name {shown}, length 0x{length:x}")
    past = f" - PAST the blob end (0x{n:x}), so this walk leaves the tree" if total > n else ""
    print(f"  next_prop would land at 0x{total:x}{past}")
    if verbose:
        print(f"  the 36 header bytes:")
        for row in range(0, PROP_HDR, 16):
            chunk = blob[off + row:off + row + 16]
            print(f"    +{row:02x}  {' '.join('%02x' % b for b in chunk)}")
    return 1 if (not printable or total > n) else 0


def landings(blob, nodes, probe_off=None):
    """Where `next_prop` can put the pointer, if the walk jumps off the list.

    `next_prop` is called once per property from the second on, and the panic happens inside it, so
    the address the device reports is one of its *outputs* - either a landing it returned and a later
    iteration was handed, or the value it was in the middle of computing when `os_add3_overflow`
    fired. Both are `prop + 36 + align4(prop->length)` for some `prop` on the list. So the question
    "could a walk over this blob reach the device's address at all" is exactly the question "is that
    address in this set", and this prints the set.

    A landing is *expected* to be a node header, a property header, or the end of the tree: a node's
    property list is followed by its first child (a node) or by its sibling's next node, and the last
    property of the whole tree is followed by nothing. A landing that is none of those is a property
    whose `length` sent the walk somewhere the tree does not have a boundary - the drift, located.
    """
    n = len(blob)
    node_offs = {nd.off for nd in nodes}
    prop_offs = set()
    for nd in nodes:
        for poff, _name, _length, _voff in node_props(blob, nd):
            prop_offs.add(poff)

    sites = []
    for nd in nodes:
        recs = node_props(blob, nd)
        for i in range(1, len(recs)):
            poff, name, length, _voff = recs[i]
            landing = poff + PROP_HDR + align4(length)
            if landing in node_offs:
                kind = "node"
            elif landing in prop_offs:
                kind = "property"
            elif landing == n:
                kind = "end"
            elif landing > n:
                kind = "PAST THE END"
            else:
                kind = "NEITHER"
            sites.append((nd.off, i, name, poff, length, landing, kind))

    distinct = sorted({s[5] for s in sites})
    bad = [s for s in sites if s[6] not in ("node", "property", "end")]
    print()
    print(f"next_prop landings: {len(sites)} over {len(nodes)} nodes "
          f"({sum(nd.nprops for nd in nodes)} properties; the first property of each node never "
          f"calls next_prop)")
    print(f"  distinct landing addresses: {len(distinct)}")
    print(f"  landings that are not a node header, a property header, or the tree end: {len(bad)}")
    for s in bad:
        print(f"    0x{s[5]:06x}  from 0x{s[3]:06x} ('{s[2]}' of node 0x{s[0]:06x}) "
              f"length 0x{s[4]:x} - {s[6]}")
    print(f"  the highest landing: 0x{distinct[-1]:x}; the tree ends at 0x{n:x}")

    if probe_off is not None:
        hits = [s for s in sites if s[5] == probe_off]
        print()
        if hits:
            print(f"probe 0x{probe_off:x} IS a landing:")
            for s in hits:
                print(f"  from property {s[1]} ('{s[2]}') of node 0x{s[0]:06x} at 0x{s[3]:06x}, "
                      f"length 0x{s[4]:x} - {s[6]}")
        else:
            print(f"probe 0x{probe_off:x} is NOT any next_prop landing over this blob - no walk "
                  f"that starts at the root of these bytes and follows next_prop arrives there, "
                  f"so the walk that died was working on a different set of bytes or was handed a "
                  f"different pointer")
        return 0 if hits else 1
    return 0 if not bad else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--blob", default=DEFAULT_BLOB)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--probe", action="append", default=[],
                    help="an absolute address (or a bare 0x offset with --probe-offset) to "
                         "explain; repeatable")
    ap.add_argument("--probe-offset", action="store_true",
                    help="treat --probe values as offsets into the blob, not device addresses")
    ap.add_argument("--landings", action="store_true",
                    help="print every address next_prop can land on, and whether any is not a "
                         "node/property boundary")
    args = ap.parse_args()

    if not os.path.isfile(args.blob):
        sys.exit(f"no {args.blob} - run tools/apple_dt_host_dump.sh first")
    blob = open(args.blob, "rb").read()
    print(f"blob {os.path.relpath(args.blob, REPO_ROOT)}  {len(blob)} bytes "
          f"(0x{len(blob):x})")

    nodes, findings, end = walk(blob)
    pw = payload_walk(blob)

    # The two walks agree about the total or the tree is being read two ways, which is worth saying
    # before anything else.
    print(f"XNU's walk ends at 0x{end:x} of 0x{len(blob):x}; "
          f"the payload's skip_node ends at "
          f"{'0x%x' % pw if pw is not None else 'FAILURE'}; "
          f"{'agree' if pw == end else 'DISAGREE'}")

    # Every node's declared count against the properties actually written under it. The independent
    # signal is the *name*: a property header that is really a value or a child header has a name
    # that is not a printable C string, so counting the printable ones catches a count that is too
    # large even when the arithmetic happens to survive.
    bad_counts = []
    for n in nodes:
        p = n.hdr_end
        printable = 0
        for i in range(n.nprops):
            name, raw = prop_name(blob, p)
            if name is not None and name and all(0x20 <= ord(c) < 0x7f for c in name):
                printable += 1
            p += PROP_HDR + align4(read_u32(blob, p + PROP_NAME_LEN))
        if printable != n.nprops:
            bad_counts.append((n, printable))

    print(f"nodes walked: {len(nodes)}; "
          f"nodes whose property headers are not all plausible names: {len(bad_counts)}")

    if args.verbose:
        print()
        print(f"{'off':>8}  {'depth':>5}  {'nprops':>6}  {'named':>5}  {'nchildren':>9}  extent")
        for n in nodes:
            p = n.hdr_end
            printable = 0
            for _ in range(n.nprops):
                name, _raw = prop_name(blob, p)
                if name and all(0x20 <= ord(c) < 0x7f for c in name):
                    printable += 1
                p += PROP_HDR + align4(read_u32(blob, p + PROP_NAME_LEN))
            nm = ""
            for i in range(n.nprops):
                q = n.hdr_end
                break
            print(f"0x{n.off:06x}  {n.depth:>5}  {n.nprops:>6}  {printable:>5}  {n.nchildren:>9}  "
                  f"0x{n.extent:06x}" if n.extent is not None else
                  f"0x{n.off:06x}  {n.depth:>5}  {n.nprops:>6}  {printable:>5}  {n.nchildren:>9}  -")

    if findings:
        print()
        print(f"{len(findings)} finding(s), in walk order - the FIRST one is where XNU stops:")
        for off, msg, idx in findings:
            print(f"  0x{off:06x}  {msg}")
        status = 1
    elif bad_counts:
        print()
        print("no overflow, but these nodes declare a property count their own bytes do not support:")
        for n, printable in bad_counts:
            print(f"  0x{n.off:x} depth {n.depth}: declares {n.nprops}, {printable} of them are "
                  f"readable property headers")
        status = 1
    else:
        print()
        print("ok: XNU's walk completes over every node with no property length that cannot be one, "
              "and every node's declared nProperties matches the property headers under it")
        status = 0

    # The probes are a different question from the walk, so they run whatever the walk said: a tree
    # that walks clean and a `prop` the device reported are two facts, and the interesting case is
    # exactly the one where they disagree.
    probe_offs = []
    if args.probe:
        by_offset = args.probe_offset
        pa = None
        if not by_offset:
            pa = tree_pa(REPO_ROOT)
            if pa is None:
                sys.exit("no out/stage90/xnu_arm_entry.h to take the tree's physical address from - "
                         "run stages/stage90/xnu_arm_boot/build_entry.sh first, or pass "
                         "--probe-offset")
            print()
            print(f"the device's tree is copied to PA 0x{pa:x} "
                  f"(STAGE90_XNU_ENTRY_BASE + STAGE90_XNU_ENTRY_DT_OFFSET, out/stage90/xnu_arm_entry.h)")
        for v in args.probe:
            off = int(v, 0)
            if pa is not None:
                if off < pa:
                    print()
                    print(f"probe 0x{off:x} is below the tree's base 0x{pa:x} - it is not inside "
                          f"the tree at all")
                    status = 1
                    continue
                off = off - pa
            probe_offs.append(off)

    if args.landings:
        status |= landings(blob, nodes, probe_offs[0] if probe_offs else None)

    for off in probe_offs:
        status |= probe(blob, nodes, off, args.verbose)

    return status


if __name__ == "__main__":
    sys.exit(main())
