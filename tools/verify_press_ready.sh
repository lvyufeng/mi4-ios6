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
  #
  # **AND `--live` IS THE SAME DEFECT CLASS ONE FLAG OVER, WHICH 679 MEASURED RATHER THAN ASSUMED.** The
  # gate resolves its tree from its own directory (`preflight_boot_check.sh:21` is
  # `OUT=$REPO_ROOT/out/stage90`, and there is no seam for it), so **this row reads `out/stage90`
  # whatever `--live` says**, while rows 1, 2, 4 and 5 read `--live`. Measured 2026-09-25: `--live
  # out/stage90/frozen/armed-selftest-wdog-ef0361a2` printed rows 1/2/4/5 about the self-test park and a
  # row 3 whose refusal named `/mnt/data/mi4-ios6/out/stage90/xnu_arm_entry-config.txt`. Nothing about
  # that is wrong in itself - the gate is right about its own tree - but a **green** row 3 read under
  # `--live` is a verdict about another arm, and the operator pointing the tool at a park is reading row
  # 4, four lines below, with no hint that row 3 is not about the same bytes. It is not repaired by
  # honouring `--live` here (the gate has no way to be pointed elsewhere, and inventing one would change
  # what row 3 means for every caller), so it is **said out loud in the reading** - the same treatment
  # the 667 finding block grew for row 1.
  _live_abs=$(readlink -f "$LIVE" 2>/dev/null || printf '%s' "$LIVE")
  if [[ $_live_abs != "$REPO_ROOT/out/stage90" ]]; then
    gate_tree_note=" **AND THIS ROW READ $REPO_ROOT/out/stage90 AND NOT $_live_abs**: the gate resolves its own tree (\`preflight_boot_check.sh:21\`) and there is no seam for it, so this verdict is about the live tree and not about the arm rows 1, 2, 4 and 5 read - read the two together and not as one."
  else
    gate_tree_note=''
  fi
  if gout=$("$GATE" ${used_flags} 2>&1); then
    gsha=$(printf '%s' "$gout" | sed -n 's/^  sha256  *\([0-9a-f]\{16\}\)[0-9a-f]*.*/\1/p' | head -1)
    ok 'the gate accepts this tree' "exit 0${gsha:+ (image ${gsha}...)} under '${used_flags:-(no flags)}' ($used_why) - the entry sources match the manifest by content, the image carries the arm the entry bin holds, and the record binds that bin$gate_tree_note"
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
    bad 'the gate accepts this tree' "exit $grc under '${used_flags:-(no flags)}' ($used_why) - $refusal$gate_tree_note"
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
  # **683: the ending switch, read here for exactly the reason `_POC`/`_MEASURE` are.** 653's entry
  # reading and 678's are the SAME sentence - the operation is byte-identical between them, only the
  # ending is new - so the reachability verdict cannot tell those two arms apart, and until this line
  # nothing else did either: row 4 named 678's arm as the ACTING arm and printed 535's pop-based
  # consequences for a run in which **the pop is never executed**. That is the defect 680 repaired in
  # `run_and_capture.sh`, one file over, and the same shape 679 was spent on.
  #
  # An ABSENT line is a legitimate reading and not a zero: `SEAM_END_RUN` did not exist before 678, so
  # every parked arm older than it - 653's, the sleeper, 666's, the 574 park - has no such line, and
  # refusing them would break the rows readiness exists to print for those arms. Absent therefore means
  # *this image predates the switch and its seam returns through the pop*, which is said below rather
  # than read as 0.
  wse=$(sed -n 's/^STAGE90_XNU_SEAM_END_RUN=//p' "$ARM_CFG" | head -1)
  # **686's ending is the same reading of the same record, one key over**, and it is read here for the
  # same reason 678's is: the two endings sit at opposite ends of the same exit, the operation is
  # byte-identical between their arms, and 653's arm is the same sentence again - so the reachability
  # verdict can tell none of the three apart and only the record can. The two keys are refused together
  # by the build, so at most one of them is 1 in any image that exists.
  wpse=$(sed -n 's/^STAGE90_XNU_POST_END_RUN=//p' "$ARM_CFG" | head -1)
  # **690: the THIRD ending, and the first one that is a CLOCK.** It is read here for the third time for
  # the same reason: 690's entry reading is 653's sentence again (`SEAM_POC=1 SEAM_MEASURE=0`, the
  # operation byte-identical), and 690's record names NEITHER of the two ending switches as on - both
  # lines read 0 - so every branch below fell through to the one that says the record *does not name*
  # `SEAM_END_RUN` **at all**, which is true of 653's arm and of no arm since 678. The fall-through is
  # therefore reached by two different facts (the key absent, the key named as 0) and it said the first
  # while meaning either; the tick key is what tells them apart, and it is also the only key in the
  # record that names this arm's ending. Read the same way as the other two - a missing line is empty
  # and not a zero.
  wpet=$(sed -n 's/^STAGE90_XNU_POST_END_TICKS=//p' "$ARM_CFG" | head -1)
  # 692: and the key that says this image's new act is a DEVICE and not a site. Read like the other
  # three - a missing line is empty, and the record of any arm before 692 does not carry it at all,
  # which is a different fact from a record that names it as 0.
  wst=$(sed -n 's/^STAGE90_XNU_STORAGE_PROBE=//p' "$ARM_CFG" | head -1)
  # 716: and the key that says WHICH rung-9 arm this is, because the rung digit no longer does - the
  # ladder names the RUNG, this switch names the BOUND, and two arms now share `STAGE90_XNU_STORAGE_PROBE=9`
  # (714's 100 ms arm and 716's 20 ms one). Read like the four above - a missing line is empty, which is
  # a different fact from a record that names it as 0 - and it is what the sentences below WRITE, so the
  # narration and the record are one value with one definition rather than a constant in prose. That
  # constant is exactly what was here before this block: the rung-9 paragraph said `= 1920000` and
  # `100 ms` in two places and would have printed them for ANY arm at this rung, which is m719's shape
  # (a value nothing reads) and this row's own subject matter - a GREEN row naming the wrong arm.
  wpt=$(sed -n 's/^STAGE90_XNU_PWR_WAIT_TICKS=//p' "$ARM_CFG" | head -1)
  wpt_txt=''; wpt_which=''
  if [[ -n $wpt ]]; then
    if [[ $wpt =~ ^[0-9]+$ ]]; then
      wpt_txt="\`STAGE90_XNU_PWR_WAIT_TICKS=$wpt\`, $(( wpt / 19200 )) ms at this device's own 19,200,000 Hz"
      if [[ $wpt == 1920000 ]]; then
        wpt_which=' **and this record carries the value 714 PRESSED**, so this is the 100 ms arm'
      else
        wpt_which=" **AND THIS ARM IS NOT THE ONE 714 PRESSED: that arm is the same rung with STAGE90_XNU_PWR_WAIT_TICKS=1920000 (100 ms), while this one's bound is $wpt ($(( wpt / 19200 )) ms). THE BOUND IS THE EXPERIMENT (experiment-715)**: if the handler's arrival is pinned to the END OF THE SPIN it arrives at about the bound, and if it is pinned to the BYTE it arrives at about 100 ms whatever the bound is. The quantity is \`_pwr_irq_at\` beside \`_wait_t0\` - the handler's own timestamp minus the spin's start - and the 100 ms arm could not read it because that arm's bound and its mask were the same length by construction"
      fi
    else
      wpt_txt="\`STAGE90_XNU_PWR_WAIT_TICKS=$wpt\` (**NOT A NUMBER**, so this line is not a bound and the paragraphs below say so rather than printing one)"
    fi
  else
    wpt_txt='**NO STAGE90_XNU_PWR_WAIT_TICKS LINE AT ALL**, so this record does not name the wait'"'"'s bound and the only place to read it is the artifact (`_wait_bound`)'
  fi
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
          entry_conseq="this press's log carries the seam pair, and on THIS arm the pair reads the operation's own cells (run_and_capture.sh:2069-2088) - and 653's EXPECTED row was STALE LINE, WRITTEN OUT, i.e. a1 == this log's own rtcpre_pop: the operation issues exactly ONE DCCIMVAC (mcr p15,0,r0,cr7,cr14,{1}, at cfmdr_loop 0x800458b0) and the built call passes len 8, so the loop runs once, over the line containing the slot. **That call's address is NOT quoted here, and 679 removed it because it is the one address in this sentence that moves with the arm**: it stood at 0x8047ca64 and is 0x8047cab4 in 678's image, because it is a site INSIDE entry_seam_flush - the function the next arm edits. The gate prints the seam's own call count and the eight bytes it covers, which is the reading; the site address is not. Clean-and-invalidate is the SAME instruction, so this cell says the invalidate ran as well as the write-back. CLEAN LINE (a1 == b1) has TWO readings as a matter of the cell table, and **686 measured that on this mechanism NEITHER of them is a verdict about the line**: both of the seam's reads are taken with \`SCTLR.C\` clear (this arm's own \`xnu_live_seam_sctlr=0x30c57879\`), so both are answered by DRAM, where the exit's own \`push {fp, lr}\` is correct BY CONSTRUCTION - the pair therefore cannot see the cached copy the pop is answered from, which survives one level below Apple's L1-only \`FlushPoU_Dcache\` at the PoU. **684's log is that measurement: \`b1 == a1 == 0x8047c9c0\`, an equal pair, on a run whose pop then died.** So read that cell TOGETHER with slot_post_calls / poll_seq, never alone - and read it as *this image's shape*, not as *the operation was inert*. CHANGED (a1 neither) is the least likely of the three. **571's claim that the invalidate does not survive the distance to the pop is measured OUT**: the four instructions 0x8004632c-0x80046338 are mrc TPIDRPRW / mov r1,#1 / ldr r0,[r0,#1484] / str r1,[r0,#304] - no cr7 write of any kind - the load is at 0xc05593bc (TPIDRPRW = 0xc0558df0, this arm's own xnu_live_pce_tpidrprw) and the store at 0x8051a130 (the dump's r0 = 0x8051a000 = cpu_data, +304), and neither is in the slot's line 0x8054fec0-0x8054feff. The live alternative is ONE LEVEL BELOW: an L2 copy that an MVA operation may not reach. **Those four and cfmdr_loop were re-measured against 678's image and are correct there**: 0x800458b0 is \`mcr 15,0,r0,cr7,cr14,{1}\` under its own \`cfmdr_loop\` symbol, and 0x8004632c is \`platform_cache_idle_exit+0x58\` and decodes to exactly the four mnemonics above - they sit before entry_seam_flush in .text, which is why they did not move with it. The frontier reading beside it is slot_post_calls, absent in every capture since 520" ;;
        measure)
          entry_arm='the MEASURE arm (574 park: the interception with its operation REMOVED, SEAM_POC=0 and SEAM_MEASURE=1)'
          entry_conseq="this press's log carries the seam pair as a CONTROL reading - whether Apple's own FlushPoU_Dcache writes the slot's line back, which 652 measured as an equal pair - and it does not by itself select a repair: that was the reading this arm existed to produce" ;;
        none)
          entry_arm='an arm that ENTERS the window with NO seam interception (SEAM_POC=0 and SEAM_MEASURE=0)'
          entry_conseq="this press's log carries NO xnu_live_seam_* key at all, so neither 638 section 3's pair table nor 642's sleh_pc join can be read on it" ;;
      esac ;;
  esac
  # --- the ending, joined to the operation it ends -------------------------------------------------
  # **683.** The block above names the ENTRY and its operation. This one says whether that operation
  # ends the run after publishing, and it is a separate join for the same reason the payload join below
  # is: the entry reading is one sentence for both arms, so the arm's name is the reading PLUS this.
  # Written once and appended in one place - a second copy of the sentences above would be one value
  # with two definitions, the defect class this project pays for most often.
  # **698: the key is a RUNG, and this branch is entered by ANY non-zero value of it, because a branch
  # that enumerates the rungs it knows is a branch that mis-names the next one in silence.** Measured on
  # the 698 press's own readiness (`/tmp/r654/press.log:689`): this line read `wst == 1 || wst == 2`, so a
  # rung-3 arm fell through to the tick arm further down and was narrated as **690's CLOCK arm** - the
  # census it was pressed for never mentioned anywhere - while the naming row still printed `ok`, because
  # that row asks whether a name was produced and not whether it is THIS arm's. The selection is now by
  # the rung's own value, **and the values this file has narration for are READ FROM THE LADDER BELOW
  # RATHER THAN LISTED HERE**: it was written as `1`, `2` and `3` and stayed that way through 701, 704
  # and 706 - a list in a comment that the branch below it outgrew, which is the same defect as the
  # branch that enumerated the rungs it knew, one level up. The ladder now carries `2` through `9`
  # (rungs 0 and 1 are the arm the set names, further down), and a rung it has no branch for is refused
  # **by the row** (the narration must quote the rung the record carries) instead of being answered by
  # falling into another arm's sentence.
  if [[ $wst =~ ^[1-9][0-9]*$ ]]; then
    # **TWO ARMS CARRY THIS KEY AND THE SET IS THE ONLY READING THAT TELLS THEM APART.** 692's
    # (`armed-storage-0da7313b`) is the one that was PRESSED, and 693's (`armed-storage-gcc-1fc30bfe`) is
    # the one in `out/` now. They share the whole entry sentence - 653's operation and 690's clock - and
    # every storage key except the five `_gcc_*` ones, so no key of the entry record distinguishes them
    # and a name for this arm is the reading PLUS the set `tools/resolve_arm_set.sh` returned from the
    # bytes of the one file `fastboot boot` sends. An unresolved set prints `<unresolved>` here and row 1
    # is where that is reported; a name this file guessed would be worse than no name at all.
    # **696: the key became a rung and this branch is the value 2, which is a different KIND of act** -
    # so it is selected by the key's own value and not by the set: at 2 the probe writes, and the set is
    # needed only to name *this* arm's record. The rung-1 sentences below are shared verbatim where they
    # are still true of it, and the one sentence that is not is corrected in its own paragraph after them.
    if [[ $wst == 2 ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 696's STORAGE arm, the first arm in this project that STORES TO A DEVICE BLOCK** (STAGE90_XNU_STORAGE_PROBE=2, resolved set \`${SET:-<unresolved>}\`), which is 693's arm - the GATE's megabyte installed before the read, then the controller's, then the gate, then six loads - **plus the vendor's own mode sequence** (\`sdhci-msm.c:2841-2868\`) at the tail of \`entry_storage_probe\`: \`CORE_HC_MODE <- 0\`, \`CORE_POWER <- |CORE_SW_RST\`, a bounded poll on \`CORE_SW_RST\`, \`CORE_HC_MODE <- HC_MODE_EN\`, \`CORE_HC_MODE <- |FF_CLK_SW_RST_DIS\`, with a readback after each store"
    elif [[ $wst == 3 ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 698's STORAGE arm, the first arm in this project that READS THE CONTROLLER'S STANDARD REGISTER FILE** (STAGE90_XNU_STORAGE_PROBE=3, resolved set \`${SET:-<unresolved>}\`), which is the rung-2 arm's whole act - the GATE's megabyte, then the controller's, then the gate read, then the four vendor stores with a readback after each - **plus a read-only census of the standard register file at the tail of \`entry_storage_probe\`, every register read at the width the vendor's own accessor uses** (\`sdhci.c:98-128\`, \`:246\`, \`:259\`: \`readb\` for \`HOST_CONTROL 0x28\`, \`POWER_CONTROL 0x29\` and \`SOFTWARE_RESET 0x2F\`; \`readw\` for \`CLOCK_CONTROL 0x2C\`, \`SLOT_INT_STATUS 0xFC\` and \`HOST_VERSION 0xFE\`; \`readl\` for \`PRESENT_STATE 0x24\`, \`CAPABILITIES 0x44\`, \`MAX_CURRENT 0x48\` and \`core_mem\`'s \`0xE0\` and \`0xE8\`), each published as a \`_reg_*\` key with \`_reg_loads\` counted bottom-up so the record's ten reads and the log's number are two derivations of one fact. **\`HOST_VERSION 0xFE\` is the register 697 section 3 left owed, and the word at offset 0 is renamed \`_mode_dma_address\`** because \`sdhci.h:27\` says \`SDHCI_DMA_ADDRESS 0x00\` - so a 696 or 697 capture's \`*hci_version\` key is this arm's \`*dma_address\`, same offset and same read"
    elif [[ $wst == 4 ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 701's STORAGE arm, the first arm in this project that STORES THROUGH \`hc_mem\`** (STAGE90_XNU_STORAGE_PROBE=4, resolved set \`${SET:-<unresolved>}\`), which is the rung-3 arm's whole act - the GATE's megabyte, then the controller's, then the gate read, then the four vendor stores on \`core_mem\` with a readback after each, then the ten-register census of the standard file - **plus \`sdhci_reset(SDHCI_RESET_ALL)\` as the vendor's own driver runs it** (\`sdhci.c:229-279\`): **ONE byte store**, \`SOFTWARE_RESET 0x2F <- 0x01\` (\`SDHCI_RESET_ALL\` is \`sdhci.h:114\`, written with \`sdhci_writeb\` at \`sdhci.c:246\` - **the first store this project has ever made through \`hc_mem\`, where every device store before it was on \`core_mem\`**), then a poll of **that same byte** with the driver's own bound (100 x \`mdelay(1)\`, \`sdhci.c:259-268\`, published as ticks on this machine's own 19,200,000 Hz counter), then five read-after values from registers this rung never writes. **The rest of \`sdhci_reset\` is guarded off by state a fresh host does not have, and the guard that matters is the one that keeps this rung small**: \`platform_reset_enter\` / \`platform_reset_exit\` are members \`sdhci_msm_ops\` does not define, and \`check_power_status(host, REQ_BUS_OFF)\` is behind \`host->pwr\` != 0 - so the unbounded \`wait_for_completion(&msm_host->pwr_irq_completion)\` in \`sdhci_msm_check_power_status\` (\`sdhci-msm.c:2179-2209\`, the one act in that path that could hang this device) is **deliberately NOT emulated**. **This arm sets NO clock**: \`sdhci_msm_ops\` replaces \`.set_clock\`, and \`sdhci_msm_set_clock\` (\`sdhci-msm.c:2402-2535\`) writes \`CORE_VENDOR_SPEC 0x10C\` - a fifth \`core_mem\` offset - and calls \`clk_set_rate\` (\`:2520\`) on the GCC's SDCC clocks, i.e. the \`0xfc400000\` megabyte 692 pressed and measured as NOT mapped, so the clock is a third block and its own step"
    elif [[ $wst == 5 ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 704's STORAGE arm, the first arm in this project that READS THE CLOCK CONTROLLER, AND IT WRITES NOTHING AT ALL** (STAGE90_XNU_STORAGE_PROBE=5, resolved set \`${SET:-<unresolved>}\`), which is the rung-4 arm's whole act - the GATE's megabyte, then the controller's, then the gate read, then the four vendor stores on \`core_mem\` with a readback after each, then the ten-register census of the standard file, then \`sdhci_reset(SDHCI_RESET_ALL)\` as ONE byte store to \`SOFTWARE_RESET 0x2F\` with a bounded poll - **plus a TWELVE-WORD read-only census of the clock surface, and not one store anywhere.** It reads the GCC's \`SDCC1_BCR 0x04C0\` and its three other branch words (\`SDCC1_APPS_CBCR 0x04C4\`, \`SDCC1_AHB_CBCR 0x04C8\`, \`SDCC1_CDCCAL_SLEEP_CBCR 0x04E4\`, \`SDCC1_CDCCAL_FF_CBCR 0x04E8\`), the apps root clock generator's five words (\`SDCC1_APPS_CMD_RCGR 0x04D0\` plus \`CFG_RCGR\`/\`M\`/\`N\`/\`D\` at \`+4/+8/+0xC/+0x10\`, \`clock-local2.c:49-53\`), \`CORE_VENDOR_SPEC 0x10C\` in \`core_mem\` - **the fifth offset of a window this project's census has bounded to four since 698** - and \`CLOCK_CONTROL 0x2C\` LAST, so that \"this census moved nothing\" is a measurement. **This is the BEFORE-VALUE arm for \`sdhci_msm_set_clock\`** (\`sdhci-msm.c:2402-2535\`), which is what writes those registers and is the next step: the arm \`CLOCK_CONTROL\` still reads 0x0003 on, bit 2 \`SD clock enable\` still clear, so no clock has been set by this image or any before it"
    elif [[ $wst == 6 ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 706's STORAGE arm, the FIRST ARM IN THIS PROJECT THAT WRITES THE CLOCK CONTROLLER** (STAGE90_XNU_STORAGE_PROBE=6, resolved set \`${SET:-<unresolved>}\`), which is 704's arm's whole act - the read-only probe, the vendor's mode sequence, the standard register file's census, the driver's own \`sdhci_reset(SDHCI_RESET_ALL)\` and the twelve-word clock census - **plus the driver's own FIRST CLOCK SET** (\`sdhci_msm_set_clock\`, \`sdhci-msm.c:2402-2535\`, at the 400000 Hz \`mmc_rescan_try_freq\` asks for): FOUR CBCR read-modify-writes of \`BIT(0)\` on the GCC in \`sdhci_msm_prepare_clocks\`' own order (pclk \`SDCC1_AHB_CBCR 0x4C8\`, clk \`SDCC1_APPS_CBCR 0x04C4\`, ff \`SDCC1_CDCCAL_FF_CBCR 0x04E8\`, sleep \`SDCC1_CDCCAL_SLEEP_CBCR 0x04E4\` - \`bus_clk\` is an ERR_OR_NULL on this board), each followed by the framework's own bounded halt check, then TWO \`CORE_VENDOR_SPEC 0x10C\` read-modify-writes (MCLK select <- DFLT \`2 << 8\`, then the HC_SELECT_IN pair cleared) - **the FIFTH and SIXTH offset of a \`core_mem\` window this project's census had bounded to four since 698** - and then the standard's own TWO \`CLOCK_CONTROL 0x2C\` halfwords with the driver's 20 ms stability poll between them: \`_clk_set_writes=6\`, of which \`_clk_set_gcc_writes=4\`. **AND IT WRITES NO RATE AT ALL**: on the first clock set \`sup_clock = get_sup_clk_rate(400000) = 400000\` equals \`msm_host->clk_rate\`, which the probe initialised to \`get_min_clock(host)\` (\`sdhci-msm.c:2795\`), so the \`clk_set_rate\` that is the RCG's only writer is SKIPPED and \`_clk_set_rate_writes=0\` - while 705 measured the register file at \`gpll4\`/div 4, i.e. 192 MHz: the driver's belief and the hardware are two readings of one quantity that disagree by 480x (experiment-706 section 2). **What it must never write is refused by the build, not by this sentence**: the GCC window's store set is asserted to be exactly \`0x4C8 0x4C4 0x4E8 0x4E4\`, so \`BCR 0x04C0\` (block reset, \`BCR_BLK_ARES_BIT\`) and every RCG word (\`0x4D0\`-\`0x4E0\`) refuse the build, and \`POWER_CONTROL 0x29\` stays unreachable in the \`hc_mem\` clause."
    elif [[ $wst == 7 ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 708's STORAGE arm, THE FIRST ARM IN THIS PROJECT THAT WRITES THE CARD'S OWN POWER REGISTER** (STAGE90_XNU_STORAGE_PROBE=7, resolved set \`${SET:-<unresolved>}\`), which is 706's arm PLUS \`mmc_power_up\`'s PASS A: the one 8-bit store \`sdhci_set_power\` makes to \`POWER_CONTROL 0x29\` (\`sdhci.c:1663\`, linked here at \`0x8000e26c\` as \`strb r4, [r6, #0x29]\` through the \`hc_mem\` base \`0xf9824900\`, so the address is \`0xF9824929\`), with the byte DERIVED from \`CAPABILITIES 0x40\` and NOT the wait that follows it. So the census's \`hc_mem\` store set is the rung-6 one plus this last offset (\`47 44 44\` -> \`47 44 44 41\`), the seam is re-pinned (\`0x800472dc\` -> \`0x800482dc\`) and every kernel address moved with it, and the pre-registration is \`docs/experiments/experiment-708-the-rung-7-pre-registration-the-card-power-byte-and-the-wait-that-is-not-taken.md\`, whose section 2 cell table carries the expected values the paragraph after this one reads against"
    elif [[ $wst == 8 ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 710's STORAGE arm, THE FIRST ARM IN THIS PROJECT WHOSE OWN INTERRUPT HANDLER IS THE VENDOR'S POWER IRQ** (STAGE90_XNU_STORAGE_PROBE=8, resolved set \`${SET:-<unresolved>}\`), which is 708's arm PLUS **the vendor's own \`sdhci_msm_pwr_irq\` (\`sdhci-msm.c:1990-2099\`) as a CLIENT of THIS image's dispatcher**: intid **170** (SPI 138, the \`pwr_irq\` \`msm8974.dtsi:502\` declares for \`sdhc_1: sdhci@f9824900\`, beside the sibling \`hc_irq\` = SPI 123 = intid 155, which this arm does NOT take) registered by \`entry_irq_register_client(170, st_pwr_irq, 0)\` and its line armed by \`entry_irq_enable_line(170, 0x01)\`, **both BEFORE the card-power byte** - so the latch the byte sets is answered by a handler instead of being left set, which is exactly what 709's press measured from the other end: the rung-7 arm's log carries \`_irq_other_count=0x00000001\` and \`_irq_other_iar=0x000000aa\` (170), the arm taken for a line handed to nobody, and the run ended there. The handler is a **three-store** body - \`CORE_PWRCTL_STATUS 0xDC\` read as a byte AND as a word from one moment, \`CORE_PWRCTL_CLEAR 0xE4 <- the status\` (the vendor's own acknowledge, \`:2872-2889\`), \`CORE_PWRCTL_CTL 0xE8 <- the ack\` with the readback 706's lesson asks for, and \`hc_mem + 0x10C\`'s read-modify-write (act 6: \`IO_HIGH\` CLEARS \`CORE_IO_PAD_PWR_SWITCH\` bit 16) - **and it is the first client in this image whose line belongs to the STORAGE block**. **The three arms that may sleep are absent ON PURPOSE**: the vendor's own \`devm_request_threaded_irq(..., NULL, sdhci_msm_pwr_irq, IRQF_ONESHOT, ...)\` (\`:2937-2939\`) declares a *threaded* handler because \`sdhci_msm_setup_vreg\` / \`setup_pins\` / \`set_vdd_io_vol\` may sleep, and this image's client runs in \`fleh_irq_kernel\`'s frame - so a build whose handler reaches one of them is refused by the build and not by this sentence. The register this ladder has been reading at the wrong address is read at BOTH addresses in one run before the byte (\`_pwr_irq_vendor_core\` is \`core_mem + 0x10C\`, \`_pwr_irq_vendor_hc\` is \`hc_mem + 0x10C\` - experiment-710 section 1.5: \`CORE_VENDOR_SPEC\` is used 23 times in \`sdhci-msm.c\`, **all 23 through \`host->ioaddr\`, zero through \`msm_host->core_mem\`**), and the pre-registration is \`docs/experiments/experiment-710-the-rung-8-pre-registration-owning-intid-170-and-the-vendor-spec-address-the-ladder-has-been-reading-in-the-wrong-window.md\`, whose section 2 cell table carries the expected values the paragraph after this one reads against"
    elif [[ $wst == 9 ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 712's STORAGE arm, THE FIRST RUNG WHOSE ACT IS A WAIT** (STAGE90_XNU_STORAGE_PROBE=9, resolved set \`${SET:-<unresolved>}\`), which is 710's arm PLUS **the driver's own next statement after rung 7's byte** - \`sdhci_set_power\`'s \`check_power_status(host, REQ_BUS_ON)\` (\`sdhci.c:1371-1372\`), whose vendor implementation \`sdhci_msm_check_power_status\` (\`sdhci-msm.c:2179-2210\`) is **a cache read of two driver-side fields plus an UNBOUNDED \`wait_for_completion\`** (\`:2205\`). The predicate is the vendor's own and is evaluated FIRST: \`req_type\` (here \`REQ_BUS_ON\` = \`BIT(1)\`, \`sdhci.h:291\`) against \`msm_host->curr_pwr_state\` and \`curr_io_level\`, the two fields **the handler's tail fills** (\`:2092-2094\`, which is why rung 8's client is this rung's completion and not its neighbour). The vendor's own \`else\` branch is TAKEN rather than skipped: on it \`init_completion\` RESETS the completion and does **not** wait (\`:2203\`), so an arm that waited anyway would be measuring its own code - and the block itself is replaced by a **bounded tick poll whose end condition is the CONTROLLER's own \`CORE_PWRCTL_CTL\` bit \`BUS_SUCCESS\` (0x01) and NOT the image-side flag**. That choice is a defect this project names, met in advance: the probe runs inside Apple's cache-off idle-exit window (\`xnu_live_seam_sctlr = 0x30c57879\`, \`C\` clear) while the handler may run with the caches ON, so a \`g_pwr_irq_done\` written by the handler can sit in L1/L2 while the probe's read of the same address is answered by DRAM - **one flag, two definitions** - and a poll armed with it would time out on a machine whose handler had already run; \`CTL\` is the same event *through the device* (Strongly-ordered), read as \`0\` by the probe on rung 7's press and written \`0x01\` by the handler on rung 8's. **The image-side flag is still written and still published** (\`_wait_done\`), because the disagreement between the two is the reading. The body is \`__attribute__((noinline))\` and that is **the mirror of rung 6's \`always_inline\`**: the helpers that write device registers are inlined so the store census can see them, this one writes no device register at all, and what the build must check is its bounded SHAPE - the budget constant, the ack it spins on, and the probe's single call to it - and a body whose shape a clause must read has to be a body. **THE SWITCH THAT NAMES WHICH RUNG-9 ARM THIS IS IS ITS BOUND AND NOT ITS RUNG DIGIT: $wpt_txt** (the rate read out of the hardware, \`xnu_live_post_cntfrq\` on 691's, 694's and 711's runs; 19200 ticks is a millisecond here and not an assumption).$wpt_which; its range is \`[1, 19200000]\` refused inside \`entry_storage.c\` by an \`#error\`, with **0 REFUSED rather than read as \"no wait\"** because rung 8 IS the no-wait arm (a budget of 0 would make this rung's cells indistinguishable from the rung below it while the record said otherwise) and 19200000 (1 s) the ceiling because this arm's run ends on 690's 6,000 ms clock. The waiter is its own symbol (\`st_pwr_wait\`, \`0x250\` bytes at \`0x8000d230\`, which pushed \`entry_storage_probe\` to \`0x8000d480\`) and the probe calls it **once**, immediately after \`st_power_set()\` and **before** rung 8's tail count, because \`sdhci.c:1370-1372\` is the byte and the check as adjacent statements of one function and an arm that ran it anywhere else would be measuring its own order rather than the driver's. The pre-registration is \`docs/experiments/experiment-712-the-rung-9-pre-registration-the-drivers-own-completion.md\`, whose section 2 cell table carries the expected values the paragraph after this one reads against"
    elif [[ $wst == 10 ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 718's STORAGE arm, THE FIRST RUNG THAT TAKES THE WAIT WITH INTERRUPTS UNMASKED** (STAGE90_XNU_STORAGE_PROBE=10, resolved set \`${SET:-<unresolved>}\`), which is 712's arm PLUS ONE MECHANISM AND NO VALUE: the same \`st_pwr_wait\`, the same \`CORE_PWRCTL_CTL\` end condition, the same 1024-read batch between two clock samples and **the same bound ($wpt_txt)** - but with the CPU's \`I\` bit CLEARED for the poll and the mask put back from the SAVED register afterwards. **The mechanism is 717's measurement and not a preference.** 712's arm at 20 ms measured the completion \`20.172 ms\` after the byte and the handler \`60.78 us\` after the spin gave up, against \`100.122 ms\` and \`60.05 us\` on the 100 ms arm - the handler's distance from the spin's END did not move while its distance from the BYTE moved with the bound - so **the poll has been measuring its OWN MASK and no budget can change that**: the mask ends where the code that owns it returns and the spin is inside that code. The vendor's \`wait_for_completion\` works because it SLEEPS, i.e. because its context can be interrupted, and **every site inside this fixture is masked** (\`_wait_cpsr\` reads \`0x80000093\` on rungs 9 and 10), so of 717 section 2's two shapes - unmask around the spin, or take the wait where the mask is already clear - only this one exists here. **THE ARM IS THE VALUE OF \`_wait_cpsr\`, AND THE NEW CELL IS THE STATE IT GIVES BACK**: \`_wait_cpsr\` keeps its definition, *the CPSR the poll runs under*, and must now read \`0x80000013\` - SVC with \`I\` clear and every other bit identical - while **\`_wait_cpsr_after\`** is the new quantity, *the CPSR the probe gives back*, which must read \`0x80000093\`: the value rungs 9 and 10 read as their PREMISE, which is what says the restore restored the SAVED state and not a constant that happens to look tidy. The restore writes the saved register rather than \`cpsid i\` for exactly that reason, and \`cpsie i\` clears only \`I\` (\`F\` is already clear, the mode is untouched). **THE OUTCOME CELLS ARE RUNG 10'S, FLIPPED, AND THEY ARE THE POINT**: \`_wait_timeout\` must read \`0\` (it read \`1\` on BOTH rungs 9 and 10) beside \`_wait_ctl_after\` = \`0x01\` (it read \`0x00\`), with \`_wait_done\` = \`1\` and \`_wait_calls\` = \`1\` - **the vendor's predicate satisfied for the first time on this ladder**, and **the first time the waiter reads the handler's two driver-side fields AFTER the handler wrote them** (on rungs 9 and 10 those reads happened before the handler had run, so the flag-against-ack pair has never had a comparison; \`_wait_done\` = 1 beside \`_wait_ctl_after\` = 0x01 is that comparison agreeing, and \`_wait_done\` = 0 beside \`_wait_ctl_after\` = 0x01 is 686's cache-boundary defect measured instead of argued). **AND THIS IS THE FIRST INTERRUPT THIS IMAGE TAKES INSIDE THE CACHE-OFF WINDOW**: \`xnu_live_seam_sctlr\` reads \`C\` clear on every run of this ladder, and every earlier delivery happened ~50 us after the spin, on the way OUT of the window - which is the one state the payload masks \`I\` to avoid, and the reason this press is not a formality: the DELIVERY is nearly certain (this site has taken the handler three times) and the CONTEXT is entirely new. The waiter is its own symbol (\`st_pwr_wait\`, \`0x284\` bytes at \`0x8000d230\`; the 52-byte growth pushed \`entry_storage_probe\` from \`0x8000d480\` to \`0x8000d4b4\` with its own size unchanged at \`0x173c\`) and the probe still calls it **once**, immediately after \`st_power_set()\` and before rung 8's tail count. The pre-registration is \`docs/experiments/experiment-718-the-rung-11-pre-registration-the-mask-comes-off-in-the-window.md\`, whose section 3 cell table carries the expected values the paragraph after this one reads against"
    elif [[ $SET == armed-storage-0da7313b ]]; then
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 692's STORAGE arm, the one whose press the next sentences record** (STAGE90_XNU_STORAGE_PROBE=1, resolved set \`$SET\`), which runs that same operation and the same ending, and whose new act is **a device and not a site**: \`__wrap_platform_cache_idle_exit\` calls \`entry_storage_probe()\` on its first return, before the clock's block, and the probe installs **one** 1 MB section for the eMMC controller (\`0xf9824000 >> 20 == 0xf9824900 >> 20 == 0xF98\`, 531's two windows inside one section) and then reads six of its registers"
    else
      entry_arm="$entry_arm - **and this is NOT any of those arms either: it is 693's STORAGE arm** (STAGE90_XNU_STORAGE_PROBE=1, resolved set \`${SET:-<unresolved>}\`), which is 692's arm with **one more mapping taken first**: \`__wrap_platform_cache_idle_exit\` calls \`entry_storage_probe()\` on its first return, before the clock's block, and the probe takes **the GATE's own megabyte** (\`entry_mmio_section(0xfc400000, 0xfc400000, ...)\`, L1 index \`0xFC4\` - the megabyte \`entry_epilogue\`'s PS_HOLD store also lives in) **before it reads the gate**, then installs the eMMC controller's own (\`0xF98\`) **before** that read, and then reads six of the controller's registers"
    fi
    entry_conseq+=" **THIS ARM READS A BLOCK THE IMAGE HAS NEVER TOUCHED, SO ITS ANSWER IS A READING AND ITS HAZARD IS A BUS WAIT.** The reads are gated and the gate is published: \`xnu_live_storage_gate\` is \`BIT(0)\` of SDCC1's CBCR (\`0xFC4004C4\`, in a megabyte \`0xFC4\` **and 692 WAS PRESSED, so this is now measured rather than inferred: that megabyte is NOT mapped, and the probe's third line faulted on it.** \`xnu_live_sleh_far_frame=0xfc4004c0\` with \`fsr_frame=0x5\` (a section translation fault, on a read), \`pc=0x8000d0c4\` = \`entry_storage_probe+0xcc\` = \`ldr r1,[r3,#1216]\` with \`r3=0xfc400000\` (692's BCR read), and the panic's own \`r0=0x80487fc8\` is the pointer to the string \`xnu_live_storage_bcr\`. **That \`r0\` is a reading of 692's entry image and NOT of this one, and the difference is a defect a reader can be caught by**: the string pool moved three keys, so \`0x80487fc8\` is \`xnu_live_storage_gcc_section\` in this arm's image and a fault at the same instruction here would print the leftover \`r0=0x80488168\` (\`_ttbr1\`, the last key written before the gate's loads are scheduled). **The \`pc\` identifies the site; a data abort's \`r0\` is a leftover register.** **The parenthetical here used to read \"in a megabyte \`entry_epilogue\`'s own PS_HOLD store proves mapped\", and that proof took a STORE inside a code path for evidence an ADDRESS is mapped** - the class of \`mi4-a-device-address-can-be-right-and-undereferenceable\`, whose citation is 532, the experiment this arm's plan came from. It was contradicted by the previous arm's own parked log, unjoined until 692: 690's fatal abort is \`pc=0x8047b488\` = \`entry_seam_end_run+0xc\`, the \`str r3,[r2,#0x65c]\` with \`r2=0x0fa00000\` (RESTART_REASON), \`far_frame=0x0fa0065c\` - **the forced ending faulted on its FIRST store** and never reached the \`dsb\`, the PS_HOLD store or the \`wfe\`, so no arm has been observed to complete a PS_HOLD store. **So read this arm's press in the order the probe runs and stop where the log stops.** The five keys 692 got - \`_calls=1\`, \`_live_state=1\`, \`_section=0x00000f98\`, \`_hc_mem_section=0x00000f98\` and \`_windows_share_section=1\` - are still published first and are still the same readings (both windows really are in one 1 MB section, so 532 section 6's step 2 is one install); then, on 693's arm only, the GATE's install publishes \`_gcc_map\`, \`_gcc_slot_before\` and \`_gcc_desc\`, and \`_gcc_section\`/\`_gcc_share_section\` were published with the five; then the controller's install publishes \`_map\`, \`_slot_before\`, \`_desc\`, \`_l1\`, \`_l1_moved\`, \`_ttbr0\` and \`_ttbr1\`; then \`_gate_read\` with \`_bcr\`, \`_cbcr\` and \`_gate\`. **A \`_gcc_map\` of 0 is the cell no earlier arm could have**: it says the GATE's megabyte could not be installed, and on that reading none of \`_bcr\`/\`_cbcr\`/\`_gate\` is published at all, so the run answers 692's fault without answering the controller. **The three end cells are told apart by three keys and not by two, because the two installs come first**: \`_gcc_map=0\` (the gate's megabyte is absent and nothing was read through it - 692's fault, with the reason in \`_gcc_slot_before\`/\`_gcc_desc\`), \`_map=0\` with \`_gate_read=1\` (the gate was read and the controller's own section refused), and \`_gated_out=1\` with \`_loads=0\` (both sections are installed, the gate was read and is closed, and the block was deliberately not touched). A \`_loads=6\` with \`_writes=0\` is the run this arm exists for. **\`xnu_live_storage_writes=0\` is the arm's own safety reading and not a comment**: the vendor's own probe opens with three writes (\`CORE_HC_MODE <- 0\`, \`CORE_SW_RST\`, \`HC_MODE_EN\`) and 531 section 8 adds a fourth reason not to - writing 0 to \`POWER_CONTROL 0x29\` **is** a bus-off request - so this arm's answer is bought with loads alone. **The two pre-registered cells are the bits 531 section 6 left open**: \`xnu_live_storage_mode_bit\` (\`CORE_HC_MODE & HC_MODE_EN\`) reads 1 if the block the bootloader handed over is already in SDHCI mode - the mode sequence is then a *re-do* - and 0 if it is still ahead of us, and \`xnu_live_storage_sw_rst\` (\`CORE_POWER & CORE_SW_RST\`) reads 1 if something is holding the core in reset, which would make the version word's answer meaningless rather than absent. A \`_mci_version\` of 0 or 0xffffffff is *the block did not answer*, which is a reading of the same arm and not a failed run. **The negative cell is a NON-RETURN (exit 2) and it is this arm's own hazard, named here rather than discovered**: a load from a block whose clock is off is a bus wait nothing ends, the run does not come back, and it owes a power press - which is why the gate read exists and why the exit-2 cell means \`the gate was READ, said the branch was on, and the block still did not answer\`, not \`the mapping failed\` (a mapping failure with a live gate is a translation fault and comes back with a log - **and 692 measured WHICH address faults first, which is why this arm installs the GATE's megabyte before it reads anything**: the fault was the gate's own \`0xfc4004c0\` and not the controller's \`0xf9824000\`, and the readiness cell that named the controller's address **named the wrong site while getting the shape right**. 693's arm is that one change - \`entry_mmio_section\` for \`0xFC4\` ahead of the read, at an L1 index of its own so 532 section 3.2's occupied-slot refusal cannot fire, and the storage block's install moved ahead of the read too so a gated-out run still publishes its four numbers - and the same question then applies to the reset path's own two stores, \`0x0fa0065c\` (faulted on 690's arm) and \`0xfc4ab000\` (in this arm's \`0xFC4\` megabyte, so \`_gcc_map=1\` beside a readable \`_bcr\` is also the measurement that store becomes reachable)**). **The ending is 690's clock and it did not move**, so \`xnu_live_post_end_calls\` is still this arm's own ending cell and every consequence above about the elapsed pair holds unchanged; the probe's records precede it because the call is placed before the clock's block."
    if [[ $wst == 2 || $wst == 3 || $wst == 4 || $wst == 5 || $wst == 6 || $wst == 7 || $wst == 8 || $wst == 9 || $wst == 10 ]]; then
      # **The correction is the rung-2 AND rung-3 arm's, because the sentence it corrects is the shared
      # rung-1 one and a rung-3 arm carries it too** - `_loads=6` with `_writes=0` is the rung-1 arm's
      # run, and every rung from 2 on writes. (Measured on the 698 press's own readiness: while this
      # guard read `$wst == 2` alone, a rung-3 arm got the rung-1 sentence with no correction beside it,
      # which is the same defect one paragraph over - a correction that is not carried to the arm it is
      # true of is a sentence the reader is left with.)
      entry_conseq+=" **AND ONE SENTENCE ABOVE IS THE RUNG-1 ARM'S AND NOT THIS ONE.** \"A \`_loads=6\` with \`_writes=0\` is the run this arm exists for\" reads, on THIS arm, as **\`_loads=6\`, then the refusal cell OR four stores**: \`_writes\` is published as the sequence goes (1 after \`CORE_HC_MODE <- 0\`, 2 after \`CORE_POWER <- |CORE_SW_RST\`, 3 after \`HC_MODE_EN\`, 4 after \`|FF_CLK_SW_RST_DIS\`) with \`_mode_stage\` beside it (0 at entry, 2, 4, 8), so a run that stopped mid-sequence is localised by the pair and not by where the log ends. **So \`xnu_live_storage_writes=0\` is NOT this arm's safety reading; these are:** (1) the four stores are the ONLY stores in the whole body - \`build_entry.sh\` refuses the build unless the offsets on the controller's base are exactly \`0x78/0x00/0x78/0x78\` in that order and every other non-\`sp\` store is an offset-0 pointer write, so \`POWER_CONTROL 0x29\` (in the OTHER window, \`hc_mem\`, where writing 0 **is** a bus-off request on this SoC) is unreachable by construction rather than by review; (2) the sequence refuses itself before any store unless the block answered a read - \`_mci_version\` of 0 or 0xffffffff gives \`_mode_refused=1\` with \`_mode_stage=0\` and \`_writes=0\`, the only cell in which this arm writes nothing. **The failure path is pre-registered and it is a cell of its own**: \`_mode_timeout=1\` with \`_writes=2\` and \`_mode_stage=4\` says the poll expired on \`CORE_SW_RST\` and the arm made **no further store** - the bound is 1,920,000 CNTVCT ticks = 100 ms at the 19,200,000 Hz this device reported, with \`_mode_rst_ticks\` recording what the reset actually took (the vendor's own comment says ~40 us). **And the arm's question is answered by \`_mode_bit_after\`**: 1 means \`CORE_HC_MODE\` now carries \`HC_MODE_EN\` where 694 measured it clear (\`_mode_bit=0\`, the reading that made this sequence a prerequisite), with \`_mode_w2_read=0x00002001\` as the vendor's own end state bit for bit; \`_mode_dma_address\`/\`_mode_capabilities\` re-read in SDHCI mode (697s arm and captures name the first of those two \`*hci_version\` - same offset, same read, renamed by 698 after \`sdhci.h:27\`; **this line was the FOURTH reader of that name and 698s rename census counted three**, because the census greps the full \`xnu_live_storage_\` name and this prose writes the abbreviated form - which is why the naming row below now checks every name this narration writes against the keys the image actually publishes) are the first spec-valid look at that register file (694's pair was taken through a block **not** in SDHCI mode, so a differing pair is the mode change made visible and an identical one is the finding that this bit alone does not move those two words); and \`_mode_pwrctl_status\` is the hazard this arm names rather than manages - the vendor's comment says the reset may latch a power-IRQ status when the previous state was BUS_ON, which \`_core_power=0x441\` says it was. **The negative cell is a NON-RETURN (exit 2) and it is this arm's own, for the reason the bound cannot answer**: the tick bound bounds the POLL, and a load from a block whose clock is off is a bus wait nothing ends - which is why the gate is read first and why \`_gate_read\`/\`_bcr\`/\`_cbcr\` are published before any store. A non-return refutes none of the cells above; it means the gate was wrong, or the block did not tolerate the reset. **Nothing else about the run changed**: the same operation, the same ending on the same 690 clock, the same key order with the eleven \`_mode_*\` keys last."
    fi
    if [[ $wst == 3 ]]; then
      entry_conseq+=" **AND THE RUNG-2 SENTENCES ABOVE HOLD FOR THIS ARM UNCHANGED - ITS ADDITION IS THAT IT READS, AND THE READS ARE WHAT THIS PRESS BUYS.** Every cell the arm pre-registered is filled by 699's press, and three of them are what make the NEXT step cheap rather than discovered: (1) \`_reg_power_control\` is **the before-value the driver's own \`sdhci_reset(SDHCI_RESET_ALL)\` needs** - bit 0 is \`BUS_POWER\`, and on this press it read 0x00, so the standard block does not believe the bus is powered and the reset's own \`sdhci_writeb(host, 0, SDHCI_POWER_CONTROL)\` is harmless by construction rather than by argument (698 section 2's branch, and the publisher of that reset must read this register back); (2) \`_reg_pwrctl_mask\` answers **the half a status register cannot** - 697's \`_mode_pwrctl_status\` of 0 means the power IRQ did not latch, and the mask (0x0000000f on the press) is what says one would have been routed, so the two together read NOTHING LATCHED WHILE THE MASK WAS ARMED rather than NOTHING WAS LISTENING; (3) \`_mode_host_version\` is **the register 697 section 3 proved the image was not reading** - 0x00001102 is spec 2 and vendor 0x11, and the vendor's own driver branches on that nibble against \`SDHCI_VER_100\` 0x2B (\`sdhci-msm.c:2909-2914\`), so a value that is not 0x2B means the \`SDHCI_QUIRK2_SLOW_INT_CLEAR\` branch is not this hardware's. **The widths are a property of the artifact and not a style**: for a rung of 3 or more \`build_entry.sh\` refuses the build unless every access in the probe whose base is not \`sp\` has a plain immediate offset satisfying its own width's alignment, because an unaligned access to a Strongly-ordered device section FAULTS on ARMv7 - 692's abort class, reached by alignment instead of by translation. **AND THE SHARED SENTENCE ONE PARAGRAPH ABOVE IS CORRECTED FOR THIS RUNG TOO**: \`_loads=6\` and \`_writes=4\` are BOTH present on a completed run of this arm, with the census's \`_reg_loads=10\` behind them, so the rung-1 sentence's \`_writes=0\` is not a cell of this arm's log at all. **What is NOT new here**: no store, no command, no sector read, nothing mounted - the four device stores are still the whole write set and \`_reg_loads=10\` is the count of the reads, so a \`_reg_*\` key in this log is a survey of registers and never an act on the medium; and a 696 or 697 capture carries NO \`_reg_*\` key at all, because the block did not exist in those arms. **The ending is 690's clock and it does not move**, the census running after the mode sequence and before the clock's block, so a run that ends at the ending still carries all of \`_reg_*\`."
    fi
    if [[ $wst == 4 ]]; then
      entry_conseq+=" **AND THE RUNG-2 AND RUNG-3 SENTENCES ABOVE HOLD FOR THIS ARM UNCHANGED - ITS ADDITION IS ONE BYTE WRITTEN TO ONE REGISTER, AND THE FIVE READS BESIDE IT ARE WHAT MAKE THAT BYTE A MEASUREMENT.** The before-values are 699's press and that is why this rung could be designed at all: \`_reg_software_reset\` read 0x00, so the poll's premise (nothing is in progress, so a bit that does not clear afterwards is the reset FAILING and not a leftover) is a reading rather than an assumption; \`_reg_power_control\` read 0x00, so the standard block does not believe the bus is powered; \`_reg_present_state\` had both inhibit bits clear, so the reset is not being asked to abort anything; and \`_reg_pwrctl_mask\` read 0x0000000f, so the power-IRQ hazard is ARMED and readable as armed. **The cells this arm publishes, in the order they mean something**: \`_rst_wrote\` is 1 if the store was reached and \`_rst_stage\` says how far the function got (0 at entry, 1 after the store, 2 in the poll, 3 done), so a stopped run is localised by the pair; \`_rst_refused\` is 1 only if \`SOFTWARE_RESET\` already carried the reset bit at entry - the register's own self-clearing contract makes that the one state in which writing it would act on a block mid-reset, and 699's reading of 0 is why it is the refusal path and not the expected one; \`_rst_polls\` is 1 in the expected case (697 measured the same controller's \`CORE_SW_RST\` already clear on its first read), \`_rst_steps\` and \`_rst_ticks\` separate a slow reset from a hit bound, and \`_rst_timeout\` with \`_rst_cleared=0\` is the driver's own \`Reset 0x%x never completed\` path - **a reading of the hardware and not a failed run**. \`_rst_stores\` is the count of stores this rung makes, counted bottom-up like \`_reg_loads\`, so the record's \"one store\" and the log's number are two derivations of one fact. **\`_reg_power_control_after\` is the cell 698 section 2 owed**: 0x00 means the standard reset did NOT turn the bus on, and a value with bit 0 set would mean the reset changed the block's belief about the bus, which the driver would then have to reconcile with the vendor's own power state - and \`_reg_pwrctl_status_after\` is the hazard's cell, 0 meaning nothing latched even though \`_reg_pwrctl_mask\` says four bits would have been routed. **WHAT PROVES THE RUNG DID NOT WANDER IS A BUILD CLAUSE AND NOT THIS SENTENCE**: for a rung of 4 \`build_entry.sh\` refuses the build unless the stores on \`core_mem\` are exactly \`0x78/0x00/0x78/0x78\` in that order AND every store in \`hc_mem\` is exactly one, at offset 47 (0x2F), as a BYTE store - so \`POWER_CONTROL 0x29\`, four offsets away in the same window, where writing 0 **is** a bus-off request on this SoC, is unreachable by construction; and below rung 4 ANY store in \`hc_mem\` refuses the build. **The negative cell is a NON-RETURN (exit 2), and it is this rung's own on the WRITE side**: a store to a block whose clock is off is a bus wait nothing ends, the run does not come back, and it owes a power press - which is the reason the write is one byte to one register and not a sequence. **What is NOT new here**: no clock, no \`POWER_CONTROL\` write, no \`CORE_PWRCTL\` write, no \`CLOCK_CONTROL\` write, no command, no sector read, nothing mounted - \`_rst_stores=1\` and the four inherited \`_writes=4\` are the whole write set, and a \`_rst_*\` or \`_reg_*_after\` key in this log is still a register and never the medium. **The ending is 690's clock and it does not move**, the reset running after the census and before the clock's block."
    fi
    if [[ $wst == 5 ]]; then
      entry_conseq+=" **AND EVERY SENTENCE ABOVE HOLDS FOR THIS ARM UNCHANGED, BECAUSE ITS ADDITION IS TWELVE READS AND NOTHING ELSE - AND ONE OF THOSE READS CORRECTS THE SENTENCE THAT HAS GUARDED THIS WHOLE LINE SINCE 692.** Since 692 the probe has published \`xnu_live_storage_gate\` as \`_cbcr & BIT(0)\` and this narration has called it the branch enable. \`BIT(0)\` of a CBCR is \`CBCR_BRANCH_ENABLE_BIT\` - **the bit the driver WRITES, i.e. the REQUEST** (\`clock-local2.c:62\`, written at \`:380-382\`) - and what \"the clock is running\" means is a second bit two lines away, \`CBCR_BRANCH_OFF_BIT\` = \`BIT(31)\` (\`:63\`), which the framework polls to a bound before it believes the clock is on (\`:386-387\`) and which its own handoff test reads ALONE (\`branch_clk_handoff\`, \`:490-497\`). **THE TWO CAN DISAGREE**, and the ordinary state in which they do is a branch whose enable is requested while the ROOT above it is off - the clock has not propagated, and a register read through it is the bus wait nothing ends. 694's \`_cbcr = 0x00004ff1\` satisfies BOTH halves, so nothing measured so far is overturned; what was wrong is that the gate was reading HALF OF ITSELF, and this arm publishes all three bits - \`_clk_<branch>_en\` (BIT(0)), \`_clk_<branch>_off\` (BIT(31)) and \`_clk_<branch>_hw\` (BIT(1), \`CBCR_HW_CTL_BIT\`, set means the framework skips its own halt check) - for each of the four branches. **THE CELL THAT MATTERS MOST IS \`_clk_ahb_en\`**: \`SDCC1_AHB_CBCR 0x04C8\` is the \`pclk\` \`sdhci_msm_prepare_clocks\` enables (\`sdhci-msm.c:2315\`), **no run in this project has ever read that register**, and the register file answering today only SUGGESTS the branch is on. A 0 there beside an answering controller would be a finding about which clock actually gates it, and its \`branch_clk\` is the one carrying \`has_sibling = 1\`, which the framework turns into \`-EPERM\` for \`round_rate\`/\`list_rate\` (\`:444-446\`, \`:460-462\`) - so enabling it is a different act from enabling the apps branch. **\`_clk_bcr_ares\` IS THE OTHER ARTICLE**: 694 published \`_bcr = 0\` without a key saying what a bit of it means, and \`BCR_BLK_ARES_BIT\` = BIT(0) (\`:66\`) clear is what makes the branch readings mean \"the clock is off\" rather than \"the block is held in reset and nothing it says is about anything\". **\`_clk_rcg_update\` IS THE NEXT WRITE RUNG'S PREMISE**: \`rcg_update_config\` (\`:90-108\`) sets \`CMD_RCGR\`'s update bit and polls THAT SAME BIT clear to a bound of \`UPDATE_CHECK_MAX_LOOPS 500\` (\`:44\`), so a 0 before anything writes is what makes \"a bit that does not clear afterwards is a FAILED UPDATE\" a reading and not an assumption - and \`_clk_rcg_root_en\`, \`_clk_rcg_src\`, \`_clk_rcg_div\` and \`_clk_rcg_m/n/d\` are the apps root's current configuration, i.e. the words \`set_rate_mnd\` (\`:126-146\`) will write. **WHAT THIS ARM DOES NOT DO, and the two claims it must not be read as making**: it publishes NO RATE (the MND and source-select FIELDS are published; converting them to Hz needs the parent's rate - \`gpll0\`/\`gpll4\`/\`cxo\`, \`clock-8974.c:1564-1574\` - which is a table this image does not carry, and a computed frequency beside the framework's own cached rate would be the same one-value-two-definitions defect one level up); and it ENABLES NOTHING: if \`_clk_ahb_en = 0\` the finding is published, not acted on. **WHAT PROVES THE RUNG DID NOT WANDER IS A BUILD CLAUSE AND NOT THIS SENTENCE**: since 704 \`build_entry.sh\`'s store classifier knows the GCC megabyte as its own window and asserts its store set is EMPTY AT EVERY RUNG, so a store to \`0xfc400000\` refuses the build - and before 704 such a store was refused anyway, by the \`DEVBAD\` clause whose stated subject is \"not one of the two windows this controller declares\": true, and a statement about the classifier's scope rather than about the clock. **The negative cell is a NON-RETURN (exit 2)**: a store to a clock-control register whose parent root is off is the bus wait nothing ends - which is why this arm writes nothing and why the writer (\`sdhci_msm_set_clock\`) is a step of its own. **What is NOT new here**: no clock, no \`POWER_CONTROL\` write, no \`CLOCK_CONTROL\` write, no command, no sector read, nothing mounted - the write set is still \`_rst_stores=1\` plus the four inherited \`_writes=4\`, and every \`_clk_*\` key in this log is a register and never a rate."
    fi

    if [[ $wst == 6 ]]; then
      entry_conseq+=" **AND EVERY SENTENCE ABOVE HOLDS FOR THIS ARM UNCHANGED, BECAUSE ITS ADDITION IS THE VENDOR'S OWN CLOCK SET AND NOT A NEW KIND OF ACT - WITH THREE THINGS THAT ARE NEW AND ARE THE ONES TO READ.** (1) **THE SD CLOCK ENABLE BIT IS SET FOR THE FIRST TIME BY THIS IMAGE**, and it is the last store of the rung: \`_clk_set_cc_card\` is the word written (expected \`0xE045\` = the divisor 960 packed beside \`SDHCI_CLOCK_INT_EN | SDHCI_CLOCK_CARD_EN\`), against 705's \`_clk_clock_control_after=0x0003\` and 699's \`_reg_clock_control=0x0003\` before it - so a run whose last key is \`_clk_set_cc_int\` (\`0xE041\`, SD clock still off) either stopped between the two stores or did not reach the second. (2) **IT IS THE FIRST RUNG WHOSE WRITES COULD STALL THE BLOCK**, so the negative cell is again a NON-RETURN (exit 2) and it is localised rather than discovered: \`_clk_set_writes\` is published after EVERY store, so a run that stops inside the sequence comes back with the count it had reached and the run that hangs is the one whose next store is the missing number - \`_clk_set_ahb_*\`/\`_apps_*\`/\`_ff_*\`/\`_sleep_*\` carry each branch's own before/after and the halt check's poll count and terminal state. (3) **THE CARD IS UNPOWERED AND NOTHING FOLLOWS THE CLOCK**: the driver's own order is clock-then-power (its comment says so, \`sdhci.c:1651-1657\`), so the SD clock is enabled at 200 kHz into a bus whose power register is still 0 exactly as 699 left it, and THIS ARM ISSUES NO COMMAND, READS NO SECTOR AND MOUNTS NOTHING. **WHAT IT DELIBERATELY DOES NOT DO IS THE NEXT RUNG'S SUBJECT**: the same \`sdhci_do_set_ios\` call reaches \`sdhci_set_power\`, whose first act is to write **0** to \`POWER_CONTROL 0x29\` - the bus-off request 531 section 8 names - and then to call \`check_power_status\`, which on this SoC is an unbounded \`wait_for_completion\` (\`sdhci-msm.c:2179-2209\`); both are kept out of this image by the \`hc_mem\` clause and both are named in experiment-706 section 4."
    fi
    if [[ $wst == 7 ]]; then
      entry_conseq+=" **AND EVERY SENTENCE ABOVE HOLDS FOR THIS ARM UNCHANGED, BECAUSE ITS ADDITION IS ONE BYTE AND NOT A NEW KIND OF ACT - WITH THREE THINGS THAT ARE NEW AND ARE THE ONES TO READ.** (1) **\`_pwr_after\` IS THE READBACK THAT SAYS THE STORE TOOK, AND RUNG 6 IS WHY THAT IS THE CELL TO READ**: \`_pwr_before\` -> \`_pwr_after\` expected \`0x00\` -> **\`0x0B\`**, with \`_pwr_wrote\` = \`0x0B\` (\`SDHCI_POWER_180 | SDHCI_POWER_ON\`) and \`_pwr_vdd\` = \`7\`; \`0x0F\` here would mean the 3.3 V capability bit was set and this arm's reading of \`_pwr_cap\` was wrong, and \`_pwr_refused\` = 1 means the derivation found no map entry (the vendor's own \`BUG()\` arm), so the store did NOT happen and \`_pwr_done\` is 0. \`_pwr_cap\` is a RE-READING of 705's \`_mode_capabilities\` (\`0x742dc8b2\`), not a constant - a different word is a reading about this boot. (2) **THE WAIT IS NOT TAKEN AND THAT IS THIS RUNG'S ONE JUDGEMENT, PUBLISHED RATHER THAN HIDDEN**: \`sdhci_msm_check_power_status\` takes its \`done\` only from the threaded \`sdhci_msm_pwr_irq\`, which this image cannot deliver (measured silent in 708 section 1.3: \`_reg_pwrctl_mask\` \`0x0f\`, \`_reg_pwrctl_status_after\` \`0\`, \`_mode_pwrctl_status\` \`0\`, \`_reg_pwrctl_ctl\` \`0\`), so a faithful port of the vendor's power path would \`wait_for_completion\` FOREVER - so read \`_pwr_status_after\` (\`CORE_PWRCTL_STATUS 0xDC\`, 32-bit) as the reading that handler would have taken and \`_pwr_done\` as the arm's own completion, and read \`SDHCI_QUIRK_SINGLE_POWER_WRITE\` being set (\`sdhci-msm.c:2897\`) as why the driver's zero/bus-off write is NOT on this path: its absence is not a missing step. (3) **THE SEAM MOVED, AND NOTHING IN THE ARITHMETIC WOULD HAVE PREDICTED IT**: this arm's entry group crossed a page boundary, so the seam went \`0x800472dc\` -> **\`0x800482dc\`** and every kernel-text address moved with it (+0x1000 for 15,979 of the image's 26,533 symbols) - and \`entry_trace.c\`'s constant and \`run_and_capture.sh\`'s literal were re-pinned TOGETHER, with the build refusing to link until they agreed with the image. The remaining cells: \`_pwr_cc_before\` expected **\`0xE045\`** - THE CELL THAT STATES THE ROUTE, because the driver's own pass A sees \`0x0000\` there while this arm runs after 706's clock set, whose card-enable readback is that word; \`_pwr_cc_after\` unchanged at \`0xE045\` is the expected reading and anything else is a coupling this ladder has not seen; \`_pwr_mask\` expected \`0x0000000f\` (four power events armed, so a status bit is evidence); \`_pwr_ctl\` expected \`0\`; and \`_pwr_ps_before\`/\`_pwr_ps_after\` expected \`0x01f80000\`, whose CARD_PRESENT bit is clear - the controller's own statement that no card is in the slot, made before and after the store. **The arm publishes no rate, no command, no sector and no mount**; what this press buys is whether the byte takes, what the power-control words answer, and which of the two routes \`_pwr_cc_before\` states"
    fi
    if [[ $wst == 8 ]]; then
      entry_conseq+=" **AND EVERY SENTENCE ABOVE HOLDS FOR THIS ARM UNCHANGED, BECAUSE ITS ADDITION IS A CLIENT AND NOT A NEW KIND OF ACT - WITH FIVE THINGS THAT ARE NEW AND ARE THE ONES TO READ.** (1) **THE LINE NOW HAS AN OWNER, AND THAT IS THE ARM'S WHOLE PURPOSE.** 709 ended on \`_irq_other_count=0x00000001\` / \`_irq_other_iar=0x000000aa\`; on THIS arm the same event must reach \`st_pwr_irq\` instead, so read \`_pwr_irq_calls\` (>=1 - the client was called at all), \`_pwr_irq_intid\` (=170), \`_pwr_irq_reg_rc\` (=0, the registration was accepted) and \`_pwr_irq_arm_rc\` beside \`_irq_other_count\`, whose expected value here is **not 1** - and a log that carries BOTH the client's keys and \`_irq_other_*\` is the more interesting reading, because it says the dispatcher answered this line and something else was still unowned. (2) **\`_pwr_irq_calls_once_before_byte = 0\` IS THE CELL THAT MAKES THE BYTE THE CAUSE**, and it is the only cell that can: the handler records \`g_pwr_irq_calls\` at the moment the byte is written, so a nonzero value there means the line was already asserted before this arm's store - 709's latch, still standing - and a zero means this store raised it. (3) **THE ACKNOWLEDGE IS A STORE, SO ITS OWN READBACK IS THE CELL.** \`_pwr_irq_status\` = \`0x02\` beside \`_pwr_irq_status32\` = \`0x00000002\` (the SAME register read at two widths from one moment - the two-width pair the vendor's own \`readb_relaxed\`/\`readl_relaxed\` pair makes), \`_pwr_irq_ack\` = \`0x01\`, and **\`_pwr_irq_status_after\` = \`0x00\`** is the rung's own falsifier read the other way: a \`0x02\` there is a latch that did NOT let go, so the level line re-asserts, the client is called to \`STAGE90_IRQ_CLIENT_CAP\` 64, and the log carries \`_irq_cli_storm\` instead of the 690 ending. (4) **THE TWO VENDOR-SPEC ADDRESSES ARE ALSO A BUILD FACT AND NOT ONLY A CELL.** \`_pwr_irq_vendor_core\` / \`_pwr_irq_vendor_hc\` are \`core_mem + 0x10C\` and \`hc_mem + 0x10C\`; the probe's own store census on THIS arm is \`hc_mem [47 44 44 41 ]\` and **not** \`[47 44 44 41 268]\` - experiment-710 section 2 predicted the second and the build falsified it, because the handler is its own symbol (\`st_pwr_irq\`, 0x1cc bytes at \`0x8000cff8\`, which pushed \`entry_storage_probe\` to \`0x8000d1c4\`) and its \`0x10C\` store is outside \`entry_storage_probe\`'s window. (5) **THE THREE ARMS THAT MAY SLEEP ARE ABSENT BY CONSTRUCTION, AND THAT IS A HAZARD STATEMENT RATHER THAN A CELL.** The client runs in \`fleh_irq_kernel\`'s frame: a handler that called a regulator or pinctrl routine would sleep in an exception, which is why the vendor asked for a threaded handler and why this image refuses such a build outright. **AND THE HAZARD THIS PRESS CARRIES IS THE ONE 709 MEASURED, FROM THE OTHER SIDE**: the line stays asserted until the latch is cleared, so a client that READ the status and did not clear it is a storm, and the only bound on it is \`STAGE90_IRQ_CLIENT_CAP\` - read \`_irq_cli_storm\` in the log beside \`_pwr_irq_calls\`, never \`_pwr_irq_calls\` alone"
    fi
    if [[ $wst == 9 ]]; then
      entry_conseq+=" **AND EVERY SENTENCE ABOVE HOLDS FOR THIS ARM UNCHANGED, BECAUSE ITS ADDITION IS A WAIT AND NOT A NEW KIND OF ACT - WITH FIVE THINGS THAT ARE NEW AND ARE THE ONES TO READ.** (1) **THE RUNG'S OWN ANSWER IS THE PAIR \`_wait_timeout\` AND \`_wait_ctl_after\`, AND BOTH VALUES ARE INFORMATIVE.** \`_wait_ctl_before\` is the controller's \`CORE_PWRCTL_CTL\` byte before the wait (expected \`0x00\`, the reading rung 7's press took) and \`_wait_ctl_after\` is the same byte after it: **\`0x01\` is the controller acking the BUS_ON request (\`BUS_SUCCESS\`, \`sdhci-msm.c:72\`) and \`0x00\` is the ack NOT arriving.** \`_wait_timeout\` = 0 beside \`_wait_ctl_after\` = 0x01 is the handshake completing inside the budget; \`_wait_timeout\` = 1 is the poll spending its whole bound and going on; and \`_wait_ctl_after\` = 0x00 with \`_wait_timeout\` = 0 is impossible by construction - which is why the pair and not either cell alone. \`_wait_polls\` and \`_wait_ticks\` are the wait's own cost (\`_wait_ticks\` is in ticks of the 19,200,000 Hz counter, so this arm's bound $wpt is $(( wpt / 19200 )) ms and a small number is the ack arriving fast), and **\`_wait_bound\` = $wpt is the switch itself, and it must read the value this record carries and not a constant**: the build asserts the budget's \`mov\`/\`movw\` + \`movt\` pair is present in \`st_pwr_wait\`'s OWN body (both spellings - on 716's arm the pair is \`mov r9, #0xDC00\` + \`movt r9, #5\`, a rotated immediate and NOT a \`movw\`; the clause accepts both and the BUILD's success echo still says \`movw\`/\`movt\`, recorded as m724), because a switch whose value never reaches the image is \`mi4-off-option-two-spellings\` with a shorter fuse and would leave this record claiming a bound the code does not carry. (2) **THE TWO-MEMORY CELL IS THE DEVICE PAIR AGAINST THE IMAGE PAIR, AND IT IS THE CELL THIS RUNG EXISTS FOR.** Read \`_wait_ctl_after\` (the controller's own byte) BESIDE \`_wait_done\` (the image-side flag \`g_pwr_irq_done\`) and \`_wait_calls\` (whether the handler ran at all). Device ack with the flag SET is the handshake complete on both sides; **device ack with the flag CLEAR is a driver-side completion that did not cross the cache boundary** - the shape 686's correction of 652 describes, one context over, and the reason the end condition is the device register; \`_wait_ctl_after\` = 0x00 with \`_wait_calls\` >= 1 is a handler that RAN and did not ack, which is rung 8's decode failing rather than its line; and \`_wait_calls\` = 0 with a nonzero \`_irq_other_count\` means the event went to nobody, which is 709's press exactly. \`_wait_state_before\` / \`_wait_io_before\` and their \`_after\` twins are the vendor's own predicate fields, so \`_wait_done_before\` = 1 with \`_wait_reset\` = 1 is the vendor's already-satisfied branch rather than a broken poll. (3) **\`CLOCK_CONTROL\` READ TWICE IS 711 SECTION 3's OPEN QUESTION, ANSWERED BY CONSTRUCTION.** \`_wait_cc_before\` and \`_wait_cc_after\` are \`hc_mem + CLOCK_CONTROL 0x2C\` read as a **halfword** either side of the wait (the vendor's own \`readw\`, \`sdhci.h:100\`), so the two boots that disagreed about bit 1 are replaced by one run that reads the same register on both sides of a hundred milliseconds: **equal halves say the bit is the machine's state, a half that changes across the wait says it is a race with the driver's own clock block** - and either reading is a fact about the MACHINE, which is what 711 could not get from two runs. (4) **\`_wait_reset\` NAMES THE BRANCH, AND IT IS NOT A FORMALITY.** 0 means the poll ran; 1 means the vendor's own already-satisfied arm was taken, the completion was RESET (\`:2202-2203\`) and NO wait happened - so on that branch \`_wait_polls\` = 0 and \`_wait_ticks\` = 0 are CORRECT and not a dead loop, and \`_wait_cpsr\` is published from both paths so the log always names which one ran. (5) **THE ARM MAKES NO DEVICE STORE AT ALL, AND THAT IS A BUILD FACT AND NOT A PROMISE.** \`st_pwr_wait\`'s device accesses are asserted by the build as EXACTLY \`[f98240e8:ldrb f982492c:ldrh]\` with minimum counts \`[f982492c:ldrh >= 2, f98240e8:ldrb >= 2]\` (two \`CLOCK_CONTROL\` halfword readings either side, and the poll's \`CTL\` byte; the counts are a MINIMUM because the compiler may duplicate a read across the vendor's own two decode branches), its non-device accesses as exactly \`ldr\`/\`str\` over the four words \`g_pwr_irq_calls\`, \`g_pwr_curr_state\`, \`g_pwr_curr_io\`, \`g_pwr_irq_done\` - resolved to those four \`.bss\` symbols in the LINKED image, so \"the waiter and the handler are two halves of ONE handshake\" is a property of the artifact and not of two source files agreeing - and the probe's single \`bl\` to it. **A \`str\`/\`strb\` anywhere in that window therefore fails the set equality rather than a separate assertion**, because the ack the poll waits for is the HANDLER's store and a waiter that wrote \`CTL\` would be answering its own completion. **The hazard this press carries is the wait's own cost**: the poll is bounded, so the run always continues - but a log whose last key is \`_wait_bound\` or \`_wait_ticks\` is a run whose ending clock was spent on the wait, and the store set is still \`_rst_stores=1\` plus the four inherited \`_writes=4\` plus the power byte: **no command, no sector, no partition table, no mount**, and every \`_wait_*\` key in this log is a register and never the medium"
    fi

  elif [[ $wse == 1 ]]; then
    entry_arm="$entry_arm - **and this is NOT that arm: it is 678's ENDING arm** (STAGE90_XNU_SEAM_END_RUN=1), which runs that same operation, publishes the pair, and then **ends the run on purpose through PS_HOLD**"
    entry_conseq+=" **BUT THE POP NEVER RUNS ON THIS ARM**, and that is the one thing 535 section 4's cells did not anticipate: the seam publishes the pair and then resets the machine, so the FACTS above hold and their CONSEQUENCES - every clause about what the pop then read - do not. \`STALE LINE, WRITTEN OUT\` therefore reads *the operation put the loop's own datum where the frame's word belongs, the pair was published, and the run then ended where this project told it to*: the reading survives a machine that would have died at the pop, which is what this arm exists for (663 section 2). And this arm has ONE cell no earlier arm can produce - \`seam_calls >= 1\` with **no pair at all** - which is *the operation did not return between the two reads* and NOT \"a dirty line, holding something this block does not name\" (680). \`run_and_capture.sh\` selects these readings off the log's own \`xnu_live_seam_end_run\` (:2025), so this press's log is read with THIS arm's rules and not 653's. **A hang at the pop is not a possible failure of this press**: the run ends before it, deliberately, on the one net every returning run has proven."
  elif [[ $wpse == 1 ]]; then
    entry_arm="$entry_arm - **and this is NOT that arm either: it is 686's POST-END arm** (STAGE90_XNU_POST_END_RUN=1), which runs the same operation, publishes the same pair, **lets the exit's own \`push {fp, lr}\` / \`pop {fp, pc}\` run**, and then ends the run on the far side of it"
    entry_conseq+=" **THE POP DOES RUN ON THIS ARM, AND WHETHER IT SURVIVED IS THE WHOLE ANSWER THIS PRESS BUYS.** The key is \`xnu_live_slot_post_calls\`, and nothing else separates the two cells: PRESENT means \`platform_cache_idle_exit\` RETURNED - the operation's PoC clean-and-invalidate plus the two restore stores carried the frame - and the run then ended in the wrapper, i.e. **the frontier is not the pop on this arm** and 662 section 4's first explanation is measured rather than assumed; ABSENT means the pop died exactly as it does on the measure arm, with the abort's four words carrying the deadline, which is 574-park repeated with the operation on. Both cells come back with a log, so a non-return is not one of this press's outcomes. **The pair reads the same as 653's** - literally the same DRAM reading, so the same non-verdict about the line (above), and on this arm the pair is not the press's answer at all: \`slot_post_calls\` is. **The operand, measured in this image and not inferred:** \`__wrap_platform_cache_idle_enter\` opens \`strd r4, [sp, #-12]!\` with \`r4 = cpu_data->rtcPop\` (cpu_idle loads it), and at that \`sp\` the address is exactly the word the pop reads as \`pc\`; the stale copy that survives Apple's L1-only sweep is one level below, at the PoU. 685 §4's \"a writer between the seam and the pop\" is wrong and experiment 686 is where it is corrected. **687 PRESSED THIS ARM AND THE POP RETURNED** (\`xnu_live_slot_post_calls=0x00000001\`), so the switch is now a PASS COUNT: a value of 2 or more is the same site read one question further along - the ending fires on that many exit returns, and \`xnu_live_slot_post_calls\` becomes a progress counter that reads the arm's own number on a boot that stayed alive for that many idle passes and less on one that died earlier."
  elif [[ $wpse =~ ^[0-9]+$ ]] && (( wpse >= 2 )); then
    entry_arm="$entry_arm - **and this is NOT that arm either: it is 687's PASS-COUNT arm** (STAGE90_XNU_POST_END_RUN=$wpse), which runs the same operation, publishes the same pair, lets the exit's \`push {fp, lr}\` / \`pop {fp, pc}\` run, **and ends the run only after the exit wrapper has returned that many times**"
    entry_conseq+=" **THE FRONTIER IS ALREADY PAST THE POP ON THIS ARM (687 pressed 686's arm and it returned), SO THE ANSWER THIS PRESS BUYS IS HOW FAR PAST IT THE BOOT GETS.** \`xnu_live_slot_post_calls\` is a progress counter and \`$wpse\` is the number the ending is armed for: it reads **$wpse** on a boot that completed $wpse whole idle passes - pop returns, \`ClearIdlePop\` and \`cpu_idle_exit\` run, \`cpu_idle\` is re-entered, its early tests pass, the enter wrapper opens the window, the \`wfi\` halts, the exit returns again - and **less than $wpse** on one that died before the ending, which is the negative cell and comes back with a log of its own. A run that reads the full count has measured that **the OS stays alive past the exit and runs its idle loop**, which is the first thing this phase can say about the boot after the frontier. The pair is the same DRAM reading as 653's and is not this press's answer. **The site did not move, so nothing else in the image changed**: no new mapping, no new call site, and the same \`entry_seam_end_run\`."
  elif [[ $wpet =~ ^[0-9]+$ ]] && (( wpet > 0 )); then
    entry_arm="$entry_arm - **and this is NOT that arm either: it is 690's CLOCK arm** (STAGE90_XNU_POST_END_TICKS=$wpet ticks - $(( wpet / 19200 )) ms at the payload's own 19200 ticks/ms - with SEAM_END_RUN and POST_END_RUN both named and off), which runs that same operation, publishes the same pair, lets the exit's \`push {fp, lr}\` / \`pop {fp, pc}\` run, and then ends the run ON A DEADLINE: \`__wrap_platform_cache_idle_exit\` takes \`entry_counter()\` as a baseline on its FIRST return and tail-branches into \`entry_post_clock\` on every return, which calls \`entry_seam_end_run\` on the return whose \`now - t0\` has reached $wpet"
    entry_conseq+=" **THE FRONTIER IS PAST THE POP ON THIS ARM, AND THE COUNT IS NO LONGER THE ANSWER (689 pressed 687's arm and the pop returned, twice).** Ending on a clock is what makes the ending independent of how many passes happen - the first arm here that counts nothing - so \`xnu_live_slot_post_calls\` becomes a progress counter and the press's own readings are \`xnu_live_post_t0\` (the counter at the wrapper's first return: the baseline), \`xnu_live_post_cntfrq\` (the HARDWARE's own rate, read once beside the baseline - the arm states the rate instead of assuming the payload's device-tree 19200 ticks/ms, which nothing in this project has measured), \`xnu_live_post_elapsed\` (the elapsed ticks, published on the powers of two of the pass count and again on the pass that ends the run) and \`xnu_live_post_end_calls\`. **That last key is the cell: PRESENT means the ending FIRED** - the boot stayed up for the whole deadline and was then ended on purpose, on that many idle passes - **and ABSENT means the machine stopped before the clock ran out**, which is this arm's negative cell and comes back with a log of its own only if the stop was a RETURN; a stop that is a hang is exit 2, the run does not come back, and it owes a power press. **Two further readings are pre-registered on this arm and both are checkable in the same log**: the seam DOES publish its identity key on an image this late - \`xnu_live_seam_end_run=0x00000000\` beside \`xnu_live_seam_post_end_ticks=$wpet\`, because the string is in the image (measured: 653's park does not carry it, this one does, so absence would be a lost publication and not an image that predates the switch) - and the abort \`entry_seam_end_run\` raises should read \`xnu_live_sleh_lr=0x8047b5bc\`, the instruction after the \`bl\` inside \`entry_post_clock\`, where the 688 arm read 0x8047ca0c after its counted \`bl\` in the wrapper. \`xnu_live_sleh_pc\` is unchanged at 0x8047b488"
  elif [[ $seam == poc ]]; then
    if [[ -n $wse || -n $wpse || -n $wpet ]]; then
      entry_conseq+=" **This record NAMES its ending switches and they are OFF** (STAGE90_XNU_SEAM_END_RUN=${wse:-absent}, STAGE90_XNU_POST_END_RUN=${wpse:-absent}, STAGE90_XNU_POST_END_TICKS=${wpet:-absent}), so this image's seam RETURNS and the cells above are read against the pop's death - but absent-from-the-log is NOT what that means here: an image this late carries the key, so \`xnu_live_seam_end_run=0x00000000\` in the log is a reading and a MISSING one would be a lost publication. The branch below is the other fact - a record that does not name the key at all - and the two are told apart by this line and not by the log"
    else
      entry_conseq+=" **And the entry record does not name STAGE90_XNU_SEAM_END_RUN at all**, so this image predates 678 and its seam RETURNS: the cells above are read against the pop's death, and the log will carry no xnu_live_seam_end_run key - the string is not in the image (measured: 653's park does not carry it, 690's does). Absent here is a fact about which image this is, never a zero."
    fi
  fi
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
    if [[ $wst == 10 ]]; then
      entry_conseq+=" **AND EVERY SENTENCE ABOVE HOLDS FOR THIS ARM UNCHANGED, WITH FIVE THINGS THAT ARE NEW AND ARE THE ONES TO READ.** (1) **TWO CELLS DECIDE WHETHER THE ARM IS A READING AT ALL, AND NEITHER OF THEM IS THE ANSWER.** \`_wait_cpsr\` must read **\`0x80000013\`** - SVC, \`I\` clear, \`F\` clear, \`N\` set, i.e. \`0x80000093 & ~0x80\`, the value rungs 9 and 10 read as \`0x80000093\`: a reading of \`0x80000093\` here means the \`cpsie\` did not take (or the compiler moved the \`mrs\` above it) and the run is **VOID AS A COMPARISON rather than negative**, with the disassembly of \`st_pwr_wait\` the first thing to re-read; and **\`_wait_cpsr_after\`** must read **\`0x80000093\`**, which is the state the probe GIVES BACK - the same value rungs 9 and 10 read as their premise, so the pair says the mask was restored from the SAVED register and not from a constant that happens to look tidy. (2) **THE OUTCOME IS THE PAIR \`_wait_timeout\` AND \`_wait_ctl_after\`, AND BOTH DIRECTIONS ARE INFORMATIVE.** \`_wait_timeout\` = \`0\` with \`_wait_ctl_after\` = \`0x01\` is the handshake COMPLETING inside the poll - the first time on this ladder - and \`_wait_timeout\` = \`1\` is the poll spending its whole bound with the mask OFF, which is the more interesting negative: the mask was off, the bound was $wpt ticks ($(( wpt / 19200 )) ms), and the handler still did not run. \`_wait_polls\` and \`_wait_ticks\` size the delivery (small numbers are the completion arriving at once, against 86,016 polls and 386,131 ticks on 716's arm, whose whole bound was spent on its own mask), and **the new reading is \`_pwr_irq_at\` against \`_wait_t0\`**: on rungs 9 and 10 that difference WAS the spin (1,922,347 and 387,298 ticks); here it must collapse to the delivery's own cost - the interval from the mask coming off to the exception being taken. (3) **\`_wait_done\` = 1 BESIDE \`_wait_calls\` = 1 IS THE VENDOR'S PREDICATE SATISFIED, AND IT IS ALSO THE CACHE-BOUNDARY COMPARISON THIS LADDER HAS NEVER HAD.** On rungs 9 and 10 the waiter read \`g_pwr_irq_done\`, \`g_pwr_curr_state\` and \`g_pwr_curr_io\` BEFORE the handler had run, so the flag-against-ack pair had no comparison; here the handler has already written all three when the waiter reads them. **Device ack (\`_wait_ctl_after\` = 0x01) with the flag SET is the handshake complete on both sides; device ack with \`_wait_done\` = 0 is 686's cache-boundary defect measured instead of argued** - and inside this window the expected answer is agreement through DRAM, because \`xnu_live_seam_sctlr\` reads \`C\` clear and the seam is why the end condition is the device register at all. (4) **A NEGATIVE THAT IS NOT ABOUT THE MASK: \`_wait_cpsr\` = 0x80000013 WITH \`_wait_timeout\` = 1.** That says something BELOW the CPSR gates this line in this context - the CPU interface's priority mask (\`GICC_PMR\`), the line's priority, its group or its target - and the next rung is then those four readings taken inside the unmasked window. It is the more valuable reading and the unlikely one: the handler has been taken at this site three times, so the line is enabled, in Group 0, at a priority the mask admits and targeted at this CPU. (5) **THE HAZARD IS THE CONTEXT AND NOT THE BOUND, AND IT IS THIS ARM'S OWN NEGATIVE CELL.** The client now runs in \`fleh_irq_kernel\`'s frame with \`SCTLR.C\` clear INSIDE the payload's own idle-exit code - a state no earlier arm entered, and the state the payload masks \`I\` to avoid. The poll is still bounded, the gate is still read before any store, the watchdog is still armed and nothing is flashed, but a non-return (exit 2), an \`_irq_other_*\` key or an ending on \`exception: irq line\` is a READING HERE and names the line or the fault rather than refuting the cells above. **The write set is UNCHANGED, and that is a build fact and not a promise**: this arm's own build printed \`st_pwr_wait\`'s device accesses as EXACTLY \`[f982492c:ldrh f98240e8:ldrb]\` (counts \`ldrh=2\`, \`ldrb=4\`) and its image accesses as exactly \`ldr\`/\`str\` over the four words \`g_pwr_irq_calls\`, \`g_pwr_curr_state\`, \`g_pwr_curr_io\`, \`g_pwr_irq_done\` - because a published cell is a CALL to \`entry_live_write\` with a string and is not an access that window's census can see, which is why the new \`_wait_cpsr_after\` changes neither set - with the probe's single \`bl\` to it and the budget pair in its own body. **So: no command, no sector, no partition table, no mount** - the arm's whole device act is still two halfword reads of \`CLOCK_CONTROL\` and a poll of one \`CORE_PWRCTL_CTL\` byte"
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
  # --- the arm's NAME is a reading too, and these are its two checks ------------------------------
  # **A NAME IN PROSE IS A READER, AND 698's RENAME CENSUS COUNTED THREE OF THEM.** 698 renamed the
  # offset-0 word `ST_SDHCI_HCI_VERSION 0x00` to `ST_SDHCI_DMA_ADDRESS` and enumerated its readers by
  # grepping the full `xnu_live_storage_hci_version` / `xnu_live_storage_mode_hci_version` names: three.
  # **This row's own narration was a fourth**, because prose writes the abbreviated form - the paragraph
  # above said `_mode_hci_version` - so no grep for the full name reaches it. Measured: the 698 press's
  # readiness told its reader to look for a key that run could not print (the arm publishes
  # `xnu_live_storage_mode_dma_address`). That is [[mi4-one-value-two-definitions]]'s m699 one reader
  # further out, and the reason these two checks exist rather than a note to be careful:
  #
  # (1) **The narration must quote the rung the record carries.** A branch that enumerates the values it
  #     knows mis-names the next one in silence - measured on the 698 press's own readiness, where a
  #     rung-3 arm was narrated as 690's CLOCK arm because the branch read `wst == 1 || wst == 2`
  #     (`/tmp/r654/press.log:689`), the census it was pressed for never mentioned, and this row still
  #     printed `ok`. A rung this file has no paragraph for therefore refuses here instead of being
  #     answered by another arm's sentence.
  # (2) **Every `_name` the narration writes is checked against the keys THIS image publishes** - the
  #     `xnu_live_` strings in `$ARM_ELF` - and a name no key ends in refuses, with the names printed.
  #     **The check's scope is a suffix and not an exact key**: the narration abbreviates (`_loads` for
  #     `xnu_live_storage_loads`), and a glob (`_gcc_*`, `_mode_*`) is a SCOPE claim rather than a name,
  #     so it is listed and not checked (m693). What it catches is a name the image cannot print, which
  #     is what a rename makes of prose; it cannot catch a name that is the suffix of the wrong key, and
  #     nothing here claims it does.
  nar_bad=''; nar_tok=''; nar_glob=''; nar_miss=''; nar_keys=''; nar_note=''; _n_named=0; _n_glob=0
  if [[ $vline == 'the window is reachable EXACTLY ONCE in this image.'* && -z $sub_arm ]]; then
    if [[ -n $wst && $wst != 0 && $arm != *"STAGE90_XNU_STORAGE_PROBE=$wst"* ]]; then
      nar_bad="the arm named above does not quote the rung the entry record carries: STAGE90_XNU_STORAGE_PROBE=$wst is in $ARM_CFG, and every rung this file narrates is narrated by its own value. Either this is a rung with no paragraph here yet - \`STAGE90_XNU_STORAGE_PROBE\` is a ladder (0 inert, then the read-only probe, the vendor's mode sequence, the register-file census, the driver's reset, the clock surface, the first clock set and the card-power byte, each later one carrying the earlier ones), and a new rung needs its own paragraph - or the narration above names a different rung, which is one quantity with two readings. That this row said nothing about it before is the 698 defect: a narration can be WRONG and still non-empty, which is all this row used to ask"
    else
      nar_keys=$(arm-none-eabi-strings "$ARM_ELF" | awk '/^xnu_live_/ { print }')
      nar_tok=$(printf '%s' "$arm$entry_conseq" | grep -oE '`_[a-z][a-z0-9_]*' | sed 's/^`//' | LC_ALL=C sort -u)
      nar_glob=$(printf '%s' "$arm$entry_conseq" | grep -oE '`_[a-z][a-z0-9_]*\*' | sed 's/^`//; s/\*$//' | LC_ALL=C sort -u)
      # **The test is one `awk` process, and the pipe it used to be in is 706's finding.** The loop
      # read `printf '%s\n' "$nar_keys" | grep -q -- "$_t\$" || printf '%s ' "$_t"`, and `set -o
      # pipefail` (line 126) makes that pipeline's status the producer's when the producer fails: `grep
      # -q` exits at the first match, and if that happens before the producer's write is through, the
      # write gets EPIPE, `printf` dies of SIGPIPE with 141, pipefail hands 141 to the `||`, and a name
      # whose key IS published is reported MISSING. Measured on 2026-09-25 against the rung-6 arm:
      # three runs of the same six lines on the same inputs returned `[]`, `[_cbcr _clk_set_cc_card
      # _clk_set_writes _gate ]` and `[_mode_dma_address ]`, every one of those names a suffix of a
      # string this image publishes - and the row above them printed a FAIL that refused the press. The
      # race is why the row passed at 705 and refused at 706 with the same image: it is a verdict that
      # depends on WHEN the reader stopped, which is the defect class this file exists to catch, one
      # level up. A test whose two inputs are in memory needs no pipe: `awk` compares the suffix itself
      # and its answer is a function of its arguments alone.
      nar_miss=$(awk -v globs="$nar_glob" -v keys="$nar_keys" '
                   BEGIN {
                     ng = split(globs, G, "\n")
                     for (i = 1; i <= ng; i++) if (G[i] != "") isglob[G[i]] = 1
                     nk = split(keys, K, "\n")
                   }
                   {
                     t = $0
                     if (t == "" || (t in isglob)) next
                     lt = length(t); ok = 0
                     for (i = 1; i <= nk; i++) {
                       k = K[i]
                       if (k != "" && length(k) >= lt && substr(k, length(k) - lt + 1) == t) { ok = 1; break }
                     }
                     if (!ok) printf "%s ", t
                   }' <<<"$nar_tok")
      _n_named=$(printf '%s\n' "$nar_tok" | grep -c '[^[:space:]]')
      _n_glob=$(printf '%s\n' "$nar_glob" | grep -c '[^[:space:]]')
      [[ -z ${nar_miss// /} ]] \
        || nar_bad="the arm named above refers to [${nar_miss% }], and $ARM_ELF publishes no xnu_live_ string ending in that name/those names - so a reader following this narration would go looking for a key this image cannot print. The usual cause is a RENAME whose reader census stopped at the full key name: 698 renamed the offset-0 word to \`_dma_address\` and counted three readers, while this narration writes the abbreviated form. Check the name against the image and not against the source that named it. (The narration's own globs - ${nar_glob:-<none>} - are scope claims and are listed, not checked.)"
    fi
    # 716 and 715 section 5: AND THE SENTENCE MUST QUOTE THE ARM'S OTHER NAMING KEY, which at rung 9 is
    # the bound. Two arms share `STAGE90_XNU_STORAGE_PROBE=9` (714's 100 ms, 716's 20 ms), so a narration
    # that quotes the RUNG alone names both with one sentence - and that is not hypothetical: this row
    # printed `ok` on 716's bytes while the paragraph said `= 1920000` and `100 ms`, two hard-coded
    # values the record does not carry. The check is a SECOND branch and not an `elif` on the rung check
    # above, deliberately: the two failures are about two different keys, and an `elif` would report the
    # rung's sentence for the bound's defect (`mi4-status-is-a-verdict-only-if-its-producer-delivered-one`,
    # one row down).
    if [[ -z $nar_bad && -n $wpt && $wpt =~ ^[0-9]+$ && $arm != *"STAGE90_XNU_PWR_WAIT_TICKS=$wpt"* ]]; then
      nar_bad="the arm named above does not quote the WAIT'S BOUND the entry record carries: $ARM_CFG says STAGE90_XNU_PWR_WAIT_TICKS=$wpt and the sentence above writes no STAGE90_XNU_PWR_WAIT_TICKS=$wpt. This is 715 section 5, and it is the rung check one branch up applied to the second key that names an arm: STAGE90_XNU_STORAGE_PROBE names the RUNG and this key names WHICH ARM AT THAT RUNG, so a narration quoting the rung alone names two different arms with one sentence. It was measured here for real - 714's arm and 716's arm both carry STORAGE_PROBE=9, and this row printed \`ok\` on 716's bytes while the paragraph printed the literal 1920000 and \`100 ms\` for both"
    fi
    [[ -n $nar_bad ]] || nar_note=" **The narration's own names are a reading too**: the $_n_named name(s) and $_n_glob glob(s) it writes were read against $ARM_ELF's own key strings, and every name is a suffix of an \`xnu_live_\` string this image publishes."
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
  elif [[ -n $nar_bad ]]; then
    bad 'the arm is named by a reading' "$nar_bad"
  elif [[ $swe != "$want" ]]; then
    bad 'the arm is named by a reading' "the ENTRY reading says $entry_arm, which is STAGE90_XNU_IDLE_NO_SLEEP=$want, and the entry record says STAGE90_XNU_IDLE_NO_SLEEP=$swe - one quantity with two readings, and they disagree"
  elif [[ -n $sub_arm ]]; then
    ok 'the arm is named by a reading' "$arm, and the entry it carries is $entry_arm ('$vline', and the entry record's STAGE90_XNU_IDLE_NO_SLEEP=$swe agrees). $conseq"
  else
    # **THE TWO READERS OF ONE RENAME, AND UNTIL 679 THE SECOND ONE READ A NAME THAT DID NOT EXIST.**
    # 667 split one variable into two - the entry-level consequence became `entry_conseq` and a new
    # `conseq` took over the sub-arm (self-test) sentence - and it updated the reader above and **not
    # this one**, which was left saying `$conseq`. On a self-test arm `conseq` is assigned, so the row
    # above was correct and this row was never reached; on any arm with **neither** self-test switch on,
    # `sub_arm` is empty and this branch runs, and `set -u` aborted the whole run here.
    #
    # Measured 2026-09-25 on `armed-seam-endrun-88972ba9` (678's arm):
    #
    #     tools/verify_press_ready.sh: line 640: conseq: unbound variable   ->  exit 1, NO VERDICT AT ALL
    #
    # The row that dies is row 4 and the row that is therefore never reached is row 5 - *a press now
    # would actually be caught*, the one row that decides whether the press is worth spending. So the
    # failure is not a missing sentence: **readiness prints nothing and a launcher waiting on it waits
    # forever.** And the defect was invisible in the step that introduced it, because the only arm in
    # `out/` when 667 measured its own five rows was **666's**, a self-test arm - one arm, and its own
    # switch selects exactly the branch that hides the break. `stage90-build-config.txt` is byte-identical
    # (`6c2b6038...`) across the 653 seam arm, the sleeper and this one, so **every non-self-test arm
    # since 666 has aborted here**, not just this one.
    #
    # The repair is the reader 646 wrote: an arm that reaches the entry reads its consequence from
    # `entry_conseq`. The orphan ran the other way too - 667 renamed the four assignment sites and left
    # `entry_conseq` read by nothing - so this line is also what makes that prose reachable at all.
    conseq=$entry_conseq
    ok 'the arm is named by a reading' "$arm: '$vline' and the entry record's STAGE90_XNU_IDLE_NO_SLEEP=$swe agrees, so $conseq$nar_note"
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
    # `<<<` and not a pipe, for 706's reason one row down (see the narration census above): a `grep -q`
    # that exits at the first match can kill its producer with SIGPIPE, and under `set -o pipefail` that
    # 141 is the `elif`s answer - so the row would report that neither list names the serial on a list
    # that does. A here-string has no producer process to lose.
    elif grep -q "^$SERIAL" <<<"$fb"; then
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
