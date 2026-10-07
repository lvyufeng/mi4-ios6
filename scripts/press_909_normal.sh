#!/usr/bin/env bash
# 909 — the RESIDENCE press: stage the residence arm into p19, boot it with a PLAIN power-on, and
# read whether XNU STAYS. Same staging and escape as 908 (scripts/press_908_normal.sh); what is
# new is what the arm does and therefore what the operator must do.
#
# WHY 908 NEEDED A 909. 908 proved a plain power-on ENTERS the OS — XNU, `BSD root: md0`, pid 1
# parked in poll — and then ENDED: the image's own deliberate ending (`entry_seam_end_run`,
# `xnu_live_post_end_calls=4`) stored `RESTART_REASON` and faulted at `0xfa0065c`. This arm
# REMOVES that ending (`STAGE90_XNU_RESIDENT=1` => POST_END_TICKS/POST_END_RUN/SEAM_END_RUN all 0)
# and PETS the armed SoC watchdog (`entry_wdt_pet`, once per idle pass), so the run does not end on
# its own clock either. It is the rung that makes 「彻底能直接开机就运行xnu」 a boot that STAYS.
#
# THE HAZARD, AND WHY 909 IS THE FIRST RUNG THAT CAN STRAND THE PHONE. A resident XNU runs no adbd
# and does not re-init USB, so once it is up the ONLY exit is a physical press. Nothing here writes
# p1/p2/p3/p7, the GPT, or p20; `fastboot boot` still writes nothing. The escape is unchanged and
# verified: VolDown + Power + USB -> fastboot -> `fastboot boot` the signed TWRP (non-persistent,
# this script does it under --rescue) -> read /proc/last_kmsg -> restore p19. Because the arm NO
# LONGER ends in 28 s, add: after the plain boot, WAIT — a dark screen with no adbd is the EXPECTED
# resident reading, not a hang. Read the log only through the escape (the RAM console survives a
# warm reset into TWRP); a cold power-off loses it.
#
# Usage:
#   scripts/press_909_normal.sh [--stage] [--boot] [--rescue] [--timeout-seconds N]
#   (no flags) = --stage            stage the live payload into p19, then print what to do next
#   --stage                         verify TWRP, save p19, write the payload region, verify readback
#   --boot                          after staging, issue a PLAIN `adb reboot` and try to capture
#   --rescue                        if the capture fails, `fastboot boot` the signed TWRP and read
#                                   the log out of it (the escape; also usable on its own)
#   --dry-run                       print the plan, touch nothing
set -Eeuo pipefail

SERIAL="4a2fe00b"
FORBIDDEN_SERIAL="33e80afe"
CARGO="out/stage90/stage90-qcdt.img"
EXPECT_ARM="armed-storage-e61ce673"
CAPTURE="out/stage90/captures/909-resident-$(date -u '+%Y%m%d-%H%M%S')-last_kmsg.txt"
CALL_TIMEOUT=120
DO_STAGE=1
DO_BOOT=0
DO_RESCUE=0
DRY_RUN=0

die() { printf 'REFUSED: %s\n' "$*" >&2; exit 1; }

usage() {
    sed -n '2,/^set -Eeuo pipefail$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//; $d'
    exit "${1:-0}"
}

while (( $# )); do
    case "$1" in
        --stage)           DO_STAGE=1; shift ;;
        --boot)            DO_BOOT=1; shift ;;
        --rescue)          DO_RESCUE=1; shift ;;
        --dry-run)         DRY_RUN=1; shift ;;
        --timeout-seconds)
            [[ "${2:-}" =~ ^[1-9][0-9]{0,2}$ ]] && (( 10#$2 <= 120 )) || \
                die '--timeout-seconds must be an integer from 1 to 120'
            CALL_TIMEOUT="$2"; shift 2 ;;
        --help|-h)         usage 0 ;;
        *) die "unknown argument: $1 (use --help)" ;;
    esac
done

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
cd "$REPO_ROOT"

adb_call()  { timeout --kill-after=2s "${CALL_TIMEOUT}s" adb -s "$SERIAL" "$@"; }
fb_call()   { timeout --kill-after=2s "${CALL_TIMEOUT}s" sudo fastboot "$@"; }

present() {  # adb lists the target as device/recovery AND the other phone is absent
    if ! adb devices 2>/dev/null | awk -v s="$SERIAL" -v f="$FORBIDDEN_SERIAL" '
            $1 == f { bad = 1 } $1 == s && ($2 == "device" || $2 == "recovery") { ok = 1 }
            END { exit (bad || !ok) }'; then
        return 1
    fi
    return 0
}

in_fastboot() { fb_call devices 2>/dev/null | grep -q "^$SERIAL"; }

echo "== 909 residence press (a plain boot that STAYS) =="
echo "cargo   $CARGO"
echo "capture $CAPTURE"

# --- 1. the escape image is verified BEFORE anything is written --------------------------------
# The whole point of staging p19 is that the fastboot press becomes the fallback. That fallback is
# only worth having if the TWRP bytes are the ones this project signed off on, so they are checked
# first, by the same tool the storage gate uses (`role=twrp`), and a failure stops before a write.
TWRP_IMG=twrp-3.7.0_9-0-cancro.img
if (( ! DRY_RUN )); then
    bash tools/verify_tool_image.sh "$TWRP_IMG" --require-role=twrp >/dev/null \
        || die "$TWRP_IMG is not the signed, role=twrp image in records/tool-images.txt; the escape would not be the verified one, so NOTHING was written"
    echo "escape  $TWRP_IMG verified (role=twrp)"
fi

# --- 2. the bytes in out/ are the arm this press names, checked BEFORE anything is written -------
# The staging step below writes whatever is in out/. `verify_press_ready.sh` is the project's own
# answer to "are these bytes the recorded arm, and does the gate accept the tree" - five checks,
# and it refuses if the live set is not `armed-storage-e61ce673` (the arm this script names). It is
# run here so a stale out/ is caught before p19 is overwritten, not after.
if (( ! DRY_RUN )); then
    bash tools/verify_press_ready.sh >/tmp/press_909_readiness.log 2>&1 \
        || die "verify_press_ready.sh refused (5 checks in /tmp/press_909_readiness.log): the bytes in out/ are not the recorded residence arm $EXPECT_ARM, or the gate does not accept the tree - NOTHING was written"
    echo "readiness ok: out/ is $EXPECT_ARM and the gate accepts the tree (5/5)"
fi

# --- 3. stage the arm into p19 ----------------------------------------------------------------
if (( DO_STAGE )); then
    if (( DRY_RUN )); then
        echo "would run: scripts/stage_boot_p19.sh --expect-sha256=\$(live payload hash) --execute"
    else
        present || die "$SERIAL is not an adb device (or $FORBIDDEN_SERIAL is present). If the phone is dark, hold Power ~10-15 s; if it is in fastboot, it must be brought back to adb first."
        PAYLOAD_SHA=$(sha256sum < "$CARGO" | awk '{print $1}')
        echo "staging payload $PAYLOAD_SHA into p19 (full before image is retained)"
        bash scripts/stage_boot_p19.sh --expect-sha256="$PAYLOAD_SHA" --execute \
            || die "staging failed; p19 is UNKNOWN. Do NOT power the phone off - read the retention note above and restore p19 from the saved before image"
        echo "staged. p19 now holds the arm; a plain power-on will run XNU, not Android."
    fi
fi

# --- 4. the plain boot (the actual press) -----------------------------------------------------
if (( DO_BOOT )); then
    if (( DRY_RUN )); then
        echo "would run: adb -s $SERIAL reboot    # a PLAIN boot, which is what loads p19"
    else
        present || die "$SERIAL is not an adb device; bring it back to Android/fastboot first"
        echo "plain reboot (this is the press)"
        adb_call reboot || die "adb could not issue a plain reboot; NOTHING was pressed"
        # Wait for the device to reappear OR for the capture window to close.
        echo "waiting up to $(( CALL_TIMEOUT ))s for adb to answer..."
        ok=0
        for _ in $(seq 1 $(( CALL_TIMEOUT / 5 ))); do
            sleep 5
            if adb_call exec-out 'cat /proc/last_kmsg' > "$CAPTURE.new" 2>/dev/null && [[ -s "$CAPTURE.new" ]]; then
                mv "$CAPTURE.new" "$CAPTURE"; ok=1; break
            fi
            rm -f "$CAPTURE.new" 2>/dev/null || true
        done
        if (( ok )); then
            echo "captured $CAPTURE"
            bash scripts/run_and_capture.sh --summarise "$CAPTURE" || true
        else
            rm -f "$CAPTURE.new" 2>/dev/null || true
            echo ""
            echo "adb did not answer within $(( CALL_TIMEOUT ))s. For a NORMAL-SLOT boot that is"
            echo "EXPECTED whether XNU ran and boot-looped, or ran and is resident: XNU brings up"
            echo "no adbd. It is NOT a verdict about the run, and it is NOT a hang."
            echo "FOR 909 SPECIFICALLY: the arm is built NOT to end, so unlike 908 a silent adb here"
            echo "is the POSITIVE reading once the run is up. Confirm residence by the escape below -"
            echo "the log should show xnu_live_wdt_countdown moving (below the bark, never zero) and"
            echo "NO xnu_live_post_end_calls. Then restore p19 to get Android back."
            DO_RESCUE=1
        fi
    fi
fi

# --- 5. the escape: read the log out of verified TWRP -----------------------------------------
if (( DO_RESCUE )); then
    if (( DRY_RUN )); then
        echo "would run: (VolDown+Power if dark) -> sudo fastboot boot $TWRP_IMG -> adb exec-out 'cat /proc/last_kmsg' > $CAPTURE"
    else
        echo "== rescue: non-persistent TWRP, then read the RAM console =="
        if ! present; then
            if in_fastboot; then
                :
            else
                echo "The phone is dark or unreachable. It needs a physical press: VolDown + Power"
                echo "+ USB -> fastboot. Then re-run with --rescue. Nothing below will touch it until"
                echo "fastboot lists $SERIAL."
                exit 0
            fi
        fi
        if in_fastboot; then
            echo "booting $TWRP_IMG (non-persistent; writes nothing to any partition)"
            fb_call boot "$TWRP_IMG" || die "fastboot boot TWRP failed; the phone is still in fastboot and nothing was written"
            sleep 20
        fi
        for _ in $(seq 1 $(( CALL_TIMEOUT / 5 ))); do
            if adb_call exec-out 'cat /proc/last_kmsg' > "$CAPTURE.new" 2>/dev/null && [[ -s "$CAPTURE.new" ]]; then
                mv "$CAPTURE.new" "$CAPTURE"; break
            fi
            rm -f "$CAPTURE.new" 2>/dev/null || true
            sleep 5
        done
        if [[ -s "$CAPTURE" ]]; then
            echo "captured $CAPTURE (out of TWRP's /proc/last_kmsg - the RAM console survives a warm reset)"
            bash scripts/run_and_capture.sh --summarise "$CAPTURE" || true
        else
            echo "no log captured out of TWRP. Do NOT power-cycle yet if you want the RAM console:"
            echo "  sudo adb -s $SERIAL exec-out 'cat /proc/last_kmsg' > $CAPTURE"
        fi
    fi
fi

echo ""
echo "done. p19 holds the arm; the Android fallback from a plain power-on is GONE until p19 is"
echo "restored (scripts/restore_boot_from_xnu.sh --execute, from the before image this run kept)."