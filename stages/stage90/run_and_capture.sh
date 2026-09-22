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

  # --- 522's verdict, and it prints only for a log that carries 522's own keys ----------------
  #
  # The gate is self-selecting: `xnu_live_slot_cwe_*` exists in exactly one image built in this
  # project (522's), so the block below appears when the log came from that image and never for
  # another step's run. That is deliberate - a verdict block that printed for every log would be
  # a block whose criteria nobody re-derives, which is how a stale check outlives its step.
  #
  # Every value is extracted with its *shape* asserted before it is compared as a number. A bare
  # `-gt` on a missing key reads as 0, and "the key is absent" and "the value is zero" are
  # different readings - one says the instrument never ran, the other says it ran and saw
  # nothing. This repository has been bitten by that distinction more than once, so the three
  # states are printed separately: PASS, FAIL, and UNREAD.
  if grep -a -q 'xnu_live_slot_cwe_' "$log"; then
    local cwe_win cwe_set cwe_calls post_calls storm panics user_ones verdict_ok=1
    # `|| true` is load-bearing and its absence was this block's first defect, found by running it
    # against the state it is meant to refuse: the script sets `pipefail`, so a key that is absent
    # makes `grep` exit 1, the pipeline returns 1, and `set -e` kills the *caller* mid-function -
    # the same shape as the `[[ ... ]] && say ...` defect documented four lines above. An absent
    # key has to reach the code that can say "this is UNREAD", not end the summary.
    keyval() { grep -ao "xnu_live_$1=[0-9a-fx]*" "$log" 2>/dev/null | tail -1 | sed 's/^[^=]*=//' || true; }
    cwe_win=$(keyval slot_cwe_win)
    cwe_set=$(keyval slot_cwe_set)
    cwe_calls=$(keyval slot_cwe_calls)
    post_calls=$(keyval slot_post_calls)
    storm=$(keyval sleh_storm)
    panics=$(grep -a -c 'panic.*sleh_abort' "$log" || true)
    user_ones=$(grep -a -c 'xnu_live_sleh_user=0x0*1' "$log" || true)

    # The live channel writes its counters as `0x%08x`, so every numeric test below is a hex
    # pattern and the arithmetic is done on the `0x...` text - which bash's `$(( ))` reads. A
    # decimal-only pattern would have made every present key read as UNREAD, which is how this
    # block's first version behaved against a PASS-shaped log.
    say ""
    say "522's arm - the idle window's D-cache is re-enabled at its near end. The verdict is"
    say "the panic's ABSENCE; the other three say whether the enable did what the image claims."

    # (1) the verdict
    if [[ $panics -eq 0 ]]; then
      say "  PASS  no 'panic ... sleh_abort' in the log - the idle exit retires its own epilogue"
    else
      say "  FAIL  $panics 'panic ... sleh_abort' record(s) - the death 519 and 520 died is back"
      say "        (521's non-return said the flush is not the answer; a panic here says the"
      say "        cache state is not either, and the next arm is the null instrument)"
      verdict_ok=0
    fi

    # (2) the enable ran inside the window and really took
    if [[ $cwe_calls =~ ^0x[0-9a-f]+$ ]] && (( cwe_calls >= 1 )); then
      if [[ $cwe_win =~ ^0x[0-9a-f]+$ ]] && (( (cwe_win & 4) == 0 )); then
        say "  PASS  slot_cwe_win=$cwe_win has SCTLR.C clear - the window was open when the"
        say "        wrapper read it, which is what makes the enable a change of state"
      else
        say "  FAIL  slot_cwe_win=${cwe_win:-absent}: C was NOT clear there, so the window was"
        say "        not open where the image claims and this arm tested nothing"
        verdict_ok=0
      fi
      if [[ $cwe_set =~ ^0x[0-9a-f]+$ ]] && (( (cwe_set & 4) == 4 )); then
        say "  PASS  slot_cwe_set=$cwe_set has SCTLR.C set - the write took"
      else
        say "  FAIL  slot_cwe_set=${cwe_set:-absent}: C is not set on the far side of the call,"
        say "        so the re-enable did not happen"
        verdict_ok=0
      fi
      say "        (the enable ran $cwe_calls time(s) in the passes this log recorded)"
    else
      say "  UNREAD  slot_cwe_calls=${cwe_calls:-absent} is not a count >= 1: 522's keys are in"
      say "          this log but the enable's own count is not readable, so nothing is claimed"
      say "          about whether the write took"
      verdict_ok=0
    fi

    # (3) the exit *returned* through the wrapper - 520's run died inside the call (its count was 0)
    if [[ $post_calls =~ ^0x[0-9a-f]+$ ]] && (( post_calls >= 1 )); then
      say "  PASS  slot_post_calls=$post_calls - the exit returned through the wrapper, which"
      say "        520's run never did (its pass died inside the call)"
    elif [[ -n $post_calls ]]; then
      say "  FAIL  slot_post_calls=$post_calls: the exit did not return through the wrapper in"
      say "        this log"
      verdict_ok=0
    else
      say "  UNREAD  slot_post_calls is absent, so whether the exit returned is not readable"
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
    if [[ $verdict_ok -eq 1 ]]; then
      say "  => all three checks pass: this is the first log in the walk where the idle exit"
      say "     completed. Item (4) says how much boot happened after it, and that is the next"
      say "     step's question - not this one's."
    else
      say "  => at least one check above is FAIL or UNREAD; read the verdict line (1) first,"
      say "     because the arm's prediction is the panic's absence and nothing else."
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
if [[ $DRY_RUN -eq 0 ]]; then
  for _ in $(seq 1 $((RETURN_TIMEOUT / 3))); do
    if sudo adb devices 2>/dev/null | grep -q "^$SERIAL"; then
      RETURNED=1
      break
    fi
    sleep 3
  done
fi

if [[ $DRY_RUN -eq 0 && $RETURNED -eq 0 ]]; then
  say ""
  say "The device did NOT come back within ${RETURN_TIMEOUT}s."
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
