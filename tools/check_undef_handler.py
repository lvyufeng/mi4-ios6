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
  - **where the user case is routed.** 475 routed it *from C* - record the user's `udf`, then `bl
    locore_fleh_undef` - and 476's run measured the cost: `locore_fleh_undef` derives the saved PC from
    `lr` (`subeq lr, lr, #4`), so a call from this image resumes the user thread at the `bl`'s own
    address, in user mode, where the fetch permission-faults and the kernel panics (the run's fifth
    abort is a user fetch of `0x80006d04`, which *is* that `bl`). 477 moves the split into the vector
    page, and this half of the check reads the *linked image* for both halves of that statement: the
    trampoline's user-mode literal must be Apple's `locore_fleh_undef` and its kernel-mode literal must
    be this image's `fleh_undef`, **and `fleh_undef` must contain no `bl` to either handler** - so the
    arrangement that crashed 476's boot cannot come back by an edit that looks innocuous.

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
`args == 1u` (the case table must then reject it); the split with the two handler symbols made
identical (the literal cannot be Apple's body if the body is this one), with slot 1's user literal
pointed back at this image's handler, and with a call to the handler inserted inside the handler's own
body; and the call scan pointed at a function that *does* call a handler, which must be refused. The
**unmutated** inputs are controls and are required to pass - the split's source half needs that one
specifically, because its first version refused the real source (see `calls_at_body_depth`). Every
mutation must be refused.

Usage:
    tools/check_undef_handler.py --guard --source src/entry/entry_stubs.c
    tools/check_undef_handler.py --split --elf out/stage90/xnu_arm_entry.elf \
        --source src/entry/entry_stubs.c
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
# The trampoline's two literals, in `entry_vectors.s`. The kernel one is the slot's target that
# `build_entry.sh`'s table compares; the user one is 477's whole change and is checked here as well,
# because the two checks are about different things (which handler the page names, and whether the C
# handler can still call across).
VEC_KERNEL_LIT = "vec_tramp_1_handler"
VEC_USER_LIT = "vec_tramp_1_user_handler"
# Every other first-level handler of this image. A `bl` from the C trap handler to any of these is the
# shape 476's run died of: `locore_*` because their bodies derive the saved PC from `lr`, and
# `fleh_undef` itself because that is a recursion inside the fault handler.
FORBIDDEN_CALLEES = ("fleh_undef", "locore_fleh_undef", "locore_fleh_prefabt", "locore_fleh_dataabt")
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


def strip_comments(text):
    """Comments and string literals blanked - for asking what the *code* names, not what it says."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r'"(?:[^"\\]|\\.)*"', '""', text)


def extract_function(text, name, static=True):
    """The whole text of a `name(...) { ... }` definition, comments included, brace/quote-aware.

    `static=True` (the default) is how `entry_stubs.c` writes the two guard functions - return type on
    its own line - and `static=False` is how it writes `fleh_undef` (`void fleh_undef(void)`).
    """
    # The signature is allowed exactly one line break, because this file's guard functions put the
    # return type on the line above the name.
    pattern = r"^static\b[^\n]*(?:\n[^\n]*)?%s\s*\(" if static else r"^void\s+%s\s*\("
    match = re.search(pattern % re.escape(name), text, flags=re.M)
    if not match:
        fail("no definition of `%s` in the source" % name)
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


# ------------------------------------------------- the split, in the linked image and in the source
def elf_words(path):
    """(vaddr -> value) for every word in an allocated section, read from the file's own bytes."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"\x7fELF":
        fail("%s is not an ELF file" % path)
    import struct
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, _shstrndx = struct.unpack_from("<HHH", data, 0x2e)
    words = {}
    for index in range(shnum):
        base = shoff + index * shentsize
        _name, _type, _flags, addr, offset, size = struct.unpack_from("<IIIIII", data, base)
        if addr == 0:
            continue
        for at in range(0, size - 3, 4):
            words[addr + at] = struct.unpack_from("<I", data, offset + at)[0]
    return words


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
    """`(start, size)` for a function whose size `nm` knows.

    **A size of zero is refused rather than guessed at, and that is 477's third defect.** The first
    version took "the nearest symbol above the start" as a size, on the reasoning that a symbol with no
    `.size` is followed by the next function. For Apple's ARM assembly it is not: `nm` gives
    `locore_fleh_dataabt` (0x800146d0) no size because `locore.s` emits no `.size` directives, and the
    nearest symbol above it is a label *inside* the same body - so the range came out 16 bytes and a
    scan of it read **zero** `bl`s. The control that pointed at it therefore failed with "the call scan
    found no `bl` in a function that has one", which is the honest failure of a control that proves
    nothing; a check that had pointed at it in earnest would have cleared it. A range that cannot be
    read is not a range, so this refuses and says which symbols are affected.
    """
    if name not in symbols:
        fail("the image defines no `%s`, so there is nothing to check" % name)
    start, size = symbols[name]
    if start == 0:
        fail("`%s` is defined at 0" % name)
    if size == 0:
        fail("`%s` has no size in this image's symbol table, so its range is unknown and a `bl` scan"
             " over a guessed one reads nothing and looks clean - Apple's `locore_*` handlers are the"
             " symbols this happens to (`nm` shows a label inside the body as the next symbol above);"
             " point the scan at a function with a size, or derive the extent from source" % name)
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
    return targets


def check_calls_no_handler(path, symbols, name=HANDLER, forbidden=FORBIDDEN_CALLEES, mutate=None):
    """`name` must not call any of `forbidden` - each one is a way the frame's PC is destroyed."""
    start, size = handler_range(symbols, name)
    if mutate is not None:
        start, size = mutate(start, size)
    targets = set(target for _addr, target in bl_targets(path, start, size))
    problems = []
    for callee in forbidden:
        if callee not in symbols:
            continue
        if symbols[callee][0] in targets:
            problems.append("`%s` contains a `bl` to `%s` (0x%08x): a handler entered from this image's"
                            " C code gets the saved PC derived from `lr`, which is this image's own"
                            " return address - 476's run measured that as a user-mode fetch of the `bl`"
                            " itself and a kernel panic"
                            % (name, callee, symbols[callee][0]))
    return problems, len(targets)


def calls_at_body_depth(body, name):
    """Where `name(` appears *inside* `body` rather than in its own signature.

    The distinction is the whole of experiment 477's second defect. `extract_function` returns a
    definition **including its signature**, so `void fleh_undef(void)` contains the text `fleh_undef(`
    - at brace depth zero, because the body's `{` comes after it. A plain search for "does this file
    mention `fleh_undef(`" therefore answers *yes* for every file that defines `fleh_undef`, and it did:
    477's first build printed `the split is wrong: fleh_undef's source calls fleh_undef` **after** the
    image and the header had been written, so the run that followed used an image whose build had
    reported a failure, and the failure was a property of the pattern and not of the code. The
    control that says so is in `--selftest`: the real source must pass this, and a call inserted
    inside the body must not.

    A call inside a function is always at depth >= 1, so the depth is what separates the two - and it
    is computed over the *comment- and string-blanked* text (`strip_comments`), so a brace or a name in
    prose cannot move it. The text is expected to be a single definition, so its braces balance within
    it; a `}` before a `{` would not crash this, it would only make an earlier position look shallower.
    """
    pattern = re.compile(r"([{}])|\b%s\s*\(" % re.escape(name))
    depth = 0
    found = []
    for match in pattern.finditer(body):
        brace = match.group(1)
        if brace == "{":
            depth += 1
        elif brace == "}":
            depth -= 1
        elif depth >= 1:
            found.append(match.start())
    return found


def check_split(elf, symbols=None, source_text=None, user_lit_mutation=None, call_mutation=None):
    words = elf_words(elf)
    if symbols is None:
        symbols = elf_symbols(elf)
    problems = []
    if HANDLER not in symbols or APPLE_HANDLER not in symbols:
        fail("the image is missing %s or %s, so nothing can be compared" % (HANDLER, APPLE_HANDLER))
    here = symbols[HANDLER][0]
    apple = symbols[APPLE_HANDLER][0]
    if here == apple:
        problems.append("`%s` and `%s` are both at 0x%08x, so the two halves of the split cannot be"
                        " told apart" % (HANDLER, APPLE_HANDLER, here))
    for literal, want, want_name in ((VEC_KERNEL_LIT, here, HANDLER),
                                     (VEC_USER_LIT, apple, APPLE_HANDLER)):
        if literal not in symbols:
            fail("the image has no `%s` literal, so the vector page's split cannot be read" % literal)
        got = words.get(symbols[literal][0])
        if got is None:
            fail("no word of the image covers 0x%08x, so `%s`'s value cannot be read"
                 % (symbols[literal][0], literal))
        if user_lit_mutation is not None and literal == VEC_USER_LIT:
            got = user_lit_mutation(got)
        if got != want:
            problems.append("`%s` holds 0x%08x and not %s (0x%08x): the vector page is not routing that"
                            " mode where this step says it does" % (literal, got, want_name, want))
    problems += check_calls_no_handler(elf, symbols, mutate=call_mutation)[0]
    if source_text is not None:
        body = strip_comments(extract_function(source_text, HANDLER, static=False))
        for callee in FORBIDDEN_CALLEES:
            if calls_at_body_depth(body, callee):
                problems.append("`%s`'s source calls `%s`: the image's own code would be entering a"
                                " handler whose frame arithmetic assumes a vector entry" % (HANDLER, callee))
    return problems, here, apple


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", default="src/entry/entry_stubs.c")
    parser.add_argument("--elf", default="out/stage90/xnu_arm_entry.elf")
    parser.add_argument("--guard", action="store_true", help="run the guard's case table on the host")
    parser.add_argument("--split", action="store_true",
                        help="check the vector page's slot-1 split and that the C handler calls no handler")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if not (args.guard or args.split or args.selftest):
        parser.error("one of --guard, --split or --selftest is required")

    with open(args.source) as handle:
        text = handle.read()

    if args.selftest:
        problems, rows = run_guard_cases(text)
        if problems:
            fail("the source's own guard does not answer the case table: %s" % "; ".join(problems))

        def unzero(page_text):
            return page_text.replace("if (args == 0u)", "if (args == 1u)")

        problems, _rows = run_guard_cases(text, mutate=unzero)
        if not any("0x00000000" in problem for problem in problems):
            fail("--selftest: `args == 0u` rewritten to `args == 1u` was still accepted")

        # (1) the two handlers made one address: the user literal cannot be Apple's body then.
        def merge(symbols):
            merged = dict(symbols)
            merged[APPLE_HANDLER] = symbols[HANDLER]
            return merged

        problems, _here, _apple = check_split(args.elf, symbols=merge(elf_symbols(args.elf)),
                                              source_text=None)
        if not any("both at" in problem for problem in problems):
            fail("--selftest: the two handler addresses made equal were not refused")

        # (2) the user literal pointed back at this image's handler.
        def point_home(value):
            return elf_symbols(args.elf)[HANDLER][0]

        problems, _here, _apple = check_split(args.elf, user_lit_mutation=point_home, source_text=None)
        if not any(VEC_USER_LIT in problem for problem in problems):
            fail("--selftest: slot 1's user literal pointed at this image's handler was not refused")

        # (3) the call scan pointed at a function that *does* call something - `fleh_irq` is
        # `entry_epilogue("exception: irq")` and nothing else - which the scan must refuse when the
        # name it calls is forbidden. **`fleh_irq` and not Apple's `locore_fleh_dataabt`**, which is
        # where this control pointed until 477's own build: that one has no size, `handler_range`
        # guessed 16 bytes from the label inside its body, and the control read zero `bl`s and failed
        # for the wrong reason. A function whose size `nm` knows is what makes the control mean
        # something, and the `scanned >= 1` below is the second half - a scan that read nothing must
        # never be able to look like a clean one.
        problems, scanned = check_calls_no_handler(args.elf, elf_symbols(args.elf),
                                                  name="fleh_irq",
                                                  forbidden=("entry_epilogue",))
        if not problems or scanned < 1:
            fail("--selftest: the call scan cleared a function that does call its forbidden name"
                 " (%d `bl`s were read)" % scanned)

        # (4) and the same scan over the real handler must be clean, so (3) is a positive control.
        problems, scanned = check_calls_no_handler(args.elf, elf_symbols(args.elf))
        if problems:
            fail("--selftest: the real handler fails the call scan: %s" % "; ".join(problems))

        # (5) the source half's own control, both directions, because the first version of it answered
        # a question about the *definition* and refused the real source. The real source must pass; a
        # call added inside the body must not. The mutation asserts it changed the text, for the reason
        # every mutation here does: a rewrite that matched nothing would leave the control silently
        # testing the unmutated input.
        def add_call(source):
            signature = "void %s(void)\n{" % HANDLER
            if signature not in source:
                fail("--selftest: the source has no `%s` signature to insert a call after, so the"
                     " mutation below cannot be built" % signature.replace("\n", "\\n"))
            return source.replace(signature, signature + "\n    %s(0);" % HANDLER, 1)

        problems, _here, _apple = check_split(args.elf, source_text=text)
        if problems:
            fail("--selftest: the real source fails the split check: %s" % "; ".join(problems))

        mutated = add_call(text)
        if mutated == text:
            fail("--selftest: the source mutation did not change the text")
        problems, _here, _apple = check_split(args.elf, source_text=mutated)
        if not any("source calls" in problem for problem in problems):
            fail("--selftest: a call to `%s` inserted inside its own body was not refused - the depth"
                 " rule is not separating a call from the definition" % HANDLER)

        print("check_undef_handler: --selftest ok (the guard's %d rows; the split refused three"
              " mutations - the two handler addresses made equal, slot 1's user literal pointed home,"
              " and a call inserted into the handler's own body - and cleared the unmutated source; the"
              " call scan refused a function that does call its forbidden name and cleared the %d `bl`s"
              " of the real handler)" % (rows, scanned))
        return

    if args.guard:
        problems, rows = run_guard_cases(text)
        if problems:
            fail("the guard's case table fails:\n  " + "\n  ".join(problems))
        print("  xnu_entry_475: the panic-argument guard answers the case table (%d rows) - a NULL frame"
              " is refused, the window that fits a page is accepted and the one that straddles it is not"
              % rows)

    if args.split:
        problems, here, apple = check_split(args.elf, source_text=text)
        if problems:
            fail("the split is wrong:\n  " + "\n  ".join(problems))
        print("  xnu_entry_477: the vector page's slot 1 holds %s (0x%08x) for kernel mode and %s"
              " (0x%08x) for user mode, and `%s` calls no handler - so a user udf is entered with the"
              " interrupted registers intact"
              % (HANDLER, here, APPLE_HANDLER, apple, HANDLER))


if __name__ == "__main__":
    main()
