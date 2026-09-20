#!/usr/bin/env python3
"""
The undefined-instruction handler's two new promises, checked rather than stated: the guard refuses a
NULL, and the `bl` it makes targets Apple's body and not itself.

Why this is a check and not a comment
-------------------------------------
Experiment 474 is the run that turned both of these from prose into failures, and they fail in opposite
ways - one silently, one catastrophically:

  - **the guard's answer for `args = 0`.** `entry_panic_args_page` was asked "does the 32-byte window
    lie in one page", and for zero it says yes: zero is 4-aligned and `[0, 32)` is inside one page. The
    comment beside it said "zero there means the reads below did not happen", so a *refusal* and an
    *accepted NULL* published the same key value, and the read that followed was of address 0 -
    inside the fault handler. Nothing in the source says which answer is right; the only way to keep the
    answer is to run it. **This check compiles the two guard functions out of `entry_stubs.c` and runs
    the case table on the host**, so the answer for zero is a build failure and not a sentence a reader
    has to trust. It is the AES check's shape (the same source, on the host) for the same reason: the
    property is about the code and not about the hardware;
  - **the forward's target.** 475 forwards a user-mode `udf` to Apple's own `locore_fleh_undef`, whose
    name is one prefix away from this image's `fleh_undef`. If the two ever resolve to the same address
    - a rename, a prefix macro, a `#define` - the handler calls itself from inside the fault handler
    and recurses until the stack is gone, which is 474's storm with a different first cause and just as
    invisible from the log. So the check reads the *linked image*: the two symbols must be distinct, the
    bytes inside `fleh_undef` must contain a `bl` whose computed target is Apple's body, and they must
    contain no `bl` to `fleh_undef` itself.

Where each side of each comparison comes from
---------------------------------------------
  - the guard's source is the file the image is compiled from, found by name (`entry_panic_args_page`,
    `entry_panic_arg_word`) and brace-matched with comments and string literals skipped, so a brace in
    prose cannot end the function early;
  - `ENTRY_PANIC_ARG_BYTES` is read from that file and required to be 32, because the case table below
    is written for the window the source's own comment derives (the `va_list` at `sp+16`, its `__ap` at
    `sp+28`, the two spilled varargs at `sp+32`/`sp+36`). If that number moves, the table is wrong and
    this check says so rather than silently testing a different window;
  - the symbols come from the image's own symbol table (`nm -S --defined-only`), and the instruction
    encodings from `objdump -d` over the handler's range only - the `bl` target is *computed* from the
    encoding (`pc + 8 + (sign_extended(imm24) << 2)`), not read off objdump's symbol column, so neither
    a comment nor a symbol table can make this agree.

`--selftest` runs both halves against mutated inputs: the guard with `args == 0u` rewritten to
`args == 1u` (the case table must then reject it), and the forward with the two addresses made equal.
Every mutation must be refused.

Usage:
    tools/check_undef_handler.py --guard   --source stages/stage90/xnu_arm_boot/entry_stubs.c
    tools/check_undef_handler.py --forward --elf out/stage90/xnu_arm_entry.elf
    tools/check_undef_handler.py --selftest
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

HANDLER = "fleh_undef"
APPLE_HANDLER = "locore_fleh_undef"
WINDOW_DEFINE = "ENTRY_PANIC_ARG_BYTES"
WINDOW = 32

# ---------------------------------------------------------------- the case table
#
# Every row is (args, expected) for `entry_panic_args_page`. The acceptance rows are the control: a
# guard that refused everything would be a different defect, and this table is what separates the two.
# The last row is the limit said out loud - the predicate is about shape and can say nothing about
# whether the page is mapped, which is the correction 474 forced into the function's comment.
PAGE_CASES = [
    (0x00000000, 0),  # 474: the NULL the handler was handed, and the read of address 0 after it
    (0xC80ABEB0, 1),  # experiment 362's `r_args`: the real panic frame must still be accepted
    (0x00000004, 1),  # 4-aligned, window [4, 0x24) in page 0
    (0x00000FE0, 1),  # window [0xFE0, 0xFFF]: ends exactly at the page's last byte
    (0x00000FF0, 0),  # window [0xFF0, 0x100F]: straddles the boundary
    (0x00000FF4, 0),  # not 4-aligned
    (0x00000001, 0),  # not 4-aligned
    (0x00001000, 1),  # the next page, likewise aligned
    (0xFFFFFFE0, 1),  # shape alone accepts an address at the top of the space - see the header note
    (0xFFFFFFF0, 0),  # `args + 31` wraps to 0x0000000F, so the two page ends differ
    (0xFFFFFFFF, 0),  # not 4-aligned
]

# (args, p, n, expected) for `entry_panic_arg_word`: "is `p` inside [args, args + 32)".
WORD_CASES = [
    (0x00001000, 0x00001000, 4, 1),
    (0x00001000, 0x0000101C, 4, 1),  # 0x1C + 4 == 32: the last word that fits
    (0x00001000, 0x00001020, 4, 0),  # 0x20 + 4 > 32
    (0x00001000, 0x00001018, 8, 1),
    (0x00001000, 0x0000101C, 8, 0),
    (0x00001000, 0x00000FFC, 4, 0),  # below the window
    (0x00000000, 0x00000000, 4, 1),  # this predicate is not the NULL filter - the page test is
]


def fail(message):
    sys.stderr.write("check_undef_handler: %s\n" % message)
    sys.exit(1)


def extract_define(text, name):
    match = re.search(r"^#define\s+%s\s+([0-9]+)u?\b" % re.escape(name), text, flags=re.M)
    if not match:
        fail("no `#define %s <number>` in the source: the window's width is not stated anywhere" % name)
    return int(match.group(1))


def extract_function(text, name):
    """The whole text of `static ... name(...) { ... }`, comments included, brace- and quote-aware."""
    # The definition may be on one line (`static int entry_x(...)`) or, as this file writes them, with
    # the return type on its own line - so the signature is allowed exactly one line break.
    match = re.search(r"^static\b[^\n]*(?:\n[^\n]*)?%s\s*\(" % re.escape(name), text, flags=re.M)
    if not match:
        fail("no `static ... %s(` definition in the source" % name)
    index = text.index("{", match.end() - 1)
    start = match.start()
    depth = 0
    quote = None
    position = index
    while position < len(text):
        char = text[position]
        if quote is not None:
            if char == "\\":
                position += 2
                continue
            if char == quote:
                quote = None
        elif char in "\"'":
            quote = char
        elif text.startswith("/*", position):
            position = text.index("*/", position) + 2
            continue
        elif text.startswith("//", position):
            position = text.index("\n", position)
            continue
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start:position + 1]
        position += 1
    fail("%s's body is not closed" % name)


def run_guard_cases(source_text, mutate=None):
    """Compile the two guard functions on the host around a generated main; compare with the tables."""
    page_text = extract_function(source_text, "entry_panic_args_page")
    word_text = extract_function(source_text, "entry_panic_arg_word")
    window = extract_define(source_text, WINDOW_DEFINE)
    if window != WINDOW:
        fail("%s is %d in the source and the case table was derived for %d: re-derive the table"
             % (WINDOW_DEFINE, window, WINDOW))
    if mutate is not None:
        page_text = mutate(page_text)
    rows = [(0, args, want) for args, want in PAGE_CASES]
    rows += [(1, args, want) for args, _p, _n, want in WORD_CASES]
    harness = ["#include <stdint.h>", "#include <stdio.h>",
               "#define %s %du" % (WINDOW_DEFINE, window), page_text, word_text, "int main(void) {"]
    for args, _want in PAGE_CASES:
        harness.append(f'    printf("%u 0x%08xu %d\\n", 0u, 0x{args:08x}u,'
                       f' entry_panic_args_page(0x{args:08x}u));')
    for args, p, n, _want in WORD_CASES:
        harness.append(f'    printf("%u 0x%08xu %d\\n", 1u, 0x{args:08x}u,'
                       f' entry_panic_arg_word(0x{args:08x}u, 0x{p:08x}u, {n}u));')
    harness.append("    return 0;")
    harness.append("}")
    problems = []
    with tempfile.TemporaryDirectory() as work:
        src = os.path.join(work, "guard.c")
        exe = os.path.join(work, "guard")
        with open(src, "w") as handle:
            handle.write("\n".join(harness) + "\n")
        cc = os.environ.get("CC", "cc")
        built = subprocess.run([cc, "-O1", "-w", "-o", exe, src], capture_output=True, text=True)
        if built.returncode != 0:
            fail("the host compiler refused the guard's own source: %s" % built.stderr.strip())
        ran = subprocess.run([exe], capture_output=True, text=True)
        if ran.returncode != 0:
            fail("the guard's case table crashed (exit %d): %s" % (ran.returncode, ran.stderr.strip()))
        lines = [line.split() for line in ran.stdout.splitlines() if line.strip()]
        if len(lines) != len(rows):
            fail("the case table printed %d rows and %d were expected" % (len(lines), len(rows)))
        for (kind, args, want), parts in zip(rows, lines):
            got = int(parts[2])
            if got != want:
                function = "entry_panic_args_page" if kind == 0 else "entry_panic_arg_word"
                problems.append("%s(0x%08x, ...) = %d and the case table says %d"
                                % (function, args, got, want))
    return problems, len(rows)


# ---------------------------------------------------------------- the forward, in the linked image
def elf_symbols(path):
    out = subprocess.run(["arm-none-eabi-nm", "-S", "--defined-only", path],
                         check=True, capture_output=True, text=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            addr, size, _kind, name = parts
            try:
                table[name] = (int(addr, 16), int(size, 16))
            except ValueError:
                continue
        elif len(parts) == 3:
            addr, _kind, name = parts
            try:
                table[name] = (int(addr, 16), 0)
            except ValueError:
                continue
    return table


def handler_range(symbols, name):
    if name not in symbols:
        fail("the image defines no `%s`, so there is nothing to check" % name)
    start, size = symbols[name]
    if start == 0:
        fail("`%s` is defined at 0" % name)
    if size == 0:
        above = [addr for addr, _s in symbols.values() if addr > start]
        if not above:
            fail("`%s` has no size and no symbol above it, so its range is unknown" % name)
        size = min(above) - start
    return start, size


def bl_targets(path, start, size):
    """(address, target) for every `bl` in the range, from the encodings rather than the symbols."""
    out = subprocess.run(["arm-none-eabi-objdump", "-d",
                          "--start-address=0x%x" % start, "--stop-address=0x%x" % (start + size), path],
                         check=True, capture_output=True, text=True).stdout
    targets = []
    for line in out.splitlines():
        match = re.match(r"\s*([0-9a-f]{8}):\s+([0-9a-f]{8})\s", line)
        if not match:
            continue
        address = int(match.group(1), 16)
        encoding = int(match.group(2), 16)
        if (encoding & 0xFF000000) != 0xEB000000:
            continue
        immediate = encoding & 0x00FFFFFF
        if immediate & 0x00800000:
            immediate -= 0x01000000
        targets.append((address, address + 8 + (immediate << 2)))
    if not targets:
        fail("no `bl` at all inside `%s`'s %u bytes: the handler cannot be forwarding anywhere"
             % (HANDLER, size))
    return targets


def check_forward(elf, mutate=None):
    symbols = elf_symbols(elf)
    if mutate is not None:
        symbols = mutate(symbols)
    start, size = handler_range(symbols, HANDLER)
    apple_start, _apple_size = handler_range(symbols, APPLE_HANDLER)
    problems = []
    if apple_start == start:
        problems.append("`%s` and `%s` are both at 0x%08x: the forward would be a call to this handler"
                        % (HANDLER, APPLE_HANDLER, start))
        return problems, start, apple_start
    targets = bl_targets(elf, start, size)
    to_apple = [addr for addr, target in targets if target == apple_start]
    to_self = [addr for addr, target in targets if target == start]
    if not to_apple:
        problems.append("no `bl` inside 0x%08x..0x%08x targets `%s` at 0x%08x, so the user-mode `udf`"
                        " is not forwarded to Apple's body (%d `bl`s were read)"
                        % (start, start + size, APPLE_HANDLER, apple_start, len(targets)))
    if to_self:
        problems.append("`%s` calls itself at 0x%08x, which is a recursion inside the fault handler"
                        % (HANDLER, to_self[0]))
    return problems, start, apple_start


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", default="stages/stage90/xnu_arm_boot/entry_stubs.c")
    parser.add_argument("--elf", default="out/stage90/xnu_arm_entry.elf")
    parser.add_argument("--guard", action="store_true", help="run the guard's case table on the host")
    parser.add_argument("--forward", action="store_true", help="check the forward in the linked image")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if not (args.guard or args.forward or args.selftest):
        parser.error("one of --guard, --forward or --selftest is required")

    if args.selftest:
        with open(args.source) as handle:
            text = handle.read()
        problems, rows = run_guard_cases(text)
        if problems:
            fail("the source's own guard does not answer the case table: %s" % "; ".join(problems))

        def unzero(page_text):
            return page_text.replace("if (args == 0u)", "if (args == 1u)")

        problems, _rows = run_guard_cases(text, mutate=unzero)
        if not any("0x00000000" in problem for problem in problems):
            fail("--selftest: `args == 0u` rewritten to `args == 1u` was still accepted")

        def merge(symbols):
            merged = dict(symbols)
            merged[APPLE_HANDLER] = symbols[HANDLER]
            return merged

        problems, _start, _apple = check_forward(args.elf, mutate=merge)
        if not any("both at" in problem for problem in problems):
            fail("--selftest: the two handler addresses made equal were not refused")
        print("check_undef_handler: --selftest ok (the guard's %d rows, and both mutations refused)"
              % rows)
        return

    if args.guard:
        with open(args.source) as handle:
            text = handle.read()
        problems, rows = run_guard_cases(text)
        if problems:
            fail("the guard's case table fails:\n  " + "\n  ".join(problems))
        print("  xnu_entry_475: the panic-argument guard answers the case table (%d rows) - a NULL frame"
              " is refused, the window that fits a page is accepted and the one that straddles it is not"
              % rows)

    if args.forward:
        problems, start, apple_start = check_forward(args.elf)
        if problems:
            fail("the forward is wrong:\n  " + "\n  ".join(problems))
        print("  xnu_entry_475: `%s` at 0x%08x forwards to `%s` at 0x%08x - distinct symbols, and the"
              " `bl` inside the handler computes to Apple's body"
              % (HANDLER, start, APPLE_HANDLER, apple_start))


if __name__ == "__main__":
    main()
