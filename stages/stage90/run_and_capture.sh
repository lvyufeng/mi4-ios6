#!/usr/bin/env bash
#
# Run the Stage90 payload on the device and capture its log, in one step.
#
# Why this exists: the gate (preflight_boot_check.sh) prints the commands to run, and the
# operator types them. That is fine for a script that gets exercised often, and poor for one
# that gets exercised a few times per session, at exactly the moments that matter most - the
# first boot after a hang, and every boot after it. A mistyped serial, a `fastboot flash`
# where a `fastboot boot` was meant, or capturing /proc/last_kmsg *after* something else
# rebooted the phone would each cost a run.
#
# So the whole cycle is one command. It never flashes, and it captures the log before doing
# anything else with the device.
#
# Usage:
#   ./run_and_capture.sh [gate flags...]        run the cycle
#   ./run_and_capture.sh --dry-run [flags...]   print the plan, touch nothing
#   ./run_and_capture.sh --summarise FILE       read an already-captured log
#
# Gate flags are passed straight through to preflight_boot_check.sh, so the run is refused
# unless the gate approves it. Any flags you would give the gate, give here.
#
# Exit status:
#   0  device came back and the log was captured
#   1  the gate refused, or the device was not found
#   2  the payload ran and the device did NOT come back - a manual power press is needed
#      (the log will not survive a power cycle, so this is also a lost run)
#   3  the device returned to the HOST but was not capturable over adb - the host's USB log
#      saw the phone enumerate again (and its SoC is running) while `adb devices` stayed
#      empty. **This is not a hang** and must not be read as one: it is an attempt to capture
#      a log from a device that came back into a state adb cannot reach. Added 2026-09-22,
#      when this phone's Android bring-up failed twice in ten minutes (`2717:0368` for 18 s,
#      one window ending before `4ee7`) and exit 2 would have been the wrong reading.

set -euo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
OUT=$REPO_ROOT/out/stage90
IMAGE=$OUT/stage90-qcdt.img

SERIAL=${SERIAL:-4a2fe00b}
LOGFILE=${LOGFILE:-/tmp/cancro-last_kmsg.txt}
RETURN_TIMEOUT=${RETURN_TIMEOUT:-180}

DRY_RUN=0
SUMMARISE_ONLY=""
GATE_FLAGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    # Summarise a log that was already captured - useful when the run happened in another
    # shell, or when re-reading one after the fact. Takes the path as the next argument.
    --summarise) SUMMARISE_ONLY=${2:-}; shift 2 ;;
    *) GATE_FLAGS+=("$1"); shift ;;
  esac
done

say() { printf '%s\n' "$*"; }
step() { printf '\n== %s ==\n' "$*"; }
die() { printf 'run_and_capture: %s\n' "$*" >&2; exit 1; }

# --- the return criterion, and why it is not `adb devices` alone -------------------------
#
# `adb devices` is what says *where* to capture the log from. It is not what says whether the
# device came back: this phone's Android bring-up is intermittent (2026-09-22, `2717:0368`
# with serial 4a2fe00b for 18 s at 19:22:45, dropped without reaching `18d1:4ee7`; two of the
# three recent `2717:0368` windows ended the same way), so a run that returns the SoC
# correctly while Android fails to come up leaves `adb devices` empty - and exit 2 is the one
# reading this phase cannot afford to get wrong.
#
# The host's own USB log is the independent reading, and it is a reading of the port *and the
# serial*: `usb 3-10` also carries a serial-less `05c6:f006` occupant, which today appeared
# on its own after 2 h 38 m of an empty port, so a port-only test would read that as a return.
# `SerialNumber: 4a2fe00b` is printed only for this phone.
serial_enum_count() {
  local n
  n=$(sudo dmesg 2>/dev/null | grep -c "SerialNumber: $SERIAL" 2>/dev/null) || true
  [[ $n =~ ^[0-9]+$ ]] && printf '%s' "$n"
}

summarise_log() {
  local log=$1
  local markers=(hw_watchdog_enabled hw_watchdog_counter_running "deadman: armed"
                 "apple_dt selftest ok" "exception"
                 "pc-sampling watchdog: rebooting after sample dump"
                 "platform_reboot entered")

  local n
  n=$(grep -a -c 'MI4IOS6_STAGE90' "$log" || true)
  say "MI4IOS6_STAGE90 lines: $n"
  if [[ $n -eq 0 ]]; then
    say "NONE. The log has no payload output from this run - either the power cycle that"
    say "brought the device back cleared DRAM, or the payload never reached log_init()."
  else
    grep -a -n 'MI4IOS6_STAGE90' "$log" | tail -60
  fi

  say ""
  say "key markers:"
  local m count
  for m in "${markers[@]}"; do
    count=$(grep -a -c "$m" "$log" || true)
    printf '  %-52s %s\n' "$m" "$count"
  done

  # Read the counts back out and say what they mean, so the operator does not have to hold
  # the mapping in their head at the moment they are looking at a fresh failure.
  local watchdog deadman abort
  watchdog=$(grep -a -c 'hw_watchdog_counter_running=0x00000001' "$log" || true)
  deadman=$(grep -a -c 'deadman: armed' "$log" || true)
  abort=$(grep -a -c 'MI4IOS6_STAGE90.*abort' "$log" || true)

  say ""
  say "reading:"
  if [[ $n -eq 0 ]]; then
    # No payload lines at all: there is nothing to read. Saying anything about the
    # watchdog here would be an inference from absence, which is the misattribution this
    # whole session has been correcting - an empty log means the log is missing, not that
    # the watchdog failed.
    say "  nothing to read - the log contains no payload output at all. Check that the"
    say "  device really rebooted with this payload, and that /proc/last_kmsg is the"
    say "  ram_console from that boot and not a stale one."
    return
  fi
  if [[ $watchdog -gt 0 ]]; then
    say "  hardware watchdog: ARMED and counting - the net was live for this run"
  else
    say "  hardware watchdog: NOT confirmed armed (no counter_running=1). If this run also"
    say "                     failed, the watchdog is the first thing to investigate."
  fi
  [[ $deadman -gt 0 ]] && say "  software dead-man: armed" \
                       || say "  software dead-man: not armed in this log"
  # `[[ ... ]] && say ...` was this function's last statement, and that is a defect, not a
  # style choice: when `abort` is 0 the list's status is 1, the function returns 1, and with
  # `set -e` the *caller* exits. The caller is `run_and_capture.sh`, which summarises the
  # previous log **before** it boots anything - so a log without an abort line made the script
  # print a summary and exit 1 without ever running the payload. Four consecutive exp-267
  # "runs" re-summarised the same stale log and reported nothing, and the run that looked
  # like a failed fix was a run that never happened. An `if` has status 0 on both branches.
  if [[ $abort -gt 0 ]]; then
    say "  an abort was logged - see the 'exception'/'abort' lines above"
  fi

  # --- the idle window's near end, and it prints only for a log that carries the pair -------------
  #
  # The gate is self-selecting: `xnu_live_slot_cwe_*` is published by `entry_window_note`, which is
  # called from the enter wrapper in every image built since 522 - so the block below appears when the
  # log came from one of those images and never for an earlier step's run. That is deliberate - a
  # verdict block that printed for every log would be a block whose criteria nobody re-derives, which
  # is how a stale check outlives its step.
  #
  # **It is no longer one arm's block.** When this was written the only image carrying the keys was
  # 522's, so the block could assert 522's shape; 533 keeps `entry_window_note` on purpose *because*
  # the pair is the reading that says which arm ran, and the two arms have opposite pairs (`_set` with
  # C set for 522's re-enable, C clear for 533's absence of it). So the pair is reported and each
  # arm's shape is named beside it, and FAIL is reserved for the one shape neither can produce. The
  # long note at clause (2) below has the measurement that made the difference visible; the change is
  # one of the two arms' expected readings no longer being printed as a failure.
  #
  # Every value is extracted with its *shape* asserted before it is compared as a number. A bare
  # `-gt` on a missing key reads as 0, and "the key is absent" and "the value is zero" are
  # different readings - one says the instrument never ran, the other says it ran and saw
  # nothing. This repository has been bitten by that distinction more than once, so the three
  # states are printed separately: PASS, FAIL, and UNREAD.
  if grep -a -q 'xnu_live_slot_cwe_' "$log"; then
    local cwe_win cwe_set cwe_calls pre_calls rtcab_calls post_calls storm panics user_ones
    local pop_death pop_named=0 arm_seen=unknown verdict_ok=1
    # `|| true` is load-bearing and its absence was this block's first defect, found by running it
    # against the state it is meant to refuse: the script sets `pipefail`, so a key that is absent
    # makes `grep` exit 1, the pipeline returns 1, and `set -e` kills the *caller* mid-function -
    # the same shape as the `[[ ... ]] && say ...` defect documented four lines above. An absent
    # key has to reach the code that can say "this is UNREAD", not end the summary.
    keyval() { grep -ao "xnu_live_$1=[0-9a-fx]*" "$log" 2>/dev/null | tail -1 | sed 's/^[^=]*=//' || true; }
    cwe_win=$(keyval slot_cwe_win)
    cwe_set=$(keyval slot_cwe_set)
    cwe_calls=$(keyval slot_cwe_calls)
    # **The three bracket publishers, because their *absence* is a localization rather than a gap.**
    # The exit wrapper calls `entry_slot_null_note(&g_slot_pre)`, `entry_slot_rtc_note(&g_slot_rtcab)`
    # and then, after the real exit returns, `entry_slot_null_note(&g_slot_post)` - and it ends in a
    # tail branch rather than returning, so all three are reached in the same pass or the pass died at
    # the one before. Each is called once per pass, and all three use the same publish schedule
    # (`entry_stubs.c:6236`: `n <= 4 || power of two`) behind the same `entry_live_ready()` gate, so at
    # pass n all three counters equal n: **if one published, the others would have published had they
    # been reached.** That is what turns "post_calls is missing" from UNREAD into the reading this run
    # exists to produce - and the three cases are the death's own address.
    pre_calls=$(keyval slot_pre_calls)
    rtcab_calls=$(keyval slot_rtcab_calls)
    post_calls=$(keyval slot_post_calls)
    storm=$(keyval sleh_storm)
    panics=$(grep -a -c 'panic.*sleh_abort' "$log" || true)
    user_ones=$(grep -a -c 'xnu_live_sleh_user=0x0*1' "$log" || true)
    # **The pop's own signature, and it is a literal read off the hardware rather than a
    # derivation.** 520's register dump carries it verbatim (`/tmp/cancro-last_kmsg.txt:4006`):
    #
    #     r12:  0xde58b701  sp: 0x8054fed0  lr: 0x800462dc  pc: 0x04b79074
    #
    # `lr = 0x800462dc` is the return address the exit's own `bl FlushPoU_Dcache` pushed at
    # `0x800462d8` - and the two `bl`s that could overwrite it before the pop
    # (`InvalidatePoU_Icache` at `0x80046304`, `flush_core_tlb` at `0x80046308`) are **skipped** in
    # this configuration, which 549 read out of this image's own boot-args rather than assumed.
    # `lr: *` because the dump's spacing is not uniform (`r10:`/`r11:` are single-spaced).
    #
    # **The pin is deliberate and its failure direction is noisy, not silent**: an exit that moves
    # in a later build stops matching, which lands on FAIL below and makes a human look. A shape
    # test that failed *quietly* on a moved address would be this project's most-paid-for defect
    # ([[mi4-measurement-defects]]), so do not "fix" this by loosening the match to `lr:` alone.
    pop_death=$(grep -a -c 'lr: *0x800462dc' "$log" || true)

    # The live channel writes its counters as `0x%08x`, so every numeric test below is a hex
    # pattern and the arithmetic is done on the `0x...` text - which bash's `$(( ))` reads. A
    # decimal-only pattern would have made every present key read as UNREAD, which is how this
    # block's first version behaved against a PASS-shaped log.
    say ""
    say "the idle window's near end, from the pair of SCTLR readings the entry wrapper publishes"
    say "and from the shape of the death when the log carries one. The pair says which arm this log"
    say "came from, and the two arms have opposite expected pairs; each arm's prediction is a shape"
    say "of death, not the death's absence (547 section 4). So a FAIL below means a shape that"
    say "neither arm's prediction names, not that one of them did not do what it promised."

    # (1) the verdict, read as the death's SHAPE rather than as its absence
    #
    # **This clause said "The arm's verdict is the panic's ABSENCE" and printed FAIL for any
    # `sleh_abort` panic - and that is the prediction of an arm this image is not.** 547 section 4
    # (`9675e82`, committed *before* the run) predicts for the enable-off cell exactly the
    # opposite: "the pass reaches the exit, the push runs with `C` = 0, the pop loads the stale
    # words, the prefetch abort panics at `pc = the popped value & ~1`, and the device comes back
    # on XNU's `MACH Reboot`". 533 section 5.2 reads the same three-note pattern as "**inside
    # `platform_cache_idle_exit`** - the `pop`", and 533 section 5.4 lists "whether Apple's own
    # panic path runs" as an *item to read*. So as written this clause scored the pre-registered
    # prediction as the arm's failure, on the frozen arm, in the one file that reads the result -
    # clause (2)'s defect one clause over, and it was fixed there first.
    #
    # What separates the two is `pop_death` above, and it is the *only* new machinery: no arm
    # variable, because whether this log is the enable-off cell is what clause (2) prints.
    if [[ $panics -eq 0 ]]; then
      say "  PASS  no 'panic ... sleh_abort' in the log - the idle exit retired its own epilogue"
      say "        rather than dying at its pop, which is past the frontier this phase has been"
      say "        measuring. 547 section 4's third row is the reading for that: 546's mechanism"
      say "        did not fire in this cell, so 535 must not be built as designed"
    elif [[ $pop_death =~ ^[0-9]+$ ]] && (( pop_death >= 1 )); then
      say "  PREDICTED  $panics 'panic ... sleh_abort' record(s) and the dump carries"
      say "        lr: 0x800462dc - the exit's own pop {fp, pc}, which is 547 section 4's prediction"
      say "        for the enable-off cell and *not* a failed arm. It is still not progress: the boot"
      say "        restarts on MACH Reboot instead of surviving the idle pass, which is 535's job"
      pop_named=1
    else
      say "  FAIL  $panics 'panic ... sleh_abort' record(s) and the dump does not carry"
      say "        lr: 0x800462dc (matches: ${pop_death:-unread}): a death of a shape that neither"
      say "        547 section 4 nor 533 section 5 names, so it is a new fault and not this arm's"
      say "        reading. (521's non-return said the flush is not the answer and 526's took the"
      say "        capture out; a panic here says the cache state is not it either - which leaves the"
      say "        enter wrapper's own store in the window, 522's addition, as the next thing to"
      say "        bisect)"
      verdict_ok=0
    fi

    # (2) the pair, read as a reading rather than as 522's verdict
    #
    # **This clause asserted 522's arm as the only correct one, and 533 is the arm that is not 522.**
    # 522 re-enables SCTLR.C at the window's near end, so its pair is `_win` clear and `_set` *set*;
    # 533 leaves the window as Apple left it, so its pair is **both clear**. Written as "FAIL unless
    # `_set` has C set" - which is what this block said until 533 - the reader would have printed
    # `FAIL slot_cwe_set=...: C is not set on the far side of the call, so the re-enable did not
    # happen` for 533's *correct* image, naming an instruction that is not in it. That is this step's
    # own recurring defect (a check whose shape was copied from the previous arm and never re-read
    # against this one) in the one file that reads the run's result, so 533's doc section 5 and this
    # block disagreed about what 533's log should contain.
    #
    # The arm this log came from is *not* decidable from anything else in the log - the entry image
    # publishes no build marker, only its readings - so the pair is reported for what it is and each
    # arm's shape is named beside it. FAIL is kept for the one shape no arm can produce: `_win` with
    # C *set*, which would say the window was never open where the image claims to read it.
    if [[ $cwe_calls =~ ^0x[0-9a-f]+$ ]] && (( cwe_calls >= 1 )); then
      if [[ $cwe_win =~ ^0x[0-9a-f]+$ ]] && (( (cwe_win & 4) == 0 )); then
        say "  PASS  slot_cwe_win=$cwe_win has SCTLR.C clear - the window really opened where the"
        say "        image says it does, which is what makes the second reading worth anything"
        if [[ $cwe_set =~ ^0x[0-9a-f]+$ ]] && (( (cwe_set & 4) == 4 )); then
          say "  ARM   522's arm: slot_cwe_set=$cwe_set has C set - the near-end re-enable ran and"
          say "        took, so this log is an image that re-enables the D-cache at the window's end"
          arm_seen=522
        elif [[ $cwe_set =~ ^0x[0-9a-f]+$ ]] && (( (cwe_set & 4) == 0 )); then
          say "  ARM   533's arm: slot_cwe_set=$cwe_set has C clear - the window is left exactly as"
          say "        Apple left it, and *this* is that arm's expected reading, not a failed enable"
          arm_seen=533
        else
          say "  UNREAD  slot_cwe_set=${cwe_set:-absent} is not a readable SCTLR - the pair is half"
          say "          a reading and which arm ran is not decidable from it"
          verdict_ok=0
        fi
        say "        (entry_window_note ran $cwe_calls time(s) in the passes this log recorded)"
      else
        say "  FAIL  slot_cwe_win=${cwe_win:-absent}: C was NOT clear there, so the window was not"
        say "        open where the image claims - neither arm's second reading means anything"
        verdict_ok=0
      fi
    else
      say "  UNREAD  slot_cwe_calls=${cwe_calls:-absent} is not a count >= 1: the pair's keys are in"
      say "          this log but the note's own count is not readable, so nothing is claimed about"
      say "          which arm this log came from"
      verdict_ok=0
    fi

    # (3) where the pass died, from which of the wrapper's three bracket publishers got out
    #
    # The counts are `<= 4 || power of two` (see the keyval note above), so a printed 4 means *at least*
    # four and possibly 5-7 - the number is the largest published count, not the total (measurement
    # defect 406, a counter published on a schedule read as a total). Nothing below uses it as a total;
    # it is used only to answer "did this site publish at all", which the schedule cannot mislead.
    if [[ $post_calls =~ ^0x[0-9a-f]+$ ]] && (( post_calls >= 1 )); then
      say "  PASS  slot_post_calls=$post_calls - the exit returned through the wrapper, which"
      say "        520's run never did (its pass died inside the call)"
    elif [[ $pre_calls =~ ^0x[0-9a-f]+$ ]] && (( pre_calls >= 1 )) \
      && [[ $rtcab_calls =~ ^0x[0-9a-f]+$ ]] && (( rtcab_calls >= 1 )); then
      say "  DIED IN THE EXIT  pre_calls=$pre_calls and rtcab_calls=$rtcab_calls both published and"
      say "        slot_post_calls did not: the pass reached the wrapper, took the rtcPop reading and"
      say "        got as far as the call, and did not come back through it - so the death is inside"
      say "        platform_cache_idle_exit, which is 520's pop {fp, pc} at the same pc. That is a"
      say "        localization and not a missing reading: the three notes share one schedule and one"
      say "        gate, so a site that published proves the later ones were reachable."
      if [[ $pop_named -eq 1 ]]; then
        say "        And it agrees with clause (1): the dump's lr: 0x800462dc puts the fault at that"
        say "        pop, which is 547 section 4's prediction for the enable-off cell - so the two"
        say "        clauses localize the same instruction, and this is not scored as a failure"
      else
        say "        **and clause (1) did not find the pop's own lr in the dump**, so this pass died"
        say "        inside the call but somewhere other than the instruction 547 section 4 names -"
        say "        that is a new fault, not this arm's prediction arriving"
        verdict_ok=0
      fi
    elif [[ $pre_calls =~ ^0x[0-9a-f]+$ ]] && (( pre_calls >= 1 )); then
      say "  DIED BEFORE THE EXIT  pre_calls=$pre_calls published but the rtcPop reading did not, so"
      say "        the pass died between the two - earlier than 520's death and a different fault"
      verdict_ok=0
    else
      say "  UNREAD  none of the wrapper's three bracket publishers is in the log (pre_calls,"
      say "          rtcab_calls, post_calls all absent), so this log cannot say where the pass died."
      say "          The likely causes are a last_kmsg ring that wrapped past them and a run that"
      say "          never reached the idle exit at all."
      verdict_ok=0
    fi

    # (4) progress: the boot got past the idle loop rather than dying in it
    say ""
    if [[ $user_ones =~ ^[0-9]+$ ]] && (( user_ones > 3 )); then
      say "  progress: $user_ones user-mode fault record(s) - 520 had 3 (0x1118, 0x1124, 0x11a4),"
      say "            so this boot got further into user mode than any run has"
    else
      say "  progress: ${user_ones:-0} user-mode fault record(s) - 520 had 3; not more than that"
      say "            means the boot did not get further than it already had"
    fi
    if [[ $storm =~ ^0x[0-9a-f]+$ ]] || [[ $storm =~ ^[0-9]+$ ]]; then
      say "            xnu_live_sleh_storm=$storm (520's fatal run ended at 9)"
    elif [[ -z $storm ]]; then
      say "            xnu_live_sleh_storm absent - no abort storm was recorded at all"
    fi
    if [[ $pop_named -eq 1 ]]; then
      say "  => the arm's prediction arrived: the death is the exit's own pop, at the instruction"
      say "     547 section 4 names, with a log and (per the runner's exit code, which is not this"
      say "     function's reading) a return. What did *not* happen: the boot still does not survive"
      say "     the idle pass, so 535's PoC flush behind 0x800462d8 is still the next step. This"
      say "     block is not the reading that decides the arm; the runner's exit code is (551: exit 3"
      say "     means the SoC returned into a state adb cannot reach)."
      # The pair from clause (2) is the only thing in the log that says which cell this is, and
      # 547 section 4's prediction is not for both cells - so the two are put side by side here
      # rather than left for the reader to join up.
      case $arm_seen in
        533)
          say "     And clause (2) reads the pair as 533's arm, which is the cell 547 section 4's"
          say "     prediction is for: the two agree." ;;
        522)
          say "     **But clause (2) reads the pair as 522's arm, which 547 section 4 does not"
          say "     predict this for**: 522's cell is an enable-on row and 547 section 3 has both of"
          say "     those rows dying with *no log at all*, so a log here is itself the surprise. Treat"
          say "     this as unread against 547 and re-derive which image ran before reading anything"
          say "     else in this block." ;;
        *)
          say "     Clause (2) could not read the pair, so which cell this log is stays open - and"
          say "     547 section 4's prediction is the enable-off cell's." ;;
      esac
    elif [[ $verdict_ok -eq 1 ]]; then
      say "  => the three checks pass: no panic, the window opened and its pair is readable, and the"
      say "     exit returned through the wrapper. The ARM line above says which of the two arms"
      say "     this log came from; item (4) says how much boot happened after the exit, and that is"
      say "     the next step's question - not this one's. And if this log is the enable-off arm, a"
      say "     genuine return here is 547 section 4's third row: 546's mechanism did not fire."
    else
      say "  => at least one check above is FAIL or UNREAD. The criteria are 547 section 4's shape"
      say "     (a panic *at the exit's pop* is PREDICTED for the enable-off cell - its absence, or a"
      say "     panic of another shape, is the news) and 533 section 5's readings, so read which line"
      say "     failed before concluding anything about the arm."
    fi
  fi
}

if [[ -n $SUMMARISE_ONLY ]]; then
  step "summarising $SUMMARISE_ONLY"
  [[ -f $SUMMARISE_ONLY ]] || die "no such log: $SUMMARISE_ONLY"
  summarise_log "$SUMMARISE_ONLY"
  say ""
  say "done."
  exit 0
fi

step "payload output"
if [[ $DRY_RUN -eq 1 ]]; then
  say "would summarise $LOGFILE (see --summarise)"
else
  summarise_log "$LOGFILE"
fi

say ""

# --- 1. the gate, with whatever flags the caller gave ------------------------------------
step "gate"
if [[ $DRY_RUN -eq 1 ]]; then
  say "would run: ./preflight_boot_check.sh ${GATE_FLAGS[*]:-}"
  "$STAGE_DIR/preflight_boot_check.sh" "${GATE_FLAGS[@]}" || die "the gate refused"
else
  "$STAGE_DIR/preflight_boot_check.sh" "${GATE_FLAGS[@]}" || die "the gate refused"
fi

# --- 2. is the device where we expect it? ------------------------------------------------
step "device"
if [[ $DRY_RUN -eq 1 ]]; then
  say "would look for serial $SERIAL via adb or fastboot"
else
  if sudo adb devices 2>/dev/null | grep -q "^$SERIAL"; then
    MODE=adb
  elif sudo fastboot devices 2>/dev/null | grep -q "^$SERIAL"; then
    MODE=fastboot
  else
    die "serial $SERIAL not found in adb or fastboot. If the device is dark, it needs a
       power press: hold Power ~10-15 s, release, then press Power normally."
  fi
  say "found $SERIAL in $MODE"
fi

# --- 3. boot the payload ----------------------------------------------------------------
step "boot"
say "sudo adb -s $SERIAL reboot bootloader   # then fastboot boot, never flash"
if [[ $DRY_RUN -eq 0 ]]; then
  if [[ $MODE == adb ]]; then
    sudo adb -s "$SERIAL" reboot bootloader
    for _ in $(seq 1 30); do
      sudo fastboot devices 2>/dev/null | grep -q "^$SERIAL" && break
      sleep 2
    done
  fi
  sudo fastboot devices 2>/dev/null | grep -q "^$SERIAL" \
    || die "device did not appear in fastboot"

  # `fastboot boot` writes nothing to storage. This is the whole safety property of the
  # workflow, so it is a separate line that is never generated from a variable.
  sudo fastboot boot "$IMAGE"
fi

# --- 4. wait for it to come back --------------------------------------------------------
step "waiting up to ${RETURN_TIMEOUT}s for the device to return"
say "(a bounded self-test should return on its own; a hang will not)"
RETURNED=0
RETURN_HOW=""
ENUM_BEFORE=$(serial_enum_count)
ENUM_BEFORE=${ENUM_BEFORE:-UNREAD}
if [[ $DRY_RUN -eq 0 ]]; then
  say "(return criterion: serial $SERIAL in \`adb devices\`, or a new \`SerialNumber: $SERIAL\`"
  say " enumeration in the host log - adb alone is not it, see the note at serial_enum_count)"
  [[ $ENUM_BEFORE == UNREAD ]] && say "(the host log is unreadable here, so only adb can speak)"
  for _ in $(seq 1 $((RETURN_TIMEOUT / 3))); do
    if sudo adb devices 2>/dev/null | grep -q "^$SERIAL"; then
      RETURNED=1; RETURN_HOW="adb"; break
    fi
    if [[ $ENUM_BEFORE != UNREAD ]]; then
      _now=$(serial_enum_count)
      if [[ -n $_now && $_now -gt $ENUM_BEFORE ]]; then
        RETURNED=1; RETURN_HOW="host log"; break
      fi
    fi
    sleep 3
  done
fi

if [[ $DRY_RUN -eq 0 && $RETURNED -eq 0 ]]; then
  ENUM_AFTER=$(serial_enum_count)
  ENUM_AFTER=${ENUM_AFTER:-UNREAD}
  say ""
  say "bounded wait expired after ${RETURN_TIMEOUT}s. Evidence:"
  say "  adb:      serial $SERIAL not listed"
  say "  host log: $ENUM_BEFORE -> $ENUM_AFTER enumeration(s) of SerialNumber: $SERIAL"
  if [[ $ENUM_BEFORE != UNREAD && $ENUM_AFTER != UNREAD && $ENUM_AFTER -gt $ENUM_BEFORE ]]; then
    say ""
    say "REFUSING to call this a non-return: the host log shows the phone enumerating again"
    say "after the boot, so the device DID come back - it came back into a state adb cannot"
    say "reach, which is a capture failure and not a hang. Do not read this as exit 2."
    say "The log lives in the top of DRAM and survives until a power cycle, so if the phone"
    say "settles into Android, re-read it with:"
    say "  sudo adb -s $SERIAL exec-out 'cat /proc/last_kmsg' > $LOGFILE"
    exit 3
  fi
  if [[ $ENUM_BEFORE == UNREAD || $ENUM_AFTER == UNREAD || $ENUM_AFTER -lt $ENUM_BEFORE ]]; then
    say ""
    say "UNREAD: the host log could not be compared, so this says the device did not return"
    say "to *adb*, and it does not say whether it returned to the host. Check by hand:"
    say "  sudo dmesg | grep 'usb 3-10'   # the phone is usb 3-10, serial $SERIAL"
    say "The reading is: an enumeration on that port after the fastboot disconnect, with the"
    say "SoC reset behind it, is a return whatever adb said; a single dead second in fastboot"
    say "with nothing after it is a hang."
    [[ $ENUM_BEFORE != UNREAD && $ENUM_AFTER != UNREAD ]] && say "  (a *fall* in the count - $ENUM_BEFORE -> $ENUM_AFTER - is the dmesg ring buffer rotating,"
    [[ $ENUM_BEFORE != UNREAD && $ENUM_AFTER != UNREAD ]] && say "   which is not evidence of absence; the two counts are not comparable)"
    exit 2
  fi
  say ""
  say "The device did not come back: no adb entry, and no new enumeration in the host log."
  say "It needs a power press: hold Power ~10-15 s, release, press Power normally."
  say "Do NOT power-cycle before considering this: the payload's log lives in the top of"
  say "DRAM and is lost on a cold boot, so a power cycle also loses whatever the run"
  say "produced. If the payload armed its watchdog, the reboot that returns the device"
  say "would have preserved the log - so a failure to return means the log is likely"
  say "unrecoverable anyway, but waiting is free and power-cycling is not."
  exit 2
fi

# --- 5. capture, before anything else touches the device --------------------------------
step "capturing the payload log"
if [[ $DRY_RUN -eq 1 ]]; then
  say "would run: sudo adb -s $SERIAL exec-out 'cat /proc/last_kmsg' > $LOGFILE"
else
  # **The `rm` is not tidiness; it is what makes the redirect work at all on this host.** The log
  # usually exists already, and it is usually owned by the invoking user - because the natural way to
  # re-capture by hand is `sudo adb ... > /tmp/cancro-last_kmsg.txt`, where the *shell* does the
  # redirect and so the file belongs to whoever typed it. `sudo` cannot then reopen that file, and the
  # three observations that pin it down are these: root's `O_CREAT` on the existing file is refused
  # (`Permission denied`), root's `rm -f` on the same file succeeds, and root's creation of a file that
  # does not exist yet succeeds - which is the signature of a sticky `/tmp` with
  # `fs.protected_regular` and a root that has no `CAP_FOWNER` to override it. (The sysctl itself is
  # not readable from this container; the three commands are.) 511's first run died here *after* the
  # boot, which is the worst place for it - the run is spent and the log is only reachable until the
  # device takes its next boot. Removing the file first makes the redirect a creation, which needs only
  # write permission on the directory.
  #
  # **And the removal has to be tried as both identities, because the file can be owned by either.**
  # 511's failure was the invoking user's file and root's refused `O_CREAT`; 512's was the mirror of
  # it - the file was *root's*, from a hand re-capture under `sudo`, and the invoking user's unlink was
  # refused with `Operation not permitted` and exit 1 from `rm`, which under `set -e` ended the script
  # after the boot had already been spent. The same sticky-`/tmp` rule produces both spellings:
  # whichever identity does not own the file is the one that cannot remove it here. So neither is
  # assumed - the plain `rm` is tried first (it succeeds whenever the file is the user's, which is
  # what the redirect below leaves behind) and `sudo`'s is the fallback.
  rm -f "$LOGFILE" 2>/dev/null || sudo rm -f "$LOGFILE" || \
    die "could not remove $LOGFILE - neither the invoking user nor root can unlink it, and the redirect would then be reopening someone else's file"
  sudo adb -s "$SERIAL" exec-out 'cat /proc/last_kmsg' > "$LOGFILE" || \
    die "could not read /proc/last_kmsg"
  say "wrote $(wc -c < "$LOGFILE") bytes to $LOGFILE"

  # --- the capture is checked before the run is read as a result ------------------------
  #
  # The payload's own lines are the only thing that says this log is *this* run's. Experiment
  # 473's first capture was 455596 bytes whose first 41 characters were the payload's banner
  # and whose remaining 455525 were binary: a stale region that held nothing this payload ever
  # wrote, and whose *length* was the same as the valid capture's - so neither the byte count
  # nor the presence of the banner's first characters tells the two apart. The count of lines
  # the payload owns does: an early death still writes dozens of them, because the banner and
  # the payload's first report are the same code path.
  #
  # Reading /proc/last_kmsg is a read, so repeating it touches nothing and cannot cost a run;
  # and if it is still suspect after three reads the log is *named* suspect rather than read.
  for _attempt in 1 2 3; do
    _payload_lines=$(grep -a -c '^MI4IOS6_STAGE90' "$LOGFILE" || true)
    if (( _payload_lines >= 8 )); then
      break
    fi
    say "  WARNING: $LOGFILE holds $_payload_lines line(s) of the payload's own output, fewer"
    say "           than any run of this payload produces - re-reading /proc/last_kmsg"
    sleep 5
    sudo adb -s "$SERIAL" exec-out 'cat /proc/last_kmsg' > "$LOGFILE" || \
      die "could not read /proc/last_kmsg"
    say "  re-read $(wc -c < "$LOGFILE") bytes"
  done
  if (( $(grep -a -c '^MI4IOS6_STAGE90' "$LOGFILE" || true) < 8 )); then
    say "  WARNING: after three reads $LOGFILE still holds fewer than 8 payload lines. Read"
    say "           this as 'the payload wrote almost nothing', NOT as 'the payload ran and"
    say "           produced this' - and repeat the run before drawing a conclusion from it."
  fi
fi

# The marker table is the part that decides what a run *meant*, so it is a function with
# its own entry point (--summarise) rather than inline: it can then be tested against
# synthetic logs, and re-run on a log captured elsewhere, without touching the device.
say "done. Full log: $LOGFILE"
