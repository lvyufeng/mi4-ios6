#!/usr/bin/env bash
#
# Does the payload's own switch set agree with the entry image it carries?
#
# ONE QUESTION, AND IT IS THE ONE 732's STEP GOT WRONG. The payload and the entry image are two
# builds, described by two records - `stage90-build-config.txt` (16 `#define`s for the payload) and
# `xnu_arm_entry-config.txt` (the entry image's own switches, one `KEY=VALUE` per line) - and nothing
# in this repository ever compared them to each other. On 2026-09-26 that gap produced a real artifact:
# a payload was built with `STAGE90_XNU_ENTRY 0u` (`scripts/build.sh`'s DEFAULT, because
# `STAGE90_EXTRA_CFLAGS` was not set) around an entry image whose own config reads
# `STAGE90_XNU_STORAGE_PROBE=14`, and **nothing refused it** - the entry image was byte-identical, the
# gate accepted the tree under the flags it derives from the ARM (and that flag is only required for an
# XNU-entry payload, so the gate had no reason to look), and the record's `role=` sentence for the
# config claimed byte-identity with the pressed arm while the bytes said otherwise. It was caught only
# because the claim was written down and then measured, and it was one `fastboot boot` away from being
# an experiment nobody asked for.
#
# The invariant is not a matter of taste. A payload with `STAGE90_XNU_ENTRY` off **never jumps into
# XNU**: the whole storage ladder runs inside the XNU-entry path, so a rung recorded as N > 0 beside
# `STAGE90_XNU_ENTRY 0` describes an arm that cannot run at all. That is a contradiction between two
# files, and it is decidable from the two files alone.
#
# Usage:
#   tools/check_arm_variant_pair.sh [DIR]        # default: REPO_ROOT/out/stage90
#   tools/check_arm_variant_pair.sh --help
#
# Exit: 0 = the pair agrees (or no rung is claimed). 1 = REFUSED, with the two readings named.
#       2 = usage. An absent file, an absent key or a value that does not parse is a REFUSAL and not
#       a pass: a tool that cannot read one side of a comparison has not found the sides equal.

set -uo pipefail

SELF=$(readlink -f "${BASH_SOURCE[0]}")
REPO_ROOT=$(cd "$(dirname "$SELF")/.." && pwd)

DIR=""
while (($# > 0)); do
  case $1 in
    -h|--help)
      awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$SELF"
      exit 0 ;;
    --*) printf 'check_arm_variant_pair: unknown option %s\n' "$1" >&2; exit 2 ;;
    *) if [[ -z $DIR ]]; then DIR=$1; else printf 'check_arm_variant_pair: unexpected argument %s\n' "$1" >&2; exit 2; fi; shift ;;
  esac
done
[[ -n $DIR ]] || DIR=$REPO_ROOT/out/stage90

refuse() { printf 'REFUSED: %s\n' "$*" >&2; exit 1; }

ENTRY_CFG=$DIR/xnu_arm_entry-config.txt
PAY_CFG=$DIR/stage90-build-config.txt

# An absent file is not a failed read, and neither is a key that is not there: each is named.
[[ -d $DIR ]]            || refuse "$DIR is not a directory, so there is no pair to compare"
[[ -r $ENTRY_CFG ]]      || refuse "$ENTRY_CFG is not readable - the entry image's own switches are one half of this comparison and a missing half is not a half that agrees"
[[ -r $PAY_CFG ]]        || refuse "$PAY_CFG is not readable - the payload's own switches are the other half"
[[ -s $ENTRY_CFG ]]      || refuse "$ENTRY_CFG is empty, which is a file that was written and says nothing"
[[ -s $PAY_CFG ]]        || refuse "$PAY_CFG is empty"

# The entry record's rung line. Its format is `KEY=VALUE`, and the key is matched by name rather than
# by position: the file carries the entry bin's hash and byte count as the two lines above it.
probe_line=$(grep -m1 '^STAGE90_XNU_STORAGE_PROBE=' "$ENTRY_CFG" || true)
[[ -n $probe_line ]] || refuse "$ENTRY_CFG carries no STAGE90_XNU_STORAGE_PROBE line, so which rung this entry image is has no reading here - and this tool will not guess it from the payload's flags, which is the whole comparison it exists to make"
probe=${probe_line#STAGE90_XNU_STORAGE_PROBE=}
[[ $probe =~ ^[0-9]+$ ]] || refuse "$ENTRY_CFG's STAGE90_XNU_STORAGE_PROBE reads '$probe', which is not a number - a bound this tool cannot read is a refusal and not a pass"

# The payload's own switch. `#define STAGE90_XNU_ENTRY <v>` - the value may be spelled `1` or `1u`, so
# the suffix is stripped before it is compared, and a value that is neither 0 nor 1 is refused rather
# than treated as "on".
ent_line=$(grep -m1 -E '^#define[[:space:]]+STAGE90_XNU_ENTRY[[:space:]]' "$PAY_CFG" || true)
[[ -n $ent_line ]] || refuse "$PAY_CFG has no '#define STAGE90_XNU_ENTRY' line, so whether this payload jumps into XNU is not recorded in it - the other half of the comparison is absent"
ent=$(printf '%s\n' "$ent_line" | awk '{print $3}')
ent_num=${ent%u}
[[ $ent_num =~ ^[0-9]+$ ]] || refuse "$PAY_CFG's STAGE90_XNU_ENTRY reads '$ent', which is not a number"
case $ent_num in 0|1) ;; *) refuse "$PAY_CFG's STAGE90_XNU_ENTRY reads '$ent', which is neither 0 nor 1 - this tool compares two states and will not read a third as one of them" ;; esac

if (( probe > 0 )) && (( ent_num == 0 )); then
  refuse "the pair contradicts itself: $ENTRY_CFG records STAGE90_XNU_STORAGE_PROBE=$probe, i.e. the storage ladder's rung $probe, and $PAY_CFG records STAGE90_XNU_ENTRY $ent, i.e. a payload that never jumps into XNU. The whole ladder runs inside the XNU-entry path, so this payload cannot run the rung its own entry image carries - it is a different experiment, and the entry image being byte-identical is exactly why no other check in this repository sees it. Give the build the previous arm's own switch set (STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1') and compare the config it produces with the record's."
fi

if (( probe == 0 )); then
  echo "ok: the pair agrees - $ENTRY_CFG claims no storage rung (STAGE90_XNU_STORAGE_PROBE=0), so nothing here requires the payload to jump into XNU ($PAY_CFG: STAGE90_XNU_ENTRY $ent)."
  exit 0
fi

echo "ok: the pair agrees - the entry image records STAGE90_XNU_STORAGE_PROBE=$probe and the payload that carries it records STAGE90_XNU_ENTRY $ent, so the payload does reach the path the rung runs in."
exit 0
