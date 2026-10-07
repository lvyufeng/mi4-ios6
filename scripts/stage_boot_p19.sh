#!/usr/bin/env bash
# Stage ONE recorded payload into Mi 4 boot(p19) so that a PLAIN POWER-ON loads it.
#
# This is the step that makes a normal-slot boot possible at all: `fastboot boot` sends an
# image over USB and writes nothing, so a normal-slot press is only reachable once the bytes
# are IN the partition. It is also the step that can lose the Android fallback, which is why
# it is a tool with a before-image and a full readback rather than a `dd` typed by hand.
#
# Default: read-only preflight. No automatic root, reboot, wait-for-device, or fastboot call.
# The operator must already have a ROOT adb shell. Nothing here touches p1/p2/p3/p7 (sbl1/rpm/
# tz/aboot), the GPT, or any partition other than p19.
#
# Usage:
#   scripts/stage_boot_p19.sh --expect-sha256=HEX [--image PATH] [--execute] [--timeout-seconds N]
#
# Host requirements: Bash, GNU coreutils (timeout/sync/stat/sha256sum), util-linux flock, adb.
# Device requirements: id, readlink -f, dd conv=fsync. `fastboot` is NOT used: this tool must
# never be able to invoke the one command that writes to storage, so it is not on its list.
# Failed/expired writes are UNKNOWN, not rolled back automatically: do not reboot or retry
# blindly. Completed and partial captures are kept for manual inspection.
set -Eeuo pipefail
umask 077

readonly SERIAL="4a2fe00b"
readonly FORBIDDEN_SERIAL="33e80afe"
readonly PARTITION="/dev/block/mmcblk0p19"
readonly BOOT_BY_NAME="/dev/block/platform/msm_sdcc.1/by-name/boot"
readonly SYS_START="/sys/class/block/mmcblk0p19/start"
readonly SYS_SIZE="/sys/class/block/mmcblk0p19/size"
readonly EXPECTED_START=393216
readonly EXPECTED_SECTORS=65536  # sysfs uses 512-byte sectors: exactly 32 MiB
readonly PARTITION_BYTES=$(( EXPECTED_SECTORS * 512 ))  # the full p19 before image is exactly this
readonly SECTOR=512
readonly ANDROID_MAGIC='ANDROID!'

EXECUTE=0
CALL_TIMEOUT=120
EXPECT_SHA=""
IMAGE=""
RUN_DIR=""
REMOTE_DIR=""
WRITE_STARTED=0

usage() {
    printf '%s\n' \
        'Usage: stage_boot_p19.sh --expect-sha256=HEX [--image PATH] [--execute] [--timeout-seconds 1..120]' \
        'Default DRY RUN: validate the payload and read-only device preflight.' \
        '--execute: save full p19 before image, then write the payload region and hash a full readback.' \
        '--expect-sha256=HEX: the payload MUST hash to this, or nothing touches the device.' \
        '--image PATH: the recorded payload (default out/stage90/stage90-qcdt.img).' \
        '--timeout-seconds N: deadline per adb call (default 120; +2s kill grace).' \
        'Requires an already running uid=0 adb shell on 4a2fe00b; no automatic reboot.' \
        'Writes p19 ONLY, and only the payload'"'"'s own byte count - never a whole-partition zeroing,' \
        'so the bytes past the payload are left exactly as they were.' \
        'An execute run retains the full p19 before image in out/stage90/backups.'
}

die() {
    printf 'REFUSED: %s\n' "$*" >&2
    exit 1
}

on_exit() {
    local rc=$?
    if (( rc != 0 )); then
        [[ -z "$RUN_DIR" ]] || printf 'Retained host record/captures: %s\n' "$RUN_DIR" >&2
        [[ -z "$REMOTE_DIR" ]] || printf 'Device temp directory may remain: %s\n' "$REMOTE_DIR" >&2
        if (( WRITE_STARTED )); then
            printf '%s\n' 'p19 stage NOT verified; a failed/timed-out remote dd may have continued. Do not reboot or retry blindly.' >&2
        fi
    fi
}
trap on_exit EXIT
trap 'die "host step failed at line $LINENO; stopping without automatic recovery"' ERR

while (( $# )); do
    case "$1" in
        --execute)
            (( EXECUTE == 0 )) || die 'duplicate --execute'
            EXECUTE=1
            shift
            ;;
        --expect-sha256)
            (( $# >= 2 )) || die '--expect-sha256 requires a 64-hex value'
            EXPECT_SHA="$2"; shift 2
            ;;
        --expect-sha256=*)
            EXPECT_SHA=${1#--expect-sha256=}; shift
            ;;
        --image)
            (( $# >= 2 )) || die '--image requires a path'
            IMAGE="$2"; shift 2
            ;;
        --image=*)
            IMAGE=${1#--image=}; shift
            ;;
        --timeout-seconds)
            (( $# >= 2 )) || die '--timeout-seconds requires an integer from 1 to 120'
            [[ "$2" =~ ^[1-9][0-9]{0,2}$ ]] && (( 10#$2 <= 120 )) || \
                die '--timeout-seconds must be an integer from 1 to 120'
            CALL_TIMEOUT="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *) die "unknown argument: $1 (use --help)" ;;
    esac
done
readonly CALL_TIMEOUT

# The pin is required and is checked against the bytes, not against a filename: this is the one
# write in the tree whose bytes decide what a plain power-on runs, and a path is not an identity.
[[ "$EXPECT_SHA" =~ ^[0-9a-f]{64}$ ]] || \
    die "--expect-sha256 must be exactly 64 lowercase hex characters (got '${EXPECT_SHA:-<empty>}'); no default is offered, because a default here is a name nobody typed"

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly REPO_ROOT
readonly BACKUPS="$REPO_ROOT/out/stage90/backups"
[[ -n "$IMAGE" ]] || IMAGE="$REPO_ROOT/out/stage90/stage90-qcdt.img"

for tool in adb timeout stat sha256sum mktemp cp chmod mv date sync flock; do
    command -v "$tool" >/dev/null || die "missing host tool: $tool"
done

# Every adb call, including inventory, goes through this serial-pinned deadline.
# Killing the HOST client cannot prove that a remote write has stopped.
adb_call() {
    timeout --kill-after=2s "${CALL_TIMEOUT}s" adb -s "$SERIAL" "$@"
}

adb_text() {
    local text
    text=$(adb_call "$@") || return $?
    # Old adb shells emit CRLF. Strip only a final CR, never alter binary dumps.
    printf '%s' "${text%$'\r'}"
}

file_size() {
    stat -L -c '%s' -- "$1"
}

file_hash() {
    local result
    result=$(sha256sum < "$1") || return $?
    printf '%s' "${result%% *}"
}

# --- the payload's identity, checked before ANY device contact --------------------------------
# Three independent facts, and all three are refusals: the byte count must be a whole number of
# sectors (so the write is exactly the file and cannot round up into a neighbouring byte), the
# hash must be the pinned one, and the first eight bytes must be the boot-image magic the
# bootloader looks for. A payload that is a valid file but not a boot image would be written and
# then ignored by aboot - a silent way to lose the fallback without losing a byte.
PAYLOAD_BYTES=0
PAYLOAD_SECTORS=0
check_payload() {
    local hash magic
    [[ -f "$IMAGE" && -r "$IMAGE" ]] || die "missing/unreadable regular file: $IMAGE"
    PAYLOAD_BYTES=$(file_size "$IMAGE") || die "cannot read byte count: $IMAGE"
    (( PAYLOAD_BYTES > 0 )) || die "$IMAGE is empty"
    (( PAYLOAD_BYTES % SECTOR == 0 )) || \
        die "$IMAGE is $PAYLOAD_BYTES bytes, not a whole number of ${SECTOR}-byte sectors"
    PAYLOAD_SECTORS=$(( PAYLOAD_BYTES / SECTOR ))
    (( PAYLOAD_SECTORS <= EXPECTED_SECTORS )) || \
        die "$IMAGE is $PAYLOAD_BYTES bytes, larger than p19's $(( EXPECTED_SECTORS * SECTOR ))"
    magic=$(head -c 8 -- "$IMAGE") || die "cannot read the first bytes of $IMAGE"
    [[ "$magic" == "$ANDROID_MAGIC" ]] || \
        die "$IMAGE does not begin with '$ANDROID_MAGIC' (got '${magic}'); aboot would load nothing from p19"
    hash=$(file_hash "$IMAGE") || die "cannot hash: $IMAGE"
    [[ "$hash" == "$EXPECT_SHA" ]] || \
        die "$IMAGE SHA256=$hash, but --expect-sha256=$EXPECT_SHA; nothing was sent"
    printf 'Payload: %s (%s bytes, %s sectors, SHA256=%s)\n' "$IMAGE" "$PAYLOAD_BYTES" "$PAYLOAD_SECTORS" "$hash"
}
readonly -f check_payload

check_devices() {
    local adb_list serial state rest count=0 ready=0
    adb_list=$(adb_call devices) || die 'bounded adb devices call failed'
    # fastboot is deliberately NOT consulted: this tool holds no fastboot call at all.
    while read -r serial state rest; do
        [[ "$serial" != "$FORBIDDEN_SERIAL" ]] || \
            die "$FORBIDDEN_SERIAL appears in adb devices; unplug it"
        if [[ "$serial" == "$SERIAL" ]]; then
            count=$((count + 1))
            case "$state" in device|recovery) ready=1 ;; esac
        fi
    done <<< "${adb_list//$'\r'/}"
    (( count == 1 && ready == 1 )) || die "$SERIAL must appear exactly once as device/recovery in adb"
}

check_root() {
    local uid
    uid=$(adb_text shell 'id -u') || die 'bounded root check failed'
    [[ "$uid" == 0 ]] || die "adb shell uid must be exactly 0 (got '$uid'); no automatic adb root"
}

check_geometry() {
    local node start sectors
    node=$(adb_text shell "test -b '$PARTITION' && test -b '$BOOT_BY_NAME' && readlink -f '$BOOT_BY_NAME'") || \
        die 'cannot resolve the boot by-name block node'
    [[ "$node" == "$PARTITION" ]] || die "boot by-name resolves to '$node', not $PARTITION"
    start=$(adb_text shell "cat '$SYS_START'") || die 'cannot read p19 sysfs start'
    sectors=$(adb_text shell "cat '$SYS_SIZE'") || die 'cannot read p19 sysfs size'
    [[ "$start" == "$EXPECTED_START" && "$sectors" == "$EXPECTED_SECTORS" ]] || \
        die "p19 geometry start='$start' size='$sectors'; expected $EXPECTED_START/$EXPECTED_SECTORS (512-byte sectors)"
}

# Also re-evaluate uid, by-name and geometry INSIDE each p19 dump/write command.
# No mutable partition name or caller-provided target can enter the dd output.
readonly REMOTE_GUARD="test \"\$(id -u)\" = 0 && test -b '$PARTITION' && test -b '$BOOT_BY_NAME' && test \"\$(readlink -f '$BOOT_BY_NAME')\" = '$PARTITION' && test \"\$(cat '$SYS_START')\" = '$EXPECTED_START' && test \"\$(cat '$SYS_SIZE')\" = '$EXPECTED_SECTORS'"

# Every binary capture is size-checked against the byte count that capture must have, and a
# mismatched one is REFUSED while still under `.partial` - never promoted. Without this the
# before image could be an old adb's truncated 1 MiB and the run would carry on to push and write,
# which is the one failure this tool exists to prevent: a write with no usable way back.
dump_binary() {
    local command="$1" destination="$2" expected="$3" bytes
    adb_call exec-out "$command" > "$destination.partial" || die "binary capture failed: $destination.partial"
    bytes=$(file_size "$destination.partial") || die "cannot read byte count: $destination.partial"
    [[ "$bytes" == "$expected" ]] || \
        die "$destination.partial has $bytes bytes, expected exactly $expected; the capture is not usable and was NOT promoted"
    mv -- "$destination.partial" "$destination"
}

save_fingerprint() {
    local hash
    hash=$(file_hash "$1") || die "cannot hash capture: $1"
    printf '%s  %s\n' "$hash" "${1##*/}" > "$1.sha256"
    # Make the before image AND its record durable on the host before a push/write.
    sync "$1" "$1.sha256" "$RUN_DIR"
    printf 'Full capture: %s (%s bytes, SHA256=%s)\n' "$1" "$(file_size "$1")" "$hash"
}

check_payload
check_devices
state=$(adb_text get-state) || die 'bounded adb get-state failed'
case "$state" in device|recovery) ;; *) die "unexpected adb state: '$state'" ;; esac
check_root
check_geometry
bootmode=$(adb_text shell 'getprop ro.bootmode') || die 'cannot inspect boot mode'
twrp_version=$(adb_text shell 'getprop ro.twrp.version') || die 'cannot inspect recovery property'
if [[ "$bootmode" == recovery || -n "$twrp_version" ]]; then
    TEMP_BASE=/tmp
else
    TEMP_BASE=/data/local/tmp
fi
readonly TEMP_BASE
marker=$(adb_text shell "test -d '$TEMP_BASE' && test -w '$TEMP_BASE' && printf 'BOOT_P19_TEMP_OK\\n'") || \
    die "temp base is not root-writable: $TEMP_BASE"
[[ "$marker" == BOOT_P19_TEMP_OK ]] || die "temp base did not confirm access: $TEMP_BASE"

if (( ! EXECUTE )); then
    printf '%s\n' \
        'DRY RUN: payload identity, root shell, by-name and p19 geometry checked.' \
        "Would save the full 32 MiB p19 before image, stage under $TEMP_BASE, dd conv=fsync" \
        "ONLY $PARTITION (bs=$SECTOR count=$PAYLOAD_SECTORS - the payload's own bytes, tail left as-is)," \
        'and hash a full readback. No push, temp-file creation, partition write, or reboot was issued.' \
        'Use --execute to stage.'
    exit 0
fi

# Serialize this helper's execute runs in this checkout. No lock/device temp is
# created by the default dry run.
exec {lock_fd}> "$BACKUPS/.stage_boot_p19.lock"
flock -n "$lock_fd" || die 'another p19 staging helper holds the host lock'
stamp=$(date -u '+%Y%m%dT%H%M%SZ')
RUN_DIR=$(mktemp -d "$BACKUPS/boot-p19-stage-$stamp-XXXXXXXX")
readonly TRUSTED_COPY="$RUN_DIR/payload.img"
readonly BEFORE="$RUN_DIR/p19-before.img"
readonly UPLOAD_CHECK="$RUN_DIR/upload-check.img"
readonly READBACK_HEAD="$RUN_DIR/p19-readback-head.img"
# Freeze the verified payload bytes instead of pushing a possibly changing original.
cp -- "$IMAGE" "$TRUSTED_COPY"
chmod 400 "$TRUSTED_COPY"
IMAGE="$TRUSTED_COPY"
check_payload
dump_binary "$REMOTE_GUARD && dd if='$PARTITION' bs=1048576 2>/dev/null" "$BEFORE" "$PARTITION_BYTES"
save_fingerprint "$BEFORE"

check_devices
REMOTE_DIR="$TEMP_BASE/${RUN_DIR##*/}"
readonly STAGED="$REMOTE_DIR/payload.img"
# mkdir must be new (no reuse/symlink). Probe real writability and conv=fsync on
# disposable files BEFORE pushing or opening a block device for write.
marker=$(adb_text shell "umask 077 && mkdir '$REMOTE_DIR' && printf x > '$REMOTE_DIR/probe' && dd if='$REMOTE_DIR/probe' of='$REMOTE_DIR/fsync-probe' bs=1 count=1 conv=fsync 2>/dev/null && printf 'BOOT_P19_STAGE_OK\\n'") || \
    die 'device temp creation/fsync probe failed; no partition write attempted'
[[ "$marker" == BOOT_P19_STAGE_OK ]] || die 'device temp/fsync probe did not confirm completion'
adb_call push "$TRUSTED_COPY" "$STAGED" || die 'bounded payload push failed'
marker=$(adb_text shell "test -f '$STAGED' && test ! -L '$STAGED' && chmod 400 '$STAGED' && printf 'BOOT_P19_SEALED_OK\\n'") || \
    die 'cannot seal the staged regular file'
[[ "$marker" == BOOT_P19_SEALED_OK ]] || die 'staged-file check did not confirm completion'
# Do not assume the device has sha256sum. Read back ALL uploaded bytes via exec-out and
# validate them on the host before the partition is opened for write.
dump_binary "test -f '$STAGED' && test ! -L '$STAGED' && dd if='$STAGED' bs=1048576 2>/dev/null" "$UPLOAD_CHECK" "$PAYLOAD_BYTES"
uploaded=$(file_hash "$UPLOAD_CHECK") || die 'cannot hash the upload readback'
[[ "$uploaded" == "$EXPECT_SHA" ]] || \
    die "the device copy hashes $uploaded, not $EXPECT_SHA; the push is not what was verified. No partition write attempted"

check_devices
check_root
check_geometry
WRITE_STARTED=1
# bs=SECTOR and count=PAYLOAD_SECTORS: the write is the payload's OWN bytes and nothing else.
# The 32 MiB partition is deliberately NOT zeroed first - aboot reads the image header's own
# sizes, and zeroing the tail would destroy bytes this tool has no mandate to change.
marker=$(adb_text shell "$REMOTE_GUARD && test -f '$STAGED' && test ! -L '$STAGED' && dd if='$STAGED' of='$PARTITION' bs=$SECTOR count=$PAYLOAD_SECTORS conv=fsync 2>/dev/null && printf 'BOOT_P19_WRITE_FSYNC_OK\\n'") || \
    die 'bounded p19 dd failed; no automatic recovery attempted'
# Some old adb versions lose the remote exit status. A success-only marker is
# required too; a host zero status alone is NOT a write-completion verdict.
[[ "$marker" == BOOT_P19_WRITE_FSYNC_OK ]] || die 'p19 dd did not confirm fsync completion'
# Read back the payload region itself (not the whole 32 MiB: the tail is not ours to compare).
dump_binary "$REMOTE_GUARD && dd if='$PARTITION' bs=$SECTOR count=$PAYLOAD_SECTORS 2>/dev/null" "$READBACK_HEAD" "$PAYLOAD_BYTES"
readback=$(file_hash "$READBACK_HEAD") || die 'cannot hash the readback'
save_fingerprint "$READBACK_HEAD"
[[ "$readback" == "$EXPECT_SHA" ]] || \
    die "readback SHA256=$readback, not $EXPECT_SHA; p19 does not hold the payload. The before image is in $RUN_DIR"

# Cleanup is bounded, touches only our three temporary files, and runs only
# after matched readback. On ANY failure no further device call is made.
marker=$(adb_text shell "rm -f '$REMOTE_DIR/probe' '$REMOTE_DIR/fsync-probe' '$STAGED' && rmdir '$REMOTE_DIR' && printf 'BOOT_P19_CLEAN_OK\\n'") || \
    die 'verified readback, but device temp cleanup failed'
[[ "$marker" == BOOT_P19_CLEAN_OK ]] || die 'verified readback, but temp cleanup did not confirm completion'
REMOTE_DIR=""
printf 'SUCCESS: boot(p19) now holds the pinned payload; its first %s bytes read back SHA256=%s.\n' \
    "$PAYLOAD_BYTES" "$EXPECT_SHA"
printf 'The p19 before image and the readback are retained in %s. No reboot issued.\n' "$RUN_DIR"
printf 'Restore p19 from the before image with a guarded tool if this arm must be abandoned.\n'