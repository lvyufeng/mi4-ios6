#!/usr/bin/env python3
"""
Replay XNU's own device-tree walk over the blob the payload builds, and name the first property
whose `length` cannot be a length.

    tools/xnu_dt_walk.py                       # walks out/apple_dt_host/apple_dt.bin
    tools/xnu_dt_walk.py --blob P              # a different blob
    tools/xnu_dt_walk.py --verbose             # per-node table
    tools/xnu_dt_walk.py --probe 0x8090cae0    # what XNU sees if `prop` points here
    tools/xnu_dt_walk.py --probe 0x8090cae0 --landings   # and is it reachable at all
    tools/xnu_dt_walk.py --fnv                 # the block the device's own replay prints

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


FNV_BASIS = 2166136261
FNV_PRIME = 16777619
MASK32 = 0xFFFFFFFF


def fnv1a(h, byte):
    return ((h ^ byte) * FNV_PRIME) & MASK32


def fnv_block(blob):
    """The numbers the entry image's `entry_dt_hash` prints, over the same range.

    Same width rule (`ceil(n / 8)`, last chunk ending exactly at `n`), same single pass, same FNV-1a
    32 - so the device's block and this one are comparable by diff rather than by reading a hex
    literal twice. That is the whole point of the flag: 442 left the question "are the bytes in
    memory the bytes in the file" between two runs, and a comparison of two printed blocks is what
    answers it.
    """
    n = len(blob)
    chunk_bytes = (n + 7) >> 3
    whole = FNV_BASIS
    chunks = [FNV_BASIS] * 8
    off = 0
    for c in range(8):
        end = min((c + 1) * chunk_bytes, n)
        while off < end:
            whole = fnv1a(whole, blob[off])
            chunks[c] = fnv1a(chunks[c], blob[off])
            off += 1
    return whole, chunks, chunk_bytes


def fnv_report(blob, nodes):
    """The device's own key block, so the comparison is `diff` and not an eye."""
    whole, chunks, chunk_bytes = fnv_block(blob)
    props = sum(nd.nprops for nd in nodes)
    print()
    print("the entry image's `fleh_undef` prints these; a device run that agrees with this block is a")
    print("run whose tree in memory is the blob, byte for byte:")
    print()
    print(f"  xnu_entry_dt_map_base=0x{0x80000000:08x}   (gVirtBase, the range the replay bounded "
          f"its reads by)")
    print(f"  xnu_entry_dt_replay_nodes=0x{len(nodes):08x}")
    print(f"  xnu_entry_dt_replay_props=0x{props:08x}")
    print(f"  xnu_entry_dt_replay_steps=0x{len(nodes) + props:08x}")
    print(f"  xnu_entry_dt_replay_end=0x{len(blob):08x}")
    print(f"  xnu_entry_dt_replay_stop=0x{0:08x}")
    print(f"  xnu_entry_dt_replay_stop_kind=0x{0:08x}")
    print(f"  xnu_entry_dt_checksum=0x{whole:08x}")
    print(f"  xnu_entry_dt_chunk_bytes=0x{chunk_bytes:08x}")
    for c in range(8):
        print(f"  xnu_entry_dt_chunk{c}=0x{chunks[c]:08x}")
    print()
    print(f"  (replay_steps is nodes + properties: {len(nodes)} + {props} = {len(nodes) + props},"
          f" which is what the step counter counts)")
    return whole


def read_order(blob, nodes):
    """Every property read the walk performs, in the order it performs them.

    The device's replay is depth-first pre-order - a node's properties, then its children - and the
    host's `walk()` appends a node to its list after finishing that node's property loop and before
    descending, so the list is in the same pre-order. Concatenating each node's properties in that
    order therefore reproduces the device's read sequence, which is what the ring is a window onto.
    """
    out = []
    for nd in nodes:
        for poff, name, length, _voff in node_props(blob, nd):
            out.append((poff, length))
    return out


TRACE_N = 16


def trace_report(blob, nodes):
    """The trace block `fleh_undef` prints, so the comparison is a `diff` and not an eye."""
    reads = read_order(blob, nodes)
    print()
    print("and the trace, which is what the totals cannot say - the same block the device prints:")
    print()
    print(f"  xnu_entry_dt_node_count=0x{len(nodes):08x}")
    for k in range(TRACE_N):
        if k < len(nodes):
            nd = nodes[k]
            off, props, child = nd.off, nd.nprops, nd.nchildren
        else:
            off = props = child = 0
        print(f"  xnu_entry_dt_node{k}_off=0x{off:08x}  "
              f"xnu_entry_dt_node{k}_props=0x{props:08x}  "
              f"xnu_entry_dt_node{k}_child=0x{child:08x}")
    print(f"  xnu_entry_dt_ring_count=0x{len(reads):08x}")
    last = reads[-TRACE_N:] if len(reads) >= TRACE_N else reads
    pad = [(0, 0)] * (TRACE_N - len(last)) + list(last)
    for k, (off, length) in enumerate(pad):
        print(f"  xnu_entry_dt_ring{k}_off=0x{off:08x}  xnu_entry_dt_ring{k}_len=0x{length:08x}")
    return reads


def node_registry_name(blob, node):
    """`MakeReferenceTable`'s name for this node: the `name` property, or nothing at all.

    iokit/Kernel/IODeviceTreeSupport.cpp:344-... builds one `IOService` per node, copies every
    property in, and calls `regEntry->setName(sym)` **only if the node has a `name` property**
    (`if nameKey == gIODTNameKey`, after the property loop's own copy). A node without one keeps the
    default name its class gives it, and `IORegistryEntry::getChildFromComponent` matches children by
    name - so a nameless node is a node no path can reach. Returns (name or None, terminated).
    """
    for _off, name, length, voff in node_props(blob, node):
        if name == "name":
            raw = blob[voff:voff + length]
            nul = raw.find(b"\0")
            return (raw[:nul] if nul >= 0 else raw).decode("latin-1"), nul >= 0
    return None, False


def alloc_plane(blob, nodes):
    """Replay `IODeviceTreeAlloc`'s stack loop and report where each node lands in the IODT plane.

    The C++ (iokit/Kernel/IODeviceTreeSupport.cpp:167-186) walks the blob with **one** iterator and an
    explicit stack of registry entries:

        parent = stack top; pop;
        while (DTIterateEntries gives dtChild) {
            child = ref(dtChild); child->attachToParent(parent, gIODTPlane);
            if (kSuccess == DTEnterEntry(dtChild)) { stack->setObject(parent); parent = child; }
        }
        while (stack nonempty && DTExitEntry succeeds);

    `DTEnterEntry` (pexpert/gen/device_tree.c:283-302) returns kSuccess for any non-NULL child, so
    **every** child is entered, leaves included: exactly one push per node, and the `attachToParent`
    above it is what fixes the parent. `DTExitEntry` (:305-322) restores the scope, its current entry
    and its index, one saved scope per outer iteration - so the stack holds exactly the ancestors of
    the iterator's current scope, and the model below is that loop literally, with each scope's own
    index carried through enter and exit.

    Returns (parent_of, names, pushed) where parent_of maps a node offset to the offset it was
    attached to (None for the tree root, which the loop attaches to the registry root at :204).
    """
    children = {n.off: [] for n in nodes}
    for n in nodes:
        if n.parent is not None:
            children[n.parent.off].append(n.off)

    root = nodes[0].off if nodes else None
    names = {n.off: node_registry_name(blob, n)[0] for n in nodes}
    parent_of = {root: None}
    pushes = 0

    stack = [root]      # the OSArray of registry entries: one per level descended into
    saved = []          # the iterator's DTSavedScope chain: the same levels, with their indices
    scope, index = root, 0
    while True:
        parent = stack.pop()
        while index < len(children[scope]):     # DTIterateEntries
            dtchild = children[scope][index]
            index += 1
            parent_of[dtchild] = parent         # attachToParent - the whole parent assignment
            # DTEnterEntry: kSuccess for any non-NULL child, so the push always happens, and a node
            # with no children is entered and exited in the same iteration.
            saved.append((scope, index))
            stack.append(parent)
            pushes += 1
            scope, index = dtchild, 0
            parent = dtchild
        # DTIterateEntries returned kIterationDone for this scope: the outer loop leaves it, and
        # DTExitEntry restores the scope it came from *with the index that scope had reached*.
        if not stack or not saved:
            break
        scope, index = saved.pop()

    return parent_of, names, pushes


def plane_resolve(blob, nodes, names, path):
    """`IORegistryEntry::fromPath(path, gIODTPlane)` over the replayed plane: the node it returns.

    The walk (iokit/Kernel/IORegistryEntry.cpp:1232-1325 plus `getChildFromComponent` at :1105-1150)
    starts at the registry root's child in the plane - the tree root the loop attached - then takes
    one component at a time and asks each entry for a child whose name is that component. First match
    wins, and the match is exact: `getChildFromComponent` compares the child's name for its whole
    length and then requires the next character to be end-of-component, `/` or `:`, or an `@`
    location suffix. Returns (node, where-it-stopped, the-component-that-failed).
    """
    return _resolve(nodes, names, path)


def blob_resolve(blob, nodes, names, path):
    """`DTLookupEntry`'s own answer for the same path: `FindChild`, which also matches by `name`."""
    return _resolve(nodes, names, path)[0]


def _resolve(nodes, names, path):
    children = {n.off: [] for n in nodes}
    for n in nodes:
        if n.parent is not None:
            children[n.parent.off].append(n.off)

    cur = nodes[0].off if nodes else None
    for comp in path.strip("/").split("/"):
        if not comp:
            continue
        found = None
        for off in children[cur]:
            nm = names.get(off)
            if nm == comp or (nm and nm.startswith(comp + "@")):
                found = off
                break
        if found is None:
            return None, cur, comp
        cur = found
    return cur, None, None


# The paths the boot resolves **in the DT plane**, taken from the source rather than invented, and
# split by whether our tree is supposed to answer them. `fromPath(path, gIODTPlane)` and
# `childFromPath(component, gIODTPlane)` are the two call shapes; the sites:
#
#   /chosen            IOPlatformExpert.cpp:1125 (createNub's provider), :1419, IONVRAM.cpp:90,
#                      IOStartIOKit.cpp:200 (`IORecordProgressBackbuffer`, with the plane named in
#                      the path), and - the one 459 needs - IOFindBSDRoot's own first lookup
#   /chosen/memory-map IOFindBSDRoot, iokit/bsddev/IOKitBSDInit.cpp:431 - the RAMDisk property
#   /cpus              IOPlatformExpert.cpp:1347 (`childFromPath`), the topology walk
#   /arm-io            the timer and interrupt nubs' provider
#   /device-tree       pe_identify_machine's target-type and model
#   /memory            the physical memory the platform expert hands out
PLANE_PATHS = [
    "/chosen",
    "/chosen/memory-map",
    "/cpus",
    "/arm-io",
    "/device-tree",
    "/memory",
]

# XNU asks for these and our tree deliberately does not provide them; a failure here is a report,
# not a defect. Kept in the table because "the plane does not answer this either" is a different
# statement from "the plane answers what it should".
PLANE_PATHS_ABSENT_BY_DESIGN = [
    "/options",
    "/efi/platform",
]


def plane_report(blob, nodes):
    """Is the IODT plane the blob's own tree, and can the paths the boot uses be resolved in it?

    This is the host-side half of 459's open question. The run showed `IOFindBSDRoot` reaching the
    `rd=md0` branch and panicking because `mdevlookup` found no device, which means
    `IORegistryEntry::fromPath("/chosen/memory-map", gIODTPlane)` returned NULL over a blob whose
    `/chosen/memory-map` the reader finds. So either the plane's *shape* differs from the blob's (a
    parent assigned elsewhere, or a node the plane's name matching cannot select), or the plane is not
    there at all (`gIODTPlane` NULL, which is one read of one symbol in the image and is not this
    tool's question). This tool answers the first.
    """
    status = 0
    parent_of, names, pushes = alloc_plane(blob, nodes)
    print()
    print("the IODT plane IODeviceTreeAlloc builds, node by node:")
    print(f"  nodes {len(nodes)}, attachToParent calls {len(nodes) - 1} "
          f"(every node but the root), DTEnterEntry pushes {pushes}")

    wrong_parent = []
    nameless = []
    for n in nodes:
        want = n.parent.off if n.parent is not None else None
        got = parent_of.get(n.off)
        if want != got:
            wrong_parent.append((n.off, want, got))
        if names.get(n.off) is None:
            nameless.append(n.off)

    if wrong_parent:
        status = 1
        print(f"  MISMATCH: {len(wrong_parent)} node(s) are attached to a parent the blob does not "
              f"give them:")
        for off, want, got in wrong_parent[:10]:
            print(f"    0x{off:06x}: blob parent 0x{(want or 0):06x}, plane parent "
                  f"0x{(got or 0):06x}")
    else:
        print("  ok        every node is attached to the parent the blob's own walk gives it, so "
              "the plane's shape is the blob's shape")

    if nameless:
        status = 1
        print(f"  NOTE: {len(nameless)} node(s) have no `name` property, so MakeReferenceTable never "
              f"names them and no path can select them:")
        for off in nameless[:10]:
            print(f"    0x{off:06x}")
    else:
        print("  ok        every node carries a `name`, so every node is reachable by path")

    print()
    print("the paths the boot resolves in that plane:")
    for path in PLANE_PATHS + PLANE_PATHS_ABSENT_BY_DESIGN:
        by_design = path in PLANE_PATHS_ABSENT_BY_DESIGN
        plane_off, _at, missing = plane_resolve(blob, nodes, names, path)
        blob_off = blob_resolve(blob, nodes, names, path)
        if plane_off is None:
            if by_design:
                print(f"  absent*   {path:24} our tree has no '{missing}' and the boot does not "
                      f"need it - XNU asks, the plane says no, and that is by design")
            else:
                status = 1
                print(f"  FAIL      {path:24} the plane has no such path (no child '{missing}')")
        elif blob_off is None:
            status = 1
            print(f"  odd       {path:24} the plane resolves it to 0x{plane_off:06x}, the blob's "
                  f"own DTLookupEntry does not")
        else:
            print(f"  ok        {path:24} plane 0x{plane_off:06x}, DTLookupEntry 0x{blob_off:06x}"
                  f"{'' if plane_off == blob_off else '   <-- DIFFERENT NODES'}")
            if plane_off != blob_off:
                status = 1

    print()
    if status == 0:
        print("RESULT: the plane is the blob's tree with the blob's names, so a lookup that fails "
              "in the plane and succeeds in the blob is not a shape difference - the plane is the "
              "runtime's to explain (gIODTPlane itself, or the values in it).")
    else:
        print("RESULT: the plane does NOT reproduce the blob's tree - the failure above is "
              "host-side and is the frontier.")
    return status


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
    ap.add_argument("--fnv", action="store_true",
                    help="print the key block the entry image's own replay and hash produce, over "
                         "the blob - comparing the two blocks is the comparison")
    ap.add_argument("--plane", action="store_true",
                    help="replay IODeviceTreeAlloc's stack loop over the blob and report the IODT "
                         "plane it builds, and whether the paths the boot resolves in it resolve")
    args = ap.parse_args()

    if not os.path.isfile(args.blob):
        sys.exit(f"no {args.blob} - run tools/apple_dt_host_dump.sh first")

    # Is the blob current? This tool reads a *generated* file, and the generator's own inputs are the
    # payload's builder, the tree format and the generated entry header whose two RAM-disk words the
    # builder now writes into the tree. Experiment 459 walked a blob from *before* its own edit and got
    # a confident, self-consistent, wrong tree out of it - `/chosen` with no `memory-map` child - which
    # is the shape of the defect the step was hunting. A stale input is not a measurement of anything,
    # so this refuses rather than printing a table.
    blob_mtime = os.path.getmtime(args.blob)
    for src in ("stages/stage90/stage90_main.c", "stages/stage90/apple_dt.c",
                "out/stage90/xnu_arm_entry.h"):
        path = os.path.join(REPO_ROOT, src)
        if os.path.isfile(path) and os.path.getmtime(path) > blob_mtime:
            sys.exit(f"{os.path.relpath(args.blob, REPO_ROOT)} is older than {src} - the tree it "
                     f"holds is from before that file changed. Run tools/apple_dt_host_dump.sh to "
                     f"rebuild it; a walk over a stale blob answers a question about an older tree.")

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

    if args.fnv:
        fnv_report(blob, nodes)
        trace_report(blob, nodes)

    if args.plane:
        status |= plane_report(blob, nodes)

    for off in probe_offs:
        status |= probe(blob, nodes, off, args.verbose)

    return status


if __name__ == "__main__":
    sys.exit(main())
