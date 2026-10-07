#!/usr/bin/env bash
# 909 — the RESIDENCE press: stage the residence arm into p19, boot it with a PLAIN power-on, and
# read whether XNU STAYS. Same staging and escape as 908 (scripts/press_908_normal.sh); what is
# new is what the arm does and therefore what the operator must do.
#
# WHY 908 NEEDED A 909, AND WHY 909's FIRST ARM NEEDED A FIX. 908 proved a plain power-on ENTERS the
# OS — XNU, `BSD root: md0`, pid 1 parked in poll — and then ENDED: the image's own deliberate
# ending (`entry_seam_end_run`, `xnu_live_post_end_calls=4`) stored `RESTART_REASON` and faulted at
# `0xfa0065c`. 909 REMOVES that ending (`STAGE90_XNU_RESIDENT=1` => POST_END_TICKS/POST_END_RUN/
# SEAM_END_RUN all 0) and PETS the armed SoC watchdog (`entry_wdt_pet`, once per idle pass), so the
# run does not end on its own clock either — it is the rung that makes 「彻底能直接开机就运行xnu」 a
# boot that STAYS. **Its first arm, `armed-storage-e61ce673`, was PRESSED and faulted at the pet on
# the pet's first idle pass** (`fault_addr=0x0, pc=entry_mmio_section+0x94, r1=0xf9017000`) because
# the pet passed `0, 0` to `entry_mmio_section` and `entry_section_install` stores `*slot_before_out`
# BEFORE its refusal test — so no `xnu_live_wdt_*` key was ever published. **This script names the
# FIXED arm `armed-storage-104b10ce`** (the pet now passes two real outputs); the fix, the guard's
# three new clauses and the press are record d622e7f and experiment-909's R7/R8.
#
# WHY ARM 2 NEEDED ARM 3. 104b10ce's pet install was REFUSED on every call (`xnu_live_wdt_map=0x0`
# x4, no other `xnu_live_wdt_*` key): `entry_mmio_section` installs a 1 MB SECTION indexed by
# `va >> 20`, and the GIC probe already owns megabyte `0xf90` (`0xf9000000`), the SAME megabyte as
# the watchdog `0xf9017000` - so the slot is occupied and the install returns 0.  **This script now
# names the arm-3 arm `armed-storage-a703257d`**, whose pet falls back to reading the watchdog
# THROUGH the GIC's own block at the same VA, guarded by `wdt_desc_maps_the_block(slot_before)` (a
# predicate over the descriptor the install copied out before its refusal test).  It publishes a new
# key `xnu_live_wdt_via` (1 = the pet's own install, 2 = the GIC's block) and the guard gained
# `claim_descriptor_guard_is_a_real_predicate` plus five mutations in `tools/test_resident_guard.py`.
# The fix and the guard are this arm; the press of arm 3 is still owed.
#
# WHY ARM 5 NEEDED AN ARM 6, AND WHY ARM 6 IS THE RESIDENCE FIX ITSELF. Arm 5 is an *instrument* arm: one
# press bisects the 5th idle pass five ways (see its own block below). Arm 6 (`armed-storage-cabba670`) is
# the goal-directed arm and it is arm 5 + ONE switch, `STAGE90_XNU_IDLE_NO_SLEEP=1`. Arm 2's log put the
# death INSIDE the 5th `platform_cache_idle_enter` - the cache-off window (`platform_cache_disable()` +
# `CleanPoU_Dcache()`, `IDLE_CACHE_ENABLE=0`) or its `wfi` - and that is EXACTLY the window 594's switch
# removes: with it, `cpu_signal_handler_internal(FALSE)` is not called, `SIGPdisabled` stays set,
# `cpu_idle`'s first test is true on every pass, the idle leaves by door 1, and
# `platform_cache_idle_enter`/`wfi`/`exit` are NEVER entered. 594's own note says it: *a port whose idle
# never sleeps is a port that reached the OS and stayed*. **The pet's site has to move with it.** On arms
# 1-5 the pet was the idle-EXIT wrapper's tail; on this arm that wrapper is skipped, so the pet is now a
# call in `__wrap_Idle_load_context` - the one wrapper `machine_idle` and BOTH of `cpu_idle`'s first-door
# bodies reach, so it is entered once per pass - placed BEFORE `__real_Idle_load_context`, which is
# `noreturn`, so a pet after it would never run. **What the operator must do is the same as arm 5's plain
# boot; what is new is that this arm, if it holds, is a boot that STAYS** - so a dark screen with no adbd
# is the POSITIVE reading, and the wait before the escape is the experiment, not a timeout.
#
# WHY ARM 4 NEEDED AN ARM 5, AND WHY ARM 5 IS A 5-WAY BISECTION. Arm 4 (`armed-storage-4f4111fa`) widened only
# `entry_slot_publish`'s bound, so its press can say which side of the exit the 5th pass died on and no
# more. Reading arm 2's log tells more than that: `xnu_live_idlestack_calls` is the idle ENTER wrapper's
# count, published UNCONDITIONALLY with the D-cache on, so `=5` is a HARD ceiling (the live channel was
# only 72% full, 11797 of 16384 records, so this is not truncation) - the idle path was entered exactly
# five times and never a sixth. And in FILE ORDER the 5th pass's only record is that enter's `=5`: there
# is NO `wfi` record for the 5th pass (the last `wfi`, `after=0x0f48ec3f`, belongs to pass 4). So the
# death is INSIDE the 5th `platform_cache_idle_enter` (the cache-off window) or its `wfi` - NOT at the
# exit. Arm 4's single widening cannot bisect that, because `entry_note_pce`/`_wfi`/`_pcx`/
# `_pce_after` publish only on powers of two, and 5 is not one. **Arm 5 (`armed-storage-b459a858`) widens those four
# idle-path gates too** (one macro, `STAGE90_IDLE_PATH_LIVE_MAX 8u`), so ONE press reads, in file order:
# `idlestack=5` (always) -> `pce_seq=5` (the 5th enter ran) -> `wfi_seq=5` (the 5th halt, with
# `before`/`after` = the whole window) -> `pcx_seq=5` (the real exit returned) -> `slot_post_calls=5`
# (the far side of the `pop`). **The LAST of these that appears names where the pass died**, and a
# present `wfi` with a `before` and no `after` is a death AT the halt. It keeps arm 4's 8u slot bound
# and arm 3's watchdog fallback, so this one press answers the location AND the watchdog question.
#
# WHY ARM 3'S FIX IS FOLDED INTO ARM 4, AND WHAT ARM 4 ADDS. Arm 3 was built and parked but NEVER
# pressed, so arm 4 keeps its watchdog fallback unchanged (`xnu_live_wdt_via`) and adds the second
# question the same press can answer: **where the run's 5th idle pass dies.** Arm-2's own log has
# `xnu_live_idlestack_calls=0x5` (the idle *enter* wrapper's count, published unconditionally with
# the D-cache on, so it is the true count) against `xnu_live_slot_post_calls=0x4` (the *exit*
# wrapper's, published on `entry_slot_publish`'s schedule). The boot entered a 5th idle pass and never
# left it - and on arm 2/3 that pass is invisible to every scheduled site, because 5 is neither `<= 4`
# nor a power of two. Arm 4's only source change is `STAGE90_SLOT_LIVE_MAX 4u -> 8u`, so the exit
# wrapper's own sites now publish count 5. **This script names the arm-4 arm `armed-storage-4f4111fa`**
# (the entry bin's own hash prefix), whose press answers both: `xnu_live_wdt_via`/`_countdown` for the
# watchdog, and `xnu_live_slot_post_calls` (=5 the 5th pass reached the exit tail and died in the
# pcx/pet gap; =4 it died at or before the real `platform_cache_idle_exit`).
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
EXPECT_ARM="armed-storage-cabba670"
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
# and it refuses if the live set is not `armed-storage-cabba670` (the arm this script names). It is
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