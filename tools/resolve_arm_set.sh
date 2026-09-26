#!/usr/bin/env bash
#
# What recorded set are the bytes in this directory?
#
# ONE QUESTION, ONE DEFINITION. Three places need to know which arm the bytes in `out/stage90` are, and
# before this file existed they answered it three ways: `tools/verify_press_ready.sh` scanned the record
# inline (667), `run_and_capture.sh` scanned it inline again (668), and `tools/rehearse_live_path.sh`
# needed the same answer to build its `--expect-arm`. Three copies of one rule is this project's most
# repeated defect class, and the failure it produces here is the quiet one - two of the three agreeing
# and the third resolving a different set, on the file whose whole job is to name the arm a press sends.
#
# **It resolves by HASH and not by name**, and the direction is the point: the caller does not know the
# name, that is why it is asking. The record's own `role=` sentences cannot answer it - three sets in
# `revert-set.txt` claim in their role text to be "the arm the next press sends" (measured 667), and a
# later step supersedes such a sentence rather than editing it. So the artifact is hashed: the file
# named below is the one `fastboot boot` sends.
#
# Usage:
#   tools/resolve_arm_set.sh DIR                     # DIR is the live arm's directory
#   tools/resolve_arm_set.sh DIR --file NAME         # resolve a different recorded member
#   tools/resolve_arm_set.sh DIR --record PATH       # a different record
#   tools/resolve_arm_set.sh --help
#
# Output, on success, one tab-separated line on stdout:
#   <set><TAB><sha256><TAB><bytes>
# and nothing else - so `cut -f1` is the name and the other two fields are available to a caller that
# wants to print what it compared.
#
# Exit: 0 = exactly one recorded set's member is those bytes. 1 = refused, with a reason on **stderr**
#       that names what it found (no such file, no such set, two sets, unreadable record) - never a
#       bare failure and never a guess. 2 = usage, from the argument loop only.
#
# **A refusal is not a silence.** Every exit-1 path prints a sentence: a tool that answers "which arm is
# this" with an empty string and a non-zero status is indistinguishable from one that crashed, and the
# caller's next act is to spend a press.

set -uo pipefail

SELF=$(readlink -f "${BASH_SOURCE[0]}")
REPO_ROOT=$(cd "$(dirname "$SELF")/.." && pwd)

DIR=""
MEMBER=stage90-qcdt.img
RECORD=$REPO_ROOT/records/revert-set.txt

while (($# > 0)); do
  case $1 in
    --file)      MEMBER=${2:-}; shift 2 ;;
    --record)    RECORD=${2:-}; shift 2 ;;
    -h|--help)
      awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$SELF"
      exit 0 ;;
    --*) printf 'resolve_arm_set: unknown option %s\n' "$1" >&2; exit 2 ;;
    *) if [[ -z $DIR ]]; then DIR=$1; else printf 'resolve_arm_set: unexpected argument %s\n' "$1" >&2; exit 2; fi; shift ;;
  esac
done

if [[ -z $DIR ]]; then
  printf 'resolve_arm_set: usage: resolve_arm_set.sh DIR [--file NAME] [--record PATH]\n' >&2
  exit 2
fi

# `kv_of KEY LINE` - the record's line format, parsed by NAME. The record is not uniform (its
# `SHA256SUMS.txt` line carries an extra key before `role=`), so a positional read is wrong by
# construction; and the word-split runs under `set -f`, so a `*` inside a role text cannot be
# pathname-expanded against the current directory - which would silently drop the token this reads.
kv_of() {
  local t out=''
  set -f
  for t in $2; do
    case $t in "$1"=*) out=${t#*=}; break ;; esac
  done
  set +f
  printf '%s' "$out"
}

if [[ ! -r $RECORD ]]; then
  printf 'resolve_arm_set: %s is not readable, so no set can be resolved from it at all - this is a check that could not look, not a set that is absent\n' "$RECORD" >&2
  exit 1
fi
if [[ ! -f $DIR/$MEMBER ]]; then
  printf 'resolve_arm_set: %s does not exist under %s, so there are no bytes to resolve - an absent file is not a failed read\n' "$MEMBER" "$DIR" >&2
  exit 1
fi

sha=$(sha256sum "$DIR/$MEMBER" | cut -d' ' -f1)
bytes=$(stat -c '%s' "$DIR/$MEMBER")

hits=()
all_sets=()
while read -r line; do
  # Every line of the record that is about this member. A line with no `set=` key is skipped rather
  # than read as the empty set name: the record's own header comment shows a `set=` example at the
  # start of a line (`#   set=      which revert set...`), and a comment is not a set.
  case $line in '#'*) continue ;; esac
  s=$(kv_of set "$line")
  [[ -n $s ]] || continue
  case " ${all_sets[*]-} " in *" $s "*) ;; *) all_sets+=("$s") ;; esac
  [[ $(kv_of file "$line") == "$MEMBER" ]] || continue
  [[ $(kv_of sha256 "$line") == "$sha" ]] || continue
  case " ${hits[*]-} " in *" $s "*) ;; *) hits+=("$s") ;; esac
done < "$RECORD"

if (( ${#hits[@]} == 1 )); then
  printf '%s\t%s\t%s\n' "${hits[0]}" "$sha" "$bytes"
  exit 0
fi

if (( ${#hits[@]} == 0 )); then
  # Name the sets the record DOES have, so the reader can see which name is wrong rather than guess.
  # **The list is built by the same `kv_of` the matching above uses, and that is not tidiness.** The
  # first version read it with `awk '$1 ~ /^set=/ { sub(...); print }'`, where `$1` is the *first field*
  # of a line that begins `set=NAME sha256=...` - so the pattern matched and `print` emitted the WHOLE
  # line, 36 of them, in the middle of a refusal whose subject was a single hash. Measured 2026-09-25 on
  # the first truncated-copy test. Two parsers of one line format is two definitions of it, and the
  # second one was wrong in the direction that looks like information.
  printf 'resolve_arm_set: %s/%s hashes to %s, which is the %s of no set in %s (the record has: %s) - either the name of the member is wrong, or the tree was rebuilt and the new bytes were never recorded\n' \
    "$DIR" "$MEMBER" "$sha" "$MEMBER" "$RECORD" "${all_sets[*]:-none}" >&2
  exit 1
fi

printf 'resolve_arm_set: %s/%s (%s) is recorded under %d set names (%s) - one artifact in two sets, so which ARM those bytes are is not something this can read\n' \
  "$DIR" "$MEMBER" "$sha" "${#hits[@]}" "${hits[*]}" >&2
exit 1
