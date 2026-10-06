#!/usr/bin/env bash
# Restore ONLY Mi 4 boot(p19) from the pinned, full-partition Android rollback.
# Default: read-only preflight. No automatic root, reboot, wait-for-device, or
# fastboot boot/flash. The operator must already have a ROOT adb shell.
# Usage: scripts/restore_boot_from_xnu.sh [--execute] [--timeout-seconds 1..120]
# Host requirements: Bash, GNU coreutils (including timeout/sync), util-linux flock,
# adb and fastboot. Device requirements: id, getprop, readlink -f, dd conv=fsync.
# Failed/expired writes are UNKNOWN, not rolled back automatically: do not reboot
# or retry blindly. Completed and partial captures are kept for manual inspection.
set -Eeuo pipefail
umask 077

readonly SERIAL="4a2fe00b"
readonly FORBIDDEN_SERIAL="33e80afe"
readonly EXPECTED_BYTES=33554432
readonly EXPECTED_SHA256="b2119252d046aa2e8e682674949bc5c60a9536358ad83a2c34dfb2de060b4874"
readonly PARTITION="/dev/block/mmcblk0p19"
readonly BOOT_BY_NAME="/dev/block/platform/msm_sdcc.1/by-name/boot"
readonly SYS_START="/sys/class/block/mmcblk0p19/start"
readonly SYS_SIZE="/sys/class/block/mmcblk0p19/size"
readonly EXPECTED_START=393216
readonly EXPECTED_SECTORS=65536  # sysfs uses 512-byte sectors: exactly 32 MiB

EXECUTE=0
CALL_TIMEOUT=120
RUN_DIR=""
REMOTE_DIR=""
WRITE_STARTED=0

usage() {
    printf '%s\n' \
        'Usage: restore_boot_from_xnu.sh [--execute] [--timeout-seconds 1..120]' \
        'Default DRY RUN: validate the rollback and read-only device preflight.' \
        '--execute: save full p19 before image, then restore and hash full readback.' \
        '--timeout-seconds N: deadline per adb/fastboot call (default 120; +2s kill grace).' \
        'Requires an already running uid=0 adb shell on 4a2fe00b; no automatic reboot.' \
        'Rollback is fixed: out/stage90/backups/boot_now.img (33554432 bytes).' \
        'An execute run retains timestamped captures in out/stage90/backups (~128 MiB).'
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
            printf '%s\n' 'p19 restore NOT verified; a failed/timed-out remote dd may have continued. Do not reboot or retry blindly.' >&2
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

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly REPO_ROOT
readonly BACKUPS="$REPO_ROOT/out/stage90/backups"
readonly ROLLBACK="$BACKUPS/boot_now.img"

for tool in adb fastboot timeout stat sha256sum mktemp cp chmod mv date sync flock; do
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

check_full_size() {
    local bytes
    [[ -f "$1" && -r "$1" ]] || die "missing/unreadable regular file: $1"
    bytes=$(file_size "$1") || die "cannot read byte count: $1"
    [[ "$bytes" == "$EXPECTED_BYTES" ]] || \
        die "$1 has $bytes bytes, expected exactly $EXPECTED_BYTES"
}

check_trusted_image() {
    local hash
    check_full_size "$1"
    hash=$(file_hash "$1") || die "cannot hash: $1"
    [[ "$hash" == "$EXPECTED_SHA256" ]] || \
        die "$1 SHA256=$hash, expected $EXPECTED_SHA256"
}

check_devices() {
    local adb_list fastboot_list serial state rest count=0 ready=0
    adb_list=$(adb_call devices) || die 'bounded adb devices call failed'
    fastboot_list=$(timeout --kill-after=2s "${CALL_TIMEOUT}s" fastboot devices) || \
        die 'bounded fastboot devices call failed'
    # Inspect BOTH lists, including offline/unauthorized rows, before using adb.
    while read -r serial state rest; do
        [[ "$serial" != "$FORBIDDEN_SERIAL" ]] || \
            die "$FORBIDDEN_SERIAL appears in adb devices; unplug it"
        if [[ "$serial" == "$SERIAL" ]]; then
            count=$((count + 1))
            case "$state" in device|recovery) ready=1 ;; esac
        fi
    done <<< "${adb_list//$'\r'/}"
    while read -r serial state rest; do
        [[ "$serial" != "$FORBIDDEN_SERIAL" ]] || \
            die "$FORBIDDEN_SERIAL appears in fastboot devices; unplug it"
        [[ "$serial" != "$SERIAL" ]] || \
            die "$SERIAL is in fastboot; obtain a root adb shell yourself"
    done <<< "${fastboot_list//$'\r'/}"
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

dump_binary() {
    local command="$1" destination="$2"
    # Preserve an incomplete dump under .partial, never mistake it for a backup.
    adb_call exec-out "$command" > "$destination.partial" || die "binary capture failed: $destination.partial"
    check_full_size "$destination.partial"
    mv -- "$destination.partial" "$destination"
}

save_fingerprint() {
    local hash
    hash=$(file_hash "$1") || die "cannot hash capture: $1"
    printf '%s  %s\n' "$hash" "${1##*/}" > "$1.sha256"
    # Make the before image AND its record durable on the host before a push/write.
    sync "$1" "$1.sha256" "$RUN_DIR"
    printf 'Full capture: %s (%s bytes, SHA256=%s)\n' "$1" "$EXPECTED_BYTES" "$hash"
}

# Reject missing, shortened, or changed rollback bytes before ANY device contact.
check_trusted_image "$ROLLBACK"
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
        'DRY RUN: rollback identity, root shell, by-name and p19 geometry checked.' \
        "Would save full p19, stage under $TEMP_BASE, dd conv=fsync ONLY $PARTITION, and hash full readback." \
        'No push, temp-file creation, partition write, or reboot was issued. Use --execute to restore.'
    exit 0
fi

# Serialize this helper's execute runs in this checkout. No lock/device temp is
# created by the default dry run. The original rollback is NEVER a write target.
exec {lock_fd}> "$BACKUPS/.restore_boot_p19.lock"
flock -n "$lock_fd" || die 'another boot restore helper holds the host lock'
stamp=$(date -u '+%Y%m%dT%H%M%SZ')
RUN_DIR=$(mktemp -d "$BACKUPS/boot-p19-restore-$stamp-XXXXXXXX")
readonly TRUSTED_COPY="$RUN_DIR/trusted-rollback.img"
readonly BEFORE="$RUN_DIR/p19-before.img"
readonly UPLOAD_CHECK="$RUN_DIR/upload-check.img"
readonly READBACK="$RUN_DIR/p19-readback.img"
# Freeze verified host bytes instead of pushing a possibly changing original.
cp -- "$ROLLBACK" "$TRUSTED_COPY"
chmod 400 "$TRUSTED_COPY"
check_trusted_image "$TRUSTED_COPY"
dump_binary "$REMOTE_GUARD && dd if='$PARTITION' bs=1048576 2>/dev/null" "$BEFORE"
save_fingerprint "$BEFORE"

check_devices
REMOTE_DIR="$TEMP_BASE/${RUN_DIR##*/}"
readonly STAGED="$REMOTE_DIR/rollback.img"
# mkdir must be new (no reuse/symlink). Probe real writability and conv=fsync on
# disposable files BEFORE pushing or opening a block device for write.
marker=$(adb_text shell "umask 077 && mkdir '$REMOTE_DIR' && printf x > '$REMOTE_DIR/probe' && dd if='$REMOTE_DIR/probe' of='$REMOTE_DIR/fsync-probe' bs=1 count=1 conv=fsync 2>/dev/null && printf 'BOOT_P19_STAGE_OK\\n'") || \
    die 'device temp creation/fsync probe failed; no partition write attempted'
[[ "$marker" == BOOT_P19_STAGE_OK ]] || die 'device temp/fsync probe did not confirm completion'
adb_call push "$TRUSTED_COPY" "$STAGED" || die 'bounded rollback push failed'
marker=$(adb_text shell "test -f '$STAGED' && test ! -L '$STAGED' && chmod 400 '$STAGED' && printf 'BOOT_P19_SEALED_OK\\n'") || \
    die 'cannot seal the staged regular file'
[[ "$marker" == BOOT_P19_SEALED_OK ]] || die 'staged-file check did not confirm completion'
# Do not assume the device has sha256sum. Read back ALL uploaded bytes via
# exec-out and validate them on the host before the partition is opened for write.
dump_binary "test -f '$STAGED' && test ! -L '$STAGED' && dd if='$STAGED' bs=1048576 2>/dev/null" "$UPLOAD_CHECK"
check_trusted_image "$UPLOAD_CHECK"

check_devices
check_root
check_geometry
WRITE_STARTED=1
marker=$(adb_text shell "$REMOTE_GUARD && test -f '$STAGED' && test ! -L '$STAGED' && dd if='$STAGED' of='$PARTITION' bs=1048576 count=32 conv=fsync 2>/dev/null && printf 'BOOT_P19_WRITE_FSYNC_OK\\n'") || \
    die 'bounded p19 dd failed; no automatic recovery attempted'
# Some old adb versions lose the remote exit status. A success-only marker is
# required too; a host zero status alone is NOT a write-completion verdict.
[[ "$marker" == BOOT_P19_WRITE_FSYNC_OK ]] || die 'p19 dd did not confirm fsync completion'
dump_binary "$REMOTE_GUARD && dd if='$PARTITION' bs=1048576 2>/dev/null" "$READBACK"
save_fingerprint "$READBACK"
check_trusted_image "$READBACK"

# Cleanup is bounded, touches only our three temporary files, and runs only
# after matched full readback. On ANY failure no further device call is made.
marker=$(adb_text shell "rm -f '$REMOTE_DIR/probe' '$REMOTE_DIR/fsync-probe' '$STAGED' && rmdir '$REMOTE_DIR' && printf 'BOOT_P19_CLEAN_OK\\n'") || \
    die 'verified readback, but device temp cleanup failed'
[[ "$marker" == BOOT_P19_CLEAN_OK ]] || die 'verified readback, but temp cleanup did not confirm completion'
REMOTE_DIR=""
printf 'SUCCESS: ONLY boot(p19) restored; full %s-byte readback SHA256=%s matches the trusted rollback.\n' \
    "$EXPECTED_BYTES" "$EXPECTED_SHA256"
printf 'Before image and readback retained in %s. No reboot issued.\n' "$RUN_DIR"
