#!/bin/bash
# Do the two predicates that answer "may this step act on the `fastboot devices` list?" agree?
#
# WHY THIS EXISTS.  Two files decide that one question, and only one of them is in this repository:
#
#   run_and_capture.sh  `fastboot_pinned_only()`  - the HARD guard.  It refuses unless the list is
#                          exactly one non-empty line AND that line begins `4a2fe00b<TAB>`.
#   press-watcher.sh    `ambiguous()`             - the catcher's PRE-check, which decides whether
#                          the watcher fires at all, and which narrates to the operator why it is
#                          holding.  It refuses when any line that does not begin `4a2fe00b` holds a
#                          non-space character.
#
# They are not the same predicate - one counts lines, the other counts offending lines - and the
# catcher's own narration *asserts* that they agree ("A press now cannot produce a run: the runner
# refuses at its FB_AMBIG_WAIT guard").  That is a claim about a different file, written in a
# comment, next to no check: 604's class, and 626's (the claim lived in a file that was not its
# subject) arriving on the press path itself.
#
# The failure it can hide is the expensive direction.  The watcher fires only when `usable()` is
# true and `ambiguous()` is false.  If that happens on a list the runner then refuses, the run is
# spent on a refusal - the phone is parked in fastboot having booted nothing - and, worse, the
# watcher writes its commitment line BEFORE invoking the runner, so the relay's stop condition
# (`press-watcher-relay.sh`) latches and no successor catcher is ever armed.
#
# WHAT IT MEASURES, AND WHAT IT DOES NOT.  It evaluates both predicates over a fixed matrix of
# `fastboot devices` strings with `sudo` stubbed, and asserts two things: the shapes this host can
# actually produce all agree, and the whole divergence is exactly the set listed in DIVERGE below.
# So a change to either side that widens the divergence stops here instead of being found on the
# morning of a press.  It does NOT say which side is right: the runner's guard is the one that
# boots, so a divergence is a defect in the watcher's narration, not a hole in the safety property.
#
# WHAT IT CANNOT SEE.  The watcher is not a repository file - it lives in the session's job
# directory - so there is nothing here to pin its revision to.  The check therefore reports the
# watcher's sha256 and refuses when it cannot read it, rather than passing on a box where the file
# is simply absent: a check that cannot look is not a check that passed (626).
#
# Usage:
#   tools/check_ambiguity_predicates.sh                     # exits 1 on any violation
#   tools/check_ambiguity_predicates.sh --watcher PATH      # a different catcher
#   tools/check_ambiguity_predicates.sh --verbose           # print every cell, not only failures
#
# Host-side only.  It reads two shell files, stubs `sudo` with a directory of its own on PATH, and
# runs no device command of any kind.  fastboot boot only - never flash - so nothing in here can
# write to storage.

set -u

REPO_ROOT=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)
RUNNER=$REPO_ROOT/stages/stage90/run_and_capture.sh
JOB=${CLAUDE_JOB_DIR:-/home/lvyufeng/.claude/jobs/ddfef593}
WATCHER=${WATCHER:-$JOB/tmp/press-watcher.sh}
VERBOSE=0
SERIAL=4a2fe00b

while (($# > 0)); do
  case $1 in
    --watcher) WATCHER=${2:-}; shift 2 ;;
    --runner)  RUNNER=${2:-};  shift 2 ;;
    --verbose) VERBOSE=1; shift ;;
    -h|--help)
      sed -n '/^# Usage:/,/^# Host-side only/p' "$(readlink -f "${BASH_SOURCE[0]}")" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) printf 'check_ambiguity_predicates: unknown argument %s\n' "$1" >&2; exit 2 ;;
  esac
done

# Refuse rather than pass when a subject is unreadable.  A `skipped` cell is not a green one - the
# write gate (629) had to be taught the same thing, and it is the whole point of this file.
for f in "$RUNNER" "$WATCHER"; do
  [[ -r $f ]] || { printf 'REFUSING: %s is not readable, so the two predicates cannot both be\n' "$f" >&2
                   printf '          evaluated and this check has nothing to say. It is not a pass.\n' >&2
                   exit 1; }
done

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# The functions are pulled out of the REAL files, not copied.  awk counts braces rather than using a
# `sed -n '/^f()/,/^}/p'` range: `fastboot_has()` in the watcher is a ONE-LINE function, so its
# closing `}` is not at the start of a line, the range never terminates and the extractor swallows
# the rest of the file - measured, and the reason the watcher's own fixture (wtest.sh) uses awk too.
extract() {  # extract FILE OUT NAME...
  local file=$1 out=$2; shift 2
  : > "$out"
  local name depth line n m
  declare -A want=(); for name in "$@"; do want[$name]=1; done
  local infn=0
  while IFS= read -r line; do
    if [[ $infn -eq 0 ]]; then
      name=${line%%\(\)*}
      if [[ $line == "$name() {"* || $line == "$name()"* ]] && [[ -n ${want[$name]:-} ]]; then
        infn=1; depth=0
      fi
    fi
    if [[ $infn -eq 1 ]]; then
      printf '%s\n' "$line" >> "$out"
      n=$(printf '%s' "$line" | tr -cd '{' | wc -c)
      m=$(printf '%s' "$line" | tr -cd '}' | wc -c)
      depth=$(( depth + n - m ))
      if (( depth == 0 && (n + m) > 0 )); then infn=0; fi
    fi
  done < "$file"
}

extract "$RUNNER"  "$WORK/runner.fns"  fastboot_pinned_only
extract "$WATCHER" "$WORK/watcher.fns" ambiguous usable fastboot_has adb_state

for f in fastboot_pinned_only; do
  grep -q "^$f()" "$WORK/runner.fns" || { printf 'REFUSING: could not extract %s from %s.\n' "$f" "$RUNNER" >&2; exit 1; }
done
for f in ambiguous usable fastboot_has adb_state; do
  grep -q "^$f()" "$WORK/watcher.fns" || { printf 'REFUSING: could not extract %s from %s.\n' "$f" "$WATCHER" >&2; exit 1; }
done
# ...and the extractor must not have run past its last function, which is the failure mode the
# one-line function causes.  Checked by looking for a line that can only come from further down.
grep -qE '^fastboot_list_count\(\)' "$WORK/runner.fns" && { printf 'REFUSING: the runner extractor ran past fastboot_pinned_only().\n' >&2; exit 1; }
grep -qE '^(why_not|show_fb)\(\)' "$WORK/watcher.fns" && { printf 'REFUSING: the watcher extractor ran past adb_state().\n' >&2; exit 1; }

# `sudo -n <tool> devices` is replaced by a stub reading a file, which is the ONLY input either
# predicate reads.  The stub records itself, so the cells can be believed only if the stub is the
# thing that answered - the same assertion the rehearsal harness makes before it trusts a reading.
mkdir -p "$WORK/bin"
cat > "$WORK/bin/sudo" <<'STUB'
#!/bin/bash
[ "${1:-}" = -n ] && shift
case ${1:-} in
  adb)      printf '%s\n' "${ADB_OUT-}" ;;
  fastboot) printf '%s\n' "${FB_OUT-}" ;;
esac
echo "stub $*" >> "${STUB_REC:?}"
STUB
chmod +x "$WORK/bin/sudo"
STUB_REC=$WORK/rec.txt
: > "$STUB_REC"
PATH=$WORK/bin:$PATH

# shellcheck disable=SC1090
. "$WORK/runner.fns"
# shellcheck disable=SC1090
. "$WORK/watcher.fns"

TAB=$'\t'
PASS=0; FAIL=0; NDIV=0

# The cells.  Column 4 is what this check asserts: `agree` for every shape this host really
# produces, and `diverge` for the rest - so widening either predicate past its own column stops here.
cell() {  # cell NAME EXPECT(fastboot-list) ASSERT(agree|diverge) [note]
  local name=$1 expect=$2 assert=$3 note=${4-}
  FB_OUT=$expect; ADB_OUT=''
  export FB_OUT ADB_OUT STUB_REC
  local pinned=no amb=no usb=no
  fastboot_pinned_only "$expect" && pinned=yes
  ambiguous && amb=yes
  usable && usb=yes
  # what the WATCHER does with this shape: it fires only on usable && !ambiguous
  local rref=no wref=no wfire=no
  [[ $pinned == no ]] && rref=yes
  [[ $amb == yes ]] && wref=yes
  [[ $usb == yes && $amb == no ]] && wfire=yes
  local got=agree
  [[ $rref != $wref ]] && got=diverge
  [[ $got == diverge ]] && NDIV=$(( NDIV + 1 ))
  local cost=''
  [[ $wfire == yes && $rref == yes ]] && cost=' - the watcher would FIRE and the runner would refuse'
  local verdict=ok
  if [[ $got != "$assert" ]]; then
    verdict=FAIL
    [[ $assert == agree ]] && cost="$cost - and this check says the shapes this host produces must agree"
  fi
  [[ $verdict == FAIL ]] && FAIL=$(( FAIL + 1 )) || PASS=$(( PASS + 1 ))
  if [[ $VERBOSE -eq 1 || $verdict == FAIL ]]; then
    printf '  %-5s %-40s pinned=%-3s bound=%-3s fire=%-3s -> %-7s%s%s\n' \
      "$verdict" "$name" "$pinned" "$wref" "$wfire" "$got" "$cost" "${note:+   ($note)}"
  fi
  [[ $verdict == ok ]]
}

printf 'check_ambiguity_predicates: %s\n' "$REPO_ROOT"
printf '  runner  %s\n' "$RUNNER"
printf '  watcher %s  sha256 %s\n' "$WATCHER" "$(sha256sum "$WATCHER" | cut -d' ' -f1)"
printf '  the two predicates, verbatim:\n'
sed 's/^/    runner  | /' "$WORK/runner.fns"
sed 's/^/    watcher | /' "$WORK/watcher.fns"
printf '\n'

# --- the shapes this host really produces (assert: agree) -------------------------------------
cell 'phone alone (the state a run needs)'      "$SERIAL${TAB}fastboot"                   agree
cell 'neighbour alone - TODAY, 2026-09-23'      "33e80afe${TAB}fastboot"                  agree
cell 'both, phone first'                        "$SERIAL${TAB}fastboot
33e80afe${TAB}fastboot"                                                                   agree
cell 'both, neighbour first'                    "33e80afe${TAB}fastboot
$SERIAL${TAB}fastboot"                                                                    agree

# --- shapes this host cannot produce today (assert: diverge, and each is a known defect) --------
cell 'empty list'                               ''                                        diverge 'inert: usable is false, so the watcher cannot fire'
cell 'phone, the same line twice'               "$SERIAL${TAB}fastboot
$SERIAL${TAB}fastboot"                                                                    diverge
cell "phone + a serial carrying phone's prefix" "$SERIAL${TAB}fastboot
${SERIAL}X${TAB}fastboot"                                                                 diverge
cell "only a serial carrying phone's prefix"    "${SERIAL}X${TAB}fastboot"                diverge
cell 'phone, no state column'                   "$SERIAL"                                 diverge
cell 'phone + a whitespace-only line'           "$SERIAL${TAB}fastboot

 "                                                                                        diverge

[[ $(wc -l < "$STUB_REC") -gt 0 ]] \
  || { printf '\nREFUSING: the sudo stub was never consulted, so no cell above ran against the\n' >&2
       printf '          predicates - a green table from a stub that never answered is not a reading.\n' >&2
       exit 1; }

printf '\n'
if (( FAIL > 0 )); then
  printf 'REFUSING: %d of %d cell(s) did not behave as asserted (%d ok).\n' "$FAIL" "$(( PASS + FAIL ))" "$PASS"
  printf '          The predicates disagree on a shape this check says they must not, or one of them\n'
  printf '          changed on a shape listed above. Both are defects in what the watcher SAYS -\n'
  printf '          the runner is the guard that boots, and it is the stricter of the two.\n'
  exit 1
fi
printf 'ok: %d cell(s). The two predicates agree on every shape this host can produce, and diverge on\n' "$PASS"
printf '    the %d listed shapes it cannot (§ 1 of experiment-634). The runner is the stricter side in\n' "$NDIV"
printf '    every one of them, so the divergence costs a fired-and-refused run and never a wrong boot.\n'
exit 0
