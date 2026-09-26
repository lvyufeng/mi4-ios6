#!/bin/bash
# verify_baseline_readings.sh - re-derive every reading in records/baseline-readings.txt from the
# log it names, and refuse on any difference.
#
# WHY THIS EXISTS. The sleeper clause of `run_and_capture.sh` prints thresholds and premises that are
# **measured on the four baseline captures** - rung 0's `door_max > 32768` rests on `0x8000` being the
# last door record of both baselines, rung 1's falsifier rests on their `poll_seq` stopping at 2, rung
# 1b's threshold on their largest ask being 40 ms, and 598's and 600's clauses on 520 having no
# `slot_cwe_` key at all and on 513's pair being the counterexample. Every one of those numbers was a
# claim in a comment with nothing able to check it (604's rule: a claim in a comment is not a check).
# This tool is the check. It is the same shape as `tools/verify_revert_set.sh`, and the record is the
# identity - the logs are outside version control, so a verifier run where the copies are gone says so
# rather than pretending.
#
# WHAT IT READS, AND THE ONE THING IT DOES NOT. It reads the record and the logs, and it writes
# nothing. The log directory is `--dir=DIR` or a bare first argument (default `out/stage90/captures`,
# the directory 627 put 513's pair in beside 520 and 533). An absolute path is resolved as given; a
# relative one is resolved against the caller's directory and said so, because 615 measured a tool that
# `cd`s before parsing its arguments and silently turns the caller's path into its own.
#
# THE KINDS ARE THE RUNNER'S OWN READERS, so the derivation here and the derivation there are the same
# one and not two: `maxhex` takes the largest value of a key (as `run_and_capture.sh`'s `maxhex` does),
# `keyval` the last in file order (as its `keyval` does), `first` the first, and `count` a plain record
# count. A value in the record that this code cannot reproduce is a **FAIL** and not a warning.
#
# Exit: 0 every reading reproduced; 1 at least one did not, or the record/log set is not readable.
# The exit code is the verdict, so it is never masked by a pipeline.

set -u

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
RECORD=${RECORD_OVERRIDE:-$REPO_ROOT/records/baseline-readings.txt}
# The log directory: `--dir=DIR` or a bare first argument. **Both spellings are implemented and not
# only documented** - the header of this file advertised `--dir` before the first version of the code
# read `$1` alone, which is a claim in a comment that nothing executes (604's rule) arriving in the
# same step that was written to close one.
DIR=
case ${1:-} in
  --dir=*) DIR=${1#--dir=} ;;
  '')      DIR=$REPO_ROOT/out/stage90/captures ;;
  *)       DIR=$1 ;;
esac
REBASED=""

# --- the log directory, resolved against the caller and not against this script (615) -------------
if [[ $DIR != /* ]]; then
  REBASED=$DIR
  DIR=$PWD/$DIR
fi
if [[ ! -d $DIR ]]; then
  printf 'verify_baseline_readings: no directory at %s\n' "$DIR" >&2
  printf '  The record names four logs and this tool cannot reach them. That is a state and not a\n' >&2
  printf '  reading: the logs are outside version control, so a host without them cannot check the\n' >&2
  printf '  numbers they carry. The record IS the identity; pass --dir (or a first argument) at the\n' >&2
  printf '  directory that holds them.\n' >&2
  exit 1
fi
if [[ -n $REBASED ]]; then
  printf "note: '%s' is a relative path, resolved against the directory this was invoked from (%s),\n" \
         "$REBASED" "$PWD"
  printf '      not against %s. Pass an absolute path to say it exactly.\n' "$(dirname "$0")"
fi

[[ -r $RECORD ]] || { printf 'verify_baseline_readings: no record at %s\n' "$RECORD" >&2; exit 1; }

# --- the derivations, one per kind -----------------------------------------------------------------
# Every one takes the kind (which carries the key or substring) and the log path, and prints the value
# in the record's own spelling. **A key that is absent yields the empty string, which matches no value
# in the record** - so an absent key is a FAIL rather than a silent zero. That is 569's rule (absent is
# not zero) applied to the verifier: a log that lost a key must not read as a log whose key is 0.
derive() {
  local kind=$1 log=$2
  case $kind in
    maxhex:*) grep -ao "${kind#maxhex:}=0x[0-9a-f]*" "$log" | sed 's/.*=//' | sort -r | head -1 ;;
    keyval:*) grep -ao "${kind#keyval:}=0x[0-9a-f]*" "$log" | sed 's/.*=//' | tail -1 ;;
    first:*)  grep -ao "${kind#first:}=0x[0-9a-f]*"  "$log" | sed 's/.*=//' | head -1 ;;
    count:*)  grep -ao -- "${kind#count:}"           "$log" | wc -l | tr -d ' ' ;;
    *)        printf 'UNKNOWN-KIND' ;;
  esac
}

# --- the logs --------------------------------------------------------------------------------------
declare -A LOGFILE=() LOGSHA=() LOGSIZE=()
nlog=0
while read -ra F; do          # one `log=` line, split on spaces: log=TAG sha256=… bytes=… file=…
  [[ ${F[0]} == log=* ]] || continue
  local_tag=${F[0]#log=}
  nlog=$((nlog+1))
  for fld in "${F[@]:1}"; do
    case $fld in
      sha256=*) LOGSHA[$local_tag]=${fld#sha256=} ;;
      bytes=*)  LOGSIZE[$local_tag]=${fld#bytes=} ;;
      file=*)   LOGFILE[$local_tag]=${fld#file=} ;;
      *)        ;;
    esac
  done
done < <(grep -E '^log=' "$RECORD")
if (( nlog == 0 )); then
  printf 'verify_baseline_readings: the record names no log (no `log=` line in %s)\n' "$RECORD" >&2
  exit 1
fi

fails=0
for tag in $(printf '%s\n' "${!LOGFILE[@]}" | sort); do
  f=$DIR/${LOGFILE[$tag]}
  want_sha=${LOGSHA[$tag]:-}; want_size=${LOGSIZE[$tag]:-}
  if [[ ! -f $f ]]; then
    printf 'FAIL  log %-6s absent: %s\n' "$tag" "$f"; fails=$((fails+1)); continue
  fi
  got_size=$(stat -c%s "$f")
  got_sha=$(sha256sum "$f" | awk '{print $1}')
  if [[ -n $want_size && $got_size != "$want_size" ]]; then
    printf 'FAIL  log %-6s size %s, record says %s\n' "$tag" "$got_size" "$want_size"; fails=$((fails+1)); continue
  fi
  if [[ -n $want_sha && $got_sha != "$want_sha" ]]; then
    printf 'FAIL  log %-6s sha256 %s, record says %s\n' "$tag" "$got_sha" "$want_sha"; fails=$((fails+1)); continue
  fi
  printf 'ok    log %-6s %s  %s bytes\n' "$tag" "${got_sha:0:16}…" "$got_size"
done

# --- the readings ----------------------------------------------------------------------------------
nread=0
while read -r name tag kind value _rest; do
  # **`kind=` has to be stripped before the `case` can see the kind.** The record's words are
  # `key=value`, so `$kind` arrives as `kind=maxhex:xnu_live_door_seq`; the first version of this loop
  # passed that whole word to the derivation, whose `case` patterns are `maxhex:*` and therefore
  # matched nothing, and the run reported **38 FAILs on a correct record** - a verifier that refuses
  # everything is exactly as uninformative as one that accepts everything (614's rule), and the reason
  # it was caught is that the tool was run before it was believed.
  kind=${kind#kind=}
  key=${tag#log=}; want=${value#value=}
  log=$DIR/${LOGFILE[$key]:-}
  nread=$((nread+1))
  if [[ ! -f $log ]]; then
    printf 'FAIL  %-18s log %-6s is absent, so nothing was derived\n' "${name#reading=}" "$key"
    fails=$((fails+1)); continue
  fi
  got=$(derive "$kind" "$log")
  if [[ $got == "$want" ]]; then
    printf 'ok    %-18s %-6s %-34s %s\n' "${name#reading=}" "$key" "$kind" "$got"
  else
    printf 'FAIL  %-18s %-6s %-34s got [%s], record says [%s]\n' \
           "${name#reading=}" "$key" "$kind" "$got" "$want"
    fails=$((fails+1))
  fi
done < <(grep -E '^reading=' "$RECORD")

printf '\n%d log(s) and %d reading(s) checked, %d FAIL\n' "$nlog" "$nread" "$fails"

# --- the record's own size, which is pinned in the record and not derived from it (627) ------------
#
# **This refusal exists because the tool's own last falsification came back green.** With the counts
# derived from the record, a record whose lines had all been deleted verified with zero logs, zero
# readings and zero failures - exit 0 - and a check that succeeds by finding nothing is the one
# property a check that never ran also has (613). Deriving the expectation from the thing it is meant
# to bound is 612's defect one level down. So the record states `expect_logs=`/`expect_readings=` and a
# set smaller than that is a refusal, which makes a deleted line a red run instead of a smaller green
# one. A missing field is a refusal too: a record too old to carry the field is not a record this tool
# has been shown to bound.
want_logs=$(sed -n 's/^expect_logs=\([0-9][0-9]*\).*/\1/p' "$RECORD" | head -1)
want_reads=$(sed -n 's/^expect_readings=\([0-9][0-9]*\).*/\1/p' "$RECORD" | head -1)
if [[ -z $want_logs || -z $want_reads ]]; then
  printf 'REFUSING: %s does not carry both expect_logs= and expect_readings=, so this tool cannot tell\n' \
         "$RECORD"
  printf 'a record that is complete from one whose lines were deleted. Refusing rather than reporting a\n'
  printf 'count nobody bounded.\n'
  exit 1
fi
if (( nlog != want_logs )) || (( nread != want_reads )); then
  printf 'REFUSING: the record names %d log(s) and %d reading(s); it says %d and %d. A set smaller than\n' \
         "$nlog" "$nread" "$want_logs" "$want_reads"
  printf 'the record claims is a line that was deleted, and every reading still present can be green.\n'
  exit 1
fi

if (( fails != 0 )); then
  printf '\nREFUSING: the record and the logs disagree, so the measured premises behind the rungs are\n'
  printf 'not established on this host. Do not read a rung as measured against these numbers until\n'
  printf 'this is green - and do not "fix" the record to match the log without establishing which of\n'
  printf 'the two moved.\n'
  exit 1
fi
printf 'Every reading the record carries is reproduced from the log it names.\n'
exit 0
