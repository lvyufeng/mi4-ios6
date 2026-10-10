#!/usr/bin/env bash
# recover_last_kmsg.sh - RECOVER THE OWED LOG OF A RUN THAT DID NOT RETURN.
#
# Run this when you are back at the device AFTER a resident/non-returning press (973 / 974 / 975).
# It reads the PREVIOUS boot's RAM console out of /proc/last_kmsg, stores it as a dated capture, and
# runs the project's own --summarise over it - printing exactly the keys the pending experiments
# are waiting on.
#
# *** THE READER CARRIER IS THE FLASHED ROM, REACHED BY A PLAIN REBOOT - *NOT* `fastboot boot`. ***
# Measured 2026-10-10 on this device: after `adb reboot` (which boots the *installed* ROM), the
# flashed kernel exposes `/proc/last_kmsg` = the previous boot's RAM console, a full ~2 MB. But after
# `fastboot boot <golden boot.img>` the very same kernel exposed NO `/proc/last_kmsg` ("No such file
# or directory"). This CORRECTS the earlier belief (in the 973/975 docs and the first version of this
# script) that the recovery carrier is a non-persistent `fastboot boot` of the golden image. Booting
# `fastboot boot <anything>` installs that new kernel AND burns the single-slot record, so it both
# fails to expose the log and destroys it. **The correct carrier is a plain reboot of the flashed ROM.**
#
# WHY THIS EXISTS. The decisive reading of the current line is the log of the boot that did NOT
# return, and the single-slot ram-console record survives only until the phone's NEXT boot. So the
# recovery is: bring the phone back up (that boot IS the one that holds the run's log as its
# previous), then read `/proc/last_kmsg` BEFORE booting anything else. The 973 log was LOST because a
# plain power-on was done first, and every boot since overwrites the single slot.
#
# *** THIS IS NOT A PRESS. *** It does not send our payload and does not spend a run. It only
# reboots the installed ROM and reads the log. (PRESS IS THE OPERATOR'S.)
#
# WHAT IT ANSWERS (see docs/experiments/experiment-973-...md §8 / experiment-975-...md §6):
#   xnu_live_wdt_pets            > 0  => the run was RESIDENT (idle loop), the goal's 「保持在 xnu 里」
#   xnu_entry_args_memSize       == 0x1e400000 => 975's 484 MiB window rung actually ran
#   xnu_live_usb_live_state / _dev_*  => the full USB ladder DID reach the host (device mode)
#   xnu_live_usb_enum_* / _stream_*   => the ladder's further rungs
#   BSD root: / launchd past __TEXT  => the 969 wall (16 MiB window) held or not
#
# Usage:
#   scripts/recover_last_kmsg.sh              # (fastboot) reboot the ROM, read, summarise
#   scripts/recover_last_kmsg.sh --read-only  # device is ALREADY in Android: just read + summarise
set -euo pipefail
cd "$(dirname "$0")/.."

SERIAL=4a2fe00b
OTHER=33e80afe
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
  if fastboot devices 2>/dev/null | grep -q "$SERIAL"; then
    # In fastboot: the ONLY way forward is to boot the installed ROM (plain reboot to system), because
    # that is the kernel whose /proc/last_kmsg exposes the previous boot's console. `fastboot boot
    # <image>` would install a different kernel AND burn the record (measured 2026-10-10).
    say "== 2. reboot into the INSTALLED ROM (plain reboot; NO fastboot boot, NEVER flash) =="
    say "   (fastboot boot <anything> would install a new kernel and destroy the single-slot record)"
    timeout 30 sudo fastboot -s "$SERIAL" reboot || die "fastboot reboot failed"
    say "   sent. Waiting for Android to come up (adbd is ~30-60s behind the kernel)..."
    for _ in $(seq 1 60); do
      adb devices 2>/dev/null | grep -q "$SERIAL" && break
      sleep 3
    done
    adb devices 2>/dev/null | grep -q "$SERIAL" \
      || die "Android did not reach adb within ~180s. Do not power-cycle: retry the read by hand:
              sudo adb -s $SERIAL exec-out 'cat /proc/last_kmsg' > /tmp/k.txt"
  elif adb devices 2>/dev/null | grep -q "$SERIAL"; then
    die "the device is up in Android already - re-run with --read-only (do NOT re-boot it: the boot would overwrite the log you are trying to read)"
  else
    die "no device on fastboot or adb. If the phone is dark: hold Power ~10-15s to bring it back (that
         boot's own log becomes /proc/last_kmsg), then re-run. If it is in Android, re-run with
         --read-only. Do not boot anything else first: each boot overwrites the single-slot record."
  fi
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