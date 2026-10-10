#!/usr/bin/env bash
# recover_last_kmsg.sh - RECOVER THE OWED LOG OF A RUN THAT DID NOT RETURN.
#
# Run this when you are back at the device AFTER a resident/non-returning press (973 / 974 / 975).
# It boots the STOCK Android image non-persistently (fastboot boot, NEVER flash), waits for adb,
# reads the PREVIOUS boot's RAM console out of /proc/last_kmsg, stores it as a dated capture, and
# runs the project's own --summarise over it - printing exactly the keys the pending experiments
# are waiting on.
#
# WHY THIS EXISTS. The decisive reading of the current line is the log of the boot that did NOT
# return, and the log survives only until the phone's next POWER CYCLE. The 973 log was LOST because
# the recovery was a set of manual steps and a plain power-on cleared the buffer first. This script
# turns recovery into ONE command so the buffer is captured before anything clears it. It is a
# READ plus one non-persistent boot; it writes NOTHING to storage and cannot brick.
#
# *** THIS IS NOT A PRESS. *** It does not send a payload and does not spend a run. It boots the
# stock image only, to read the log the last press left behind. (PRESS IS THE OPERATOR'S.)
#
# WHAT IT ANSWERS (see docs/experiments/experiment-973-...md §8 / experiment-975-...md §6):
#   xnu_live_wdt_pets            > 0  => the run was RESIDENT (idle loop), the goal's 「保持在 xnu 里」
#   xnu_entry_args_memSize       == 0x1e400000 => 975's 484 MiB window rung actually ran
#   xnu_live_usb_enum_device_*   N/A  => the full USB ladder DID reach the host (device mode)
#   xnu_live_uboot_enum_* / stream=> the ladder's further rungs
#   BSD root: / launchd past __TEXT  => the 969 wall (16 MiB window) held or not
#
# Usage:
#   scripts/recover_last_kmsg.sh              # boot stock, read, summarise
#   scripts/recover_last_kmsg.sh --read-only  # device is ALREADY in Android: just read + summarise
set -euo pipefail
cd "$(dirname "$0")/.."

SERIAL=4a2fe00b
OTHER=33e80afe
GOLDEN_DIR=xiaomi4-cancro-backup-20260604-112053
STOCK=$GOLDEN_DIR/boot.img
OUTDIR=out/stage90/captures
STAMP=$(date +%Y%m%d-%H%M%S)
CAPTURE_TPL="$OUTDIR/recovered-ARM-${STAMP}-last_kmsg.txt"

READ_ONLY=0
[[ ${1:-} == --read-only ]] && READ_ONLY=1

say() { printf '%s\n' "$*"; }
die() { printf 'REFUSING: %s\n' "$*" >&2; exit 1; }

mkdir -p "$OUTDIR"

if (( ! READ_ONLY )); then
  say "== 1. device state =="
  # The hardware gate: this phone present, the OTHER phone absent.
  if fastboot devices 2>/dev/null | grep -q "$OTHER"; then
    die "$OTHER (the OTHER phone) is present - unplug it first (hardware gate)"
  fi
  if ! fastboot devices 2>/dev/null | grep -q "$SERIAL"; then
    if adb devices 2>/dev/null | grep -q "$SERIAL"; then
      die "the device is up in Android already - re-run with --read-only (do NOT re-boot it: the boot would overwrite the log you are trying to read)"
    fi
    die "no device in fastboot. If the phone is dark: hold Power ~10-15s to bring it back, then
         VolDown+Power -> fastboot, and re-run. Do NOT do a normal power-on first: it CLEARS the
         RAM console this script is here to read."
  fi
  [[ -r $STOCK ]] || die "stock image not found: $STOCK (the recovery carrier; booted, never flashed)"

  say "== 2. boot the STOCK image (fastboot boot only, NEVER flash) =="
  say "   image: $STOCK"
  fastboot -s "$SERIAL" boot "$STOCK"
  say "   sent. Waiting for Android to come up (adbd is ~30-60s behind the kernel)..."
  # Bounded wait: adb is slow, so allow up to ~180s without treating slowness as failure.
  for _ in $(seq 1 60); do
    adb devices 2>/dev/null | grep -q "$SERIAL" && break
    sleep 3
  done
  adb devices 2>/dev/null | grep -q "$SERIAL" \
    || die "Android did not reach adb within ~180s. The log is STILL in DRAM (do not power-cycle):
            retry the read by hand: sudo adb -s $SERIAL exec-out 'cat /proc/last_kmsg' > /tmp/k.txt"
else
  say "== --read-only: reading the log from the already-running device =="
  adb devices 2>/dev/null | grep -q "$SERIAL" || die "no adb device $SERIAL"
fi

say "== 3. read the PREVIOUS boot's RAM console (/proc/last_kmsg) =="
TMP=$(mktemp)
if ! sudo adb -s "$SERIAL" exec-out 'cat /proc/last_kmsg' > "$TMP" 2>/dev/null || [[ ! -s $TMP ]]; then
  rm -f "$TMP"
  die "the read returned nothing. Either adbd is not ready yet (wait ~20s and retry --read-only), or
       this boot is NOT Android (TWRP has no /proc/last_kmsg). Do not power-cycle: retry the read."
fi

# Name the capture after the arm whose log it holds, when the log says so.
ARM=$(grep -aoE 'armed-[a-z0-9-]*' "$TMP" | head -1 || true)
[[ -n $ARM ]] || ARM=unknown-arm
CAPTURE=${CAPTURE_TPL/ARM/$ARM}
mv "$TMP" "$CAPTURE"
say "   stored: $CAPTURE  ($(wc -c < "$CAPTURE") bytes)"

say "== 4. is this OUR log, and whose? =="
if grep -aq "MI4IOS6_STAGE90" "$CAPTURE"; then
  say "   yes - it is a MI4IOS6_STAGE90 console."
else
  say "   WARNING: no MI4IOS6_STAGE90 banner in this capture. It may be the stock Android kernel's
           own last_kmsg, not ours - i.e. the previous boot was not our payload. Check the press.
           (The capture is kept anyway: $CAPTURE)"
fi

say "== 5. the readings the open experiments are waiting on =="
# The exact keys 973/975 name; printed with their values, absent ones marked so silence is a reading
# (a key that is absent is as informative as one that is present - [[mi4-silence-is-a-reading-only-if-success-is-silent]]).
for k in xnu_live_wdt_pets xnu_entry_args_memSize \
         xnu_live_usb_live_state xnu_live_usb_dev_is_device xnu_live_usb_dev_proceed \
         xnu_live_usb_enum_state xnu_live_usb_enum_requests xnu_live_usb_enum_configured \
         xnu_live_usb_stream_state xnu_live_ostext_limited xnu_live_ostext_chars \
         xnu_live_capped; do
  v=$(grep -aoE "$k=[^ ]*" "$CAPTURE" | tail -1 || true)
  if [[ -n $v ]]; then printf '   %-34s %s\n' "$k" "$v"; else printf '   %-34s %s\n' "$k" "(absent)"; fi
done
say "   --- last BSD/launchd/vfs lines ---"
grep -aE "BSD root:|launchd|attempting to load|failed loading|panic|assert|vfs_root" "$CAPTURE" | tail -8 || true
say "   --- last 12 console lines (where it stopped, if it stopped) ---"
tail -12 "$CAPTURE" | sed 's/^/   /'

say "== 6. the project's own summary over this capture =="
scripts/run_and_capture.sh --summarise "$CAPTURE" || true

say ""
say "Done. Capture: $CAPTURE"
say "Read: wdt_pets>0 => RESIDENT (goal's 「保持在 xnu 里」). memSize=0x1e400000 => 975's window ran."