#!/usr/bin/env python3
"""
Extract the payload's device-tree builder from stages/stage90/stage90_main.c, for a host build.

    tools/apple_dt_extract.py --src stages/stage90/stage90_main.c --out builder.inc
    tools/apple_dt_extract.py --src ... --out extract.c --include stage90_dt_shim.h \
                              --facts facts.env

Why this exists, and why it is the *only* extractor. Two harnesses need this builder - the dump that
writes the blob (`tools/apple_dt_host_dump.sh`, whose blob `tools/xnu_dt_walk.py` walks) and XNU's
own reader's harness (`tools/host_dt_check.sh`) - and they used to find it two different ways: one by
matching the function's braces, one by **hard-coded line numbers** with each end asserted. When
experiment 459's edit to `stage90_main.c` moved the lines, the brace matcher followed it and the line
numbers did not, so the dump kept writing a blob from *before* the edit while the check tested the
tree *after* it. Nothing failed: the line-number script's own assertions caught the move (it exits 1),
but its output file was left in place from the previous run, and a reader of that breadcrumb of a
file gets a confident, self-consistent, stale tree - `/chosen` with the memory-map child missing, which
is exactly the shape of the defect 459 was hunting.

So the extent has one definition now, here, and it is structural: every piece is found by its
declaration and closed by matching braces, and every piece that cannot be found aborts loudly. What
it finds:

    g_apple_dt                        the tree's storage, with its declared bound and alignment
    STAGE90_CHOSEN_RANDOM_SEED_BYTES  the seed length the builder allocates for
    stage90_chosen_random_seed_rule   the seed's content, a string, so it comes across verbatim
    build_chosen_random_seed          the function that fills it
    build_stage90_apple_dt            the builder itself

`static` is removed from the two functions and the buffer, because the harness compiles them in a
different unit; the payload's own copies keep their linkage. The buffer's **bound and alignment are
reported** (`--facts`), so a consumer that needs them - the shim header declares `g_apple_dt` with the
bound - takes them from the same reading rather than restating 32768. Three copies of that number in
three files with nothing comparing them is the defect class this project has paid for twenty-four
times.

Exit status is 0 only if every piece was found and the output was written.
"""

import argparse
import os
import re
import sys


def die(msg):
    sys.exit("apple_dt_extract: " + msg)


def find_function(src, signature_re, label, src_path):
    """Return (start, end_exclusive) of a function's definition, by braces."""
    m = re.search(signature_re, src)
    if not m:
        die("cannot find %s in %s - it is the payload's own builder, so a host harness can only "
            "test it by extracting it. Fix the extraction rather than the harness." %
            (label, src_path))
    i = src.index("{", m.end() - 1)
    depth = 0
    j = i
    while j < len(src):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    if depth != 0:
        die("unbalanced braces while extracting %s from %s" % (label, src_path))
    return m.start(), j + 1


def line_of(src, offset):
    return src.count("\n", 0, offset) + 1


def report_lines(dt_bytes, dt_align, dt_line, seed_bytes, seed_define_line, seed_rule_text, seed_rule,
                 seed_fn, seed_fn_span, builder_fn, builder_span, src):
    """What was extracted and where from, as text - the same list the caller prints and the banner."""
    return [
        "g_apple_dt: %d bytes, aligned %d (line %d)" % (dt_bytes, dt_align, dt_line),
        "STAGE90_CHOSEN_RANDOM_SEED_BYTES: %d (line %d)" % (seed_bytes, seed_define_line),
        'stage90_chosen_random_seed_rule: "%s" (line %d)' %
        (seed_rule_text, line_of(src, src.index(seed_rule))),
        "build_chosen_random_seed: %d lines (lines %d-%d)" %
        (seed_fn.count("\n") + 1, line_of(src, seed_fn_span[0]) + 1, line_of(src, seed_fn_span[1])),
        "build_stage90_apple_dt: %d lines (lines %d-%d)" %
        (builder_fn.count("\n") + 1, line_of(src, builder_span[0]) + 1,
         line_of(src, builder_span[1])),
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="stages/stage90/stage90_main.c")
    ap.add_argument("--out", required=True, help="the extracted text to write")
    ap.add_argument("--include", action="append", default=[],
                    help="a header to #include at the top of the output (repeatable)")
    ap.add_argument("--facts", help="write KEY=VALUE lines about what was extracted")
    ap.add_argument("--banner-note", help="prepend a comment block with this note and the report")
    args = ap.parse_args()

    src_path = args.src
    src = open(src_path).read()

    # --- the tree's storage, whose bound and alignment every other copy must come from ---
    m = re.search(r"\nstatic uint8_t g_apple_dt\[(\d+)\]([^;]*);\n", src)
    if not m:
        die("cannot find the declaration of g_apple_dt in %s. Its bound is the tree buffer's size, "
            "and this harness takes that number from here rather than restating it." % src_path)
    dt_bytes = int(m.group(1))
    dt_align_m = re.search(r"aligned\((\d+)\)", m.group(2))
    dt_align = int(dt_align_m.group(1)) if dt_align_m else 1
    dt_decl = "uint8_t g_apple_dt[%d] __attribute__((aligned(%d)));" % (dt_bytes, dt_align)
    dt_line = line_of(src, m.start(1))

    # --- the seed: its length, its content, and the function that expands it ---
    m = re.search(r"\n#define STAGE90_CHOSEN_RANDOM_SEED_BYTES (\d+)u\n", src)
    if not m:
        die("cannot find STAGE90_CHOSEN_RANDOM_SEED_BYTES in %s" % src_path)
    seed_bytes = int(m.group(1))
    seed_define = m.group(0).strip()
    seed_define_line = line_of(src, m.start()) + 1  # the match starts on the preceding newline

    # The rule string is the seed's whole content, so it comes across verbatim rather than being
    # restated here - a shim that spelled its own rule would test itself.
    m = re.search(r'\nstatic const char stage90_chosen_random_seed_rule\[\] = "([^"]*)";\n', src)
    if not m:
        die("cannot find stage90_chosen_random_seed_rule in %s" % src_path)
    seed_rule_text = m.group(1)
    seed_rule = m.group(0).strip()

    s, e = find_function(src, r"\nstatic void build_chosen_random_seed\(uint8_t \*out, uint32_t len\)",
                         "build_chosen_random_seed", src_path)
    seed_fn_span, seed_fn = (s, e), src[s:e]
    seed_fn = seed_fn.replace("static void build_chosen_random_seed",
                              "void build_chosen_random_seed", 1)

    s, e = find_function(src, r"\nstatic void build_stage90_apple_dt\(struct apple_dt_builder \*b\)",
                         "build_stage90_apple_dt", src_path)
    builder_span, builder_fn = (s, e), src[s:e]
    builder_fn = builder_fn.replace("static void build_stage90_apple_dt",
                                    "void build_stage90_apple_dt", 1)

    pieces = []
    report = report_lines(dt_bytes, dt_align, dt_line, seed_bytes, seed_define_line, seed_rule_text,
                          seed_rule, seed_fn, seed_fn_span, builder_fn, builder_span, src)
    if args.banner_note:
        pieces.append("/* %s" % args.banner_note)
        pieces += [" * " + r for r in report]
        pieces.append(" */")
    for h in args.include:
        pieces.append('#include "%s"' % h)
    pieces += [dt_decl, seed_define, seed_rule, seed_fn, builder_fn]
    out = "\n".join(pieces) + "\n"
    open(args.out, "w").write(out)

    # The line spans, so a caller can print where the text came from - the point of extracting the
    # shipping function is that a compiler error in it has to be read against the real file.
    for line in report:
        print("extracted " + line)

    if args.facts:
        with open(args.facts, "w") as fh:
            fh.write("STAGE90_APPLE_DT_BYTES=%d\n" % dt_bytes)
            fh.write("STAGE90_APPLE_DT_ALIGN=%d\n" % dt_align)
            fh.write("STAGE90_CHOSEN_RANDOM_SEED_BYTES=%d\n" % seed_bytes)

    print("wrote %s (%d lines)" % (args.out, out.count("\n")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
