#!/usr/bin/env bash
#
# Refuse an unescaped backtick inside a double-quoted string in a shell script.
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
# Exit status: 0 nothing to refuse, 1 at least one line to refuse, 2 the scanner could not run (a
# missing file list is a refusal, never a pass).

set -uo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$REPO_ROOT" || { echo "check_backtick_messages: cannot enter $REPO_ROOT" >&2; exit 2; }

LIST=$(mktemp) || { echo "check_backtick_messages: mktemp failed" >&2; exit 2; }
trap 'rm -f "$LIST"' EXIT

if ! git ls-files -z -- '*.sh' 'Makefile' > "$LIST"; then
    echo "check_backtick_messages: git ls-files could not produce the file list" >&2; exit 2
fi
if [[ ! -s "$LIST" ]]; then
    echo "check_backtick_messages: the tracked file list is EMPTY - a refusal, not a pass" >&2; exit 2
fi

out=$(python3 - "$LIST" <<'PY'
import io, re, sys

def scan(line):
    """Indices of unescaped backticks sitting inside a double-quoted span."""
    out, i, n, indq, insq = [], 0, len(line), False, False
    while i < n:
        ch = line[i]
        if ch == '\\' and i + 1 < n:
            i += 2
            continue
        if ch == "'" and not indq:
            insq = not insq; i += 1; continue
        if ch == '"' and not insq:
            indq = not indq; i += 1; continue
        if ch == '`' and indq:
            out.append(i)
        i += 1
    return out

paths = io.open(sys.argv[1], encoding='utf-8').read().split('\0')
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
        hits = scan(line)
        if hits:
            frag = s if len(s) <= 140 else s[:137] + '...'
            print('%s:%d: %d unescaped backtick(s) inside a double-quoted string: %s'
                  % (path, num, len(hits), frag))
            bad += 1
print('__SCANNED__ %d' % len(paths))
sys.exit(0 if bad == 0 else 1)
PY
)
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
    exit 1
fi

echo "check_backtick_messages: ok - $nfiles tracked shell file(s), no unescaped backtick in any double-quoted string."
