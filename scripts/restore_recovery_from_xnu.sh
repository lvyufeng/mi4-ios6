#!/usr/bin/env bash
# RESTORE: put the stock recovery back into `recovery` (p20) after the XNU-persistent-boot test.
# Two routes, in order of preference:
#  A) if the device is in Android (adb):  run this script -> dd from the backup
#  B) if the device is DARK (XNU latched, watchdog disarmed): hold Power+VolDown ~10-15s to enter
#     fastboot, then:  scripts/restore_recovery_from_xnu.sh --fastboot
set -euo pipefail
cd "$(dirname "$0")/.."
BACKUP=out/stage90/backups/recovery_now_twrp.img   # = stock recovery, sha fbb01c55...
SERIAL=4a2fe00b
if [[ "${1:-}" == "--fastboot" ]]; then
  echo "== fastboot route: boot the stock recovery non-persistently, then dd it back =="
  fastboot devices | grep -q "$SERIAL" || { echo "no fastboot device; hold Power+VolDown"; exit 1; }
  fastboot boot xiaomi4-cancro-backup-20260604-112053/recovery.img
  echo "waiting for recovery adb..."; adb wait-for-device; sleep 5
  adb -s "$SERIAL" push "$BACKUP" /tmp/rec.img
  adb -s "$SERIAL" shell "dd if=/tmp/rec.img of=/dev/block/mmcblk0p20 bs=1048576"
else
  adb -s "$SERIAL" shell "id" | grep -q uid=0 || { echo "need adb root (adb root)"; exit 1; }
  adb -s "$SERIAL" push "$BACKUP" /data/local/tmp/rec.img
  adb -s "$SERIAL" shell "dd if=/data/local/tmp/rec.img of=/dev/block/mmcblk0p20 bs=1048576 conv=fsync"
fi
echo "done. p20 restored to stock recovery (fbb01c55...)."
