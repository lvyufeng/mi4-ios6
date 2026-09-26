#!/usr/bin/env bash
#
# Which `--allow-*` flags does the gate need for the arm in this directory?
#
# WHY THIS EXISTS
# ---------------
# The gate refuses a hazardous state without its own flag - `--allow-xnu-entry`, `--allow-hw-watchdog-selftest`,
# `--allow-selftest`, `--allow-fault-inject`, `--allow-preflight`, `--allow-full`,
# `--allow-attr-normal-nc`, `--allow-attr-normal-wb`, `--allow-icache`, `--allow-dcache` - and refuses one of
# them the other way round (a ladder image may not be gated as an entry image). So **the set a run must be
# given is a function of the arm's own switches**, and on this host that fact has now broken three instruments
# in one day, each time the same way: `tools/verify_press_ready.sh` invoked the gate with
# `--allow-xnu-entry` alone and reported the gate's flag refusal as a verdict about the tree (667);
# `tools/rehearse_live_path.sh` did the same, so **all twenty of its live-path cells went red** on the arm
# 666 parked, and had been red since that arm was built; and the armed press launcher carries a
# `--allow-xnu-entry`-only line, so a press fired by it today is spent on a gate refusal. Three callers, one
# missing definition, and the third of them costs a press.
#
# So the derivation lives here, once, and the callers ask. The table below is still a **copy** of the gate's
# conditions - a copy can drift - and two checks bound the cost:
#
#   * every flag the gate's own usage line and refusals name must be a flag this table can produce, so an
#     eleventh flag turns into a refusal that NAMES it rather than a silently under-supplied set;
#   * every switch this file reads must be named exactly once WITH a value in the record, so a parse that
#     stops seeing a value refuses instead of quietly deriving a smaller set.
#
# The residual risk, stated rather than papered over: the gate could give an existing `--allow-X` a new
# meaning while keeping its name, and nothing here would see it. Under-supply is always loud (the gate
# refuses at the call); over-supply requires the arm's own record to name the hazard, which is the same act
# that built the arm. The falsification seam is on the caller's side (`verify_press_ready.sh --gate-flags`).
#
# Usage:
#   tools/gate_flags_for_arm.sh DIR                  # DIR is the arm's directory (default: out/stage90)
#   tools/gate_flags_for_arm.sh DIR --gate PATH       # a different gate (for its flag vocabulary)
#   tools/gate_flags_for_arm.sh DIR --cfg NAME        # a different switch record under DIR
#   tools/gate_flags_for_arm.sh --help
#
# Output: the flags, **one per line**, on stdout, and nothing else - so a caller may
# `while read -r f; do args+=("$f"); done` and be sure of where the boundaries are. Zero lines is a
# legitimate answer and means this arm's switches demand none of them; it is not an error.
#
# Exit: 0 = derived (possibly zero flags). 1 = refused, with the reason on stderr. 2 = usage.

set -uo pipefail

SELF=$(readlink -f "${BASH_SOURCE[0]}")
REPO_ROOT=$(cd "$(dirname "$SELF")/.." && pwd)

DIR=$REPO_ROOT/out/stage90
CFG_NAME=stage90-build-config.txt
GATE=$REPO_ROOT/scripts/preflight_boot_check.sh

while (($# > 0)); do
  case $1 in
    --gate) GATE=${2:-}; shift 2 ;;
    --cfg)  CFG_NAME=${2:-}; shift 2 ;;
    -h|--help)
      awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$SELF"
      exit 0 ;;
    --*) printf 'gate_flags_for_arm: unknown option %s\n' "$1" >&2; exit 2 ;;
    *) DIR=$1; shift ;;
  esac
done

CFG=$DIR/$CFG_NAME
if [[ ! -r $CFG ]]; then
  printf 'gate_flags_for_arm: %s is not readable, so the switches this arm was built with cannot be read and the flag set it needs cannot be derived - and running the gate with a guessed set would be a verdict about an invocation the run will not make\n' "$CFG" >&2
  exit 1
fi

# The record is the preprocessor's own `-dM` dump, so its shape is `#define NAME VALUE` and a
# command-line definition appears **verbatim** (`-DSTAGE90_X = 1` records `1`, not `1u`). The value is
# extracted with the gate's own expression (`preflight_boot_check.sh:132-135`, `value_of`'s `sub()`)
# character for character, because a second hand-rolled parser of the same line would be a second
# definition of one value - and the failure it produces, a value that is empty here and correct there,
# is the silent kind.
declare -A sw=()
declare -A sw_n=()
while IFS=$'\t' read -r _name _val; do
  [[ -n $_name ]] || continue
  sw[$_name]=$_val
  sw_n[$_name]=$(( ${sw_n[$_name]:-0} + 1 ))
done < <(awk '
  $1 == "#define" && $2 ~ /^STAGE90_/ {
    v = $0; sub(/^[[:space:]]*#define[[:space:]]+[^[:space:]]+[[:space:]]*/, "", v)
    print $2 "\t" v
  }' "$CFG")

# The switches the table decides on, named once. A switch this list does not name is not read - and the
# gate closes the key set from its own side (`BUILD_CFG_KEYS` plus the converse check that refuses any
# `STAGE90_*` key that list does not carry), so a switch added on the build side is refused by the gate
# itself before it could make the derived set below silently incomplete.
SWITCHES=(STAGE90_XNU_ENTRY STAGE90_HW_WATCHDOG_SELFTEST STAGE90_DEADMAN_SELFTEST
          STAGE90_HANDOFF_MODE STAGE90_PMAP_ATTR_MODE STAGE90_CACHE_MODE
          STAGE90_HANDOFF_FAULT_INJECT_VA)
problem=''
for _s in "${SWITCHES[@]}"; do
  (( ${sw_n[$_s]:-0} == 1 )) \
    || problem+="$_s is named ${sw_n[$_s]:-0} time(s) in $CFG, and a switch read at all has to be named exactly once; "
  [[ -n ${sw[$_s]:-} ]] \
    || problem+="$_s is named with no value after it, which the gate refuses for every key in BUILD_CFG_KEYS because that is where its own off branches sit; "
done
if [[ -n $problem ]]; then
  printf 'gate_flags_for_arm: the switch record cannot be read well enough to derive the flag set: %s\n' "$problem" >&2
  exit 1
fi

# OFF is `0|0u`, exactly the gate's own `is_off` (`preflight_boot_check.sh:158`) - and NOT the empty
# value, which the loop above has already refused. An empty value reaching here would take the off arm
# and silently drop a flag the gate then demands.
on() {
  case ${sw[$1]:-} in 0|0u) return 1 ;; *) return 0 ;; esac
}
flags=()
add_flag() { on "$1" && flags+=("$2"); return 0; }
add_flag STAGE90_XNU_ENTRY --allow-xnu-entry
add_flag STAGE90_HW_WATCHDOG_SELFTEST --allow-hw-watchdog-selftest
add_flag STAGE90_DEADMAN_SELFTEST --allow-selftest
add_flag STAGE90_HANDOFF_FAULT_INJECT_VA --allow-fault-inject
case ${sw[STAGE90_HANDOFF_MODE]:-} in
  STAGE90_HANDOFF_MODE_PREFLIGHT_WATCHDOG_ONLY|1|1u) flags+=(--allow-preflight) ;;
  STAGE90_HANDOFF_MODE_FULL|2|2u)                    flags+=(--allow-full) ;;
esac
case ${sw[STAGE90_PMAP_ATTR_MODE]:-} in
  STAGE90_PMAP_ATTR_MODE_NORMAL_NC|1|1u) flags+=(--allow-attr-normal-nc) ;;
  STAGE90_PMAP_ATTR_MODE_NORMAL_WB|2|2u) flags+=(--allow-attr-normal-wb) ;;
esac
case ${sw[STAGE90_CACHE_MODE]:-} in
  STAGE90_CACHE_MODE_ICACHE|1|1u)        flags+=(--allow-icache) ;;
  STAGE90_CACHE_MODE_ICACHE_DCACHE|2|2u) flags+=(--allow-dcache) ;;
esac
# `--allow-xnu-entry` is the one flag required in both directions: needed on an entry image, refused on a
# ladder image (`preflight_boot_check.sh:1680`). The table above adds it only when the switch is ON, so an
# arm with the switch off derives a set without it - which is the set that arm needs.

# --- the vocabulary check, which is what bounds the copy --------------------------------------------
if [[ ! -r $GATE ]]; then
  printf 'gate_flags_for_arm: %s is not readable, so the flag vocabulary of the gate this set is derived FOR cannot be read - and a set derived without it could be missing a flag the gate demands\n' "$GATE" >&2
  exit 1
fi
VOCAB=(--allow-preflight --allow-full --allow-selftest --allow-attr-normal-nc
       --allow-attr-normal-wb --allow-icache --allow-dcache --allow-xnu-entry
       --allow-hw-watchdog-selftest --allow-fault-inject)
unknown=()
seen=$( { sed -n 's/^#[[:space:]]*Usage:[[:space:]]*//p' "$GATE"
          grep -oE 'needs --allow-[a-z-]+|without --allow-[a-z-]+' "$GATE"; } \
        | grep -oE -- '--allow-[a-z-]+' | LC_ALL=C sort -u )
while read -r _f; do
  [[ -n $_f ]] || continue
  case " ${VOCAB[*]} " in *" $_f "*) ;; *) unknown+=("$_f") ;; esac
done <<< "$seen"
if (( ${#unknown[@]} > 0 )); then
  printf 'gate_flags_for_arm: the gate names flag(s) this file has no switch for: %s - so the derived set could be missing one, and a run given it would be refused for a reason nothing here would have printed. Add the switch that requires it to the table\n' "${unknown[*]}" >&2
  exit 1
fi

if (( ${#flags[@]} > 0 )); then
  printf '%s\n' "${flags[@]}"
fi
exit 0
