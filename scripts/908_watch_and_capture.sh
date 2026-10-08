#!/usr/bin/env bash
# Watch for the (dark) phone to reappear after a normal-slot press, then pull the RAM console out
# of the running Android/TWRP (non-persistent `fastboot boot` — writes nothing; within the hardware
# gate) and summarise it. Runs until a log is captured, an adb device appears, or the window closes.
#
# **The recovery carrier is the STOCK ANDROID image, not TWRP (corrected 2026-10-08).** The reader
# that exposes the previous boot's RAM console as `/proc/last_kmsg` lives in the stock Android kernel
# only; TWRP 3.7.0_9-0's kernel has none (no pstore, no ramoops, `/dev/mem` refused) — measured when a
# TWRP-first recovery returned TWRP's OWN kernel log instead of the run's, because booting any new
# kernel takes the single previous-boot slot. So a fastboot fallback boots the golden Android image.
set -u
cd /mnt/data/mi4-ios6
SERIAL=4a2fe00b
# Prefer the golden stock Android boot image; fall back to TWRP only if it is absent (it should not be).
GOLDEN_DIR=xiaomi4-cancro-backup-20260604-112053
TWRP="$GOLDEN_DIR/boot.img"
[ -r "$TWRP" ] || TWRP=twrp-3.7.0_9-0-cancro.img
CAP=""
mkdir -p out/stage90/captures

grab() {  # pull last_kmsg out of whatever we are connected to, retrying a bounded number of times
  # **The acceptance test is not `-s` (non-empty): a missing `/proc/last_kmsg` makes `cat` print a
  # 61-byte error to STDOUT, which passed `-s` and was captured as if it were the log** (measured
  # 2026-10-08). A real console is hundreds of KB; require that AND that the content is not a shell
  # error line, so "the reader is absent" cannot masquerade as "the log was empty".
  local out="$1" j
  for j in $(seq 1 15); do
    if timeout 25 adb -s "$SERIAL" exec-out 'cat /proc/last_kmsg' > "$out.new" 2>/dev/null \
       && [ -s "$out.new" ] && [ "$(wc -c <"$out.new")" -ge 1024 ] \
       && ! head -c 200 "$out.new" | grep -qaE '^[a-z]+: .*(No such file|can.t open|not found)'; then
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
    echo "[$(date -u +%H:%M:%S)] fastboot appeared -> non-persistent boot of $TWRP (stock carrier for /proc/last_kmsg)"
    timeout 60 sudo fastboot boot "$TWRP" 2>&1 | tail -2
    sleep 25
    if grab "$CAP"; then echo "CAPTURED $CAP $(wc -c <"$CAP")B"; break; fi
  elif [ "$a" = device ]; then
    echo "[$(date -u +%H:%M:%S)] adb=device (stock Android) -> grabbing last_kmsg directly"
    if grab "$CAP"; then echo "CAPTURED $CAP $(wc -c <"$CAP")B"; break; fi
    echo "adb=device present but no log; leaving for the operator"; break
  elif [ "$a" = recovery ]; then
    # TWRP's kernel has no /proc/last_kmsg (measured); grabbing here reads nothing useful.
    echo "[$(date -u +%H:%M:%S)] adb=recovery (TWRP) - its kernel has no /proc/last_kmsg; reboot to"
    echo "  bootloader and boot the STOCK Android image instead. Leaving for the operator."
    break
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