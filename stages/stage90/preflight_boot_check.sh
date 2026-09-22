#!/usr/bin/env bash
#
# Gate a hardware run of the Stage90 payload.
#
# The 2026-09-16 PREFLIGHT_WATCHDOG_ONLY run ended with the device hung and a
# manual power-button hold needed. Knowing which switches an image was actually
# built with, and refusing the risky ones by default, is the cheapest way to stop
# that happening again - a rebuild that silently picks up a risky mode is exactly
# how an unintended hang gets booted.
#
# This script never runs fastboot and never touches the device. It verifies the
# image and prints the command to run, or refuses and says why.
#
# Usage: ./preflight_boot_check.sh [--allow-preflight] [--allow-full] [--allow-selftest] [--allow-attr-normal-nc] [--allow-attr-normal-wb] [--allow-icache] [--allow-dcache] [--allow-xnu-entry] [--allow-hw-watchdog-selftest] [--allow-fault-inject]

set -euo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
OUT=$REPO_ROOT/out/stage90

ALLOW_PREFLIGHT=0
ALLOW_FULL=0
ALLOW_SELFTEST=0
ALLOW_ATTR=0
ALLOW_ATTR_WB=0
ALLOW_ICACHE=0
ALLOW_DCACHE=0
ALLOW_XNU_ENTRY=0
ALLOW_HW_SELFTEST=0
ALLOW_FAULT_INJECT=0

for arg in "$@"; do
  case "$arg" in
    --allow-preflight)   ALLOW_PREFLIGHT=1 ;;
    --allow-full)        ALLOW_FULL=1 ;;
    --allow-selftest)    ALLOW_SELFTEST=1 ;;
    --allow-attr-normal-nc) ALLOW_ATTR=1 ;;
    --allow-attr-normal-wb) ALLOW_ATTR_WB=1 ;;
    --allow-icache)       ALLOW_ICACHE=1 ;;
    --allow-dcache)       ALLOW_DCACHE=1 ;;
    --allow-xnu-entry)    ALLOW_XNU_ENTRY=1 ;;
    --allow-hw-watchdog-selftest) ALLOW_HW_SELFTEST=1 ;;
    --allow-fault-inject) ALLOW_FAULT_INJECT=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

fail() { echo "REFUSING: $*" >&2; exit 1; }

# The tools this gate calls. PYTHON is configurable for the same reason build.sh's is -
# the host may have python3 under another name.
PYTHON=${PYTHON:-python3}

CONFIG=$OUT/stage90-build-config.txt
IMAGE=$OUT/stage90-qcdt.img

[[ -f $CONFIG ]] || fail "no $CONFIG - run ./build.sh first to record the build switches"
[[ -f $IMAGE  ]] || fail "no $IMAGE - run ./build.sh first"

echo "== build configuration =="
cat "$CONFIG"

value_of() {
  sed -n "s/^#define $1 //p" "$CONFIG"
}

MODE=$(value_of STAGE90_HANDOFF_MODE)
SELFTEST=$(value_of STAGE90_DEADMAN_SELFTEST)
DEADMAN=$(value_of STAGE90_DEADMAN_ENABLE)
LADDER=$(value_of STAGE90_ENTRY_LADDER_LEVEL)
HWWDT=$(value_of STAGE90_HW_WATCHDOG)
HWSELFTEST=$(value_of STAGE90_HW_WATCHDOG_SELFTEST)
FAULT_INJECT=$(value_of STAGE90_HANDOFF_FAULT_INJECT_VA)

[[ -n $MODE ]] || fail "STAGE90_HANDOFF_MODE missing from $CONFIG"

echo
echo "== image integrity =="
( cd "$OUT" && sha256sum -c SHA256SUMS.txt ) || fail "image does not match SHA256SUMS.txt; rebuild before booting"
echo "sha256 verified against $OUT/SHA256SUMS.txt"
# **The hash, computed here rather than written into the prose below, and 520 is why.** 519c's edit put
# the literal `40bf8a0b...` into the XNU-entry block, where it was true of the image it was written for -
# and then 520's build produced `25c4bc7d...` and the gate went on telling the operator that the image
# being checked was 519's. The manifest check above does not catch it: `40bf8a0b...` is not compared with
# anything, so a hash in a comment is a claim, which is this project's oldest rule about checks. This is
# the value the block below prints, and it is of the file about to be booted.
IMAGE_SHA=$(sha256sum "$OUT/stage90-qcdt.img" | cut -d' ' -f1)
IMAGE_SHA16=${IMAGE_SHA:0:16}

echo
echo "== image freshness =="
# The manifest check above proves the image matches SOMETHING, but not that the something
# is the current source. If a source file was edited and not rebuilt, the image and its
# manifest are both stale and agree with each other - so the gate would report the OLD
# switches and approve the OLD image, which is precisely the failure this gate exists to
# prevent, in the direction it was blind to. Verified: without this check, touching
# stage90.h and running the gate passes.
#
# `|| true` on each `find`: `find` exits non-zero on a directory it cannot read, and under `set -e` the
# assignment would then end the script right here with no message at all - the same shape as the
# `[[ ... ]] && echo` note below and as clause 2's missing-`nm` guard. A scan that could not look is not
# a scan that found something, and the direction this fails in is the quiet one.
STALE=$(find "$STAGE_DIR" -maxdepth 1 -type f \
         \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.ld' \) \
         -newer "$IMAGE" -printf '%f\n' 2>/dev/null | sort || true)
# **533 put the entry image's own sources in this sweep for one commit, and 533 removed them again -
# because an mtime is the wrong measurement for that directory and the refusal it produced was of a
# correct tree.** The entry sources are one directory down and `-maxdepth 1` never looked at them,
# which was true; but comparing their mtimes against the *payload image's* mtime asks about a file
# the payload's build never opens. `build.sh` consumes exactly one thing from `xnu_arm_boot/` -
# `out/stage90/xnu_arm_entry.bin`, byte-embedded at `build.sh:93` - and mentions `build_entry.sh`
# only in prose, so a newer build script cannot make the payload stale. Measured on the committed
# tree at 4787622, `git status` empty: `build_entry.sh` 17:53:35 against the image it had produced
# at 17:53:15, refused, and the remedy this clause prescribes - rebuild - is the one thing this
# phase cannot do, because a payload rebuild does not reproduce (408) and would cost the freeze for
# a change in nothing the payload consumes.
#
# The entry side is now checked where content can be checked instead (the clause "the entry image's
# own sources" below, which reads the manifest `build_entry.sh` writes beside the image), so this
# sweep is back to the payload's own sources - the files `build.sh` actually compiles - and the
# tools that generate what it compiles.
BUILD_TOOLS_NEWER=$(find "$REPO_ROOT/tools" -maxdepth 1 -name 'mkmacho_fixture.py' \
                    -newer "$IMAGE" -printf '%f\n' 2>/dev/null || true)
if [[ -n $STALE || -n $BUILD_TOOLS_NEWER ]]; then
  echo "source newer than the image:"
  # `[[ ... ]] && echo` as a bare statement returns 1 when the test is false, which under `set -e`
  # would leave the script before the `fail` below and print no reason at all. Explicit `if`s.
  if [[ -n $STALE ]]; then echo "$STALE" | sed 's/^/  /'; fi
  if [[ -n $BUILD_TOOLS_NEWER ]]; then echo "  tools/$BUILD_TOOLS_NEWER"; fi
  echo "  (compared against the image's own mtime: $(stat -c '%y' "$IMAGE"))"
  # **"Run ./build.sh" is the wrong remedy about half the time, and for the XNU-entry arm it is the
  # actively harmful one.** Two different events produce this line and the gate cannot tell them apart:
  # an edit that was not rebuilt, and a `git checkout` / mirror that rewrote an *unchanged* file and so
  # bumped its mtime. Measured (2026-09-22): committing 533 and fast-forwarding master left
  # xnu_arm_boot/build_entry.sh and entry_trace.c at 17:30:27 with `git diff HEAD` empty, two minutes
  # after the 17:28 image they had in fact produced - a false stale, and the safe direction, but the
  # operator's next move is a rebuild and a blind `./build.sh` drops -DSTAGE90_XNU_ENTRY back to the
  # header's default 0, i.e. it rebuilds a payload that never jumps into XNU. Hence the second half of
  # the message: a rebuild only clears this gate for the run the run was for if it carries the switches
  # that run needs.
  #
  # **The entry side of that pair is gone, and this clause is now payload-only; the false-stale
  # hazard is not.** A checkout that bumps a payload source (`stage90_main.c`, `stage90.h`, ...) still
  # reaches this line with the content unchanged, and here the prescribed rebuild is *not* free: 408
  # says the payload link does not reproduce, so the new image is a different artifact and any frozen
  # comparison with the old one is spent. That is a decision for the step that takes it - the sound
  # repair is the same one the entry side got, a content manifest over build.sh's own sources - and
  # it is deliberately not being smuggled in with this one. What is *not* being deferred is the case
  # that has actually happened: a false stale produced by a file the payload's build never reads.
  fail "the image is stale - rebuild it, then re-run this gate. If the files above are unchanged (a checkout or a master mirror bumps an unchanged file's mtime), rebuild anyway - the gate compares mtimes and cannot tell an edit from a checkout. Rebuild with the switches the run needs: an XNU-entry run is STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh (a plain ./build.sh produces a payload that never jumps into XNU)"
fi
echo "no source file is newer than the image"

echo
echo "== the entry image the payload embeds =="
# **The image carries the arm out/stage90/xnu_arm_entry.bin holds, byte for byte** - and until now
# nothing compared the two. The payload does not merely *name* the entry image, it IS it: build.sh
# generates xnu_arm_entry_blob.c from out/stage90/xnu_arm_entry.bin on every payload build and the
# linker puts those bytes in .rodata. So a rebuild of one and not the other leaves an image whose
# manifest verifies, whose build switches are the ones printed above, and whose *arm* is a different
# one from the source's - the single thing the XNU-entry prose further down describes and cannot
# check. Measured (2026-09-22, experiment 533's session): out/stage90/stage90-qcdt.img carried
# 05596cc1... (the 526 arm) at image offset 496100 while out/stage90/xnu_arm_entry.bin was
# f202f246... (the 533 arm) - the same length, 5519996 bytes, so neither the file size nor
# SHA256SUMS.txt betrays it.
#
# **That 496100 is a reading of an image that no longer exists, and the difference between it and
# today's 496148 is itself a reading.** The image measured above was built *without*
# STAGE90_XNU_ENTRY; the one this gate now passes was built with it, and the `#if` block's `bl` plus
# its argument setup is exactly 48 bytes: kernel_size 6015308 -> 6015356 and blob offset 496100 ->
# 496148, so the arm sits 48 bytes later in a payload 48 bytes longer, inside the same-sized image.
# Two consequences worth having. First, **the switch is visible in two numbers of the boot image's own
# header, with no disassembly** - a payload that never jumps has a `kernel_size` 48 bytes smaller than
# one that does, and that is a third way to tell them apart beside the call site and the config dump.
# Second, the flag-on 533 payload and 526's are the same size (6015356) and put the blob at the same
# offset (496148), which is what "533 is 526 minus the idle-cache enable" looks like when it is measured
# by layout rather than asserted. None of it is compared here - the numbers below are computed from
# these files at gate time, and the parenthetical is a date, not an expectation.
#
# Nothing here is written down: not the offset, not a hash of the entry image compared against
# prose. The arithmetic is build.sh's own, read back out of the files it produced - the boot image's
# header carries page_size (build.sh's `--pagesize 2048`) and kernel_addr (its `--kernel_offset
# 0x00008000`), and kernel_size is the size of the stage90.bin it handed to mkbootimg; the payload
# ELF's own symbol table carries where the blob and its size word sit. So four readings are compared
# and no two of them come from the same place: the blob's offset found by searching the bytes, the
# same offset derived from the symbol's address less kernel_addr, the blob's length as the linker
# recorded it, and the length the payload *compiled in* (stage90_xnu_entry_blob_size, which
# build.sh generates from `stat -c%s` of the entry image). That last one is a different reading from
# the byte comparison: a shrunken entry image would still match the blob's prefix, and only the
# payload's own size word would say so.
STAGE90_NM=${STAGE90_NM:-arm-none-eabi-nm}
ENTRY_BIN=$OUT/xnu_arm_entry.bin
PAYLOAD_BIN=$OUT/stage90.bin
PAYLOAD_ELF=$OUT/stage90.elf
[[ -f $ENTRY_BIN   ]] || fail "no $ENTRY_BIN - the payload embeds the entry image; run ./build.sh"
[[ -f $PAYLOAD_BIN ]] || fail "no $PAYLOAD_BIN - run ./build.sh"
[[ -f $PAYLOAD_ELF ]] || fail "no $PAYLOAD_ELF - run ./build.sh"
# `set -euo pipefail` is on, so a missing tool would abort the assignment below and leave the script
# *before* the `fail` - a refusal with no reason, which is the same defect as the `[[ ... ]] &&` above.
command -v "$STAGE90_NM" >/dev/null 2>&1 \
  || fail "no '$STAGE90_NM' - the entry blob's offset is read out of the payload's symbol table; install binutils-arm-none-eabi, or set STAGE90_NM"
BLOB_SYM=$("$STAGE90_NM" -S "$PAYLOAD_ELF" 2>/dev/null \
           | awk '$4 == "stage90_xnu_entry_blob" { print $1, $2 }' || true)
BLOB_SIZE_SYM=$("$STAGE90_NM" -S "$PAYLOAD_ELF" 2>/dev/null \
                | awk '$4 == "stage90_xnu_entry_blob_size" { print $1, $2 }' || true)
[[ -n $BLOB_SYM && -n $BLOB_SIZE_SYM ]] \
  || fail "$PAYLOAD_ELF has no stage90_xnu_entry_blob / _size symbol - build.sh generates both from the entry image; rebuild"
BLOB_SIZE_ADDR=$((16#${BLOB_SIZE_SYM%% *}))
BLOB_SIZE_LEN=$((16#${BLOB_SIZE_SYM##* }))
[[ $BLOB_SIZE_LEN -eq 4 ]] \
  || fail "stage90_xnu_entry_blob_size is $BLOB_SIZE_LEN bytes, not 4 - it is not the uint32_t the payload compiled in"
if ! "$PYTHON" - "$IMAGE" "$PAYLOAD_BIN" "$ENTRY_BIN" \
                $((16#${BLOB_SYM%% *})) $((16#${BLOB_SYM##* })) "$BLOB_SIZE_ADDR" <<'PY'
import struct, sys

img_p, payload_p, entry_p = sys.argv[1:4]
blob_va, blob_sym_size, blob_size_va = (int(a, 0) for a in sys.argv[4:7])
img = open(img_p, 'rb').read()
payload = open(payload_p, 'rb').read()
entry = open(entry_p, 'rb').read()

def die(*a):
    sys.stdout.flush()
    print(*a, file=sys.stderr)
    sys.exit(1)

if img[:8] != b'ANDROID!':
    die("%s has no ANDROID! magic - it is not a boot image" % img_p)
# The v0 header, in the order build.sh's mkbootimg invocation fills it in.
ksize, kaddr = struct.unpack_from('<2I', img, 8)
psize, = struct.unpack_from('<I', img, 36)
print("  header: page_size=%d kernel_size=%d kernel_addr=0x%08x" % (psize, ksize, kaddr))
if psize <= 0 or psize + ksize > len(img):
    die("%s: the header's page_size (%d) + kernel_size (%d) runs past the file's %d bytes"
        % (img_p, psize, ksize, len(img)))
kern = img[psize:psize + ksize]
if kern != payload:
    die("%s's kernel section is not %s (%d bytes in the image, %d in the file)"
        % (img_p, payload_p, len(kern), len(payload)))
print("  the image's kernel section is %s, byte for byte (%d bytes)" % (payload_p, ksize))

# The payload's own compiled-in claim about the blob, printed before anything is compared so that a
# refusal below is read beside the length the payload itself believes: it is what catches an entry
# image that shrank instead of changing, which a prefix match cannot see.
size_off = blob_size_va - kaddr
if not 0 <= size_off <= len(payload) - 4:
    die("stage90_xnu_entry_blob_size at file offset %d is outside %s (%d bytes)"
        % (size_off, payload_p, len(payload)))
compiled = struct.unpack_from('<I', payload, size_off)[0]
if compiled != len(entry):
    die("%s compiled in a %d-byte entry blob; %s is %d bytes"
        % (payload_p, compiled, entry_p, len(entry)))
print("  the payload compiled in a %d-byte entry blob, and %s is %d bytes"
      % (compiled, entry_p, len(entry)))

def locate(hay, what):
    """The blob's offset in `hay`, by content: exactly one match, and it must fit whole."""
    o = hay.find(entry)
    if o < 0:
        die("%s does not contain %s at all" % (what, entry_p))
    if hay.find(entry, o + 1) >= 0:
        die("%s contains %s more than once" % (what, entry_p))
    if len(hay) - o < len(entry):
        die("%s: only %d bytes follow the blob, of the %d it needs"
            % (what, len(hay) - o, len(entry)))
    return o

p_off = locate(payload, payload_p)
i_off = locate(kern, "the image's kernel section")
if i_off != p_off:
    die("the blob is at kernel-section offset %d in %s and %d in %s"
        % (i_off, img_p, p_off, payload_p))
print("  the blob is %d bytes at payload offset %d = image offset %d (page_size %d + %d)"
      % (len(entry), p_off, psize + i_off, psize, i_off))

# The same offset, from the linker's symbol table rather than from a search.
sym_off = blob_va - kaddr
if sym_off != p_off:
    die("the payload's symbol table puts stage90_xnu_entry_blob at file offset %d, but its bytes are at %d"
        % (sym_off, p_off))
if blob_sym_size != len(entry):
    die("the linker recorded a %d-byte stage90_xnu_entry_blob; %s is %d bytes"
        % (blob_sym_size, entry_p, len(entry)))
print("  the linker's symbol says the blob is %d bytes at %d, which agrees"
      % (blob_sym_size, sym_off))
PY
then
  fail "the image does not carry the arm $ENTRY_BIN holds, byte for byte (see above)"
fi

echo
echo "== the entry image's own switches =="
# **Which arm the entry image is, in its own words, bound to its own bytes.** The clause above proves
# the boot image carries the arm `xnu_arm_entry.bin` holds; it cannot say *which variant* that arm is,
# because the entry side had no record of its switches anywhere - `xnu_arm_entry.h` carries layout
# only, and `stage90-build-config.txt` is the payload's switches, not the entry image's. So a frozen
# `xnu_arm_entry.bin` was identified by hash and by prose, and the variant it was built as (null
# instrument or capture, the D-cache enable or not, the exit flush on or off, the separate interrupt
# stack or not) existed only in whatever command line made it - the same hole as 520's gate printing a
# hash out of a comment literal, one level down and for the other artifact.
#
# `build_entry.sh` now writes `out/xnu_arm_entry-config.txt` at every build, from the variables that
# build actually used, with the sha256 of the bin it produced. This reads it and refuses a record that
# describes a different image, so the three artifacts are chained: image <-> bin (above) and
# bin <-> record (here). Nothing is written down here: the hash compared is the hash of the file in
# front of it, computed at gate time.
ENTRY_CFG=$OUT/xnu_arm_entry-config.txt
[[ -f $ENTRY_CFG ]] \
  || fail "no $ENTRY_CFG - the entry image's switches are recorded there by build_entry.sh, and without it this gate can prove which bytes the image carries but not which arm they are; rebuild the entry image (stages/stage90/xnu_arm_boot/build_entry.sh) and then ./build.sh"
recorded_sha=$(awk -F= '$1 == "STAGE90_XNU_ENTRY_SHA256" { print $2 }' "$ENTRY_CFG")
[[ -n $recorded_sha ]] \
  || fail "$ENTRY_CFG has no STAGE90_XNU_ENTRY_SHA256 line - a record that names no artifact is a note, not a reading"
actual_sha=$(sha256sum "$ENTRY_BIN" | awk '{ print $1 }')
[[ "$recorded_sha" == "$actual_sha" ]] \
  || fail "$ENTRY_CFG describes entry image $recorded_sha and $ENTRY_BIN is $actual_sha: the record names a different artifact than the one on disk, so the switches it lists are about some other image. Rebuild the entry image, then ./build.sh"
#
# **Every key by name, because the five that *are* the variant are not named like the artifact.** The
# four keys that identify the record (SHA256, BYTES, TRACE, REAL_ARM_INIT) all begin `STAGE90_XNU_ENTRY_`
# or `STAGE90_ENTRY_`, and the five that say *which arm this is* - SLOT_NULL, EXIT_POC_FLUSH,
# IDLE_CACHE_ENABLE, ISTACK_SEPARATE, IDLE_STACK - begin `STAGE90_XNU_` and end there. So the obvious
# display filter, `$1 ~ /^STAGE90_(XNU_ENTRY|ENTRY_)/`, prints four lines, drops all five of the ones
# this clause exists to publish, and **prints no error doing it**: the gate would report success while
# saying nothing about the arm, which is the whole reason the clause was added. Measured on a record
# with the nine keys `build_entry.sh` writes: 9 in, 4 out.
#
# So the keys are named twice - once to be required, once to be printed - and a record missing one is
# refused rather than shown as a shorter list (`[[ -n $v ]]`, so an empty value is a missing key: an
# `X=` line is not `X=0`, which is [[mi4-off-option-two-spellings]] one register over).
ENTRY_CFG_KEYS=(STAGE90_XNU_ENTRY_SHA256 STAGE90_XNU_ENTRY_BYTES STAGE90_ENTRY_TRACE
                STAGE90_ENTRY_REAL_ARM_INIT STAGE90_XNU_SLOT_NULL STAGE90_XNU_EXIT_POC_FLUSH
                STAGE90_XNU_IDLE_CACHE_ENABLE STAGE90_XNU_ISTACK_SEPARATE STAGE90_XNU_IDLE_STACK)
for _k in "${ENTRY_CFG_KEYS[@]}"
do
  _v=$(awk -F= -v k="$_k" '$1 == k { print $2 }' "$ENTRY_CFG")
  [[ -n $_v ]] \
    || fail "$ENTRY_CFG has no $_k line - this gate prints the entry image's variant by name, and a record without that key would let a run go out with a switch nobody recorded. The five variant keys (SLOT_NULL, EXIT_POC_FLUSH, IDLE_CACHE_ENABLE, ISTACK_SEPARATE, IDLE_STACK) are exactly the ones a display filter written around the artifact keys drops in silence"
  printf '  %s=%s\n' "$_k" "$_v"
done
# And the converse, so a *tenth* key cannot arrive unshown: every `STAGE90_` key the record carries
# must be one of the names above. Without this the list above would be the only definition of what is
# visible, and a key added on the build side would be recorded and never read - the same defect with
# the arrow reversed.
# `LC_ALL=C` on all three, the same pin as the entry-sources comparison below: these agree with each
# other under the ambient locale only by accident, and a `comm` whose input was sorted in a different
# collation answers about the order rather than about the keys - see the note at that clause.
_unshown=$(awk -F= '!/^#/ && $1 ~ /^STAGE90_/ { print $1 }' "$ENTRY_CFG" | LC_ALL=C sort -u \
           | LC_ALL=C comm -23 - <(printf '%s\n' "${ENTRY_CFG_KEYS[@]}" | LC_ALL=C sort -u))
[[ -z $_unshown ]] \
  || fail "$ENTRY_CFG carries key(s) this gate does not print: $(printf '%s\n' "$_unshown" | tr '\n' ' ')- a switch recorded on the build side and not shown here is a switch the next run would go out with unread; add it to ENTRY_CFG_KEYS above"
echo "  (recorded in $ENTRY_CFG, bound to $actual_sha)"

echo
echo "== the entry image's own sources =="
# **Is this entry image the build of the tree in front of it - asked by content, because the question
# the gate used to ask it with was an mtime and the answer was wrong.** The entry sources used to sit
# in the freshness sweep above, compared against the *payload image's* mtime: a file the payload's
# build never opens, so committing an edit to `build_entry.sh` moved its mtime past the image the
# same file had just produced and the gate refused a tree that was byte-identical to what it was
# built from. Rebuild was the prescribed remedy and the wrong one here - 408, the payload link does
# not reproduce - so the repair is to ask the question the refusal was standing in for.
#
# `build_entry.sh` writes `out/xnu_arm_entry-sources.txt` beside the image: every regular file in
# `xnu_arm_boot/`, hashed, with the image's own sha256 on its second line. This recomputes the same
# list by the same rule and refuses on any difference, which catches three things the mtime sweep
# either missed or got backwards:
#
#   1. an entry source edited and the image not rebuilt - the case the sweep existed for, now caught
#      by content rather than by a timestamp that a `git checkout` also moves;
#   2. a source *added or removed* since the build, in either direction: a file the manifest lists and
#      the directory no longer has, and a file the directory has that the manifest never saw. The
#      mtime sweep could only see the second, and only if the new file's mtime happened to be newer;
#   3. the case the sweep produced a false refusal for - a checkout, or a commit, that rewrote an
#      unchanged file. Same bytes, no difference, no refusal, which is what "unchanged" should mean.
#
# **And it closes a blind spot the sweep had from the day it was written: `*.S` is case-sensitive
# and every assembly source in that directory is lowercase `.s`.** `entry_vectors.s`,
# `entry_ramdisk.s`, `entry_macho.s`, `entry_arm_rtabi.s` and `assym.s` are all inputs to this image
# and not one of them was ever compared with anything. That is not a property of the fix being
# weaker than the sweep; it is the fix being the first check that ever looked at those files.
#
# Bound to the artifact the same way the switches are: the manifest's `STAGE90_XNU_ENTRY_SHA256` line
# must be the hash of the entry bin on disk, so a manifest left over from an earlier build cannot be
# read as this one's - which is the one way a content check can be satisfied by the wrong content.
ENTRY_SRC_MANIFEST=$OUT/xnu_arm_entry-sources.txt
[[ -f $ENTRY_SRC_MANIFEST ]] \
  || fail "no $ENTRY_SRC_MANIFEST - build_entry.sh writes it beside the image, and without it nothing compares the entry image with the sources it claims to be built from. Rebuild the entry image (stages/stage90/xnu_arm_boot/build_entry.sh)"
manifest_sha=$(awk -F= '$1 == "STAGE90_XNU_ENTRY_SHA256" { print $2 }' "$ENTRY_SRC_MANIFEST")
[[ -n $manifest_sha ]] \
  || fail "$ENTRY_SRC_MANIFEST has no STAGE90_XNU_ENTRY_SHA256 line - a manifest bound to no artifact can be satisfied by any content"
[[ "$manifest_sha" == "$actual_sha" ]] \
  || fail "$ENTRY_SRC_MANIFEST was written for entry image $manifest_sha and $ENTRY_BIN is $actual_sha: the source list describes a different build, so it can say nothing about this one. Rebuild the entry image, then ./build.sh"
# The recorded list is `name hash` so the two sides of `comm` sort by name and a changed file appears
# once from each side - which is what lets the refusal name it rather than report a count.
#
# **`$2`, not `$3`, and the difference is every run.** The manifest is written as `hash` + **two**
# spaces + `name`, and awk's default field splitting collapses a run of whitespace into one separator,
# so that line has **two** fields: `$1` is the hash and `$2` is the name - there is no `$3`. Written
# as `$3 " " $1` this produced a leading space and the hash and **dropped the filename**, which is
# measured on the real directory rather than argued: the writer's own pipeline and this one both run
# over `stages/stage90/xnu_arm_boot/` give `comm -3` **40 lines for 20 files** - every file reported
# as both changed and added, with `comm` additionally printing `file 2 is not in sorted order`,
# because the space-prefixed lines sort differently from the `name hash` ones. The refusal would then
# name **hashes** where it promises filenames. With `$2` the same two pipelines give **0**. So the
# pattern was right - it is the one that matches the writer's two spaces - and the field was wrong,
# which is the shape this project records as a reader that recognises a shape and then consumes the
# wrong subject. Two implementations of one rule is the oldest defect here; this is the seam where
# they meet, so it is asserted by running both against the real directory rather than by eye.
ENTRY_SRC_RECORDED=$(awk '/^[0-9a-f][0-9a-f]*  / { print $2 " " $1 }' "$ENTRY_SRC_MANIFEST" | LC_ALL=C sort)
# `|| true` for the same reason as the sweeps above: a `find` that cannot read the directory exits
# non-zero, and under `set -e` the assignment would end the script with no message at all - a scan
# that could not look is not a scan that found nothing, in the direction that fails quietly.
ENTRY_SRC_NOW=$(cd "$STAGE_DIR/xnu_arm_boot" 2>/dev/null && find . -maxdepth 1 -type f -printf '%f\n' 2>/dev/null | LC_ALL=C sort \
                | while IFS= read -r _f; do
                    printf '%s %s\n' "$_f" "$(sha256sum -- "$_f" | awk '{print $1}')"
                  done || true)
# **A scan that could not look must not be reported as a tree that changed, and without this line it
# was.** The `|| true` above keeps a directory `find` cannot read from ending the script with no message
# at all - but it also makes that failure silent in a misleading direction: with `ENTRY_SRC_NOW` empty,
# *every* recorded file appears as a difference, and the clause refuses with "the entry image is not the
# build of these sources", naming all twenty of them. Measured by pointing `STAGE_DIR` at a directory
# that has no `xnu_arm_boot/` under it: the output began `  on disk:  ` with no filename at all - a line
# whose second column is empty - and then listed all twenty names under `manifest:`, which is the same
# 40-lines-for-20-files signature the `$2`-vs-`$3` field defect produces, arrived at from the other side.
# The distinction the `|| true` was written to preserve is exactly the one it loses, so it is asserted
# here rather than assumed: an empty scan is a tool that could not reach the directory, and the honest
# answer to "did the sources change" is then "nothing was read", not "all of them did".
[[ -n $ENTRY_SRC_NOW ]] \
  || fail "the scan of $STAGE_DIR/xnu_arm_boot produced no files at all, while the manifest names $(printf '%s\n' "$ENTRY_SRC_RECORDED" | grep -c . || true) - so this is a scan that could not look, not a tree in which every source changed. Check that the directory exists and is readable before reading this clause's answer as one about content"
# **`LC_ALL=C` on `comm` itself, not only on the sorts that feed it - because the two sides were
# sorted in one locale and compared in another, and `comm` says so out loud.** Run under this host's
# ambient `LANG=en_US.UTF-8` against the real `xnu_arm_boot/`, the line below printed
# `comm: file 1 is not in sorted order`, `comm: file 2 is not in sorted order` and
# `comm: input is not in sorted order`, and exited 1. Both files *are* sorted - by `LC_ALL=C`, three
# lines up - and glibc's `en_US.UTF-8` collation ignores punctuation at the primary level, so it orders
# the same twenty names differently from C: C puts `.gitignore` first and `entry.ld` fourth, the ambient
# order puts `.gitignore` **last** and `entry.ld` ninth. That is not cosmetic, because `comm` merges by
# walking two files in lockstep and comparing adjacent lines: an order the two sides do not agree on is
# the assumption the merge is built on, and the answers it can give instead are a line present in both
# files reported as a difference, or a differing line matched against the wrong counterpart. Measured
# both ways on one pair: `comm -3` -> three warnings, exit 1; `LC_ALL=C comm -3` -> exit 0, no output.
# One rule, one locale, so the clause's answer cannot depend on the environment of whoever runs the gate.
ENTRY_SRC_DIFF=$(LC_ALL=C comm -3 <(printf '%s\n' "$ENTRY_SRC_RECORDED") <(printf '%s\n' "$ENTRY_SRC_NOW") | LC_ALL=C sort || true)
if [[ -n $ENTRY_SRC_DIFF ]]; then
  ENTRY_SRC_NAMES=$(printf '%s\n' "$ENTRY_SRC_DIFF" | awk 'NF { print $1 }' | LC_ALL=C sort -u | tr '\n' ' ')
  echo "files that differ from the manifest $ENTRY_SRC_MANIFEST was written with:"
  # `s/^/` PREFIXES and `s/^[^ \t]/` REPLACES - and the difference is a character of the filename.
  # Written with the second form, the "changed" line below printed `ntry_arm_rtabi.s`, one letter
  # short, because `sed` was substituting the label *for* the first character instead of putting it in
  # front of the line. Measured on a manifest with one hash altered. The labels are also now the two
  # sides of `comm` rather than a claim about what happened: column 2 is what is on disk, column 1 is
  # what the manifest recorded, and a file whose content changed appears **once in each** - so
  # "added"/"changed" was a guess at an intent the comparison does not compute, while the summary
  # above it already names each file once. `comm -3` prints the second column with a leading TAB.
  #
  # **`t` is load-bearing and its absence is visible in the output.** Two `s` commands in one `sed`
  # both run over the line: without `t`, the relabelled `on disk` line is fed straight into the second
  # substitution and prints as `  manifest:   on disk:  <name>`, carrying *both* labels - measured on
  # the same altered-hash manifest, and it reads as a claim about both sides at once. `t` branches to
  # the end on a successful substitution, so the two labels are mutually exclusive: TAB -> `on disk`,
  # no TAB -> `manifest`.
  printf '%s\n' "$ENTRY_SRC_DIFF" | sed 's/^\t/  on disk:  /; t; s/^/  manifest: /' | awk 'NF'
  echo "  (manifest bound to entry image $manifest_sha, $ENTRY_BIN's own hash)"
  fail "the entry image is not the build of these sources: $ENTRY_SRC_NAMES. Rebuild the entry image with the switches this arm needs (stages/stage90/xnu_arm_boot/build_entry.sh) and then ./build.sh - the payload embeds the rebuilt bin, and the gate's blob clause refuses the image until it does. If those files are unchanged in content, the manifest is stale rather than the tree: rebuild the entry image, which is reproducible byte for byte and does not disturb the payload"
fi
echo "the entry image is the build of xnu_arm_boot/ as it stands: $(printf '%s\n' "$ENTRY_SRC_NOW" | grep -c . || true) file(s), every one matching the manifest, and none the manifest does not name"

echo
echo "== storage tripwire =="
# Two independent checks, because they catch different things and only one of them is
# sufficient. Symbols catch a NAMED storage reference. Addresses catch an unnamed one - a raw
# store to a controller register - which is the plausible case here, since the payload
# already writes raw literals to PS_HOLD and IMEM.
#
# Verified by negative test: with a deliberate `*(volatile uint32_t *)0xf9824000u = 1;`
# added to the payload, the symbol check below found NOTHING and would have approved the
# run. The address check caught it. Both are kept because the symbol check is cheap and
# catches a different shape.
if arm-none-eabi-nm -a "$OUT/stage90.elf" 2>/dev/null \
     | grep -iE 'sdcc|emmc|\bmmc\b|ufs|partition|flash_|nand' ; then
  fail "payload references storage symbols (see above)"
fi
echo "no storage symbols in the payload"

if [[ -x $REPO_ROOT/tools/check_storage_refs.py ]] || [[ -f $REPO_ROOT/tools/check_storage_refs.py ]]; then
  "$PYTHON" "$REPO_ROOT/tools/check_storage_refs.py" "$OUT/stage90.elf" \
    || fail "payload addresses a storage controller (see above)"
else
  fail "tools/check_storage_refs.py missing - the address half of the storage tripwire cannot run"
fi

echo
echo "== recovery net =="
# Two independent nets. The hardware watchdog is the one that matters, because it does
# not depend on the GIC, the timer, IRQ delivery or IRQs being unmasked - any of which
# may be exactly what broke in a given hang.
case "$HWWDT" in
  STAGE90_HW_WATCHDOG_ARMED|1|1u)
    echo "hardware watchdog: ARMED. The SoC resets itself if the payload stops making"
    echo "                   progress, whatever the CPU is doing - and platform_reboot()"
    echo "                   forces a bite so the reboot does not depend on PS_HOLD."
    ;;
  STAGE90_HW_WATCHDOG_DISABLED|0|0u)
    echo "WARNING: STAGE90_HW_WATCHDOG=disabled - there is NO hardware reset net."
    echo "         A hang that the software dead-man cannot see will need a manual"
    echo "         power-button hold."
    ;;
  *)
    fail "unrecognised STAGE90_HW_WATCHDOG: $HWWDT"
    ;;
esac

if [[ $DEADMAN != "1u" ]]; then
  echo "WARNING: STAGE90_DEADMAN_ENABLE=$DEADMAN - the software dead-man net is off."
else
  echo "software dead-man: armed (60s), as a second net."
fi

case "$HWSELFTEST" in 1|1u)
  [[ $ALLOW_HW_SELFTEST -eq 1 ]] || fail "the hardware-watchdog SELFTEST skips the normal boot path and spins until a net reboots it; needs --allow-hw-watchdog-selftest"
  echo "HW WATCHDOG SELFTEST: allowed. Three outcomes, and the time it takes is the result:"
  echo "          ~33s  the hardware watchdog fired - it works"
  echo "          ~90s  it did NOT, but the bounded spin's PS_HOLD reset brought the"
  echo "                device back - the watchdog needs investigating, PS_HOLD is fine"
  echo "          never both failed. Note this third case is reachable: platform_reboot()"
  echo "                falls back to the same watchdog, so a dead watchdog plus a PS_HOLD"
  echo "                that does not land means a manual power press. That is a real"
  echo "                finding, not a lost run, but it is the one outcome with no log."
  ;;
esac

echo
echo "== mode policy =="
# A -D override records the numeric value rather than the symbolic name, so accept
# either form. Reading the build's own config is only worth anything if the gate can
# actually recognise what it finds there.
case "$MODE" in
  STAGE90_HANDOFF_MODE_HARD_SKIP|0|0u)
    echo "HARD_SKIP: stops before the candidate L1, the watchdog loop and the jump."
    ;;
  STAGE90_HANDOFF_MODE_PREFLIGHT_WATCHDOG_ONLY|1|1u)
    [[ $ALLOW_PREFLIGHT -eq 1 ]] || fail "PREFLIGHT_WATCHDOG_ONLY is not allowed without --allow-preflight"
    echo "PREFLIGHT_WATCHDOG_ONLY: allowed."
    ;;
  STAGE90_HANDOFF_MODE_FULL|2|2u)
    [[ $ALLOW_FULL -eq 1 ]] || fail "FULL is not allowed without --allow-full"
    echo "FULL: allowed. This installs the candidate L1 and jumps to the high-VA target."
    ;;
  *)
    fail "unrecognised STAGE90_HANDOFF_MODE: $MODE"
    ;;
esac

if [[ $SELFTEST == "1u" ]]; then
  [[ $ALLOW_SELFTEST -eq 1 ]] || fail "dead-man SELFTEST hangs the payload on purpose; needs --allow-selftest"
  echo "SELFTEST: allowed. The payload will NOT reach platform_reboot();"
  echo "          the dead-man is the only route back to Android (~60s)."
fi

echo
echo "== ladder =="
# The ladder level decides how much of the payload runs, so it decides how much a green
# run tells you. Printing the raw macro said nothing useful: a level-0 build under HARD_SKIP
# exercises the device tree, the watchdog arm and kernel_entry's early checks and then
# returns - skipping the whole arm_init ladder - and the gate presented that identically to
# a FULL run. Spelling out what each level reaches makes the run's value visible before it
# is booted rather than after.
case "$LADDER" in
  STAGE90_ENTRY_LADDER_FULL|4|4u)
    echo "FULL (4): boot-args, early pmap, PE_init_platform, post-PE bootstrap, arm_vm_init,"
    echo "          the high-VA windows and the loader. The only level that reaches the handoff."
    ;;
  STAGE90_ENTRY_LADDER_POST_PE_BOOTSTRAP|3|3u)
    echo "POST_PE_BOOTSTRAP (3): stops before arm_vm_init - no candidate L1, no high-VA"
    echo "          windows, no loader, no handoff. Tests boot-args through post-PE only."
    ;;
  STAGE90_ENTRY_LADDER_PE_INIT_PLATFORM|2|2u)
    echo "PE_INIT_PLATFORM (2): stops before the post-PE bootstrap. Tests boot-args, early"
    echo "          pmap and PE_init_platform only."
    ;;
  STAGE90_ENTRY_LADDER_EARLY_PMAP|1|1u)
    echo "EARLY_PMAP (1): stops after the early pmap step. A thin test - boot-args plus one"
    echo "          stage of the ladder."
    ;;
  STAGE90_ENTRY_LADDER_BOOT_ARGS_ONLY|0|0u)
    echo "BOOT_ARGS_ONLY (0): the stub returns after validating boot-args. This run exercises"
    echo "          almost none of the ladder - useful only for isolating an early failure."
    ;;
  *)
    fail "unrecognised STAGE90_ENTRY_LADDER_LEVEL: $LADDER"
    ;;
esac

echo
echo "== fault injection =="
case "$FAULT_INJECT" in
  0|0u|"")
    echo "off: the handoff targets the Stage-owned high-VA function as usual."
    ;;
  *)
    [[ $ALLOW_FAULT_INJECT -eq 1 ]] || fail "this build jumps at an intentionally unmapped VA ($FAULT_INJECT); needs --allow-fault-inject"
    echo "FAULT INJECTION: this build will jump at $FAULT_INJECT, which is expected to be"
    echo "          unmapped, and the abort path should log the fault and reboot."
    echo "          Expected evidence: an 'exception pabort ... lr=$FAULT_INJECT' line."
    echo "          A silent hang or a boot loop instead means the address IS mapped -"
    echo "          the failure mode this mode exists to avoid."
    ;;
esac

echo
echo "== entering XNU =="
case "$(value_of STAGE90_XNU_ENTRY)" in
  0|0u|"")
    # **The flag and the image can disagree, and until now nothing compared them.** Every step in
    # this line builds the payload with STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' — e.g.
    # docs/experiments/experiment-491-the-bit-registration-sets.md, step 4 — and a payload built
    # without it never jumps: `xnu_kernel.c:234`'s call site is behind `#if STAGE90_XNU_ENTRY`, so the
    # linked image has no branch to `stage90_xnu_entry_run` at all and the run ends in the payload's
    # own ladder and a reboot. A `--allow-xnu-entry` run spent against one therefore produces no XNU
    # log, and the run is paid for with a power press. This is the 533 shape one level out: the gate
    # described the arm the image carries and could not compare it, and here it describes *whether
    # the run reaches XNU at all* in prose and then lets the run through. Measured (2026-09-22,
    # 17:28): out/stage90 was rebuilt without the flag, this gate printed "off" and exited 0, and
    # `arm-none-eabi-objdump` on that payload has no reference to `stage90_xnu_entry_run` outside its
    # own body — so the two readings agree and the prose was the only place the mismatch appeared.
    # Only the flag makes it a refusal: entry off is a legitimate configuration, and every stage
    # before 241 ran in it.
    [[ $ALLOW_XNU_ENTRY -eq 0 ]] \
      || fail "--allow-xnu-entry was passed, but this image was built with STAGE90_XNU_ENTRY off: it never jumps into XNU, so it cannot produce the log the flag is passed for. Rebuild the payload with STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' (see docs/experiments/experiment-491-the-bit-registration-sets.md step 4), or drop the flag if a ladder run is what was meant"
    echo "off: the payload runs its own ladder and reboots, as in every stage so far."
    echo "          (--allow-xnu-entry was NOT passed, so this image is gated as a ladder run.)"
    ;;
  *)
    [[ $ALLOW_XNU_ENTRY -eq 1 ]] || fail "this build jumps into XNU's _start and never returns; needs --allow-xnu-entry"
    echo "ENTERING XNU: the payload copies a linked image containing XNU's real osfmk/arm/start.s"
    echo "          to PA 0x80000000 (experiment 241's base; 0x00200000 before it), hands it a"
    echo "          boot_args (physBase == virtBase), and jumps."
    echo "          Nothing after that jump is the payload's: the page tables, vectors, caches"
    echo "          and MMU state are XNU's, and the payload never runs again."
    echo
    echo "          Endings, and the log says which:"
    echo "            '...real XNU entry: _start ran to completion and branched to arm_init'"
    echo "                 XNU's own entry sequence ran on this device, end to end."
    echo "            '...real XNU entry: exception: <which>'"
    echo "                 it faulted, and the vector names itself."
    echo "            neither, ending at 'jumping to XNU's _start'"
    echo "                 it hung; the hardware watchdog brings the phone back in ~28s."
    echo "            no log at all, and the device does not come back"
    echo "                 the net below did not recover it and the ram_console died with the"
    echo "                 cold boot; the phone needs a power-button press and the run is lost."
    echo "                 This is not hypothetical and it is not rare enough to ignore: it is"
    echo "                 experiment 517's first run (2026-09-21). One observation, and the"
    echo "                 cause is not known - the run had two changes in it, so the next image"
    echo "                 carries one (see STAGE90_XNU_EXIT_POC_FLUSH in build_entry.sh)."
    echo "                 A candidate mechanism exists and is now measured rather than"
    echo "                 reasoned (experiments 517/518): the interrupt handler's stack starts"
    echo "                 at cpu_data->istackptr, which is also where the idle loop runs, so"
    echo "                 the handler's 5th and 6th pushed words land on the return address"
    echo "                 the idle exit is about to pop - and 518's run shows it popping a"
    echo "                 timebase value into the PC. 518's arm (move istackptr to the middle"
    echo "                 of the interrupt stack) RAN on 2026-09-22 and could not test it:"
    echo "                 istackptr is read by the vector AND by the context switch that puts"
    echo "                 the idle thread on that stack, so the arm moved both and the gap is"
    echo "                 invariant (SS_SP == istackptr - 16 in 516 and in 518 alike). The"
    echo "                 same panic returned, with the device recovering on its own. The arm"
    echo "                 cannot be another value of that field, so 519 changes where the idle"
    echo "                 thread's stack comes from instead: Idle_context is the one place that"
    echo "                 choice is made, and the arm rewrites its ldr sp from"
    echo "                 cpu_data->istackptr into a load of this image's own 16 KB array - so"
    echo "                 the handler keeps the whole interrupt stack and the idle body runs"
    echo "                 outside [intstack_top - 16384, intstack_top), which is exactly what"
    echo "                 ml_at_interrupt_context() tests. (518 section 5 proposed"
    echo "                 __wrap_Idle_load_context; that hook restores sp from the thread's pcb,"
    echo "                 so it cannot choose the idle body's stack - experiment 519 section 1.)"
    echo "                 **519 RAN on 2026-09-22 and obtained both of its predictions.**"
    echo "                 The arm worked: xnu_live_idlestack_sp = 0x8054fea8 is inside"
    echo "                 [stage90_idle_stack, +16384) with xnu_live_idlestack_inwin = 0 and"
    echo "                 _calls = 1, so the idle body is out of the window the kernel's"
    echo "                 predicate tests - and the live chain agrees with the panic's own sp"
    echo "                 from two sides (array top 0x8054fee0, cpu_idle's 'sub sp, sp, #8',"
    echo "                 the exit wrapper's 8-byte frame => the exit's entry sp is"
    echo "                 0x8054fed0). And the run ends in the *diagnosed* panic, not"
    echo "                 Apple's flat one: 'sleh_abort: prefetch abort in kernel mode:"
    echo "                 fault_addr=0x7152a6c' with frame_ok = 1, fsr_frame = 5,"
    echo "                 sp 0x8054fed0, lr 0x800462dc, pc = far = 0x07152a6c - i.e."
    echo "                 ml_at_interrupt_context() answered false. The panel is the idle"
    echo "                 path's own registers one instruction before the pop (r0/r1 are"
    echo "                 the exit's own str, r5 and r4 are cpu_idle's, r4 = the value of"
    echo "                 cpu_data->rtcPop), so the two words the pop read are two counter"
    echo "                 readings one tick apart, the later one the deadline the idle loop"
    echo "                 held. 519 section 10 removes 518's mechanism by arithmetic:"
    echo "                 EXC_CTX_SIZE is 360 (80 + 264 + 16, genassym.c:188) and the VFP"
    echo "                 area never ends above the interrupted sp, so no exception frame"
    echo "                 writes above the sp it interrupted - the writer has to be"
    echo "                 WATCHED, not reasoned about. **520 RAN on 2026-09-22 and watched it.**"
    echo "                 That arm was 519's switch set unchanged with seven live-channel keys"
    echo "                 added per site and no state change anywhere. The death reproduced at"
    echo "                 the same instruction - xnu_live_sleh_storm = 9, sp = 0x8054fed0,"
    echo "                 lr = 0x800462dc, numerically identical to 519's - and the writer is"
    echo "                 now a number rather than a candidate: xnu_live_slot_rtcpre_pop ="
    echo "                 0x04b79075 is cpu_data->rtcPop read immediately before the fatal"
    echo "                 call, and the fault's pc = 0x04b79074 is rtcPop - 1, the Thumb"
    echo "                 relationship 519 read off its own r4/far pair. lr = 0x800462dc is"
    echo "                 the return address of platform_cache_idle_exit's bl FlushPoU_Dcache"
    echo "                 (0x800462d8) and its bcc at 0x80046300 is taken, so the faulting"
    echo "                 instruction is that function's own pop {fp, pc} at 0x8004633c - a"
    echo "                 pop, which is why lr still names the flush. The deadline reaches"
    echo "                 that word only via strd r4, [sp, #-12]! in"
    echo "                 __wrap_platform_cache_idle_enter (0x8047c8d4) and"
    echo "                 __wrap_cpu_idle_wfi (0x8047c888): pre-indexed 12 from the same X"
    echo "                 the exit wrapper's 8-byte frame lands on, so r4 goes to E-4, the"
    echo "                 slot's UPPER word. The exit's own push writes it first and still"
    echo "                 loses, because the window is open: caches.c:406 clears SCTLR.C,"
    echo "                 so the push reaches DRAM and touches neither the valid L1 line nor"
    echo "                 the L2 - 516's enter-side CleanPoC_Dcache is DCCSW (c7,c10,2),"
    echo "                 which CLEANS and leaves the line VALID - then caches.c:490 sets"
    echo "                 SCTLR.C again and the pop's load MISSES nothing, it hits. Apple's"
    echo "                 FlushPoU_Dcache is DCCISW, the right operation, but L1 only."
    echo "                 The repair is FlushPoC_Dcache (DCCISW over L1 AND L2), which is"
    echo "                 exactly the call 517 added at this site behind"
    echo "                 STAGE90_XNU_EXIT_POC_FLUSH and which has never been run on its own."
    echo "                 **So the arm to run is the exit-side PoC flush, switch on, with 520's"
    echo "                 instrument repaired per its section 11** - the two slot loads moved"
    echo "                 into the wrapper's inline asm (they were read through"
    echo "                 entry_slot_note's own frame: pre_m4 = 0x8047c974 is that note's own"
    echo "                 saved lr), the abort site publishing every abort instead of a"
    echo "                 geometric subsequence (the fatal seq 9 was neither <= 4 nor a power"
    echo "                 of two and its SS_SP 0x8054fed0 was INSIDE the guard - the one"
    echo "                 reading the arm existed for was sampled away), and the window"
    echo "                 widened to the kernel map's own bounds with the rtc note's inner"
    echo "                 refusals counted. The prediction is the panic's ABSENCE, not"
    echo "                 a further reading: the push then writes the only copy there is and"
    echo "                 the pop reads DRAM. Reading 4 answered its own question unused:"
    echo "                 ml_get_timebase was called >= 1024 times, 517's instrument"
    echo "                 published no xnu_live_tb_* record at all (its early return on"
    echo "                 SCTLR.C set), and its own pair spans exactly 24 ticks - so it is"
    echo "                 not the source of a 1-tick pair."
    echo "                 **That arm ran (521) and the device did not come back.** One"
    echo "                 non-persistent fastboot boot, 'Booting OKAY', and then no adb, no"
    echo "                 fastboot, no getvar - a power press, and the log lost with the cold"
    echo "                 boot. The second such non-return in the project and the second"
    echo "                 flag-on image, which is what leaves the flush as the only remaining"
    echo "                 difference from the flag-off runs that all come back: a full L1 AND"
    echo "                 L2 clean-and-invalidate inside the idle window, with SCTLR.C clear,"
    echo "                 is the one operation this hardware has never survived - 517's first"
    echo "                 run carried the same call and also needed a power press."
    echo "                 **So the arm to run is 522: turn the D-cache back ON at the window's"
    echo "                 near end and take the flush out of the picture.**"
    echo "                 entry_idle_cache_enable does the one thing Apple's own clear does in"
    echo "                 reverse - read SCTLR, orr #4, write it back, dsb, isb - and"
    echo "                 __wrap_platform_cache_idle_enter calls it the moment the real"
    echo "                 platform_cache_idle_enter returns (0x8047c924), before the WFI. That"
    echo "                 is a coherence property rather than a maintenance operation: the"
    echo "                 WFI, the exit's push {fp, lr} and the pop {fp, pc} that 519 and 520"
    echo "                 died in all run with the cache on, so a store updates the line a"
    echo "                 later load reads instead of landing in DRAM behind a line nothing"
    echo "                 had invalidated. The exit-side flush is OFF in this image, so the"
    echo "                 one change from 521 is the cache enable - and a run that does not"
    echo "                 come back after this is a datum rather than a failure to fix: the"
    echo "                 next arm is a null instrument (the same wrapper with the readings"
    echo "                 replaced by a counter) to separate the cost of the measurements"
    echo "                 from the cost of the state change."
    echo "                 Four things to read, and the first is the verdict: (1) no"
    echo "                 'sleh_abort' panic and no xnu_live_sleh_storm growth past 520's"
    echo "                 9 - the idle exit retires its own epilogue; (2)"
    echo "                 xnu_live_slot_cwe_win shows C clear and xnu_live_slot_cwe_set shows"
    echo "                 it set, with _calls >= 1 - the enable ran inside the window and"
    echo "                 really took; (3) xnu_live_slot_post_calls >= 1, which says the exit"
    echo "                 RETURNED through the wrapper (520's was 0, its pass dying inside the"
    echo "                 call); (4) any later ending than 520's is progress - a different pc"
    echo "                 or the boot moving on - and the same death at the same pc says the"
    echo "                 mechanism is not the cache state but something the window itself"
    echo "                 does."
    echo "                 **The 522 arm ran on 2026-09-22 and the device did not come back** (section 3.3"
    echo "                 of experiment 522 has the USB timeline: dev 88 = 18d1:d00d at 14:14:45, gone at"
    echo "                 14:14:46, nothing after), so the prediction above is spent and the four readings"
    echo "                 it named have no values - there was no log. What the non-return did produce is a"
    echo "                 ledger, and it is the reason the arm to run now is different in kind rather than"
    echo "                 in value: 521 and 522 both carry 521's repair of the exit wrapper's capture (the"
    echo "                 four words of the idle exit's {fp, lr} slot read by the caller at each of two"
    echo "                 sites, 8 loads and 8 stores a pass) and BOTH failed to come back, while 520 -"
    echo "                 which carries neither that capture nor any state change - came back; 521 and 522"
    echo "                 differ only in which state change they carry, the exit-side FlushPoC_Dcache and"
    echo "                 the enter-side SCTLR.C re-enable, so the capture is present in every image that"
    echo "                 did not return and absent from every one that did."
    echo "                 **So the arm to run is 526: the null instrument.** Its image is 522's with the"
    echo "                 capture replaced by a counter - STAGE90_XNU_SLOT_NULL=1, which takes the four"
    echo "                 loads and the four pend_* stores out of each capture site and leaves"
    echo "                 entry_slot_null_note publishing the pass count alone; the exit-side flush stays"
    echo "                 at 0 and everything else in the wrapper (the 8-byte frame, the single"
    echo "                 mov r?, sp, 516's CleanPoC_Dcache, 519's rtcPop reading, the one call to the"
    echo "                 real exit, and 522's re-enable in the enter wrapper) is unchanged. That was"
    echo "                 checked rather than intended: the same tree built with STAGE90_XNU_SLOT_NULL=0"
    echo "                 reproduces 522's image 13d771938336fe65... byte for byte, so the two images"
    echo "                 differ by the capture and by nothing else."
    echo "                 What to read, in the order that matters: (1) **does the device come back** - that"
    echo "                 is the whole experiment, and a return says the readings cost something (and the"
    echo "                 next arm bisects them: the two loads before the call, or the two after it) while a"
    echo "                 third non-return says the cost is the state change 521 and 522 do not share; (2) if"
    echo "                 it returns, xnu_live_slot_pre_calls and xnu_live_slot_post_calls are present and"
    echo "                 rising with the four words ABSENT - the site ran and published a count and nothing"
    echo "                 else, which is what a null run should look like, and their absence is the other"
    echo "                 reading (the site never ran); (3) the enable is untouched, so 522's block in"
    echo "                 run_and_capture.sh --summarise reads this log too - xnu_live_slot_cwe_win shows C"
    echo "                 clear, _set shows it set with _calls >= 1; (4) the ending: the same sleh_storm 9 at"
    echo "                 the same pc as 520 would say the mechanism is neither the capture nor the re-enable,"
    echo "                 and any later ending is progress."
    echo "                 The image this preflight is being run against is"
    echo "                 ${IMAGE_SHA16}... , computed from the file itself two screens up."
    echo
    echo "          Safety: the hardware watchdog is the only net across the jump, deliberately"
    echo "          - the software dead-man needs the payload's GIC and vector state, which are"
    echo "          gone the moment _start switches tables. The watchdog needs neither."
    echo "          Its record, read from the ledgers rather than from this text: it recovered"
    echo "          every run from 506 to 515, including runs parked in the kernel's own idle"
    echo "          WFI and runs in an abort storm, and 516's two runs came back on XNU's own"
    echo "          'MACH Reboot'. It then did not recover 517's first run. So: a net that has"
    echo "          held many times and is not proved to hold always. A hang here may need a"
    echo "          power press, and the device is never at risk of being bricked - nothing in"
    echo "          this project is ever written to storage."
    ;;
esac

echo
echo "== later-phase probes =="
# These two run inside kernel_entry and are non-fatal by construction: each reports and the boot
# continues, because they are the next phase's work rather than a precondition for this run. They
# are listed rather than ignored so that "the run passed" and "the shim passed" cannot be
# confused - the payload says so in the log too. Neither changes any mapping or boot decision, so
# there is nothing to allow; if either ever does, it needs a flag of its own.
case "$(value_of STAGE90_XNU_BOOT_ARGS)" in
  0|0u|"") echo "conforming boot_args (Phase 2): off - only the ladder's own identity-based args exist." ;;
  *)       echo "conforming boot_args (Phase 2): ON, as a second object alongside the ladder's. It"
           echo "          is built and its invariants checked against the real __stage90_image_end."
           echo "          Non-fatal: read xnu_ba_checks/xnu_ba_failures in the log for its verdict." ;;
esac
case "$(value_of STAGE90_XNU_MSM8974_SHIM)" in
  0|0u|"") echo "MSM8974 platform shim (Phase 3): off." ;;
  *)       echo "MSM8974 platform shim (Phase 3): ON. Registers its tbd_ops and checks the EOI"
           echo "          pairing, the measured CNTP interrupt number and the validated CNTFRQ."
           echo "          Non-fatal: read msm8974_shim_failures in the log for its verdict." ;;
esac

case "$(value_of STAGE90_XNU_MSM8974_FIQ_PROBE)" in
  0|0u|"") echo "FIQ availability probe: off." ;;
  *)       echo "FIQ availability probe: ON. Unmasks CPSR.F with the timer armed and a bounded"
           echo "          spin, to measure whether non-secure PL1 can take an FIQ on this SoC."
           echo "          If a FIQ IS delivered the vector logs 'exception: fiq' and reboots,"
           echo "          which is the expected successful outcome, not a hang." ;;
esac

echo
echo "== mapping attributes =="
case "$(value_of STAGE90_PMAP_ATTR_MODE)" in
  STAGE90_PMAP_ATTR_MODE_SO_ONLY|0|0u)
    echo "SO_ONLY: every mapping Strongly-ordered, as in every stage so far."
    ;;
  STAGE90_PMAP_ATTR_MODE_NORMAL_NC|1|1u)
    [[ $ALLOW_ATTR -eq 1 ]] || fail "NORMAL_NC changes DRAM memory types and is not allowed without --allow-attr-normal-nc"
    echo "NORMAL_NC: DRAM is Normal/Non-cacheable, MMIO stays Strongly-ordered."
    echo "           This changes the memory model of the whole payload, so run it"
    echo "           on its own and read the log."
    ;;
  STAGE90_PMAP_ATTR_MODE_NORMAL_WB|2|2u)
    [[ $ALLOW_ATTR_WB -eq 1 ]] || fail "NORMAL_WB marks DRAM cacheable and is not allowed without --allow-attr-normal-wb"
    echo "NORMAL_WB: DRAM is Normal/Write-Back/Write-Allocate and SHAREABLE, MMIO stays"
    echo "           Strongly-ordered. The section descriptor also drops PL0 access"
    echo "           (AP 11 -> 01), which is F-AM2 and safe because everything in the"
    echo "           payload runs at PL1. Cacheable descriptors do nothing on their own:"
    echo "           the cache switches below decide whether they are used."
    ;;
  *)
    fail "unrecognised STAGE90_PMAP_ATTR_MODE: $(value_of STAGE90_PMAP_ATTR_MODE)"
    ;;
esac

echo
echo "== caches =="
case "$(value_of STAGE90_CACHE_MODE)" in
  STAGE90_CACHE_MODE_NONE|0|0u)
    echo "none: SCTLR.C and SCTLR.I both clear, as in every stage so far."
    echo "      Cacheable DRAM descriptors, if the mode above sets them, are then"
    echo "      treated as non-cacheable by the hardware."
    ;;
  STAGE90_CACHE_MODE_ICACHE|1|1u)
    [[ $ALLOW_ICACHE -eq 1 ]] || fail "the I-cache is enabled and that is not allowed without --allow-icache"
    echo "I-cache: SCTLR.I set at MMU enable time, after an ICIALLU. This is XNU's own"
    echo "         order - its start.s enables the I-cache in its first instructions."
    echo "         It is safe here only because nothing in this payload writes code at"
    echo "         runtime; the Mach-O loader is the path that eventually would, and it"
    echo "         currently refuses to copy at all. What to look for in the log: the"
    echo "         same kernel_entry ok as the default build, and on a real regression a"
    echo "         prefetch abort with a plausible pc rather than a silent difference."
    ;;
  STAGE90_CACHE_MODE_ICACHE_DCACHE|2|2u)
    [[ $ALLOW_DCACHE -eq 1 ]] || fail "the D-cache is enabled and that is not allowed without --allow-dcache"
    echo "I-cache + D-cache: SCTLR.I and SCTLR.C, the D-cache after the MMU is already on"
    echo "         and after a whole-cache clean-and-invalidate. This is the full Phase 1"
    echo "         configuration. It is the first run where the payload is not"
    echo "         immediately-visible memory, so the things to read in the log are the"
    echo "         ones that depend on cache maintenance being right:"
    echo "           - ram_console must still log, and the whole log must be there (it is"
    echo "             mapped non-cacheable even in NORMAL_WB, so it should not need any"
    echo "             maintenance - a truncated or stale log would mean that failed);"
    echo "           - the candidate-L1 window and the TTBR0 roundtrip must still verify"
    echo "             their translations, which is what the table cleans are for;"
    echo "           - the timer IRQ must still be delivered and the device must come back"
    echo "             on its own."
    echo "         A device that hangs here costs a power press, and the two nets are both"
    echo "         armed, so it comes back either way - but the log is how the failure is"
    echo "         told apart from a stale-cache symptom, which is why it is read first."
    ;;
  *)
    fail "unrecognised STAGE90_CACHE_MODE: $(value_of STAGE90_CACHE_MODE)"
    ;;
esac

echo
echo "== mapping attributes: consistency =="
if [[ ! "$(value_of STAGE90_CACHE_MODE)" =~ ^(STAGE90_CACHE_MODE_NONE|0|0u)$ ]] &&
   [[ ! "$(value_of STAGE90_PMAP_ATTR_MODE)" =~ ^(STAGE90_PMAP_ATTR_MODE_NORMAL_WB|2|2u)$ ]]; then
  fail "the I-cache is enabled but DRAM is not marked cacheable, so it would cache nothing"
fi
echo "ok: cache and attribute switches agree"

echo
echo "All checks passed. To run the non-persistent boot (writes nothing to storage):"
echo
echo "  sudo adb -s 4a2fe00b reboot bootloader"
echo "  sudo fastboot boot $IMAGE"
echo
echo "Recover afterwards with:"
echo "  sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-last_kmsg.txt"
