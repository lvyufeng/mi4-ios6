#!/usr/bin/env bash
#
# Is everything in place for the owed press, right now, in one verdict?
#
# WHY THIS EXISTS
# ---------------
# The next press sends exactly one command - `fastboot boot out/stage90/stage90-qcdt.img` - and the
# payload build is NOT byte-reproducible (408). `out/stage90/` holds the only copy of the armed arm
# that has ever existed, and the press cannot be repeated: one press, one run, and the arm is spent.
# Five things must be true at the moment of the press, and until this file existed each was checked
# by a *different* tool, by hand, in a different session. **They are numbered in the order they RUN
# and PRINT, and that is the only numbering in this file** - prose below cites a row by the label the
# table prints (`the arm is named by a reading`, `the press would be caught`), because an ordinal is
# a name a later insert can silently move to another row:
#
#   1. the bytes in `out/` are the bytes the park holds      - the gate does NOT check this
#   2. the park is the set the record describes              - tools/verify_revert_set.sh
#   3. the gate accepts this tree                            - stages/stage90/preflight_boot_check.sh
#   4. WHICH ARM the bytes are, named by a reading of the ELF - tools/check_idle_window_unreachable.py
#   5. a press now would actually be caught                  - `fastboot devices` / `adb devices`
#
# The arm row was added by 646, from `run-experiment-526`'s proposal, and it took the fourth position
# rather than the fifth because the rows are ordered by what they read - the arm is a property of the
# bytes in `out/`, and the two device reads come last - so the list reads top to bottom as the run
# prints it. The operator has two gate-clean choices that answer *different* questions - the sleeper
# arm (594/595, whose idle never sleeps and whose log therefore carries no `xnu_live_seam_*` key at
# all, 640) and the 574 park (which enters the window once per boot and whose log is the one that
# decides 638 section 3's `a1`/`b1` pair) - and none of the other rows says which one `out/` holds.
# The record cannot say it either: the switch that separates them is an *entry* switch, so
# `stage90-build-config.txt` - the payload's own switch record - is byte-identical between the two
# arms (`6c2b6038...`, measured 646), and the payload's switch list is what the gate prints. So the
# arm is named by a property of the image instead, and the arm row is what reads it.
#
# **Rows 1-3 say the press will not be *wasted*; the arm row says what the run that follows will be
# able to answer, and the device row says whether a press fired now would be caught at all.** That is
# the distinction that makes the arm row worth having: two gate-clean arms pass rows 1-3 identically,
# and only the arm row tells the operator which question the one press is about to ask.

# (1) is the gap this file is written for. The gate chains image <-> entry bin <-> config record and
# the source manifest, so it can say the image carries the arm the entry bin holds and that the entry
# sources are the ones the manifest lists - and all of that is still true of a tree in which
# `out/stage90-qcdt.img` has been *replaced by a different build of the same entry*. The park is the
# only thing that knows the armed bytes, and nothing compared the live file with it.
#
# THE TWO WAYS THE ARMED WINDOW BREAKS, both of which this file turns into a red row rather than a
# memory:
#
#   * **running `build_entry.sh`** rewrites `out/xnu_arm_entry.bin`, its config record and its source
#     manifest, and then `build.sh` rewrites the payload and the qcdt. The live arm is then a
#     *different* arm from the parked one, and the gate would still accept it - its freshness sweep
#     covers the payload's own sources and its entry clause asks by content about the sources, so a
#     successful rebuild is self-consistent. (1) is what notices.
#   * **editing anything under `xnu_arm_boot/`** is caught by the gate's entry-source clause, which
#     compares content and not mtime - so check (3) refuses and a press spent then boots NOTHING:
#     the catcher's own narration for that is `GATE REFUSED - nothing booted, nothing spent`, and the
#     press that bought it is gone. This is the reason the rule for the armed window is "read the
#     tree, do not edit it", and why a `git checkout` (which rewrites mtimes but not content) is fine
#     while an edit is not.
#
# WHAT IT DOES NOT DO
# -------------------
# It fires nothing: no gate-by-proxy, no run, no boot. It does not arm, disarm, or wait for a catcher,
# and it says nothing about whether one is alive. It reads device *lists* - the same two reads the
# armed catcher makes on its own 30 s poll - and those act on nothing. And it cannot tell you the
# press will *succeed*: it tells you the press will not be wasted, which is a different and checkable
# claim (597, 598).
#
# Usage:
#   tools/verify_press_ready.sh                      # the whole chain, one verdict
#   tools/verify_press_ready.sh --quiet              # only the verdict and any FAIL row
#   tools/verify_press_ready.sh --park DIR --set NAME
#   tools/verify_press_ready.sh --gate PATH        (testing seam: falsifies the gate row)
#   tools/verify_press_ready.sh --help
#
# Exit: 0 = every check passed and the live device state is one a press can be fired from;
#       1 = refused, with the failing check named. A check that could not look refuses too - a
#           `skipped` row is never a pass (629).
#
# Host-side only. `fastboot boot` only, never `flash` - nothing here can write to storage.

set -uo pipefail

SELF=$(readlink -f "${BASH_SOURCE[0]}")
REPO_ROOT=$(cd "$(dirname "$SELF")/.." && pwd)
LIVE=${LIVE:-$REPO_ROOT/out/stage90}
PARK=${PARK:-$LIVE/frozen/armed-sleepless-696a0f39}
SET=${SET:-armed-sleepless-696a0f39}
RECORD=$REPO_ROOT/stages/stage90/revert-set.txt
GATE=$REPO_ROOT/stages/stage90/preflight_boot_check.sh
SERIAL=4a2fe00b
NEIGHBOUR=33e80afe
QUIET=0

while (($# > 0)); do
  case $1 in
    --park)  PARK=${2:-}; shift 2 ;;
    --set)   SET=${2:-};  shift 2 ;;
    --live)  LIVE=${2:-}; shift 2 ;;
    # A testing seam, and named as one: the gate's own branch can only be falsified by a gate that
    # refuses, and the real one accepts this tree.  The tool refuses if the target is not executable,
    # so pointing it at nothing is a refusal and not a pass.
    --gate)  GATE=${2:-};  shift 2 ;;
    --quiet) QUIET=1; shift ;;
    -h|--help)
      awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$SELF"
      exit 0 ;;
    *) printf 'verify_press_ready: unknown argument %s\n' "$1" >&2; exit 1 ;;
  esac
done

CHECKS=(); VERDICT=(); DETAIL=(); NFAIL=0
ok()   { CHECKS+=("$1"); VERDICT+=(ok);   DETAIL+=("$2"); }
bad()  { CHECKS+=("$1"); VERDICT+=(FAIL); DETAIL+=("$2"); NFAIL=$(( NFAIL + 1 )); }

row() {
  (( QUIET == 1 )) && [[ $2 != FAIL ]] && return 0
  printf '  %-5s %-34s %s\n' "$2" "$1" "$3"
}

say() { (( QUIET == 1 )) || printf '%s\n' "$1"; }

say "verify_press_ready: $REPO_ROOT"
say "  live arm  $LIVE"
say "  park      $PARK   (set $SET)"
say "  record    $RECORD"
say ""

# --- 1. the live bytes ARE the parked bytes -------------------------------------------------------
# Both directions are checked and they are not redundant: the record's hash is the AUTHORITY (the
# park is a copy and copies can be wrong), and `cmp` against the park is a second, independent field
# of the same bytes - which is what makes a typo in either a reading rather than a silence. The file
# list comes from the record, so a member the record names and the live tree does not have is named
# as absent rather than reported as a mismatch.
if [[ ! -r $RECORD ]]; then
  bad 'live arm is the recorded arm' "$RECORD is not readable, so which files the armed set has, and what they should hash to, cannot be read at all"
elif [[ ! -d $LIVE ]]; then
  bad 'live arm is the recorded arm' "$LIVE is not a directory - an absent directory is not a failed read"
elif [[ ! -d $PARK ]]; then
  bad 'live arm is the recorded arm' "$PARK is not a directory - the park is the only copy of an arm that cannot be rebuilt (408), so its absence is a refusal and not a check that passed"
else
  nset=0; nlive=0; ncmp=0; miss=(); wrong=(); diffs=()
  while read -r line; do
    # **Parsed by NAME, not by field position, and that is a repair of this file's own first draft.**
    # It read `set= sha256= bytes= file= role=` positionally - four fixed fields then the rest - and
    # `file=` is the FOURTH token, so `bytes` was given `file=<name>`, `file` was given the *role*
    # text, and every member was reported absent with a path ending in
    # `manifest_members=stage90_fixture.macho,...`. The record's shape was assumed rather than read,
    # which is the defect this project's measurement notes rank among the first to suspect - and it
    # is the more embarrassing here because the record's own line for `SHA256SUMS.txt` carries an
    # extra key before `role=`, so the shape is not even uniform down the file.
    [[ $line == *"set=$SET "* ]] || continue
    _kv() { local t; for t in $line; do case $t in "$1"=*) printf '%s' "${t#*=}"; return 0 ;; esac; done; }
    file=$(_kv file)
    if [[ -z $file ]]; then
      bad 'live arm is the recorded arm' "a line of set $SET in the record has no file= key, so this check cannot say which member it is about: $(printf '%s' "$line" | cut -c1-90)..."
      nset=-1; break
    fi
    sha=$(_kv sha256); bytes=$(_kv bytes)
    (( nset++ ))
    if [[ ! -f $LIVE/$file ]]; then miss+=("$LIVE/$file (absent)"); continue; fi
    if [[ ! -f $PARK/$file ]]; then miss+=("$PARK/$file (absent)"); continue; fi
    (( nlive++ ))
    have=$(sha256sum "$LIVE/$file" | cut -d' ' -f1)
    [[ $have == "$sha" ]] || wrong+=("$file: live $have, record $sha")
    if cmp -s "$LIVE/$file" "$PARK/$file"; then (( ncmp++ )); else diffs+=("$file"); fi
  done < "$RECORD"
  if (( nset == -1 )); then
    :   # the malformed line already produced the refusal
  elif (( nset == 0 )); then
    bad 'live arm is the recorded arm' "the record has no line for set=$SET - the name is wrong, or the set was removed, and either way this check compared nothing"
  elif (( ${#miss[@]} > 0 )); then
    bad 'live arm is the recorded arm' "member(s) of the set are not where the record says: ${miss[*]}"
  elif (( ${#wrong[@]} > 0 )); then
    bad 'live arm is the recorded arm' "the LIVE file(s) are not the recorded bytes: ${wrong[*]} - a rebuild, or a tree that moved under the armed catcher"
  elif (( ${#diffs[@]} > 0 )); then
    bad 'live arm is the recorded arm' "the live file(s) MATCH the record but differ from the parked copy: ${diffs[*]} - the copy and the record disagree, and which one is the armed arm is now a question"
  else
    ok 'live arm is the recorded arm' "$nset file(s) in set $SET: $nlive present in both, every one hashes to the record, and all $ncmp are byte-identical to the park"
  fi
fi

# --- 2. the park is the set the record describes ---------------------------------------------------
# Delegated rather than reimplemented: verify_revert_set.sh is the recorded method (and `sha256sum -c`
# is NOT - the manifest it reads is absolute-pathed, so it would hash the live tree and report a park
# it never looked at).
if [[ ! -x $REPO_ROOT/tools/verify_revert_set.sh ]]; then
  bad 'the park verifies against the record' "$REPO_ROOT/tools/verify_revert_set.sh is not executable, so the park cannot be compared with the record by the recorded method"
else
  if out=$("$REPO_ROOT/tools/verify_revert_set.sh" "$PARK" --set="$SET" 2>&1); then
    ok 'the park verifies against the record' "$(printf '%s' "$out" | sed -n 's/^VERIFIED: //p' | head -1)"
  else
    bad 'the park verifies against the record' "$(printf '%s' "$out" | grep -m2 'FAIL' | tr '\n' ' ')"
  fi
fi

# --- 3. the gate accepts this tree, right now ------------------------------------------------------
# This is the check that catches an edit under xnu_arm_boot/ (content, not mtime), an entry image
# whose sources moved, and a record that binds a different entry bin. It is host-only: the gate never
# runs fastboot and never touches the device, and its own header says so - measured in 634 by running
# it with sudo/adb/fastboot stubbed, where the stub was never called and the output was byte-identical.
if [[ ! -x $GATE ]]; then
  bad 'the gate accepts this tree' "$GATE is not executable, so the tree has not been checked by the thing that checks it"
else
  # Invoked by its own path and not as `./preflight_boot_check.sh` inside its directory: the gate
  # resolves everything it reads from `$(dirname "$0")` itself, so a cd is not needed - and the first
  # draft's `cd ... && ./preflight_boot_check.sh` **ignored the --gate flag's value for the
  # invocation** while honouring it for the executability test, so a `--gate` pointing at any other
  # name reported `exit 127` (command not found) as if it were the gate's own verdict. A flag whose
  # value is used in one place and not another is this project's most repeated defect class.
  if gout=$("$GATE" --allow-xnu-entry 2>&1); then
    gsha=$(printf '%s' "$gout" | sed -n 's/^  sha256  *\([0-9a-f]\{16\}\)[0-9a-f]*.*/\1/p' | head -1)
    ok 'the gate accepts this tree' "exit 0${gsha:+ (image ${gsha}...)} - the entry sources match the manifest by content, the image carries the arm the entry bin holds, and the record binds that bin"
  else
    grc=$?
    bad 'the gate accepts this tree' "exit $grc - $(printf '%s' "$gout" | grep -m1 -E 'REFUS|source newer|not |no ' | cut -c1-160)"
  fi
fi

# --- 4. which arm the bytes in `out/` are, named by a reading of the ELF --------------------------
# **Why a reading and not the record.** The reading order for the press depends on this answer: the
# sleeper arm's log carries no `xnu_live_seam_*` key (640 section 2: its seam's acting site is inside
# `platform_cache_idle_exit`, behind 599's closed `SIGPdisabled` gate), so scoring its log on 638
# section 3's `a1`/`b1` pair table would be a reading of a key that cannot appear. And the files a
# reader would reach for cannot tell the arms apart: the entry record differs by one line
# (`STAGE90_XNU_IDLE_NO_SLEEP`), and the *payload's* record - the one the gate prints - is
# byte-identical (`6c2b6038...`, measured), because the separating switch is an entry switch.
#
# **The extractor's exit code is not the verdict.** Both of its verdicts exit 0, so the sentence is
# read out of its text, and a third shape is refused rather than folded into either arm - the rule this
# project keeps re-learning about an assumed output shape. It is also read with a timeout, because a
# row that hangs is a row that never reached its verdict.
ARM_CHECK=$REPO_ROOT/tools/check_idle_window_unreachable.py
ARM_ELF=$LIVE/xnu_arm_entry.elf
ARM_CFG=$LIVE/xnu_arm_entry-config.txt
if [[ ! -x $ARM_CHECK ]]; then
  bad 'the arm is named by a reading' "$ARM_CHECK is not executable, so which arm this press sends would be an operator's memory and not a reading"
elif [[ ! -f $ARM_ELF ]]; then
  bad 'the arm is named by a reading' "$ARM_ELF is absent - the reachability reading is taken from the entry ELF, and the ELF is not here to read"
elif [[ ! -r $ARM_CFG ]]; then
  bad 'the arm is named by a reading' "$ARM_CFG is not readable, so the entry record's switch cannot be compared with the reading"
else
  aout=$(timeout 120 "$ARM_CHECK" "$ARM_ELF" 2>&1); arc=$?
  vline=$(printf '%s\n' "$aout" | sed -n 's/^VERDICT: //p' | head -1)
  nsc=$(grep -c '^STAGE90_XNU_IDLE_NO_SLEEP=' "$ARM_CFG" || true)
  swe=$(sed -n 's/^STAGE90_XNU_IDLE_NO_SLEEP=//p' "$ARM_CFG" | head -1)
  want=''; arm=''; conseq=''
  case $vline in
    'the window is UNREACHABLE in this image.'*)
      want=1
      arm='the SLEEPLESS arm (594/595)'
      conseq="this press's log carries NO xnu_live_seam_* key, so 638 section 3's pair table and 642's sleh_pc join are UNREAD on it (640)" ;;
    'the window is reachable EXACTLY ONCE in this image.'*)
      want=0
      arm="the arm that ENTERS the window (574's park)"
      conseq="this press's log carries the seam pair, which is what chooses between 597's candidates (A) and (B) (638 section 3)" ;;
  esac
  if (( arc != 0 )); then
    bad 'the arm is named by a reading' "the reachability check exited $arc on $(basename "$ARM_ELF"), so nothing here names the arm: $(printf '%s' "$aout" | grep -m1 -E 'REFUS|Error|Traceback' | cut -c1-120)"
  elif [[ -z $vline ]]; then
    bad 'the arm is named by a reading' "$(basename "$ARM_ELF") produced no line beginning 'VERDICT: ' - the two sentences this row knows are not the output it got, and which arm it is cannot be inferred from either direction"
  elif (( nsc != 1 )); then
    bad 'the arm is named by a reading' "$(basename "$ARM_CFG") carries $nsc STAGE90_XNU_IDLE_NO_SLEEP= line(s); the record must name that switch exactly once for the reading to be compared with anything"
  elif [[ -z $arm ]]; then
    bad 'the arm is named by a reading' "the extractor's verdict is a sentence this row has no reading for: '$vline' - neither of the two it knows, so the arm is not named rather than named wrongly"
  elif [[ $swe != "$want" ]]; then
    bad 'the arm is named by a reading' "the reading says $arm, which is STAGE90_XNU_IDLE_NO_SLEEP=$want, and the entry record says STAGE90_XNU_IDLE_NO_SLEEP=$swe - one quantity with two readings, and they disagree"
  else
    ok 'the arm is named by a reading' "$arm: '$vline' and the entry record's STAGE90_XNU_IDLE_NO_SLEEP=$swe agrees, so $conseq"
  fi
fi

# --- 5. a press now would actually be caught -------------------------------------------------------
# The one row that decides whether the press is worth spending. Reads only.
if ! command -v sudo >/dev/null 2>&1; then
  bad 'the press would be caught' "no sudo on PATH, so the device lists cannot be read - and whether a run would be fired is exactly what this row is for"
else
  # **A failed read is not an empty device list.** `sudo -n` can fail for its own reasons (no cached
  # credential), and both lists would then be empty - which the device branches below would report as
  # "neither list names $SERIAL", a claim about a phone this row never managed to look at. 626's rule,
  # and the reason the two statuses are captured separately from the two strings.
  fb=$(sudo -n fastboot devices 2>/dev/null); fb_rc=$?
  adb_raw=$(sudo -n adb devices 2>/dev/null); adb_rc=$?
  if (( fb_rc != 0 || adb_rc != 0 )); then
    bad 'the press would be caught' "the device lists could not be READ (sudo -n exit $fb_rc for fastboot, $adb_rc for adb), so this row has no answer to \"would a press be caught\" - it is not a statement that the phone is absent"
  else
    adb_state=$(printf '%s\n' "$adb_raw" | tail -n +2 | awk -v s="$SERIAL" '$1==s { print $2; exit }')
    nfb=$(printf '%s\n' "$fb" | grep -c . || true)
    nother=$(printf '%s\n' "$fb" | grep -v "^$SERIAL" | grep -c '[^[:space:]]' || true)
    if (( nfb == 1 )) && [[ $fb == "$SERIAL"$'\t'* ]]; then
      ok 'the press would be caught' "fastboot lists $SERIAL ALONE, so a run fired now would clear the ambiguity guard"
    elif (( nother > 0 )); then
      bad 'the press would be caught' "the fastboot list holds $nother device(s) that are not $SERIAL: [$(printf '%s\n' "$fb" | grep . | tr '\n' ';')] - a run fired now is refused at the ambiguity guard (FB_AMBIG_WAIT, 60 s) and boots NOTHING. Unplug $NEIGHBOUR first; this list does not settle on its own (634/635 measured it at 3 h 35 m open)"
    elif printf '%s\n' "$fb" | grep -q "^$SERIAL"; then
      bad 'the press would be caught' "fastboot names $SERIAL but NOT in the pinned form the guard demands (\`$SERIAL<TAB>...\`): [$(printf '%s\n' "$fb" | grep . | tr '\n' ';')]- the runner's own presence checks would pass and its guard would refuse. That divergence is 634's"
    elif [[ -n $adb_state ]]; then
      case $adb_state in
        device|recovery)
          ok 'the press would be caught' "adb lists $SERIAL as '$adb_state', so the catcher's usable() is true and a press is caught; the run takes the adb branch (\`reboot bootloader\`) and then boots" ;;
        *)
          bad 'the press would be caught' "adb lists $SERIAL as '$adb_state' - every state but device|recovery makes the catcher's usable() FALSE, so it HOLDS and spends nothing even with the phone up (a run now would exit 1 having booted nothing). $([ "$adb_state" = unauthorized ] && echo "Accept the 'Allow USB debugging' prompt on the phone's screen." || echo 'Wait for the state to clear.')" ;;
      esac
    else
      bad 'the press would be caught' "neither list names $SERIAL (fastboot: ${fb:-nothing}; adb: nothing). A press can still be taken from here - this is the state Vol-Down + Power is taken from - but the catcher sees only $SERIAL in \`fastboot devices\` ALONE, so if $NEIGHBOUR is in fastboot when the phone arrives the run is refused and the press is spent. Unplug it first"
    fi
  fi
fi

# --- the table and the verdict ---------------------------------------------------------------------
say ""
printf '%-7s %-34s %s\n' 'VERDICT' 'CHECK' 'READING'
for i in "${!CHECKS[@]}"; do
  row "${CHECKS[$i]}" "${VERDICT[$i]}" "${DETAIL[$i]}"
done
say ""
if (( NFAIL > 0 )); then
  printf 'REFUSING: %d of %d check(s) failed. The press sends the bytes in %s, once.\n' \
    "$NFAIL" "${#CHECKS[@]}" "$LIVE"
  printf '          Read the FAIL row(s) above before pressing: a press spent on a refused gate is a\n'
  printf '          press spent on nothing, and the payload build does not reproduce (408).\n'
  exit 1
fi
printf 'ok: %d check(s). The bytes the press sends are the recorded armed bytes, the gate accepts the\n' "${#CHECKS[@]}"
printf '    tree, and a press would be fired on. Nothing here says the press will SUCCEED - that is the\n'
printf '    run'"'"'s verdict (exit 0 or 3 came back; 2 did not return and owes another press).\n'
exit 0
