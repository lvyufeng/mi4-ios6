#!/usr/bin/env python3
"""Check that our boot_args layout matches the one XNU's entry code reads.

`osfmk/arm/start.s` reads `boot_args` with raw loads:

    ldr  r8, [r0, BA_PHYS_BASE]
    ldr  r9, [r0, BA_VIRT_BASE]
    ldr  r10, [r0, BA_MEM_SIZE]
    ldr  r4, [r0, BA_TOP_OF_KERNEL_DATA]

and those BA_* constants come from `offsetof(struct boot_args, ...)` in the same tree
(osfmk/arm/genassym.c). So if our struct's field list or field types drift from
`pexpert/pexpert/arm/boot.h`, XNU does not fail to build or fail loudly - it reads the
wrong 32-bit word and uses it as the physical base of memory. There is no way to catch
that on the device except by watching a kernel misbehave, which is exactly the kind of
failure that costs days. Catching it here costs a build-time check.

Layouts are computed for 32-bit ARM (ILP32, little-endian, natural alignment), which is
the only ABI this payload is built for.

Usage:
    check_boot_args_abi.py [--repo-root DIR]
Exit status 0 if the layouts agree, 1 if they do not, 2 on a parse problem.
"""

import argparse
import os
import re
import sys

# --- the two definitions -----------------------------------------------------

XNU_HEADER = "external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h"
OUR_HEADER = "stages/stage90/stage90.h"

# Types as they appear, mapped to (size, alignment) on ARM ILP32. `unsigned long` is
# 4 bytes on 32-bit ARM, which is why Boot_Video can be read as six uint32_t.
SIZES = {
    "uint8_t": (1, 1), "char": (1, 1),
    "uint16_t": (2, 2),
    "uint32_t": (4, 4), "int": (4, 4), "unsigned int": (4, 4),
    "unsigned long": (4, 4), "long": (4, 4),
    "void *": (4, 4),
}

def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def find_struct(text, names):
    """Return the body of the first struct whose name is in `names`."""
    for name in names:
        m = re.search(
            r"(?:typedef\s+)?struct\s+" + re.escape(name) + r"\s*\{(.*?)\}\s*"
            r"(?P<alias>[A-Za-z_][A-Za-z0-9_]*)?\s*;",
            text, flags=re.S)
        if m:
            return m.group(1), (m.group("alias") or "").strip()
    return None, None


FIELD_RE = re.compile(
    r"^(?P<type>.+?)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"(?:\s*\[\s*(?P<count>[A-Za-z0-9_]+)\s*\])?$")


def parse_fields(body, defines, known_structs):
    """Yield (type, name, size) for each field, resolving array counts and macros."""
    fields = []
    for raw in body.split(";"):
        line = raw.strip()
        if not line or line.startswith("/*"):
            continue
        line = re.sub(r"\s+", " ", line)
        # Normalise pointer spelling: `void *p`, `void* p` and `void * p` are all the
        # same declaration, and all three appear in these headers.
        line = re.sub(r"\s*\*\s*", " * ", line)
        m = FIELD_RE.match(line)
        if not m:
            raise ValueError("cannot parse field: %r" % line)
        ftype = m.group("type").strip()
        fname = m.group("name")
        count = 1

        if m.group("count") is not None:
            token = m.group("count")
            if token in defines:
                count = defines[token]
            else:
                try:
                    count = int(token, 0)
                except ValueError:
                    raise ValueError("unknown array size %r for %s" % (token, fname))

        # An embedded struct counts as its own layout.
        if ftype in known_structs:
            sub_fields = known_structs[ftype]
            for _ in range(count):
                fields.extend(sub_fields)
            continue

        if ftype not in SIZES:
            raise ValueError("unknown type %r for field %s" % (ftype, fname))
        size, _align = SIZES[ftype]
        for i in range(count):
            name = fname if count == 1 else "%s[%d]" % (fname, i)
            fields.append((ftype, name, size))
    return fields


def layout(fields):
    """Compute (name, offset, size) for each field under ARM ILP32."""
    out = []
    off = 0
    for ftype, name, size in fields:
        align = SIZES.get(ftype, (0, 4))[1]
        if off % align:
            off += align - (off % align)
        out.append((name, off, size))
        off += size
    # struct alignment = max member alignment
    max_align = max([SIZES.get(f, (0, 4))[1] for f, _n, _s in fields] or [4])
    if off % max_align:
        off += max_align - (off % max_align)
    return out, off


def collect_defines(text):
    def to_int(tok):
        tok = tok.strip().rstrip("uUlL")
        return int(tok, 0)
    return {m.group(1): to_int(m.group(2))
            for m in re.finditer(
                r"#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+"
                r"(0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*)\s*$",
                text, flags=re.M)}


def load(path, struct_names):
    with open(path) as fh:
        text = strip_comments(fh.read())
    defines = collect_defines(text)

    # Nested structs first, so an embedded one can be expanded. Both spellings are
    # registered: XNU writes `Boot_Video Video` (typedef) and our header writes
    # `struct boot_video Video`.
    known = {}
    for nested in ("Boot_Video", "boot_video"):
        body, _alias = find_struct(text, [nested])
        if body is not None:
            sub = parse_fields(body, defines, {})
            known[nested] = sub
            known["struct " + nested] = sub

    body, alias = find_struct(text, struct_names)
    if body is None:
        raise ValueError("no struct %s in %s" % (struct_names, path))
    if alias:
        known.setdefault(alias, None)
    fields = parse_fields(body, defines, known)
    return fields


def squash(rows):
    """Collapse runs of array elements into one row: CommandLine[0..255] -> one line."""
    out = []
    for name, off, size in rows:
        m = re.match(r"^(.*)\[(\d+)\]$", name)
        if m and out:
            prev_name, prev_off, prev_size, prev_count = out[-1]
            if (prev_name == m.group(1) and prev_count is not None and
                    prev_off + prev_size * prev_count == off):
                out[-1] = (prev_name, prev_off, prev_size, prev_count + 1)
                continue
        if m:
            out.append((m.group(1), off, size, 1))
        else:
            out.append((name, off, size, None))
    return out


def label(name, count):
    return "%s[%d]" % (name, count) if count else name


def camel_to_upper(name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).upper()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo-root", default=os.path.join(os.path.dirname(__file__), ".."))
    ap.add_argument("--verbose", action="store_true",
                    help="list every array element instead of collapsing runs")
    args = ap.parse_args()
    root = os.path.abspath(args.repo_root)

    try:
        xnu_fields = load(os.path.join(root, XNU_HEADER), ["boot_args"])
        our_fields = load(os.path.join(root, OUR_HEADER), ["boot_args"])
    except (OSError, ValueError) as exc:
        print("check_boot_args_abi: cannot parse: %s" % exc, file=sys.stderr)
        return 2

    xnu_layout, xnu_size = layout(xnu_fields)
    our_layout, our_size = layout(our_fields)

    print("boot_args ABI (ARM ILP32)")
    print("  %-24s %8s %8s   %s" % ("field", "xnu off", "our off", "verdict"))

    bad = 0
    if args.verbose:
        for (xn, xo, xs), (on_, oo, osz) in zip(xnu_layout, our_layout):
            ok = (xn == on_ and xo == oo and xs == osz)
            bad += 0 if ok else 1
            print("  %-24s %8d %8d   %s" % (xn, xo, oo, "ok" if ok else "MISMATCH"))
    else:
        for (xn, xo, xs, xc), (on_, oo, osz, oc) in zip(squash(xnu_layout),
                                                        squash(our_layout)):
            ok = (xn == on_ and xo == oo and xc == oc)
            bad += 0 if ok else 1
            print("  %-24s %8d %8d   %s" % (label(xn, xc), xo, oo,
                                            "ok" if ok else "MISMATCH"))

    if len(xnu_layout) != len(our_layout):
        print("  field count differs: xnu %d, ours %d" % (len(xnu_layout), len(our_layout)))
        bad += 1

    print("  %-24s %8d %8d   %s" % ("sizeof", xnu_size, our_size,
                                    "ok" if xnu_size == our_size else "MISMATCH"))
    if xnu_size != our_size:
        bad += 1

    # The four offsets start.s actually loads. Called out separately because a mismatch
    # here is the failure mode that matters: XNU silently uses the wrong 32-bit word as
    # the physical base of memory, rather than failing to build or faulting visibly.
    print()
    print("  offsets osfmk/arm/start.s loads by hand:")
    for name in ("virtBase", "physBase", "memSize", "topOfKernelData"):
        off = next((o for n, o, _s in xnu_layout if n == name), None)
        if off is None:
            print("    BA_%s MISSING" % camel_to_upper(name))
            bad += 1
        else:
            print("    BA_%-22s @ %3d  (ldr rN, [r0, #%d])" % (camel_to_upper(name), off, off))

    if bad:
        print("\nFAIL: %d difference(s) - XNU's entry code would read the wrong data." % bad)
        return 1
    print("\nOK: layout matches XNU's boot_args.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
