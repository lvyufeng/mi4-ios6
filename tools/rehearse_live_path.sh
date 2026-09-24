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
# failed; **2 the live path's own tree (`out/stage90/`) changed under one or more states, so those
# states have no verdict** - a separate code because "the runner is wrong" and "this run proves
# nothing about the runner" are different claims, and the second must not be spelled like the first.
# The TREE verdict and the pin set are explained where they are defined, above the state matrix.
#
# To see the TREE detector fire without writing into the live `out/`:
#   p=$(mktemp); ( sleep 5; printf 'moved\n' >> "$p" ) & REH_TREE_EXTRA=$p tools/rehearse_live_path.sh
# (the first state runs ~25 s, so the write lands inside it; the run then ends at exit 2 with that
# state's -- TREE -- row instead of `ok`, and every other state is untouched). The exit code is the
# verdict, so it is never masked by a pipeline.

set -u
VERBOSE=0
[[ ${1:-} == -v ]] && VERBOSE=1

ROOT=$(cd "$(dirname "$0")/.." && pwd)
LIVE_RUNNER=$ROOT/stages/stage90/run_and_capture.sh
# **`RUNNER_OVERRIDE` exists so a *mutation* can be measured without the live path ever being the
# mutated file, and it is the peer session's finding (its own m636 rule, one layer out).** m636 says
# rename-never-in-place, because bash reads a running script by fd and an in-place edit can make the
# live process execute at a shifted offset. That protects a *process already reading* a file. It does
# not protect a file that a **live process fires by path**: `press-watcher.sh:286` does `cd "$STAGE"`
# and `:292` runs `./run_and_capture.sh --allow-xnu-entry`, and neither file holds a lock
# (`grep -c flock` = 0 in both). So an in-place mutation of the runner is a window in which **a press
# executes the mutant** - and the two ways that loses the press are a `die` before `fastboot boot`
# (a press spent on nothing) and a boot that classifies wrongly (a press spent on a reading of the
# mutation, which is worse, because it produces a plausible log).
#
# So the override is not a convenience: it is how the mutation batch points at a *copy* while the
# press's path stays the recorded bytes. The mutated copy has to sit at `stages/stage90/<name>.sh`
# because the runner does `cd "$(dirname "$0")"` and derives `STAGE_DIR`/`REPO_ROOT`/`OUT` from `$0`,
# and a copy in `/tmp` would look for the gate and `out/` beside itself. A `.sh` file there is
# invisible to the gate's freshness scan (`preflight_boot_check.sh:177-179` is `-type f` and a
# `-name '*.c' -o '*.h' -o '*.S' -o '*.ld'` list) and is never fired by the catcher, which names
# `./run_and_capture.sh`.
RUNNER=${RUNNER_OVERRIDE:-$LIVE_RUNNER}
[[ -r $RUNNER ]] || { printf 'rehearse: no runner at %s\n' "$RUNNER" >&2; exit 1; }
# **And it is made absolute here, because section C `cd`s and the sections do not share a directory.**
# 628 measured this: with a *relative* `RUNNER_OVERRIDE` (`stages/stage90/.r628-mut.sh`) sections A and
# B are green and section C reports `No such file or directory` on all four of its cells, because the
# path-argument section runs the runner from a different directory on purpose (`cd "$dir" && bash
# "$RUNNER"`) - and a relative runner path is then resolved against *that* directory. The live path is
# absolute so the default never showed it; the override I added in 620 is what made it reachable, and
# the failure mode is the worst kind: a mutation measured with a relative path reports section C broken
# rather than reporting what it was measuring. Resolved once, here, so every section fires the same file.
RUNNER=$(readlink -f "$RUNNER" 2>/dev/null || printf '%s' "$RUNNER")
[[ -r $RUNNER ]] || { printf 'rehearse: the runner resolved to %s, which is not readable\n' "$RUNNER" >&2; exit 1; }
# **And the override may not be the live path.** A `RUNNER_OVERRIDE` pointed at `run_and_capture.sh`
# would silently restore exactly the hazard above while looking like it had been handled, so the
# refusal is structural rather than a rule in a comment.
if [[ -n ${RUNNER_OVERRIDE:-} ]]; then
  _live=$(readlink -f "$LIVE_RUNNER" 2>/dev/null || printf '%s' "$LIVE_RUNNER")
  _ovr=$(readlink -f "$RUNNER" 2>/dev/null || printf '%s' "$RUNNER")
  if [[ $_ovr == "$_live" ]]; then
    printf 'rehearse: RUNNER_OVERRIDE resolves to the LIVE runner (%s).\n' "$RUNNER" >&2
    printf '          The override exists so a mutation is measured on a copy; pointing it at the live\n' >&2
    printf '          path puts the mutant back on the path a press fires. Refusing.\n' >&2
    exit 1
  fi
  printf 'rehearse: RUNNER overridden to %s (live path %s untouched)\n' "$RUNNER" "$LIVE_RUNNER"
fi

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
      printf ' %s\n' "$k"; done
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
        # **The state column is part of the output, because 619 made it part of the reading.** The
        # runner's mode detection now tests the state and not just field 1, so a stub that printed
        # `device` always would leave the branch 619 added untestable - which is the same defect as
        # 616's `boot *` glob: a stub whose output is coarser than the thing the runner reads.
        if [[ -f $S/adb_up && ! -f $S/adb_down ]]; then
          if [[ -f $S/adb_unauthorized ]]; then
            printf 'List of devices attached\n4a2fe00b\tunauthorized\n'
          elif [[ -f $S/adb_offline ]]; then
            printf 'List of devices attached\n4a2fe00b\toffline\n'
          else
            printf 'List of devices attached\n4a2fe00b\tdevice\n'
          fi
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
        # Four lists, and which one is printed depends on how many times this command has been called
        # and on the state's markers. `fastboot_other` is a single device that is NOT this phone;
        # `fastboot_two` is two devices, which is the state `fastboot boot` without `-s` silently
        # picks from; and `leave_after:N` makes the two-device list appear for the first N calls only,
        # which is how **618's wait is exercised** - the switch has to land on the call the wait makes,
        # so N is set from the runner's own call count and not guessed.
        #
        # The count is exact for the state it is used in: with the phone already in fastboot, the
        # runner's calls are (1) mode detection, (2) the boot step's presence check, (3) the guard's
        # first read, then one per wait poll - so `leave_after:3` puts the switch on the wait's first
        # refetch. If that call count ever changes, the cell that uses it goes red rather than
        # quietly testing nothing, because a wait that never runs leaves the refusal in the output.
        #
        # **And the list is written to a file, because the boot below is checked against it.** `-s` is
        # only a pin if the named device is on the bus; a stub that booted anyway would make `-s` look
        # like a guarantee in a state where it is a no-op.
        if [[ -f $S/fastboot_up ]]; then
          _fc=$(cat $S/fb_calls 2>/dev/null || echo 0); _fc=$((_fc+1)); echo "$_fc" > $S/fb_calls
          _leave=$(cat $S/leaves_after 2>/dev/null || echo 0)
          if [[ $_fc -le $_leave ]]; then
            _list=$(printf '4a2fe00b\tfastboot\n33e80afe\tfastboot')
          elif [[ -f $S/fastboot_two ]]; then
            _list=$(printf '4a2fe00b\tfastboot\n33e80afe\tfastboot')
          elif [[ -f $S/fastboot_other ]]; then
            _list=$(printf '33e80afe\tfastboot')
          else
            _list=$(printf '4a2fe00b\tfastboot')
          fi
          printf '%s\n' "$_list" > $S/last_fb_list
          printf '%s\n' "$_list"
        fi ;;
      # **The pin is enforced here rather than asserted in a comment.** The stub accepts the boot
      # only with `-s 4a2fe00b`; a bare `fastboot boot <image>` is refused, so the happy-path
      # state itself proves the serial is on the command line. `boot *` matched both forms before
      # 616, which is exactly why the pin could have been removed without any cell noticing.
      "boot -s 4a2fe00b "*)
        # A STUB. Nothing is booted; no image is sent anywhere.
        # **The pin is checked against the last list, because the pin is the thing being tested.** Real
        # `fastboot -s <serial> boot` with that serial absent fails; the state
        # `fastboot-other-after-wait` exists because the runner must refuse that case *itself*, with a
        # message, rather than let `fastboot` produce an error this file's exit contract has no clause
        # for. A stub that booted anyway would have made the count-only condition look sound.
        if ! grep -q '^4a2fe00b' "$S/last_fb_list" 2>/dev/null; then
          printf 'fastboot: error: Device 4a2fe00b not found\n' >&2; exit 1
        fi
        # **620: the boot call's own status, which the runner must record rather than inherit.** The
        # real `fastboot boot` sends the image and *then* waits for the device to acknowledge, so a
        # non-zero status can come from either side of the send and the value does not say which. Both
        # sides are modelled, because the runner's correct behaviour differs between them and neither
        # is the common case:
        #
        #   `boot_fail_rc:N`  the send side - nothing is booted (`$S/booted` untouched, so no return
        #                     can appear) and the status is N. The runner must still reach the wait and
        #                     the capture, and must report **exit 1** rather than exit 2, because exit
        #                     2 claims the payload ran.
        #   `boot_sent_rc:N`  the acknowledge side - the payload IS running (so the return appears on
        #                     its own) and the status is still N. Here a run that aborted on the status
        #                     would throw away the only reading the press produced, which is the whole
        #                     reason the status is captured instead of fatal.
        if [[ -f $S/boot_fail_rc ]]; then
          printf 'fastboot: error: cannot load %s\n' "$2" >&2
          exit "$(cat $S/boot_fail_rc)"
        fi
        touch $S/booted $S/fastboot_up; date +%s > $S/booted_at
        printf 'Sending boot image... OKAY\nBooting... OKAY\n'
        if [[ -f $S/boot_sent_rc ]]; then
          printf 'fastboot: error: the device did not acknowledge the boot\n' >&2
          exit "$(cat $S/boot_sent_rc)"
        fi ;;
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

# --- the tree the live path reads, pinned so a concurrent build cannot be read as a runner bug -----
#
# WHY THIS EXISTS. The states below fire the LIVE runner, and the live runner's own gate reads five
# files in `out/stage90/` - their contents and their mtimes decide whether it proceeds or refuses.
# Nothing here pinned them, so anything that rebuilds the arm rewrites the tree *between two of the
# gate's own reads*, and the gate then refuses a pair of files that never existed as a whole.
#
# **Measured, 2026-09-24 (this is not a hypothetical).** The battery ran 17:30:04-17:39:59 while an
# entry rebuild in this same session wrote `xnu_arm_entry.elf`/`.bin` at 17:39:41 and the config
# record at 17:45:44 - so from 17:39:41 until the payload was rebuilt at 17:47:21 the tree was torn.
# The two cells that straddled the write, `adb-plan-line` (17:39:34-17:39:59) and `fastboot-plan-line`
# (17:39:59), both came back **exit 1, promised 0**, and both refusals name the tear exactly: the
# record saying `STAGE90_XNU_SEAM_MEASURE=1` beside an image whose `entry_seam_flush` calls
# `FlushPoC_DcacheRegion`, and `stage90.bin` embedding an entry of *the same length and different
# bytes*. The runner was right - refusing a torn tree is the whole point of that clause - and the
# battery printed it as a runner failure. That is 595's defect ("a harness that reported the tree's
# motion instead of the edit under test") landing on this file, which cites 595 in its own comment
# about `RUNNER_OVERRIDE` and did not close the same door one section down.
#
# So a cell whose pinned tree changed while it ran has **no verdict**: not ok, and not FAIL either,
# because "the runner did not behave as its contract says" is a claim the reading cannot support when
# the contract's inputs were moving. The verdicts are separate and the counts are separate, because a
# green table that hides a torn read is the same lie in the other direction.
#
# **The pins are measurements, not the build's own record.** `out/stage90/SHA256SUMS.txt` is written by
# the build that produced the files it lists, so it agrees with whatever is on disk and cannot witness
# a swap ([[mi4-self-written-record-is-not-a-constraint]]); these are `sha256sum` and `stat` on the
# files themselves. The mtime is part of the identity because the gate's freshness sweep *compares
# mtimes*, so a rebuild that only rewrote a source file - or a `touch` - moves the tree the runner
# reads without moving a byte of the five.
#
# **`REH_TREE_EXTRA` is this harness's own falsification knob, and it can only ADD pins.** A cell that
# has never been seen to go TREE is the 613 shape - a detector whose firing is unreported - but a
# detector can only be shown to fire by moving something it reads, and moving one of the five means
# writing into the live `out/`. So the knob appends extra paths to the identity: running a state with
# `REH_TREE_EXTRA=/tmp/probe` while something appends to `/tmp/probe` fires the in-loop guard end to
# end. It is additive by construction (the live five are always pinned *and* are printed first), so it
# cannot be used to disarm the guard, which is the failure mode a `TREE_DIR`-style override would have.
REH_PINS=(xnu_arm_entry.elf xnu_arm_entry.bin stage90.bin stage90-qcdt.img xnu_arm_entry-config.txt)
TREE_DIR=$ROOT/out/stage90
tree_pin_line() {
  local f=$1 label=$2
  if [[ -r $f ]]; then
    printf '%s sha=%s mtime=%s\n' "$label" "$(sha256sum "$f" | cut -d' ' -f1)" "$(stat -c %Y "$f")"
  else
    printf '%s ABSENT\n' "$label"
  fi
}
tree_identity() {
  local p e
  for p in "${REH_PINS[@]}"; do tree_pin_line "$TREE_DIR/$p" "$p"; done
  if [[ -n ${REH_TREE_EXTRA:-} ]]; then
    while IFS= read -r e; do [[ -n $e ]] && tree_pin_line "$e" "$e"; done \
      < <(printf '%s\n' "$REH_TREE_EXTRA" | tr ':' '\n')
  fi
}
# **And the knob's own effect is asserted, because the first version of it added NOTHING and printed
# as if it had.** `printf '%s' "$REH_TREE_EXTRA"` writes no trailing newline, so with a single path
# (`REH_TREE_EXTRA=/tmp/probe`, the exact form the usage block documents) the pipeline's only line is
# unterminated, and `while IFS= read -r` **drops an unterminated last line** - the loop body never ran,
# `tree_identity` returned the five live pins alone, and the banner below still said "identity extended
# with ...". Measured: the falsification run of 2026-09-24 18:09 wrote its probe 12 s into `happy-adb`
# (state window 18:09:25-18:09:49, write 18:09:37) and the state still printed `ok`. So the detector's
# own proof would have come back "it does not fire" while the thing that did not fire was the *knob* -
# [[mi4-silence-is-a-reading-only-if-success-is-silent]] at the level of the guard's test, and the
# second defect found in this block in one sitting. With N >= 2 colon-separated paths only the *last*
# was dropped, which is the shape that would have hidden it.
#
# The repair is `printf '%s\n'` **plus this check**, because the fix's correctness is not the property
# worth having: a knob whose effect is not counted can go inert again under any later edit to the loop.
# A refusal here is the difference between "the detector did not fire" and "the detector was never armed".
if [[ -n ${REH_TREE_EXTRA:-} ]]; then
  _want=$(( ${#REH_PINS[@]} + $(printf '%s\n' "$REH_TREE_EXTRA" | tr ':' '\n' | grep -c .) ))
  _got=$(tree_identity | grep -c .)
  if (( _got != _want )); then
    printf 'rehearse: REH_TREE_EXTRA is set to %s and the identity carries %d pin(s), not the %d\n' \
           "$REH_TREE_EXTRA" "$_got" "$_want" >&2
    printf '          (%d live + the extra ones) that were asked for. The knob exists to show the\n' \
           "${#REH_PINS[@]}" >&2
    printf '          TREE detector firing; a knob that silently adds nothing makes this run read as\n' >&2
    printf '          "the detector does not fire". Refusing rather than running it.\n' >&2
    trap - EXIT
    exit 1
  fi
  printf 'rehearse: identity extended with %s - the FALSIFICATION knob, not the live pin set.\n' \
         "$REH_TREE_EXTRA"
  printf '          The five files in %s are pinned as well; this can only add.\n' "$TREE_DIR"
  printf '          %d pin(s) in the identity, as asked.\n' "$_got"
fi

# --- the state matrix --------------------------------------------------------------------------
#
# Every row is a state section 1-5 of run_and_capture.sh can reach, the markers that produce it, the
# exit code its own wording promises, and a line that must appear in its output. The `expect` lines
# are quoted from the runner, so a state that keeps its code but changes its message is caught too.
#
# The pin is applied in `run_state` and deliberately NOT in `reader_state` or in section C: both of
# those run `--summarise`, which reaches only `summarise_log` and never reads `out/stage90/` (the
# runner's own gate and plan are below the `SUMMARISE_ONLY` branch, `run_and_capture.sh:2383`), so a
# TREE verdict there would invalidate a cell whose reading the tree cannot have touched.

pass=0; fail=0; invalid=0
declare -a ROWS=()

run_state() {
  local name=$1 expect_code=$2 expect_text=$3; shift 3
  local d=$WORK/$name
  # `FORBID` is the assertion of **absence**, and it is a local so a state that does not set it cannot
  # inherit the previous state's. Per 615, an absence assertion is only worth anything beside a sibling
  # that asserts the presence - the pair is what turns "it did not say X" into "X is what this state
  # must not say".
  #
  # **It is an array as of 620, and the reason is a state that has to forbid two sentences at once.**
  # `boot-call-fails` must not print what exit 2 says (the action "It needs a power press") *and* must
  # not print the adb plan line on a path where no adb call is made - two different claims about two
  # different parts of the runner, and a single-value `FORBID` would silently keep only the last one.
  # A silent drop is the failure mode this file exists to catch, so the multiplicity is the point.
  local -a FORBID=()
  rm -rf "$d" "$STATE"; mkdir -p "$d" "$STATE"
  # every state starts with the phone in fastboot unless it says otherwise
  touch "$STATE/fastboot_up" "$STATE/enum_after_boot" "$STATE/capture_ok"
  printf '/dev/null' > "$STATE/capture_source"
  local logfile=$d/last_kmsg.txt
  for marker in "$@"; do
    case $marker in
      adb_up)                touch "$STATE/adb_up" ;;
      adb_unauthorized)      touch "$STATE/adb_up" "$STATE/adb_unauthorized" ;;
      adb_offline)           touch "$STATE/adb_up" "$STATE/adb_offline" ;;
      no_fastboot)           rm -f "$STATE/fastboot_up" ;;
      reboot_disables_fastboot) touch "$STATE/reboot_disables_fastboot" ;;
      fastboot_two)          touch "$STATE/fastboot_two" ;;
      fastboot_other)        touch "$STATE/fastboot_other" ;;
      qdl_return)            touch "$STATE/qdl_return" ;;
      leave_after:*)         printf '%s' "${marker#leave_after:}" > "$STATE/leaves_after" ;;
      boot_fail_rc:*)        printf '%s' "${marker#boot_fail_rc:}" > "$STATE/boot_fail_rc" ;;
      boot_sent_rc:*)        printf '%s' "${marker#boot_sent_rc:}" > "$STATE/boot_sent_rc" ;;
      no_serial_ever)        touch "$STATE/no_serial_ever" ;;
      return_after:*)        printf '%s' "${marker#return_after:}" > "$STATE/ret_after" ;;
      forbid:*)              FORBID+=("${marker#forbid:}") ;;
      dmesg_unreadable)      touch "$STATE/dmesg_unreadable" ;;
      no_enum_after_boot)    rm -f "$STATE/enum_after_boot" ;;
      capture_fails)         rm -f "$STATE/capture_ok" ;;
      log_exists)            printf 'the previous run\n' > "$logfile" ;;
      log_missing)           : ;;
      capture_is_a_real_log) cp "$REH_PAYLOAD_LOG" "$STATE/capture_source" ;;
      # **An unrecognised marker is a refusal, and 628 measured why it has to be.** This `case` had no
      # default, so a typo in a cell's marker list - or a marker renamed in one place and not the other -
      # was **silently ignored**, and the cell then ran in a *different state* than the one its label
      # names while still printing `ok`. That is a cell that cannot reach the state it claims to test,
      # which is m639's shape in the harness's own wiring, and it is the failure mode a green table
      # cannot show. Found by writing exactly such a typo into the new `fastboot-plan-line` row and
      # noticing that nothing objected.
      *) printf 'rehearse: unknown state marker %s (cell %s)\n' "$marker" "$name" >&2
         printf '          The marker would have been ignored and the cell run in a different state\n' >&2
         printf '          than its label names. Refusing.\n' >&2
         trap - EXIT; exit 1 ;;
    esac
  done
  # **And an absence needs a positive expectation beside it, which this section did not require until
  # 631 - and the reason it was missing is a false claim in a comment.** `reader_state` has carried this
  # guard since 626, and its own comment says it is "the guard the path cells below carry for the same
  # reason". **The path cells did not carry it.** So the rule was written down, exercised in one section,
  # and asserted to exist in the other by a sentence nothing executes - 604's class ("a claim in a comment
  # is not a check") turning into a *reason not to look*, because a reader who trusts the comment has no
  # reason to go and check whether `run_state` really refuses one.
  #
  # **Measured, not argued (631)**: a cell written as
  # `run_state PROBE-ABSENCE-ONLY 0 "" 'forbid:The device did not come back'` - no expectation at all,
  # one forbid - printed `ok    PROBE-ABSENCE-ONLY           exit=0`. Accepted, on a green table row. That
  # is 628's defect one layer down and in the other section: there, only the *fastboot* half of the plan
  # line went unasserted; here, a whole row can carry nothing but an absence, and an absence is satisfied
  # by a state that reached nothing, printed nothing and called nothing.
  #
  # The refusal is the exact mirror of `reader_state`'s, including its wording, so the two sections'
  # rules are one rule rather than two that agree today.
  if (( ${#FORBID[@]} > 0 )) && [[ -z $expect_text ]]; then
    printf 'rehearse: run_state %s asserts only an absence; give it a positive expectation too\n' \
           "$name" >&2
    printf '          a command that failed to run at all also "did not say" the thing, so this row\n' >&2
    printf '          would pass on a runner that reached none of the state its label names.\n' >&2
    trap - EXIT; exit 1
  fi
  local out=$d/out.txt err=$d/err.txt
  # **`RETURN_TIMEOUT=6` rather than 3, and it is 617's code that needs the second poll.** At 3 the
  # loop makes exactly one check before its `sleep 3` and then exits, so nothing that takes any time
  # at all can be seen *inside* the wait - every state would land in the post-wait block and the
  # in-wait branches would stop being exercised. Six gives two polls: a return delayed to 2 s is seen
  # inside the wait (states 1/2/9) and one delayed past the window (10 s, the port-advance state) is
  # not. It also keeps the battery's clock honest against the stub's - the stub measures elapsed
  # seconds, so the window it is measured against has to be long enough to contain a real poll.
  # **`FB_AMBIG_WAIT=3`, not the runner's 60.** The wait is a time budget and the battery is what it
  # is measured against, so it is shortened here for the same reason `RETURN_TIMEOUT` is - and it has
  # to stay *longer than one poll* (the runner sleeps 2 s), or the wait's refetch would never happen
  # and 618's cell would be testing the refusal path while claiming to test the wait.
  local ident_before
  ident_before=$(tree_identity)
  LOGFILE=$logfile RETURN_TIMEOUT=6 CAPTURE_WAIT=1 FB_AMBIG_WAIT=3 \
    timeout 120 bash "$RUNNER" --allow-xnu-entry > "$out" 2> "$err"
  local code=$?
  # **The two reads that bracket the state, and the reason they are here rather than at the end.** The
  # identity is taken immediately before and immediately after the runner call, so what it witnesses is
  # *this* state's window and not the whole battery's: a tree that moved once between state 3 and state 4
  # invalidates neither of them (each saw a constant tree), while the state that straddled it is the one
  # that gets the TREE verdict. Taking it is O(27 MB) of hashing per read - ~0.1 s - against a state that
  # takes ~25 s, so it is measured rather than traded against.
  local ident_after
  ident_after=$(tree_identity)
  local moved=0 motion=""
  if [[ $ident_before != "$ident_after" ]]; then
    moved=1
    motion=$(diff <(printf '%s\n' "$ident_before") <(printf '%s\n' "$ident_after") | grep '^[<>]' | head -6)
  fi
  local ok=1 why=""
  if (( code != expect_code )); then ok=0; why="exit $code, promised $expect_code"; fi
  # The expectation is met by stdout **or** stderr. Half of these states end in `die`, and `die`
  # writes to stderr - so a check on stdout alone would report "the runner did not say X" about a
  # runner that said X on the other stream. That is the harness's own version of the defect this
  # whole file is about, and it is why the two streams are tested together rather than assumed.
  if [[ -n $expect_text ]] && ! { grep -qF -- "$expect_text" "$out" || grep -qF -- "$expect_text" "$err"; }; then
    ok=0; why="${why:+$why$'\n'}    did not say (either stream): $expect_text"
  fi
  # The absence half. Checked on both streams for the same reason the expectation is, and every
  # forbidden phrase is checked rather than the last one (620: the array).
  local _f
  for _f in ${FORBID[@]+"${FORBID[@]}"}; do
    if grep -qF -- "$_f" "$out" || grep -qF -- "$_f" "$err"; then
      ok=0; why="${why:+$why$'\n'}    said what this state must NOT say: $_f"
    fi
  done
  # a stub refusal means the state reached code the rehearsal does not model - that is a refusal,
  # not a pass, whatever the exit code said
  if grep -q 'rehearse-stub: unhandled\|rehearse-stub: REFUSING\|rehearse-stub: a bare\|rehearse-stub: fastboot boot pinned' "$err"; then
    ok=0; why="${why:+$why$'\n'}    the stub refused: $(grep -o 'rehearse-stub: .*' "$err" | head -1)"
  fi
  local nl; nl=$(printf '%s\n' "$why" | grep -c . || true)
  # **TREE outranks both.** A state whose inputs moved has no usable reading in either direction: its
  # exit code may be the runner refusing a torn tree (which is correct behaviour and would still be
  # printed as `FAIL exit 1, promised 0`), and a state that *passed* is no better evidence - it may have
  # read a tree that was briefly whole. So the verdict is withheld rather than guessed, and the reader
  # is told which pin moved instead of which sentence was missing.
  if (( moved == 1 )); then
    invalid=$((invalid+1))
    printf '  TREE  %-28s the pinned tree changed while this state ran - NO VERDICT\n' "$name"
    printf '%s\n' "$motion" | sed 's/^/        | /'
    if (( VERBOSE == 1 )); then sed 's/^/        | /' "$out" | tail -20; fi
  elif (( ok == 1 )); then
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
# `fastboot_two` is the *permanent* ambiguity: the neighbour never leaves, so this cell now covers
# both halves of 618's change at once - the guard waits its whole budget and then refuses with the
# same message it would have used immediately. Its expected line moved when the refusal was reworded
# (`fastboot lists 2 device(s)` -> `did not settle to`), because the new sentence has to be true for
# N = 0, 1 and 2 rather than only for the case the old one named.
run_state two-devices-in-fastboot      1 "did not settle to"                             fastboot_two
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
# 5c. **The state the listing does not tell you about, and the one 619 exists for.** `adb devices` prints
# `SERIAL<TAB>STATE`, and before 619 mode detection matched field 1 alone - so all seven states selected
# the adb branch. From `unauthorized` the reboot fails, the 30-poll wait for fastboot expires, and the
# run died with `device did not appear in fastboot`: exit 1 either way, but **sixty seconds later, on a
# message about fastboot, with a press spent** - and with nothing in the text to tell the operator that
# the device was sitting there waiting for an RSA prompt. The cell asserts the message that names the
# state, and **forbids the fastboot sentence**, which is what the broken version prints.
#
# Two states for one branch on purpose: the message interpolates `$ADB_STATE`, so `adb-offline` fails a
# version that hard-codes `unauthorized` while the first cell passes it. One cell per branch is the rule;
# this is one cell per *value the branch reads*, which is the narrower thing.
run_state adb-unauthorized            1 "its state is 'unauthorized'" adb_unauthorized no_fastboot \
                                      'forbid:device did not appear in fastboot'
run_state adb-offline                 1 "its state is 'offline'" adb_offline no_fastboot \
                                      'forbid:device did not appear in fastboot'
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
# **The state 618's wait exists for, and the one that was refused before it.** `leave_after:3` puts the
# neighbour's two-device list on the runner's three calls up to and including the guard's first read,
# and a single-device list on the wait's refetch - so the run must *wait* and then boot, and the
# assertion is that it reaches the happy path at all. Measured: deleting the wait makes this cell fail
# (exit 1, `cannot act on a` in the output, which is also what it forbids here).
run_state neighbour-leaves-fastboot  0 "reading the log this run captured" leave_after:3 \
                                       capture_is_a_real_log 'forbid:cannot act on a'
# **And the case that makes the condition stronger than a count.** Here the neighbour's list is
# followed by a list holding the *stranger* alone - the phone has left fastboot and the other device
# has not. A bare `count != 1` test would see `1`, pass, and reach `fastboot boot -s 4a2fe00b` against a
# bus where that serial is absent: `fastboot`'s own error, for which this file's exit contract has no
# clause. The runner must refuse that itself. The stub refuses such a boot too (see its boot arm), so
# the count-only version fails this cell on the *message* rather than passing on fastboot's error.
run_state fastboot-other-after-wait  1 "did not settle to" leave_after:3 fastboot_other
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

# --- 620: the boot call's own status, which had no cell because it had no behaviour to check -------
#
# Every device call in the runner is guarded except the one that boots the payload, and the battery
# had cells for every *pre*-boot refusal and none for the call itself. Three now, and they are not
# three spellings of one state: the two `boot_fail_rc` / `boot_sent_rc` arms differ on **which side of
# the send** the status came from, which is the thing the runner must not decide for itself.
#
# `boot_fail_rc:1` is the send side: nothing was booted, so no return can appear, and the operator
# must be told that the two facts together are *not* exit 2. **The forbid list is the assertion**, and
# it is why `FORBID` became an array in this step: `It needs a power press` is the sentence that costs
# the press (exit 2's action), and `sudo adb -s 4a2fe00b reboot bootloader` is the plan line the runner
# used to print unconditionally and does not print on this path, because with the device already in
# fastboot it issues no adb command at all. Two claims about two different parts of the runner, and a
# single-value `FORBID` would have kept only the last one - silently, which is this file's whole
# subject.
run_state boot-call-fails            1 "did not report success" boot_fail_rc:1 no_enum_after_boot \
                                       'forbid:It needs a power press' \
                                       'forbid:sudo adb -s 4a2fe00b reboot bootloader'
# And this is the cell that shows what the abort cost, which is the argument for capturing the status
# rather than letting `set -e` end the script on it. The status is non-zero *after* the image was sent,
# so the payload is running and the return appears on its own; a runner that aborted on the non-zero
# status would exit 1 with fastboot's message and **never reach sections 4 and 5** - the press spent
# and the only reading lost, because the log does not survive the phone's next power cycle. `exit 0`
# here is therefore the assertion that the reading was taken, and the expected text is the note that
# records the status instead of inheriting it.
run_state boot-call-fails-after-send 0 "fastboot boot exited" boot_sent_rc:1
# The presence half of the plan-line pair above: on the adb path the line IS the plan, so it must be
# printed - otherwise "print it never" would satisfy `boot-call-fails` while making the adb path's own
# recorded plan false in the other direction.
run_state adb-plan-line              0 "sudo adb -s 4a2fe00b reboot bootloader" adb_up
# **And the other half of that pair, which 628 measured was missing - on the branch the press takes.**
# Every state in this section starts with the phone in fastboot and no adb (`fastboot_up` is the default
# marker), which is exactly what Vol-Down + Power produces, so the `fastboot)` arm of the runner's
# plan-line `case` is the *normal* path. Nothing asserted its text: the only cell that named it was
# `boot-call-fails`'s **forbid** of the adb line, and a forbidding expectation passes on a `case` that
# prints nothing at all. Measured rather than argued - with the `fastboot)` arm replaced by `: ;;` the
# whole of sections A and B stayed green, **19 ok / 0 failed and 15 ok / 0 failed**, so the branch the
# press takes could have lost its plan line with no cell noticing (613's rule: to assert a phrase's
# absence the detector must first be seen to see it when present; 614's: a verifier whose only shown
# behaviour is refusal).
#
# It carries **no marker at all** - the default state is already the press's - and the `forbid:` half
# makes it the converse of `adb-plan-line` rather than a second assertion of the same line.
run_state fastboot-plan-line         0 "the device is already in fastboot, so this run issues no adb" \
                                       'forbid:sudo adb -s 4a2fe00b reboot bootloader'

printf '\n  %d ok, %d failed' "$pass" "$fail"
if (( invalid > 0 )); then printf ', %d with no verdict (the tree moved)' "$invalid"; fi
printf '\n'
# **Two exits, two claims, and the order is the point.** A FAIL is outranks TREE, because one real
# misbehaviour ends the question; but a run with ONLY TREE cells is not exit 1, because exit 1 would say
# "a live-path state does not behave as its own contract says" about states whose contract inputs were
# moving. It is exit 2: a distinct verdict, so a reader (or a script) can tell "the runner is wrong" from
# "this run proves nothing about the runner" without reading prose.
if (( fail > 0 )); then
  printf '\nREFUSING: at least one live-path state does not behave as its own contract says.\n'
  printf 'Every output is kept under %s while this shell lives; re-run with -v to see them.\n' "$WORK"
  trap - EXIT
  exit 1
fi
if (( invalid > 0 )); then
  printf '\nNO VERDICT: the tree the live path reads changed while %d state(s) were running (above),\n' "$invalid"
  printf 'so those states were neither green nor red. That is NOT a statement about the runner: %s\n' "$TREE_DIR"
  printf 'was rewritten under a state that had already read part of it, and the runner'\''s gate refusing a\n'
  printf 'torn tree is what it is supposed to do. Run the battery against a quiescent tree - nothing\n'
  printf 'building, in either session - for a reading of the runner. (636 is the same hazard from the\n'
  printf 'press side: a build with the catch armed swaps the arm the gate is checking.)\n'
  printf 'Every output is kept under %s while this shell lives; re-run with -v to see them.\n' "$WORK"
  trap - EXIT
  exit 2
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
#
# **And the live records are written in the shape their writer emits, which the capture's own shape
# is not (626).** `entry_live_write` hands its key to `entry_write_kv`, which puts down a leading
# space and then `key=0x%08x` and nothing else (`entry_stubs.c:2419` and `:2499`), so a live record in
# a real capture is ` xnu_live_poll_seq=0x00000001` - **bare**, with no `MI4IOS6_STAGE90` in the line
# at all; only the payload's own *result* records carry the `MI4IOS6_STAGE90_XNU loader_` prefix.
# These fixtures used to prefix the live records too, and it never mattered because every pattern in
# the reader is a loose substring match. It is named here because it very nearly did matter: 626's
# first draft asked whether the payload *record* carried each arm key, which is a stricter pattern,
# and on that form these fixtures stay green (the prefix is present) while **every real capture** -
# 520, 533 and 513's pair all carry the keys bare - would have lost `door_seq` and been read as "the
# boot never reached the idle". A fixture that is not the artifact's shape cannot falsify a pattern
# change, which is the whole reason it is written the writer's way now.

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
    printf ' xnu_live_cap=0x00002000\n'
    [[ $variant == *-capped ]] && printf ' xnu_live_capped=0x00002000\n'
    local p=1
    if [[ $base != no-door-seq ]]; then
      while (( p <= 16777216 )); do            # 1, 2, 4, ... 0x1000000 - 25 records
        if [[ $base == door-max-0x8000 ]] && (( p > 32768 )); then break; fi
        printf ' xnu_live_idle_seq=0x%08x\n' "$p"
        printf ' xnu_live_door_seq=0x%08x\n' "$p"
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
    printf ' xnu_live_open_seq=0x00000001\n xnu_live_open_error=%s\n' "$open1_err"
    if [[ $base != goal-truncated ]]; then
      printf ' xnu_live_open_seq=0x00000002\n xnu_live_open_error=0x00000002\n'
      printf ' xnu_live_read_seq=0x00000001\n xnu_live_read_nbytes=0x00000004\n'
      printf ' xnu_live_read_ret_lo=0x00000004\n xnu_live_read_buf=0x00102000\n'
      printf ' xnu_live_read_word_before=0x00102000\n xnu_live_read_word_after=0xfeedface\n'
      printf ' xnu_live_getpid_seq=0x00000001\n xnu_live_getpid_value=0x00000001\n'
      printf ' xnu_live_exit_seq=0x00000001\n xnu_live_exit_pid=0x00000002\n xnu_live_exit_rval=0x00000003\n'
      printf ' xnu_live_wait_seq=0x00000001\n xnu_live_wait_seq=0x00000002\n'
      printf ' xnu_live_wait_status=0x00000300\n xnu_live_wait_error=0x0000000a\n'
      printf ' xnu_live_ast_seq=0x00000001\n xnu_live_ast_seq=0x00000002\n'
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
      printf ' xnu_live_poll_seq=0x%08x\n' "$i"
      printf ' xnu_live_poll_timeout_ms=0x%08x\n' "$tmo"
      printf ' xnu_live_poll_error=0x00000000\n'
      printf ' xnu_live_poll_retval=0x00000000\n'
      printf ' xnu_live_poll_ticks=0x024c072a\n'
    done
    # the payload's own arm record, which every real capture carries (533 does) and which the
    # reader's rung-1 FAIL narration is **guarded on**: with it the reader may say the SoC's reset
    # is not what stopped the park, without it it must say that the interval is cited and not read.
    # The `poll-seq-2-no-arm` variant is the one that removes it, so both branches are states here.
    [[ $base == poll-seq-2-no-arm ]] || \
      printf 'MI4IOS6_STAGE90_XNU loader_hw_watchdog_counter_running=0x00000001\n'
    [[ $base == no-poll-over ]] || printf ' xnu_live_poll_over=0x00000008\n'
    printf ' xnu_live_sleh_user=0x00000001\n'
    # **626: the two sides of the arm test, one state each, differing in one kind of key.** Everything
    # above is the *sleeper* signature, which is why every variant here takes the arm's ladder - so
    # until 626 nothing in this battery had ever run the *branch choice*, and the choice was the thing
    # 622 measured: a behavioural key present with no `repair_seq` failed the six-term test and the log
    # was read as the baseline arm, printing the other arm's death block with the rung lines simply
    # missing. `slot-cwe-only` is that log, built the honest way (one `win` record added to this same
    # signature, nothing else changed), and `baseline-arm` is the same signature plus the
    # **structural** key, which is the one an image with the repair publisher must publish. A reader
    # that confuses the two has a cell red here rather than a paragraph somewhere.
    if [[ $base == slot-cwe-only ]]; then
      printf ' xnu_live_slot_cwe_win=0x00000005\n'
      printf ' xnu_live_slot_cwe_set=0x00000005\n'
      printf ' xnu_live_slot_cwe_calls=0x00000005\n'
    fi
    if [[ $base == baseline-arm ]]; then
      printf ' xnu_live_repair_seq=0x00000001\n'
      printf ' xnu_live_repair_caller=0x00000001\n'
      printf ' xnu_live_repair_before=0x00000001\n'
      printf ' xnu_live_repair_after=0x00000002\n'
      printf ' xnu_live_sip_seq=0x00000001\n'
      printf ' xnu_live_pce_seq=0x00000001\n'
      printf ' xnu_live_wfi_seq=0x00000001\n'
      printf ' xnu_live_slot_cwe_win=0x00000005\n'
    fi
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
    printf ' xnu_live_cap=0x00002000\n'
    for ((i=0; i<cap-1; i++)); do      # the capacity line is itself record #1 of the cap
      printf ' xnu_live_fill_seq=0x%08x\n' "$i"
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
  # **`forbid:` is how a row asserts an absence, and it is scoped to this function the way section B's
  # `reader_state` is scoped to the reader.** 626 needed it: the cell that matters is not only "the
  # sleeper arm's ladder ran" but "the baseline arm's death block did not", and a row that could only
  # say what must appear would let a reader print *both* and pass.
  local -a WANT=() FORBID=()
  local w
  for w in "$@"; do
    case $w in
      forbid:*) FORBID+=("${w#forbid:}") ;;
      *)        WANT+=("$w") ;;
    esac
  done
  # **And an absence needs a positive expectation beside it**, which this section has required since 626
  # and which the path section now carries too. **The sentence that used to be here - "the guard the path
  # cells below carry for the same reason" - was false, and 631 measured that it was**: `run_state`
  # accepted a cell with no expectation and one `forbid:` and printed it as `ok`. So the comment did not
  # merely fail to help; it named the other section as already guarded, which is a reason for the next
  # reader not to look there. The two sections now refuse the same shape with the same wording.
  if (( ${#FORBID[@]} > 0 )) && (( ${#WANT[@]} == 0 )); then
    printf 'rehearse: reader_state %s asserts only an absence; give it a positive expectation too\n' \
           "$variant" >&2
    trap - EXIT; exit 1
  fi
  local log=$WORK/reader-$variant.log
  if [[ $variant == cap-full ]]; then mk_capfull_log "$log"; else mk_sleeper_log "$variant" "$log"; fi
  local out=$WORK/reader-$variant.out err=$WORK/reader-$variant.err
  bash "$RUNNER" --summarise "$log" > "$out" 2> "$err"
  local code=$? ok=1 why="" want
  (( code == 0 )) || { ok=0; why="exit $code, promised 0"; }
  for want in "${WANT[@]}"; do
    grep -qF -- "$want" "$out" || { ok=0; why="${why:+$why$'\n'}    the reader did not say: $want"; }
  done
  for want in "${FORBID[@]}"; do
    grep -qF -- "$want" "$out" && { ok=0; why="${why:+$why$'\n'}    said what this state must NOT say: $want"; }
  done
  if (( ok == 1 )); then
    rpass=$((rpass+1))
    # The table says how many expectations the row carried, because a row with two of them that
    # printed only its first would look exactly like a row with one - and the second is the one that
    # tests the branch. A pass that hides what it checked is the shape this file exists to catch.
    # **626: the forbidding expectations are counted in that number too**, for the same reason in the
    # other direction - a row that asserts an absence and prints only its positive half would read as
    # a row that never checked the absence at all.
    local total=$(( ${#WANT[@]} + ${#FORBID[@]} ))
    if (( total > 1 )); then
      printf '  ok    %-22s %s (+%d more)\n' "$variant" "${WANT[0]}" "$(( total - 1 ))"
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
#
# **And the ladder's own reading of this state is asserted line by line as of 630, because until then
# it was asserted only through a proxy.** Every row that touched the ladder asserted a way it can FAIL -
# `door-max-0x8000` ("stops at or below 0x8000"), `poll-seq-2` ("no poll record past the second"),
# `small-timeout` (the 40 ms ask) - and the two rows that could have caught a *PASS* changing assert the
# arm clause, which is a paragraph gated on `verdict_ok` and therefore **downstream of rungs 1 and 1b
# only**. 630 measured both halves of that: a rung-1 flip (`poll_seq_max > 2` raised to `> 99`) and a
# rung-1b flip (the `poll_tmo_max >= park_min` test raised) each suppress the arm clause, so those two
# rungs were covered indirectly; and a rung-0 and rung-3 flip (the `door_max > 32768` threshold raised,
# `[[ -n $park_over ]]` made unsatisfiable) leaves the arm clause and every other cell untouched -
# **all fifteen reading rows stayed green on the harness's own fixture logs**, measured cell by cell.
# Rungs 0, 2, 3 and 4 had no cell at all, and the witness the owed run is pre-registered against -
# `poll_seq` past the second with the largest ask at or above the park's threshold - is read off those
# lines. The five added expectations are the witness's own reading, on the same fixture.
reader_state predicted        "this arm did what it was built to do at the point that matters" \
                              "live channel: not full" \
                              "=> SLEEPER ARM (594's switch)" \
                              "went past the last record every previous boot published" \
                              "a poll came back after the second" \
                              "it is the park and not a stray ask" \
                              "the park's console group is not in the log, although its poll returned" \
                              "rung 3: xnu_live_poll_over"
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
# **626: the branch choice itself, one row per side, and the row that was the defect.** Every state
# above is the sleeper signature, so before 626 not one of them ran the *choice* - and the choice is
# what 622 measured. `slot-cwe-only` is the sleeper signature plus ONE behavioural key: the reader must
# say the two definitions disagree, must keep the ladder's own PASS line (so the arm's reading is not
# lost), and must NOT print the baseline arm's death block. `baseline-arm` is the same signature plus
# the **structural** key, which is the only one that makes a log the baseline arm's, and there the
# death block *is* the reading - so the two rows assert opposite halves of one decision.
reader_state slot-cwe-only    "AND A DISAGREEMENT: a behavioural key IS present in this log" \
                              "this arm did what it was built to do at the point that matters" \
                              'forbid:from the pair of SCTLR readings the entry wrapper publishes'
reader_state baseline-arm     "=> BASELINE ARM, decided on repair_seq present" \
                              "the idle window's near end"
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
  # **The rule stated in the comment below was, until 631, stated in three sections and enforced in
  # one.** `reader_state` got the refusal in 626; `run_state` got it in 631; this is the third, and its
  # comment claimed the rule was already carried - "So MUST_NOT_SAY is only ever asserted together with
  # a positive expectation" is a sentence about every caller, and nothing checked it. A cell with an
  # empty `MUST_SAY` and a non-empty `MUST_NOT_SAY` would have been accepted and printed `ok`, for the
  # shape 631's other two sections now refuse: the absence is satisfied by a runner that printed nothing
  # at all, which is what a cell whose invocation never reached the runner also produces. Refused before
  # the runner is called, with `reader_state`'s and `run_state`'s own wording.
  if [[ -z $must && -n $mustnot ]]; then
    printf 'rehearse: path_state %s asserts only an absence; give it a positive expectation too\n' \
           "$name" >&2
    printf '          a command that failed to run at all also "did not say" the thing, so this row\n' >&2
    printf '          would pass on an invocation that never reached the runner.\n' >&2
    trap - EXIT; exit 1
  fi
  out=$( cd "$dir" && bash "$RUNNER" "$@" 2>&1 ); rc=$?
  local why=""
  (( rc == want )) || why="exit $rc, promised $want"
  if [[ -n $must ]]; then
    printf '%s' "$out" | grep -qF -- "$must" || why="${why:+$why; }did not say: $must"
  fi
  # The absence assertion carries the same guard the shrink cells in the revert-set rehearsal needed:
  # a command that failed to run at all also "did not say" the thing - which is why the refusal above
  # refuses an absence-only cell rather than trusting this sentence about its callers.
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
