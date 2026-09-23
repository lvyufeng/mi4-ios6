#!/bin/bash
# rehearse_live_path.sh - drive run_and_capture.sh's LIVE path through every state it can
# reach, with no device, and refuse if any state's behaviour differs from the expectation.
#
# WHY THIS EXISTS. The live path is the one that spends the press: it gates, boots, waits and
# captures, and a bug in it costs the scarcest thing this phase has. Reading it is not enough -
# 601 found a real defect in `summarise_log` (`eleven grep errors and a blank count, on the state
# the device is actually in`) that only appeared when the path was *run*, and `--summarise` cannot
# reach that state at all because the CLI refuses a missing path before summarise_log is called.
# Every reading this phase has taken with `--summarise` ran on a file that existed.
#
# So the states below are the ones that cost something if they are wrong, and each is driven end to
# end: the gate, the device detect, the boot, the return wait, the capture, and the reading.
#
# SAFETY, BY CONSTRUCTION. This script never touches a device and cannot:
#   * `sudo`, `adb` and `fastboot` are replaced by stubs that are *first* on PATH and refuse
#     anything they do not model;
#   * the harness asserts, before any state runs, that all three resolve into its own stub
#     directory -- so a PATH mistake is a refusal here and not a real `fastboot boot`;
#   * the stubs are plain shell reading marker files in a temporary directory. There is no code
#     path from this script to a USB device, and `fastboot boot` in it is a `touch`.
# It also does not build anything, does not write to the repository, and leaves the tree as it
# found it (the one file it reads is stages/stage90/run_and_capture.sh).
#
# Usage:
#   tools/rehearse_live_path.sh            # run every state, print a table, refuse on any mismatch
#   tools/rehearse_live_path.sh -v         # ... and print each state's full runner output
#
# Exit: 0 all states behaved as the contract says; 1 at least one did not, or the safety assert
# failed. The exit code is the verdict, so it is never masked by a pipeline.

set -u
VERBOSE=0
[[ ${1:-} == -v ]] && VERBOSE=1

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUNNER=$ROOT/stages/stage90/run_and_capture.sh
[[ -r $RUNNER ]] || { printf 'rehearse: no runner at %s\n' "$RUNNER" >&2; exit 1; }

WORK=$(mktemp -d /tmp/rehearse-live.XXXXXX) || { printf 'rehearse: mktemp failed\n' >&2; exit 1; }

# The log the "happy" states hand back as this run's capture. A real archived capture if this host
# still has one, so the reader is exercised on a real log rather than on a placeholder; otherwise a
# minimal payload log built here. Either way the source is a *copy* - the stub `cat`s it, so the
# original is never written to.
REH_PAYLOAD_LOG=${REH_PAYLOAD_LOG:-/tmp/513-run2-kmsg.txt}
[[ -r $REH_PAYLOAD_LOG ]] || { REH_PAYLOAD_LOG=$WORK/synthetic-payload.log
  { printf 'MI4IOS6_STAGE90 stage90_image_end=0x00666000\n'
    for k in xnu_live_idle_seq=0x00000001 xnu_live_door_seq=0x00000001 xnu_live_poll_seq=0x00000001 \
             xnu_live_poll_timeout_ms=0x00000005 xnu_live_poll_error=0x00000000; do
      printf 'MI4IOS6_STAGE90_XNU loader_%s\n' "$k"; done
  } > "$REH_PAYLOAD_LOG"; }
export REH_PAYLOAD_LOG
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

STUB=$WORK/bin
STATE=$WORK/state
mkdir -p "$STUB" "$STATE"
export REH_STATE=$STATE

# --- the stubs -------------------------------------------------------------------------------
#
# One `sudo` that dispatches on the command it was handed, plus bare `adb`/`fastboot` stubs so that
# a call which bypassed `sudo` still cannot reach a device. Everything unmodelled is refused with a
# message naming it, so an unmodelled call shows up as a refusal rather than as a silent pass.

cat > "$STUB/sudo" <<'STUB'
#!/bin/bash
S=${REH_STATE:?}
case "$1" in
  adb)
    shift
    case "$*" in
      "devices")
        if [[ -f $S/adb_up && ! -f $S/adb_down ]]; then
          printf 'List of devices attached\n4a2fe00b\tdevice\n'
        else
          printf 'List of devices attached\n'
        fi ;;
      "-s 4a2fe00b reboot bootloader")
        [[ -f $S/reboot_disables_fastboot ]] && touch $S/adb_down
        touch $S/in_fastboot; exit 0 ;;
      "-s 4a2fe00b exec-out cat /proc/last_kmsg")
        [[ -f $S/capture_ok ]] || exit 1
        cat "$S/capture_source" ;;
      *) printf 'rehearse-stub: unhandled adb: %s\n' "$*" >&2; exit 64 ;;
    esac ;;
  fastboot)
    shift
    case "$*" in
      "devices")
        [[ -f $S/fastboot_up ]] && printf '4a2fe00b\tfastboot\n' ;;
      boot\ *)
        # A STUB. Nothing is booted; no image is sent anywhere.
        touch $S/booted $S/fastboot_up; printf 'Sending boot image... OKAY\nBooting... OKAY\n' ;;
      *) printf 'rehearse-stub: unhandled fastboot: %s\n' "$*" >&2; exit 64 ;;
    esac ;;
  dmesg)
    [[ -f $S/dmesg_unreadable ]] && exit 1
    # The serial is on `usb 3-10`; the count is 1 until the phone re-enumerates, which the stub
    # makes happen on the *second* dmesg read after the boot - the first is the baseline the
    # runner takes immediately after `fastboot boot` returns, and that one must not already
    # contain the return or the comparison would have nothing to detect.
    if [[ -f $S/booted ]]; then
      n=$(cat $S/dmesg_calls 2>/dev/null || echo 0); n=$((n+1)); echo "$n" > $S/dmesg_calls
    else
      n=0
    fi
    printf '[1.0] usb 3-10: New USB device found, idVendor=18d1, idProduct=d00d\n'
    printf '[1.1] usb 3-10: SerialNumber: 4a2fe00b\n'
    if [[ -f $S/booted && -f $S/enum_after_boot && ${n:-0} -ge 2 ]]; then
      printf '[2.0] usb 3-10: New USB device found, idVendor=2717, idProduct=0368\n'
      printf '[2.1] usb 3-10: SerialNumber: 4a2fe00b\n'
    fi
    if [[ -f $S/booted && -f $S/adb_up && ! -f $S/adb_down ]]; then
      printf '[3.0] usb 3-10: New USB device found, idVendor=18d1, idProduct=4ee7\n'
      printf '[3.1] usb 3-10: SerialNumber: 4a2fe00b\n'
    fi ;;
  *) printf 'rehearse-stub: REFUSING (not a command this rehearsal models): %s\n' "$*" >&2; exit 64 ;;
esac
STUB
cat > "$STUB/adb"   <<'STUB'
#!/bin/bash
printf 'rehearse-stub: a bare adb call - the runner must reach the device only through sudo\n' >&2
exit 64
STUB
cat > "$STUB/fastboot" <<'STUB'
#!/bin/bash
printf 'rehearse-stub: a bare fastboot call - the runner must reach the device only through sudo\n' >&2
exit 64
STUB
chmod +x "$STUB/sudo" "$STUB/adb" "$STUB/fastboot"

# --- the safety assert, before any state runs --------------------------------------------------
#
# If this fails, nothing below runs: a rehearsal that could reach a real device is not a rehearsal.
export PATH="$STUB:$PATH"
for t in sudo adb fastboot; do
  got=$(command -v "$t")
  case $got in
    "$STUB"/*) : ;;
    *) printf 'rehearse: REFUSING - `%s` resolves to %s, not to this script'\''s stub\n' "$t" "$got" >&2
       printf '          a rehearsal that can reach a real device is not a rehearsal\n' >&2
       exit 1 ;;
  esac
done

# --- the state matrix --------------------------------------------------------------------------
#
# Every row is a state section 1-5 of run_and_capture.sh can reach, the markers that produce it, the
# exit code its own wording promises, and a line that must appear in its output. The `expect` lines
# are quoted from the runner, so a state that keeps its code but changes its message is caught too.

pass=0; fail=0
declare -a ROWS=()

run_state() {
  local name=$1 expect_code=$2 expect_text=$3; shift 3
  local d=$WORK/$name
  rm -rf "$d" "$STATE"; mkdir -p "$d" "$STATE"
  # every state starts with the phone in fastboot unless it says otherwise
  touch "$STATE/fastboot_up" "$STATE/enum_after_boot" "$STATE/capture_ok"
  printf '/dev/null' > "$STATE/capture_source"
  local logfile=$d/last_kmsg.txt
  for marker in "$@"; do
    case $marker in
      adb_up)                touch "$STATE/adb_up" ;;
      no_fastboot)           rm -f "$STATE/fastboot_up" ;;
      reboot_disables_fastboot) touch "$STATE/reboot_disables_fastboot" ;;
      dmesg_unreadable)      touch "$STATE/dmesg_unreadable" ;;
      no_enum_after_boot)    rm -f "$STATE/enum_after_boot" ;;
      capture_fails)         rm -f "$STATE/capture_ok" ;;
      log_exists)            printf 'the previous run\n' > "$logfile" ;;
      log_missing)           : ;;
      capture_is_a_real_log) cp "$REH_PAYLOAD_LOG" "$STATE/capture_source" ;;
    esac
  done
  local out=$d/out.txt err=$d/err.txt
  LOGFILE=$logfile RETURN_TIMEOUT=3 CAPTURE_WAIT=1 \
    timeout 120 bash "$RUNNER" --allow-xnu-entry > "$out" 2> "$err"
  local code=$?
  local ok=1 why=""
  if (( code != expect_code )); then ok=0; why="exit $code, promised $expect_code"; fi
  # The expectation is met by stdout **or** stderr. Half of these states end in `die`, and `die`
  # writes to stderr - so a check on stdout alone would report "the runner did not say X" about a
  # runner that said X on the other stream. That is the harness's own version of the defect this
  # whole file is about, and it is why the two streams are tested together rather than assumed.
  if [[ -n $expect_text ]] && ! { grep -qF -- "$expect_text" "$out" || grep -qF -- "$expect_text" "$err"; }; then
    ok=0; why="${why:+$why$'\n'}    did not say (either stream): $expect_text"
  fi
  # a stub refusal means the state reached code the rehearsal does not model - that is a refusal,
  # not a pass, whatever the exit code said
  if grep -q 'rehearse-stub: unhandled\|rehearse-stub: REFUSING\|rehearse-stub: a bare' "$err"; then
    ok=0; why="${why:+$why$'\n'}    the stub refused: $(grep -o 'rehearse-stub: .*' "$err" | head -1)"
  fi
  local nl; nl=$(printf '%s\n' "$why" | grep -c . || true)
  if (( ok == 1 )); then
    pass=$((pass+1)); printf '  ok    %-28s exit=%s  %s\n' "$name" "$code" "$expect_text"
  else
    fail=$((fail+1)); printf '  FAIL  %-28s %s\n' "$name" "$why"
    if (( VERBOSE == 1 )); then sed 's/^/        | /' "$out" | tail -40; fi
  fi
  ROWS+=("$name $code")
  cp "$out" "$WORK/$name.out" 2>/dev/null || true
}

printf '\n== the live path''s states, rehearsed with no device ==\n'
printf '   runner: %s\n' "$RUNNER"
printf '   stubs:  %s (sudo, adb, fastboot - first on PATH, all three asserted)\n\n' "$STUB"

# 1. the happy path: in fastboot, boot, the phone re-enumerates, adb comes up, the capture reads.
run_state happy-adb                 0 "reading the log this run captured" adb_up capture_is_a_real_log
# 2. the phone returned to a state adb cannot reach: the host log sees the new enumeration, and the
#    capture cannot run. This is exit 3 - "a return the host saw and adb missed", not a hang.
run_state host-log-return-no-adb    3 "REFUSING to call this a hang" capture_fails
# 3. nothing came back at all: no adb entry and no new enumeration.
run_state no-return                 2 "The device did not come back" no_enum_after_boot
# 4. the host log itself cannot be read: NOT a non-return, and not exit 2.
run_state host-log-unreadable       1 "the host could not read its own USB log" dmesg_unreadable no_enum_after_boot
# 5. adb mode, and the phone never shows up in fastboot after the reboot.
run_state no-fastboot-after-reboot  1 "device did not appear in fastboot" adb_up no_fastboot
# 6. the log this run captures is real payload output, and the reader reads it in the same run.
run_state happy-reads-the-log       0 "reading the log this run captured" adb_up capture_is_a_real_log
# 7. and the same exit 3 with an earlier log already at the name: step 2b must park it, and the
#    message must say where it went, because the operator's next action is to read that file.
run_state exit3-log-was-parked      3 "step 2b parked the previous run" capture_fails log_exists

printf '\n  %d ok, %d failed\n' "$pass" "$fail"
if (( fail > 0 )); then
  printf '\nREFUSING: at least one live-path state does not behave as its own contract says.\n'
  printf 'Every output is kept under %s while this shell lives; re-run with -v to see them.\n' "$WORK"
  trap - EXIT
  exit 1
fi
printf '\nAll of the live path''s reachable states behave as their own wording promises, with no\n'
printf 'device, no build, no fastboot and nothing written to storage.\n'

# ===============================================================================================
# B. the reading the coming run will have to be read by
# ===============================================================================================
#
# The live path produces a log; this half is whether that log *reads*. The states below are the
# ones the next press can actually produce - 593's pre-registered reading and its falsifiers - and
# each is asserted to print a specific line, so "the reader handled this state" is a test and not a
# hope. Every log is **synthesised here** rather than read out of /tmp: a committed check that
# depends on a host artifact degrades silently on the next host, and 595 already paid for a harness
# that reported the tree's motion instead of the edit under test.
#
# The base log is the *sleeper arm's* signature - `door_seq` and `idle_seq` climbing the powers of
# two with no ceiling, the poll records of a boot that got to the userland fixture, and **none** of
# the window family (`repair`/`sip`/`pce`/`wfi`/`slot_cwe_`) - which is what makes it take the arm
# branch. The variants remove one thing each.

mk_sleeper_log() {
  local variant=$1 out=$2
  {
    printf 'MI4IOS6_STAGE90 stage90_image_end=0x00666000\n'
    printf 'MI4IOS6_STAGE90_XNU loader_xnu_early_init_status_rollup=0x90000001\n'
    local p=1
    if [[ $variant != no-door-seq ]]; then
      while (( p <= 16777216 )); do            # 1, 2, 4, ... 0x1000000 - 25 records
        if [[ $variant == door-max-0x8000 ]] && (( p > 32768 )); then break; fi
        printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_idle_seq=0x%08x\n' "$p"
        printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_door_seq=0x%08x\n' "$p"
        p=$((p*2))
      done
    fi
    # the userland fixture's own records: two opens, the read, a fork/exit/wait, two ASTs.
    #
    # **Two of the variants exist for the *goal block's* own branch, and not for the arm's ladder.**
    # That block is outside the arm branch (every log that has payload output reaches it), and its
    # FAIL has two shapes: the fixture's sequence is *missing* a call (a position - where the boot
    # stopped) or every call is present and the *values* are wrong (a fault). One state per shape, so
    # neither side of that split is a paragraph: `goal-truncated` stops the fixture after its first
    # call, and `goal-bad-values` has every call present with the driver's open answering non-zero.
    local open1_err=0x00000000
    [[ $variant == goal-bad-values ]] && open1_err=0x00000005
    printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_open_seq=0x00000001\nMI4IOS6_STAGE90_XNU loader_xnu_live_open_error=%s\n' "$open1_err"
    if [[ $variant != goal-truncated ]]; then
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_open_seq=0x00000002\nMI4IOS6_STAGE90_XNU loader_xnu_live_open_error=0x00000002\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_read_seq=0x00000001\nMI4IOS6_STAGE90_XNU loader_xnu_live_read_nbytes=0x00000004\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_read_ret_lo=0x00000004\nMI4IOS6_STAGE90_XNU loader_xnu_live_read_buf=0x00102000\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_read_word_before=0x00102000\nMI4IOS6_STAGE90_XNU loader_xnu_live_read_word_after=0xfeedface\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_getpid_seq=0x00000001\nMI4IOS6_STAGE90_XNU loader_xnu_live_getpid_value=0x00000001\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_exit_seq=0x00000001\nMI4IOS6_STAGE90_XNU loader_xnu_live_exit_pid=0x00000002\nMI4IOS6_STAGE90_XNU loader_xnu_live_exit_rval=0x00000003\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_wait_seq=0x00000001\nMI4IOS6_STAGE90_XNU loader_xnu_live_wait_seq=0x00000002\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_wait_status=0x00000300\nMI4IOS6_STAGE90_XNU loader_xnu_live_wait_error=0x0000000a\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_ast_seq=0x00000001\nMI4IOS6_STAGE90_XNU loader_xnu_live_ast_seq=0x00000002\n'
    fi
    # the polls. 1 and 2 are the two short asks the baseline also makes; 3 is the park (2000 ms).
    local seqs=2 tmo=5
    case $variant in
      poll-seq-2|poll-seq-2-no-arm) seqs=2 ;;
      small-timeout)   seqs=3 ;;
      predicted|door-max-0x8000|no-poll-over) seqs=4 ;;
      *)               seqs=4 ;;
    esac
    for ((i=1; i<=seqs; i++)); do
      case $i in
        1) tmo=5 ;; 2) tmo=40 ;; *) tmo=2000 ;;
      esac
      # the small-timeout variant makes the *third* ask a short one again, so a poll returns but
      # not the park's own
      if [[ $variant == small-timeout && $i -eq 3 ]]; then tmo=40; fi
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_poll_seq=0x%08x\n' "$i"
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_poll_timeout_ms=0x%08x\n' "$tmo"
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_poll_error=0x00000000\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_poll_retval=0x00000000\n'
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_poll_ticks=0x024c072a\n'
    done
    # the payload's own arm record, which every real capture carries (533 does) and which the
    # reader's rung-1 FAIL narration is **guarded on**: with it the reader may say the SoC's reset
    # is not what stopped the park, without it it must say that the interval is cited and not read.
    # The `poll-seq-2-no-arm` variant is the one that removes it, so both branches are states here.
    [[ $variant == poll-seq-2-no-arm ]] || \
      printf 'MI4IOS6_STAGE90_XNU loader_hw_watchdog_counter_running=0x00000001\n'
    [[ $variant == no-poll-over ]] || printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_poll_over=0x00000008\n'
    printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_sleh_user=0x00000001\n'
  } > "$out"
}

# One row: the variant and the line(s) that must appear. The lines are quoted from the runner, so a
# state that keeps its shape but changes its message is caught as well.
#
# **A row may carry more than one expectation**, and that is what makes a *branch* testable: the
# rung-1 FAIL is one branch with two guards inside it (the arm record present or absent), and one
# expectation per row would only ever reach the first line of whichever guard ran. Every expectation
# must appear, so a row with two of them asserts the branch *and* what it said.
#
# **The expectation must not span a line wrap.** These messages are `say` strings the runner breaks
# by hand at ~88 columns, so a phrase taken across a break matches nothing and reports "the reader
# did not say X" about a reader that said X in two pieces - which is what the first draft of the
# rung-1b row did (`below the park's own threshold`). Each expectation here is one printed line.
reader_state() {
  local variant=$1; shift
  local -a WANT=("$@")
  local log=$WORK/reader-$variant.log
  mk_sleeper_log "$variant" "$log"
  local out=$WORK/reader-$variant.out err=$WORK/reader-$variant.err
  bash "$RUNNER" --summarise "$log" > "$out" 2> "$err"
  local code=$? ok=1 why="" want
  (( code == 0 )) || { ok=0; why="exit $code, promised 0"; }
  for want in "${WANT[@]}"; do
    grep -qF -- "$want" "$out" || { ok=0; why="${why:+$why$'\n'}    the reader did not say: $want"; }
  done
  if (( ok == 1 )); then
    rpass=$((rpass+1))
    # The table says how many expectations the row carried, because a row with two of them that
    # printed only its first would look exactly like a row with one - and the second is the one that
    # tests the branch. A pass that hides what it checked is the shape this file exists to catch.
    if (( ${#WANT[@]} > 1 )); then
      printf '  ok    %-22s %s (+%d more)\n' "$variant" "${WANT[0]}" "$(( ${#WANT[@]} - 1 ))"
    else
      printf '  ok    %-22s %s\n' "$variant" "${WANT[0]}"
    fi
  else
    rfail=$((rfail+1)); printf '  FAIL  %-22s %s\n' "$variant" "$why"
    (( VERBOSE == 1 )) && sed 's/^/        | /' "$out" | tail -30
  fi
}

printf '\n== the reading, on every state the coming run can produce ==\n\n'
rpass=0; rfail=0
reader_state predicted        "this arm did what it was built to do at the point that matters"
# the falsifier, and the branch inside its FAIL: the arm record is present, so the reader must say
# the SoC's reset is excluded by measurement rather than only that the park did not come back
reader_state poll-seq-2       "no poll record past the second" \
                              "the SoC's own reset is not what stopped it - measured, not argued"
# and the same FAIL with the arm record *absent*: the reader must say the interval is cited and not
# read from this log - a branch that no other state reaches
reader_state poll-seq-2-no-arm "no poll record past the second" \
                              "whether the reset is what stopped it is not read here"
reader_state small-timeout    "the largest recorded timeout is 40 ms"
reader_state door-max-0x8000  "stops at or below 0x8000"
reader_state no-door-seq      "carries no xnu_live_door_seq= record"
reader_state no-poll-over     "this arm did what it was built to do at the point that matters"
# the goal block's FAIL, one state per side of its presence-before-values split - so the *position*
# reading (the boot stopped inside the fixture) and the *values* reading (a call answered wrongly)
# each have a state, and neither can be reached only through the other
reader_state goal-truncated   "no record of the control open" \
                              "missing here is a POSITION and not a driver fault"
reader_state goal-bad-values  "the fault is in the values" \
                              "the driver's open answered 0x00000005 (must be 0)"
printf '\n  %d ok, %d failed\n' "$rpass" "$rfail"
(( rfail == 0 )) || { printf '\nREFUSING: the reader has a state it cannot read.\n'; trap - EXIT; exit 1; }
printf '\nEvery state the next press can produce is read by its own line.\n'
