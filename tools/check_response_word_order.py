#!/usr/bin/env python3
"""The 136-bit response has ONE word order, and it is the driver's.

WHY THIS EXISTS. `sdhci_finish_command` assembles a long response like this (the vendor's own file,
`external/android_kernel_xiaomi_cancro/drivers/mmc/host/sdhci.c:1163-1172`):

    /* CRC is stripped so we need to do some shifting. */
    for (i = 0; i < 4; i++) {
        host->cmd->resp[i] = sdhci_readl(host, SDHCI_RESPONSE + (3-i)*4) << 8;
        if (i != 3)
            host->cmd->resp[i] |= sdhci_readb(host, SDHCI_RESPONSE + (3-i)*4 - 1);
    }

Three things are load-bearing and NONE of them is obvious from the register's name: the words are read
WORD 3 FIRST (`(3-i)*4` runs 12, 8, 4, 0); each is shifted left by 8; and the stripped CRC's low byte is
OR'd back in from the address ONE BELOW the word, EXCEPT for the last word (`i != 3`), which has no byte
below it. Experimental rung 33 read a real SanDisk CID through this arithmetic, so the ladder's copy is
right. The defect this check exists for is that a copy which is WRONG here cannot fail loudly: read the
four words in ascending order, or drop the byte-below OR, and the result is still 128 plausible bits that
decode to a zeroed-out or nonsense CSD/CID - no fault, no error, no cell that says "wrong" (experiment
809, section 4). The next rung (CMD9, `SEND_CSD`, `ac R2`) is the next place a fifth copy would be
written, and 809's instruction to call the existing assembler rather than re-derive it was, until now,
only a sentence in a document.

THE RULE. Three conditions, each read off the vendor's two lines rather than invented:

  1. OFFSETS. A `ST_SDHCI_RESPONSE` access in the entry sources may use a 32-bit offset in {0,4,8,12}
     and an 8-bit offset in {3,7,11} - the vendor's `(3-i)*4` and `(3-i)*4 - 1` for i in [0,4).
  2. WORD 3 FIRST, PER COMMAND. The driver assembles `resp[]` once, when a command has completed. So the
     reads inside one stimulus segment - a maximal run of the function's response reads not broken by a
     `st_send_command(...)` - must walk the offsets STRICTLY DESCENDING. An ascending pair inside one
     segment is the defect; the ladder's own two runs inside one body (`st_cmd3_noidx` reads the four
     words before the window and the four after it) are separated by the command and are therefore two
     segments, not one. Reading the SAME offset twice inside one segment is refused for the reason
     `st_cmd3_noidx`'s own comment gives: two readings of one address at one moment are a number no cell
     can tell from the other.
  3. THE BYTE IS THE ONE BELOW ITS WORD. In `<word> << 8 | <byte>`, the byte's offset must be exactly one
     less than the word's. The word is either an inline `st_read32(... RESPONSE + K)` or a variable whose
     most recent assignment in the same function is that read. This is what makes `i != 3` structural: an
     OR onto the offset-0 word has no offset -1 to name, so it cannot be written without using a byte
     that belongs to another word.

WHAT IT REFUSES ON, AND WHAT IT ONLY PRINTS. The refusing scope is the entry sources - `src/entry/*.c` -
because that is the tree the entry image is built from and the tree the gate binds the armed arm to by
CONTENT. It is a check about the shape of code that is ABOUT to be written, so it must be green on the
tree as it stands: it is, and the six sites it reads are printed by name rather than counted.

TWO NARROWINGS, EACH MADE BY A MEASURED COUNTER-EXAMPLE RATHER THAN IN ADVANCE - the first two runs of
this tool refused four correct sites, and the rule moved, not the code:

  * COMMENTS ARE STRIPPED FIRST. A definition is recognised by the last `name(...)` before a `{`, and this
    file's comments are full of parenthesised prose - so a comment ending in a brace was read as the
    definition of a function called `29` (`rung 29 (`), which reset the enclosing body part way through
    and produced two PAIR refusals against correct code at `:3539` and `:3647`.
  * A PREPROCESSOR DIRECTIVE ENDS A SEGMENT. `st_send_command` reads the offset-0 word at `:2771` under
    `#if STAGE90_XNU_STORAGE_PROBE >= 12` and again at `:2776` under the `#else`. Those are ALTERNATIVES,
    and only one is ever compiled, so a repeated offset across a `#if`/`#else` boundary is not a second
    reading of the same address - which is the thing the rule is about.

Exit 0 when the tree is clean; 1 with every divergence printed; 2 on a usage error, and 2 for a failed
`--selftest` - the check's own width being wrong is a different failure from the tree being wrong.
"""
import glob
import os
import re
import sys

RESPONSE = "ST_SDHCI_RESPONSE"
LEGAL32 = (12, 8, 4, 0)
LEGAL8 = (11, 7, 3)
MAX_WORD = 12

# `ST_SDHCI_RESPONSE + 12u`, and the bare `ST_SDHCI_RESPONSE` the last word is read from.
OFFSET = re.compile(r"ST_SDHCI_RESPONSE\s*(?:\+\s*(\d+)u)?")
READ32 = re.compile(r"st_read32\s*\(([^;]*?)\)\s*(?:<<\s*8)?", re.S)
READ8 = re.compile(r"st_read8\s*\(([^;]*?)\)", re.S)
SHIFT = re.compile(r"<<\s*8")
FUNC = re.compile(r"^[A-Za-z_][\w \t*]*\b(\w+)\s*\([^;]*\)\s*$")
ASSIGN = re.compile(r"^\s*(\w+)\s*=\s*(.*)$", re.S)


def offset_of(text):
    """The register offset an access names, with the absent `+ 0u` meaning 0. None if not ours."""
    m = OFFSET.search(text)
    if not m or RESPONSE not in text:
        return None
    return int(m.group(1)) if m.group(1) is not None else 0


def statements(lines):
    """Join each logical C statement, so an expression split across lines is read as one."""
    out, buf, depth, start = [], [], 0, 1
    for n, line in enumerate(lines, 1):
        stripped = line.split("//")[0]
        if not buf:
            start = n
        buf.append(line)
        depth += stripped.count("(") - stripped.count(")")
        if depth <= 0 and (stripped.rstrip().endswith(";") or stripped.rstrip().endswith("{")):
            out.append((start, "\n".join(buf)))
            buf, depth = [], 0
    if buf:
        out.append((start, "\n".join(buf)))
    return out


KEYWORDS = {"if", "for", "while", "switch", "else", "do", "return", "sizeof"}
DEFN = re.compile(r"(\w+)\s*\([^()]*\)\s*$")


def strip_comments(text):
    """Blank every comment, keeping the text's line structure so line numbers stay the file's.

    WITHOUT THIS THE TOOL INVENTS FUNCTIONS. A definition is recognised by the last `name(...)` before a
    `{`, and this file's comments are full of parenthesised prose - `rung 29 (`, `**722 (rung 29)` - so a
    comment ending in a brace was read as the definition of a function called `29`, which reset the
    enclosing body mid-way and produced two PAIR refusals against correct code at :3539 and :3647. The
    comments are prose ABOUT the arithmetic; the check is about the arithmetic.
    """
    out, i, n, depth = [], 0, len(text), 0
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if c == "\n" else " " for c in text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def statements(lines):
    """Join each logical C statement, so an expression split across lines is read as one.

    A preprocessor directive is a statement of its own, because `#if`/`#else` branches are ALTERNATIVES
    and not a sequence: `st_send_command` reads the offset-0 word at :2771 under `#if >= 12` and again at
    :2776 under the `#else`, and only one of the two is ever compiled.
    """
    out, buf, depth, start = [], [], 0, 1
    for n, line in enumerate(lines, 1):
        stripped = line.split("//")[0]
        if stripped.lstrip().startswith("#"):
            if buf:
                out.append((start, "\n".join(buf)))
                buf, depth = [], 0
            out.append((n, stripped.rstrip()))
            continue
        if not buf:
            start = n
        buf.append(line)
        depth += stripped.count("(") - stripped.count(")")
        if depth <= 0 and (stripped.rstrip().endswith(";") or stripped.rstrip().endswith("{")):
            out.append((start, "\n".join(buf)))
            buf, depth = [], 0
    if buf:
        out.append((start, "\n".join(buf)))
    return out


def function_bodies(path):
    """(name, def_line, [statement text by line]) for each function definition in the file.

    The name is the identifier of the final parenthesised group before the opening brace, which is what
    survives an attribute list: `static __attribute__((noinline, noclone)) void st_resp_before(void)`
    matches `__attribute__(` first, but the group at the END of the head is `st_resp_before(void)`. A
    control statement cannot pass, because its final group is not at the end: `if (st_foo(x))` leaves a
    `)` after its last complete group, and `if (x)` is excluded by name.

    **`def_line` is the line the head's `name(...)` group is on, and it is computed, not the statement's
    first line.** `statements()` joins a logical statement, and a definition that follows a `#if
    STAGE90_XNU_STORAGE_PROBE >= N` and its documentation comment is joined with them - the comment is
    blanked by `strip_comments` but its lines are still in the statement - so the statement's first line
    is the TOP OF THAT COMMENT, up to 84 lines above the definition. Printed as `path:line name()`, that
    is a citation a reader cannot resolve: 810 and 812 both printed one, and 812's own doc quoted
    `st_send_csd()` at `:4475` when the definition is at `:4534`. The offset is a count of newlines to
    the group, so it is exact and it survives the file growing above the function.
    """
    text = strip_comments(open(path, encoding="utf-8").read())
    funcs, cur = [], None
    for n, stmt in statements(text.split("\n")):
        if "{" in stmt:
            m = DEFN.search(stmt.rsplit("{", 1)[0].rstrip())
            if m and m.group(1) not in KEYWORDS:
                cur = (m.group(1), n + stmt[: m.start(1)].count("\n"), [])
                funcs.append(cur)
                continue
        if cur is not None:
            cur[2].append((n, stmt))
    return funcs


def findings_in(name, stmts):
    """Every divergence from the three rules, as (line, tag, message)."""
    out = []
    var_offset = {}          # variable -> the offset its last response read32 came from
    segment, seen = [], set()
    for n, text in stmts:
        if "st_send_command" in text or text.lstrip().startswith("#"):
            segment, seen = [], set()
        # --- rule 1: the offsets themselves ------------------------------------------------
        for kind, rx, legal in (("read32", READ32, LEGAL32), ("read8", READ8, LEGAL8)):
            for m in re.finditer(rx, text):
                off = offset_of(m.group(1))
                if off is None:
                    continue
                if off not in legal:
                    out.append((n, "OFFSET", f"{kind} reads {RESPONSE} + {off}u, which is not one of "
                                             f"{'/'.join(str(x) for x in legal)}"))
        # --- rule 2: word 3 first, once per command ---------------------------------------
        for m in re.finditer(READ32, text):
            off = offset_of(m.group(1))
            if off is None:
                continue
            if off in seen:
                out.append((n, "REPEAT", f"the offset-{off} word is read twice inside one command "
                                         f"segment; the driver assembles resp[] once per completion"))
            elif segment and off >= segment[-1]:
                out.append((n, "ASCENDING", f"{RESPONSE} + {off}u is read after + {segment[-1]}u in the "
                                            f"same command segment; the driver reads word 3 first"))
            seen.add(off)
            segment.append(off)
        # --- the variable the next OR will name -------------------------------------------
        m = ASSIGN.match(text)
        if m and "st_read32" in text:
            off = offset_of(m.group(2))
            if off is not None:
                var_offset[m.group(1)] = off
        # --- rule 3: the byte is the one below its word -----------------------------------
        if "|" in text and "st_read8" in text:
            for m8 in re.finditer(READ8, text):
                byte = offset_of(m8.group(1))
                if byte is None:
                    continue
                word = None
                head = text[:m8.start()]
                mi = list(re.finditer(r"(\w+)\s*<<\s*8", head))
                if mi:
                    operand = mi[-1].group(1)
                    if operand in var_offset:
                        word = var_offset[operand]
                    else:
                        out.append((n, "PAIR", f"the byte at + {byte}u is OR'd onto `{operand}`, which no "
                                               f"response read in this function assigns"))
                        continue
                else:
                    m32 = list(re.finditer(READ32, head))
                    if m32:
                        word = offset_of(m32[-1].group(1))
                if word is None:
                    out.append((n, "PAIR", f"the byte at + {byte}u is OR'd with no shifted word before it"))
                elif byte != word - 1:
                    out.append((n, "PAIR", f"the byte at + {byte}u is OR'd onto the word at + {word}u; the "
                                           f"stripped CRC byte belongs to the address ONE BELOW the word "
                                           f"(+ {word - 1}u)"))
    return out


def scan(roots=None):
    files = sorted(glob.glob(os.path.join(r, "*.c"))
                   for r in (roots or ["src/entry"]))
    all_of, sites, res = [], [], []
    for group in files:
        for path in group:
            text = open(path, encoding="utf-8").read()
            if RESPONSE not in text:
                continue
            all_of.append(path)
            for name, line, stmts in function_bodies(path):
                reads = [off for _, t in stmts
                         for rx in (READ32, READ8)
                         for m in re.finditer(rx, t)
                         if (off := offset_of(m.group(1))) is not None]
                if not reads:
                    continue
                sites.append((path, name, line, len(reads)))
                for n, tag, msg in findings_in(name, stmts):
                    res.append((path, n, name, tag, msg))
    return all_of, sites, res


SELFTEST = (
    # --- the shapes this tree actually holds: all four must be clean --------------------
    ("the vendor's own loop, transcribed",
     """static void a(void) {
    raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    raw1 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    raw2 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    raw3 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("w0", (raw0 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 11u));
    ST_LIVE("w1", (raw1 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 7u));
    ST_LIVE("w2", (raw2 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 3u));
    ST_LIVE("w3", raw3 << 8);
}""", 0),
    ("word 0 alone, inline",  # :3995's shape
     """static void b(void) {
    before = (st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u) << 8)
           | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 11u);
    ST_LIVE("pre", before);
}""", 0),
    ("the four raw words as a freshness witness, no OR at all",  # :4283's shape
     """static void c(uint32_t int_enable) {
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("pre0", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    ST_LIVE("pre1", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    ST_LIVE("pre2", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("pre3", raw);
    st_send_command(ST_CMD_OP_SEND_RELATIVE_ADDR, ST_MMC_RCA_1, r1, &c3);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("post0", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    ST_LIVE("post1", raw);
}""", 0),
    # --- and the ways it goes wrong, which must all be refused --------------------------
    ("the four words read in ascending order",
     """static void d(void) {
    raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    raw1 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
}""", 1),
    ("the byte taken from ABOVE the word",
     """static void e(void) {
    raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("w0", (raw0 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u));
}""", 2),
    ("the byte taken from two below",
     """static void f(void) {
    raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("w0", (raw0 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 10u));
}""", 1),
    ("the LAST word OR'd, which the vendor's i != 3 forbids",
     """static void g(void) {
    raw2 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    raw3 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("w3", (raw3 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 3u));
}""", 1),
    ("an offset the register does not have",
     """static void h(void) {
    raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 13u);
}""", 1),
    ("one word read twice inside one command segment",
     """static void i(void) {
    raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
}""", 1),
)


def selftest():
    bad = 0
    for label, text, want in SELFTEST:
        got = len(findings_in("fixture", statements(text.split("\n"))))
        if want == 0 and got != 0:
            print(f"selftest FAIL: a shape this tree holds was refused ({got} finding(s)): {label}")
            bad += 1
        elif want and got == 0:
            print(f"selftest FAIL: a wrong shape passed: {label}")
            bad += 1
    if bad:
        return 2
    print(f"selftest ok: {len(SELFTEST)} fixtures - the four shapes this tree holds are clean, and each "
          f"of the five ways the arithmetic goes wrong is refused")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if len(argv) > 1:
        print(__doc__.strip().split("\n")[0])
        print("usage: check_response_word_order.py [--selftest]")
        return 2
    files, sites, res = scan()
    for path, name, line, reads in sites:
        print(f"note    {path}:{line} {name}() reads the response register, {reads} access(es)")
    for path, n, name, tag, msg in res:
        print(f"REFUSED {path}:{n} [{tag}] in {name}(): {msg}")
    print(f"{len(files)} entry source(s) read the response register, across {len(sites)} response-reading "
          f"function(s); {len(res)} divergence(s) from the driver's word order")
    return 1 if res else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
