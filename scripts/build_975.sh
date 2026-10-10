#!/usr/bin/env bash
# Build 975: the 484 MiB ENTRY WINDOW on the WORKING entry line (971/973/974).
#
# WHY: 969 measured that the real iOS 7.1.2 /sbin/launchd links libSystem/libbsm out of the 301 MiB
# dyld shared cache, while XNU managed only the 16 MiB window - and concluded iOS could not run
# without the whole-kernel 915-B pmap port. 970 designed the one-switch rung
# (STAGE90_XNU_ENTRY_WINDOW=0x1e400000, 484 MiB, under the 485 MiB console ceiling) that gives XNU
# ~470 MiB free with NO pmap port - but it was pressed ONLY on the entry bin 7107b998, which NEVER
# BOOTED (970 6c: the 484 MiB arm and its 16 MiB control failed identically). So the window was never
# tested on a working entry. NOW the entry line boots (971) and 974 makes its console observable.
# 975 is the missing measurement: the SAME window rung, on the NOW-WORKING entry line.
#
# The window reaches XNU ONLY through the payload's generated header (build_entry.sh substitutes
# @ENTRY_SIZE@ into xnu_arm_entry.h, read at xnu_entry_jump.c:150 as `a->memSize = STAGE90_XNU_ENTRY_SIZE`),
# so the entry BIN is unchanged and the arm name comes from the qcdt (the armed-window-* family, 970 §4).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 975 step 1: rebuild the entry image with the 484 MiB window =="
echo "   (entry bin should NOT move; only the generated xnu_arm_entry.h moves)"

# Build the export list from the arm's own record, then run the entry build in a clean env so no
# stray STAGE90_* from this shell leaks in (mi4-build-variant-comes-from-an-env-default).
EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;  # outputs, not inputs
    STAGE90_XNU_ENTRY_WINDOW) continue ;;                          # override below
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
EXPORTS+=("STAGE90_XNU_ENTRY_WINDOW=0x1e400000")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "== step 1 result =="
grep -E "ENTRY_WINDOW" out/stage90/xnu_arm_entry-config.txt
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "  (974's was 73475747 - if this matches, the window moved ONLY the payload header, as 970 4 predicts)"
printf 'generated header window: '
grep -E "STAGE90_XNU_ENTRY_SIZE" out/stage90/xnu_arm_entry.h | head -1