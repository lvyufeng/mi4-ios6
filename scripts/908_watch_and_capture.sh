#!/usr/bin/env bash
# Watch for the (dark) phone to reappear after a normal-slot press, then pull the RAM console out
# of verified TWRP (non-persistent `fastboot boot` — writes nothing; within the hardware gate) and
# summarise it. Runs until a log is captured, an adb device appears, or the window closes.
set -u
cd /mnt/data/mi4-ios6
SERIAL=4a2fe00b
TWRP=twrp-3.7.0_9-0-cancro.img
CAP=""
mkdir -p out/stage90/captures

grab() {  # pull last_kmsg out of whatever we are connected to, retrying a bounded number of times
  local out="$1" j
  for j in $(seq 1 15); do
    if timeout 25 adb -s "$SERIAL" exec-out 'cat /proc/last_kmsg' > "$out.new" 2>/dev/null && [ -s "$out.new" ]; then
      mv "$out.new" "$out"; return 0
    fi
    rm -f "$out.new" 2>/dev/null
    sleep 6
  done
  return 1
}

for i in $(seq 1 200); do   # ~40 min at ~12s/iteration
  f=$(timeout 12 sudo fastboot devices 2>/dev/null | awk -v s="$SERIAL" '$1==s{print $2}')
  a=$(timeout 12 adb devices 2>/dev/null | awk -v s="$SERIAL" '$1==s{print $2}')
  CAP="out/stage90/captures/908-normal-$(date -u '+%Y%m%d-%H%M%S')-last_kmsg.txt"
  if [ -n "$f" ]; then
    echo "[$(date -u +%H:%M:%S)] fastboot appeared -> non-persistent TWRP boot"
    timeout 60 sudo fastboot boot "$TWRP" 2>&1 | tail -2
    sleep 25
    if grab "$CAP"; then echo "CAPTURED $CAP $(wc -c <"$CAP")B"; break; fi
  elif [ "$a" = device ] || [ "$a" = recovery ]; then
    echo "[$(date -u +%H:%M:%S)] adb=$a -> grabbing last_kmsg directly"
    if grab "$CAP"; then echo "CAPTURED $CAP $(wc -c <"$CAP")B"; break; fi
    echo "adb=$a present but no log; leaving for the operator"; break
  fi
  sleep 6
done

if [ -n "$CAP" ] && [ -s "$CAP" ]; then
  echo "=== summarise ==="
  bash scripts/run_and_capture.sh --summarise "$CAP" > "${CAP%.txt}.summary.txt" 2>&1 || true
  echo "--- decisive keys ---"
  grep -aoE 'xnu_live_rootmedia_card_off=0x[0-9a-f]+|xnu_live_card_last_lba=0x[0-9a-f]+|xnu_live_capped|attempting to load|failed loading|xnu_live_storage_wr_[a-z_]*=[0-9x]+|xnu_entry_panic_entered=[0-9]|0x8004d2dc' "$CAP" | sort | uniq -c | head -40
  echo "summary -> ${CAP%.txt}.summary.txt"
else
  echo "watcher done, no log (device never reappeared within the window)"
fi