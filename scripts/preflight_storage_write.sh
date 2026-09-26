#!/usr/bin/env bash
#
# Gate the first PERSISTENT write of this project - the step the user's condition gates on
# 「如果os已经能进去了的话，可以twrp写入到存储里了」. Until now every device touch in this tree has been
# `fastboot boot`, which writes nothing to storage, and that is the whole reason a brick has been
# impossible by construction. This script is where that property ends, so it is built to refuse.
#
# It refuses by default. It writes nothing, runs no fastboot, and touches no device: it verifies the
# evidence and the rollback, prints the exact commands, or names the precondition that is missing.
#
# THE FOUR PRECONDITIONS, and why each is one
# -------------------------------------------
#  1. **The OS has been observed staying up.** The user's condition is 「如果os已经能进去了的话」, and a
#     condition that is read by feel is not a precondition. It is defined here as the runner's own two
#     clauses, in ONE capture, because the runner's text already names the ceiling:
#       * the goal block's PASS (user mode reached and a driver answering), and
#       * the arm's own clause PASS (the machine stayed up past the park).
#     The first alone is a FLOOR that 520 and 533 also meet - they died at the idle exit's `pop` and
#     their logs carry the whole userland phase. A gate that accepted the floor would clear a write on
#     the strength of a boot that died. So the criterion is both, and it is read out of the runner
#     rather than re-derived here (see below).
#  2. **The rollback exists and verifies.** `xiaomi4-cancro-backup-20260604-112053/` holds the stock
#     images and a `SHA256SUMS.txt`. The whole manifest must verify AND the target's own stock image
#     must be present - a rollback that has not been checked is a promise, not a path.
#
#     **What this establishes is CONSISTENCY, and that limit is measured, not assumed.** The manifest's
#     entries are absolute paths written when the backup was taken, so the check answers "do these bytes
#     match this manifest", not "is this the phone's factory image". Falsified (629) against a faithful
#     copy of the backup: with one byte of `boot.img` moved the check refuses, and with that byte moved
#     *and the manifest re-written to match* it passes, 6/6 ok, exit 0. Nothing about hashes could do
#     better - a self-written record is not a constraint (the record and its subject would agree) - and
#     the property that makes this rollback known-good is the **date it was taken**, which is in the
#     directory's own name and in `docs/reference/recovery-and-rollback.md`, not in this check. The
#     tool image does not have this limit, because its record is `tool-images.txt` **plus** TWRP's own
#     detached signature (precondition 4); the rollback has no such signature to appeal to.
#  3. **The target is on the low-risk list.** `docs/reference/recovery-and-rollback.md` names `boot` and
#     `recovery` as the only two least-bad persistent targets, and forbids the boot chain, radio and
#     calibration partitions. Anything else is refused by name.
#  4. **TWRP is never flashed.** This is the part worth having: the user's sentence names TWRP as the
#     tool that writes storage, and the tempting reading is "flash TWRP". It is not needed. TWRP can be
#     **booted** non-persistently (`fastboot boot twrp.img`) and then write the target from inside
#     itself (`adb shell dd`). That keeps `fastboot flash` out of the picture for the tool and leaves
#     exactly ONE persistent change - the intended one - instead of two. The plan below prints that
#     form, and the script has no `fastboot flash` line at all.
#
#     **And the tool is THE recorded one, not merely a boot image.** `--boot-image` used to be checked
#     by parsing it, which is a claim about a *format*; a file that parses and is not the image this
#     tree recorded would have been cleared. The record is `records/tool-images.txt` (hash,
#     size, md5, source page, signer) and the check is `tools/verify_tool_image.sh`, run below with
#     `--require-role=twrp`. It refuses anything whose hash is not in the record, anything recorded
#     for another role, and - the case a hash alone cannot catch - an image edited *and* re-recorded
#     to match, because TWRP's own detached signature then disagrees. An image that is not recorded
#     cannot be cleared here whatever it is called (608).
#
# WHY THE CRITERION STRINGS ARE READ OUT OF THE RUNNER
# ---------------------------------------------------
# Grepping the summariser's output for a phrase is a claim about what that file prints, and this
# project has paid for that class repeatedly (602: a rule written down in a comment and enforced by
# nothing). So before using the phrases, this script asserts that `run_and_capture.sh` still contains
# each one; if it does not, the gate refuses rather than silently clearing a write on a phrase nothing
# prints any more.
#
# Usage:
#   ./preflight_storage_write.sh                       # print the plan and every precondition; refuse
#   ./preflight_storage_write.sh --evidence=LOG --target=boot --image=PATH \
#        --boot-image=PATH --allow-storage-write
#   ./preflight_storage_write.sh --help                # print THIS header, whole, and exit 1
#
# `--boot-image` is required: it is the tool that gets BOOTED (never flashed) and the target is
# written from inside it. Its hash must be one of `records/tool-images.txt`'s lines.
#
# Exit: 0 = every precondition verified and the commands are printed; 1 = refused, named reason.
# `--help` also exits 1. **That is not a clearance and never can be** - 0 is the only status that
# clears a write, so the code that asks for help must not be the code that says a write is cleared.

set -uo pipefail

# **`$0` is resolved to an absolute path BEFORE the `cd`, and that is a repair, not a style choice.**
# This file used to `cd "$(dirname "$0")"` with `$0` still relative, and `usage()` reads the file
# through `$0`; so `bash scripts/preflight_storage_write.sh --help` from the repository root
# made `$0` the *caller's* relative path, the `cd` made the working directory the *tool's*, and the
# read then looked for `src/scripts/preflight_storage_write.sh` - printing
# `sed: can't read ...` and **nothing else**. Measured before the repair, and it is 615's defect
# exactly (a path is the caller's or the tool's, and a `cd` before argument parsing silently makes it
# the tool's) arriving in the file that guards the only irreversible action this project has. A
# reader's first move on a gate is `--help`; it answered with an error message.
SELF=$(readlink -f "${BASH_SOURCE[0]}")
cd "$(dirname "$SELF")"
SCRIPT_DIR=$PWD
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
SRC_DIR=$REPO_ROOT/src

SERIAL=${SERIAL:-4a2fe00b}
BACKUP=$REPO_ROOT/xiaomi4-cancro-backup-20260604-112053
RUNNER=$SCRIPT_DIR/run_and_capture.sh

ALLOW_STORAGE_WRITE=0
TARGET=""
IMAGE=""
EVIDENCE=""
BOOTLOADER_IMAGE=""       # the TWRP (or other) image to *boot*, never to flash - see precondition 4

# The two clauses of precondition 1, quoted from the runner. They are checked against the runner
# below before they are used, so a reworded runner is a refusal here and not a quiet pass.
GOAL_PASS='PASS  and this is 504'"'"'s reading re-read here'
ARM_PASS='=> this arm did what it was built to do at the point that matters'

refuse() { echo "REFUSING: $*" >&2; exit 1; }

# **`--help` prints the WHOLE header, and it is bounded by the file's own comment block rather than by
# line numbers.** The previous form was `sed -n '2,50p' "$0" … | head -20`, and it dropped the tail: the
# `Usage:` block and the exit contract sit at lines 52-60, past both bounds, so the two things a reader
# asks `--help` for were the two things it never printed - **silently**, because a slice that stops early
# still exits on its own terms. That is 613's two findings in one place (a help text pinned to a line
# number, and an assertion of completeness made by a command that cannot see what it missed). This
# reads until the first line that is not a comment, so an edit to this header changes the help text and
# cannot cost it its tail; `head` is gone, and the printed text is the file's own words.
usage() {
  awk 'NR > 1 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "$SELF"
  exit 1
}

for arg in "$@"; do
  case "$arg" in
    --allow-storage-write) ALLOW_STORAGE_WRITE=1 ;;
    --target=*)            TARGET=${arg#--target=} ;;
    --image=*)             IMAGE=${arg#--image=} ;;
    --evidence=*)          EVIDENCE=${arg#--evidence=} ;;
    --boot-image=*)        BOOTLOADER_IMAGE=${arg#--boot-image=} ;;
    --help|-h)             usage ;;
    *)                     refuse "unknown argument: $arg (see --help)" ;;
  esac
done

echo "== persistent-storage write: the preconditions, checked =="
echo "   serial:        $SERIAL"
echo "   target:        ${TARGET:-<not given>}"
echo "   image:         ${IMAGE:-<not given>}"
echo "   evidence:      ${EVIDENCE:-<not given>}"
echo "   boot image:    ${BOOTLOADER_IMAGE:-<not given> - only needed if the target is written from inside TWRP>}"
echo

# --- 0. the tool may not be run into a cleared state -------------------------- ----------------
if [[ $ALLOW_STORAGE_WRITE -ne 1 ]]; then
  echo "REFUSING: this script clears a PERSISTENT write, and the flag that says so is not set."
  echo "          Every device touch in this tree so far has been \`fastboot boot\`, which writes"
  echo "          nothing to storage - that is why a brick has been impossible by construction, and"
  echo "          this is the step where that stops being true. Read the plan above, then re-run with"
  echo "          --allow-storage-write plus --evidence, --target and --image."
  exit 1
fi

# --- the verdict machinery, and why every check runs -------------------------------------------
#
# **This gate used to stop at its first refusal, and that is 629's defect.** The checks are a
# conjunction, and the ONE that fails in the state the phone is actually in - the evidence that the OS
# stayed up - sits third of eight. So the five checks after it had **never been evaluated** on this
# host: the image's existence and hash, the rollback against its own manifest, the tool image's
# existence, the tool-versus-payload identity test, and the check that the tool image is the one this
# tree records. They run for the first time on the day the OS finally stays up - which is the one
# moment the operator is holding a booted phone with a spent press, and the moment this file exists
# for. That is `[[mi4-the-step-after-the-point-of-no-return]]` arriving in the gate that guards the
# only action in this project that can brick the device: the steps that only run once the boot has
# already succeeded are the least tested.
#
# So every check now runs, each records a named verdict, and the refusal is the *conjunction* of them
# rather than the first one reached. **The permit does not weaken**: the plan and `CLEARED` are
# printed only when all of them pass, and the exit is 1 if any did not. What changes is that the
# operator sees the whole state instead of a chain that reveals itself one refusal at a time.
#
# The check names are the ones the refusals already used; nothing is renamed, and every refusal
# message below is the text this gate already printed.
CHECKS=()      # "name" per check, in the order they run
VERDICT=()     # "ok" or "REFUSED" or "not-reached"
DETAIL=()      # the reason, verbatim - the same sentence the first-refusal form printed
NFAIL=0
NSKIP=0

ok()   { CHECKS+=("$1"); VERDICT+=(ok);      DETAIL+=("$2"); }
fail() { CHECKS+=("$1"); VERDICT+=(REFUSED); DETAIL+=("$2"); NFAIL=$((NFAIL+1)); }

# A check whose *input* is absent is neither a pass nor a failure of what it tests: it could not be
# made to look. That is 601's distinction (an unreadable producer is not a value), and it is why the
# summary prints `not reached` rather than `ok` for it - a green cell for a check that never ran is
# the shape this project's whole instrumentation exists to avoid.
#
# **And a check that could not look is not a check that passed, so a skip refuses too.** The third
# verdict is in the refusal condition at the end of this file and not only in the table, because the
# alternative is a printed sentence that the state does not support: with the condition on refusals
# alone, a run with zero refusals and one skip would end "all 6 check(s) passed", which is a claim
# about a check that was never made. With the six checks this file has, every skip is *also*
# accompanied by a refusal (check 2's skip runs into check 3's summarise, and check 5's skip needs
# `--target` empty, which check 1 refuses), so the clause does not change any current verdict - it
# removes the shape rather than a case, and 629 falsified it by mutating one skip site on a copy (see
# the experiment doc).
skip() { CHECKS+=("$1"); VERDICT+=(not-reached); DETAIL+=("$2"); NSKIP=$((NSKIP+1)); }

# --- 1. the target is one of the two low-risk partitions --------------------------------------
case "$TARGET" in
  boot|recovery) ok "the target is a low-risk partition" "target $TARGET" ;;
  "")  fail "the target is a low-risk partition" "no --target given. The only targets this project allows are boot and recovery (docs/reference/recovery-and-rollback.md); the boot chain, radio and calibration partitions are refused by name there and here" ;;
  *)   fail "the target is a low-risk partition" "target [$TARGET] is not one of the two low-risk partitions. docs/reference/recovery-and-rollback.md forbids writing sbl1, rpm, tz, DDR, ssd, dbi, aboot, modem, modemst1, modemst2, fsg, fsc and persist; a mistake in any of those may need EDL/JTAG, for which this repository documents no verified path" ;;
esac

# --- 2. the criterion is a reading of the runner, and the runner still says it -----------------
if [[ ! -r $RUNNER ]]; then
  skip "the criterion strings are still in the runner" "cannot read the runner at $RUNNER, so the criterion below cannot be established"
elif ! grep -qF -- "$GOAL_PASS" "$RUNNER"; then
  fail "the criterion strings are still in the runner" "the goal clause [$GOAL_PASS] is not in run_and_capture.sh any more, so this gate would be reading a phrase nothing prints. Re-derive the criterion from the runner before clearing a write"
elif ! grep -qF -- "$ARM_PASS" "$RUNNER"; then
  fail "the criterion strings are still in the runner" "the arm clause [$ARM_PASS] is not in run_and_capture.sh any more, so this gate would be reading a phrase nothing prints. Re-derive the criterion from the runner before clearing a write"
else
  ok "the criterion strings are still in the runner" "both clauses are in run_and_capture.sh"
fi
RUNNER_OK=$(( NFAIL == 0 ? 1 : 0 ))

# --- 3. the evidence says the OS stayed up ------------------------------------------------------
# The cascade below was six `refuse` calls in a row; each becomes a verdict, and the ones that need the
# file to exist are recorded as failures rather than skipped, because an absent log is exactly the
# state the criterion is unmet in (601: an absent file is not a capture that failed to say it).
if [[ -z $EVIDENCE ]]; then
  fail "the evidence is a capture where the OS stayed up" "no --evidence given. This write is cleared only by a capture in which the machine was observed staying up; a write cleared without one is not gated at all"
elif [[ ! -e $EVIDENCE ]]; then
  fail "the evidence is a capture where the OS stayed up" "no log at $EVIDENCE. An absent file is not a capture that failed to say the criterion - it is no evidence at all (601's class: an unreadable producer is not a value)"
elif [[ ! -r $EVIDENCE ]]; then
  fail "the evidence is a capture where the OS stayed up" "the file at $EVIDENCE exists and cannot be read. Read it as root and pass the copy; an unreadable log is not a cleared criterion"
elif [[ ! -s $EVIDENCE ]]; then
  fail "the evidence is a capture where the OS stayed up" "the file at $EVIDENCE is empty. An empty log is a file that exists (596a) and it carries no reading"
else
  EVIDENCE_OUT=$(mktemp) || EVIDENCE_OUT=""
  if [[ -z $EVIDENCE_OUT ]]; then
    fail "the evidence is a capture where the OS stayed up" "mktemp failed, so the criterion could not be read out of $EVIDENCE"
  else
    trap 'rm -f "$EVIDENCE_OUT"' EXIT
    bash "$RUNNER" --summarise "$EVIDENCE" > "$EVIDENCE_OUT" 2>/dev/null
    sum_code=$?
    if [[ $sum_code -ne 0 ]]; then
      fail "the evidence is a capture where the OS stayed up" "the runner could not summarise $EVIDENCE (exit $sum_code), so the criterion is UNREAD. Do not clear a write on a log the reader refused"
    elif ! grep -qF -- "$GOAL_PASS" "$EVIDENCE_OUT"; then
      fail "the evidence is a capture where the OS stayed up" "the capture at $EVIDENCE does not show the goal's own clause (user mode reached and a driver answering). The OS has not been observed getting in, so 「如果os已经能进去了的话」 is unmet"
    elif ! grep -qF -- "$ARM_PASS" "$EVIDENCE_OUT"; then
      fail "the evidence is a capture where the OS stayed up" "the capture at $EVIDENCE shows the goal's clause but NOT the arm's - the machine was not observed staying up past the park. That is the floor 520 and 533 also meet, and they died at the idle exit's pop; a write cleared on it would be cleared by a boot that died"
    else
      ok "the evidence is a capture where the OS stayed up" "the criterion is met in $EVIDENCE: the goal's clause AND the arm's, in one capture"
    fi
  fi
fi

# --- 4. the image exists, and is hashed ---------------------------------------------------------
IMG_SHA=""; IMG_BYTES=""
if [[ -z $IMAGE ]]; then
  fail "the image to write exists and is hashed" "no --image given. Precondition 5 of docs/reference/recovery-and-rollback.md is that the target partition, the image's origin and the rollback are explicit"
elif [[ ! -e $IMAGE ]]; then
  fail "the image to write exists and is hashed" "no image at $IMAGE"
elif [[ ! -r $IMAGE ]]; then
  fail "the image to write exists and is hashed" "the image at $IMAGE exists and cannot be read"
elif ! IMG_SHA=$(sha256sum "$IMAGE" | awk '{print $1}') || [[ -z $IMG_SHA ]]; then
  fail "the image to write exists and is hashed" "could not hash $IMAGE"
elif ! IMG_BYTES=$(stat -c %s "$IMAGE"); then
  fail "the image to write exists and is hashed" "could not size $IMAGE"
else
  ok "the image to write exists and is hashed" "$IMAGE  sha256 $IMG_SHA  bytes $IMG_BYTES"
fi

# --- 5. the rollback exists and verifies, for THIS target --------------------------------------
STOCK=""; STOCK_SHA=""
if [[ ! -d $BACKUP ]]; then
  fail "the rollback exists and verifies" "no backup directory at $BACKUP. Rule 7 of the reference: keep a known-good recovery path available before testing anything"
elif [[ ! -r $BACKUP/SHA256SUMS.txt ]]; then
  fail "the rollback exists and verifies" "no readable SHA256SUMS.txt in $BACKUP - the backup is present and its manifest is not, so nothing about it is verified"
elif ! ( cd "$REPO_ROOT" && sha256sum -c "$BACKUP/SHA256SUMS.txt" >/dev/null 2>&1 ); then
  # The manifest uses absolute paths, so it must be verified from the directory it was written for.
  fail "the rollback exists and verifies" "the backup at $BACKUP does NOT verify against its own SHA256SUMS.txt. A rollback whose hashes have moved is not a rollback - re-take the backup before any write"
elif [[ -z $TARGET ]]; then
  skip "the rollback exists and verifies" "the backup verifies, but no --target was given, so which stock image is this target's rollback cannot be decided"
elif [[ ! -e $BACKUP/$TARGET.img ]]; then
  fail "the rollback exists and verifies" "the manifest verifies but there is no $TARGET.img in $BACKUP, so this target has no rollback image. Rule 5: do not flash an image unless the rollback path is explicit"
elif ! STOCK_SHA=$(sha256sum "$BACKUP/$TARGET.img" | awk '{print $1}') || [[ -z $STOCK_SHA ]]; then
  fail "the rollback exists and verifies" "could not hash the rollback image $BACKUP/$TARGET.img"
else
  STOCK=$BACKUP/$TARGET.img
  ok "the rollback exists and verifies" "$STOCK  sha256 $STOCK_SHA  (in the manifest, and the whole manifest verifies: CONSISTENT with it. The manifest is self-written, so what makes this rollback known-good is the date it was taken, not this line - precondition 2 says why)"
fi

# --- 6-8. the tool: it exists, it is not the payload, and it is THE recorded image --------------
#
# The three checks below are one action, and they are the ones no run on this host has ever reached.
TOOL_SHA=""; VERIFY_OUT=""
if [[ -z $BOOTLOADER_IMAGE ]]; then
  fail "the tool image is the recorded one and is booted, never flashed" "no --boot-image given. This gate clears exactly one form of the write - boot the tool, write the target from inside - and that form needs the image to boot. It will not print a bootloader flash as a fallback: that is a different action with a different blast radius, and printing it under a CLEARED verdict would be one verdict covering two actions"
elif [[ ! -e $BOOTLOADER_IMAGE ]]; then
  fail "the tool image is the recorded one and is booted, never flashed" "no boot image at $BOOTLOADER_IMAGE"
elif [[ ! -r $BOOTLOADER_IMAGE ]]; then
  fail "the tool image is the recorded one and is booted, never flashed" "the boot image at $BOOTLOADER_IMAGE exists and cannot be read"
elif [[ -n $IMAGE && -e $IMAGE && "$(readlink -f "$BOOTLOADER_IMAGE")" == "$(readlink -f "$IMAGE")" ]]; then
  # Booting a thing and then writing that same thing is almost always a mistyped flag, and the
  # failure it produces (a tool that is not a tool) is not one either image would explain.
  fail "the tool image is the recorded one and is booted, never flashed" "--boot-image and --image are the same file. One of them is the tool and the other is the payload; a gate that clears booting the payload in order to write the payload has cleared the wrong thing"
elif [[ ! -r $REPO_ROOT/tools/verify_tool_image.sh ]]; then
  fail "the tool image is the recorded one and is booted, never flashed" "no verifier at $REPO_ROOT/tools/verify_tool_image.sh, so whether $BOOTLOADER_IMAGE is the image this project recorded cannot be established - and this gate will not boot an image it cannot name"
elif ! VERIFY_OUT=$(bash "$REPO_ROOT/tools/verify_tool_image.sh" "$BOOTLOADER_IMAGE" --require-role=twrp 2>&1); then
  # It must be THE image this project recorded, not merely a file that parses as one: until 608 this
  # check was the parser alone, which is a statement about a *format* and not about a *file*.
  fail "the tool image is the recorded one and is booted, never flashed" "the tool image did not verify against the tree's own record:
$(printf '%s\n' "$VERIFY_OUT" | sed 's/^/          /')
          This is not a formatting complaint: a tool image is booted only after its origin and its
          hash are written down, and this one is not the file that record names"
else
  TOOL_SHA=$(printf '%s\n' "$VERIFY_OUT" | sed -n 's/^  ok   sha256 \([0-9a-f]*\) is a record line$/\1/p' | head -1)
  if [[ -z $TOOL_SHA ]]; then
    fail "the tool image is the recorded one and is booted, never flashed" "the verifier passed and printed no sha256, so this gate has no hash to record - and a record without the number is the thing this gate exists to avoid"
  else
    ok "the tool image is the recorded one and is booted, never flashed" "$BOOTLOADER_IMAGE  sha256 $TOOL_SHA  (booted, never flashed)"
  fi
fi

# --- the verdict, and the whole of it -----------------------------------------------------------
#
# **Every check has now run**, which is the point of the restructure: the table below is the gate's
# whole state, printed in one place, so the four header preconditions are answered together instead of
# one per run. `skipped` is a third value and not a soft `ok` (569's absent-is-not-zero).
echo
echo "== every check this gate makes, and what each one said =="
echo
_i=0
for _i in "${!CHECKS[@]}"; do
  case ${VERDICT[$_i]} in
    ok)          printf '  ok       %-52s %s\n' "${CHECKS[$_i]}" "${DETAIL[$_i]}" ;;
    REFUSED)     printf '  REFUSED  %-52s\n' "${CHECKS[$_i]}"
                 printf '%s\n' "${DETAIL[$_i]}" | sed 's/^/           /' ;;
    not-reached) printf '  skipped  %-52s %s\n' "${CHECKS[$_i]}" "${DETAIL[$_i]}" ;;
  esac
done
echo
if (( NFAIL > 0 || NSKIP > 0 )); then
  printf 'REFUSING: %d of %d check(s) refused' "$NFAIL" "${#CHECKS[@]}"
  if (( NSKIP > 0 )); then
    printf ' and %d could not be made to look at all' "$NSKIP"
  fi
  echo "."
  echo "          The ones that refused are named above with their reasons - and the counts above name"
  echo "          a category with no members when it is 0. **The ones that did NOT refuse were still"
  echo "          run**: that is what this table is for. The checks after the first refusal used to be"
  echo "          skipped, and they are the ones that only run on the day the OS has already stayed up."
  if (( NSKIP > 0 )); then
    echo "          A check that could not look is not a check that passed: the skip lines above name"
    echo "          an input this run did not have, so that precondition is unestablished and the"
    echo "          write is not cleared."
  fi
  exit 1
fi
echo "all ${#CHECKS[@]} check(s) passed, and none was skipped: $NFAIL refused, $NSKIP not reached."
echo

echo "== the write, in the form that flashes nothing =="
echo
echo "TWRP is a TOOL here, not a target: this project never flashes it. Booting it non-persistently"
echo "and writing the target from inside leaves exactly ONE persistent change instead of two - and it"
echo "is the only form this gate clears, so a \`fastboot flash $TARGET\` line is not printed anywhere"
echo "above. If you want that form instead, it is in docs/reference/recovery-and-rollback.md section 5"
echo "and it is not a form this script will put in front of you."
echo
[[ -n $BOOTLOADER_IMAGE ]] || refuse "no --boot-image given. This gate clears exactly one form of the write - boot the tool, write the target from inside - and that form needs the image to boot. It will not print a bootloader flash as a fallback: that is a different action with a different blast radius, and printing it under a CLEARED verdict would be one verdict covering two actions"
[[ -e $BOOTLOADER_IMAGE ]] || refuse "no boot image at $BOOTLOADER_IMAGE"
[[ -r $BOOTLOADER_IMAGE ]] || refuse "the boot image at $BOOTLOADER_IMAGE exists and cannot be read"
# The tool image and the payload must not be the same file. Booting a thing and then writing that
# same thing is almost always a mistyped flag, and the failure it produces (a tool that is not a
# tool) is not one either image would explain.
if [[ "$(readlink -f "$BOOTLOADER_IMAGE")" == "$(readlink -f "$IMAGE")" ]]; then
  refuse "--boot-image and --image are the same file. One of them is the tool and the other is the payload; a gate that clears booting the payload in order to write the payload has cleared the wrong thing"
fi
# And it must be THE image this project recorded, not merely a file that parses as one. Until now
# this check was the parser alone, which is a statement about a *format* and not about a *file*: any
# Android boot image would have been cleared to boot. The record at records/tool-images.txt
# names the image by hash, `tools/verify_tool_image.sh` compares the two - and corroborates it with
# TWRP's own signature - and the requirement is `role=twrp`, because the form this gate clears is the
# one that boots the recovery tool. An image recorded for any other role is refused by name, and an
# image that is not recorded at all cannot be cleared whatever it is called.
VERIFIER=$REPO_ROOT/tools/verify_tool_image.sh
[[ -r $VERIFIER ]] || refuse "no verifier at $VERIFIER, so whether $BOOTLOADER_IMAGE is the image this project recorded cannot be established - and this gate will not boot an image it cannot name"
if ! VERIFY_OUT=$(bash "$VERIFIER" "$BOOTLOADER_IMAGE" --require-role=twrp 2>&1); then
  refuse "the tool image did not verify against the tree's own record:
$(printf '%s\n' "$VERIFY_OUT" | sed 's/^/          /')
          This is not a formatting complaint: a tool image is booted only after its origin and its
          hash are written down, and this one is not the file that record names"
fi
TOOL_SHA=$(printf '%s\n' "$VERIFY_OUT" | sed -n 's/^  ok   sha256 \([0-9a-f]*\) is a record line$/\1/p' | head -1)
[[ -n $TOOL_SHA ]] || refuse "the verifier passed and printed no sha256, so this gate has no hash to record - and a record without the number is the thing this gate exists to avoid"
printf '%s\n' "$VERIFY_OUT" | grep -E '^   (name|role|source|fetched) ' | sed 's/^/       /'
echo
echo "  1. put the phone in fastboot (Vol-Down + Power), then confirm it is THERE:"
echo "       sudo fastboot devices          # must list $SERIAL"
echo "  2. boot the tool image - NOT a flash, nothing is written:"
echo "       sudo fastboot boot $BOOTLOADER_IMAGE"
echo "  3. from inside the booted tool, confirm the target is the partition you think it is:"
echo "       sudo adb -s $SERIAL shell 'cat /proc/partitions; ls -l /dev/block/by-name/$TARGET'"
echo "  4. write it, and only it:"
echo "       sudo adb -s $SERIAL push $IMAGE /tmp/write.img"
echo "       sudo adb -s $SERIAL shell 'dd if=/tmp/write.img of=/dev/block/by-name/$TARGET bs=4096'"
echo "       sudo adb -s $SERIAL shell 'sync'"
echo
echo "  5. reboot and read the result before doing anything else:"
echo "       sudo fastboot reboot   (or    sudo adb -s $SERIAL reboot   )"
echo
echo "== the rollback, if the reboot above is worse than what was there =="
echo "       sudo fastboot flash $TARGET $STOCK"
echo "       sudo fastboot reboot"
echo
echo "   Record before the write, per the reference's own checklist: current mode, the device's own"
echo "   listing, the target, this image's path and sha256, whether the image was built locally, and"
echo "   the rollback command. The two sha256 values above are that record's numbers."
echo
echo "CLEARED: every precondition verified. Nothing was written by this script - it runs no fastboot."
exit 0
