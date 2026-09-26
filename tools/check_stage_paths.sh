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

if (( hits > 0 )); then
  printf '\n%d stale path literal(s). A tracked file outside docs/ and archive/ names the layout that\n' "$hits"
  printf 'was replaced on 2026-09-26 with an executable literal. Rewrite it to the current tree\n'
  printf '(src/, scripts/, records/, archive/stages/), or move the name into a comment.\n'
  exit 1
fi

printf 'check_stage_paths: %d tracked file(s) read, no executable literal names the old layout.\n' "$files_read"
exit 0
