#!/usr/bin/env bash
# RESTORE the `recovery` partition (mmcblk0p20) to the STOCK recovery image.
# Needed after the XNU-persistent-boot test (attempt-2, A2 payload) left the phone dark.
#
# The device will come back when the operator power-presses:
#   - a plain power-on boots boot(p19) = Android (aboot's recovery flag is one-shot), or
#   - VolDown + Power + USB boots fastboot.
#
# Route A (device in Android — the expected route after a plain power-on):
#   scripts/restore_recovery_from_xnu.sh
# Route B (device in fastboot — holds if the plain power-on does not land in Android):
#   scripts/restore_recovery_from_xnu.sh --fastboot
set -euo pipefail
cd "$(dirname "$0")/.."
SERIAL=4a2fe00b
STOCK=out/stage90/backups/recovery_now_twrp.img   # stock recovery, sha fbb01c55...
TWRP=twrp-3.7.0_9-0-cancro.img                    # used only to get an adb shell in route B

if [[ "${1:-}" == "--fastboot" ]]; then
  echo "== route B: fastboot -> boot TWRP non-persistently -> dd the stock recovery back =="
  fastboot devices | grep -q "$SERIAL" || { echo "no fastboot device; hold VolDown+Power"; exit 1; }
  fastboot boot "$TWRP"
  echo "waiting for TWRP's adb..."; adb wait-for-device; sleep 8
  adb -s "$SERIAL" push "$STOCK" /tmp/rec_stock.img
  adb -s "$SERIAL" shell "dd if=/tmp/rec_stock.img of=/dev/block/mmcblk0p20 bs=1048576"
  adb -s "$SERIAL" shell "dd if=/dev/block/mmcblk0p20 bs=512 count=6 2>/dev/null" | sha256sum
else
  echo "== route A: device in Android -> dd =="
  adb -s "$SERIAL" get-state >/dev/null || { echo "no adb device on $SERIAL"; exit 1; }
  adb -s "$SERIAL" shell "id" | grep -q 'uid=0' || { echo "need root (adb root)"; exit 1; }
  adb -s "$SERIAL" push "$STOCK" /data/local/tmp/rec_stock.img
  adb -s "$SERIAL" shell "dd if=/data/local/tmp/rec_stock.img of=/dev/block/mmcblk0p20 bs=1048576 conv=fsync"
fi
echo "done. p20 restored to the stock recovery (fbb01c55...)."
echo "verify: adb reboot recovery should bring up the device's own recovery, not XNU."
