#!/usr/bin/env python3
"""Where does an output section move when inputs are added to, or taken out of, a linker map?

    ./predict_layout.py --map out/stage90/xnu_arm_entry.map --section .text \
        --insert out/xnu_platform_obj/MSM8974PlatformExpert.o --after osfmk_kern_sched_average.o
    ./predict_layout.py --map out/stage90/xnu_arm_entry.map --section .text --remove osfmk_kern_sched_average.o

The map is a record of what `ld` did: an ordered list of input sections, each with the address it was
placed at and its size. `ld` places input i at `align_up(cursor, alignment_i)` and moves the cursor on
by its size, so the effect of adding or removing bytes is *not* a translation: every later input is
re-aligned to its own alignment, and each of those alignments either absorbs part of the change or
turns it into fill.

That is the whole model, and it needs no knowledge of the linker's other behaviour - merging
(`.rodata.str1.1` is `SHF_MERGE`, and its *linked* size is a deduplicated contribution, not the
object's size) does not enter it, because merging only affects the sizes, which are read from the map
rather than from the objects. The one thing it does need from outside the map is each input's
alignment, which `objdump -h` prints and the map does not.

    shift_i   = new_i - old_i                (how far this input moved)
    new_i     = align_up(old_i + shift_in, align_i)
    shift_out = new_i + size_new_i - old_i - size_old_i

Validation, which the tool does not do itself: take the map of a step and *remove* the object that
step linked. The result must be the previous step's map, row for row. For 362 -> 361 that means
removing `osfmk_kern_sched_average.o` and getting `realstubs.o`'s `.text` back at 0x80179BD4 and
`__entry_text_end` at 0x801A3280, which is what experiment 361 measured.

What it is worth, measured (experiment 363, which used it for every row of its prediction): of the
sixteen rows, every section boundary below `.text` came out exact -- `.data` 0x801A4000, `.sysctl_set`
0x801BDA58, `.init_array` 0x801BDBA8, `__bss_end` 0x801F6D58, and the two new objects' `.text`, `.bss`
and `.init_array` placements to the byte -- while the `.rodata` run came out **8 low** (three rows:
the class's `.rodata`, its `.rodata.str1.1` and `realstubs.o`'s `.rodata.str1.4`) and the raw end of
`.text` **0x10 high**. Both are the fill term this model does not carry: it re-aligns each input
against the *previous* input's end, and where a run contains a mergeable section whose linked size is
a deduplicated contribution rather than the object's size, the replay drifts. So read a `.rodata`-run
row as +/- 0x10 and a `.text` end as +/- 0x10, and prefer the rows it gets exactly (every `.text`
placement, and every section start) as the ones to predict a build's success on.
"""

import argparse
import re
import subprocess
import sys
from functools import lru_cache

OBJDUMP = "arm-none-eabi-objdump"
# Input entries come in two shapes: `name addr size path` on one line, or the name on its own line
# followed by `addr size path`. Both start with at least one space; an output section's own header
# line starts in column 0.
ONELINE_RE = re.compile(r"^\s+(\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)\s*$")
TWOLINE_RE = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)\s*$")
HEADER_RE = re.compile(r"^(\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s*$")


def align_up(v, a):
    return (v + a - 1) // a * a


@lru_cache(maxsize=None)
def section_align(path, name):
    """The alignment of one section of one object, from `objdump -h`'s last column."""
    if not path.startswith("/"):
        return 4                                   # `linker stubs` and friends
    try:
        out = subprocess.run([OBJDUMP, "-h", path], capture_output=True, text=True).stdout
    except OSError:
        return 4
    for line in out.splitlines():
        # `Idx Name Size VMA LMA File-off Algn`, with the name possibly containing a space
        # (`__TEXT, initcode` is one section name, not two words) - so the name is what sits between
        # the index and the five trailing columns.
        f = line.split()
        if len(f) < 7 or not f[0].isdigit():
            continue
        m = type("M", (), {"group": lambda self, i, f=f: (" ".join(f[1:-5]) if i == 1 else f[-1])})()
        if m.group(1) == name:
            a = m.group(2)
            if a.startswith("2**"):
                return 1 << int(a[3:])
            return int(a, 16) or 1
    return 4


def read_section(map_path, want):
    lines = open(map_path).read().splitlines()
    start = None
    order = []
    name = None
    inside = False
    for line in lines:
        h = HEADER_RE.match(line)
        if h and h.group(1) == want:
            start = int(h.group(2), 16)
            inside = True
            continue
        if not inside:
            continue
        if HEADER_RE.match(line):
            break                                   # the next output section
        if line.strip() == "" or line.strip().startswith("*fill*") \
           or line.lstrip().startswith("*("):
            continue
        one = ONELINE_RE.match(line)
        if one:
            order.append([one.group(1), one.group(4), int(one.group(2), 16), int(one.group(3), 16)])
            name = one.group(1)
            continue
        two = TWOLINE_RE.match(line)
        if two and not re.match(r"^\s+0x[0-9a-f]+\s+\S+$", line):
            if name is None:
                continue
            order.append([name, two.group(3), int(two.group(1), 16), int(two.group(2), 16)])
            continue
        if re.match(r"^ [^ ]", line):
            name = line.strip()
    if start is None:
        sys.exit("no output section %s in %s" % (want, map_path))
    return start, order


def read_load_order(map_path):
    """The command line, as `ld` recorded it: `LOAD <file>` lines appear in load order.

    This is what an insertion's *position within a pattern run* needs. The map's `.text` run is in
    command-line order and so is its `.rodata` run, but the two runs do not line up with each other:
    the last `.rodata` entry before a `.text`-run anchor is not "the anchor's neighbour in the
    command line", it is the neighbour of whatever object happens to precede the anchor *in that run*.
    The LOAD lines say directly which object follows which.
    """
    order = []
    for line in open(map_path):
        if line.startswith("LOAD "):
            order.append(line[5:].strip())
    return order


def sizes_of(obj):
    """(section name, size) for an object, in section-header order, minus the non-allocated ones."""
    out = subprocess.run([OBJDUMP, "-h", obj], capture_output=True, text=True).stdout
    got = []
    for line in out.splitlines():
        f = line.split()
        if len(f) < 7 or not f[0].isdigit() or not re.match(r"^[0-9a-f]+$", f[-5]):
            continue
        name, size = " ".join(f[1:-5]), int(f[-5], 16)
        if size == 0 or name.startswith((".comment", ".note", ".ARM.attributes", ".group")):
            continue
        got.append((name, size))
    return got


def apply_changes(start, order, removed_flag, changes):
    """Walk the order once, carrying the shift.

    `removed_flag[i]` says input i is not placed. `changes` maps an index to inputs inserted *before*
    that entry (a list of (name, path, size)).

    `old_cursor` is where the original cursor was, which is what an inserted input's alignment has to
    be taken against: an insertion does not have an old address of its own.
    """
    by_index = {}
    for idx, entries in changes:
        by_index.setdefault(idx, []).extend(entries)

    old_cursor = start
    shift = 0
    out = []
    for i in range(len(order) + 1):
        for name, path, size in by_index.get(i, []):
            al = section_align(path, name)
            addr = align_up(old_cursor + shift, al)
            out.append([name, path, addr, size, True])
            shift = addr + size - old_cursor
        if i == len(order):
            break
        name, path, old_addr, size = order[i]
        if removed_flag[i]:
            shift -= size                      # the bytes are not placed; nothing is aligned to them
            old_cursor = old_addr + size
            continue
        al = section_align(path, name)
        addr = align_up(old_addr + shift, al)
        out.append([name, path, addr, size, False])
        shift = addr - old_addr
        old_cursor = old_addr + size
    return out


def belongs(section, name):
    """Does an input section of this name land in this output section?"""
    if section == ".text":
        return name.startswith(".text") or name.startswith(".rodata") or name.startswith("__TEXT")
    return name.startswith(section)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", required=True)
    ap.add_argument("--section", required=True)
    ap.add_argument("--insert", action="append", default=[],
                    metavar="OBJ[:AFTER]",
                    help="add this object's inputs; AFTER names the object whose last input they "
                         "follow (default: the end of the section)")
    ap.add_argument("--replace", action="append", default=[], metavar="OLD:NEW",
                    help="take OLD's inputs out and put NEW's in at the same place")
    ap.add_argument("--remove", action="append", default=[], metavar="OBJ")
    ap.add_argument("--show", type=int, default=8, help="how many changed rows to print")
    args = ap.parse_args()

    load_order = read_load_order(args.map)
    load_pos = {}
    for i, pth in enumerate(load_order):
        load_pos.setdefault(pth, i)
    start, order = read_section(args.map, args.section)
    removed_flag = [False] * len(order)
    changes = []

    for spec in args.remove:
        hit = [i for i, e in enumerate(order) if spec in e[1]]
        if not hit:
            sys.exit("nothing in %s comes from %s" % (args.section, spec))
        for i in hit:
            removed_flag[i] = True

    def class_of(name):
        """The pattern run an input belongs to inside this output section.

        `ld` folds an object's inputs into the output section *by pattern*: every `.text*` input
        first, in command-line order, then every `.rodata*` input, in the same order. So an object
        inserted between two others does not put its inputs in one place - its `.text` goes into the
        `.text` run, its `.rodata` into the `.rodata` run, and the two positions are different.
        Placing them all at one index is what the first version of this did, and it put a 0x440-byte
        `.rodata` in the middle of the `.text` run.
        """
        if args.section == ".text":
            if name.startswith(".text"):
                return "text"
            if name.startswith((".rodata", "__TEXT")):
                return "rodata"
            return None
        return "own" if name.startswith(args.section) else None

    def index_for(class_name, after_all_of=None, at=None):
        """Where this class's entries go: at a replaced entry's own index, or in the same class run
        after `after_all_of`'s last entry - the first entry of that class later in the order, because
        within a run the order is the command line's."""
        if at is not None:
            return at
        anchor = -1
        for i, e in enumerate(order):
            if after_all_of in e[1]:
                anchor = i
        if anchor < 0:
            sys.exit("nothing in %s comes from %s" % (args.section, after_all_of))
        for i in range(anchor + 1, len(order)):
            if class_of(order[i][0]) == class_name:
                return i
        return len(order)

    for spec in args.replace:
        old, new = spec.split(":", 1)
        hit = [i for i, e in enumerate(order) if old in e[1]]
        if not hit:
            sys.exit("nothing in %s comes from %s" % (args.section, old))
        for i in hit:
            removed_flag[i] = True
        anchor = hit[-1] + 1
        by_class = {}
        for n, sz in sizes_of(new):
            c = class_of(n)
            if c is None:
                continue
            by_class.setdefault(c, []).append((n, new, sz))
        for c, entries in by_class.items():
            # The old object's own entry of this class is where the new one goes; failing that, the
            # position is the anchor rule above.
            at = None
            same = [i for i in hit if class_of(order[i][0]) == c]
            if same:
                at = same[0]
            else:
                at = index_for(c, after_all_of=(old.rsplit("/", 1)[-1]), at=None) \
                    if False else anchor
            changes.append((at, entries))

    for spec in args.insert:
        obj, _, after = spec.partition(":")
        after_path = None
        if after:
            anchor = -1
            for i, e in enumerate(order):
                if after in e[1]:
                    anchor = i
                    after_path = e[1]
            if anchor < 0:
                sys.exit("nothing in %s comes from %s" % (args.section, after))
        by_class = {}
        for n, sz in sizes_of(obj):
            c = class_of(n)
            if c is None:
                continue
            by_class.setdefault(c, []).append((n, obj, sz))
        if not by_class:
            sys.exit("%s has no input for %s" % (obj, args.section))
        anchor_pos = load_pos.get(after_path, -1)
        for c, entries in by_class.items():
            if not after:
                changes.append((len(order), entries))
                continue
            idx = None
            for i in range(anchor + 1, len(order)):
                if class_of(order[i][0]) != c:
                    continue
                if load_pos.get(order[i][1], 1 << 30) > anchor_pos:
                    idx = i
                    break
            changes.append((len(order) if idx is None else idx, entries))

    placed = apply_changes(start, order, removed_flag, changes)
    end = placed[-1][2] + placed[-1][3]
    print("%s: %d inputs, start %#x, raw end %#x, %d removed, %d inserted"
          % (args.section, len(order), start, end, sum(removed_flag),
             sum(len(e) for _, e in changes)))
    old_by_key = {}
    for e in order:
        old_by_key.setdefault((e[0], e[1]), e[2])
    n = 0
    for name, path, addr, size, is_new in placed:
        was = old_by_key.get((name, path))
        if is_new or (was is not None and was != addr):
            print("    %#010x %#06x %-22s %s%s" % (addr, size, name, path.rsplit("/", 1)[-1],
                                                   "   (was %#x)" % was if was is not None else "   NEW"))
            n += 1
            if n >= args.show:
                break


if __name__ == "__main__":
    main()
