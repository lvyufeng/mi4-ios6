#!/usr/bin/env bash
#
# Verify that a tool image is THE image this project recorded, before anything is told to boot it.
#
# Why this exists at all
# ----------------------
# `preflight_storage_write.sh` clears exactly one form of the persistent write - boot the tool, write
# the target from inside it - and it needs an image to boot. Until now it accepted anything that
# parsed as an Android boot image, which is a statement about a *format* and not about a *file*. This
# project's own rule is that a claim about an artifact has to be a check on the artifact, so the file
# is now named by a record and this script is what compares the two. 605 section 5 carried this as its
# first gap: "A `cancro` TWRP build has to be obtained and its provenance recorded before
# `--boot-image` means anything." This is the other half of that sentence.
#
# **A record nothing compares against is not a constraint**, which is the failure this file is written
# to avoid rather than to commit: `stages/stage90/tool-images.txt` is written by hand, and it does no
# work until something hashes the image and disagrees with it. That something is here, and it runs
# before the gate prints a single command.
#
# What is checked, in order, and what each is worth
# -------------------------------------------------
#  1. the record is readable and non-empty      - a missing record is a refusal, never a pass
#  2. the image exists, is readable, non-empty  - 601's class: an absent file is not a failed read
#  3. the sha256 is IN the record               - the constraint; a byte off and this refuses
#  4. bytes and md5 also match the record       - three independent fields agreeing is what makes a
#                                                 typo in one of them a reading and not a silence
#  5. it parses as an Android boot image        - through the tree's own reader, so the gate's
#                                                 previous check is not lost, only preceded
#  6. the detached PGP signature, when gpg and the key and the .asc are all present
#                                               - CORROBORATION, not the constraint: it is reported
#                                                 either way, and only a *bad* signature refuses.
#                                                 gpg absent prints NOT CHECKED rather than nothing,
#                                                 because a check that succeeds by printing nothing
#                                                 cannot be told from one that never ran.
#
# The signature is corroboration on purpose, and the reason is honest: the key came from a public
# keyserver and its UID is a self-claim, so what it proves is that the bytes were signed by the holder
# of key 9570 7D42 ... 43DF - and the identity half of that ("TeamWin") rests on a fingerprint the
# operator should confirm out of band. What it does not need is trust: the sha256 must equal a value
# TWRP itself publishes, served from TWRP's own host, and the signature must be good, so the two
# channels have to agree about one 16,240,640-byte file. A file that satisfies both is not a guess.
#
# Usage:
#   ./tools/verify_tool_image.sh PATH [--record=PATH] [--require-role=ROLE]
#
# Exit: 0 = every check that could run, passed; 1 = refused, with the failing check named.

set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RECORD=$ROOT/stages/stage90/tool-images.txt
TOOLDIR=$ROOT/stages/stage90/tool-images
REQUIRE_ROLE=""

IMAGE=""
for arg in "$@"; do
  case "$arg" in
    --record=*)       RECORD=${arg#--record=} ;;
    --require-role=*) REQUIRE_ROLE=${arg#--require-role=} ;;
    --help|-h)        sed -n '2,50p' "$0" | sed 's/^# \{0,1\}//' | grep -v '^$' | head -22; exit 1 ;;
    -*)               echo "REFUSING: unknown option: $arg" >&2; exit 1 ;;
    *)                IMAGE=$arg ;;
  esac
done

refuse() { echo "REFUSING: $*" >&2; exit 1; }
unread() { echo "  NOT CHECKED  $*"; }

# --- 0. the tools this script is made of, named before their output is trusted -------------------
# Every conclusion below is drawn by running one of these programs, so a missing one does not produce
# a weaker check - it produces a *wrong* one. This guard is here because that happened: a PATH built
# without `grep` made the record lookup return nothing, and the script said "is NOT in the record",
# which is a sentence about the image and not about the host. That is 595's class (a refusal naming
# the wrong fault), and the fix is to check the tools by name before any of them speaks.
MISSING=""
for t in sha256sum md5sum stat awk sed grep tr head cut mktemp; do
  command -v "$t" >/dev/null 2>&1 || MISSING="$MISSING $t"
done
[[ -z $MISSING ]] || refuse "this host is missing$MISSING, which this script needs to hash, read or compare. A check that cannot run is not a check that passed: an absent tool would make the record lookup return nothing and the refusal would blame the image"
if [[ -r $ROOT/tools/parse_android_bootimg.py ]] && ! command -v python3 >/dev/null 2>&1; then
  refuse "python3 is not on PATH, so $ROOT/tools/parse_android_bootimg.py cannot read this image's format. Install it, or run this on a host that has the tree's own readers"
fi

echo "== is this the image the tree recorded? =="
echo "   image:   ${IMAGE:-<not given>}"
echo "   record:  $RECORD"
echo

[[ -n $IMAGE ]] || refuse "no image given. Usage: verify_tool_image.sh PATH [--record=PATH] [--require-role=ROLE]"
[[ -e $IMAGE ]] || refuse "no file at $IMAGE. An absent file is not an image that failed to verify - it is no image at all"
[[ -r $IMAGE ]] || refuse "the file at $IMAGE exists and cannot be read, so it cannot be verified"
[[ -s $IMAGE ]] || refuse "the file at $IMAGE is empty"

# --- 1. the record, because every claim below is a comparison against it -------------------------
[[ -e $RECORD ]] || refuse "no record at $RECORD, so there is nothing to compare this image against - and an image accepted without one is accepted on its filename"
[[ -r $RECORD ]] || refuse "the record at $RECORD exists and cannot be read"
[[ -s $RECORD ]] || refuse "the record at $RECORD is empty, so it names no image and can clear nothing"

# --- 2. the hash, and the record line it selects --------------------------------------------------
IMG_SHA=$(sha256sum "$IMAGE" | awk '{print $1}') || refuse "could not hash $IMAGE"
# A record line is a set of space-separated key=value fields; the hash is the key into it. `grep -F`
# on the literal `sha256=<hex>` rather than on the bare hex, so a hash that happens to appear inside a
# comment or a path cannot select a line.
LINE=$(grep -E "^sha256=${IMG_SHA}([[:space:]]|\$)" "$RECORD" 2>/dev/null || true)
[[ -n $LINE ]] || refuse "$(basename "$IMAGE") hashes to $IMG_SHA, which is NOT in $RECORD. This project boots a tool image only after recording where it came from and what it hashes to; an image whose hash is not written down is an image of unknown origin"
echo "  ok   sha256 $IMG_SHA is a record line"

field() { printf '%s\n' "$LINE" | tr ' ' '\n' | sed -n "s/^$1=//p" | head -1; }
NAME=$(field name); ROLE=$(field role); BYTES=$(field bytes); MD5=$(field md5)
SOURCE=$(field source); FETCHED=$(field fetched); SIGBY=$(field signed_by); SIGON=$(field signed_on)

# --- 3. the other two fields, each checked rather than printed -----------------------------------
[[ -n $ROLE ]] || refuse "the record line for $IMG_SHA carries no role= field, so what this image is *for* is not stated anywhere - and this file will not infer it from a filename"
if [[ -n $REQUIRE_ROLE && $ROLE != "$REQUIRE_ROLE" ]]; then
  refuse "$(basename "$IMAGE") is recorded with role=$ROLE, and this caller requires role=$REQUIRE_ROLE. The persistent-write gate clears the form that boots a recovery tool; an image recorded for another role is not that tool, whatever it is called"
fi
IMG_BYTES=$(stat -c %s "$IMAGE") || refuse "could not size $IMAGE"
if [[ -n $BYTES ]]; then
  [[ $IMG_BYTES == "$BYTES" ]] || refuse "size disagrees with the record: $IMAGE is $IMG_BYTES bytes and the record says $BYTES"
  echo "  ok   bytes   $IMG_BYTES"
fi
IMG_MD5=$(md5sum "$IMAGE" | awk '{print $1}') || refuse "could not md5 $IMAGE"
if [[ -n $MD5 ]]; then
  [[ $IMG_MD5 == "$MD5" ]] || refuse "md5 disagrees with the record: $IMAGE is $IMG_MD5 and the record says $MD5 - two hashes of one file disagreeing means one of the two is not of this file"
  echo "  ok   md5     $IMG_MD5"
fi

# --- 4. it is a boot image, through the tree's own reader -----------------------------------------
PARSER=$ROOT/tools/parse_android_bootimg.py
if [[ -r $PARSER ]]; then
  PARSED=$(python3 "$PARSER" "$IMAGE" 2>&1) || refuse "$(basename "$IMAGE") does not parse as an Android boot image (tools/parse_android_bootimg.py refused it), so it is not something this project will tell you to boot"
  echo "  ok   parses as an Android boot image:"
  printf '%s\n' "$PARSED" | sed -n 's/^/         /p' | grep -E 'page_size|kernel_size|ramdisk_size|dt_size|cmdline' || true
else
  unread "$PARSER is not readable, so this image's format was not checked"
fi

# --- 5. the signature, which corroborates and cannot be the constraint ----------------------------
# Resolved from the record rather than assumed: the `.asc` sits next to the key in the stage's
# tool-images directory, named after the image it signs, and the key is named after its fingerprint -
# so a record line and a directory listing have to agree for this check to run at all.
SIG_FILE=$TOOLDIR/$(basename "${NAME:-$IMAGE}").asc
KEY_FILE=$TOOLDIR/TeamWin-${SIGBY}.asc
if [[ -n $SIGBY && -r $SIG_FILE && -r $KEY_FILE ]] && command -v gpg >/dev/null 2>&1; then
  GH=$(mktemp -d) || refuse "mktemp failed"
  chmod 700 "$GH"
  if ! gpg --homedir "$GH" --import "$KEY_FILE" >/dev/null 2>&1; then
    rm -rf "$GH"; refuse "gpg could not import $KEY_FILE, so the signature was not checked"
  fi
  if gpg --homedir "$GH" --verify "$SIG_FILE" "$IMAGE" >/dev/null 2>&1; then
    echo "  ok   PGP signature is GOOD, signed by $SIGBY${SIGON:+ on $SIGON}"
    # The trailing `[unknown]` is gpg's *trust* level for the key, not part of the UID, and printing
    # it beside the name invites reading "unknown" as a doubt about the image. The trust half is
    # stated in this file's header; the UID is what belongs on this line.
    gpg --homedir "$GH" --verify "$SIG_FILE" "$IMAGE" 2>&1 \
      | sed -n 's/^gpg: *Good signature from /         uid /p' | sed 's/ \[unknown\]$//'
  else
    rm -rf "$GH"
    refuse "the detached signature in $SIG_FILE is NOT a good signature for $(basename "$IMAGE"). The sha256 matched the record, so these two disagree about the same file, and a disagreement between a hash and a signature is not a detail: do not boot this image"
  fi
  rm -rf "$GH"
elif [[ -z $SIGBY ]]; then
  unread "the record names no signer for this image, so the signature was not checked"
elif ! command -v gpg >/dev/null 2>&1; then
  unread "gpg is not on PATH, so the signature was not checked (the hash and the record were)"
else
  unread "$SIG_FILE or $KEY_FILE is missing, so the signature was not checked (the hash and the record were)"
fi

# --- 6. and what it is, in the record's own words --------------------------------------------------
echo
echo "   name      ${NAME:-<unnamed>}"
echo "   role      $ROLE"
echo "   source    ${SOURCE:-<no source recorded>}"
echo "   fetched   ${FETCHED:-<no fetch date recorded>}"
echo
echo "VERIFIED: this is the file the record names - $(basename "$IMAGE"), $IMG_BYTES bytes, sha256 $IMG_SHA."
echo "Nothing was booted, flashed or written by this script."
exit 0
