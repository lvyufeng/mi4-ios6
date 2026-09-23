#!/usr/bin/env bash
#
# Verify that a directory is the revert set this project recorded, before anyone copies it back.
#
# Why this exists at all
# ----------------------
# The standing constraint on this device is that it must never be bricked and never hang permanently,
# and the mechanism behind that claim is not the watchdog - it is that the arm in `out/` can always be
# put back to one that was known to reach a specific point. "Revert is two `cp`s and no build" is the
# sentence this project has repeated since 595, and it is a claim about a *directory of files*: which
# files, with which bytes. Those files are outside version control (`out/` is gitignored and the 574
# park is in `/tmp`), so until now the identity of the revert set lived in prose in 22 documents.
#
# `stages/stage90/revert-set.txt` is the record; this script is the comparison. A record nothing
# compares against is not a constraint, which is the failure this file is written to avoid rather than
# to commit.
#
# What is checked, in order, and what each is worth
# -------------------------------------------------
#  1. the record is readable, non-empty and hashes at least one line - a missing record is a refusal,
#     never a pass
#  2. the named directory exists and is a directory       - 601's class: an absent directory is not a
#                                                           failed read
#  3. every file of the set is present and non-empty       - an absent member is named as absent, not
#                                                           reported as a hash mismatch
#  4. its size equals the record's                          - a second, independent field of the same
#                                                           bytes; two fields agreeing is what makes a
#                                                           typo in either a reading and not a silence
#  5. its sha256 equals the record's                        - the constraint
#  6. the manifest's member list, twice over. The trio of 5, 6a and 6b partitions cleanly, and each leg is
#     load-bearing - which direction is covered by which check is worth stating because it is not the
#     obvious split:
#
#       | check | reads from              | catches                                                        |
#       | ----- | ----------------------- | -------------------------------------------------------------- |
#       |  5    | the record              | the manifest FILE doctored - a member added **or removed**; a   |
#       |       |                         | 4-member manifest is refused by its size and hash alone         |
#       |  6a   | the record              | the record naming a member it does not itself carry             |
#       |  6b   | the target              | a target manifest that has GROWN names outside the set          |
#
#     **The shrinkage direction is therefore covered by pinning the manifest as bytes, not by either
#     member check** - measured: against a park whose manifest names four members, 6a prints its `ok` lines
#     from the record's own field and 6b prints `every one of the 4 member(s) its own manifest names is in
#     the set`, and both are SILENT, while check 5 refuses with `SHA256SUMS.txt is 451 bytes, the record
#     says 560`. So `manifest_members=` and check 5 are complements and not redundant, and a record that
#     dropped the manifest from its file list would lose this direction entirely.
#
#     a. every name in the record's `manifest_members=` field is also a `file=` line of the same set -
#        criterion B closed over the record. The gate verifies the manifest (`sha256sum -c`, its line
#        139), and `sha256sum -c` opens every path the manifest names, so a file named only in the
#        manifest is still read by the gate and a revert that omits it leaves the gate red while nothing
#        in this record notices. The record's first version omitted two such files.
#     b. if the target carries a manifest of its own, **every name in it is in the set** - the guard that
#        fires when a build starts writing a sixth member. Coverage is one-directional, so this is the
#        only direction that can catch a growing manifest, and it is checked against the file on disk
#        rather than against a remembered list.
#
# WHAT THIS SCRIPT DELIBERATELY DOES NOT READ: the target directory's own `SHA256SUMS.txt`, even though
# one is a member of the set and is hashed like any other file. Every build writes that file with
# **absolute** paths into the tree that produced it, so `sha256sum -c SHA256SUMS.txt` run inside a park
# hashes the *live* tree and prints `FAILED` for every file that has changed since - while the parked
# bytes are all correct. A manifest of absolute paths is a record of a location, not of a directory. The
# script therefore hashes from its own record and prints the location a foreign manifest names as
# narration, so the trap is visible at the moment a reader meets the directory rather than rediscovered
# as a "corrupt park".
#
# Success is loud. Every verified file prints an `ok` line with its hash, and the summary names the
# absolute path that was hashed, because a verdict about a directory that does not say which directory
# it was about is how a check taken against a copy gets quoted as a check taken against the original.
#
# Usage:
#   ./tools/verify_revert_set.sh DIR [--record=PATH] [--set=NAME]
#
#   DIR           the directory to verify, e.g. /tmp/r594/frozen-payload
#   --set=NAME    verify only this set (default: every set in the record)
#
# Exit: 0 = every file of every requested set matched; 1 = refused, with the failing member named.

set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RECORD=$ROOT/stages/stage90/revert-set.txt
WANT_SET=""
DIR=""

for arg in "$@"; do
  case "$arg" in
    --record=*) RECORD=${arg#--record=} ;;
    --set=*)    WANT_SET=${arg#--set=} ;;
    # The header is what this prints, and the range is DERIVED from the file rather than written as a
    # line number. It was `sed -n '2,45p' ... | head -24` until this step, and editing the header above
    # moved the 6a/6b description past line 45 - so `--help` silently stopped at check 5 and dropped the
    # trap, the union criterion and the usage line. A help text pinned to a line number is a claim about
    # how long the comment is, and this project has already paid for that shape of claim. Now it prints
    # every leading comment line and stops at the first line of code, whatever the header's length.
    --help|-h)  awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; exit 1 ;;
    -*)         echo "REFUSING: unknown argument $arg" >&2; exit 1 ;;
    *)          if [[ -n $DIR ]]; then
                  echo "REFUSING: more than one directory given ($DIR and $arg). One directory per run, so the verdict names one thing." >&2
                  exit 1
                fi
                DIR=$arg ;;
  esac
done

refuse() { echo "REFUSING: $*" >&2; exit 1; }

# A check that cannot run is not a check that passed, and an absent tool makes the record lookup return
# nothing - so the refusal would blame the artifact for a fact about the host. 608's defect, its guard.
#
# The survey is a loop and not a `command -v` chain on purpose: this file must run under `set -u` with
# no `-e`, so a chain that stops at the first miss would report one tool when three are missing.
MISSING=""
for t in sha256sum stat awk sed grep tr head cut mktemp sort; do
  command -v "$t" >/dev/null 2>&1 || MISSING="$MISSING $t"
done
[[ -z $MISSING ]] || refuse "this host is missing$MISSING, which this script needs to hash, read or compare. A check that cannot run is not a check that passed: an absent tool would make the record lookup return nothing and the refusal would blame the directory"

[[ -n $DIR ]] || refuse "no directory given. This script verifies ONE directory against the recorded revert set; without one there is nothing to hash"

[[ -e $DIR ]]     || refuse "$DIR does not exist. An absent directory is not a set that failed to verify"
[[ -d $DIR ]]     || refuse "$DIR is not a directory"
[[ -r $DIR ]]     || refuse "$DIR is not readable by this user"
[[ -s $RECORD ]]  || refuse "no readable record at $RECORD - without a record there is nothing to compare against, and an empty record would verify any directory at all. Do not pass this as a green run"

ABS=$(cd "$DIR" && pwd)

# ---- 1. read the record, keeping the sets in order and refusing a malformed line ----------------
SETS=""
NLINES=0
LINENO_REC=0
while IFS= read -r line || [[ -n $line ]]; do
  LINENO_REC=$((LINENO_REC + 1))
  [[ $line =~ ^[[:space:]]*# ]] && continue
  [[ -z ${line//[[:space:]]/} ]] && continue
  s=""; h=""; b=""; f=""; r=""; mm=""
  for kv in $line; do
    case "$kv" in
      set=*)    s=${kv#set=} ;;
      sha256=*) h=${kv#sha256=} ;;
      bytes=*)  b=${kv#bytes=} ;;
      file=*)   f=${kv#file=} ;;
      role=*)   r=${kv#role=} ;;
      manifest_members=*) mm=${kv#manifest_members=} ;;
      *) refuse "the record's line $LINENO_REC has a field this script does not know: '$kv'. Refusing rather than ignoring it, because an ignored field is how a typo in 'sha256=' becomes a file nobody checked" ;;
    esac
  done
  [[ -n $s ]] || refuse "the record's line $LINENO_REC has no set= - the set is what groups lines into one revert"
  [[ $h =~ ^[0-9a-f]{64}$ ]] || refuse "the record's line $LINENO_REC has no usable sha256= ('$h'). A hash that is not 64 hex characters cannot be compared against anything"
  [[ $b =~ ^[0-9]+$ ]] || refuse "the record's line $LINENO_REC has no usable bytes= ('$b')"
  [[ -n $f ]] || refuse "the record's line $LINENO_REC has no file="
  [[ $f == */* ]] && refuse "the record's line $LINENO_REC names '$f' with a path separator. The record names files inside the set, not paths - a path here is how a record starts describing a location instead of a directory"
  case " $SETS " in *" $s "*) ;; *) SETS="$SETS $s" ;; esac
  # The set is stored beside the line rather than re-derived from it later. Re-deriving it would be one
  # value with two definitions: the first pass parses `set=` as a field, and a second pass that matches
  # the line with a substring test agrees with it only for as long as no set name is a prefix of another.
  eval "REC_$NLINES=\$line"
  eval "RECSET_$NLINES=\$s"
  eval "RECMAN_$NLINES=\$mm"
  NLINES=$((NLINES + 1))
done < "$RECORD"

[[ $NLINES -gt 0 ]] || refuse "the record at $RECORD carries no file lines. An empty record is not a set that verified"

if [[ -n $WANT_SET ]]; then
  case " $SETS " in
    *" $WANT_SET "*) SETS=" $WANT_SET" ;;
    *) refuse "the record has no set named '$WANT_SET'. It carries:$SETS" ;;
  esac
fi

# ---- 2. narration: a foreign manifest in the target, and where its paths point -------------------
# Printed before the verdict so that a reader who is about to trust `sha256sum -c` here sees why this
# script does not. It never refuses: a correct park has one of these too.
if [[ -s $ABS/SHA256SUMS.txt ]]; then
  nabs=$(grep -c '^[0-9a-f]\{64\}  /' "$ABS/SHA256SUMS.txt" 2>/dev/null || true)
  [[ $nabs =~ ^[0-9]+$ ]] || nabs=0
  if [[ $nabs -gt 0 ]]; then
    loc=$(sed -n 's|^[0-9a-f]\{64\}  \(/.*\)/[^/]*$|\1|p' "$ABS/SHA256SUMS.txt" 2>/dev/null | sort -u | head -3 | tr '\n' ' ')
    echo "  note  this directory carries its own build manifest with $nabs absolute path(s), pointing at: $loc"
    echo "        Those paths are not read here. A manifest of absolute paths records a location, not a"
    echo "        directory: run inside a park it hashes the tree the build ran in, and prints FAILED for"
    echo "        every file that has changed since. This script hashes from the record instead."
  fi
fi

# ---- 3. verify -----------------------------------------------------------------------------------
OK=0
OKM=0
FAILED=0
for s in $SETS; do
  members=0
  SEEN=""
  echo "== set $s in $ABS =="
  i=0
  while [[ $i -lt $NLINES ]]; do
    eval "line=\$REC_$i"
    eval "lset=\$RECSET_$i"
    i=$((i + 1))
    [[ $lset == "$s" ]] || continue
    h=""; b=""; f=""; r=""
    for kv in $line; do
      case "$kv" in
        sha256=*) h=${kv#sha256=} ;;
        bytes=*)  b=${kv#bytes=} ;;
        file=*)   f=${kv#file=} ;;
        role=*)   r=${kv#role=} ;;
      esac
    done
    members=$((members + 1))
    SEEN="$SEEN $f"

    if [[ ! -e $ABS/$f ]]; then
      echo "  FAIL  $f is ABSENT from this directory. The set is incomplete: a revert that omits a file"
      echo "        the gate reads is not a revert, whatever the files present hash to."
      FAILED=$((FAILED + 1))
      continue
    fi
    if [[ ! -f $ABS/$f ]]; then
      echo "  FAIL  $f is not a regular file"
      FAILED=$((FAILED + 1))
      continue
    fi
    if [[ ! -s $ABS/$f ]]; then
      echo "  FAIL  $f is empty (the record says $b bytes)"
      FAILED=$((FAILED + 1))
      continue
    fi

    got_bytes=$(stat -c%s "$ABS/$f" 2>/dev/null)
    if [[ $got_bytes != "$b" ]]; then
      echo "  FAIL  $f is $got_bytes bytes, the record says $b. Two fields of one file disagreeing is a"
      echo "        refusal before any hashing: these are not the bytes that were measured."
      FAILED=$((FAILED + 1))
      continue
    fi

    got=$(sha256sum "$ABS/$f" 2>/dev/null | cut -d' ' -f1)
    if [[ -z $got ]]; then
      echo "  FAIL  $f could not be read by this user, so it was NOT verified. An unreadable file is not"
      echo "        a file that matched."
      FAILED=$((FAILED + 1))
      continue
    fi
    if [[ $got != "$h" ]]; then
      echo "  FAIL  $f hashes to $got"
      echo "        the record has    $h"
      echo "        role: $r"
      FAILED=$((FAILED + 1))
      continue
    fi
    printf '  ok    %-28s %s  %s bytes\n' "$f" "${got:0:16}…" "$b"
    OK=$((OK + 1))
  done
  [[ $members -gt 0 ]] || refuse "set '$s' selected from the record but no line matched it, which cannot happen after the name check above - refusing rather than printing an empty set as verified"

  # ---- 6a. criterion B, closed over the record ------------------------------------------------
  # The gate verifies the manifest (its line 139: `sha256sum -c SHA256SUMS.txt`), and `sha256sum -c`
  # opens every path the manifest names - so a file named only inside the manifest is read by the gate
  # all the same. A revert that omits one leaves the gate red while every hash in this record matched.
  # Refusing on a record-only inconsistency (a member not in the set) is not pedantry: it is the state in
  # which this record's first version shipped two files short of its own criterion.
  i=0
  while [[ $i -lt $NLINES ]]; do
    eval "lset=\$RECSET_$i"
    eval "lmm=\$RECMAN_$i"
    eval "lfile=\$REC_$i"
    i=$((i + 1))
    [[ $lset == "$s" ]] || continue
    [[ -n $lmm ]] || continue
    for m in ${lmm//,/ }; do
      if ! printf '%s\n' $SEEN | grep -qxF -- "$m"; then
        echo "  FAIL  the manifest member '$m' is not a file= line of set '$s'. The gate reads it through"
        echo "        \`sha256sum -c\`, so it belongs in the set; a record that names it in one field and not"
        echo "        in the other is exactly how a revert leaves the gate red with every hash matching."
        FAILED=$((FAILED + 1))
      else
        printf '  ok    manifest member %-24s is a member of the set (criterion B)\n' "$m"
        OK=$((OK + 1)); OKM=$((OKM + 1))
      fi
    done
  done

  # ---- 6b. the manifest on disk must not name anything outside the set --------------------------
  # Coverage is one-directional - a set that is too LARGE still passes 6a - so this is the direction that
  # catches a build which starts writing a sixth member into the manifest: the gate would read that file
  # through `sha256sum -c`, the record would not name it, and a revert would leave the gate red with no
  # hash mismatching. Checked against the file on disk rather than against a remembered list, so it stays
  # true as `build.sh` changes what it writes.
  if [[ -s $ABS/SHA256SUMS.txt ]]; then
    # The optional directory is part of the pattern on purpose: a build writes ABSOLUTE paths, but a
    # hand-made or relative manifest has none, and a pattern that requires a slash silently derives
    # NOTHING from those - an empty list, no refusal, a green line. The rehearsal's own cell caught this.
    ondisk=$(sed -n 's|^[0-9a-f]\{64\}  \(.*/\)\?||p' "$ABS/SHA256SUMS.txt" 2>/dev/null | sort -u)
    n_ondisk=$(printf '%s\n' $ondisk | grep -c . || true)
    outside=""
    for m in $ondisk; do
      printf '%s\n' $SEEN | grep -qxF -- "$m" || outside="$outside $m"
    done
    if [[ -n $outside ]]; then
      echo "  FAIL  the manifest in this directory names file(s) the set does not:$(printf ' %s' $outside)"
      echo "        The gate reads every one of them through \`sha256sum -c\`, so a revert that does not"
      echo "        restore them leaves the gate red. Add them to the record, or the record is not the set."
      FAILED=$((FAILED + 1))
    else
      printf '  ok    every one of the %s member(s) its own manifest names is in the set\n' "$n_ondisk"
      OK=$((OK + 1)); OKM=$((OKM + 1))
    fi
  else
    printf '  note  this directory carries no manifest, so check 6b had nothing to read here\n'
  fi
done

echo
if [[ $FAILED -eq 0 ]]; then
  echo "VERIFIED: $((OK - OKM)) file(s) of$SETS matched the record at $RECORD, hashed in place in $ABS,"
  echo "          and $OKM manifest-member check(s) agree with the set."
  echo "          This says these are the recorded bytes and that the set covers what the gate reads"
  echo "          through the manifest it verifies. It does not say a revert cannot leave the gate red in"
  echo "          some way neither derivation sees - the test for that is revert, then run the gate."
  exit 0
fi
echo "REFUSING: $FAILED of $((OK + FAILED)) file(s) did not match the record. Nothing in $ABS was"
echo "          modified, and no file was copied anywhere: this run only hashed."
exit 1
