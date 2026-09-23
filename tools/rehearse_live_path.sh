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
        # Three lists, because the runner has a guard for the first two and the third is the
        # happy path. `fastboot_other` is a single device that is NOT this phone; `fastboot_two`
        # is two devices, which is the state `fastboot boot` without `-s` silently picks from.
        if [[ -f $S/fastboot_up ]]; then
          if [[ -f $S/fastboot_two ]]; then
            printf '4a2fe00b\tfastboot\n33e80afe\tfastboot\n'
          elif [[ -f $S/fastboot_other ]]; then
            printf '33e80afe\tfastboot\n'
          else
            printf '4a2fe00b\tfastboot\n'
          fi
        fi ;;
      # **The pin is enforced here rather than asserted in a comment.** The stub accepts the boot
      # only with `-s 4a2fe00b`; a bare `fastboot boot <image>` is refused, so the happy-path
      # state itself proves the serial is on the command line. `boot *` matched both forms before
      # 616, which is exactly why the pin could have been removed without any cell noticing.
      "boot -s 4a2fe00b "*) 
        # A STUB. Nothing is booted; no image is sent anywhere.
        touch $S/booted $S/fastboot_up; date +%s > $S/booted_at
        printf 'Sending boot image... OKAY\nBooting... OKAY\n' ;;
      "boot -s "*)
        printf 'rehearse-stub: fastboot boot pinned to a serial that is not 4a2fe00b: %s\n' "$*" >&2; exit 64 ;;
      "boot "*)
        printf 'rehearse-stub: a bare fastboot boot - the runner must pin the serial with -s: %s\n' "$*" >&2; exit 64 ;;
      *) printf 'rehearse-stub: unhandled fastboot: %s\n' "$*" >&2; exit 64 ;;
    esac ;;
  dmesg)
    [[ -f $S/dmesg_unreadable ]] && exit 1
    # The serial is on `usb 3-10`. The baseline lines below are the phone as it stands, and the
    # re-enumeration that follows the boot is added only once enough time has passed for it - see the
    # note on the elapsed-time test. Both are needed: the comparison the runner makes is
    # baseline-versus-now, so a stub that showed the return in the baseline would leave it nothing to
    # detect, and a stub that never showed it could not produce a return at all.
    # **The return is a function of elapsed TIME, not of how many times the runner has read the log,
    # and the first version of this stub got that wrong.** It made the re-enumeration appear on the
    # *second* read after the boot - which was true for as long as the runner took exactly one baseline
    # read. 617 gave the runner a second baseline (the port-keyed count, taken in the same instant);
    # under the read-count model the return then landed INSIDE that baseline, so `PORT_BEFORE` came
    # out already carrying the enumeration, `PORT_AFTER` equalled it, and the one state the new cell
    # exists to produce became unreachable. The model was a claim about the runner dressed as a model
    # of the device. A real device does not re-enumerate between two `dmesg` calls microseconds apart:
    # it takes seconds, and the runner's wait loop sleeps 3 s between polls. So the model is elapsed
    # seconds since the boot, which stays true however many baseline reads the runner makes.
    #
    # The delay is per-state, because which *branch* a return exercises is a function of whether it
    # lands inside the runner's wait window or after it: `return_after:N` moves it out past the whole
    # window, which is the only way to reach the post-wait block deliberately.
    _need=$(cat $S/ret_after 2>/dev/null || echo 2)
    RET_SEEN=0
    if [[ -f $S/booted && -f $S/enum_after_boot && -f $S/booted_at ]]; then
      _el=$(( $(date +%s) - $(cat $S/booted_at) ))
      (( _el >= _need )) && RET_SEEN=1
    fi
    # **`no_serial_ever` does NOT print an empty log, and the difference is the whole point of it.**
    # The runner reads a `dmesg` that printed nothing as a channel it could not read (UNREAD), not as a
    # log with no phone in it - which is 609's "the machine stopped, or the channel that records it
    # did", one layer in. A real `dmesg` is never empty. So this state emits a readable log that simply
    # never mentions the phone: unrelated devices, no `usb 3-10` line at all. That is what makes the
    # port UNDERIVABLE rather than the log UNREAD, and the two states below are the pair that says so.
    if [[ -f $S/no_serial_ever ]]; then
      printf '[0.0] usb 1-1: New USB device found, idVendor=1d6b, idProduct=0002\n'
      printf '[0.1] usb 2-1: New USB device found, idVendor=8087, idProduct=0024\n'
      printf '[0.2] hub 2-1:1.0: USB hub found\n'
    else
      # The phone's own baseline: `phone_port` DERIVES the port from these lines.
      printf '[1.0] usb 3-10: New USB device found, idVendor=18d1, idProduct=d00d\n'
      printf '[1.1] usb 3-10: New USB device strings: Mfr=1, Product=2, SerialNumber=3\n'
      printf '[1.2] usb 3-10: SerialNumber: 4a2fe00b\n'
      if (( RET_SEEN == 1 )); then
        # **Two shapes of return, and the second is the one the serial test cannot see.** `qdl_return`
        # makes the phone come back in a Qualcomm mode with EMPTY USB descriptors - measured on this
        # host: `05c6:f006`, `Mfr=0 Product=0 SerialNumber=0`, five seconds apart from the same
        # device's `2717:0368 MI 4LTE` lines on `usb 3-10`.
        if [[ -f $S/qdl_return ]]; then
          printf '[2.0] usb 3-10: New USB device found, idVendor=05c6, idProduct=f006, bcdDevice= 0.00\n'
          printf '[2.1] usb 3-10: New USB device strings: Mfr=0, Product=0, SerialNumber=0\n'
        else
          printf '[2.0] usb 3-10: New USB device found, idVendor=2717, idProduct=0368\n'
          printf '[2.1] usb 3-10: New USB device strings: Mfr=1, Product=2, SerialNumber=3\n'
          printf '[2.2] usb 3-10: SerialNumber: 4a2fe00b\n'
        fi
      fi
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
  # `FORBID` is the assertion of **absence**, and it is a local so a state that does not set it cannot
  # inherit the previous state's. Per 615, an absence assertion is only worth anything beside a sibling
  # that asserts the presence - the pair is what turns "it did not say X" into "X is what this state
  # must not say". Both new states in section 6 are such a pair.
  local FORBID=""
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
      fastboot_two)          touch "$STATE/fastboot_two" ;;
      fastboot_other)        touch "$STATE/fastboot_other" ;;
      qdl_return)            touch "$STATE/qdl_return" ;;
      no_serial_ever)        touch "$STATE/no_serial_ever" ;;
      return_after:*)        printf '%s' "${marker#return_after:}" > "$STATE/ret_after" ;;
      forbid:*)              FORBID=${marker#forbid:} ;;
      dmesg_unreadable)      touch "$STATE/dmesg_unreadable" ;;
      no_enum_after_boot)    rm -f "$STATE/enum_after_boot" ;;
      capture_fails)         rm -f "$STATE/capture_ok" ;;
      log_exists)            printf 'the previous run\n' > "$logfile" ;;
      log_missing)           : ;;
      capture_is_a_real_log) cp "$REH_PAYLOAD_LOG" "$STATE/capture_source" ;;
    esac
  done
  local out=$d/out.txt err=$d/err.txt
  # **`RETURN_TIMEOUT=6` rather than 3, and it is 617's code that needs the second poll.** At 3 the
  # loop makes exactly one check before its `sleep 3` and then exits, so nothing that takes any time
  # at all can be seen *inside* the wait - every state would land in the post-wait block and the
  # in-wait branches would stop being exercised. Six gives two polls: a return delayed to 2 s is seen
  # inside the wait (states 1/2/9) and one delayed past the window (10 s, the port-advance state) is
  # not. It also keeps the battery's clock honest against the stub's - the stub measures elapsed
  # seconds, so the window it is measured against has to be long enough to contain a real poll.
  LOGFILE=$logfile RETURN_TIMEOUT=6 CAPTURE_WAIT=1 \
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
  # The absence half. Checked on both streams for the same reason the expectation is.
  if [[ -n $FORBID ]] && { grep -qF -- "$FORBID" "$out" || grep -qF -- "$FORBID" "$err"; }; then
    ok=0; why="${why:+$why$'\n'}    said what this state must NOT say: $FORBID"
  fi
  # a stub refusal means the state reached code the rehearsal does not model - that is a refusal,
  # not a pass, whatever the exit code said
  if grep -q 'rehearse-stub: unhandled\|rehearse-stub: REFUSING\|rehearse-stub: a bare\|rehearse-stub: fastboot boot pinned' "$err"; then
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
# 5b. **The ambiguity guard, and these two states are why it exists.** `fastboot boot` with more than
# one device listed does not refuse - it picks one - so a second phone-class device in fastboot at the
# same moment redirects the boot to it, and this run then waits for a `usb 3-10`/`4a2fe00b` return that
# cannot come. Measured on this host: `usb 3-3` carries such a device (serial `33e80afe…`, 12 fastboot
# entries in the host log). Both states must REFUSE, and neither may boot.
run_state two-devices-in-fastboot      1 "fastboot lists 2 device(s)"                    fastboot_two
# The wrong-serial case never reaches the boot step at all: mode detection asks adb first and
# fastboot second, and a lone `33e80afe` answers neither, so this state dies at `step "device"`.
# **This cell has now been wrong twice and both corrections are the point.** The first version
# asserted a message from a guard branch added for this state - a branch that could not be reached,
# because the pre-existing check fires first. The second asserted the check at the boot step
# (`device did not appear in fastboot`) - also unreachable here, because mode detection is earlier
# still. The state is covered at the FIRST of the three, and the three are a partition worth naming:
#
#   | guard                                  | state it refuses                            |
#   | -------------------------------------- | ------------------------------------------- |
#   | `step "device"` mode detection         | no device with $SERIAL anywhere             |
#   | boot step's presence check             | adb mode, serial never shows in fastboot    |
#   | boot step's COUNT guard (new in 616)   | $SERIAL present AMONG OTHERS                |
#
# Only the third is new, and only the third was missing: the other two ask whether `$SERIAL` is
# present and neither asks whether it is *alone*, which is the state `fastboot boot` without `-s`
# silently picks from.
run_state one-device-wrong-serial      1 "serial 4a2fe00b not found in adb or fastboot"  fastboot_other
# and the adb-mode state where the phone answers adb but never appears in fastboot - the middle row
# of that table, which the `no-fastboot-after-reboot` state above already exercises.
# 6. the log this run captures is real payload output, and the reader reads it in the same run.
run_state happy-reads-the-log       0 "reading the log this run captured" adb_up capture_is_a_real_log
# 7. and the same exit 3 with an earlier log already at the name: step 2b must park it, and the
#    message must say where it went, because the operator's next action is to read that file.
run_state exit3-log-was-parked      3 "step 2b parked the previous run" capture_fails log_exists

# 6. **The return the serial test cannot see, and the state that is only here to make it a reading.**
#
# The criterion in section 4 keys the host-log half of the return test on `SerialNumber: $SERIAL`, and
# this phone does not always carry one: measured out of the host log, `usb 3-10` has 1536 `New USB
# device found` records, 1530 with `SerialNumber: 4a2fe00b` and **6** that are `05c6:f006` with
# `Mfr=0 Product=0 SerialNumber=0` - the same device cycling into a Qualcomm mode five seconds after
# its own `2717:0368 MI 4LTE` lines. A phone that comes back in that mode advanced neither the adb
# test nor the serial test, so it fell through every branch and was reported as **exit 2, "the device
# did not come back"** - the one code this contract reserves for a hang, and the one that tells the
# operator to press power. The device came back; the criterion could not see it.
#
# `return_after:5` is the whole difficulty of this cell and it is worth stating, because the value is
# a window and not a number. The runner reads the host log at the END of its wait, so a return that is
# to be seen there and *not* during the wait has to land in the last `sleep 3` of the window - with
# `RETURN_TIMEOUT=6` the in-wait polls are at ~0.05 s and ~3.05 s and the post-wait reads at ~6.05 s,
# so the band is (3.05, 6.05]. It is one sleep wide and no value can have more than ~1.5 s of margin on
# both sides, which is a property of the runner (the wait ends and the reads happen at once) and not of
# the stub. A stub that had to be exact to reproduce this state would be modelling the wrong thing.
run_state qdl-return                 3 "REFUSING to call this a non-return: an enumeration appeared on" \
                                       return_after:5 qdl_return 'forbid:The device did not come back'
# And this is the second half of that pair, and the reason the first one is a reading rather than a
# lucky exit code. The port is DERIVED from the phone's own `SerialNumber: 4a2fe00b` lines, so with
# none of them in the log there is no port to key on: `PORT_BEFORE` is UNREAD, the port branch cannot
# fire, and this state must fall back to the serial criterion and say the non-return it can actually
# justify. `forbid:` the port refusal here is the assertion - without it, a version that fired the
# port branch on UNREAD-and-zero would pass this cell too.
run_state port-underivable           2 "The device did not come back" no_serial_ever \
                                       'forbid:an enumeration appeared on'
# **And this is the cell that makes the two branches' ORDER structural rather than a preference**, and
# it exists because a measurement said it had to. The order was changed to serial-first on the
# reasoning that a normal return advances BOTH counts, so the port branch written first would have
# fired for every return the post-wait block saw. Then the change was falsified by putting the port
# branch back first - and the battery stayed GREEN, because the only state that reached that block
# with an advance was `qdl-return`, which advances the port and *not* the serial by construction. The
# claim "a normal return advances both" was covered by nothing: a statement about the ordering with no
# cell that could observe it. This state is the missing half - a normal return, timed with the same
# 5 s so it lands after the last in-wait poll, so the post-wait block sees both counts advance and the
# cell can say which branch owns that state. Measured: reverting the order turns THIS cell red and
# leaves `qdl-return` green, so each branch is now pinned by a state that only it can answer.
run_state return-after-last-poll     3 "the host log shows the phone enumerating again" return_after:5 \
                                       'forbid:an enumeration appeared on'

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
  # **`-capped` is a modifier and not a variant.** Its base is whatever precedes the suffix, so the
  # two states it has to separate - "this boot filled the channel and dropped records" and "this boot
  # did not" - are the same log otherwise, which is what makes the reading a reading of the channel
  # rather than of some other difference between two fixtures (609).
  local variant=$1 out=$2 base=${1%-capped}
  {
    printf 'MI4IOS6_STAGE90 stage90_image_end=0x00666000\n'
    printf 'MI4IOS6_STAGE90_XNU loader_xnu_early_init_status_rollup=0x90000001\n'
    # The channel's own capacity line, which every real capture carries: `entry_live_init` publishes
    # it ahead of the first record. Without it the reader's new clause is in its third state ("records
    # and no cap"), which is a real state and is exercised by nothing here - so it is emitted always
    # and the `-capped` variants add the *second* line, the one published at the first dropped record.
    printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_cap=0x00002000\n'
    [[ $variant == *-capped ]] && printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_capped=0x00002000\n'
    local p=1
    if [[ $base != no-door-seq ]]; then
      while (( p <= 16777216 )); do            # 1, 2, 4, ... 0x1000000 - 25 records
        if [[ $base == door-max-0x8000 ]] && (( p > 32768 )); then break; fi
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
    [[ $base == goal-bad-values ]] && open1_err=0x00000005
    printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_open_seq=0x00000001\nMI4IOS6_STAGE90_XNU loader_xnu_live_open_error=%s\n' "$open1_err"
    if [[ $base != goal-truncated ]]; then
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
    case $base in
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
      if [[ $base == small-timeout && $i -eq 3 ]]; then tmo=40; fi
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
    [[ $base == poll-seq-2-no-arm ]] || \
      printf 'MI4IOS6_STAGE90_XNU loader_hw_watchdog_counter_running=0x00000001\n'
    [[ $base == no-poll-over ]] || printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_poll_over=0x00000008\n'
    printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_sleh_user=0x00000001\n'
  } > "$out"
}

# **The channel's second signal, and the one no `-capped` variant can exercise.** `xnu_live_capped` is
# published at the moment of the *first dropped record*, so a boot that filled the channel exactly and
# then stopped - nothing more to write - carries no such record at all, and the only evidence is the
# **count**. That is the state this builds: a log with the capacity line and exactly that many records,
# which the reader must read as truncated from the count alone. Without this row the count half of the
# clause would be a branch nothing has ever run (609).
mk_capfull_log() {
  local out=$1 cap=8192 i
  {
    printf 'MI4IOS6_STAGE90 stage90_image_end=0x00666000\n'
    printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_cap=0x00002000\n'
    for ((i=0; i<cap-1; i++)); do      # the capacity line is itself record #1 of the cap
      printf 'MI4IOS6_STAGE90_XNU loader_xnu_live_fill_seq=0x%08x\n' "$i"
    done
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
  if [[ $variant == cap-full ]]; then mk_capfull_log "$log"; else mk_sleeper_log "$variant" "$log"; fi
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
# **The positive text of the new channel clause is asserted, not assumed.** A check whose success is
# printed only on the bad state cannot be told from one that never ran, so the "not full" line is an
# expectation of its own on the state where it must appear (609).
reader_state predicted        "this arm did what it was built to do at the point that matters" \
                              "live channel: not full"
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
# **The three states 609 added, one per clause that read absence as a fact about the machine.** Each is
# the *same log* as the FAIL it replaces, plus the channel's own `capped` record, so the difference in
# the reader's output is attributable to the channel and nothing else. Without these the three UNREAD
# branches would be narration that nothing has ever run - which is the debt 603 section 7 named.
reader_state door-max-0x8000-capped "live channel: **TRUNCATED**" \
                              "UNREAD  and it stops at or below 0x8000"
reader_state poll-seq-2-capped "UNREAD  and no poll record past the second" \
                              "Whether the park's poll came back is **UNREAD on this log**"
reader_state goal-truncated-capped "UNREAD  and the fixture's sequence has no record of" \
                              "live channel: **TRUNCATED**"
# and the count-only state: full channel, no `capped` record, so the number is the whole of the evidence
reader_state cap-full         "live channel: **TRUNCATED**" \
                              "0x00002000"
printf '\n  %d ok, %d failed\n' "$rpass" "$rfail"
(( rfail == 0 )) || { printf '\nREFUSING: the reader has a state it cannot read.\n'; trap - EXIT; exit 1; }
printf '\nEvery state the next press can produce is read by its own line.\n'

# ---------------------------------------------------------------------------------------------------
# == the path argument, which is the caller's and not this script's =================================
#
# 615's defect, and it is the one every cell above was blind to because every cell above passes an
# ABSOLUTE path: `run_and_capture.sh` `cd`s to its own directory before parsing its arguments, so a
# relative `--summarise` path was joined to `stages/stage90/` instead of to the caller's directory and
# refused as `no such log`, naming a file that exists. Measured before the fix: refused from the
# repository root, and refused from the capture's own directory with the bare filename - only an
# absolute path worked. The operator's next action after the one gated boot is to re-read that run's
# capture, and the natural thing to type is a path relative to where they are.
#
# The states below are the three the fix has to keep apart, and the third is the one that keeps the
# note honest: a re-based path must SAY it was re-based, and an absolute path must stay silent, because
# a note that prints either way is not a note about anything.
printf '\n== the path argument, resolved against the caller and not this script ==\n\n'
PPASS=0; PFAIL=0
path_state() {  # path_state NAME EXPECTED_EXIT MUST_SAY MUST_NOT_SAY DIR ARGS...
  local name=$1 want=$2 must=$3 mustnot=$4 dir=$5; shift 5
  local out rc
  out=$( cd "$dir" && bash "$RUNNER" "$@" 2>&1 ); rc=$?
  local why=""
  (( rc == want )) || why="exit $rc, promised $want"
  if [[ -n $must ]]; then
    printf '%s' "$out" | grep -qF -- "$must" || why="${why:+$why; }did not say: $must"
  fi
  # The absence assertion carries the same guard the shrink cells in the revert-set rehearsal needed:
  # a command that failed to run at all also "did not say" the thing. So MUST_NOT_SAY is only ever
  # asserted together with a positive expectation that the same output does contain.
  if [[ -n $mustnot ]]; then
    printf '%s' "$out" | grep -qF -- "$mustnot" && why="${why:+$why; }said what it must not: $mustnot"
  fi
  if [[ -n $why ]]; then
    PFAIL=$((PFAIL+1)); printf '  FAIL  %-26s %s\n' "$name" "$why"
    printf '%s\n' "$out" | sed 's/^/          /' | head -5
  else
    PPASS=$((PPASS+1)); printf '  ok    %-26s exit=%s  %s\n' "$name" "$rc" "${must:-$mustnot}"
  fi
}
RELLOG=$WORK/reader-predicted.log
mk_sleeper_log predicted "$RELLOG"
RELDIR=$WORK/relative
mkdir -p "$RELDIR"
cp "$RELLOG" "$RELDIR/capture.txt"
# 1. a path relative to the CALLER's directory reads the log (it used to be refused as "no such log")
path_state relative-path-reads    0 "MI4IOS6_STAGE90 lines:" "no such log"  "$RELDIR" --summarise capture.txt
# 2. and the same invocation from the repository root, with a path relative to *there* - which is the
#    shape a user types. Derived rather than written down: a hard-coded tree path would make this cell a
#    statement about which captures happen to be on this host, and it would pass by not running.
REL_FROM_ROOT=$(python3 -c 'import os,sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$RELLOG" "$ROOT")
case $REL_FROM_ROOT in
  /*) printf '  FAIL  %-26s relpath returned an absolute path (%s), so this cell cannot test anything\n' \
        "relative-from-root" "$REL_FROM_ROOT"; PFAIL=$((PFAIL+1)) ;;
  *)  path_state relative-from-root 0 "MI4IOS6_STAGE90 lines:" "no such log" "$ROOT" --summarise "$REL_FROM_ROOT" ;;
esac
# 3. an absent relative path is STILL refused, and the refusal names the absolute path and the directory.
#    This one is a no-regression cell rather than a cell about the resolution - it passes before and
#    after the fix, because a missing file is refused either way - and it is kept because the failure it
#    would catch is the fix going too far (a reader that re-bases and then accepts what it finds).
path_state relative-absent-refused 1 "is not a readable regular file" "" "$RELDIR" --summarise not-here.txt
# 4. an absolute path prints NO note. **This cell passes in the broken version too** - it asserts an
#    absence, and the broken version printed no note either, because it printed nothing but the refusal.
#    Its evidence is only readable together with cells 1 and 2, which assert that the note IS printed
#    when a path was re-based; the pair is what makes the absence mean "absolute paths stay silent"
#    rather than "this line never runs". Measured by falsification: with the resolution removed, cells 1
#    and 2 go red and cells 3 and 4 stay green - the two greens are exactly the two that a broken
#    version also satisfies.
path_state absolute-path-no-note  0 "MI4IOS6_STAGE90 lines:" "is a relative path" "$RELDIR" --summarise "$RELLOG"
printf '\n  %d ok, %d failed\n' "$PPASS" "$PFAIL"
(( PFAIL == 0 )) || { printf '\nREFUSING: a path argument is interpreted against the wrong directory.\n'; trap - EXIT; exit 1; }
printf '\nA path the caller names is resolved against the caller.\n'
