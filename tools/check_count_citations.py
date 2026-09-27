#!/usr/bin/env python3
"""A count quoted in two bases must agree with itself.

WHY THIS EXISTS. Every capture in `out/stage90/captures/` reports a cell as a hexadecimal word -
`xnu_live_storage_cmd1_polls=0x004db000` - and every document, record block and readiness narration that
cites one writes it twice, as `0x004db000 = 5,091,328`. The hexadecimal is the reading; the decimal is a
transcription OF it, and nothing has ever checked the transcription. Three of them were wrong at the same
time and in the same sentence, and one of them is the number the ladder's `#error` text, the record and the
readiness narration all use for CMD1's poll count (see experiment 778).

THE RULE, AND WHY IT IS NOT SIMPLY `hex == decimal`. `A = B` in this repository is not always an equality
between two bases of one value: `src/entry/build_entry.sh` writes `offset (step number)` pairs (`0x2404
(281)`), the record writes `hex = <a millisecond figure>` beside a tick count (`0x06e0953c = 6,006 ms`), and
a README index table writes `experiment number (count)`. So the rule is a CONJUNCTION of four conditions,
each of which was fixed by measuring the tree it runs on:

  1. the pair is joined by `=` or by `( ... )` with at most whitespace and backticks between;
  2. the decimal is comma-grouped (at least one `,ddd` group) - this repository writes counts >= 1000 with
     separators and writes step and experiment numbers without them, so it separates the two uses;
  3. for the parenthesised form, at least TWO groups (>= 7 digits): the one-group form is the pervasive
     `offset (step)` idiom and adding it produces 40 false findings;
  4. `h / 1000 <= d <= h * 1000`. A citation of one value in two bases differs at most by rounding; a pair
     that differs by three orders of magnitude is two different quantities wearing one `=`.

It prints the number of pairs it read and how many disagree on every run, rather than carrying a count of
its own: a tool built to catch a remembered figure must not ship with one.

WHAT IT REFUSES ON, AND WHAT IT ONLY PRINTS. The refusing scope is this repository's live bookkeeping -
`tools/`, `scripts/`, `records/`, `Makefile` and `src/` - minus `src/entry/`, which is exempt for a COST
reason and not a lane reason: an entry source is a member of the entry image's recorded source manifest, so
editing a character of a comment in one makes the gate refuse the arm a press is waiting on (763 section 5's
measured cost, applied by 768 section 8). The two owed sites there are `entry_storage.c:3054` and `:3861`,
named in 778. `docs/experiments/` is REPORT-ONLY: those files are the project's historical record, they are
superseded rather than rewritten, and a refusal there would make `make check` red forever over documents
whose value is that they say what was believed when they were written.

Exit 0 when the refusing scope is clean; 1 with every disagreement printed; 2 on a usage error.
"""
import os
import re
import subprocess
import sys

HX = r"0[xX]([0-9A-Fa-f]+)"
GAP = r"[\s`]*"
DEC = r"([0-9]{1,3}(?:,[0-9]{3})+)"
NOT_MORE_DIGITS = r"(?![0-9]|,[0-9]{3})"
RULES = (
    ("=", re.compile(HX + GAP + "=" + GAP + DEC + NOT_MORE_DIGITS)),
    ("()", re.compile(HX + GAP + r"\(" + GAP + DEC + r"[\s`]*\)")),
)
REFUSING_PREFIXES = ("tools/", "scripts/", "records/", "src/")
REFUSING_EXACT = ("Makefile",)
EXEMPT_PREFIXES = ("src/entry/",)
REPORT_ONLY_PREFIXES = ("docs/",)


def citations(text):
    """Yield (rule, matched_text, hex_value, decimal_value) for every same-quantity pair on a line."""
    for tag, pat in RULES:
        for m in pat.finditer(text):
            if tag == "()" and m.group(2).count(",") < 2:
                continue
            h = int(m.group(1), 16)
            d = int(m.group(2).replace(",", ""))
            if not (h / 1000.0 <= d <= h * 1000.0):
                continue
            yield tag, m.group(0), h, d


def tracked_files():
    out = subprocess.run(["git", "ls-files"], capture_output=True, text=True, check=True).stdout
    return [f for f in out.split("\n") if f]


def scope_of(path):
    if path.startswith(EXEMPT_PREFIXES):
        return "exempt"
    if path.startswith(REFUSING_PREFIXES) or path in REFUSING_EXACT:
        return "refuse"
    if path.startswith(REPORT_ONLY_PREFIXES):
        return "report"
    return "refuse"


def scan():
    findings = []
    pairs = 0
    for path in tracked_files():
        where = scope_of(path)
        if where == "exempt":
            continue
        try:
            with open(path, errors="replace") as fh:
                lines = fh.read().split("\n")
        except OSError:
            continue
        for n, line in enumerate(lines, 1):
            for tag, text, h, d in citations(line):
                pairs += 1
                if h != d:
                    findings.append((where, path, n, tag, text, h, d))
    return pairs, findings


SELFTEST = (
    # (text, expected number of findings)
    ("`_cmd1_polls = 0x004db000` = 5,088,000 polls", 1),
    ("the same count written correctly: 0x004db000 = 5,091,328", 0),
    ("a tick count beside a millisecond figure: `0x06e0953c` = 6,006 ms", 0),
    ("the offset-and-step idiom: 0x2404 (281)", 0),
    ("a value with no separators is not a citation: 0x18 = 24", 0),
    ("a decimal three orders below its hexadecimal is another quantity: 0x01000000 = 16,777", 0),
    ("a comma-grouped exact value is clean: 0x01000000 = 16,777,216", 0),
    ("the parenthesised form, two groups: `0x004da800` (5,088,256)", 1),
    ("the parenthesised form, one group, is the offset idiom: 0x801D9100 (4)", 0),
    ("a transposed decimal: 0x092a0bea` = 153,747,946", 1),
)


def selftest():
    bad = 0
    for text, want in SELFTEST:
        got = sum(1 for _, _, h, d in citations(text) if h != d)
        if got != want:
            print(f"selftest FAIL: {text!r} -> {got} finding(s), wanted {want}")
            bad += 1
    if bad:
        return 2
    print(f"selftest ok: {len(SELFTEST)} cells, and the four idioms that are not citations are excluded")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if len(argv) > 1:
        print(__doc__.strip().split("\n")[0])
        print("usage: check_count_citations.py [--selftest]")
        return 2
    pairs, findings = scan()
    refusing = [f for f in findings if f[0] == "refuse"]
    reporting = [f for f in findings if f[0] == "report"]
    for where, path, n, tag, text, h, d in refusing + reporting:
        print(f"{'REFUSED' if where == 'refuse' else 'note   '} {path}:{n} [{tag}] {text!r}")
        print(f"        the hexadecimal says {h:,}; the decimal says {d:,}")
    print(
        f"{pairs} count(s) quoted in two bases; {len(refusing)} disagree in the refusing scope, "
        f"{len(reporting)} in the historical record (reported, not refused)"
    )
    return 1 if refusing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
