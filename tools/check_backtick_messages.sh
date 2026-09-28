#!/usr/bin/env bash
#
# Refuse an unescaped backtick, and a `$(` written inside a backtick-quoted code span, in a
# double-quoted string in a shell script.
#
# Why this exists
# ---------------
# **m730/m743/m750's class, and this is the check that makes the repair belong to the class rather
# than to the file where it was found.** Inside a double-quoted shell string a backtick is a COMMAND
# SUBSTITUTION, not punctuation:
#
#   * m730: the rung-14 narration paragraph of `tools/verify_press_ready.sh` was written with five
#     unescaped backticks, so five key names were executed and the row that said `ok` printed
#     `line 829: _cmd_gate_kind: command not found` - **the refusal fired and its explanation had a
#     hole**, and because the count was even the file still parsed, so nothing was loud.
#   * m743: the same mechanism reintroduced by the step that wrote a sentence ABOUT that refusal;
#     its repair was a check on the lines that BUILD `verify_press_ready.sh`'s narration.
#   * m750: a commit message passed with `-m "..."`, where the whole 39-cell rehearsal battery RAN.
#   * **m752: `src/entry/build_entry.sh`, which is the file next door.** Nineteen lines in that file
#     carried prose backticks - symbol names, register names, a file:line citation - and the two that
#     were OBSERVED to fire were both in a clause's own SUCCESS report: rung 18's `xnu_entry_739`
#     clause printed `so 738's doubt (is  the card's or the block's?)` where the sentence says
#     `is 0x40ff8080 the card's or the block's?`, and rung 19's `xnu_entry_741` clause would have
#     lost `mmc.c:1409` the same way. **A report that succeeds with a word missing from it is the
#     failure this check exists to stop**, and it was found by rung 20's build.
#   * **m806: `tools/verify_press_ready.sh` again, one character over from m730 and in the paragraph
#     that m730's own class is ABOUT.** The rung-34 narration quoted a shell idiom as code -
#     `\`x=$(... | grep ...)\`` - and the `$` was not escaped, so the shell RAN it: the readiness
#     tool printed `line 1256: ...: command not found` immediately above its verdict table, and the
#     sentence came out as `and \`x=\` fails the same way` - **the clause's own record of a defect
#     whose text was destroyed by that defect.** Escaping a BACKTICK quotes a name; it does not
#     quote a `$(`, and a code span is exactly where a reader assumes substitution is off.
#
# The rule, and it is deliberately strict
# ---------------------------------------
# On any non-comment line of a tracked shell script, an unescaped backtick inside a double-quoted
# string is a REFUSAL. Two escapes are correct and both pass:
#
#   \`   a literal backtick in the message, which is what a name being quoted wants
#   '`'  a backtick inside SINGLE quotes, where the shell substitutes nothing
#
# **A backtick outside a double-quoted string is not this check's subject** - it is an ordinary
# command substitution and the scripts here use it freely.
#
# **And a `$(` inside a `\`...\`` code span, in a double-quoted string, is a REFUSAL too** (m806).
# The discriminator is the span and not the `$(`, deliberately: a narration line legitimately
# INTERPOLATES - `bad 'the park verifies...' "$(printf ...)"` computes part of the sentence - so a
# rule against `$(` in double quotes would refuse correct code and would be turned off. A code span
# is quoted CODE: nothing inside one is meant to be substituted, so a substitution there always
# destroys the text it was quoting. `\$(` is the escape and it prints the idiom as written.
#
# **`$(( ))` IS EXEMPT, AND THE WIDTH WAS FIXED BY MEASURING A COUNTER-EXAMPLE** (`src/entry/
# build_entry.sh:32339`, and this is the m775 discipline: narrow a rule because something outside it
# was read, never because the check was inconvenient). That line writes
# `\`cmp #${sxw_cimm:-none}\` ... \`cmp #$((SEAM_POST_END_RUN - 1))\`` and it is CORRECT: with
# `SEAM_POST_END_RUN=18` it prints `either \`cmp #18\` with \`bcs\`/\`bhs\`, or the folded \`cmp #17\`
# with \`bhi\``, which is the sentence's own intent, and its siblings on the same line
# (`$SEAM_POST_END_RUN`, `${sxw_cimm:-none}`, `${sxw_caddr:-?}`) are interpolations the message is
# built from. **So a code span in this project's messages is a quoted TOKEN, not a no-substitution
# zone** - what stands out is a COMMAND being run from prose, which is the m730/m752/m806 family.
# The rule is deliberately narrow: `$( ... )` is refused, `$(( ... ))` is not. **One known FALSE
# POSITIVE is stated rather than left to be discovered**: in the `Makefile`, a `$` that reaches the
# shell is written `$$`, so a `\`$$( )\`` code span - correct make, correct shell - is reported as a
# hit, because the scanner reads the make-level text. It fired on the first draft of `make help`'s
# own new sentence and the sentence was reworded instead of the rule; a future author who really
# needs that span should measure the case before widening anything (m775).
#
# **`--selftest` runs the scanner over named fixtures and exits 2 if its width is wrong**, because a
# rule whose refusals have never been OBSERVED is not a check. The fixtures pin both directions: the
# m730 shape and the m806 shape must be REFUSED, and the escapes, the `$((` counter-example, a
# `$(` outside a span, a `$(` inside a SPAN that sits in single quotes, and a backtick inside single
# quotes must all PASS. Narrowing the rule any further fails the selftest, which is what keeps the
# measured counter-example above from being a sentence instead of a test.
#
# What it scans
# -------------
# Every tracked `*.sh` plus the `Makefile`, read with a small state machine rather than a regex over
# the whole line, because `"` inside a single-quoted span and `\"` inside a double-quoted one are
# the two ways a line-level regex gets the span boundaries wrong. **Files are named as they are
# tracked**, so a new script is covered the day it is added.
#
# **Heredoc bodies are SKIPPED**, and that is a deliberate limit rather than an oversight: a
# `<<'PY'` body is another language's text - python, C, awk - where the shell's double-quote rule
# does not apply, and the three instances this scanner first reported were all python docstrings
# inside such a body. A check that cannot tell the two languages apart would refuse correct code.
# The cost is stated: an unquoted heredoc's body is not scanned, so a backtick written there is this
# check's blind spot. The producers of this project's reports are `echo` and `printf` lines, not
# heredocs.
# Exit status: 0 nothing to refuse, 1 at least one line to refuse, 2 the scanner could not run OR its
# own selftest failed (a missing file list is a refusal, never a pass).

set -uo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$REPO_ROOT" || { echo "check_backtick_messages: cannot enter $REPO_ROOT" >&2; exit 2; }

SCANNER=$(cat <<'PY'
import io, re, sys

MODE = sys.argv[1] if len(sys.argv) > 1 else 'files'

def scan(line):
    """Two hazards inside a double-quoted span, returned as (backtick_indices, substitution_indices).

    The first is an unescaped backtick anywhere in the span. The second is a `$(` written INSIDE a
    backtick-quoted code span, which is m806: the span is quoted code, so the shell running it both
    loses the text and executes it. `$((` is EXEMPT and the header carries the measured
    counter-example that fixed that width (`src/entry/build_entry.sh:32339`): arithmetic yields a
    VALUE, and these messages interpolate values inside code spans on purpose, while a command
    substitution inside one runs a command from prose."""
    bt, sub = [], []
    i, n, indq, insq, span = 0, len(line), False, False, False
    while i < n:
        ch = line[i]
        if ch == '\\' and i + 1 < n:
            if indq and line[i + 1] == '`':
                span = not span          # \` opens or closes a code span inside the prose
            i += 2
            continue
        if ch == "'" and not indq:
            insq = not insq; i += 1; continue
        if ch == '"' and not insq:
            indq = not indq; i += 1; span = False; continue
        if ch == '`' and indq:
            bt.append(i)
        if (ch == '$' and indq and span and i + 1 < n and line[i + 1] == '('
                and not (i + 2 < n and line[i + 2] == '(')):
            sub.append(i)
        i += 1
    return bt, sub

# (why, line as it appears in a script, expected unescaped backticks, expected `$(` in a code span)
FIXTURES = [
    ('m730: an unescaped backtick in prose',            'x="a `b` c"',                             2, 0),
    ('the same line with both escaped',                 'x="a \\`b\\` c"',                         0, 0),
    ('m806: `$(` inside a code span',                   'x="p \\`q=$(... | grep ...)\\` r"',       0, 1),
    ('m806 escaped: `\\$(` inside a code span',         'x="p \\`q=\\$(... | grep ...)\\` r"',    0, 0),
    ('the measured counter-example: arithmetic runs',   'ok "f \\`cmp #$((N - 1))\\` with \\`bhi\\`"', 0, 0),
    ('an interpolation OUTSIDE any span',               'bad "x" "count: $(wc -l < f)"',           0, 0),
    ('a `$(` after the span closed',                    'x="a \\`b\\` $(c)"',                      0, 0),
    ('a backtick inside SINGLE quotes',                 "y='a `b` c'",                             0, 0),
    ('a span inside SINGLE quotes substitutes nothing', "bad 'x' 'a \\`b=$(c)\\` d'",             0, 0),
]

def selftest():
    wrong = 0
    for why, line, want_bt, want_sub in FIXTURES:
        bt, sub = scan(line)
        good = (len(bt) == want_bt and len(sub) == want_sub)
        print('  %-4s %-46s backtick %d/%d  code-span $( %d/%d'
              % ('ok' if good else 'FAIL', why, len(bt), want_bt, len(sub), want_sub))
        if not good:
            wrong += 1
    print('__SELFTEST__ %d fixture(s), %d wrong' % (len(FIXTURES), wrong))
    return 1 if wrong else 0

if MODE == 'selftest':
    sys.exit(selftest())

paths = io.open(sys.argv[2], encoding='utf-8').read().split('\0')
paths = [p for p in paths if p]
if not paths:
    print('the file list read back empty')
    sys.exit(1)

HEREDOC = re.compile(r"<<-?\s*[\"']?([A-Za-z_][A-Za-z0-9_]*)['\"]?")

bad = 0
for path in paths:
    try:
        text = io.open(path, encoding='utf-8', errors='replace').read()
    except OSError as exc:
        print('%s: cannot read (%s)' % (path, exc)); bad += 1; continue
    pending = []        # heredoc delimiters opened on the current/previous line
    for num, line in enumerate(text.split('\n'), 1):
        if pending:
            # Inside a heredoc body: another language's text (python, C, awk), where the shell's
            # double-quote rule does not apply. Skipped, and named as skipped in the header.
            if line.strip() == pending[0]:
                pending.pop(0)
            continue
        s = line.strip()
        if not s or s.startswith('#'):
            continue
        for m in HEREDOC.finditer(line):
            pending.append(m.group(1))
        hits, subs = scan(line)
        if hits:
            frag = s if len(s) <= 140 else s[:137] + '...'
            print('%s:%d: %d unescaped backtick(s) inside a double-quoted string: %s'
                  % (path, num, len(hits), frag))
            bad += 1
        if subs:
            frag = s if len(s) <= 140 else s[:137] + '...'
            print('%s:%d: %d `$(` inside a backtick-quoted code span, in a double-quoted string: %s'
                  % (path, num, len(subs), frag))
            bad += 1
print('__SCANNED__ %d' % len(paths))
sys.exit(0 if bad == 0 else 1)
PY
)

if [[ ${1:-} == --selftest ]]; then
    out=$(printf '%s\n' "$SCANNER" | python3 - selftest); rc=$?
    printf '%s\n' "$out"
    nst=$(printf '%s\n' "$out" | grep -c '^__SELFTEST__ ' || true)
    if [[ $rc -ne 0 || "$nst" != "1" ]]; then
        echo "check_backtick_messages: SELFTEST FAILED - the scanner's own width is wrong, so its"
        echo "  verdicts about the tree are not evidence. This is a refusal about the CHECK, not"
        echo "  about the tree, and it is exit 2 rather than 1 for that reason." >&2
        exit 2
    fi
    exit 0
fi

LIST=$(mktemp) || { echo "check_backtick_messages: mktemp failed" >&2; exit 2; }
trap 'rm -f "$LIST"' EXIT

if ! git ls-files -z -- '*.sh' 'Makefile' > "$LIST"; then
    echo "check_backtick_messages: git ls-files could not produce the file list" >&2; exit 2
fi
if [[ ! -s "$LIST" ]]; then
    echo "check_backtick_messages: the tracked file list is EMPTY - a refusal, not a pass" >&2; exit 2
fi

out=$(printf '%s\n' "$SCANNER" | python3 - files "$LIST")
rc=$?

if [[ $rc -eq 2 ]]; then
    echo "check_backtick_messages: the scanner could not run" >&2; exit 2
fi

scanned=$(printf '%s\n' "$out" | grep -c '^__SCANNED__ ')
if [[ "$scanned" != "1" ]]; then
    echo "check_backtick_messages: the scanner produced no completion line - a refusal, not a pass" >&2
    printf '%s\n' "$out" >&2
    exit 2
fi
nfiles=$(printf '%s\n' "$out" | sed -n 's/^__SCANNED__ //p')

if [[ $rc -ne 0 ]]; then
    printf '%s\n' "$out" | grep -v '^__SCANNED__ '
    echo
    echo "check_backtick_messages: REFUSED. An unescaped backtick inside a double-quoted shell string"
    echo "  is a COMMAND SUBSTITUTION: the name vanishes from the message and the shell tries to run"
    echo "  it, so a refusal that fires has a hole in its explanation and a report that succeeds"
    echo "  prints a sentence with a word missing. Escape it (a backslash before the backtick) if the"
    echo "  message means a literal backtick, or single-quote it."
    echo "  And a \`\$(...)\` inside a backtick-quoted code span is the same substitution through the"
    echo "  other escape: quoting a NAME with backticks does not quote a \`\$(...)\` written beside it."
    echo "  \`\\\$(...)\` prints the idiom as written."
    exit 1
fi

echo "check_backtick_messages: ok - $nfiles tracked shell file(s); no unescaped backtick, and no \`\$(\` inside a code span, in any double-quoted string."
