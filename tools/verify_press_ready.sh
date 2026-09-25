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
#      (and the park is FOUND from those bytes - see below)
#   2. the park is the set the record describes              - tools/verify_revert_set.sh
#   3. the gate accepts this tree, under the flag set the    - stages/stage90/preflight_boot_check.sh
#      arm's OWN switches demand
#   4. WHICH ARM the bytes are: the ELF reading JOINED TO    - tools/check_idle_window_unreachable.py
#      the payload's own switch record
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
# **THREE ARMS NOW, AND THE PAYLOAD RECORD IS NOT THE BLIND ALLEY THAT PARAGRAPH SAYS IT IS.** The
# sentence above is true of the two arms 646 had, and it was measured true again on 2026-09-25: for the
# sleeper and the 574 park the separating switch is an *entry* switch, so their payload records are
# byte-identical (`6c2b6038...`). It is NOT a property of the payload record that it cannot separate
# arms. 666's arm carries the acting arm's entry byte for byte (`a43304f2...`, the same SEAM_POC=1
# SEAM_MEASURE=0 IDLE_NO_SLEEP=0) and differs from it in exactly one line of `stage90-build-config.txt`,
# `#define STAGE90_HW_WATCHDOG_SELFTEST 0u` -> `1`. So the ELF reading alone names 653's arm for BOTH
# arms, and this row - whose whole job is to say which question the one press is about to ask - was
# GREEN while naming the wrong arm. That is this file's own subject matter happening inside this file:
# a reading that is correct about its own input, and answers a different question than the operator is
# asking. So the arm is named by the ELF reading **joined to** the payload's own switch record, and the
# join is required rather than best-effort: if that record cannot be read, or a switch it decides on is
# not named exactly once with a value, the row refuses instead of falling back to the entry reading -
# because the fallback IS the wrong answer.
#
# **THE SAME RECORD DECIDES THE GATE'S FLAG SET, and that is the third repair.** The gate refuses a
# self-test arm without `--allow-hw-watchdog-selftest` (`preflight_boot_check.sh:1570`), and row 3
# invoked it with `--allow-xnu-entry` alone - so on the arm in `out/` the row whose whole job is "the
# gate accepts this tree" printed `exit 1`, and no argument this file accepts could make it green. A
# refusal is safe. A *permanently* red row on a tree that is fine is not: the next red row gets read as
# the tool being stale instead of as a press that must not be fired, which is the reading this file
# exists to protect. The flag set is now DERIVED from the arm's own switches and PRINTED as the command
# the operator types, so the flags the gate is checked with here and the flags the run is given are one
# value with one definition. Two checks keep that derivation honest - see the block above row 3.
#
# **AND THE PARK IS FOUND, NOT REMEMBERED.** The default used to be the set `armed-sleepless-696a0f39`
# - an arm that was the live one two steps before this file's last edit - so on any other arm row 1
# compared the live tree with the WRONG PARK and its refusal was about the default rather than about
# the tree. A name nobody typed is exactly how the wrong arm gets pressed, so there is no default name
# any more: row 1 hashes the live `stage90-qcdt.img` and asks the record which set those bytes are.
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
#   tools/verify_press_ready.sh --park DIR --set NAME   # ask about a NAMED arm instead of the live one
#   tools/verify_press_ready.sh --gate PATH        (testing seam: falsifies the gate row)
#   tools/verify_press_ready.sh --gate-flags '--allow-xnu-entry'
#                                                  (testing seam: replaces the derived flag set with
#                                                   this one, so the derivation can be falsified)
#   tools/verify_press_ready.sh --help
#
# With no `--set`, the set is FOUND rather than defaulted: the live `stage90-qcdt.img` is hashed and the
# record is asked which set those bytes belong to (row 1). `--set`/`--park` point this tool at an arm
# that is not the one in `out/`, which answers a different question than the press's.
#
# The flag set the gate is run with is printed near the top and is the set the RUN must be given: the
# runner passes unrecognised arguments straight to the gate (`run_and_capture.sh:142`), so a press fired
# with a narrower set is a press spent on a gate refusal.
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
# **No default arm NAME, for the park and for the set alike, and that is one of the three repairs.**
# The default used to be `armed-sleepless-696a0f39` - the arm that was live two steps before this
# file's last edit - so on any other arm this tool compared the live tree against the WRONG PARK and
# row 1's refusal was about the default rather than about the tree. A default is a name nobody typed,
# and a name nobody typed is how the wrong arm gets pressed. So: empty here means "not given", `--set`
# names an arm explicitly, and with no `--set` row 1 FINDS the set from the bytes the press sends.
# Non-empty from the environment still counts as given, which is why this is `${VAR:-}` and not a
# separate flag variable.
PARK=${PARK:-}
SET=${SET:-}
GATE_FLAGS_SEAM=${GATE_FLAGS_SEAM:-}
RECORD=$REPO_ROOT/stages/stage90/revert-set.txt
GATE=$REPO_ROOT/stages/stage90/preflight_boot_check.sh
# The artifact `fastboot boot` sends - the one file whose bytes name the arm, and the two names read
# out of the live tree by rows 1, 3 and 4.
QCDT_NAME=stage90-qcdt.img
PAYLOAD_CFG_NAME=stage90-build-config.txt
SERIAL=4a2fe00b
NEIGHBOUR=33e80afe
QUIET=0

while (($# > 0)); do
  case $1 in
    --park)  PARK=${2:-}; shift 2 ;;
    --set)   SET=${2:-};  shift 2 ;;
    --live)  LIVE=${2:-}; shift 2 ;;
    # A testing seam, and named as one: the flag set is derived from the arm's own switches below, and a
    # derivation that can only ever be seen to work is not a reading. This replaces the derived set
    # wholesale, so the gate row goes red exactly when the derivation mattered.
    --gate-flags) GATE_FLAGS_SEAM=${2:-}; shift 2 ;;
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

# ======================================================================================================
# The readings the rows consume, taken ONCE, before any row runs
# ======================================================================================================
# Each of these is a value more than one row needs, so it is read here and not inside a row: a second
# reading of the same file would be two definitions of one quantity - this project's most repeated
# defect, and the one that made row 4's old name green on the wrong arm. Nothing here prints a verdict:
# a reading that fails is consumed by the row whose claim depends on it and refused there, with that
# row's own reason for needing it.

# --- the record's line format, parsed by NAME and defined ONCE ---------------------------------------
# `kv_of KEY LINE`. This file's first draft read `set= sha256= bytes= file= role=` positionally - four
# fixed fields and then the rest - and `file=` is the FOURTH token, so `bytes` was given `file=<name>`
# and every member was reported absent with a path ending in `manifest_members=stage90_fixture.macho,`.
# The record's shape was assumed rather than read, and it is not even uniform: the `SHA256SUMS.txt`
# line carries an extra key before `role=`. A key's name is the only stable handle on a line like this.
kv_of() {   # kv_of KEY LINE -> the value of KEY= in LINE; empty when the key is not there
  local t out=''
  # `set -f` for the word-split below: a `*` anywhere in a role text would otherwise be pathname-
  # expanded against the current directory, and the token this reads would not be the record's.
  set -f
  for t in $2; do
    case $t in "$1"=*) out=${t#*=}; break ;; esac
  done
  set +f
  printf '%s' "$out"
}

# --- which SET the live bytes are: FOUND from the artifact the press sends ---------------------------
# Row 1 asks "are the live bytes the recorded bytes", and it can only ask that once the set is known.
# The alternatives to finding it were tried and are worse. A default name is a name nobody typed. And
# the record's own `role=` sentences cannot answer it: THREE sets in this record claim in their role
# text to be "the arm the next press sends" - `armed-sleepless-696a0f39`, `armed-seam-poc-a43304f2` and
# `armed-selftest-wdog-ef0361a2` (measured 2026-09-25). A claim in a record is not a reading, so the
# bytes of `stage90-qcdt.img` - the one file `fastboot boot` sends - are asked instead.
#
# **Asked OF A TOOL AND NOT OF A SCAN HERE, since 668.** This block resolved the set inline from 667
# until the runner needed the same answer (`--expect-arm`, 668) and the rehearsal needed it too - one
# question with three would-be answers, on the file whose whole job is to name the arm a press sends.
# `tools/resolve_arm_set.sh` is now the only place that answers *which set are these bytes*, and it is
# called from here, from `run_and_capture.sh` and from `tools/rehearse_live_path.sh`. The test that
# keeps this honest is not the comment: `kv_of` below is NOT that parser being kept alive a second time
# - it reads a *different* question (enumerate the members of a NAMED set) - and if the two ever
# disagreed about the record's line format, the set would resolve here and the member hashes would not,
# which row 1 would report as ten members "not the recorded bytes".
SET_FOUND=''; SET_WHY=''
RESOLVE_ARM_SET=$REPO_ROOT/tools/resolve_arm_set.sh
if [[ -n $SET ]]; then
  SET_WHY='given on the command line'
elif [[ ! -x $RESOLVE_ARM_SET ]]; then
  SET_WHY="$RESOLVE_ARM_SET is not executable, so which set the live bytes are cannot be resolved - and this row must not fall back to a name it made up"
else
  if _ro=$("$RESOLVE_ARM_SET" "$LIVE" 2>&1); then
    SET_FOUND=$(printf '%s' "$_ro" | head -1 | cut -f1)
    # The resolver's own second and third fields are printed rather than recomputed: they are the
    # hash it compared, and hashing the file again here would be a second reading of the thing the
    # first reading was about.
    SET_WHY="found by hashing the live $QCDT_NAME ($(printf '%s' "$_ro" | head -1 | cut -f2)), which exactly one set in the record records"
  else
    # Its refusal is quoted, not paraphrased: a second wording of one finding is a second definition
    # of it, and the resolver's version names the hash it looked for and the sets that do exist.
    SET_WHY="$_ro"
  fi
fi
[[ -n $SET_FOUND ]] && SET=$SET_FOUND
if [[ -z $PARK ]]; then
  # The park's directory is named for its set (that is the convention the park rule rests on), so it
  # follows the set. An unresolvable set leaves both empty and the rows say so by name rather than
  # comparing against whatever `frozen/` happens to hold.
  if [[ -n $SET ]]; then PARK=$LIVE/frozen/$SET; fi
fi

# --- the payload's own switch record: read ONCE, it decides BOTH row 3's flag set and row 4's name ---
# Two records, two shapes, and this one is C source (`#define NAME VALUE`) where the entry record is
# `NAME=VALUE`. The value is extracted with a copy of the gate's own expression
# (`preflight_boot_check.sh:132-135`, `value_of`'s `sub()`) character for character, and the copy is
# deliberate: a second, hand-rolled parse of the same line would be a second definition of one value,
# and the failure it produces - a value that is empty or truncated here and correct there - is exactly
# the silent kind. What makes the copy safe is that a drift cannot pass unseen: every switch below is
# required to be named exactly once WITH a value, so a parse that stops seeing a value refuses the row.
PAYLOAD_CFG=$LIVE/$PAYLOAD_CFG_NAME
declare -A payload_sw=()
declare -A payload_sw_n=()
payload_cfg_ok=0
payload_cfg_why=''
if [[ ! -r $PAYLOAD_CFG ]]; then
  payload_cfg_why="$PAYLOAD_CFG is not readable, so the payload's own switches cannot be read"
else
  payload_cfg_ok=1
  while IFS=$'\t' read -r _name _val; do
    [[ -n $_name ]] || continue
    payload_sw[$_name]=$_val
    payload_sw_n[$_name]=$(( ${payload_sw_n[$_name]:-0} + 1 ))
  done < <(awk '
    $1 == "#define" && $2 ~ /^STAGE90_/ {
      v = $0; sub(/^[[:space:]]*#define[[:space:]]+[^[:space:]]+[[:space:]]*/, "", v)
      print $2 "\t" v
    }' "$PAYLOAD_CFG")
fi

# The switches the two rows decide on, named once, here. A switch this list does not name is not read -
# and the gate closes the key set from its own side (`BUILD_CFG_KEYS` plus the converse check that
# refuses any `STAGE90_*` key that list does not carry), so a switch added on the build side is refused
# by the gate before it could make the flag set below silently incomplete.
PAYLOAD_SWITCHES=(STAGE90_XNU_ENTRY STAGE90_HW_WATCHDOG_SELFTEST STAGE90_DEADMAN_SELFTEST
                  STAGE90_HANDOFF_MODE STAGE90_PMAP_ATTR_MODE STAGE90_CACHE_MODE
                  STAGE90_HANDOFF_FAULT_INJECT_VA)
sw_problem=''
if (( payload_cfg_ok == 1 )); then
  for _s in "${PAYLOAD_SWITCHES[@]}"; do
    (( ${payload_sw_n[$_s]:-0} == 1 )) \
      || sw_problem+="$_s is named ${payload_sw_n[$_s]:-0} time(s), and a switch read at all has to be named exactly once; "
    [[ -n ${payload_sw[$_s]:-} ]] \
      || sw_problem+="$_s is named with no value after it, which the gate refuses for every key in BUILD_CFG_KEYS because that is where its own off branches sit; "
  done
fi

# --- the flag set the RUN must be given: DERIVED, and by a tool since 668 --------------------------
# The gate refuses a hazardous state without its own `--allow-*` (`preflight_boot_check.sh:1570`, `:1592`,
# `:1605`, `:1649`, `:1684`, `:1948`, `:1954`, `:1975`, `:1985`; and `:1680` is the one the other way
# round, a flag it refuses when the switch is OFF). So the set a run needs is a function of the arm's own
# switches - and **this file had its own copy of that derivation until 668**, which is how it came to
# invoke the gate with `--allow-xnu-entry` alone while the arm in `out/` needed a second flag (667's
# section 1b). `tools/gate_flags_for_arm.sh` is now the one place that answers it, and it is called from
# here and from `tools/rehearse_live_path.sh` - whose twenty live-path cells were red on this arm for the
# same missing definition.
#
# What stays HERE is the payload's own record parse, and that is not the same question asked twice: row 4
# needs the *values* of two switches to name the arm (`STAGE90_HW_WATCHDOG_SELFTEST` and
# `STAGE90_DEADMAN_SELFTEST`), where the tool needs the whole table to derive flags. Two questions, two
# readers, one record - and the drift between them is loud rather than quiet: a parse that stopped seeing
# a value refuses row 4 and the tool both, each in its own words.
GATE_FLAGS_FOR_ARM=$REPO_ROOT/tools/gate_flags_for_arm.sh
gate_args=(); gate_args_str=''; gate_flags_ok=0; gate_flags_why=''
if [[ ! -x $GATE_FLAGS_FOR_ARM ]]; then
  gate_flags_why="$GATE_FLAGS_FOR_ARM is not executable, so the flag set this arm needs cannot be derived - and the gate's flags are a function of the arm's own switches, so without it this row would be gating on a guessed set"
else
  # `--gate` is passed through so the vocabulary check reads the same gate this row invokes, and both get
  # the `--gate` seam's value: a flag whose value is used in one place and not another is this project's
  # most repeated defect class, and it has happened once in this very file (the first draft's
  # `cd ... && ./preflight_boot_check.sh` ignored the seam for the invocation and reported `exit 127` as
  # the gate's own verdict).
  if _gfo=$("$GATE_FLAGS_FOR_ARM" "$LIVE" --gate "$GATE" 2>&1); then
    gate_flags_ok=1
    while IFS= read -r _f; do
      [[ -n $_f ]] || continue
      gate_args+=("$_f")
    done <<< "$_gfo"
    if (( ${#gate_args[@]} > 0 )); then
      gate_args_str=$(printf '%s ' "${gate_args[@]}")
      gate_args_str=${gate_args_str% }
    fi
  else
    # The tool's own sentence, quoted rather than paraphrased: a second wording of one finding is a
    # second definition of it, and this one names the switch or the flag it could not derive.
    gate_flags_why=$_gfo
  fi
fi

# Row 4's arms: which of the two self-tests this payload runs. `0|0u` is the gate's own `is_off`
# (`preflight_boot_check.sh:158`) and the empty value has already been refused above, so an empty value
# cannot take the off arm here and silently name a self-test arm that is not built.
sw_on() {
  case ${payload_sw[$1]:-} in 0|0u) return 1 ;; *) return 0 ;; esac
}

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
if [[ -n $SET ]]; then
  say "  park      $PARK   (set $SET - $SET_WHY)"
else
  say "  park      (NOT RESOLVED: $SET_WHY)"
fi
say "  record    $RECORD"
if (( gate_flags_ok == 1 )); then
  say "  gate flags  ${gate_args_str:-(none - no switch this arm declares needs one)}"
  # **The runner's line carries `--expect-arm` and the gate's does not, and the asymmetry is the point.**
  # Since 668 `run_and_capture.sh` refuses to send anything unless the caller says which arm the press is
  # for, so a press fired without that argument never reaches the gate at all; the gate has no such
  # argument and refuses an unknown one. The name is the set resolved above, so the two lines below are a
  # pair that agrees by construction rather than two commands to be assembled by hand.
  say "  the run     ./preflight_boot_check.sh ${gate_args_str:-(no flags)}"
  say "              ./run_and_capture.sh $gate_args_str --expect-arm=${SET:-<unresolved>}"
  say "              (the runner hands unrecognised flags to the gate, so a press given a NARROWER set"
  say "               than this is a press spent on a gate refusal - and a press given no --expect-arm"
  say "               is refused before the gate runs, with nothing sent)"
else
  say "  gate flags  (NOT DERIVED - see row 3, which refuses rather than gating on a guessed set)"
fi
say ""

# --- 1. the live bytes ARE the parked bytes -------------------------------------------------------
# Both directions are checked and they are not redundant: the record's hash is the AUTHORITY (the
# park is a copy and copies can be wrong), and `cmp` against the park is a second, independent field
# of the same bytes - which is what makes a typo in either a reading rather than a silence. The file
# list comes from the record, so a member the record names and the live tree does not have is named
# as absent rather than reported as a mismatch.
#
# **The set this row compares against is FOUND, and when it cannot be found this row refuses rather
# than comparing against something else.** The finding is done above, once, from the live
# `stage90-qcdt.img` - the artifact the press sends - because the set's NAME cannot be defaulted and
# the record's `role=` text cannot be asked (three sets claim to be next; see the finding block). So
# there are two more red shapes here than there were: a live payload that no set records, and one that
# two sets record. Both are named in the row's own detail line.
if [[ -z $SET ]]; then
  bad 'live arm is the recorded arm' "the set was not found, so there is nothing to compare the live bytes WITH: $SET_WHY"
elif [[ ! -r $RECORD ]]; then
  bad 'live arm is the recorded arm' "$RECORD is not readable, so which files the armed set has, and what they should hash to, cannot be read at all"
elif [[ ! -d $LIVE ]]; then
  bad 'live arm is the recorded arm' "$LIVE is not a directory - an absent directory is not a failed read"
elif [[ -z $PARK ]]; then
  bad 'live arm is the recorded arm' "the park's directory is named for the set and the set is not resolved, so no park was found to compare against ($SET_WHY)"
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
    # extra key before `role=`, so the shape is not even uniform down the file. The parser itself is
    # `kv_of`, defined once above, because the set-finding block reads the same lines.
    [[ $line == *"set=$SET "* ]] || continue
    file=$(kv_of file "$line")
    if [[ -z $file ]]; then
      bad 'live arm is the recorded arm' "a line of set $SET in the record has no file= key, so this check cannot say which member it is about: $(printf '%s' "$line" | cut -c1-90)..."
      nset=-1; break
    fi
    sha=$(kv_of sha256 "$line"); bytes=$(kv_of bytes "$line")
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
if [[ -z $SET || -z $PARK ]]; then
  bad 'the park verifies against the record' "there is no park to verify - the set was not found and the park's name follows it ($SET_WHY)"
elif [[ ! -x $REPO_ROOT/tools/verify_revert_set.sh ]]; then
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
#
# **The flag set is the arm's, not a constant, and the row refuses rather than guesses.** The gate is
# invoked here with exactly the flags the run must be given (`gate_args`, derived above from the arm's
# own switches). Until 2026-09-25 this row passed `--allow-xnu-entry` and nothing else, which on the
# arm 666 put in `out/` is a set the gate refuses (`:1570`) - a *true* refusal, and a useless one: the
# row is the only thing that says the press will not be wasted, so a permanently red row on a good tree
# teaches the operator to read the next red row as staleness. The three ways the derivation can fail
# are all refusals here, because each of them would otherwise make this row a verdict about a
# *different* invocation than the run's.
if [[ ! -x $GATE ]]; then
  bad 'the gate accepts this tree' "$GATE is not executable, so the tree has not been checked by the thing that checks it"
elif (( gate_flags_ok != 1 )); then
  bad 'the gate accepts this tree' "$gate_flags_why - and the gate's flags are a function of the arm's own switches, so running it with a guessed set would be a verdict about an invocation the run will not make"
else
  # Invoked by its own path and not as `./preflight_boot_check.sh` inside its directory: the gate
  # resolves everything it reads from `$(dirname "$0")` itself, so a cd is not needed - and the first
  # draft's `cd ... && ./preflight_boot_check.sh` **ignored the --gate flag's value for the
  # invocation** while honouring it for the executability test, so a `--gate` pointing at any other
  # name reported `exit 127` (command not found) as if it were the gate's own verdict. A flag whose
  # value is used in one place and not another is this project's most repeated defect class.
  if [[ -n $GATE_FLAGS_SEAM ]]; then
    used_flags=$GATE_FLAGS_SEAM
    used_why="the --gate-flags seam, NOT the derivation"
  else
    used_flags=$gate_args_str
    used_why="derived from the arm's own switches"
  fi
  # Unquoted on purpose: this is a LIST of flags, and it is the one value here that has to reach the
  # gate as several arguments. Empty is a legitimate list (an arm whose switches need none of them),
  # and an unquoted empty variable expands to nothing under `set -u`.
  if gout=$("$GATE" ${used_flags} 2>&1); then
    gsha=$(printf '%s' "$gout" | sed -n 's/^  sha256  *\([0-9a-f]\{16\}\)[0-9a-f]*.*/\1/p' | head -1)
    ok 'the gate accepts this tree' "exit 0${gsha:+ (image ${gsha}...)} under '${used_flags:-(no flags)}' ($used_why) - the entry sources match the manifest by content, the image carries the arm the entry bin holds, and the record binds that bin"
  else
    grc=$?
    # The refusal is the gate's own `REFUSING:` line (`fail()` writes it to stderr, and `2>&1` above
    # has it). The first draft matched a bare `/REFUS|not |no /` and, on this arm, printed the gate's
    # *passing* line `no source file is newer than the image` as if it were the reason - measured
    # 2026-09-25. A FAIL row whose text is a line that does not contain the refusal is worse than no
    # text, because it reads as a reason.
    refusal=$(printf '%s' "$gout" | grep -m1 '^REFUSING:' | cut -c1-220)
    [[ -n $refusal ]] || refusal=$(printf '%s' "$gout" | grep -m1 -E 'REFUS|not allowed|needs --allow|was passed|unknown argument' | cut -c1-220)
    [[ -n $refusal ]] || refusal="no REFUSING line at all; last line: $(printf '%s' "$gout" | tail -1 | cut -c1-160)"
    bad 'the gate accepts this tree' "exit $grc under '${used_flags:-(no flags)}' ($used_why) - $refusal"
  fi
fi

# --- 4. which arm the bytes in `out/` are, named by a reading of the ELF AND the seam pair ----------
# **Why a reading and not the record.** The reading order for the press depends on this answer: the
# sleeper arm's log carries no `xnu_live_seam_*` key (640 section 2: its seam's acting site is inside
# `platform_cache_idle_exit`, behind 599's closed `SIGPdisabled` gate), so scoring its log on 638
# section 3's `a1`/`b1` pair table would be a reading of a key that cannot appear. And the files a
# reader would reach for cannot tell THOSE TWO arms apart: the entry record differs by one line
# (`STAGE90_XNU_IDLE_NO_SLEEP`), and their *payload* records - the one the gate prints - are
# byte-identical (`6c2b6038...`, measured 646 and again 2026-09-25), because the switch separating them
# is an entry switch. **"The two arms 646 had" is the scope of that sentence, and it is not a property
# of the payload record** - see the last paragraph here.
#
# **Reachability alone stopped naming the arm the moment 653's arm was built, and that is the defect
# this row was repaired for on 2026-09-24.** There are now **two** arms that enter the window - the
# 574 park (the seam's *measure* arm, `SEAM_POC=0`/`SEAM_MEASURE=1`) and the acting arm (`SEAM_POC=1`/
# `SEAM_MEASURE=0`) - and the extractor answers them **identically** (`the window is reachable EXACTLY
# ONCE in this image.`), so a row that read only reachability named both of them *"574's park"* and
# pre-registered the *measure* arm's question for the acting arm's log. The two arms' pairs mean
# different things (572 section 6): the measure arm's pair is Apple's own L1 flush as a control - 652
# measured it equal - while the acting arm's pair reads its own operation's cells. So the arm is named
# from the **record's seam pair joined to the reading**, and a record that does not carry that pair
# exactly once is refused rather than named from the reachability sentence alone.
#
# **THE SAME SHAPE RECURRED ONE LEVEL DOWN on 2026-09-25, AND THIS TIME THE READING COULD NOT SEE IT
# AT ALL.** 666's arm - the hardware-watchdog self-test - carries 653's entry byte for byte
# (`a43304f2...`, SEAM_POC=1 SEAM_MEASURE=0 IDLE_NO_SLEEP=0 unchanged: the payload never reaches the
# handoff on a self-test arm, so the entry's seam switches cannot affect it either way) and differs from
# it in exactly ONE line of the payload's record, `#define STAGE90_HW_WATCHDOG_SELFTEST 0u` -> `1`
# (measured, 666 section 4). Both of this row's inputs - the reachability sentence and the entry record
# - are therefore identical between 653's arm and 666's, so the row named "the ACTING arm", with the
# acting arm's whole pre-registered reading attached, for a press that asks an entirely different
# question. It did that **green**. So the arm is named by the ELF reading JOINED TO the payload's own
# switch record, and the join is required rather than best-effort: if that record cannot be read, or a
# switch the join reads is not named exactly once with a value, this row REFUSES instead of falling back
# to the entry reading - because the fallback is the wrong answer, and it is the one that was green.
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
elif (( payload_cfg_ok != 1 )); then
  bad 'the arm is named by a reading' "$payload_cfg_why - and the entry reading ALONE cannot name this arm: 653's entry and 666's are byte-identical, so without that record the name would be the ENTRY's name, which on 666's arm is the wrong arm and is the answer that was green until 2026-09-25. A missing record here is a refusal and not a fallback"
elif [[ -n $sw_problem ]]; then
  bad 'the arm is named by a reading' "the payload's switch record cannot be read well enough to name the arm: $sw_problem"
else
  aout=$(timeout 120 "$ARM_CHECK" "$ARM_ELF" 2>&1); arc=$?
  vline=$(printf '%s\n' "$aout" | sed -n 's/^VERDICT: //p' | head -1)
  nsc=$(grep -c '^STAGE90_XNU_IDLE_NO_SLEEP=' "$ARM_CFG" || true)
  swe=$(sed -n 's/^STAGE90_XNU_IDLE_NO_SLEEP=//p' "$ARM_CFG" | head -1)
  # The seam's arm, read from the record's own pair rather than inferred from the reachability
  # sentence - the two window-entering arms answer that sentence identically (see the block above).
  nsp=$(grep -c '^STAGE90_XNU_SEAM_POC=' "$ARM_CFG" || true)
  nsm=$(grep -c '^STAGE90_XNU_SEAM_MEASURE=' "$ARM_CFG" || true)
  wsp=$(sed -n 's/^STAGE90_XNU_SEAM_POC=//p' "$ARM_CFG" | head -1)
  wsm=$(sed -n 's/^STAGE90_XNU_SEAM_MEASURE=//p' "$ARM_CFG" | head -1)
  seam=''; seamwhy=''
  if [[ $nsp == 1 && $nsm == 1 ]]; then
    case "$wsp$wsm" in
      10) seam=poc ;;
      01) seam=measure ;;
      00) seam=none ;;
      *)  seam=badpair; seamwhy="$ARM_CFG says STAGE90_XNU_SEAM_POC=$wsp STAGE90_XNU_SEAM_MEASURE=$wsm; the build refuses both at 1, so there are only three possible pairs (10, 01, 00)" ;;
    esac
  else
    seam=badcount
    seamwhy="$ARM_CFG carries $nsp STAGE90_XNU_SEAM_POC= and $nsm STAGE90_XNU_SEAM_MEASURE= line(s); both must be named exactly once, or which of two window-entering arms this is cannot be read at all"
  fi
  want=''; entry_arm=''; entry_conseq=''
  case $vline in
    'the window is UNREACHABLE in this image.'*)
      want=1
      entry_arm='the SLEEPLESS arm (594/595)'
      entry_conseq="this press's log carries NO xnu_live_seam_* key, so 638 section 3's pair table and 642's sleh_pc join are UNREAD on it (640)" ;;
    'the window is reachable EXACTLY ONCE in this image.'*)
      want=0
      case $seam in
        poc)
          entry_arm='the ACTING arm (653: the seam WITH its operation, SEAM_POC=1 and SEAM_MEASURE=0)'
          entry_conseq="this press's log carries the seam pair, and on THIS arm the pair reads the operation's own cells (run_and_capture.sh:2069-2088) - and the EXPECTED row is STALE LINE, WRITTEN OUT, i.e. a1 == this log's own rtcpre_pop: the operation issues exactly ONE DCCIMVAC (mcr p15,0,r0,cr7,cr14,{1}, at cfmdr_loop 0x800458b0) and the built call entry_seam_flush:0x8047ca64 passes len 8, so the loop runs once at 0x8054fec0 - the line containing the slot. Clean-and-invalidate is the SAME instruction, so this cell says the invalidate ran as well as the write-back. CLEAN LINE (a1 == b1) has TWO readings and the frontier decides which: the operation inert, OR the line already clean - in which case the invalidate still ran and the boot should still get past the pop. Read that cell TOGETHER with slot_post_calls / poll_seq, never alone. CHANGED (a1 neither) is the least likely of the three. **571's claim that the invalidate does not survive the distance to the pop is measured OUT**: the four instructions 0x8004632c-0x80046338 are mrc TPIDRPRW / mov r1,#1 / ldr r0,[r0,#1484] / str r1,[r0,#304] - no cr7 write of any kind - the load is at 0xc05593bc (TPIDRPRW = 0xc0558df0, this arm's own xnu_live_pce_tpidrprw) and the store at 0x8051a130 (the dump's r0 = 0x8051a000 = cpu_data, +304), and neither is in the slot's line 0x8054fec0-0x8054feff. The live alternative is ONE LEVEL BELOW: an L2 copy that an MVA operation may not reach. The frontier reading beside it is slot_post_calls, absent in every capture since 520" ;;
        measure)
          entry_arm='the MEASURE arm (574 park: the interception with its operation REMOVED, SEAM_POC=0 and SEAM_MEASURE=1)'
          entry_conseq="this press's log carries the seam pair as a CONTROL reading - whether Apple's own FlushPoU_Dcache writes the slot's line back, which 652 measured as an equal pair - and it does not by itself select a repair: that was the reading this arm existed to produce" ;;
        none)
          entry_arm='an arm that ENTERS the window with NO seam interception (SEAM_POC=0 and SEAM_MEASURE=0)'
          entry_conseq="this press's log carries NO xnu_live_seam_* key at all, so neither 638 section 3's pair table nor 642's sleh_pc join can be read on it" ;;
      esac ;;
  esac
  # --- the payload's own switches, JOINED to the entry reading ------------------------------------
  # The entry reading names the ENTRY. The arm the press sends is that entry joined to what the payload
  # does with it, and today only the two self-test switches change the answer: on such an arm the
  # payload spins BEFORE the handoff (`stage90_main.c:1224-1249` and `:1251-1264`, both after the DT
  # build), and `stage90_selftest_bounded_spin` returns only by rebooting - so the entry is never
  # entered, and every entry-side reading (the reachability sentence, the seam pair, the idle-exit
  # cells, the `xnu_live_*` keys) is about code this run does not execute.
  sub_arm=''
  if sw_on STAGE90_HW_WATCHDOG_SELFTEST; then
    sub_arm='the HARDWARE-WATCHDOG SELFTEST arm (666: STAGE90_HW_WATCHDOG_SELFTEST=1)'
    conseq="**THIS PAYLOAD NEVER REACHES THE ENTRY**, so the entry reading above is a fact about the code this payload carries and NOT about what this press's log can contain: there is no xnu_live_* key in it at all, so 638 section 3's a1/b1 pair table, 642's sleh_pc join and the idle-exit cells are UNREAD. THIS press's own keys are the hw_watchdog_* block the payload logs before it spins (hw_watchdog_enabled, hw_watchdog_readback_ok, hw_watchdog_counter_running, hw_watchdog_countdown_plausible, hw_watchdog_checksum) - and that block is the PRECONDITION, not the question: hw_watchdog_enabled=0 says in the log that this press tested no hardware net, which is worth having and is not the answer. The question - does the SoC's own countdown reset the device - is answered by the TIME the device takes to return, which is the spin's own stated result (stage90_selftest_bounded_spin: 'a device back at the net's own timeout means the net fired, and one back at the deadline means it did not'). The two times are 25.0 s bark / 28.0 s bite (533's capture, cited at run_and_capture.sh:1214) against 90.0 s for STAGE90_SELFTEST_DEADLINE_US, 3.2x apart. **And the deadline line's absence is not the watchdog's verdict**: that line ('deadline reached - the hardware watchdog did NOT fire') can be missing because the bite fired OR because the log was read before 90 s had elapsed, since the deadline is measured from the spin's start - so read the return time and the run's exit code (0 or 3 came back, 2 did not) beside it, and never the silence alone"
    if sw_on STAGE90_DEADMAN_SELFTEST; then
      conseq="**AND BOTH SELF-TESTS ARE ON in this image**: the dead-man self-test is the #if block BELOW the watchdog's, and the watchdog's spin returns only by rebooting, so the dead-man block is unreachable on this arm and this press is a watchdog press. Build with STAGE90_HW_WATCHDOG=0 to test the dead-man alone, which is what the source says above that block. $conseq"
    fi
  elif sw_on STAGE90_DEADMAN_SELFTEST; then
    sub_arm='the DEAD-MAN SELFTEST arm (STAGE90_DEADMAN_SELFTEST on)'
    conseq="**THIS PAYLOAD NEVER REACHES THE ENTRY** (stage90_main.c:1251-1264: the block arms the dead-man and then spins before the handoff, and the spin returns only by rebooting), so no xnu_live_* key can be in this log and 638 section 3's pair table and 642's sleh_pc join are UNREAD on it. This arm's own readings are the dead-man's (deadman_gicd_ctlr_*, deadman_gicc_ctlr_*, deadman_interval_us, deadman_samples, and whether stage90_arm_deadman_reset returned 1) plus the TIME the device takes to return: the dead-man's own budget is ~60 s against the 90 s deadline, so a device back at about 60 s is the software net firing. **The hardware watchdog is still armed underneath** unless the payload was built with STAGE90_HW_WATCHDOG=0, so attribute a reset to the dead-man only if the log carries the dead-man's own dump - which is what the source says above that block"
  fi
  if [[ -n $sub_arm ]]; then
    arm=$sub_arm
  else
    arm=$entry_arm
  fi
  if (( arc != 0 )); then
    bad 'the arm is named by a reading' "the reachability check exited $arc on $(basename "$ARM_ELF"), so nothing here names the arm: $(printf '%s' "$aout" | grep -m1 -E 'REFUS|Error|Traceback' | cut -c1-120)"
  elif [[ -z $vline ]]; then
    bad 'the arm is named by a reading' "$(basename "$ARM_ELF") produced no line beginning 'VERDICT: ' - the two sentences this row knows are not the output it got, and which arm it is cannot be inferred from either direction"
  elif (( nsc != 1 )); then
    bad 'the arm is named by a reading' "$(basename "$ARM_CFG") carries $nsc STAGE90_XNU_IDLE_NO_SLEEP= line(s); the record must name that switch exactly once for the reading to be compared with anything"
  elif [[ $seam == badcount || $seam == badpair ]]; then
    bad 'the arm is named by a reading' "$seamwhy"
  elif [[ -z $entry_arm ]]; then
    bad 'the arm is named by a reading' "the extractor's verdict is a sentence this row has no reading for: '$vline' - neither of the two it knows, and a sentence that is not one of them means the ENTRY was not read, so the arm is not named rather than named from the payload switch alone"
  elif [[ $swe != "$want" ]]; then
    bad 'the arm is named by a reading' "the ENTRY reading says $entry_arm, which is STAGE90_XNU_IDLE_NO_SLEEP=$want, and the entry record says STAGE90_XNU_IDLE_NO_SLEEP=$swe - one quantity with two readings, and they disagree"
  elif [[ -n $sub_arm ]]; then
    ok 'the arm is named by a reading' "$arm, and the entry it carries is $entry_arm ('$vline', and the entry record's STAGE90_XNU_IDLE_NO_SLEEP=$swe agrees). $conseq"
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
printf '    tree under %s, and a press would be fired on - give the run those same flags.\n' "'${gate_args_str:-(no flags)}'"
printf '    Nothing here says the press will SUCCEED - that is the\n'
printf '    run'"'"'s verdict (exit 0 or 3 came back; 2 did not return and owes another press).\n'
exit 0
