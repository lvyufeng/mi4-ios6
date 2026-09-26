#!/usr/bin/env bash
#
# No live path literal still points at the snapshot layout.
#
# WHAT THIS IS FOR. On 2026-09-26 the repository stopped being one directory per stage and became one
# evolving tree: the live code left `stages/stage90/` for `src/` (the payload's `.c` flat, the seven
# named subtrees as `src/entry/`, `src/platform/`, `src/supply/`, `src/shims/`, `src/shims_arm/`,
# `src/firehose/`, `src/targets/`), the shell scripts for `scripts/`, the record `.txt` files for
# `records/`, and the five retained snapshots for `archive/stages/`. Every tracked file that pinned the
# old location was rewritten by hand, and "every" is a claim in a commit message. This is the check the
# project's own rule asks for instead: make the property structural, and prefer one that stops the
# build over a true sentence in a comment.
#
# **It is not a `grep` for one string, and the reason is measured.** The hand sweep that did the
# rewrite grepped for `stages/stage` and found thirteen files. Three of them were still broken
# afterwards, because `os.path.join(REPO_ROOT, "stages", "stage90", "xnu_arm_boot", ...)` spells the
# same path as **three separate arguments** and contains no `stages/stage` substring at all - so the
# build refused with `missing /mnt/data/mi4-ios6/stages/stage90/xnu_platform/stage90_platform_config_tables.c`
# while the sweep reported clean. A check that greps one spelling answers a question about that
# spelling, which is `[[mi4-one-value-two-definitions]]` with the path as the value. So this looks for
# four spellings:
#
#   1. `stages/stage<N>` - unless the occurrence is `archive/stages/stage<N>`, which is where the five
#      retained snapshots really are and which `tools/stage-archive.sh` exists to read.
#   2. `"stages", "stage<N>"` - the split-argument form above, and the one that was missed.
#   3. `xnu_arm_boot/`, `xnu_platform/`, `xnu_supply/` - the three directories that were *renamed*, so a
#      path prefix naming one of them is stale even where no `stages/` is on the line.
#   4. The bare `stage90/` prefix is deliberately NOT checked, because `stage90` is still the name of
#      the payload's stage, its `.img`, its object plan and `out/stage90/` - all of which are current.
#
# A COMMENT IS ALLOWED TO NAME THE OLD LAYOUT, and that is not a loophole. This file's own header names
# it; `src/entry/build_entry.sh` says in a sentence that it "lived in `stage90/xnu_arm_boot/`" before the
# restructure; and the experiment logs and the record's prose are records of what was run at the time.
# What must not survive is an *executable* literal, because that is the one that makes the build reach
# for a directory that is gone. So the test is per line, and the line is skipped when its first
# non-blank characters open a comment: `#`, `//`, `/*`, `*` (which covers a C block's continuation, a
# markdown bullet and a linker script's `*`) or `<!--`.
#
# **A trailing comment after code is NOT skipped, deliberately.** Deciding where a comment starts on a
# line that also has code means parsing strings and quotes, and getting that wrong in the permissive
# direction is the failure this check exists to prevent. Naming the old layout after a `#` on a code
# line is rare enough to be worth a refusal, and the fix is to drop the path or give it its own line.
#
# WHAT IT DOES NOT COVER. `docs/` is exempt: it is the historical record, and this project supersedes
# rather than edits - the four `docs/history/**` notes, the ~700 `docs/experiments/**` logs and
# `README.md` name the layout that existed when they were written, and rewriting them would destroy the
# record the repository's value rests on. `archive/` is exempt for the same reason, and `out/` is build
# output and is not tracked.
#
# Usage:
#   tools/check_stage_paths.sh            # every tracked file outside docs/, archive/ and out/
#   tools/check_stage_paths.sh PATH ...   # only these (still skipped if under an exempt root)
#   tools/check_stage_paths.sh --help
#
# Exit: 0 = no stale executable path literal. 1 = at least one, printed as
#       `path:line: [spelling] text` on stdout, then a count. 2 = usage.
#
# It reads files and writes nothing, builds nothing, and touches no device.

set -uo pipefail

SELF=$(readlink -f "${BASH_SOURCE[0]}")
REPO_ROOT=$(cd "$(dirname "$SELF")/.." && pwd)

usage() {
  awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$SELF"
}

if [[ ${1:-} == -h || ${1:-} == --help ]]; then usage; exit 0; fi

EXEMPT_RE='^(docs|archive|out|\.git)/'

files=()
if (( $# == 0 )); then
  while IFS= read -r f; do
    [[ $f =~ $EXEMPT_RE ]] && continue
    files+=("$f")
  done < <(git -C "$REPO_ROOT" ls-files)
else
  for f in "$@"; do
    f=${f#"$REPO_ROOT"/}
    [[ $f =~ $EXEMPT_RE ]] && continue
    if [[ ! -f $REPO_ROOT/$f ]]; then
      printf 'check_stage_paths: %s is not a file under %s - an absent file is not a clean one\n' "$f" "$REPO_ROOT" >&2
      exit 1
    fi
    files+=("$f")
  done
fi

# A check whose file list is empty passes by having looked at nothing, which is the shape this file's
# own subject is full of. `git ls-files` from the wrong directory is exactly how that happens.
if (( ${#files[@]} == 0 )); then
  printf 'check_stage_paths: no file to read under %s - git tracks nothing here, or the exempt roots swallowed the list\n' "$REPO_ROOT" >&2
  exit 1
fi

PAT_STAGE='stages/stage[0-9]'
PAT_SPLIT='"stages",[[:space:]]*"stage[0-9]'
PAT_OLDSUBDIR='(xnu_arm_boot|xnu_platform|xnu_supply)/'

is_comment_line() {
  local body=$1
  body=${body#"${body%%[![:space:]]*}"}
  case $body in
    '#'*|'//'*|'/*'*|'*'*|'<!--'*) return 0 ;;
    *) return 1 ;;
  esac
}

hits=0
files_read=0
for f in "${files[@]}"; do
  [[ -f $REPO_ROOT/$f ]] || continue
  files_read=$((files_read + 1))
  for spec in stage split oldsubdir; do
    case $spec in
      stage)     pat=$PAT_STAGE ;;
      split)     pat=$PAT_SPLIT ;;
      oldsubdir) pat=$PAT_OLDSUBDIR ;;
    esac
    if [[ $spec == stage ]]; then
      # Sentinel the archive first, so `archive/stages/stage85` cannot be reported while
      # `$REPO_ROOT/stages/stage90` still is. Rewriting the text keeps this to POSIX ERE, which grep
      # has everywhere, where a negative lookbehind would need `-P`.
      found=$(sed 's|archive/stages/stage|@@ARCHIVE@@/|g' "$REPO_ROOT/$f" | grep -nE "$pat")
    else
      found=$(grep -nE "$pat" "$REPO_ROOT/$f")
    fi
    [[ -n $found ]] || continue
    while IFS= read -r hit; do
      [[ -n $hit ]] || continue
      lineno=${hit%%:*}
      text=${hit#*:}
      is_comment_line "$text" && continue
      printf '%s:%s: [%s] %s\n' "$f" "$lineno" "$spec" \
        "$(printf '%s' "$text" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' | cut -c1-140)"
      hits=$((hits + 1))
    done <<< "$found"
  done
done

# --- spelling 5: a BARE name the restructure moved, and a depth arithmetic instead of a path -------
#
# **This is the spelling that got through, and it got through by having nothing to grep for.** The four
# above all name the old layout in some form; what broke the payload build after the move had no prefix
# at all. `scripts/build.sh` compiled `SOURCES=(start.S vectors.S ...)` and passed `-Wl,-T,linker.ld`,
# `scripts/xnu_link_proof.sh` set `SUPPORT_SRC=xnu_link_support.c`, `scripts/xnu_object_subset_compile.sh`
# set `SHIM_SRC=xnu_object_shims.c` and passed `-Ishims`, and `scripts/xnu_compile_graph_scan.py` computed
# `root = stage_dir.parent.parent`. Before the restructure the scripts and the payload's sources were in
# one directory, so every one of those resolved by virtue of where the script sat; the move separated them
# and not one of the literals changed. The first post-move `./build.sh` died in the link proof; the scan
# before it reported `exists=0` for all 23 candidates, which reads exactly like an empty checkout (m702,
# m698). Two of those were not even paths - `-Ishims` is a bare word and `parent.parent` is arithmetic.
#
# **So this pass asks a different question: does a name this file EXECUTES exist where the file runs from?**
# `src/`'s own top level is the list of what moved - its files, and the seven subtrees that were renamed -
# and a bare occurrence of one of them, resolved against the referring file's own directory, that points at
# nothing there is refused. Two things keep it from being a spell-checker:
#
#   * **Only names that can be a path.** A `src/` entry with a `.` in it (a file) always counts; a subtree
#     name counts only when a `/` follows it. Without that rule the words `entry`, `platform`, `targets`
#     and `supply` are English nouns and Python variables, and the pass reported ninety of them.
#   * **A quoted message is not an access.** A token inside a double-quoted string, a single-quoted string
#     or a triple-quoted one is skipped - unless the line is exactly `VAR="token"`, which is how
#     `SUPPORT_SRC="xnu_link_support.c"` was written and is the shape a message never has (a message with a
#     space in it is not a path). Markdown is exempt outright: `README.md` is prose and backticks in it are
#     markup, not command substitution.
#
# **A file whose bare names are right can say so, and the check then verifies them rather than trusting it.**
# One comment line anywhere in the file, with `src` as the base:
#
#     # check_stage_paths: bare-names-resolve-against=src
#
# and every bare name in that file must exist under `src/`. `scripts/build.sh` carries it because it
# `cd`s into the source tree before compiling; `tools/host_dt_check.sh` carries it because it passes
# `-I"$STAGE_DIR"` with `STAGE_DIR=$REPO_ROOT/src`, which is how `-include stage90.h` resolves. Without the
# marker the base is the file's own directory. A name that exists in neither is refused, so the marker
# changes where a name is looked for and cannot silence the check.

# A file whose bare names are right can carry one comment line saying so, and the check then VERIFIES
# them rather than trusting the sentence:
#
#     # check_stage_paths: bare-names-resolve-against=src
#
# In such a file every bare name must exist under that base. `scripts/build.sh` carries it because it
# `cd`s into the source tree before it compiles; `tools/host_dt_check.sh` carries it because it passes
# `-I"$STAGE_DIR"` with `STAGE_DIR=$REPO_ROOT/src`, which is how `-include stage90.h` resolves. Without
# the marker the base is the referring file's own directory. A name that exists in neither is refused, so
# the marker moves where a name is looked for and cannot silence the check.

bare_prog=$(cat <<'PY'
import os, re, sys

repo = sys.argv[1]
src = os.path.join(repo, "src")
try:
    entries = os.listdir(src)
except OSError as e:
    sys.stderr.write("check_stage_paths: cannot read %s (%s) - the base for this spelling is unreadable,"
                     " which is a refusal and not an empty list\n" % (src, e))
    sys.exit(3)

files_named = {n for n in entries if os.path.isfile(os.path.join(src, n))}
subdirs = {n for n in entries if os.path.isdir(os.path.join(src, n))}
# `tools/` was not renamed and is not a project directory under `src/`; leaving it in would make every
# `tools/` mention a hit.
subdirs -= {"tools"}

TOKEN = re.compile(r'(?<![\w/$.\-])([A-Za-z0-9_][A-Za-z0-9_.+-]*)(/?)')
ASSIGN = re.compile(r'^\s*[A-Za-z_][A-Za-z0-9_]*=("[^"$]*"|\'[^\'$]*\')\s*$')
MARKER = re.compile(r'^#\s*check_stage_paths:\s*bare-names-resolve-against=(\S+)\s*$')

# A name counts when it can BE a path: a `src/` file always, a `src/` subtree only when a `/` follows it.
# Without the second half the words `entry`, `platform`, `targets` and `supply` are English nouns and
# Python loop variables, and the pass reported ninety of them.


def code_mask(text):
    """Per-character mask: b'1' where the character is executable text, b'0' inside a comment or a
    string. Written as one forward scan rather than per line because the false positives this replaced
    were all multi-line: a C block comment whose lines do not all start with `*`, and a Python module
    docstring. The four comment forms are `#`, `//`, `/* */` and the triple quotes; the two string forms
    are the single and double quotes, with a backslash escape."""
    mask = bytearray(b'1' * len(text))
    i, n = 0, len(text)
    while i < n:
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
        elif text[i] == '#':
            j = text.find('\n', i)
            j = n if j < 0 else j
        elif text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
        elif text[i:i + 3] in ('"""', "'''"):
            q = text[i:i + 3]
            j = text.find(q, i + 3)
            j = n if j < 0 else j + 3
        elif text[i] in '"\'':
            j = i + 1
            while j < n and text[j] != text[i]:
                if text[j] == '\\':
                    j += 1
                j += 1
            j = min(j + 1, n)
        else:
            i += 1
            continue
        for k in range(i, j):
            mask[k] = 48
        i = j
    return mask


def report(rel, lineno, line):
    sys.stdout.write("%s:%d: [bare] %s\n" % (
        rel, lineno, re.sub(r'\s+', ' ', line.strip())[:140]))
    return 1


def include_path(line, at):
    """True when the token at `at` is the value of an include-path flag - `-Ishims` (glued) or
    `-I shims` (separate)."""
    if line[max(0, at - 2):at] == '-I':
        return True
    return line[:at].rstrip().endswith('-I')


def forced_spans(line, offset):
    """`VAR="token"` - a quoted LONE name, which is an access and not a message. A message with a space
    in it is not a path, and that is the whole of the distinction: `SUPPORT_SRC="xnu_link_support.c"`
    was the miss the spelling exists for."""
    m = ASSIGN.match(line)
    if not m or any(ch.isspace() for ch in m.group(1)[1:-1]):
        return set()
    return set(range(offset + line.index(m.group(1)), offset + line.index(m.group(1)) + len(m.group(1))))


hits = 0
for rel in [p for p in sys.stdin.read().split('\n') if p]:
    path = os.path.join(repo, rel)
    if not os.path.isfile(path) or rel.endswith('.md'):
        continue
    own = os.path.dirname(path)
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        continue
    lines = text.split('\n')
    base = own
    for line in lines:
        m = MARKER.match(line)
        if m:
            base = os.path.join(repo, m.group(1).rstrip('/'))
            break
    mask = code_mask(text)
    pos = 0
    for lineno, line in enumerate(lines, 1):
        forced = forced_spans(line, pos)
        for mt in TOKEN.finditer(line):
            name, slash = mt.group(1), mt.group(2)
            if name in files_named:
                target = name
            elif name in subdirs and (slash or include_path(line, mt.start())):
                # **A subtree name counts with a `/` after it OR as the value of `-I`, and the second
                # half is not decoration: `-Ishims` is one of the four literals the restructure left
                # behind, it has no `/` and no extension, and without this rule the pass that exists
                # for that bug would not fire on it.**
                target = name
            else:
                continue
            at = pos + mt.start()
            if mask[at] == 48 and at not in forced:
                continue
            if os.path.exists(os.path.join(base, target)):
                continue
            hits += report(rel, lineno, line)
        # **The glued include flag is invisible to the token scan, and it is one of the four literals
        # this spelling exists for**: in `-Ishims` the `I` is a word character, so the lookbehind that
        # stops `tools` matching inside `mytools` also stops `shims` being seen at all. It gets its own
        # scan rather than a loosened lookbehind, because loosening that one would make every
        # `name.py` inside a longer word a hit.
        for mt in re.finditer(r'-I([A-Za-z0-9_]+)', line):
            name = mt.group(1)
            if name not in subdirs:
                continue
            if mask[pos + mt.start(1)] == 48:
                continue
            if os.path.exists(os.path.join(base, name)):
                continue
            hits += report(rel, lineno, line)
        pos += len(line) + 1

sys.exit(1 if hits else 0)

PY
)

if (( ${#files[@]} > 0 )); then
  bare_out=$(python3 -c "$bare_prog" "$REPO_ROOT" <<<"$(printf '%s\n' "${files[@]}")")
  bare_rc=$?
else
  bare_out=""
  bare_rc=3
fi
if (( bare_rc > 1 )); then
  printf 'check_stage_paths: the bare-name pass could not run (exit %d) - an unreadable base is a\n' "$bare_rc"
  printf '                   refusal, because the pass would otherwise report nothing and exit 0.\n'
  exit 1
fi
if [[ -n $bare_out ]]; then
  printf '%s\n' "$bare_out"
  hits=$((hits + $(printf '%s\n' "$bare_out" | grep -c .)))
fi


if (( hits > 0 )); then
  printf '\n%d stale path literal(s). A tracked file outside docs/ and archive/ names the layout that\n' "$hits"
  printf 'was replaced on 2026-09-26 with an executable literal. Rewrite it to the current tree\n'
  printf '(src/, scripts/, records/, archive/stages/), or move the name into a comment.\n'
  exit 1
fi

printf 'check_stage_paths: %d tracked file(s) read, no executable literal names the old layout, and no bare\n' "$files_read"
printf '                    name this tree moved resolves against a directory that no longer has it.\n'
exit 0
