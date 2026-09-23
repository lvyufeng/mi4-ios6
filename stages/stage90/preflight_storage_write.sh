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
#     tree recorded would have been cleared. The record is `stages/stage90/tool-images.txt` (hash,
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
#
# `--boot-image` is required: it is the tool that gets BOOTED (never flashed) and the target is
# written from inside it. Its hash must be one of `stages/stage90/tool-images.txt`'s lines.
#
# Exit: 0 = every precondition verified and the commands are printed; 1 = refused, named reason.

set -uo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)

SERIAL=${SERIAL:-4a2fe00b}
BACKUP=$REPO_ROOT/xiaomi4-cancro-backup-20260604-112053
RUNNER=$STAGE_DIR/run_and_capture.sh

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

usage() {
  sed -n '2,50p' "$0" | sed 's/^# \{0,1\}//' | grep -v '^$' | head -20
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

# --- 1. the target is one of the two low-risk partitions --------------------------------------
case "$TARGET" in
  boot|recovery) : ;;
  "")  refuse "no --target given. The only targets this project allows are boot and recovery (docs/reference/recovery-and-rollback.md); the boot chain, radio and calibration partitions are refused by name there and here" ;;
  *)   refuse "target [$TARGET] is not one of the two low-risk partitions. docs/reference/recovery-and-rollback.md forbids writing sbl1, rpm, tz, DDR, ssd, dbi, aboot, modem, modemst1, modemst2, fsg, fsc and persist; a mistake in any of those may need EDL/JTAG, for which this repository documents no verified path" ;;
esac

# --- 2. the criterion is a reading of the runner, and the runner still says it -----------------
[[ -r $RUNNER ]] || refuse "cannot read the runner at $RUNNER, so the criterion below cannot be established"
grep -qF -- "$GOAL_PASS" "$RUNNER" || refuse "the goal clause [$GOAL_PASS] is not in run_and_capture.sh any more, so this gate would be reading a phrase nothing prints. Re-derive the criterion from the runner before clearing a write"
grep -qF -- "$ARM_PASS"  "$RUNNER" || refuse "the arm clause [$ARM_PASS] is not in run_and_capture.sh any more, so this gate would be reading a phrase nothing prints. Re-derive the criterion from the runner before clearing a write"

# --- 3. the evidence says the OS stayed up ------------------------------------------------------
[[ -n $EVIDENCE ]] || refuse "no --evidence given. This write is cleared only by a capture in which the machine was observed staying up; a write cleared without one is not gated at all"
[[ -e $EVIDENCE ]] || refuse "no log at $EVIDENCE. An absent file is not a capture that failed to say the criterion - it is no evidence at all (601's class: an unreadable producer is not a value)"
[[ -r $EVIDENCE ]] || refuse "the file at $EVIDENCE exists and cannot be read. Read it as root and pass the copy; an unreadable log is not a cleared criterion"
[[ -s $EVIDENCE ]] || refuse "the file at $EVIDENCE is empty. An empty log is a file that exists (596a) and it carries no reading"

EVIDENCE_OUT=$(mktemp) || refuse "mktemp failed"
trap 'rm -f "$EVIDENCE_OUT"' EXIT
bash "$RUNNER" --summarise "$EVIDENCE" > "$EVIDENCE_OUT" 2>/dev/null
sum_code=$?
[[ $sum_code -eq 0 ]] || refuse "the runner could not summarise $EVIDENCE (exit $sum_code), so the criterion is UNREAD. Do not clear a write on a log the reader refused"

grep -qF -- "$GOAL_PASS" "$EVIDENCE_OUT" \
  || refuse "the capture at $EVIDENCE does not show the goal's own clause (user mode reached and a driver answering). The OS has not been observed getting in, so 「如果os已经能进去了的话」 is unmet"
grep -qF -- "$ARM_PASS" "$EVIDENCE_OUT" \
  || refuse "the capture at $EVIDENCE shows the goal's clause but NOT the arm's - the machine was not observed staying up past the park. That is the floor 520 and 533 also meet, and they died at the idle exit's pop; a write cleared on it would be cleared by a boot that died"
echo "  ok  the criterion is met in $EVIDENCE: the goal's clause AND the arm's, in one capture"

# --- 4. the image exists, and is hashed ---------------------------------------------------------
[[ -n $IMAGE ]] || refuse "no --image given. Precondition 5 of docs/reference/recovery-and-rollback.md is that the target partition, the image's origin and the rollback are explicit"
[[ -e $IMAGE ]] || refuse "no image at $IMAGE"
[[ -r $IMAGE ]] || refuse "the image at $IMAGE exists and cannot be read"
IMG_SHA=$(sha256sum "$IMAGE" | awk '{print $1}') || refuse "could not hash $IMAGE"
IMG_BYTES=$(stat -c %s "$IMAGE") || refuse "could not size $IMAGE"
echo "  ok  image: $IMAGE"
echo "          sha256 $IMG_SHA"
echo "          bytes  $IMG_BYTES"

# --- 5. the rollback exists and verifies, for THIS target --------------------------------------
[[ -d $BACKUP ]] || refuse "no backup directory at $BACKUP. Rule 7 of the reference: keep a known-good recovery path available before testing anything"
[[ -r $BACKUP/SHA256SUMS.txt ]] || refuse "no readable SHA256SUMS.txt in $BACKUP - the backup is present and its manifest is not, so nothing about it is verified"
# The manifest uses absolute paths, so it must be verified from the directory it was written for.
if ! ( cd "$REPO_ROOT" && sha256sum -c "$BACKUP/SHA256SUMS.txt" >/dev/null 2>&1 ); then
  refuse "the backup at $BACKUP does NOT verify against its own SHA256SUMS.txt. A rollback whose hashes have moved is not a rollback - re-take the backup before any write"
fi
STOCK=$BACKUP/$TARGET.img
[[ -e $STOCK ]] || refuse "the manifest verifies but there is no $TARGET.img in $BACKUP, so this target has no rollback image. Rule 5: do not flash an image unless the rollback path is explicit"
STOCK_SHA=$(sha256sum "$STOCK" | awk '{print $1}') || refuse "could not hash the rollback image $STOCK"
echo "  ok  rollback: $STOCK"
echo "          sha256 $STOCK_SHA  (in the manifest, and the whole manifest verifies)"

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
# Android boot image would have been cleared to boot. The record at stages/stage90/tool-images.txt
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
echo "  ok  boot image (booted, never flashed): $BOOTLOADER_IMAGE"
echo "          sha256 $TOOL_SHA"
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
