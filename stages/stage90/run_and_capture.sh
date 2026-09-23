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
#   1  the gate refused, the device was not found, or - after the boot - the host could not
#      write the log where it wanted to (an unwritable $LOGFILE in a sticky /tmp: 511, 512)
#   2  the payload ran and the device did NOT come back - a manual power press is needed
#      (the log will not survive a power cycle, so this is also a lost run)
#   3  the device returned to the HOST but no log was captured - the host's USB log saw the
#      phone enumerate again (and its SoC is running) while `adb devices` stayed empty, so
#      **this is not a hang** and must not be read as one: it is a failed attempt to capture
#      a log from a device that came back into a state adb cannot reach. Added 2026-09-22,
#      when this phone's Android bring-up failed twice in ten minutes (`2717:0368` for 18 s,
#      one window ending before `4ee7`) and exit 2 would have been the wrong reading.
#      **It has two producers, and the second one is step 5** (before 564, step 5 called
#      `die` here, i.e. exit 1, for the same state this code is defined by). **What separates
#      them is *when* the enumeration was seen, not whether adb came up** - and that
#      distinction is easy to state wrongly, because section 4's own condition ("the host log
#      shows the phone enumerating") is satisfied on *both* paths:
#        * the enumeration is seen **inside** the bounded wait -> `RETURNED=1`, section 4
#          returns normally, and the capture is step 5's problem -> **step 5** exits 3;
#        * the phone is silent for the **whole** wait and only then enumerates -> section 4's
#          `exit 3` below fires -> **section 4** exits 3.
#      At the default `RETURN_TIMEOUT` the first is what this phone does: 542 measured the
#      fastboot-to-Android handover at ~17 s, well inside 180 s. The second requires a silence
#      longer than the whole wait, so it is the rarer state - which is why a test that shortens
#      `RETURN_TIMEOUT` silently moves the producer (566 §3, corrected in 566c). A "1" printed
#      after a boot that reached step 5 is a host-side write failure and not a device state.

set -euo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
OUT=$REPO_ROOT/out/stage90
IMAGE=$OUT/stage90-qcdt.img

SERIAL=${SERIAL:-4a2fe00b}
LOGFILE=${LOGFILE:-/tmp/cancro-last_kmsg.txt}
RETURN_TIMEOUT=${RETURN_TIMEOUT:-180}
# The capture wait is a **second window and not a repeat of the one above**, which is the whole point
# of it: section 4's criterion is satisfied the moment the host log shows the phone enumerating, and on
# this phone the fastboot-to-Android handover is 17 s (542: `18d1:d00d` gone at 02:00:59, `2717:0368` at
# 02:01:16) with Android's own bring-up a further ~20 s to the `18d1:4ee7` that carries adb (522:
# 14:13:49 -> 14:14:09). adbd therefore does not exist for the first several seconds after the return is
# seen, so a single immediate `adb exec-out` at step 5 fails on the *normal* path. A read is free and
# cannot cost a run, so this bounds how long the capture keeps trying, and nothing else uses it.
CAPTURE_WAIT=${CAPTURE_WAIT:-90}

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

# --- the exit's own return address: derived when it is used, never a literal ---------------------
#
# The one criterion the idle-window block compares a run's abort against is `lr` at the exit's
# `pop {fp, pc}` - the return address the exit's own `bl FlushPoU_Dcache` pushes - and **an address a
# check compares against is the same object as an address a gate prints**: 520's defect was a gate
# narrating the previous image's hash out of a comment literal, and a pinned `lr` would be that
# defect one level down ([[mi4-a-claim-in-a-comment-is-not-a-check]]). So it is computed here, out
# of the entry image's own ELF, with no pinned address in the arithmetic either:
#
#   * `platform_cache_idle_exit`'s start and size come from the ELF's symbol table;
#   * its body is disassembled from those two numbers;
#   * the answer is the address of the instruction *after* the `bl` to `FlushPoU_Dcache` - which in
#     this build is `movw r0,#4516 / movt r0,#32853`, the load of `up_style_idle_exit` at
#     0x805511a4, so the address identifies itself to a reader who checks it.
#
# On any failure the callers fall back to `EXIT_POP_LR_LITERAL` **and print that they did**, so a
# re-read from a tree whose `out/` has been cleaned still answers - labelled, rather than silently
# unread, which is the direction this project keeps paying for.
EXIT_POP_LR_LITERAL=0x800462dc
exit_pop_lr_addr() {
  local elf=${1:-$OUT/xnu_arm_entry.elf} od=${OBJDUMP:-arm-none-eabi-objdump}
  local start size body ret
  [[ -r $elf ]] || return 1
  command -v "$od" >/dev/null 2>&1 || return 1
  read -r start size < <("$od" -t "$elf" 2>/dev/null \
    | awk '$NF == "platform_cache_idle_exit" { print "0x" $1, "0x" $5; exit }') || return 1
  [[ $start =~ ^0x[0-9a-fA-F]+$ && $size =~ ^0x[0-9a-fA-F]+$ ]] || return 1
  body=$("$od" -d --start-address="$start" --stop-address=$(( start + size )) "$elf" 2>/dev/null) \
    || return 1
  # The call site line carries the callee's name; the next line that begins with a hex address is
  # the instruction the `bl` returns to. The colon is stripped rather than required, because the two
  # disassemblers this project has disagree about whether to print a separate symbol line for an
  # address that is not a symbol (`llvm-objdump` does, `arm-none-eabi-objdump` does not) - and a
  # rule that assumed one of them would have found nothing under the other, which is how this
  # derivation failed on its first run.
  #
  # **The callee's name is matched by its tail, not spelled once.** 535's arm reaches this call through
  # `--wrap=FlushPoU_Dcache`, so in that image the disassembly reads `bl ... <__wrap_FlushPoU_Dcache>`
  # and a pattern written as `/<FlushPoU_Dcache>/` - the literal this line carried until 535 - matched
  # **nothing**: the derivation returned empty, the reader fell back to its pinned literal, and the
  # criterion stopped being derived from the image in exactly the run whose whole question is that call.
  # (Measured, not imagined: on 535's `12684433…` the pattern above found 0 call sites and the gate's
  # own copy of this derivation printed UNREAD for the same reason.) Any wrapper prefix is allowed, so
  # the rule is about the routine and not about the spelling the linker chose for the call.
  ret=$(printf '%s\n' "$body" | awk '
    /<[^<>]*FlushPoU_Dcache>/ { seen = 1; next }
    seen && $1 ~ /^[0-9a-f]+:?$/ { a = $1; sub(/:$/, "", a); print "0x" a; exit }') || return 1
  [[ $ret =~ ^0x[0-9a-fA-F]+$ ]] || return 1
  # A return address is the instruction after a `bl`: 4-byte aligned, inside the kernel's own VA
  # range (`pmap_kernel_va` is [0x80000000, 0xFFFEFFFF]), and not the function's own entry.
  (( ret % 4 == 0 )) || return 1
  (( ret >= 0x80000000 && ret <= 0xFFFEFFFF )) || return 1
  (( ret != start )) || return 1
  printf '0x%08x' "$ret"
}

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
    local cwe_win cwe_set cwe_calls pre_calls rtcpre_calls post_calls storm panics user_ones
    local sleh_lr="" sleh_pc="" sleh_sp="" sleh_seen=""
    local pop_lr="" pop_lr_src="" pop_death=0 pop_named=0 cache_arm=unread arm_seen=unknown
    local pce_up="" pce_ncpu="" verdict_ok=1
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
    # The exit wrapper calls `entry_slot_null_note(&g_slot_pre)`, `entry_slot_rtc_note(&g_slot_rtcpre)`
    # and then, after the real exit returns, `entry_slot_null_note(&g_slot_post)` - and it ends in a
    # tail branch rather than returning, so all three are reached in the same pass or the pass died at
    # the one before. Each is called once per pass, and all three use the same publish schedule
    # (`entry_stubs.c:6236`: `n <= 4 || power of two`) behind the same `entry_live_ready()` gate, so at
    # pass n all three counters equal n: **if one published, the others would have published had they
    # been reached.** That is what turns "post_calls is missing" from UNREAD into the reading this run
    # exists to produce - and the three cases are the death's own address.
    #
    # **The second of the three is `rtcpre`, not `rtcab`, and the two are different sites.** This
    # block read `slot_rtcab_calls` here until 558 and that was wrong: `g_slot_rtcpre` is the exit
    # wrapper's (`entry_trace.c:1973`, `entry_slot_rtc_note(&g_slot_rtcpre, entry_tpidrprw())`), while
    # `g_slot_rtcab` belongs to the **abort** path (`entry_stubs.c:1792`, inside `entry_note_sleh`,
    # `entry_slot_rtc_note(&g_slot_rtcab, thread)`) - so `rtcab` is a *storm* count, and 520's own log
    # shows it: 5 records (`1,2,3,4,8`) against 9 abort episodes, which is the schedule and not one
    # per exit pass. Keying the bracket on it was a false-localization generator: a pass that reached
    # the wrapper but took no abort would have missed the `DIED IN THE EXIT` branch below and printed
    # `DIED BEFORE THE EXIT ... the rtcPop reading did not publish` while the rtcPop reading was in the
    # log under its own name. (Found by the peer session reading the same three-note claim against the
    # source; verified here at both call sites before the change, not after.)
    pre_calls=$(keyval slot_pre_calls)
    rtcpre_calls=$(keyval slot_rtcpre_calls)
    post_calls=$(keyval slot_post_calls)
    storm=$(keyval sleh_storm)
    panics=$(grep -a -c 'panic.*sleh_abort' "$log" || true)
    user_ones=$(grep -a -c 'xnu_live_sleh_user=0x0*1' "$log" || true)

    # **The abort's own registers, from the keys this image publishes - not from the dump's text.**
    # 520's log carries the final abort as first-class fields (`:8341-8350`: `sleh_storm=9`,
    # `sleh_seen=9`, `pc=0x04b79074`, `lr=0x800462dc`, `sp`, `cpsr`, `fsr_frame`, `far_frame`),
    # identical to the human-readable dump at `:4006`. Two reasons this replaces the block's first
    # version, which grepped `lr: *0x800462dc` out of the dump text:
    #
    #  * **the same file holds an earlier, unrelated abort episode** (`:7651`, `lr=0x8029231c`,
    #    `pc=0x800176c0`, `seq=2` of nine), so a whole-file match on the text can be satisfied by the
    #    wrong episode, and a panic dump belonging to a sibling abort satisfies the pattern just as
    #    well as the death does. `keyval` takes the **last** publication - which is the fatal one,
    #    because the episode that panics is the last one that happens;
    #  * the dump line is space-padded (`r12:  0xde58b701  sp: ...`, two spaces), so a single-space
    #    pattern misses it - an *ambiguous* miss, which is the shape this project's measurement
    #    defects keep taking.
    #
    # (Peer session's finding, verified here against the log and against the frozen image: every
    # `xnu_live_sleh_*` and `xnu_live_pce_*` name this block reads is in
    # `out/stage90/xnu_arm_entry.bin`, `f202f246…`, one occurrence each.)
    sleh_lr=$(keyval sleh_lr)
    sleh_pc=$(keyval sleh_pc)
    sleh_sp=$(keyval sleh_sp)
    sleh_seen=$(keyval sleh_seen)

    # 535's arm, and it is extracted here rather than inside clause (5) because clause (2) needs it
    # too: that clause reads the SCTLR pair as a *cell*, and since 535 the cell has two images in it
    # (both leave the window as Apple left it). The seam's counter is what says which image this log
    # came from, and it is published by 535's own wrapper and by nothing earlier.
    seam_calls=$(keyval seam_calls)

    # The criterion the shape test compares against, **derived at run time** - see
    # `exit_pop_lr_addr` - and labelled when it could not be, so a re-read from a tree whose `out/`
    # has been cleaned says which it used instead of quietly comparing against a number nobody
    # recomputed.
    pop_lr=$(exit_pop_lr_addr 2>/dev/null || true)
    if [[ $pop_lr =~ ^0x[0-9a-fA-F]+$ ]]; then
      pop_lr_src="derived from $OUT/xnu_arm_entry.elf at run time"
    else
      pop_lr=$EXIT_POP_LR_LITERAL
      pop_lr_src="*** PINNED LITERAL - the entry ELF could not be read, so this criterion is NOT derived ***"
    fi
    pop_death=0
    [[ $sleh_lr == "$pop_lr" ]] && pop_death=1

    # **The lr test is the enable-off cell's criterion, and which cell this log is comes out of the
    # log too.** In the other arm of `platform_cache_idle_enter`/`_exit` the two `bl`s at
    # `0x80046304` (`InvalidatePoU_Icache`) and `0x80046308` (`flush_core_tlb`) are **taken**, and
    # then `lr` at the pop is whatever `flush_core_tlb` left there - not this address - so a plain
    # "lr != the criterion" test would print FAIL for a run behaving exactly as *that* arm predicts.
    # Both selectors are published by name, so neither is inferred: `up_style_idle_exit`/`real_ncpus`
    # by the enter wrapper (`xnu_live_pce_up`/`_ncpu`, 549) and the `SCTLR.C` pair's far side by the
    # same wrapper's note (`_cwe_set`, 540).
    pce_up=$(keyval pce_up)
    pce_ncpu=$(keyval pce_ncpu)
    if [[ $pce_up =~ ^0x[0-9a-f]+$ && $pce_ncpu =~ ^0x[0-9a-f]+$ ]]; then
      if (( pce_up == 1 && pce_ncpu == 1 )); then cache_arm=up1; else cache_arm=other; fi
    else
      cache_arm=unread
    fi
    arm_seen=unknown
    if [[ $cwe_set =~ ^0x[0-9a-f]+$ ]]; then
      if (( (cwe_set & 4) == 4 )); then arm_seen=522; else arm_seen=533; fi
    fi

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
    say "  lr criterion: ${pop_lr:-?}   ($pop_lr_src)"
    say "  this log's abort: xnu_live_sleh_lr=${sleh_lr:-absent} pc=${sleh_pc:-absent}"
    say "                    storm=${storm:-absent} episodes_seen=${sleh_seen:-absent}"

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
    # What separates the shapes is `pop_death` above - `xnu_live_sleh_lr` against the address the
    # exit's own `bl` returns to - and the criterion is *derived* at run time rather than pinned, so
    # this clause compares against the image that was booted rather than against a number written
    # down once. The test's scope is asserted too: it is the **enable-off** arm's criterion, which
    # the log itself says by publishing `up_style_idle_exit`/`real_ncpus`.
    if [[ $panics -eq 0 ]]; then
      say "  PASS  no 'panic ... sleh_abort' in the log - the idle exit retired its own epilogue"
      say "        rather than dying at its pop, which is past the frontier this phase has been"
      say "        measuring"
      # 547 section 4's third row, and it is a *falsifier* rather than a pass when the log is the
      # cell whose prediction is that it dies: the peer session's point, and the reason this line
      # is printed loudly instead of left inside the PASS sentence.
      if [[ $arm_seen == 533 ]]; then
        # **Which image the pair is in decides which of two readings this is, and since 535 the pair
        # alone does not say.** The third row is a falsifier for *533's* image - the one whose arm is
        # absent from the seam. In 535's image the operation is at the seam on that very pop, so a
        # panic-free boot is that arm retiring the epilogue it was built for, and printing "535 must
        # not be built as designed" over it would score the arm's success as its falsification. 533
        # did die at the pop (568 section 4), so the row is closed for its cell either way.
        if [[ $seam_calls =~ ^0x[0-9a-f]+$ ]] && (( seam_calls >= 1 )); then
          say "  RESULT  and this log's image is the later one in that cell - 535's, whose arm operates"
          say "        at the seam on that same pop. So this is 535's success and not 547 section 4's"
          say "        third row: the pass retired the epilogue the arm was built for. The row was a"
          say "        falsifier for 533's image and 533 died at the pop (568 section 4), so the"
          say "        mechanism stands for the cell. What this log now decides is how far the boot got"
          say "        past the exit, which is item (4), and clause (5)'s pair - read those before"
          say "        calling the frontier moved"
        else
          say "  FALSIFIER  and this log's pair is the enable-off cell's, whose prediction is that it"
          say "        DOES die at the pop - 547 section 4's third row. If the runner's exit code says"
          say "        the device returned, this falsifies 546's mechanism for this cell and 535 must"
          say "        not be built as designed. It is the loudest reading in this block"
        fi
      fi
    elif [[ $cache_arm == unread ]]; then
      say "  UNREAD  $panics 'panic ... sleh_abort' record(s), but the enter wrapper's own"
      say "        xnu_live_pce_up/_ncpu are absent, so the arm that decides what lr at the pop IS"
      say "        (${pop_lr:-?}) cannot be read - in the other arm the two skipped bls run and lr is"
      say "        whatever flush_core_tlb leaves. Nothing is claimed about the death's shape"
      verdict_ok=0
    elif [[ $cache_arm == other ]]; then
      say "  UNREAD  $panics 'panic ... sleh_abort' record(s), and xnu_live_pce_up=$pce_up /"
      say "        _ncpu=$pce_ncpu say the window took the OTHER cache arm - the one where the exit's"
      say "        InvalidatePoU_Icache/flush_core_tlb are taken, so lr at the pop is not ${pop_lr:-?}"
      say "        and this block's criterion does not apply to this log at all. No frozen image of"
      say "        this phase has that arm: it means the boot-args changed"
      verdict_ok=0
    elif [[ $pop_death -eq 1 ]]; then
      say "  PREDICTED  $panics 'panic ... sleh_abort' record(s) and xnu_live_sleh_lr=$sleh_lr is the"
      say "        return address of the exit's own bl FlushPoU_Dcache - i.e. the death is at its"
      say "        pop {fp, pc}, which is 547 section 4's prediction for the enable-off cell and *not*"
      say "        a failed arm. It is still not progress: the boot restarts on MACH Reboot instead"
      say "        of surviving the idle pass, which is 535's job"
      say "        (criterion $pop_lr, $pop_lr_src)"
      say "        Corroborate the shape by eye: the aborted pc should be a value that is NOT a"
      say "        valid address - 520's is pc=$sleh_pc sp=$sleh_sp at storm=$storm, and 547"
      say "        section 1 reads it as the popped word itself, pc = r11 & ~1"
      pop_named=1
    else
      say "  FAIL  $panics 'panic ... sleh_abort' record(s) and xnu_live_sleh_lr=${sleh_lr:-absent}"
      say "        is not the exit pop's own return address (${pop_lr:-?}) - a death of a shape that"
      say "        neither 547 section 4 nor 533 section 5 names, so it is a new fault and not this"
      say "        arm's reading. (521's non-return said the flush is not the answer and 526's took"
      say "        the capture out; a panic here says the cache state is not it either - which leaves"
      say "        the enter wrapper's own store in the window, 522's addition, as the next thing to"
      say "        bisect)"
      # A new shape in an image that *carries* the arm is a different candidate list from one that
      # does not: the seam's own operation writes two words, its clean half writes a whole line back,
      # and both are new since 533. So this says which of the two lists to read rather than leaving
      # the sentence above to be applied to an image it was not written for.
      if [[ $seam_calls =~ ^0x[0-9a-f]+$ ]] && (( seam_calls >= 1 )); then
        say "        **and this image carries 535's arm**, so the new fault has two more candidates"
        say "        before that one: the seam's own two stores to the exit's frame slot, and the clean"
        say "        half of its operation writing back a line whose other words are the idle thread's."
        say "        Clause (5) printed what the arm read there - read it against this death's registers"
        say "        ($sleh_lr/$sleh_pc/$sleh_sp) before any of them is blamed or cleared"
      fi
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
    # `arm_seen` is decided in the extraction block above rather than here, because clause (1) needs
    # it too (547 section 4's third row is a falsifier for the enable-off cell and a plain pass for
    # the other). The pair is the only thing in the log that can decide it - the entry image
    # publishes no build marker, only its readings - and FAIL is kept for the one shape no arm can
    # produce: `_win` with C *set*, which would say the window was never open where the image says.
    if [[ $cwe_calls =~ ^0x[0-9a-f]+$ ]] && (( cwe_calls >= 1 )); then
      if [[ $cwe_win =~ ^0x[0-9a-f]+$ ]] && (( (cwe_win & 4) == 0 )); then
        say "  PASS  slot_cwe_win=$cwe_win has SCTLR.C clear - the window really opened where the"
        say "        image says it does, which is what makes the second reading worth anything"
        if [[ $arm_seen == 522 ]]; then
          say "  ARM   522's arm: slot_cwe_set=$cwe_set has C set - the near-end re-enable ran and"
          say "        took, so this log is an image that re-enables the D-cache at the window's end"
        elif [[ $arm_seen == 533 ]]; then
          say "  ARM   533's arm: slot_cwe_set=$cwe_set has C clear - the window is left exactly as"
          say "        Apple left it, and *this* is that arm's expected reading, not a failed enable"
          # The pair decides the *cell*, and since 535 the cell has two images in it: 535 leaves the
          # window as 533 does (`STAGE90_XNU_IDLE_CACHE_ENABLE=0` in both), so this line names a cell
          # and not necessarily the image. Which image it is comes out of the log rather than being
          # assumed - `xnu_live_seam_*` is published by 535's arm and by nothing earlier - so the
          # sentence below is printed from that key and not from this pair.
          if [[ $seam_calls =~ ^0x[0-9a-f]+$ ]] && (( seam_calls >= 1 )); then
            say "        **and this log's image is the LATER one in that cell**: xnu_live_seam_calls is"
            say "        present, so the arm under test is 535's, whose cache settings are 533's. Read"
            say "        clause (5) for what that arm's own instrument saw"
          else
            say "        (no xnu_live_seam_* in this log, so the image is the earlier one in this cell"
            say "        and 535's arm is not in it)"
          fi
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
      && [[ $rtcpre_calls =~ ^0x[0-9a-f]+$ ]] && (( rtcpre_calls >= 1 )); then
      say "  DIED IN THE EXIT  pre_calls=$pre_calls and rtcpre_calls=$rtcpre_calls both published and"
      say "        slot_post_calls did not: the pass reached the wrapper, took the rtcPop reading and"
      say "        got as far as the call, and did not come back through it - so the death is inside"
      say "        platform_cache_idle_exit, which is 520's pop {fp, pc} at the same pc. That is a"
      say "        localization and not a missing reading: the three notes share one schedule and one"
      say "        gate, so a site that published proves the later ones were reachable."
      if [[ $pop_named -eq 1 ]]; then
        say "        And it agrees with clause (1): xnu_live_sleh_lr=$sleh_lr is the pop's own return"
        say "        address, which is 547 section 4's prediction for the enable-off cell - so the two"
        say "        clauses localize the same instruction, and this is not scored as a failure."
        say "        (This exemption is conditioned on **both**: an attributed death *and* a panic."
        say "        post_calls absent with no panic anywhere stays loud - the bracket's absence is"
        say "        informative precisely because all three publishers share one schedule and one"
        say "        gate, 538, and it must not be excused by an lr nobody saw.)"
      else
        say "        **and clause (1) did not find the pop's own return address (${pop_lr:-?}) in this"
        say "        log's abort**, so the pass died inside the call but somewhere other than the"
        say "        instruction 547 section 4 names - a new fault, not this arm's prediction arriving."
        say "        Note that a log with no panic at all takes this branch, and is meant to: absence"
        say "        of the bracket is not absence of a death"
        verdict_ok=0
      fi
    elif [[ $pre_calls =~ ^0x[0-9a-f]+$ ]] && (( pre_calls >= 1 )); then
      say "  DIED BEFORE THE EXIT  pre_calls=$pre_calls published but the rtcPop reading"
      say "        (slot_rtcpre_calls) did not, so the pass died between the two notes - earlier than"
      say "        520's death and a different fault"
      verdict_ok=0
    else
      say "  UNREAD  none of the wrapper's three bracket publishers is in the log (pre_calls,"
      say "          rtcpre_calls, post_calls all absent), so this log cannot say where the pass died."
      say "          The likely causes are a last_kmsg ring that wrapped past them and a run that"
      say "          never reached the idle exit at all."
      say "          (slot_rtcab_calls is NOT one of the three - it is the abort path's counter, so"
      say "          its presence or absence says nothing about whether the wrapper was reached.)"
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
    # (5) the seam's own instrument: the exit's own call, and the two pairs of words its readings decide
    #
    # **This is the arm's own instrument, and it prints only for a log that carries it.** 535 and 572 both
    # wrap `FlushPoU_Dcache` and keep only the call whose return address is the exit's own `bl`, so
    # `xnu_live_seam_*` is published by those images and by no earlier one. Like the block the pair above
    # is gated on, this is self-selecting rather than a verdict on every log: a log without the keys is
    # a log from an image that does not carry the arm, and silence is the right reading for it - there
    # is no build marker in the entry image for this block to test instead (clause (2)'s note).
    #
    # **The criterion is the one clause (1) already derived, not a second copy of the address.** `pop_lr`
    # above is `platform_cache_idle_exit`'s own `bl FlushPoU_Dcache` + 4, read out of this tree's entry
    # ELF at run time; the seam's `lr` must be exactly that, because the seam *is* that call. Writing it
    # again here would be 556's defect - one address with two definitions and only one of them bound to
    # the image - and the two would drift the first time either moved.
    #
    # **The two pairs are the near-side measurement 546 could only infer.** With `SCTLR.C` clear the
    # window's stores are DRAM-only, so `_b0`/`_b1` are the frame the exit's `push` wrote *as memory
    # holds it*, read with the cache off before the arm does anything - and on 546 section 1 the second
    # of them names `cpu_idle`. `_a0`/`_a1` are what those same two words hold *after* Apple's PoC
    # clean-and-invalidate of the region: a **dirty** stale line has its copy written out by that
    # routine's clean half, so the pair comes back changed and, on 546 section 3, holding the
    # `cpu_data->rtcPop` deadline the exit wrapper publishes in the same pass as
    # `xnu_live_slot_rtcpre_pop`. Two equal pairs say the line was clean and the pop's wrong value came
    # from somewhere this arm has not touched.
    #
    # **And the pair means the opposite thing in the other arm, which is why `seam_op` is read and
    # branched on rather than the pair being read alone.** 572's arm is the same interception with no
    # operation behind it, so its `_a0`/`_a1` are the same two words across Apple's own L1 flush and
    # nothing else: there an *unequal* pair says the L1 flush writes the slot's line back - a reading
    # 535's run could not produce, because its own operation changed those words afterwards - and an
    # equal pair is the arm working as designed, not "the line was clean"
    # ([[mi4-silence-is-a-reading-only-if-success-is-silent]]). An image that published the pair and not
    # the arm would be read with the wrong one of these rules, so an absent `seam_op` is UNREAD here
    # rather than a default.
    #
    # The `sp` test is the one that says the arm read the *right object*: 546 section 1's slot is the
    # address the `pop` reads, so if the seam's `sp` and the abort's `sp` disagree the two are readings
    # of different words and nothing below them joins up.
    if [[ $seam_calls =~ ^0x[0-9a-f]+$ ]] && (( seam_calls >= 1 )); then
      local seam_lr seam_sp seam_sctlr seam_other seam_other_lr rtcpre_pop seam_op
      local seam_b0 seam_b1 seam_a0 seam_a1
      seam_op=$(keyval seam_op)
      seam_lr=$(keyval seam_lr)
      seam_sp=$(keyval seam_sp)
      seam_sctlr=$(keyval seam_sctlr)
      seam_other=$(keyval seam_other)
      seam_other_lr=$(keyval seam_other_lr)
      seam_b0=$(keyval seam_b0)
      seam_b1=$(keyval seam_b1)
      seam_a0=$(keyval seam_a0)
      seam_a1=$(keyval seam_a1)
      rtcpre_pop=$(keyval slot_rtcpre_pop)
      say ""
      say "  the seam - the exit's own bl FlushPoU_Dcache, hooked by the return address it was"
      say "  entered with. xnu_live_seam_calls=$seam_calls (the schedule is <=4 then powers of two, so"
      say "  a printed 4 means *at least* four idle passes reached it), lr=${seam_lr:-absent}"
      say "  sp=${seam_sp:-absent} (the frame's own SCTLR at the seam: ${seam_sctlr:-absent})"
      say "  the two words the exit's push wrote, as memory held them: ${seam_b0:-absent} ${seam_b1:-absent}"
      # Which arm made these readings is a value in the log and not an assumption here: the same four
      # keys are published by both, and the line below says which rule the pair is read with.
      if [[ $seam_op == "0x00000001" ]]; then
        say "  xnu_live_seam_op=$seam_op  ARM 535: the PoC clean-and-invalidate IS in the image and the"
        say "  same two after it:                                       ${seam_a0:-absent} ${seam_a1:-absent}"
      elif [[ $seam_op == "0x00000000" ]]; then
        say "  xnu_live_seam_op=$seam_op  ARM 572: the interception with NO operation behind it, so the"
        say "  same two across Apple's own L1 flush only:               ${seam_a0:-absent} ${seam_a1:-absent}"
      else
        say "  xnu_live_seam_op=${seam_op:-absent}  UNREAD - the arm is not published by this image, so the"
        say "  same two words again:                                    ${seam_a0:-absent} ${seam_a1:-absent}"
        say "        pair below has two meanings and this log does not say which rule applies (the key is"
        say "        written by the body's own STAGE90_XNU_SEAM_POC, and its absence is not a default)"
      fi
      if [[ $seam_lr == "$pop_lr" ]]; then
        say "  PASS  seam_lr=$seam_lr is the exit's own call's return address, so the hook was entered"
        say "        at the seam and not at one of that routine's three other callers"
      else
        say "  FAIL  seam_lr=${seam_lr:-absent} is not the exit's own call's return address"
        say "        (${pop_lr:-?}, $pop_lr_src): the arm published from a site that is not the seam, so"
        say "        none of the readings below is about the instruction 547 section 5 names"
        verdict_ok=0
      fi
      if [[ $seam_other =~ ^0x[0-9a-f]+$ ]]; then
        say "  PASS  xnu_live_seam_other=$seam_other call(s) were handed straight through (last lr"
        say "        ${seam_other_lr:-absent}) - the filter is measured and not asserted: 'the hook never"
        say "        ran' and 'it ran and rejected this site' are different readings (526)"
      else
        say "  UNREAD  xnu_live_seam_other is absent, so nothing here says whether the routine's other"
        say "          three call sites were reached - only that the seam was"
      fi
      if [[ -n $sleh_sp && -n $seam_sp ]]; then
        if [[ $seam_sp == "$sleh_sp" ]]; then
          say "  PASS  seam_sp=$seam_sp equals the abort's own sp: the address the arm read and restored"
          say "        is the address the pop reads, which is 546 section 1's slot"
        else
          say "  FAIL  seam_sp=$seam_sp is not the abort's sp=$sleh_sp - the arm's slot and the pop's"
          say "        are different addresses, so the two readings are of different words"
          verdict_ok=0
        fi
      else
        say "  UNREAD  ${seam_sp:-seam_sp} against ${sleh_sp:-sleh_sp}: one of the two addresses is"
        say "          absent, so whether the arm read the pop's own slot is not established here"
      fi
      if [[ $seam_b1 =~ ^0x80[0-9a-f]{6}$ ]] && (( seam_b1 < 0x80600000 )); then
        say "  PASS  b1=$seam_b1 is a kernel-text address, so memory held the frame the exit's push"
        say "        wrote - 546 section 1's premise for this cell, measured rather than assumed"
      else
        say "  READING  b1=${seam_b1:-absent} is not a kernel-text address: memory did *not* hold the"
        say "        frame the push wrote, so the window's store had not reached it - which contradicts"
        say "        546 section 1's premise and is a finding in its own right, not a failed arm"
      fi
      if [[ $seam_op == "0x00000000" ]]; then
        # 572's arm: nothing was done to the line, so the pair is a reading of Apple's own L1 flush.
        if [[ -n $seam_b0 && $seam_a0 == "$seam_b0" && $seam_a1 == "$seam_b1" ]]; then
          say "  AS DESIGNED  the pair is unchanged (b=${seam_b0}/${seam_b1} -> a=${seam_a0}/${seam_a1}) with"
          say "        no operation in the body: Apple's own FlushPoU_Dcache did not write the slot's line"
          say "        back, and the arm's only effect was the reading - which is what makes it a control"
          say "        cell for 535 rather than a second attempt at it (572 section 6)"
        else
          say "  THE L1 FLUSH WRITES IT BACK  the pair changed (b=${seam_b0:-?}/${seam_b1:-?} ->"
          say "        a=${seam_a0:-?}/${seam_a1:-?}) with **no operation in the body** - so what changed those"
          say "        two words in memory is Apple's own FlushPoU_Dcache (caches.c:460, the call this arm is"
          say "        entered on). That is a reading 535's run could not produce, because its own operation"
          say "        overwrote the same two words afterwards: it makes the L1 flush's clean half a candidate"
          say "        on its own, and the near-side pair of 535's arm uninterpretable without it"
        fi
      elif [[ $seam_op == "0x00000001" ]]; then
# 535's arm: the operation ran, so the pair is a reading of its clean half.
        if [[ -n $seam_b0 && $seam_a0 == "$seam_b0" && $seam_a1 == "$seam_b1" ]]; then
          say "  CLEAN LINE  the pair came back unchanged (b=${seam_b0}/${seam_b1}), so the line was not"
          say "        dirty: the clean had nothing to write out, and a pop that still died on a stale"
          say "        value got it from somewhere this operation does not reach - the falsifier for 546"
          say "        section 3's mechanism rather than its confirmation"
        elif [[ -n $seam_a1 && -n $rtcpre_pop && $seam_a1 == "$rtcpre_pop" ]]; then
          say "  STALE LINE, WRITTEN OUT  a1=$seam_a1 is this same pass's xnu_live_slot_rtcpre_pop: the"
          say "        dirty line's copy of the slot's lr word is the deadline that pass read, which is"
          say "        546 section 3's mechanism seen from the near side, before the pop that died on it"
        else
          say "  CHANGED  the pair came back changed (b=${seam_b0:-?}/${seam_b1:-?} ->"
          say "        a=${seam_a0:-?}/${seam_a1:-?}) and a1 is not this pass's rtcpre_pop=${rtcpre_pop:-absent}:"
          say "        a dirty line, holding something this block does not name"
        fi
      else
        say "  the pair is NOT interpreted: the arm that made these readings is not published, and"
        say "        the same two words mean opposite things in the two arms - 572's unequal pair is"
        say "        Apple's own L1 flush writing the line back and 535's is its clean's write-back,"
        say "        so reading either rule over this pair would be a claim this log does not carry"
      fi
    fi

    if [[ $pop_named -eq 1 ]]; then
      say "  => the arm's prediction arrived: the death is the exit's own pop, at the instruction"
      say "     547 section 4 names, with a log and (per the runner's exit code, which is not this"
      say "     function's reading) a return. What did *not* happen: the boot still does not survive"
      say "     the idle pass - and which arm was in the image is read off clause (5) rather than"
      say "     assumed: xnu_live_seam_op names it (535 with the clean-and-invalidate, 572 with no"
      say "     operation behind the interception), and the pair's meaning follows from that rather than"
      say "     from a default; clause (5) absent means the image is the earlier one and this project's"
      say "     seam work is still owed. This block is not the reading that decides the arm; the exit code is"
      say "     (551: exit 3 means the SoC returned into a state adb cannot reach)."
      # The pair from clause (2) is the only thing in the log that says which cell this is, and
      # 547 section 4's prediction is not for both cells - so the two are put side by side here
      # rather than left for the reader to join up.
      case $arm_seen in
        533)
          say "     And clause (2) reads the pair as 533's arm, which is the cell 547 section 4's"
          say "     prediction is for: the two agree."
          if [[ $seam_calls =~ ^0x[0-9a-f]+$ ]] && (( seam_calls >= 1 )); then
            say "     The image this log came from is the later one in that cell, though - 535's, whose"
            say "     cache settings are 533's - so clause (5)'s pair is the reading and the pop's"
            say "     survival is 535's question rather than this prediction's"
          fi ;;
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
      say "     the next step's question - not this one's."
      if [[ $arm_seen == 533 ]]; then
        say "     **Read the FALSIFIER line at clause (1) before this one**: for this pair, no death"
        say "     at the pop is 547 section 4's third row, the cell that forbids 535"
      fi
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

# --- 2b. park the log that is on disk, because it is the PREVIOUS run's ------------------
#
# **This is the structural half of the trap the peer session found in the gate (562), and it is here
# rather than in the reader because it can only be done before the boot.** The file named by `$LOGFILE`
# is written in step 5, which is reached only if the device returned; so on a non-return and on a failed
# capture the name still holds the *previous* run's log - and today that file carries, verbatim, the
# bracket 547 section 4 pre-registers for the arm in `out/` (`pre_calls` 1, `rtcpre_calls` 1,
# `post_calls` absent), because it is the death that prediction was read from. Read through that file, a
# run whose capture failed would confirm 547 section 4 with the data 547 section 4 was derived from, and
# the confirmation would be unattributable. The gate fingerprints the file before the run, which needs a
# human to make the comparison; vacating the name makes the comparison unnecessary, because after the
# boot the name is *this* run's or it is absent, and either way the previous bytes cannot be mistaken for
# this run's. It is a `mv`, not a delete: nothing is destroyed, the gate's own "unchanged sha256 means no
# capture" test becomes impossible-to-pass-by-accident, and the previous log - today the only copy of a
# measurement - stays on disk under a name that says what it is.
PREV_LOG=""
if [[ $DRY_RUN -eq 1 ]]; then
  say "would move $LOGFILE (the previous run's log) aside, so that the name holds this run's or nothing"
elif [[ -e $LOGFILE ]]; then
  PREV_LOG=$LOGFILE.prev
  _n=2
  while [[ -e $PREV_LOG ]]; do PREV_LOG=$LOGFILE.prev.$_n; _n=$(( _n + 1 )); done
  if mv "$LOGFILE" "$PREV_LOG" 2>/dev/null; then
    say "parked the previous run's log: $LOGFILE -> $PREV_LOG"
    say "  ($(wc -c < "$PREV_LOG" || echo '?') bytes, sha256 $(sha256sum "$PREV_LOG" | cut -d' ' -f1 || echo '?')"
    say "   - the same file the gate fingerprinted before the boot; it is NOT this run's, whatever the"
    say "   bracket in it says, and 547 section 4's prediction was written from that bracket)"
  else
    # Not fatal, and deliberately so: the run is not spent yet and the file is still usable evidence at
    # its old name. What must not happen is a silent fall-through, because the whole point of this step
    # is that the name is unambiguous afterwards - so it is said out loud and the operator is told the
    # one comparison that still applies.
    PREV_LOG=""
    say "WARNING: could not move $LOGFILE aside (a sticky /tmp and an identity that does not own it:"
    say "         511, 512). It will be REPLACED by this run's capture if the capture works, and it"
    say "         will look untouched if it does not - so in that case compare its sha256 against the"
    say "         one the gate printed above before reading any bracket out of it."
  fi
else
  say "no log at $LOGFILE yet - so after this run, the name existing at all is the first check"
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
    # **And the name to read it *into* is vacant, which the hand-retry command above does not say.**
    # Step 2b parked whatever was there before the boot, so the previous run's log is at `$PREV_LOG`
    # and not under `$LOGFILE` - and `> $LOGFILE` creates this run's file at a name that currently
    # holds nothing. 566 §3's producer attribution is what makes this the message that matters: at the
    # real `RETURN_TIMEOUT`, a phone that enumerates inside the wait leaves section 4 with `RETURNED`
    # true and this block is never reached, so the operator who *does* reach it is the one whose device
    # was silent past the whole wait - and for them the earlier log's location is the one thing the
    # section 5 message says and this one did not. Same condition, same path, said once.
    if [[ -n $PREV_LOG ]]; then
      say "The previous run's log is NOT at that name: step 2b parked it before the boot, so reading"
      say "into $LOGFILE above creates this run's file beside it. The earlier one is at"
      say "  $PREV_LOG"
      say "- read it for the 2026-09-22 death, and never as this run's, whatever bracket it carries."
    fi
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
  # what the redirect below leaves behind) and `sudo`'s is the fallback. It is a function now because
  # the attempt loop below repeats it, and **the removal is deferred to the moment a replacement
  # exists** - see the next note, which is the 564 change.
  rm_log() {
    rm -f "$LOGFILE" 2>/dev/null || sudo rm -f "$LOGFILE" 2>/dev/null || \
      die "could not remove $LOGFILE - neither the invoking user nor root can unlink it, and the redirect would then be reopening someone else's file"
  }
  # **The capture is staged through a temporary file, and the reason is the failure path.** The
  # obvious order - remove `$LOGFILE`, then redirect adb into it - makes a *failed* capture destroy the
  # previous log, and the previous log is not nothing: the file named by `$LOGFILE` right now is the
  # 2026-09-22 death that 547 section 4's prediction was read from, and the gate fingerprints its
  # sha256 immediately before the run precisely so that "the file changed" can serve as the post-hoc
  # proof that a capture happened. Deleting it on a failed attempt would spend the run, leave that test
  # unanswerable, and destroy the only copy of a measurement. So the read goes to `$LOGFILE.new` and
  # only a non-empty result is moved into place; the destination is removed at that point and not
  # before. A failed capture now leaves the previous file byte-identical, which is also what makes the
  # gate's before/after test say the true thing - *no capture happened* - rather than "the file is
  # gone".
  TMP=$LOGFILE.new
  # **And the wait is why this loop exists.** `adb exec-out` immediately after section 4 returns is
  # expected to fail: the return is read out of the host log within seconds of the enumeration, and
  # adbd is ~20 s behind it (see CAPTURE_WAIT above). One attempt here, failing into `die`, would
  # report the normal bring-up as a capture failure - and `die` is exit 1, whose meaning is "the gate
  # refused, or the device was not found", so the header's exit-3 state would have been reached
  # through a code the header does not name for it. That is one state with two definitions, in the
  # file that defines them.
  CAPTURED=0
  for _attempt in $(seq 1 $(( CAPTURE_WAIT / 5 + 1 ))); do
    if sudo adb -s "$SERIAL" exec-out 'cat /proc/last_kmsg' > "$TMP" 2>/dev/null && [[ -s $TMP ]]; then
      rm_log
      mv "$TMP" "$LOGFILE"
      CAPTURED=1
      break
    fi
    rm -f "$TMP" 2>/dev/null || true
    if (( _attempt == 1 )); then
      say "  adb cannot reach the device yet. That is expected here and not yet a verdict: the return"
      say "  is read from the host log when the phone enumerates, and adbd appears ~20 s later. Waiting"
      say "  up to ${CAPTURE_WAIT}s (a read costs nothing and cannot spend a run)."
    fi
    sleep 5
  done
  if (( CAPTURED == 0 )); then
    rm -f "$TMP" 2>/dev/null || true
    say ""
    say "REFUSING to call this a hang: the device returned to the host (section 4 read a new"
    say "enumeration of SerialNumber: $SERIAL after the boot), and what failed is the capture."
    say "The payload's log is in the top of DRAM and survives until the phone's next power"
    say "*cycle*, so it is still there - do NOT power-cycle first, and do not re-run: a second"
    say "boot overwrites it and spends another run. Retry this read by hand until Android is up:"
    say "  sudo adb -s $SERIAL exec-out 'cat /proc/last_kmsg' > $LOGFILE"
    say "and check the return against the host's own log by serial, not by port (this host's"
    say "port has a second occupant that appears on its own after hours of silence):"
    say "  sudo dmesg | grep $SERIAL"
    if [[ -n $PREV_LOG ]]; then
      say "$LOGFILE is absent and stays absent, because step 2b parked the previous run's log at"
      say "  $PREV_LOG"
      say "- so nothing at the name $LOGFILE can be this run's. Do not read that parked file as this"
      say "run's either: its bracket is the one 547 section 4 was derived from."
    else
      say "$LOGFILE is untouched, so it is still the run before this one - do not read it as this one's"
      say "(step 2b could not park it, so its sha256 must still equal the one the gate printed)."
    fi
    exit 3
  fi
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
  #
  # **And the re-read is staged through the same temporary file, for a sharper version of the same
  # reason**: it exists to *repair* a capture that looks wrong, so a failed re-read that had already
  # truncated `$LOGFILE` would destroy the very bytes it was sent to improve - a repair that can
  # destroy the thing it repairs. On a failed re-read the file that is on disk is kept, and the loop
  # stops rather than reading a third time into the same hole.
  for _attempt in 1 2 3; do
    _payload_lines=$(grep -a -c '^MI4IOS6_STAGE90' "$LOGFILE" || true)
    if (( _payload_lines >= 8 )); then
      break
    fi
    say "  WARNING: $LOGFILE holds $_payload_lines line(s) of the payload's own output, fewer"
    say "           than any run of this payload produces - re-reading /proc/last_kmsg"
    sleep 5
    if sudo adb -s "$SERIAL" exec-out 'cat /proc/last_kmsg' > "$TMP" 2>/dev/null && [[ -s $TMP ]]; then
      rm_log
      mv "$TMP" "$LOGFILE"
      say "  re-read $(wc -c < "$LOGFILE") bytes"
    else
      rm -f "$TMP" 2>/dev/null || true
      say "  the re-read did not come back. Keeping the $(wc -c < "$LOGFILE" || echo 0) bytes already"
      say "  in $LOGFILE instead of overwriting them with an empty read, and stopping here - the"
      say "  warning above still stands for the file as it is."
      break
    fi
  done
  # **The count is read out of the loop, not written into the message.** The loop can leave early - the
  # `break` above on a re-read that did not come back - so a message that says "after three reads" is
  # a number that was true of the loop's *bound* and false of the loop's path, which is the same shape
  # as 550's repair (the hardcoded number removed, the hardcoded *test* kept). `_attempt` is the
  # iteration the loop actually stopped on, so it is the number of reads that were attempted.
  _reads=${_attempt:-0}
  if (( $(grep -a -c '^MI4IOS6_STAGE90' "$LOGFILE" || true) < 8 )); then
    say "  WARNING: after $_reads read(s) $LOGFILE still holds fewer than 8 payload lines. Read"
    say "           this as 'the payload wrote almost nothing', NOT as 'the payload ran and"
    say "           produced this' - and repeat the run before drawing a conclusion from it."
  fi
fi

# The marker table is the part that decides what a run *meant*, so it is a function with
# its own entry point (--summarise) rather than inline: it can then be tested against
# synthetic logs, and re-run on a log captured elsewhere, without touching the device.
say "done. Full log: $LOGFILE"
