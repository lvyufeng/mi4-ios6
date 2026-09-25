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
#      write the log where it wanted to (an unwritable $LOGFILE in a sticky /tmp: 511, 512),
#      or the host could not READ its own USB log to compare (dmesg returned nothing, so the
#      run has no reading of the device at all: 2026-09-23), or `fastboot devices` never
#      settled to this phone alone (618 - **nothing was booted**, and the phone is still in
#      fastboot, so this refusal costs no press), or adb lists the device in a state adbd
#      cannot be commanded through (619 - `unauthorized`, `offline`, ... - again **nothing
#      was sent**, and the message names the state so the operator knows what to do). All of
#      these are host-side, host-list or device-state failures that say nothing about what
#      the payload would have done, which is why they share a code. **620 adds the one that
#      would otherwise have been read as 2**: `fastboot boot` itself exited non-zero and the
#      device did not come back - a boot call that did not report success cannot support exit
#      2's claim that the payload ran, the image may never have been sent, and the phone is
#      probably still in fastboot, so **no press is spent**. Read the section that produces it
#      before pressing power; it says what to check.
#   2  the payload ran and the device did NOT come back - a manual power press is needed
#      (the log will not survive a power cycle, so this is also a lost run). Also the code a
#      *fall* in the host log's enumeration count is reported as (the ring buffer rotating):
#      that is not evidence of absence, and it is not evidence of presence either, so it is
#      reported as the reading it cannot rule out.
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

# **The caller's directory, captured before the `cd` below, because paths that arrive as input belong to
# the caller and not to this script.** This file `cd`s to its own directory at line 58 (so that every
# relative path it uses internally is stable), and until 615 that `cd` also silently re-based the paths
# it was *given*: `--summarise out/stage90/captures/533-....txt` from the repository root resolved
# against `stages/stage90/` and refused with `no such log`, naming a path that exists. Measured before
# the fix, three ways - from the repo root, and from the capture's own directory with the bare filename -
# all three refused, and only an absolute path worked. The message blamed the file for a fact about this
# script's working directory, which is the exact shape of a die that names the artifact when the truth is
# about the harness (`mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one`). The one thing this
# reader produces is a reading OF a log, and the log is named by the caller.
INVOKE_PWD=$PWD
cd "$(dirname "$0")"
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
OUT=$REPO_ROOT/out/stage90
IMAGE=$OUT/stage90-qcdt.img

# Resolve an input path against the caller's directory. Absolute paths pass through untouched; a
# relative one is joined to `$INVOKE_PWD`, and the join is recorded so it can be *printed* rather than
# performed silently - a re-based path that is invisible is how the next reader is surprised.
#
# **The result comes back in `$RESOLVED` rather than on stdout, and that is not style.** The first
# version of this was `LOGFILE=$(resolve_path ...)` with the function `printf`ing the path, and it
# silently lost the `REBASED` record: a command substitution runs in a subshell, so the append inside
# the function was discarded and the note below printed nothing while the resolution itself worked. The
# symptom was a check that *looked* absent, on a path that had been taken - the same failure shape this
# project keeps finding in cells, in a shell function.
RESOLVED=""
REBASED=""
resolve_path() {   # resolve_path PATH - sets RESOLVED, appends to REBASED
  local p=$1
  RESOLVED=""
  [[ -z $p ]] && return 0
  if [[ $p == /* ]]; then RESOLVED=$p; return 0; fi
  REBASED="$REBASED $p"
  RESOLVED="$INVOKE_PWD/$p"
}

SERIAL=${SERIAL:-4a2fe00b}
resolve_path "${LOGFILE:-/tmp/cancro-last_kmsg.txt}"; LOGFILE=$RESOLVED
RETURN_TIMEOUT=${RETURN_TIMEOUT:-180}
# The capture wait is a **second window and not a repeat of the one above**, which is the whole point
# of it: section 4's criterion is satisfied the moment the host log shows the phone enumerating, and on
# this phone the fastboot-to-Android handover is 17 s (542: `18d1:d00d` gone at 02:00:59, `2717:0368` at
# 02:01:16) with Android's own bring-up a further ~20 s to the `18d1:4ee7` that carries adb (522:
# 14:13:49 -> 14:14:09). adbd therefore does not exist for the first several seconds after the return is
# seen, so a single immediate `adb exec-out` at step 5 fails on the *normal* path. A read is free and
# cannot cost a run, so this bounds how long the capture keeps trying, and nothing else uses it.
CAPTURE_WAIT=${CAPTURE_WAIT:-90}
# The wait for an unambiguous `fastboot devices` list, before the boot step refuses one - see the guard
# there. 60 s and not longer, because the state it waits out is usually seconds: the second phone-class
# device on this host (`usb 3-3`, serial `33e80afe…`) has 13 fastboot windows in the host log and
# **11 of them are under 14 s** - but two are not (1946 s on 09-17; and on 2026-09-23 it sat there for
# 47 min and counting). So a wait cannot replace the refusal, only stop the common, transient case from
# costing the operator a re-run.
FB_AMBIG_WAIT=${FB_AMBIG_WAIT:-60}
# **The status of the one call that spends the press, kept as a value instead of being allowed to
# end the script.** `fastboot boot` is section 3's last command and had no guard, so under `set -e`
# a non-zero status aborted the run with fastboot's own code *before section 4 ran at all* - and
# sections 4 and 5 are the only things that produce a reading. It is declared here, before section
# 3, so that the section-4 verdict can read it under `set -u` and so that the assignment at the
# call site is the only thing that ever changes it (`ENUM_ADVANCED`'s two-place rule, 617, read the
# other way round: initialize where the value is *born*, assign where it is *made*).
FB_BOOT_RC=0

DRY_RUN=0
SUMMARISE_ONLY=""
say() { printf '%s\n' "$*"; }
step() { printf '\n== %s ==\n' "$*"; }
die() { printf 'run_and_capture: %s\n' "$*" >&2; exit 1; }

# **The self-test arm's answer is a TIME, so the run that spends the press measures it.** 666's arm
# clears its one bit by how long the device takes to come back - ~28 s if the SoC's countdown fired,
# ~90 s if the bounded spin did instead (`stage90_main.c:1244-1254` states both) - and until 669
# nothing in this file computed that interval: an operator was expected to subtract two clock readings
# in their head, on the run whose log is the only copy of the measurement. `BOOT_SENT_AT` is taken
# immediately before the send and `RETURN_AFTER_S` is filled by `mark_return` at whichever of the
# three tests saw the return, so the two are one reading and not two.
#
# It is the **empty string** while no return has been seen, and not zero. Zero would be a duration
# ("came back instantly"), and "not measured" and "measured as zero" are different facts (the
# distinction this file makes everywhere else). `summarise_log` prints the duration only when it is
# non-empty, and says where it lives when it is not.
BOOT_SENT_AT=""
RETURN_AFTER_S=""
mark_return() {   # mark_return HOW - the return was seen by test HOW
  RETURNED=1
  RETURN_HOW=$1
  RETURN_AFTER_S=$(( $(date +%s) - BOOT_SENT_AT ))
}

# **Which arm the press is FOR, declared by the caller, with no default - and it is required.**
# 654 section 6 named this file's exposure: the gate call is not atomic against a build ("the gate reads
# five files in sequence, so a rebuild can put a *different* consistent pair in front of it"), and 660
# sharpened it - *a rebuild is not caught by anything*, because image, config and manifest move
# together, so every gate clause is satisfied by construction and **nothing compares `out/` against the
# arm the press is OWED for**. This variable is that comparison's left-hand side, and the right-hand
# side is read out of `revert-set.txt` below rather than out of the record's `role=` sentences: three of
# those claim to be "the arm the next press sends" (667), so they cannot answer the question.
#
# **There is no default, and that is the same decision the readiness tool took in 667.** A default is a
# name nobody typed, and a name nobody typed is how the wrong arm gets pressed. The flag is REQUIRED for
# a run that would actually send an image; `--dry-run` prints what it would check instead of refusing,
# so the flag can be exercised without the phone. A caller that does not know the arm it is for has the
# answer already: readiness row 4 names it, and this file names the candidate sets in its own refusal.
EXPECT_ARM=""
GATE_FLAGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    # Summarise a log that was already captured - useful when the run happened in another
    # shell, or when re-reading one after the fact. Takes the path as the next argument.
    #
    # **The value is required, and the check is not decoration.** `set -euo pipefail` is on, so a
    # `shift 2` with one argument left fails and ends the script with status 1 and **no output at
    # all** - measured 2026-09-25 on this flag and on the new one beside it (`--summarise` alone:
    # `EXIT=1 lines=0`). A refusal that prints nothing cannot be told from a crash, and an operator
    # reading it has no way to know which flag was wrong. The pre-existing one is fixed here rather
    # than reported: it is two lines in the same `case`, and leaving a known silent exit beside a new
    # flag is the shape this project ranks first to suspect.
    --summarise)
      [[ -n ${2:-} ]] || die "--summarise needs a log path after it (e.g. --summarise /tmp/cancro-last_kmsg.txt); an empty value is a refusal here and not an empty read"
      SUMMARISE_ONLY=$2; shift 2 ;;
    # Both spellings, and the separate-argument one is not a convenience: a launcher that builds its
    # command as a string cannot use `--expect-arm=NAME` without quoting games, and a launcher is
    # exactly the caller this flag exists for (the armed one on this host fires
    # `./run_and_capture.sh --allow-xnu-entry` and nothing else - see the finding in 667's section 5).
    --expect-arm)
      # An empty value is refused here rather than at the pin, because "the caller did not say" and
      # "the caller said nothing" are two different records (632) and only one of them is a caller who
      # does not know the answer.
      [[ -n ${2:-} ]] || die "--expect-arm needs a recorded set name after it (e.g. --expect-arm=armed-selftest-wdog-ef0361a2). Which sets exist is in $STAGE_DIR/revert-set.txt, and readiness row 4 names the one the bytes in out/ are"
      EXPECT_ARM=$2; shift 2 ;;
    --expect-arm=*)
      EXPECT_ARM=${1#--expect-arm=}
      [[ -n $EXPECT_ARM ]] || die "--expect-arm= was passed with nothing after the '='; that is an empty value and not an absent flag, and both are refused"
      shift ;;
    # **A typo here must not become a gate flag.** `*)` sends anything unrecognised to the gate, which
    # refuses an unknown argument at exit 2 - and a misspelled `--expect_arm=...` would then read as the
    # gate's own verdict about the tree. The prefix test below catches that class by name before the
    # fallthrough, at the cost of one pattern.
    --expect*) die "unknown argument $1 (did you mean --expect-arm=SET?)" ;;
    *) GATE_FLAGS+=("$1"); shift ;;
  esac
done

resolve_path "$SUMMARISE_ONLY"; SUMMARISE_ONLY=$RESOLVED


# **The resolution is printed, not silent.** A path argument that was re-based is a decision this script
# made on the caller's behalf, and a caller who wrote `out/stage90/captures/x.txt` and sees the reading
# come back has no way to know which directory it was joined to. One line per re-based path, and none
# when every path was already absolute - so the line's presence is itself information.
for _rb in $REBASED; do
  say "note: '$_rb' is a relative path, resolved against the directory this was invoked from"
  say "      ($INVOKE_PWD), not against $STAGE_DIR. Pass an absolute path to say it exactly."
done
unset _rb

# --- a backtick inside a printed string is a command, not punctuation ----------------------------
#
# 594's own defect, and it was found by *running* the clause it was written for rather than by
# reading it: a new line read `say " ... entry_note_poll runs only *after* __real_poll ... "` with
# those two names in backticks, so bash ran them - the printed sentence came out as "the park is the
# third poll and  runs only *after*  returns" and the two commands were reported missing on stderr.
# The sentence still read as if it said what it meant, which is exactly the shape this project keeps
# paying for: a claim that is not what the artifact does
# ([[mi4-a-claim-in-a-comment-is-not-a-check]]). It is checked here rather than left to review
# because the check is total and free - the source *is* the artifact, and every string this script
# prints is a `say`/`step`/`die` argument written in it.
#
# Deliberately not "no backticks anywhere": the notes above are full of them, and a rule that
# refused those would be switched off the first time it was inconvenient.
_self=$STAGE_DIR/run_and_capture.sh
if [[ -r $_self ]]; then
  # `\`` is a *literal* backtick inside a double-quoted string and this file already uses it (the
  # two reading lines that quote `adb devices`), so the escaped form is stripped before the search -
  # otherwise the guard refuses the script it is guarding, which is what its first draft did.
  _bad=$(grep -n -E '^[[:space:]]*(say|step|die)[[:space:]]+"' "$_self" \
         | sed 's/\\`//g' | grep -E '`' || true)
  if [[ -n $_bad ]]; then
    printf 'run_and_capture: a printed string contains a backtick, so bash will RUN it:\n%s\n' \
           "$_bad" >&2
    exit 1
  fi
fi
unset _self _bad

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

# --- the address a correct frame holds in its second word, derived the same way --------------------
#
# Clause (5)'s `b0`/`b1` are the two words the exit's `push {fp, lr}` wrote, read back out of memory at
# the seam. `b1` is therefore a **return address**, and there is exactly one value it can hold on a
# correct frame: the address `platform_cache_idle_exit` returns to. That value is *not* in `cpu_idle`,
# which is what 546 section 1 says it is, and the difference is the `--wrap` added in 517: the caller is
# `__wrap_platform_cache_idle_exit`, whose `bl <platform_cache_idle_exit>` returns into the wrapper.
# Measured on the frozen 574 arm: that `bl` is at `8047c98c` and returns to **`0x8047c990`**, an address
# inside the wrapper's own extent `0x8047c964..0x8047c9c4` - and `0x8047c990` appears nowhere in this
# repository's prose, because until now nothing compared against it.
#
# **Which of the two words that is.** `b0` is `[seam_sp]`, where the push wrote `fp`; `b1` is
# `[seam_sp+4]`, where it wrote `lr` - and `seam_sp` is that address *not* by assumption but by the
# check below (`seam_sp + 8 == sleh_sp`, i.e. the abort's own `sp` after the `pop` freed exactly the two
# words the push wrote). The base is not guessed either: `entry_seam_flush` is reached through
# `__wrap_FlushPoU_Dcache`, whose first statement is `mov r0, sp` (`0x8047cb90`), and a `bl` does not
# move `sp`, so that base *is* the exit's `sp` as its `push {fp, lr}` left it. `pop {fp, pc}` at
# `0x8004633c` loads `fp <- [seam_sp]` and `pc <- [seam_sp+4]`, so **`b1` is the word the pop takes as
# pc**, and `b0` is not. The reader's own `STALE LINE, WRITTEN OUT` arm already reads the pair this way
# - it compares `a1` against `xnu_live_slot_rtcpre_pop` - which is the second, independent statement of
# the same fact. The other pairing (that the pop's `pc` comes from `b0`) is the one arrived at by
# counting the two *words* instead of the two *registers*, and it sends a failure to the wrong word:
# `pop {fp, pc}`'s register list is ascending, and in a `pop` the lowest register is loaded from the
# lowest address.
#
# **And all four of those words are read with `SCTLR.C = 0`, while the pop runs with it back at 1.** The
# `b` pair is read before the arm's `bl FlushPoU_Dcache` and the `a` pair after it, and Apple's flush
# does not re-enable caching: `platform_cache_idle_exit` does that itself, at `0x8004631c`-`0x80046328`
# (`mrc`/`orr #4`/`mcr` on `SCTLR` plus an `isb`), and only then reaches the `pop`. So `b1` equal to this
# address says *DRAM* held the frame's word at a moment when the caches were off. It does not say the
# pop will see it, because that lookup happens with `C` on and a stale L1/L2 line answers it instead. A
# **wrong** `b1` is therefore a finding; a **right** `b1` is not a clearance of the pop - which is why
# the PASS below says what it establishes and stops there.
#
# **Why this is derived rather than pinned, and why the loose test it replaces was not enough.** The
# reader used to ask only whether `b1` looked like kernel text (`^0x80…` and `< 0x80600000`), and 520's
# own log shows what that admits: a stale stack value such as `0x80553520` passes it - as would any other
# address the idle thread's stack happens to hold, which is precisely the wrong-value case the arm exists
# to detect. Comparing against the one address the frame *should* carry turns "is it plausible" into "is
# it this image's frame", and the wrapper's return site is read out of the ELF at run time, so a rebuild
# that moves it moves the comparison with it ([[mi4-a-claim-in-comment-is-not-a-check]]).
#
# On any failure the callers get the empty string and the reader says UNREAD rather than falling back to
# a literal: unlike the `lr` criterion above, there is no pinned copy of this address anywhere, and
# inventing one would be the same defect the derivation exists to avoid.
exit_caller_lr_addr() {
  local elf=${1:-$OUT/xnu_arm_entry.elf} od=${OBJDUMP:-arm-none-eabi-objdump}
  local start size body ret
  [[ -r $elf ]] || return 1
  command -v "$od" >/dev/null 2>&1 || return 1
  read -r start size < <("$od" -t "$elf" 2>/dev/null \
    | awk '$NF == "__wrap_platform_cache_idle_exit" { print "0x" $1, "0x" $5; exit }') || return 1
  [[ $start =~ ^0x[0-9a-fA-F]+$ && $size =~ ^0x[0-9a-fA-F]+$ ]] || return 1
  body=$("$od" -d --start-address="$start" --stop-address=$(( start + size )) "$elf" 2>/dev/null) \
    || return 1
  # The callee is matched by its **tail** for the same reason `exit_pop_lr_addr` does it: the name may
  # be spelled with a `__real_` prefix, and a pattern written as a literal found nothing the once it
  # mattered. The frame's second word is the *real* routine's return address, so the callee is the one
  # that is not the wrapper's own name.
  ret=$(printf '%s\n' "$body" | awk '
    /<[^<>]*platform_cache_idle_exit>/ && $0 !~ /<__wrap_/ { seen = 1; next }
    seen && $1 ~ /^[0-9a-f]+:?$/ { a = $1; sub(/:$/, "", a); print "0x" a; exit }') || return 1
  [[ $ret =~ ^0x[0-9a-fA-F]+$ ]] || return 1
  (( ret % 4 == 0 )) || return 1
  (( ret >= 0x80000000 && ret <= 0xFFFEFFFF )) || return 1
  (( ret != start )) || return 1
  # And the answer identifies itself to a reader who checks it: it must be inside the wrapper's own
  # extent, which is the whole point of the correction above. A caller whose frame held something else
  # is what the comparison is for; a derivation that returned an address *outside* the wrapper would be
  # a derivation that had found the wrong `bl`.
  (( ret > start && ret < start + size )) || return 1
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
#
# **And the function has to be able to say "I could not read it", or every call site's UNREAD
# branch is decoration.** The first form was
#
#   n=$(sudo dmesg 2>/dev/null | grep -c "SerialNumber: $SERIAL" 2>/dev/null) || true
#
# and it cannot: `grep -c` prints `0` for empty input whether the input is empty because the log
# has no such line or because `dmesg` never produced any output at all. Measured, with a stub whose
# `dmesg` fails: the count came back `0`, and the call site's `${...:-UNREAD}` was never reached -
# so a *blind* reader reported the strongest negative reading this phase has ("the device did not
# come back", exit 2, press power) with no reading behind it. That is the same defect the file's
# exit-code contract exists to prevent, one level in: a reader silent because it broke reads
# exactly like a reader that read a zero. So the *read* is tested, not the count: a `dmesg` that
# fails, or that prints nothing at all, returns no value and every call site sees UNREAD.
#
# `return 0` on that path is deliberate and is not a swallowed error: the value is the reading, and
# an empty reading is a reading, while a non-zero status under this file's `set -e` would abort the
# run at the *assignment* (line 933/944/954) with exit 1 - whose documented meaning is "the gate
# refused, or the device was not found" - turning "the host log is unreadable" into a wrong claim
# about the device.
serial_enum_count() {
  local out n
  out=$(sudo dmesg 2>/dev/null) || return 0
  [[ -n $out ]] || return 0
  n=$(printf '%s\n' "$out" | grep -c "SerialNumber: $SERIAL") || true
  if [[ $n =~ ^[0-9]+$ ]]; then printf '%s' "$n"; fi
  return 0
}

# --- the PORT reading, and why the criterion needed a second one ------------------------------
#
# The comment above the serial counter says the port alone is unsafe because `usb 3-10` "also carries
# a serial-less `05c6:f006` occupant". **616 measured what that occupant is, and it is this phone.**
# On 3-10, five seconds apart and numbered consecutively by the host:
#
#   2153329 usb 3-10: USB disconnect, device number 74
#   2153344 usb 3-10: new high-speed USB device number 75 ... 2717:0368, Product: MI 4LTE,
#                                                              SerialNumber: 4a2fe00b
#   2153363 usb 3-10: USB disconnect, device number 75
#   2153368 usb 3-10: new high-speed USB device number 76 ... 05c6:f006, Mfr=0 Product=0
#                                                              SerialNumber=0
#
# Device numbers 74 -> 75 -> 76 in sequence on one port: the phone cycling between Android and a
# **Qualcomm mode whose USB descriptors are all empty**. Counted over the whole log: 1536
# `New USB device found` on 3-10, of which **1530** carry the serial and **6** are that mode. So the
# hardening the serial test exists for made the mirror-image mistake - it is safe against reading a
# stranger as the phone, and it **cannot see a real return by the phone in a mode that has no
# serial**, which is precisely the state a failsafe boot is most likely to produce and the state
# where the operator's next action (power press, or 610's EDL route) is different from "wait".
#
# The port is DERIVED from the phone's own enumeration lines rather than written down, so it is a
# fact about where this phone has been seen and not a constant that drifts when someone re-cables.
# The count alone cannot establish identity and is not asked to: the rollup below prints the
# vendor:product of every device seen on that port, so the *reading* stays with the operator and the
# claim this file makes is only "something enumerated on the port $SERIAL is on, and here is what".
phone_port() {
  local out p
  out=$(sudo dmesg 2>/dev/null) || return 0
  [[ -n $out ]] || return 0
  # `|| true` on the assignment is not decoration: this file runs `set -o pipefail`, so a grep that
  # matches nothing makes the whole pipeline non-zero, and an assignment whose command substitution
  # failed aborts the script under `set -e` - which would turn "this port is not in the log" into a
  # run that dies before it boots anything. `serial_enum_count` carries the same guard for the same
  # reason, and its comment names the symptom.
  p=$(printf '%s\n' "$out" | grep -oE "usb [0-9]+-[0-9]+: SerialNumber: $SERIAL" \
        | tail -1 | sed 's/^usb \([0-9]*-[0-9]*\):.*/\1/') || true
  [[ -n $p ]] && printf '%s' "$p"
  return 0
}

# The count of enumerations on the phone's port, **any id**. Deliberately not folded into
# `serial_enum_count`: they answer different questions - "the phone identified itself" versus
# "something enumerated where the phone is" - and the second is the one that survives a mode with
# empty USB descriptors. Both return UNREAD (no value) rather than 0 when they could not read.
port_enum_count() {
  local out n
  out=$(sudo dmesg 2>/dev/null) || return 0
  [[ -n $out ]] || return 0
  [[ -n ${PHONE_PORT:-} ]] || return 0
  n=$(printf '%s\n' "$out" | grep -c "usb $PHONE_PORT: New USB device found") || true
  if [[ $n =~ ^[0-9]+$ ]]; then printf '%s' "$n"; fi
  return 0
}

port_enum_rollup() {
  local out
  out=$(sudo dmesg 2>/dev/null) || return 0
  [[ -n $out ]] || return 0
  [[ -n ${PHONE_PORT:-} ]] || return 0
  printf '%s\n' "$out" | grep "usb $PHONE_PORT: New USB device found" \
    | grep -oE 'idVendor=[0-9a-f]+, idProduct=[0-9a-f]+' \
    | sed 's/idVendor=//; s/, idProduct=/:/' | sort | uniq -c \
    | awk '{printf "%s=%s ", $2, $1}' | sed 's/ $//' || true
  return 0
}

# --- the fastboot list, read ONCE and judged as ONE value --------------------------------------
#
# **One command, one list, one reading.** The test takes the list as an argument and counts it itself,
# rather than reading an `FB_COUNT` and an `FB_LIST` that two earlier lines happened to produce. That is
# not style: 618 made the check entered twice, once before a wait and once after it, and a version that
# re-tested a count from one moment against a list from another would be this project's most-repeated
# defect - one value with two definitions - sitting inside the check that decides whether the boot acts.
#
# The condition is "exactly one device AND it is `$SERIAL`", which is **stronger** than a bare count
# test. Before the wait the two are equivalent, because the presence check immediately above has already
# established that `$SERIAL` is in the list. They stop being equivalent once a wait is between them: the
# list can lose this phone and keep the stranger, and a bare count test would then read `1` and pass.
# What the stronger form buys is a **refusal instead of a raw `fastboot` error**. The reasoning is that
# with `-s "$SERIAL"` and no such device on the bus, `fastboot` fails with its own message, for which this
# file's exit contract has no clause - **and that step is an assumption, not a measurement.** What IS
# measured is what `-s` means: `fastboot --help` says `-s SERIAL  Specify a USB device`, so the call names
# its target rather than relying on there being exactly one. Whether an *absent* `-s` target makes
# `fastboot` refuse or fall back to a device that is present is not measured here, and deliberately not:
# measuring it means sending a fastboot command on a bus where someone else's phone is sitting in
# fastboot, and the fall-back branch of that test is a touch of a device this project has no business
# talking to. So the stronger condition is justified on the part that is measured - that it cannot pass a
# list holding the stranger alone - and this paragraph records the part that is not. If a future run
# shows `fastboot` falling back to a present device under an absent `-s`, the condition is still correct
# and the *reason* here is wrong, which is the failure mode this comment exists to make findable.
fastboot_pinned_only() {
  local list=$1 n
  n=$(printf '%s\n' "$list" | grep -c . || true)
  [[ ${n:-0} -eq 1 && $list == "$SERIAL"$'\t'* ]]
}
# The count, for prose only. Derived from the same list every time, so the number in a message and the
# verdict that produced it cannot disagree.
fastboot_list_count() {
  local n
  n=$(printf '%s\n' "$1" | grep -c . || true)
  printf '%s' "${n:-0}"
}

# --- what adb says the device's STATE is - a different question from whether adb lists it ---------
#
# `adb devices` prints `SERIAL<TAB>STATE`, and the state is not decoration: `adb -s X reboot bootloader`
# only works from a state where adbd is up and willing. Until 619 the caller tested the serial alone
# (`grep -q "^$SERIAL"`), which matches field 1 and therefore **every** state column - measured over all
# of them on this host: `device`, `unauthorized`, `offline`, `no permissions`, `sideload`, `recovery`
# and `bootloader` **all match**. So the branch that needs a usable device was entered on a listing that
# only proved presence.
#
# The consequence is not theoretical. From `unauthorized` (the host's key has not been accepted on the
# device - after a wipe, or a `rm /data/misc/adb/adb_keys`) the reboot fails, the 30-poll wait for
# fastboot expires, and the run ends at `die "device did not appear in fastboot"` - **exit 1, having sent
# nothing, with a press spent**. From `offline` it is the same failure in transient form, and `offline`
# is a race rather than a state: adb can report it for minutes while Android is up and adbd is wedged.
#
# So the reader returns the state and the caller decides. It returns **nothing** when adb cannot be read
# at all or does not list the serial - the same UNREAD convention the host-log readers use, and for the
# same reason the file's own `serial_enum_count` note gives: a failed read and a read of "not there" must
# not be one value.
#
# **`recovery` counts as usable and `sideload` does not**, which is a choice rather than a fact about
# adb: `reboot bootloader` is a command any adbd that is rooted and running will accept, and TWRP's adbd
# answers it, while sideload mode's adbd serves exactly one command and is not one this file sends.
adb_state() {
  local out
  out=$(sudo adb devices 2>/dev/null) || return 0
  [[ -n $out ]] || return 0
  printf '%s\n' "$out" | awk -v s="$SERIAL" '$1 == s { print $2; exit }'
  return 0
}

summarise_log() {
  local log=$1
  # **The self-test arm's own keys were missing from this table until 669, and they are the ones that
  # answer its question.** The arm in `out/` (the hardware-watchdog self-test) publishes FIVE
  # `hw_watchdog_*` keys before it spins (`hw_watchdog.c:161-165`) and two SELFTEST sentences of its own
  # (`stage90_main.c:1244-1254`), and this list carried two of the five and neither sentence - so the
  # run whose whole purpose is to test the recovery net printed a marker table that could not show the
  # net's own evidence. The table is not decoration: it is what the operator reads first, and a key that
  # is not in it is a key nobody counts.
  local markers=(hw_watchdog_enabled hw_watchdog_readback_ok hw_watchdog_counter_running
                 hw_watchdog_countdown_plausible hw_watchdog_checksum
                 "hw_watchdog SELFTEST: spinning"
                 "hw_watchdog SELFTEST: deadline reached"
                 "deadman: armed"
                 "apple_dt selftest ok" "exception"
                 "pc-sampling watchdog: rebooting after sample dump"
                 "platform_reboot entered")

  # --- the log may not be there at all, and that is a state rather than a value ----------------
  #
  # **Measured on the live path (601), in the state the device is actually in.** `grep -c` on a
  # missing file writes `grep: <path>: No such file or directory` to **stderr** and *nothing* to
  # stdout, and the `|| true` that swallows its exit status does not swallow that message: this
  # function used to emit **eleven** such lines, one per grep below, and then print
  # `MI4IOS6_STAGE90 lines: ` with no number next to it - because `n` was the empty string and
  # `[[ "" -eq 0 ]]` is **true**, so both `-eq 0` tests below were satisfied by a value that was
  # never produced rather than by a count of zero. The consequences were not only cosmetic: the
  # marker table printed seven blanks, and the `reading:` section printed
  # `hardware watchdog: NOT confirmed armed` - a claim about the watchdog inferred from an absent
  # file, which is the misattribution the `-eq 0` branch twelve lines down says in as many words
  # that it must not make. `$LOGFILE` is absent right now, so this fires at step 1 of the next run,
  # before anything has been booted, and it is the first thing the operator reads on the run that
  # costs the press.
  #
  # The two states are named separately because they are different facts (588's rule): a path with
  # no file behind it is not an empty log, and neither is a log that exists and cannot be read
  # (511/512's ownership state). Both return **0**: this function's last statement was once
  # `[[ ... ]] && say ...`, whose status-1-on-false made the *caller* exit under `set -e` before it
  # booted anything (see the note on the `abort` test at the end of this function).
  if [[ ! -e $log ]]; then
    say "MI4IOS6_STAGE90 lines: NONE (no file at $log)"
    say "NONE. There is no log at that path at all, so no line of it was counted and every count"
    say "below is absent rather than zero: either nothing has written one yet (this run has not"
    say "booted anything), or the path is wrong. An empty log is a file that exists - this is not"
    say "that state, and it is not a reading of any log."
    return 0
  fi
  if [[ ! -r $log ]]; then
    say "MI4IOS6_STAGE90 lines: UNREAD (a file is there at $log and is not readable)"
    say "UNREAD. The file exists and cannot be read, so nothing in it was counted - this is a"
    say "permissions or ownership state (511/512) and not a reading. Read it as root:"
    say "  sudo head -20 $log"
    return 0
  fi

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

  # --- the self-test arm's own question, and the one number that answers it ------------------------
  #
  # A self-test arm does not ask "did the boot get far": it asks **whether the SoC's own countdown
  # reset the device**, and the answer is a TIME. The arm states its own two times in the log
  # (`stage90_main.c:1244-1254` prints them with `log_hex32`, so they arrive as `~0x0000001c` and
  # `~0x0000005a` rather than as 28 and 90), and the run that booted it measures the third: how long
  # the device took to come back. This clause joins them, and it is here rather than in the run because
  # the two arm-side numbers live in the LOG and the measurement lives in the RUN - neither site has
  # both.
  #
  # **The selector is the arm's own sentence and not a switch**, deliberately: `--summarise` is used on
  # logs captured in other sessions, where no configuration file is being read, and a clause gated on
  # the build record would be silent for exactly the logs a later reader brings back. A log that
  # carries "SELFTEST: spinning" is a log from a self-test arm, whoever booted it.
  #
  # The gap that this clause exists to close, measured 2026-09-25: the arm was parked and owed a press
  # and *nothing in the tree turned its answer into a number*. The marker table had two of its five
  # keys, the deadline sentence was not counted, and the runner never computed the return time at all -
  # so the reading was three hex strings in a 60-line tail and a subtraction the operator was expected
  # to do by hand, at the moment the run's log is the only copy of the measurement.
  if grep -aq 'hw_watchdog SELFTEST: spinning' "$log" 2>/dev/null; then
    local _line _nums _bite _deadline _n
    _line=$(grep -am1 'hw_watchdog SELFTEST: spinning' "$log" 2>/dev/null || true)
    # Both spellings of `log_hex32` are accepted - `ram_console.c:37` prints `0x%08x` and
    # `xnu_log.c:29`'s raw variant prints the eight digits alone - because which one wrote this line is
    # a property of where the arm was when it logged, not of the arm, and a reader that required one
    # spelling would report the arm's own numbers as absent (m672's shape: an absence claim is a claim
    # about how the value is encoded).
    _nums=$(printf '%s' "$_line" | grep -oE '~(0x)?[0-9a-f]{1,8}' | sed 's/^~//; s/^0x//' || true)
    say ""
    say "  HARDWARE-WATCHDOG SELFTEST: this log carries the self-test block, so the arm that ran was"
    say "    the self-test arm and its question is not whether the boot progressed - it is whether"
    say "    the SoC's own countdown reset the device. That answer is a TIME, and the numbers that"
    say "    settle it are the arm's own two - which the log states - and the one the run measured."
    _n=0
    while read -r _h; do
      [[ -n $_h ]] || continue
      _n=$(( _n + 1 ))
      case $_n in
        1) _bite=$(( 16#$_h )); say "      the arm's own statement: the hardware countdown should reboot us at ~${_bite}s" ;;
        2) _deadline=$(( 16#$_h )); say "      the arm's own statement: if it does not, the bounded spin reboots us at ~${_deadline}s" ;;
      esac
    done <<< "$_nums"
    if (( _n < 2 )); then
      say "      (only ${_n} of the arm's two numbers could be read out of that line, so the two"
      say "       candidate readings cannot be told apart from this log alone. The line that states"
      say "       them is printed whole in the marker table above; read the numbers off it by hand.)"
    fi
    if [[ -n $RETURN_AFTER_S ]]; then
      say "      this run measured: the device came back ${RETURN_AFTER_S}s after \`fastboot boot\` was called"
      say "      **and that number includes the send and the payload's own start-up, so it is a"
      say "      little larger than the arm's time** - 633 measured 1.1805 s of that prelude for the"
      say "      park arm, and the two candidate readings are still far apart: a countdown that fired"
      say "      lands near the first number, a countdown that did NOT fire lands near the second."
      say "      Read the return time against BOTH, and read the exit code beside it (0 or 3 came"
      say "      back, 2 did not): the deadline sentence's ABSENCE is not the countdown's verdict,"
      say "      because it is also absent when the log was read before the deadline elapsed."
    elif (( _n >= 2 )); then
      say "      this run measured: **NOT IN THIS LOG** - the return time is not a property of a"
      say "      captured log, so a \`--summarise\` of one cannot supply it. It is printed by the run"
      say "      that booted it, in section 4, as 'the device came back Ns after this run called"
      say "      fastboot boot'. Read that line beside the two numbers above; if the log arrived by"
      say "      hand and no run printed it, the difference between ${_bite}s and ${_deadline}s is"
      say "      3.2x, so the two readings are distinguishable from a stopwatch too."
    fi
    # --- the device's own statement of how long it lived, and it is the one number a third outcome
    # --- cannot hide from (677) -------------------------------------------------------------------
    #
    # **Why this second number exists at all.** The two candidate readings above are the arm's own
    # predictions, and 677's run matched *neither*: the device came back inside a session shorter than
    # the countdown, which is a third outcome the pre-registration did not contain, and this block
    # could not say so from the log - because the only elapsed-time key the payload had was printed on
    # the deadline path, i.e. only on the path that means the net did *not* fire. The host clocks had
    # to reconstruct it afterwards, and that reconstruction is what the periodic tick replaces: the
    # payload now writes `selftest_tick_us` once a second from inside the spin, into the ram_console,
    # which a PS_HOLD warm reset preserves. So the **last** such line in the log is the device's own
    # answer to "how long were you alive", stated at the moment before whatever ended it.
    #
    # Read it as a lower bound and say so: it is the last tick *written*, and the run ends somewhere
    # in the following second. A log with no tick lines is an arm built before 677 and is reported as
    # absent rather than as zero - the same distinction the `_elapsed` line above makes, and for the
    # same reason (an absent key is not a value).
    local _ticks _nticks _lasttick
    _ticks=$(grep -a -o 'selftest_tick_us=0x[0-9a-f]\{1,8\}' "$log" 2>/dev/null | sed 's/.*=0x//' || true)
    if [[ -n $_ticks ]]; then
      _nticks=$(printf '%s\n' "$_ticks" | grep -c . || true)
      _lasttick=$(printf '%s\n' "$_ticks" | tail -n 1)
      say "      AND THE DEVICE'S OWN LAST TICK: selftest_tick_us = $(( 16#$_lasttick )) us, out of"
      say "      ${_nticks} tick(s) in this log - which the payload wrote from inside the spin, once a"
      say "      second, into the ram_console that the warm reset preserves. **It is a LOWER BOUND on"
      say "      the payload's life** (the run ends within the following second), and it is the one"
      say "      number that cannot be produced by a prediction: read it against the arm's two numbers"
      say "      above. A last tick well BELOW the first number is 677's third outcome - reset, by"
      say "      something, earlier than the net's own interval - and it is the case this line was"
      say "      added for."
    else
      say "      (no \`selftest_tick_us\` line in this log: the periodic tick is 677's instrument and"
      say "       an arm built before it does not carry it, so its absence says which image ran and"
      say "       NOT that the payload lived under a second. 677's own capture is such a log, and the"
      say "       session length had to be reconstructed from the host's clocks.)"
    fi
  fi

  # --- the idle window's near end, and it prints for every log that reached `cpu_idle` -------------
  #
  # The gate is self-selecting, and **598 moved the selector from the instrument to the signature**.
  # It was `xnu_live_slot_cwe_*`'s presence - `entry_window_note`'s key, published by an image built
  # since 522 - which is a fact about the *image* and not about the arm: 520's image predates the
  # instrument and carries the same arm, so half of this project's baseline satisfied every criterion
  # this block has and could not enter it. The selector is now `xnu_live_door_seq`, published by
  # `__wrap_Idle_load_context` on the earliest passes of every boot that reaches `cpu_idle`: the
  # block appears for any log that got to the idle, which is exactly the set of logs its five clauses
  # are written for, and clause (2) reads the *cell* from whichever of the two publishers that log's
  # image has. That is still not "every log" - a verdict block whose criteria nobody re-derives is how
  # a stale check outlives its step, and this one is now gated on the boot having reached the
  # question rather than on the image having the newest instrument for it.
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
  #
  # **`keyval` is defined here, above both branches, because 594's clause needs it too** - and a
  # second copy of one reader is the defect this repository keeps meeting: the two would drift the
  # first time either moved, and nothing would compare them. `|| true` is load-bearing and its
  # absence was this block's first defect, found by running it against the state it is meant to
  # refuse: the script sets `pipefail`, so a key that is absent makes `grep` exit 1, the pipeline
  # returns 1, and `set -e` kills the *caller* mid-function - the same shape as the
  # `[[ ... ]] && say ...` defect documented four lines above. An absent key has to reach the code
  # that can say "this is UNREAD", not end the summary.
  keyval() { grep -ao "xnu_live_$1=[0-9a-fx]*" "$log" 2>/dev/null | tail -1 | sed 's/^[^=]*=//' || true; }

  # **The largest of a key's occurrences, as a decimal - and why it is not `keyval`.** The keys this
  # is for (`door_seq`, `poll_seq`, `poll_timeout_ms`) are published once per event and *in
  # increasing order*, so the last occurrence and the largest agree - until the events outnumber the
  # publishes a reader wants. `xnu_live_poll_timeout_ms` is the case that decided it: the first four
  # polls each publish their own timeout, the *park* is the third, and a fourth short ask after the
  # park would leave `keyval` (which takes the last) reporting the fourth's short timeout as if it
  # were the park's. Taking the largest asks the question actually being asked - "did any poll with
  # a park-sized timeout return" - and never depends on which call published last.
  maxhex() {
    local m=0 v d
    while read -r v; do
      [[ -n $v ]] || continue
      d=$(( 16#${v#0x} ))
      if (( d > m )); then m=$d; fi
    done < <(grep -ao "xnu_live_$1=0x[0-9a-f]*" "$log" 2>/dev/null | sed 's/^[^=]*=//' || true)
    printf '%s' "$m"
  }

  # **A key's occurrences in the order the log published them, one per line - and why the two readers
  # above cannot answer this.** `keyval` takes the last and `maxhex` the largest; both collapse a
  # key's history to a single value, which is right for a counter and wrong for a *pair of calls*.
  # The fixture's own driver reading is exactly such a pair: `entry_ramdisk.s` opens `/dev/rmd0` and
  # then `/dev/nosuch` as its control, and 504 states the criterion in its own words - "a pair of
  # opens whose second is also answered positively would mean the first told us nothing; a pair in
  # which both fail would mean the path shape is wrong rather than the driver missing". A reader that
  # reports one `xnu_live_open_error` reports the **control's** - it is published second, so `keyval`
  # takes it - while claiming to report the driver's. That is 558's defect ("the number was right and
  # the question was wrong") in a third place, and the repair is the same: ask the question the pair
  # asks. `|| true` is load-bearing here for the reason `keyval`'s note gives - the script runs under
  # `pipefail`, so an absent key makes `grep` exit 1 and `set -e` would kill the caller mid-function.
  #
  # **That the values come back in *insertion* order is measured, not assumed** - it is the one
  # property this reader rests on. 533's capture runs its records as `poll_seq=1`, `door_seq=0x8000`,
  # `poll_seq=2`, `read`, `open` (the control), `exit`, `wait`, `wait_done`, then the park's
  # `repair_seq=1`, `sip_seq=1`, `pce_seq=1`, `wfi_seq=1` and finally the fatal `sleh` - which is the
  # fixture's own program order (`+96`, `+116`, `+140`, `+156`, `+176`, `+188`, `+212`, `+232`,
  # `+276`, `+300`) followed by 593's causal chain, in one uninterrupted run at the end of the file.
  ordered() { grep -ao "xnu_live_$1=0x[0-9a-f]*" "$log" 2>/dev/null | sed 's/^[^=]*=//' || true; }

  # --- the live channel's own capacity, and whether this log filled it (609) ----------------------
  #
  # **Every `xnu_live_*` key below travels in a channel that is finite, that says what its capacity
  # is, and that says once when it starts dropping.** `entry_live_write` (`entry_stubs.c`, the
  # `ENTRY_LIVE_CAP` block) publishes `xnu_live_cap` at init, and once `g_live_records` reaches the
  # cap it publishes **`xnu_live_capped` exactly once** - at the moment of the first drop - and then
  # keeps counting without writing anything. So a log can stop publishing in the middle of a boot
  # that is still running, and every key's last value then belongs to the machine's state *at the
  # cap* and not at its death.
  #
  # **Until 609 no clause in this file read either key.** By the time it was found, three clauses were
  # reading absence as a fact about the machine: rung 0 says "this run did not get past 32768 passes"
  # from where `door_seq` stops, rung 1 says "the park's poll has not come back" because `poll_seq`
  # stops at 2, and the goal block says an absent fixture value is a **POSITION** - the boot stopping
  # there. Each of those is a statement about the boot, and a full channel turns each of them into a
  # statement about the *channel*. That is this project's most-repeated shape (one value, two
  # definitions) at the level of the instrument rather than of the reading.
  #
  # **Measured, so the size of the risk is a number and not a worry.** `xnu_live_cap=0x2000` on every
  # boot; the archived pair carry **~4400** live records, about 54% of capacity, and **neither is
  # capped** - which is why no run so far has shown this. A run that goes further publishes more, so
  # the number to watch is this one, and it is printed on every log rather than only on the bad state.
  #
  # Two signals, and the log carries both: the `capped` key when the channel dropped a record, and the
  # **count of records in the log against the cap the log itself published** - which catches the case
  # the first signal cannot, a run that ended exactly at the cap (the key is published *when the first
  # record is dropped*, so a boot that filled the channel and stopped with nothing more to say has
  # none). Both are read here, and the flag they set is what the three clauses above consult.
  local live_cap live_capped live_recs live_trunc
  live_cap=$(keyval cap)
  live_capped=$(keyval capped)
  live_recs=$(grep -ao 'xnu_live_[a-z0-9_]*=0x[0-9a-f]*' "$log" 2>/dev/null | grep -c . || true)
  [[ $live_recs =~ ^[0-9]+$ ]] || live_recs=""
  live_trunc=0
  if [[ -n $live_capped ]]; then
    live_trunc=1
  elif [[ -n $live_cap && $live_cap =~ ^0x[0-9a-fA-F]+$ && -n $live_recs ]] \
       && (( live_recs >= 16#${live_cap#0x} )); then
    live_trunc=1
  fi
  if (( live_trunc == 1 )); then
    say "  live channel: **TRUNCATED** - ${live_capped:+xnu_live_capped=$live_capped, }$live_recs"
    say "                record(s) against xnu_live_cap=${live_cap:-absent}. The channel stopped"
    say "                publishing while the machine was still running, so every xnu_live_* value"
    say "                below is the state at the cap and NOT the machine's last state: an absent"
    say "                key is a record that was not published, which is not the same as an event"
    say "                that did not happen. Read the three clauses that say otherwise (rung 0's"
    say "                'did not get past 32768', rung 1's 'the park's poll has not come back', and"
    say "                the goal block's 'a POSITION') as UNREAD on this log."
  else
    # **And "not full" is a reading of the channel only when the channel is in the log.** The two
    # states below are not the same fact and one of them is not a value at all (601's class): a log
    # with no `xnu_live_*` record and no `xnu_live_cap` has no live channel in it - the payload's own
    # output only - and saying "the channel published every record this boot made" there would be a
    # claim inferred from a producer that never ran. The middle state is the one that is *about* the
    # channel and cannot be read as either: records without the capacity line, which the init
    # publishes before any record, so it is a capture that lost the channel's beginning.
    if [[ -z $live_cap ]]; then
      if [[ -z $live_recs || $live_recs == 0 ]]; then
        say "  live channel: not in this log - no xnu_live_* record at all, and no xnu_live_cap, so"
        say "                the channel was never brought up in this boot (or this log holds only"
        say "                the payload's own output). Nothing below is a reading of the live keys"
      else
        say "  live channel: $live_recs record(s) and NO xnu_live_cap: the capacity line is published"
        say "                by the channel's own init, ahead of any record, so records without it mean"
        say "                the capture lost the beginning of the channel. It cannot be read as 'not"
        say "                full' from here - but it does not make the three clauses below UNREAD"
        say "                either, because they read the *last* records and what is lost is the"
        say "                *first* (the live channel truncates from the end, a console ring from the"
        say "                start - the two failure directions are opposite and that is why both are"
        say "                read here)"
      fi
    else
      say "  live channel: not full - $live_recs record(s) of $live_cap, and no xnu_live_capped"
      say "                record, so the channel published every record this boot made and an absent"
      say "                key below is an event that did not happen"
    fi
  fi

  # **594 widened this gate, because the arm it registers has no `slot_cwe_` keys at all - and the
  # gate is the whole block.** The switch skips 514's repair, so `SIGPdisabled` stays set, `cpu_idle`
  # leaves by its first door on every pass, and `platform_cache_idle_enter` / `_wfi` / `_exit` are
  # never called. That is every publisher the clauses below score: `slot_cwe_*`, the exit wrapper's
  # three bracket notes, the seam, and the idle's own three counters. Gated on `slot_cwe_` alone the
  # block would print *nothing* for such a log, and a reader silent for a reason and a reader silent
  # because it broke look identical from outside (564's defect, one layer up). So the arm is decided
  # here, from the log's own keys, and the gate accepts either.
  #
  # The signature has to separate "this arm ran" from "the log is truncated", and the key that does
  # it is in the *live channel*, which the arm does not silence: `xnu_live_door_seq` is published by
  # `__wrap_Idle_load_context`, so its presence proves `cpu_idle` ran at all. The other half of the
  # test is the base arm's own evidence *absent* - `repair_seq` (the note went inside the `#if` with
  # the call, `entry_trace.c:1283`). **626 narrowed this to those two keys**, because the four
  # window-family absences are behavioural and not structural: an image that reaches the window
  # publishes them and would otherwise be read as the baseline arm. The table that decision now prints
  # is below; the two keys' *kinds* are the whole of the reading.
  #
  # **The park's console group is deliberately not the marker, and this is the part worth writing
  # down.** It reads like the obvious arm marker - the line says
  # `cpu_signal_handler_internal(FALSE) called %d time(s)` and the count is 1 on the baseline and 0
  # here. But those lines are printed *after* `__real_poll` returns (their own comment: "printed
  # *after* it", both counts "read here, at the park's return"), and on the baseline the park's poll
  # never returns - so the group is in neither 520's log nor 533's. A marker the baseline cannot
  # produce cannot separate the two arms; here it is a witness instead, and a strong one (below).
  # --- the arm, as a table of the keys it is decided on, and which KIND each key is (626) --------
  #
  # **The conjunction that stood here tested six keys as though they were one kind, and 622 measured
  # what treating them as one kind costs.** Exactly one of the six is absent on this arm *by
  # construction*: `entry_note_repair` - the publisher of `xnu_live_repair_seq` - sits inside
  # `#if !STAGE90_XNU_IDLE_NO_SLEEP` in `entry_trace.c` (`:1283`, the call at `:1295`), so the image
  # that skips the repair cannot publish that key whatever the machine does. The other four are
  # **behavioural** - their publishers are compiled into both images and are reached only by a pass
  # that gets into the window: `slot_cwe_*` from `entry_window_note` (`entry_trace.c:1973`, inside the
  # exit), `sip_seq` from `entry_note_setidlepop` (`:1835`, past `cpu_idle`'s first test), `pce_seq`
  # from `entry_note_pce` (`:1912`, the enter wrapper), `wfi_seq` from `entry_note_wfi` (`:1882`).
  # `door_seq` (`entry_note_door`, `:1824`) is neither: it is published on **both** arms, and its
  # absence means `cpu_idle` was never entered at all - which makes it a *reach guard* and not an arm
  # test.
  #
  # **So the old test could be failed by behaviour while the structural key agreed, and the branch it
  # then took was the baseline arm's.** That is the sleeper arm's own reading printed as the other
  # arm's ordinary death, with the rung lines simply absent and nothing anywhere saying why. 622
  # measured it on a 533-derived fixture: appending ONE `xnu_live_slot_cwe_win=` line to a log that
  # had scored rung 1 and rung 1b PASS turned the whole reading into a death block. An absence no
  # clause reads is 609's shape, and here the unread absence was the rung lines themselves.
  #
  # **So the decision is the structural key plus the reach guard, and the four behavioural keys are
  # reported rather than scored.** `door_seq` present with `repair_seq` absent is the sleeper arm;
  # `door_seq` present with `repair_seq` present is the baseline arm; no `door_seq` is the third state
  # below. Excluding the behavioural keys is 598's widening read one level further in: an image older
  # than one of their instruments is still its arm, and 520 is the measured case - it carries
  # `repair_seq`/`sip_seq`/`pce_seq`/`wfi_seq` and **no** `slot_cwe_` key at all, because its image
  # predates that instrument. Measured against every log on this host with a live channel in it (520,
  # 533, 513's two captures, the 576 rehearsal pair), the new test chooses **the same branch the old
  # conjunction chose in all six cases**, so nothing with an artifact behind it is re-keyed. What
  # differs is exactly the log 622 built - a behavioural key present with no `repair_seq` - which now
  # stays on the ladder and prints the disagreement instead of silently becoming a death block.
  #
  # **And the table is printed in every state, before the branch, because the branch choice was the
  # thing answered by silence**: a reader could not tell "this log is the sleeper arm" from "one
  # behavioural key was present and the ladder was skipped". One definition feeds both the branch and
  # the six lines, so a table that says one thing and a branch that does another is not expressible
  # here.
  local arm_door arm_cwe arm_repair arm_sip arm_pce arm_wfi arm_seam arm_behav
  arm_door=$(grep -a -c 'xnu_live_door_seq=' "$log" || true)
  arm_cwe=$(grep -a -c 'xnu_live_slot_cwe_' "$log" || true)
  arm_repair=$(grep -a -c 'xnu_live_repair_seq=' "$log" || true)
  arm_sip=$(grep -a -c 'xnu_live_sip_seq=' "$log" || true)
  arm_pce=$(grep -a -c 'xnu_live_pce_seq=' "$log" || true)
  arm_wfi=$(grep -a -c 'xnu_live_wfi_seq=' "$log" || true)
  # **646: the seam family is the one key kind this table was missing, and it is the kind clause (5)
  # is about.** The family is published from the exit's own `bl` - inside the window - so it behaves
  # like the behavioural keys on a baseline image and is absent *by construction* on the sleeper, which
  # is exactly the distinction the KIND column exists to draw and the distinction 640 measured. It is
  # REPORTED and is deliberately not folded into `arm_behav`, so that this row changes no existing
  # decision: `arm_behav` drives the DISAGREEMENT narration below, and moving a key into it would
  # change what that narration says on a log this phase has never seen.
  arm_seam=$(grep -a -c 'xnu_live_seam_' "$log" || true)
  arm_behav=$(( (arm_cwe > 0 || arm_sip > 0 || arm_pce > 0 || arm_wfi > 0) ? 1 : 0 ))
  idle_no_sleep_arm=0
  if (( arm_door > 0 )) && (( arm_repair == 0 )); then
    idle_no_sleep_arm=1
  fi
  local -a arm_words=(absent present)
  say ""
  say "  arm: which arm this log reads as, and on which keys. Each state is a count from this log, and"
  say "  the KIND is what an absence means: a key whose publisher the arm's switch removes is absent by"
  say "  CONSTRUCTION, and a key published inside the window is absent only because the window was not"
  say "  reached. A key can be both at once - the seam family is - and that row says which arm makes it"
  say "  unreachable by construction."
  printf '    %-26s %-8s %s\n' 'xnu_live_door_seq'   "${arm_words[$(( arm_door   > 0 ))]}" 'reach guard (published on BOTH arms)'
  printf '    %-26s %-8s %s\n' 'xnu_live_repair_seq' "${arm_words[$(( arm_repair > 0 ))]}" 'STRUCTURAL (entry_trace.c:1283)'
  printf '    %-26s %-8s %s\n' 'xnu_live_slot_cwe_*' "${arm_words[$(( arm_cwe    > 0 ))]}" 'behavioural (the exit wrapper)'
  printf '    %-26s %-8s %s\n' 'xnu_live_sip_seq'    "${arm_words[$(( arm_sip    > 0 ))]}" "behavioural (past cpu_idle's first test)"
  printf '    %-26s %-8s %s\n' 'xnu_live_pce_seq'    "${arm_words[$(( arm_pce    > 0 ))]}" 'behavioural (the enter wrapper)'
  printf '    %-26s %-8s %s\n' 'xnu_live_wfi_seq'    "${arm_words[$(( arm_wfi    > 0 ))]}" 'behavioural (the WFI)'
  printf '    %-26s %-8s %s\n' 'xnu_live_seam_*'     "${arm_words[$(( arm_seam   > 0 ))]}" 'window-only (the exit'
  printf '    %-26s %-8s %s\n' ''                    ''                                      "  own bl) - absent by"
  printf '    %-26s %-8s %s\n' ''                    ''                                      '  CONSTRUCTION on the sleeper'
  if (( arm_door == 0 )); then
    say "  => NO door_seq: cpu_idle was never entered, so neither arm's clause is reached - the third"
    say "     branch below is the only one that applies, and it says which fault that is."
  elif (( arm_repair == 0 )); then
    say "  => SLEEPER ARM (594's switch), decided on the two keys that can decide it: door_seq present"
    say "     and repair_seq absent. The ladder below is the reading the owed run is pre-registered"
    say "     against, and it runs on this log."
    if (( arm_behav == 1 )); then
      say "     **AND A DISAGREEMENT: a behavioural key IS present in this log, and on this arm nothing"
      say "     gets into the window** - the switch removes the repair, so SIGPdisabled stays set and"
      say "     cpu_idle leaves by its first door on every pass. A log with a behavioural key and no"
      say "     repair_seq is therefore a log on which the two definitions disagree: the structural key"
      say "     says this image skips the repair, and the behavioural key says the window ran anyway."
      say "     Both can hold only if SIGPdisabled was cleared by something other than the skipped"
      say "     repair, which this phase has never seen - so this reader does not decide it, and says"
      say "     what it did instead: **the ladder below runs**, because its witnesses (door_seq,"
      say "     poll_seq, poll_timeout_ms, the park's console group) are published by sites compiled into"
      say "     BOTH arms and are therefore readings of the machine rather than of the image - and the"
      say "     baseline arm's pop-death block is **withheld on this log**. If the pop's own keys are"
      say "     here (xnu_live_slot_cwe_win / _set / _calls, or a panic ... sleh_abort at the exit), then"
      say "     the window WAS reached and the death block is the reading that applies: read it there,"
      say "     beside the rung lines below, and read the gate's record for which image is in the"
      say "     machine."
    fi
  else
    say "  => BASELINE ARM, decided on repair_seq present - the positive half of the same test, since"
    say "     that key is published from inside the switch and an image carrying the publisher cannot be"
    say "     the arm that skips the call. The behavioural keys above are reported and are not part of"
    say "     the decision: an image older than one of their instruments is still this arm, and 520 is"
    say "     the measured case - repair_seq/sip_seq/pce_seq/wfi_seq present and no slot_cwe_ key at all,"
    say "     because its image predates the window instrument. The death block below is this arm's"
    say "     reading."
  fi

  if (( idle_no_sleep_arm == 1 )); then
    # (6) the arm where the idle never slept - **and this branch is entered from the log's own keys
    # (626's table above), never from the image.** A capture whose image predates the repair lands here
    # too: 513's two captures (2026-09-21, an image with no repair instrument at all) satisfy both
    # tests (door_seq present, repair_seq absent) and take this branch. So what this clause reads is
    # the *machine's behaviour* - "the window did not run" - and which image produced it is the
    # *gate's* reading (the arm switch is in the build's own record). The two cases differ in cause and
    # not in reading: on 594's arm the window's whole family is absent *by construction*, on 513's it is
    # absent because the window was never reached, and the ladder below scores both the same -
    # correctly, since rung 1's park is the same park either way. The gate's note above is what keeps
    # the pair apart; nothing in the log can.
    #
    # **This clause had to exist before the arm was built, not after.** See the gate's note above for
    # why the block would otherwise be skipped; what follows is the reading that replaces it, and it
    # is deliberately not a list of absences scored as passes. Every absence is *named* instead, so a
    # log missing one of them for a different reason can be told from this one.
    #
    #   * `xnu_live_repair_seq` / `_caller` / `_before` / `_after` - the note is inside the `#if`
    #     with the call, so on this arm the kernel's own clear is not made and nothing is published
    #     for it. On the baseline all four are present, `_seq=1`, `_before != _after`.
    #   * `xnu_live_sip_seq` / `_true` - `__wrap_SetIdlePop` is reached only by a pass that gets past
    #     `cpu_idle`'s first test, and that test is exactly what the skipped repair would make false.
    #   * `xnu_live_pce_seq` / `xnu_live_wfi_seq` - the enter wrapper and the WFI, both inside.
    #   * `xnu_live_seam_*` (535/572's instrument) and `xnu_live_slot_cwe_*` - both inside the exit.
    #   * `xnu_live_slot_pre_*` / `_rtcpre_*` / `_post_*` - the exit wrapper's three bracket
    #     publishers.
    #   * `panic ... sleh_abort` at the exit's pop - the death the window leads to. **Its absence is
    #     not evidence here** and nothing below scores it: on this arm the pop is not reached, so a
    #     panic-free log is the arm working, and a panic would be a new fault rather than this arm's
    #     prediction arriving.
    #
    # **And what it must contain: a ladder, because no single key is a witness.** 593 section 4's
    # pre-registered falsifier is *not* an absence - a live spin is on the bus and returns exactly
    # like a working kernel, so the reading has to be something that advances only if pid 1's thread
    # runs past the old death point. Weakest rung first, and the verdict is the strongest rung read:
    local door_max poll_seq_max poll_tmo_max park_min park_over park_group user_ones verdict_ok=0
    local rung1_ok=0
    door_max=$(maxhex door_seq)
    poll_seq_max=$(maxhex poll_seq)
    poll_tmo_max=$(maxhex poll_timeout_ms)
    park_over=$(keyval poll_over)
    user_ones=$(grep -a -c 'xnu_live_sleh_user=0x0*1' "$log" || true)
    park_group=$(grep -a -c 'mini4: the repair --' "$log" || true)
    # The threshold comes out of this tree rather than being written here again: `ENTRY_PARK_MIN_MS`
    # is `entry_trace.c`'s own definition of which `poll` is the park, and a second copy of it in
    # this file is the defect this project has paid for most often. Unreadable means UNREAD, not a
    # default.
    #
    # **And it is this tree's value, which is an assumption about which build the log came from.**
    # Rung 1b compares the log's `poll_timeout_ms` against the constant in the tree *reading* the log,
    # so a log from an image built with a different value would be compared against the wrong number -
    # 593's class, where a value from one build was resolved against another build's artifact. The
    # assumption has never been violated here and that is measured rather than hoped: the line has
    # been `1000` at **every commit that carries it** (512, which introduced it, 514, and every step
    # since), so no log in this tree can be read against a different one. It is stated because the
    # next change to that constant would silently re-point every archived log's rung 1b, and the
    # sentence that says which build a comparison is made against is cheap here (the criterion line
    # below prints its own source for the same reason).
    park_min=""
    park_src=$REPO_ROOT/stages/stage90/xnu_arm_boot/entry_trace.c
    # **The extraction tolerates any whitespace, and the refusal it falls back to names the right
    # cause - both repaired in 632, and the reason is that this is the witness's own threshold.**
    #
    # The pattern used to be `^#define ENTRY_PARK_MIN_MS \([0-9]*\)`, requiring **exactly one space** in
    # each of the two gaps. 632 measured it, verbatim, against six spellings of one line and **four of
    # them emptied `park_min` on a perfectly readable file**: two spaces after the name (an editor
    # aligning the value column), a tab, two spaces after `#define`, and a parenthesised value. So a
    # reformat - the kind of edit that looks like nothing - would have turned rung 1b from PASS/FAIL
    # into UNREAD on the morning of a press, and the operator would have had no way to tell the reformat
    # from a genuinely absent definition.
    #
    # **And the sentence it printed was false in that case.** `park_min` is empty in two different
    # states, and both printed "ENTRY_PARK_MIN_MS could not be read from <path>": the file being
    # unreadable, and the file being read without the expected form being found. The second names a
    # failure of *reading* about a file that reads fine - 615's class (a refusal naming a fact about a
    # different thing) arriving in the clause whose number decides the witness. The two are now separate
    # sentences, and the second says what actually happened.
    if [[ -r $park_src ]]; then
      park_min=$(sed -n 's/^#[[:space:]]*define[[:space:]][[:space:]]*ENTRY_PARK_MIN_MS[[:space:]][[:space:]]*\([0-9][0-9]*\).*/\1/p' \
                   "$park_src" | head -1 || true)
    fi

    # **This clause's opening used to assert a fact about the image that the log cannot establish,
    # and 600 measured the counterexample on a real artifact.** It said "This log's image skipped
    # 514's one-shot repair" - but what the test above asks (door_seq present, repair_seq absent) is
    # *"the window did not run"*, and an image that **predates the repair's existence** satisfies it
    # identically. Measured: 513's two archived captures (2026-09-21, two days before the arm existed,
    # an image with no repair to skip and no `entry_window_note` to publish) carry `door_seq` 25
    # records to `0x01000000`, `poll_seq` to 4 with two 2000 ms parks, and **no**
    # `repair_seq`/`sip_seq`/`pce_seq`/`wfi_seq`/`slot_cwe_*` - so this branch prints its PASS lines
    # for them, which it should: the machine did reach the park and did come back. What it must not do
    # is call that a reading of *this arm*, because the log alone cannot tell the two apart - an image
    # publishes no build marker (549), so the arm is the **gate's** reading and the record's, and this
    # clause reads the machine's behaviour. The sentence now says which is which.
    #
    # **And the paragraph below makes a claim about the machine that a behavioural key falsifies, so
    # 626 conditionalised it.** "SIGPdisabled stayed set / the window was never entered" is the
    # ordinary reading of this branch, and a behavioural key is published only by a pass that *did*
    # enter the window - so on the disagreement log the reader would be asserting the opposite of a
    # record it printed two paragraphs above. That is the defect this file names most often, in the
    # one place where it would be the reader contradicting itself.
    say ""
    if (( arm_behav == 1 )); then
      say "  **Not the ordinary sentence for this branch: this log's own behaviour contradicts it**"
      say "  (the disagreement above). What that sentence would say - SIGPdisabled stayed set, the idle"
      say "  left by its first door, the window was never entered - is the opposite of a record this log"
      say "  carries, so it is not printed and what follows is the ladder alone. Its readings do not"
      say "  depend on which arm this is: their witnesses are published on both."
    else
      say "  the idle does not sleep - which is 594's arm, and also any image where the window did not"
      say "  run. **What this log establishes is the machine's behaviour, not the image's identity**:"
      say "  SIGPdisabled stayed set, cpu_idle left by its first door on every pass, and the window"
      say "  whose pop {fp, pc} this phase measures was never entered - so the whole family clauses"
      say "  (1)-(5) score is absent, and this clause reads what is left. **Measured counterexample, so"
      say "  the distinction is not academic**: 513's two captures (2026-09-21, an image that predates"
      say "  514's repair, so there was nothing for it to skip) satisfy this same test - and"
      say "  **measured on those two logs**, the ladder below scores them rung 0 PASS, rung 1 PASS,"
      say "  rung 1b PASS, rung 2 its NOTE and rung 3 PASS. So a log in this branch is a reading of"
      say "  the machine and not of the arm: which image is in the machine is the gate's reading -"
      say "  the arm switch is in the build's own record - and this clause cannot see it from a log."
    fi
    say "    xnu_live_door_seq reaches ${door_max:-absent} - the passes that left by the first door"
    # **Rung 0, and the threshold is `> 32768` rather than `>= 32768`** - which is the correction
    # this clause needed and could only be read off the archived pair. Both publishers of this key
    # (`entry_stubs.c`'s `entry_note_idle` and `entry_note_door`) write at powers of two with **no
    # ceiling**: there is no `0x8000` cap in either, and `xnu_live_capped` is absent from both
    # archived logs, so the live channel never dropped a record. Both logs therefore stop at
    # **exactly 16 records ending at `0x8000`**. A run that survives past that point publishes the
    # next powers of two and nothing has to be assumed to say so.
    #
    # **`0x8000` is where the publisher last spoke, not where the machine died - and the earlier
    # wording said the second.** Measured on 533's capture, which is one uninterrupted run in
    # insertion order: the `door_seq=0x8000` record sits at line 8133 and the fixture's own
    # `poll_seq=2`, `read`, `open` (the control), `exit`, `wait_done`, the park's `repair_seq=1`,
    # `sip_seq=1`, `pce_seq=1`, `wfi_seq=1` and the fatal `sleh` all follow it, through line 8363.
    # So both baselines **lived on past `0x8000` for the whole remainder of the boot** and died
    # before reaching the next power of two. The test is unchanged - reaching `0x10000` passes is
    # still the thing no baseline run did - but the claim that `0x8000` *is* the death point is
    # falsified by the same log that supplied the number, and a reader sent to "where the machine
    # died" would look at the wrong record.
    #
    # It is a **witness and not only a guard**, and it is the one that does not need the park's
    # poll to return: `entry_note_idle` is entered on *every* pass, so a machine whose idle is
    # spinning past 32768 passes says so whether or not pid 1's thread ever runs again. That is
    # exactly the ambiguity 593 section 4 named - a live spin looks like a working kernel from
    # outside - and this rung separates "died at the first window pass" from "did not die", leaving
    # rung 1 to decide whether the thread also progressed. `>= 32768` could not do that: the
    # baseline satisfies it, and this clause printed a clearance for a number its own baseline
    # reaches ([[mi4-measurement-defects]]).
    if [[ -n $door_max ]] && (( door_max > 32768 )); then
      say "  PASS  and it went past the last record every previous boot published: 520 and 533 both"
      say "        end their series at exactly 0x8000 (16 records, and xnu_live_capped absent, so"
      say "        nothing was dropped). That is where the *publisher* last spoke and not where the"
      say "        machine died - 533's own capture shows the baseline running on past it, through"
      say "        the fixture's own waits and the park's repair, sip, pce and wfi, before the fatal"
      say "        sleh. So this run reaching the *next* power of two is the pass that killed every"
      say "        boot before it - the first arrival at the window, 593 - not having happened"
    elif [[ -n $door_max && $live_trunc -eq 1 ]]; then
      # **609: the FAIL below is a claim about the machine, and a full channel makes it a claim about
      # the channel.** The sentence it would print - "this run did not get past 32768 passes" - takes
      # `door_seq`'s ceiling as the machine's own stopping point, which is true only while the channel
      # is still publishing. `entry_live_write` drops records once its counter reaches the cap, so on a
      # truncated log the series ends because the *channel* ended, and the machine may have run far
      # past 32768 passes with its later publishes discarded. The state is therefore UNREAD here, and
      # it says which of the two facts it has: the series stops at or below 0x8000 AND the log's own
      # channel is full, so this reading cannot separate them.
      say "  UNREAD  and it stops at or below 0x8000 - and this log's live channel is TRUNCATED, so"
      say "        the two readings this rung exists to separate cannot be separated here: the machine"
      say "        may have stopped before 32768 passes, or it may have run past them and had its later"
      say "        publishes dropped at the cap. The line above is the channel's state; rung 1 below"
      say "        reads the park, and it is UNREAD on this log for the same reason"
      verdict_ok=0
    elif [[ -n $door_max ]]; then
      say "  FAIL  and it stops at or below 0x8000, the last record 520 and 533 publish: the"
      say "        publisher is powers-of-two with no ceiling and neither log is capped, so a machine"
      say "        that reached 65536 passes would have published 0x10000 and this one did not. (It"
      say "        is a threshold and not a death point: the baselines went on past 0x8000 - 533's"
      say "        capture shows the whole userland phase after it - and died below the next power of"
      say "        two.) So this run did not get past 32768 passes and the arm's own reading is not"
      say "        in it - check the gate's signature before reading anything below as this arm."
      say "        **And this rung is a progress test, not an identifier**: 513's image - the one that"
      say "        predates the repair - reaches 0x1000000 here, 512 times this threshold, so passing"
      say "        it says the machine kept going and not that 594's arm is what ran. The image is the"
      say "        gate's reading; this is the machine's."
      say "        **And if rung 1 below passes anyway the two disagree, which is itself the"
      say "        reading**: the park is the third poll and cannot return on a machine that never"
      say "        reached 32768"
    else
      say "  UNREAD  xnu_live_door_seq is absent, and that is the one thing this arm cannot make"
      say "        absent: entry_note_idle is entered on every pass and publishes this key from the"
      say "        first. Its absence means cpu_idle was never reached at all, so nothing below is"
      say "        a reading of this arm"
      verdict_ok=0
    fi
    say "    xnu_live_repair_seq absent: the repair was not made, which is the switch. (Baseline:"
    say "    0x00000001, with _before != _after - 593 section 4.)"

    verdict_ok=0
    rung1_ok=0
    if [[ -n $poll_seq_max ]] && (( poll_seq_max > 2 )); then
      rung1_ok=1
      say "  PASS  a poll came back after the second: xnu_live_poll_seq reaches $poll_seq_max. The"
      say "        park is the third poll and entry_note_poll runs only *after* __real_poll"
      say "        returns, so a published 3 is the park having returned - and on the frozen arm it"
      say "        structurally cannot: 520 and 533 both stop at 2, with timeouts 5 ms and 40 ms,"
      if (( idle_no_sleep_arm == 1 )); then
        say "        because the boot dies inside the park's own poll. **A return here is NOT that"
        say "        death point passed**: this arm's switch removes the repair, so the window is"
        say "        unreachable by construction (599 section 5, 649) and the pop that kills the"
        say "        other arm never runs. A returning park on this arm is 513's outcome - the"
        say "        window never entered - so it is evidence about the door and not about the pop"
      else
        say "        because the boot dies inside the park's own poll. This is the death point passed"
      fi
      # **And the return is measured, not hoped for** - which is what turns the falsifier into a
      # sharp one. The two polls that DID return in the archived pair both returned *before* the
      # repair: it is made inside the `timeout >= ENTRY_PARK_MIN_MS` block, so a 5 ms and a 40 ms
      # ask never enter it, and `xnu_live_repair_seq` is 1 in both logs with `_before != _after`,
      # i.e. exactly once, at the park. So those two returns happened with `SIGPdisabled` **set**
      # and `cpu_idle` leaving by door 1 - the state 594's arm freezes the machine in - and both
      # came back with `error=0`, `retval=0` and tick counts that scale with the ask (5 ms ->
      # 0x203c7/0x30c06, 40 ms -> 0xd5976/0xe2fd4). The timer path demonstrably works in this
      # state, so a `poll_seq` of 2 on this arm cannot be explained by a clock that stopped: it
      # would be a real surprise owing another explanation, which is what a falsifier is for
      if [[ -n $poll_tmo_max ]]; then
        say "        (measured premise: the two asks that returned in both archived logs did so"
        say "        *before* the repair - a 5 ms and a 40 ms ask never enter the park block, and"
        say "        repair_seq is 1 in both logs - so they returned with SIGPdisabled SET and the"
        say "        idle leaving by door 1, which is this arm's state. The park's return is the"
        say "        same path, not a new one)"
        say "        **And the same state has already returned from a park on hardware**: 513's two"
        say "        captures each ran two 2000 ms parks to completion with SIGPdisabled set and the"
        say "        window never entered, and their four park tick counts are 0x024c37ca / 0x024c3e14"
        say "        in the first capture and 0x024c072a / 0x024c0c95 in the second (~38.5 M ticks for"
        say "        the asked 2000 ms plus this arm's own idle overhead), so rung 1's shape is a"
        say "        measured outcome of this state and not an extrapolation - and it is also the"
        say "        hardware confirmation that the tick does not need the IPI, which 599 derived"
        say "        from the disassembly: three of the four bl rtclock_intr sites are outside the"
        say "        IPI handler, and here the parks expired with that handler unable to run.)"
      fi
      verdict_ok=1
      if [[ -n $park_min ]] && (( poll_tmo_max >= park_min )); then
        say "  PASS  and it is the park and not a stray ask: the largest recorded timeout is"
        say "        $poll_tmo_max ms, at or above this tree's own park threshold, $park_min ms"
      elif [[ -z $park_min && ! -r $park_src ]]; then
        say "  UNREAD  and whether it is the park is unread: $park_src could not be READ at all, so"
        say "        this clause has no threshold to compare the largest recorded timeout"
        say "        (${poll_tmo_max:-none}) against - and it will not write a number here instead,"
        say "        because a second copy of entry_trace.c's own constant is this project's most"
        say "        repeated defect"
        verdict_ok=0
      elif [[ -z $park_min ]]; then
        say "  UNREAD  and whether it is the park is unread: $park_src IS readable and carries no"
        say "        \`#define ENTRY_PARK_MIN_MS <digits>\` line this clause could read, so the largest"
        say "        recorded timeout (${poll_tmo_max:-none}) is not compared against a number written"
        say "        here. **This is a statement about the definition's FORM and not about the file's"
        say "        presence** - open it and look: a reformat (a second space, a tab, a parenthesised"
        say "        value) is the likely cause, and 632 measured four such spellings that this clause"
        say "        used to refuse as if the file were unreadable"
        verdict_ok=0
      elif [[ $live_trunc -eq 1 ]]; then
        say "  UNREAD  and whether it is the park is unread: this log's live channel is TRUNCATED,"
        say "        and the largest recorded timeout is ${poll_tmo_max:-absent} ms - below the park's"
        say "        own threshold of $park_min ms. A cap landing between the park's own two writes"
        say "        publishes poll_seq without its timeout (they are separate entry_live_write calls"
        say "        and the seq is written first), so the number this clause would compare against is"
        say "        a statement about the instrument and not about the machine - and a falsifier that"
        say "        fires on a full channel is not a falsifier"
        verdict_ok=0
      else
        say "  FAIL  and the largest recorded timeout is ${poll_tmo_max:-absent} ms, below the"
        say "        park's own threshold of $park_min ms - so the poll that returned is not the"
        say "        park, and what the third call did is not read by this rung"
        verdict_ok=0
      fi
    elif [[ $live_trunc -eq 1 ]]; then
      # **609, and this is the rung where it matters most: this FAIL is the arm's falsifier.** "The
      # park's poll has not come back" is read from `poll_seq` stopping at 2, and on a truncated log
      # the third poll may have returned with its record dropped at the cap - which would turn the
      # strongest negative reading this project has into a statement about the instrument. The
      # distinction is the whole falsifier, so it is UNREAD here rather than FAIL: a falsifier that
      # fires on a full channel is not a falsifier. The three-causes account below (the arm, the SoC's
      # reset) does not apply either - it excludes a *cause of the machine not returning*, and this
      # state is one where the machine may have returned and said so into a full channel.
      say "  UNREAD  and no poll record past the second (xnu_live_poll_seq reaches"
      say "        ${poll_seq_max:-absent}) - on a log whose live channel is TRUNCATED, and that is"
      say "        the one state in which this rung's FAIL is not available: the third poll may have"
      say "        returned with its record dropped at the cap, so the absence here is a record that"
      say "        was not published rather than a park that did not come back. **This does not clear"
      say "        the arm** - it means this log cannot answer the question, and the run that can has"
      say "        to be one whose channel did not fill"
      verdict_ok=0
    else
      say "  FAIL  no poll record past the second (xnu_live_poll_seq reaches"
      say "        ${poll_seq_max:-absent}) - the park's own poll has not come back, which is this"
      say "        arm's failure mode (593 section 4: the spin is alive and never progresses) and"
      say "        **not** a hang: the device returns either way, so the runner's exit code cannot"
      say "        tell these apart and this line is the only thing that can"
      # **And one cause of this FAIL is excluded by measurement rather than by argument, because
      # the FAIL has two of them and only one is about the arm.** A park that does not return could
      # be the arm's failure mode, or it could be the SoC being reset out from under the park - and
      # the second would make this line a statement about the watchdog instead of about the arm.
      # It is the first. The numbers are 533's capture and not a recollection: the payload arms the
      # watchdog before the jump and nothing in the image pets it, so the reset comes 28 s later
      # (two of the payload's own records cross-check it: bark 0x000c7fb5 at hw_watchdog_hz=0x7ffd
      # is 25.0 s against hw_watchdog_timeout_s=0x19, and bite 0x000dffac is 28.0 s); and 533's
      # capture carries both ends of the boot's own share of that budget on ONE counter - the
      # payload's `timebase_boot_ticks_lo` (0x0355ded6) and the park's first read, which on the arm
      # that makes the repair is `xnu_live_repair_before` (0x04afba8b), against the image's own
      # `timebase_cntfrq` - 1.18 s.
      #
      # **It is guarded on the arm record, and the guard is the point.** This sentence and the
      # `hardware watchdog:` line above it are about the same instrument, so an unconditional
      # version would print "the watchdog is armed, 28 s" twelve lines under "NOT confirmed armed"
      # on any log that lost the payload's records - one value with two definitions, in one output,
      # which is this project's most-repeated defect. The guard makes them agree by construction.
      #
      # **And the numbers are cited rather than re-read from this log on purpose.** The key that
      # marks the park's start is `xnu_live_repair_before`, and that key is absent on this arm by
      # construction (594's guard, `entry_stubs.c`), so a derivation keyed on it would be UNREAD in
      # exactly the state it exists for - 586's lesson. The mechanism and one artifact that
      # measures it is what can be said here; the interval's own arithmetic from this log's payload
      # records is owed and not taken.
      if (( watchdog > 0 )); then
        say "        **And the SoC's own reset is not what stopped it - measured, not argued**: this"
        say "        log carries the payload's own arm record, and nothing in the image pets the"
        say "        watchdog, so the reset is 28 s after the arm - 533's capture states it twice"
        say "        over: hw_watchdog_timeout_s=0x19 with bark 0x000c7fb5 at hw_watchdog_hz=0x7ffd"
        say "        is 25.0 s, and bite 0x000dffac is 28.0 s. 533 also carries both ends of the"
        say "        boot's own share of that budget on one 19.2 MHz counter - the payload's timebase"
        say "        sample 0x0355ded6 and the park's first read 0x04afba8b, timebase_cntfrq=0x0124f800"
        say "        - 1.18 s apart. So the park starts about a second in, and a 2000 ms ask is not"
        say "        racing a 28 s reset: read this FAIL as the park not coming back."
      else
        say "        **And whether the reset is what stopped it is not read here**: this log carries"
        say "        no arm record (the line above is where that absence is read), so the interval is"
        say "        cited and not measured - 28 s after an arm the payload makes unconditionally"
        say "        (stage90_main.c:1205, 533's capture for the number), of which the boot spends"
        say "        about 1.18 s before the park starts. Read this FAIL as the park not coming back"
        say "        either way, but on this log the margin between them is 533's and not this log's."
      fi
    fi

    if (( park_group > 0 )); then
      say "  PASS  rung 2, and it is the strongest positive record here: the park's own console group"
      say "        is in the log ($park_group line(s) matching 'mini4: the repair --'). It is printed"
      say "        after the park's poll returns and the baseline cannot print it at all - neither"
      say "        520 nor 533 has it - so it says the same thing as rung 1 and says it as a record"
      say "        rather than as a count. Read its own count: it should read"
      say "        'cpu_signal_handler_internal(FALSE) called 0 time(s)' - 0 is this arm stating"
      say "        itself, where the baseline's would read 1"
    elif (( verdict_ok == 1 )); then
      say "  NOTE  the park's console group is not in the log, although its poll returned. The group"
      say "        is 4 printf lines after that return, so a ring that wrapped between the return and"
      say "        the read loses them while keeping the live keys - the two are different channels."
      say "        It costs a corroboration and not rung 1, but it is the thing to check by hand"
    fi

    if [[ -n $park_over ]]; then
      say "  PASS  rung 3: xnu_live_poll_over=$park_over - a fifth poll published, so pid 1 ran on"
      say "        past the park and asked again rather than parking once and never returning"
    fi
    if [[ $user_ones =~ ^[0-9]+$ ]] && (( user_ones > 3 )); then
      say "  PASS  rung 4: $user_ones user-mode fault record(s), against 520's 3 - the boot got"
      say "        further into user mode than any run has"
    fi

    say ""
    if (( verdict_ok == 1 )); then
      say "  => this arm did what it was built to do at the point that matters: the pass that killed"
      say "     every boot before it - the first arrival at the window, 593 - did not happen, and the"
      say "     boot ran past the park. **That is progress and it is not the goal yet**: 593 section 4"
      say "     traded the sleep for the boot, so the readings that decide 'the OS is up' are the ones"
      say "     after this point (item (4)'s user-mode records, the console, and the fixture's own"
      say "     syscalls in the live channel). If they are here, read them before calling this a boot."
      if (( park_group > 0 )) && (( poll_seq_max > 2 )); then
        say "     Both of the two independent witnesses are present, which is the conjunction this"
        say "     phase asks for rather than the weaker of the two."
      fi
    else
      say "  => the arm's own reading: the idle is spinning by the first door - which is the arm"
      say "     working - and what the park's poll did decides the rest. It is **not a hang and not a"
      say "     brick**: nothing was written to storage (fastboot boot only), a power press returns the"
      say "     device, and no outcome of this arm can change that."
      if (( rung1_ok == 1 )); then
        say "     A poll that is not the park came back, so rung 1's FAIL is rung 1b's: read that line"
        say "     rather than this one - the third call returned something other than the park's own"
        say "     ask, and what that means is not read here."
      else
        if (( live_trunc == 1 )); then
          # **609: this sentence is the falsifier said in the summary's own voice, and on a truncated
          # log it is the one claim that must not be made.** The branch above prints UNREAD for the
          # same state; a summary line that then asserts "the park's poll has not come back at all"
          # would be the reader contradicting itself in its last paragraph, which is worse than either
          # half alone - a reader that says UNREAD and then draws the conclusion anyway.
          say "     Whether the park's poll came back is **UNREAD on this log**, because the live"
          say "     channel is full: the third poll may have returned with its record dropped at the"
          say "     cap. This is not the falsifier and it is not a clearance - the falsifier needs a"
          say "     log whose channel published to the end of the run."
        else
          say "     The park's poll has not come back at all, which is the falsifier 593 section 4"
          say "     pre-registered for this arm. Read rung 1's FAIL line: it names which half of the"
          say "     witness is missing, and rung 2's NOTE (if present) says whether the console channel"
          say "     simply lost the group that rung 1's return should have printed."
        fi
      fi
    fi
  # **The third state, and the branch this one changed in 598: the condition was
  # `grep -a -q 'xnu_live_slot_cwe_'`, which is 533's *instrument* rather than the arm's signature.**
  # Half of this project's baseline - 520 - has no `slot_cwe_` key at all, because its image predates
  # `entry_window_note`, so a log that satisfies the baseline signature *and* one that never reached
  # `cpu_idle` arrived at the same place: the `else` below, under one paragraph that separated them in
  # prose and not in control flow. The three states are now three branches, and the test between the
  # second and third is the one that paragraph already named - `xnu_live_door_seq`, published by
  # `__wrap_Idle_load_context` on the first passes of every boot that reaches `cpu_idle`:
  #
  #   * the sleeper arm   - `door_seq` present, `repair_seq` absent (**626**: the four window-family
  #     absences are no longer part of the test - they are behavioural, and a log that reaches the
  #     window publishes them, so scoring them here read that log as the arm below);
  #   * the baseline arm  - `door_seq` present and `repair_seq` present, that key's presence being the
  #     positive half of the same test, since its publisher is inside the switch; the window family is
  #     then itself *or not*, because an image older than one of their instruments is still this arm and
  #     its death is still the reading (520 has no `slot_cwe_` key at all and is read here);
  #   * no `door_seq`     - `cpu_idle` was never entered, which is a fault earlier than either arm's.
  #
  # So the `elif` is a **widening**, not a re-keying: every log that took this branch before still
  # does, and the gain is exactly the logs that satisfy the *baseline* signature without the pair.
  # 594 section 5 and 595 section 9(b) named this as the better fix and left it as its own step,
  # because it changes the condition under which five clauses run and the validation it owes is
  # against 520 - a real artifact, and the one log in this project that lands in the gap. **626 took
  # the same step on the other side of the branch**, and measured it the same way: the six logs on this
  # host with a live channel in them all take the branch they took before.
  elif grep -a -q 'xnu_live_door_seq=' "$log"; then
    local cwe_win cwe_set cwe_calls pre_calls rtcpre_calls post_calls storm panics user_ones
    local sleh_lr="" sleh_pc="" sleh_sp="" sleh_seen=""
    local pop_lr="" pop_lr_src="" pop_death=0 pop_named=0 cache_arm=unread arm_seen=unknown
    local pce_up="" pce_ncpu="" pce_after_sctlr="" arm_set="" arm_set_key="" verdict_ok=1
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

    # The one address a correct frame can hold in its second word, derived from the same ELF - see
    # `exit_caller_lr_addr`. Empty means UNREAD, and there is deliberately no literal to fall back on.
    caller_lr=$(exit_caller_lr_addr 2>/dev/null || true)

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
    # **The cell, read from whichever of the two publishers this image has - and the second is not a
    # stand-in for the first.** `slot_cwe_set` is `entry_window_note`'s read of `SCTLR` at the
    # window's near end; `xnu_live_pce_after_sctlr` is `entry_note_pce_after`'s read of the *same
    # register immediately afterwards*, and the source is why the two must be equal: `entry_trace.c`
    # calls `entry_window_note(win, entry_sctlr())` and then, with nothing between but the two
    # publishers' own argument reads, `entry_note_pce_after(..., entry_sctlr())` - no `SCTLR` writer
    # between them, and neither publish path contains a cache operation. Both archived baselines
    # carry the older key; only 533 carries the pair.
    #
    # Keying the cell on the pair alone left **half of this project's baseline with no cell reading at
    # all** (520, whose image predates `entry_window_note`), which is what 598 is for. The
    # substitution is *checked* wherever both keys are present - clause (2) compares them and says so
    # in the log - because two names for one value is the defect class this project has paid for most
    # often, and the check is what makes this a reading of one register rather than a second definition
    # of one number.
    pce_after_sctlr=$(keyval pce_after_sctlr)
    if [[ $cwe_set =~ ^0x[0-9a-f]+$ ]]; then
      arm_set=$cwe_set
      arm_set_key="xnu_live_slot_cwe_set"
    elif [[ $pce_after_sctlr =~ ^0x[0-9a-f]+$ ]]; then
      arm_set=$pce_after_sctlr
      arm_set_key="xnu_live_pce_after_sctlr"
    else
      arm_set=""
      arm_set_key=""
    fi
    arm_seen=unknown
    if [[ -n $arm_set ]]; then
      if (( (arm_set & 4) == 4 )); then arm_seen=522; else arm_seen=533; fi
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
      say "        valid address - this log's is pc=$sleh_pc sp=$sleh_sp at storm=$storm, and 547"
      say "        section 1 reads it as the popped word itself, pc = r11 & ~1"
      # **"520's is" until 598, and it interpolates this log's own registers.** The sentence
      # compares nothing - it prints `$sleh_pc`/`$sleh_sp`/`$storm` from the log in hand - so on
      # 533 it read "520's is pc=0x33f1c1b4", a value 520's log does not contain anywhere (520's
      # abort is `pc=0x04b79074`, as this same file's clause (1) note says). It was written when
      # 520 was the only baseline that carried this line and became false the moment a second one
      # did; the fix is to name the artifact the numbers actually come from.
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
          say "  ARM   522's arm: $arm_set_key=$arm_set has C set - the near-end re-enable ran and"
          say "        took, so this log is an image that re-enables the D-cache at the window's end"
        elif [[ $arm_seen == 533 ]]; then
          say "  ARM   533's arm: $arm_set_key=$arm_set has C clear - the window is left exactly as"
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
          say "  UNREAD  neither xnu_live_slot_cwe_set nor xnu_live_pce_after_sctlr is a readable"
          say "          SCTLR in this log, so which arm ran is not decidable from it"
          verdict_ok=0
        fi
        say "        (entry_window_note ran $cwe_calls time(s) in the passes this log recorded)"
      else
        say "  FAIL  slot_cwe_win=${cwe_win:-absent}: C was NOT clear there, so the window was not"
        say "        open where the image claims - neither arm's second reading means anything"
        verdict_ok=0
      fi
    elif [[ -z $cwe_set && -z $pce_after_sctlr ]]; then
      # **An image older than both publishers, and the state this branch is for is *not* 520's.**
      # 520 predates the pair and carries `xnu_live_pce_after_sctlr`, so it is read by the branch
      # above; what lands here is a log that has `door_seq` (so `cpu_idle` ran) and no SCTLR publisher
      # at all, and for it the cell is not a reading under either name. The text that used to sit here
      # said "the pair's keys are in this log but the note's own count is not readable", which was
      # true when this branch was reachable *only* through the pair and became false of exactly the
      # logs 598's widening adds - a reader would have gone looking for keys the log does not have.
      # The first draft of this very branch named 520 as its example anyway, and **running it on 520
      # is what caught that**: 520 took the branch above and never reached this text. The printed
      # sentence now says so in as many words, because the next reader will make the same guess.
      say "  UNREAD BY CONSTRUCTION  this log carries xnu_live_door_seq, so cpu_idle ran, and it"
      say "          carries neither the pair (xnu_live_slot_cwe_*) nor the older publisher of the"
      say "          same register (xnu_live_pce_after_sctlr): the cell is not a reading of this log"
      say "          under either name, as a fact about the image rather than a failed read."
      say "          **This is not 520's shape** - 520 predates the pair and carries the older key,"
      say "          which is why it is read instead of named here; the state this branch is for is an"
      say "          image older than *both*. Clauses (1) and (3) still read the log - the death's"
      say "          shape and where the pass stopped - and they are what this lane exists for."
      verdict_ok=0
    else
      say "  UNREAD  slot_cwe_calls=${cwe_calls:-absent} is not a count >= 1, while some key of the"
      say "          pair or the older publisher is in the log: the count did not publish and the"
      say "          window's own proof (slot_cwe_win) is missing with it, so nothing is claimed"
      say "          here about the window's opening. Clause (1) above reads the death without it"
      verdict_ok=0
    fi

    # **The substitution's own check, and it is why the fallback above is a reading rather than a
    # second definition of one value.** Wherever an image carries both publishers they are two reads
    # of one register at one point in the wrapper (see the note above the extraction), so they must be
    # equal; the sentence is printed on both outcomes rather than only on the disagreement, because a
    # check that succeeds by printing nothing cannot be told from one that never ran. A disagreement
    # is a FAIL and not a curiosity: it would mean one of the two keys is not the register read this
    # file's note says it is, and every cell reading in this block rests on that.
    if [[ $cwe_set =~ ^0x[0-9a-f]+$ && $pce_after_sctlr =~ ^0x[0-9a-f]+$ ]]; then
      if [[ $cwe_set == "$pce_after_sctlr" ]]; then
        say "  both publishers of the window's SCTLR agree: xnu_live_slot_cwe_set=$cwe_set and"
        say "        xnu_live_pce_after_sctlr=$pce_after_sctlr are the same value, so the cell read"
        say "        above does not depend on which of the two this block keyed on"
      else
        say "  FAIL  xnu_live_slot_cwe_set=$cwe_set and xnu_live_pce_after_sctlr=$pce_after_sctlr"
        say "        disagree, and they are two reads of one register with no SCTLR writer between"
        say "        them (entry_trace.c: entry_window_note(win, entry_sctlr()) then"
        say "        entry_note_pce_after(..., entry_sctlr())). One of the two keys is therefore not"
        say "        the read this file's note says it is, and the cell reading above rests on it"
        verdict_ok=0
      fi
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
    # **This block is self-selecting, and until 646 it was self-silencing, which is a different thing.**
    # 535 and 572 both wrap `FlushPoU_Dcache` and keep only the call whose return address is the exit's
    # own `bl`, so `xnu_live_seam_*` is published by those images and by no earlier one - there is no
    # build marker in the entry image for this block to test instead (clause (2)'s note).
    #
    # **But "this log has no key" has TWO causes, and the arm key below separates them on both arms of
    # this phase.** The first is that the image predates the instrument (520's). The second is that the
    # image CARRIES the arm and cannot publish it: on 594's arm the seam's only acting site is the
    # exit's own `bl`, `platform_cache_idle_exit` is entered only through `cpu_idle` past the
    # `SIGPdisabled` gate, and that gate is a closed fixed point when the switch removes 514's repair -
    # so on that arm the window is entered *never* and the pair is unreachable by construction
    # (640 sections 2-4, `tools/check_idle_window_unreachable.py`). A reader who took the first cause
    # alone would read "no key" as "the arm was not run", which is the opposite of the truth on the arm
    # the owed press sends.
    #
    # So a keyless log is **narrated with both causes named** (the `else` at the bottom of this gate,
    # keyed on the `arm:` table's own `idle_no_sleep_arm`) rather than passed over in silence. The
    # reading stays honest about what a log can decide: the two causes are separated by the arm table
    # above, and which *image* is in the machine is the gate's reading and not this log's (628's
    # discipline, the same one clause (6)'s comment applies to 513's captures).
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
    # The `sp` test is the one that says the arm read the *right object*, and **it is a `+8`, not an
    # equality** - which this block had wrong until 576, and the wrong form prints FAIL on a *good* run.
    # The arithmetic, from the image and from a log that carries both numbers:
    #
    #   * the arm's `sp` at the seam is the exit's own frame slot: `platform_cache_idle_exit` pushes 8
    #     bytes at `0x800462d4` and the seam's `bl` is the next instruction, so `sp` there is what the
    #     `push` wrote - and the wrapper is a `naked` three-instruction trampoline, so it hands that same
    #     `sp` on unchanged;
    #   * the abort's `sleh_sp` is **the same sp after the `pop`**: the `pop {fp, pc}` at `0x8004633c`
    #     frees those 8 bytes, and 546 section 1's whole observation is that the push and the pop are 8/8
    #     with no `sp` change between them - so post-pop `sp` = pre-push `sp` = slot + 8;
    #   * and the entry wrapper publishes the same number under another name: its own `mov %0, sp` runs
    #     *after* its prologue (`8047c964: str r4,[sp,#-8]!`), so `xnu_live_slot_pre_sp` is the `sp` the
    #     real exit is entered with - 520's log has `slot_pre_sp=0x8054fed0` and that same log's fatal
    #     abort has `sleh_sp=0x8054fed0`, while the exit's slot is 8 below both.
    #
    # So the slot is `sleh_sp - 8`, and the two relations are checked separately because they are two
    # independent records of it: the dump's register and the exit wrapper's own capture. **The reading the
    # equality would have thrown away is the good run**: 535 and 574 both put a correct arm's `sp` at
    # 0x8054fec8 against a `sleh_sp` of 0x8054fed0, so `==` prints FAIL for exactly the run this clause
    # exists to score a PASS.
    if [[ $seam_calls =~ ^0x[0-9a-f]+$ ]] && (( seam_calls >= 1 )); then
      local seam_lr seam_sp seam_sctlr seam_other seam_other_lr rtcpre_pop seam_op seam_end_run
      local slot_pre_sp seam_m8 seam_m4
      local seam_b0 seam_b1 seam_a0 seam_a1 pair_cell
      seam_op=$(keyval seam_op)
      seam_end_run=$(keyval seam_end_run)
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
      slot_pre_sp=$(keyval slot_pre_sp)
      seam_m8=$(keyval slot_pre_m8)
      seam_m4=$(keyval slot_pre_m4)
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
      # **And whether this arm ENDS THE RUN at the seam - which is what decides what the pair's three
      # cells below are allowed to say.** Every one of those sentences is about a `pop {fp, pc}` that ran
      # with the word the pair describes, and on 678's arm the pop does not run at all: the seam publishes
      # the pair and then ends the run on purpose through the entry image's own reset path, so the exit
      # this project has been reading a death at is never reached. Printing them there would assert a code
      # path the log cannot contain, from a log whose whole point is that it exists **without** one.
      #
      # The key is read rather than inferred from the arm, for the same reason `seam_op` is: the same
      # `SEAM_POC=1` body is behind two arms now, and the two need opposite readings of one pair. Its
      # absence has **two** causes and this log cannot tell them apart - which is the shape 677's
      # `selftest_tick_us` clause was written for, and it is reported the same way, as a fact about which
      # image ran rather than as a zero.
      if [[ $seam_end_run == "0x00000001" ]]; then
        say "  xnu_live_seam_end_run=$seam_end_run  ARM 678: this arm PUBLISHES the pair above and then"
        say "  ends the run on purpose - RESTART_REASON, dsb sy, PSHOLD<-0, dsb sy, for(;;) wfe, which is"
        say "  the entry image's own reset path and the one reset every returning run of this project has"
        say "  measured. **So the exit never returns and the pop never runs**: the pair above is the whole"
        say "  reading, and this log's existence says nothing at all about the pop."
      elif [[ $seam_end_run == "0x00000000" ]]; then
        say "  xnu_live_seam_end_run=$seam_end_run  the seam RETURNS: the pop runs, and the pair is read"
        say "  against its death - which is how the 535 and 572 cells below were written."
      else
        say "  xnu_live_seam_end_run=${seam_end_run:-absent}  UNREAD - and it has two causes this log"
        say "        cannot tell apart: the image predates 678's ending (653's arm and every earlier one),"
        say "        or the machine did not survive to the publish. **A pair above settles which**: the"
        say "        first call always publishes when the live channel is up (entry_seam_publish's"
        say "        schedule starts at 1 and entry_seam_flush ends its body on the same call), so a pair"
        say "        with no ending key is the first cause and is a fact about the image - while"
        say "        seam_calls>=1 with NO pair at all is the second, and is the arm's own reading."
      fi
      # **What those four words were read *with* is a value in the log, so it is read rather than
      # asserted.** The arm takes `b0`/`b1` before its `bl FlushPoU_Dcache` and `a0`/`a1` after it, and
      # nothing inside the seam writes `SCTLR` - so the one `SCTLR` the arm captures is `C`'s value for
      # all four reads. Apple puts `C` back to 1 later, at `0x8004631c`-`0x80046328`, after the arm has
      # returned. `C = 0` is what makes the four words readings of DRAM; `C = 1` would make them readings
      # *through* the caches, which is a much weaker statement and one this block was not written for -
      # a cache hit would report the line's contents, so the arm could no longer tell a stale line from a
      # written-back one, and the pair below would be a reading of the cache rather than of the frame.
      if [[ $seam_sctlr =~ ^0x[0-9a-f]+$ ]]; then
        if (( (seam_sctlr >> 2) & 1 )); then
          say "  FINDING  seam_sctlr=$seam_sctlr has C=1 at the seam, so the four words the arm read were"
          say "        read *through* the caches and not out of memory - a weaker statement than this block"
          say "        is built on: a hit reports the line's contents, so a stale line and a correct memory"
          say "        word are the same reading here. 546 section 1's premise for this cell is that the"
          say "        seam runs with C=0, so if this line fires *that premise* is the finding, and the pair"
          say "        below is a reading of the cache rather than of the exit's frame"
          # Two claims, two labels: the fact above is a FINDING, and what it costs is that the four
          # words below are not evidence - which is this file's UNREAD, and the one thing that must not
          # be left to the reader to notice, since every comparison below still runs and still prints.
          say "  UNREAD  and that makes the four words above, and the comparisons made against them below,"
          say "        unreadable as evidence about the frame: this log does not establish what the exit's"
          say "        push left in memory, which is the only thing this block is for"
          verdict_ok=0
        else
          say "  PASS  seam_sctlr=$seam_sctlr has C=0 at the seam, so the four words above were read out"
          say "        of DRAM and not through the caches - which is what makes them readings of the frame."
          say "        **It is not a statement about the pop**: the pop at 0x8004633c runs after"
          say "        platform_cache_idle_exit puts C back to 1 at 0x8004631c, so a stale L1/L2 line can"
          say "        still answer that lookup, and this key cannot see it. A wrong b1 below refutes the"
          say "        frame; a right one does not clear the pop"
        fi
      else
        say "  UNREAD  xnu_live_seam_sctlr is ${seam_sctlr:-absent}, so whether the four words above were"
        say "        read with the caches off is not established here: they are still two pairs, but what"
        say "        they were read out of is this log's claim to make and it does not make it"
        # The frozen 574 arm does publish this key (the string is in its own ELF, and 586's key list
        # was read off the arm), so this branch is a guard and not the expected state - which is the
        # reason it is `verdict_ok=0` too: a log that reaches here cannot say what the push left in
        # memory, and that is the same cost as the C=1 case above.
        verdict_ok=0
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
        if (( seam_sp + 8 == sleh_sp )); then
          say "  PASS  seam_sp=$seam_sp is the exit's frame slot: it is sleh_sp-8, and sleh_sp is that"
          say "        same sp after the pop freed the 8 bytes the push wrote - so the address the arm read"
          say "        is the one the pop reads, which is 546 section 1's slot"
        else
          say "  FAIL  seam_sp=$seam_sp is not sleh_sp-8 (the abort's sp is $sleh_sp, so the slot is"
          say "        $(printf '0x%08x' $(( sleh_sp - 8 )))): the arm's address and the pop's are different words, and"
          say "        nothing below this line joins up"
          verdict_ok=0
        fi
      else
        say "  UNREAD  ${seam_sp:-seam_sp} against ${sleh_sp:-sleh_sp}: one of the two addresses is"
        say "          absent, so whether the arm read the pop's own slot is not established here"
      fi
      # The exit wrapper's own record of the same address, checked as its own relation rather than
      # folded into the one above: two independent publishers of one value are what makes a
      # disagreement readable, and folding them would let one silent key hide the other.
      if [[ -n $slot_pre_sp && -n $seam_sp ]]; then
        if (( seam_sp + 8 == slot_pre_sp )); then
          say "  PASS  and the exit wrapper's own capture agrees: slot_pre_sp=$slot_pre_sp is the sp the"
          say "        real exit was entered with, one push above the slot"
        else
          say "  FAIL  slot_pre_sp=$slot_pre_sp should be seam_sp+8=$(printf '0x%08x' $(( seam_sp + 8 ))) - the wrapper's"
          say "        capture and the arm's slot are two records of one frame and they disagree"
          verdict_ok=0
        fi
      elif [[ $pre_calls =~ ^0x[0-9a-f]+$ ]]; then
        # **This arm cannot publish these words at all, and saying nothing here is the defect.**
        # `STAGE90_XNU_SLOT_NULL=1` - the frozen 574 arm, and 533's and 535's, all three read out of
        # their own config files - makes the wrapper call `entry_slot_null_note`, whose whole point is
        # that it publishes the call count and *not* `sp` or the four words (entry_stubs.c: "What it
        # deliberately does not do is publish the four words, or `sp`"). So the comparison above is
        # not run on the coming boot, and the arm's own disassembly says so too: at 0x8047c978 the
        # wrapper calls `<entry_slot_null_note>`, not `<entry_slot_note>`. An absent comparison in a
        # block where every neighbouring absence prints a line reads exactly like agreement, which is
        # 547's silence rule - and the arm publishes the discriminator that distinguishes the cases:
        # `slot_pre_calls` present with the words absent means the site ran and published no words;
        # both absent means the site did not run.
        say "  UNREAD  xnu_live_slot_pre_calls=$pre_calls is published and xnu_live_slot_pre_sp is not:"
        say "        this arm takes the SLOT_NULL path (STAGE90_XNU_SLOT_NULL=1 - the wrapper calls"
        say "        entry_slot_null_note, which writes the count and no words), so the wrapper's own"
        say "        capture cannot be compared with the seam's sp on this boot. The absence of this"
        say "        line's comparison is a switch in the image, not an agreement"
      else
        say "  UNREAD  neither xnu_live_slot_pre_sp nor xnu_live_slot_pre_calls is in this log, so the"
        say "        wrapper's own capture of the slot did not run on this boot at all - which is a"
        say "        different fact from the arm taking the null path, and the reason the two are"
        say "        separated here is that this one is not explained by a build switch"
      fi
      # **The slot's two words as the *wrapper* read them, just before the push.** `slot_pre_m8`/`_m4`
      # are the words at `sp-8`/`sp-4` at the exit wrapper's entry, i.e. exactly the two addresses the
      # exit's push is about to write - so on a pass through a loop that keeps the same frame they are
      # the previous pass's leftovers, and this arm's `_b0`/`_b1` are this pass's push. Equal is the
      # expected reading and *not* a pass: a difference is the frame having moved between two passes
      # through the same code, which is a fact about the idle loop rather than about this arm.
      # **And `pre_m4` is `b1`'s own address, so the derived return site is a prediction about both** -
      # *on an arm that publishes the words.* The wrapper's capture reads its `sp` after its prologue
      # and at negative offsets (`pre_m4` is `sp-4`), and the real exit is entered with that same `sp` -
      # so `slot+4` and `pre_m4` are one word, read once before the push and once after it. `pre_m4` is
      # therefore the *previous* pass's pushed `lr` on a frame that has not moved, and the two sources
      # turn the old "did the frame move" reading into four distinguishable states: both equal to the
      # derived site (the frame is in memory and did not move), `b1` right and `pre_m4` not (the frame
      # moved), `pre_m4` right and `b1` not (the word was right before the push and the push's store did
      # not leave it there), and neither (the push's store did not put this code's return address in the
      # word the `pop` reads).
      #
      # **The frozen 574 arm is not one of those arms**, which is why the absence below prints a line
      # rather than nothing: `STAGE90_XNU_SLOT_NULL=1` makes the wrapper call `entry_slot_null_note`,
      # which publishes the count and no words. So on the coming boot the *reachable* half of the
      # prediction is `b1` alone, and 583's promise that it "lands in a second key that the older
      # instrument already publishes" holds only of an arm built with SLOT_NULL off - 520's, whose
      # `pre_m4` is that instrument's own pre-521 call frame anyway. Both facts are stated rather than
      # inferred: the config file carries the switch, and the wrapper's own `bl` names the null note.
      #
      # **And this block had the mirror of the defect the `b1` line below records.** Its first version
      # tested the derived comparison first, so with no readable entry ELF `caller_lr` was empty, both
      # derived branches failed, and the *fallback* branch printed a sentence about a comparison that
      # had not happened - one line above the `b1` line's own UNREAD, in the same block, about the same
      # non-derivation. Measured, not reasoned: `OBJDUMP=/nonexistent` on the `both` rehearsal printed
      # `m4=0x8047c990 - equal to b0/b1 when the frame did not move between two passes` and then
      # `UNREAD ... the return site could not be derived`. So the empty-derivation case is tested
      # first here too, and for the same reason: a reading whose comparison did not happen is UNREAD,
      # not a reading.
      if [[ -n $seam_m8 && -n $seam_m4 ]]; then
        if [[ -z $caller_lr ]]; then
          say "  UNREAD  m8=$seam_m8 m4=$seam_m4: the return site could not be derived (no readable"
          say "        entry ELF), so whether this word is the one this pass's push writes is not"
          say "        established here - the b1 line below says the same, for the same reason"
        elif [[ $seam_m4 == "$caller_lr" && $seam_b1 == "$caller_lr" ]]; then
          say "  PASS  and the wrapper's own capture of the same word agrees: pre_m4=$seam_m4 is the"
          say "        derived return site both before and after the push, so the frame is in memory and"
          say "        did not move between two passes (m8=$seam_m8, to be compared with b0=$seam_b0)"
        elif [[ $seam_b1 == "$caller_lr" ]]; then
          say "        the wrapper's capture reads the same word: pre_m4=$seam_m4 against the derived"
          say "        site ${caller_lr}, so the frame MOVED between two passes - the word the push wrote"
          say "        is this image's return site, and the same word read before the push held something"
          say "        else, i.e. the previous pass used a different frame at this address"
        elif [[ $seam_m4 == "$caller_lr" ]]; then
          # The one state the three-way form could not name. `pre_m4` is read *before* this pass's
          # push and holds the derived site; `b1` is read *after* it and does not. So the word at this
          # address was right and the push's store did not leave it there - 546 section 1's premise
          # failing *in the window*, stated from the side that needs no assumption about where the
          # previous pass's frame was, which is what separates it from the branch above.
          say "  READING  pre_m4=$seam_m4 is the derived return site read before the push, while"
          say "        b1=$seam_b1 read after it is not: this word held the right value and the push's"
          say "        store did not leave it there. That is the window, not the loop - see the b1 line"
          say "        below for what the pop would then read"
        else
          say "  (the wrapper's own capture of those two addresses, read before the push: m8=$seam_m8"
          say "   m4=$seam_m4 - equal to b0/b1 when the frame did not move between two passes; and here"
          say "   b1=$seam_b1 is not the derived site ${caller_lr}, so read the b1 line below first)"
        fi
      elif [[ $pre_calls =~ ^0x[0-9a-f]+$ ]]; then
        say "  UNREAD  m8/m4 are the second half of the same absent capture (xnu_live_slot_pre_calls="
        say "        $pre_calls says the wrapper's site ran; SLOT_NULL says it publishes no words), so on"
        say "        this boot b1's address has only the seam's own reading behind it and not the"
        say "        cross-check this block exists to make"
      else
        say "  UNREAD  m8/m4 are absent and xnu_live_slot_pre_calls is too, so the wrapper's capture did"
        say "        not run at all: nothing here is a reading of b1's address from the other side"
      fi
      # **`b1` compared against the one value a correct frame can hold, not against a range.** The
      # frame's second word is the address the real exit returns to, and the wrapper's `bl` fixes it:
      # `${caller_lr:-?}` (derived - see `exit_caller_lr_addr`). The earlier form of this test asked only
      # whether `b1` looked like kernel text, and 520's own log shows what that admits - a stale stack
      # word such as `0x80553520` passes it, as would any other address the idle stack happens to hold,
      # which is exactly the wrong-value case this arm exists to detect. `546 section 1's premise for
      # this cell` is still the premise, but its *value* is corrected here: the return site is in
      # `__wrap_platform_cache_idle_exit` (517's `--wrap`), not in `cpu_idle`, so the derived address is
      # inside the wrapper's own extent rather than in the caller the document names.
      if [[ -n $caller_lr && $seam_b1 == "$caller_lr" ]]; then
        say "  PASS  b1=$seam_b1 is the address the real exit returns to in THIS image (derived from"
        say "        $OUT/xnu_arm_entry.elf), so memory held the frame the exit's push wrote - 546"
        say "        section 1's premise for this cell, with its value corrected: the return site is in"
        say "        __wrap_platform_cache_idle_exit (the --wrap), not in cpu_idle"
        say "        **b1 is the pop's own pc word** (b0=[seam_sp] is the pushed fp, b1=[seam_sp+4] the"
        say "        pushed lr, and pop {fp, pc} loads pc from [seam_sp+4]) - so a wrong b1 refutes the"
        say "        frame, and this PASS does not clear the pop: all four words are read with SCTLR.C=0,"
        say "        before Apple re-enables caching at 0x8004631c, and the pop runs after that with C=1."
        say "        A stale line answering the pop's lookup is outside what this key can see"
      elif [[ -z $caller_lr ]]; then
        # **This test comes before the shape tests, and the first version had it after them**, which
        # made it unreachable: with no decoder `caller_lr` is empty and a `b1` that looks like kernel
        # text fell into the "kernel text but not this image's return site" arm and printed a
        # *comparison* that had not happened - measured, on the very first rehearsal of this branch
        # (`OBJDUMP=/nonexistent`, b1=0x8047c990: the reading printed with an empty comparison
        # address). An UNREAD branch that cannot fire is the defect the three-state convention exists
        # to prevent, so the "could not derive" case is tested first.
        say "  UNREAD  b1=${seam_b1:-absent}: the return site could not be derived (no readable entry"
        say "        ELF), so whether memory held this frame's word is not established here - and the"
        say "        pinned literal that would let this line answer anyway does not exist, on purpose"
      elif [[ $seam_b1 =~ ^0x80[0-9a-f]{6}$ ]] && (( seam_b1 < 0x80600000 )); then
        say "  READING  b1=${seam_b1:-absent} is kernel text but is NOT this image's return site"
        say "        ($caller_lr): memory held *some* kernel address where the push wrote lr, and"
        say "        not the one this code leaves there - so the word the pop reads is the idle stack's"
        say "        own stale content and not this frame's. That is the mechanism, seen from the near"
        say "        side, and it is a finding rather than a failed arm"
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
# **One classification, two readings - and 678 is why there are two.** The three conditions below are the
# only place the pair is turned into a cell, and they stay in one place on purpose: a second copy of them
# under the ending arm would be one value with two definitions, which is this project's most expensive
# defect class, and the two copies would drift on the first edit. What differs between the arms is not the
# classification but **what a cell is allowed to mean**, because on the ending arm no pop ever runs.
        if [[ -n $seam_b0 && $seam_a0 == "$seam_b0" && $seam_a1 == "$seam_b1" ]]; then pair_cell=clean
        elif [[ -n $seam_a1 && -n $rtcpre_pop && $seam_a1 == "$rtcpre_pop" ]]; then pair_cell=stale
        elif [[ -z $seam_b0 && -z $seam_b1 && -z $seam_a0 && -z $seam_a1 ]]; then pair_cell=absent
        else pair_cell=changed
        fi
        if [[ $seam_end_run == "0x00000001" ]]; then
# 678's arm: the same operation, on an arm that ends the run before the exit returns. Each cell keeps the
# FACT 535's arm reports and replaces only the consequence, because the consequence is what a pop makes
# true or false.
          case $pair_cell in
            clean)
          say "  CLEAN LINE  the pair came back unchanged (b=${seam_b0}/${seam_b1}), so the line was not"
          say "        dirty: the clean had nothing to write out. **And on THIS arm that is a reading of"
          say "        the operation alone** - the sentence 535's arm prints after it (a pop that still died"
          say "        on a stale value) is about a code path this log cannot contain, so a clean line here"
          say "        says 546 section 3's mechanism was not exercised and says nothing about the pop"
          say "        either way" ;;
            stale)
          say "  STALE LINE, WRITTEN OUT  a1=$seam_a1 is this same pass's xnu_live_slot_rtcpre_pop,"
          say "        which the instrumentation reads from cpu_data+RTCPOP - the idle loop's own deadline,"
          say "        not a stack word (585 section 3). **And this is the arm's answer**: the operation put"
          say "        the loop's own datum where the frame's word belongs, the pair was published, and the"
          say "        run then ended where this project told it to - so the reading survives a machine that"
          say "        would have died at the pop, which is the whole reason this arm exists (663 section 2)."
          say "        The near-side confirmation 535's arm quotes (the pop reading (rtcpre_pop,"
          say "        rtcpre_pop-1) into (fp, pc)) is a fact about 520's and 533's fatal dumps and not about"
          say "        this log" ;;
            absent)
          say "  NO PAIR, and on THIS arm that is the reading the arm was built to make: xnu_live_seam_calls"
          say "        is ${seam_calls} but none of the four words was published, and the first call always"
          say "        publishes when the live channel is up - so the machine did **not** survive from the"
          say "        seam's entry to its publish. Read it with 663 section 2's row for it: the run was"
          say "        reached, the operation (or the read after it) did not return, and **the pair is lost**."
          say "        What it does NOT say: whether the pop would have died - nothing here ran the pop. The"
          say "        device's return time is the next question, and it belongs to the run and not to this"
          say "        log: a return at the countdown's own interval (25.0 s bark / 28.0 s bite) means the"
          say "        payload's net brought it back rather than the ending, since the ending was never"
          say "        reached. And this is the ONE cell that could not have been read before 678: on the"
          say "        arms that return, an absent pair and a hang are the same silence." ;;
            *)
          say "  CHANGED  the pair came back changed (b=${seam_b0:-?}/${seam_b1:-?} ->"
          say "        a=${seam_a0:-?}/${seam_a1:-?}) and a1 is not this pass's rtcpre_pop=${rtcpre_pop:-absent}:"
          say "        a dirty line, holding something this block does not name. Read it on this arm as the"
          say "        operation's own effect and NOT as a pop that survived it" ;;
          esac
        elif [[ $pair_cell == clean ]]; then
          say "  CLEAN LINE  the pair came back unchanged (b=${seam_b0}/${seam_b1}), so the line was not"
          say "        dirty: the clean had nothing to write out, and a pop that still died on a stale"
          say "        value got it from somewhere this operation does not reach - the falsifier for 546"
          say "        section 3's mechanism rather than its confirmation"
        elif [[ $pair_cell == stale ]]; then
          say "  STALE LINE, WRITTEN OUT  a1=$seam_a1 is this same pass's xnu_live_slot_rtcpre_pop,"
          say "        which the instrumentation reads from cpu_data+RTCPOP - the idle loop's own deadline,"
          say "        not a stack word (585 section 3). What that means: the word the operation wrote back"
          say "        is the value the loop computed, i.e. the operation put the loop's own datum where the"
          say "        frame's word belongs - 546 section 3's mechanism from the near side. 585 measured the"
          say "        same pair in 520's and 533's fatal dumps, where the pop read (rtcpre_pop,"
          say "        rtcpre_pop-1) into (fp, pc) while lr still held the address the push had saved"
        else
          # **This branch also catches `pair_cell=absent`, and that is today's behaviour preserved on
          # purpose** - on an arm that RETURNS, the four keys being absent and the pair being unreadable
          # for another reason were never distinguished here, and a log of that shape is one this block
          # has already been read against. Naming the cell above without changing this arm's text keeps
          # every capture already in `out/stage90/captures/` reading byte for byte as it did. The
          # returning arm's own absent-pair sentence is **owed** - it is a reading this block prints
          # as "a dirty line" from two absent keys, which is the class this project ranks first to
          # suspect, and it is left as found rather than repaired in a step whose subject is the arm
          # that makes it reachable.
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
    else
      # **A KEYLESS LOG IS NOT SILENCE - and this branch is only ever the BASELINE case, which the
      # first draft of this repair got wrong by testing the arm again.** This gate sits inside the
      # `elif` at `:1291`, whose chain's head is `if (( idle_no_sleep_arm == 1 ))`; so reaching here
      # *means* `idle_no_sleep_arm == 0`. A second `if (( idle_no_sleep_arm == 1 ))` in this `else`
      # would be a cell that cannot reach the branch its label names - the defect class this project
      # ranks first to suspect - so the arm is not re-tested and the two causes are named in one
      # sentence instead. Measured, not read: 513's capture (sleeper-shaped) prints **0** lines of
      # clauses (1)-(5) and takes the ladder above, while 520's (baseline-shaped) prints them here.
      say ""
      say "  (5) xnu_live_seam_calls is ABSENT. Two causes, and this reader reaches them by different"
      say "     routes: the window was never reached, or this image predates the instrument. Neither"
      say "     is decidable from this log - and which image is in the machine is the gate's reading"
      say "     and not this one's (clause (6) makes the same distinction about 513's captures)."
      say "     **The third cause is not available here**: a log from the sleeper arm never reaches"
      say "     this clause at all - its keys are absent by construction and it is read by the ladder"
      say "     above - so a log that gets this far is not that arm, and the arm row above says so"
      say "     from the log's own keys rather than from this sentence."
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
          say "     And the cell is read as 533's arm, from $arm_set_key=$arm_set, which is the cell"
          say "     547 section 4's prediction is for: the two agree."
          if [[ $seam_calls =~ ^0x[0-9a-f]+$ ]] && (( seam_calls >= 1 )); then
            say "     The image this log came from is the later one in that cell, though - 535's, whose"
            say "     cache settings are 533's - so clause (5)'s pair is the reading and the pop's"
            say "     survival is 535's question rather than this prediction's"
          fi ;;
        522)
          say "     **But the cell is read as 522's arm ($arm_set_key=$arm_set), which 547 section 4"
          say "     does not predict this for**: 522's cell is an enable-on row and 547 section 3 has both of"
          say "     those rows dying with *no log at all*, so a log here is itself the surprise. Treat"
          say "     this as unread against 547 and re-derive which image ran before reading anything"
          say "     else in this block." ;;
        *)
          say "     Neither publisher of the window's SCTLR is readable in this log, so which cell"
          say "     it is stays open - and 547 section 4's prediction is the enable-off cell's." ;;
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
  else
    # **The state with no arm at all, and until 598 this paragraph described three states while the
    # control flow kept two.** Every branch above is reached by *recognising* a log, so one that
    # matched none was passed over in silence - a reader silent for a reason and a reader silent
    # because it broke read identically from outside (564, and 593 for the same defect one layer up),
    # and 595 gave the state this `else`. Its census was right about the keys and wrong about which
    # logs reach it: **it named 520 as landing here**, and 520 is half of this project's own baseline.
    # 598 moved the `elif` above from `xnu_live_slot_cwe_*`'s presence (533's *instrument*) to
    # `xnu_live_door_seq`'s (the *arm's* signature), so 520 is read by clauses (1)-(5) and this branch
    # is now reached only by a log that never got to `cpu_idle` at all. Measured rather than assumed:
    # 520's capture carries `door_seq`=16 records with `sip_seq`/`pce_seq`/`wfi_seq`/`repair_seq`=1 and
    # `slot_pre_calls`=1 and `xnu_live_slot_cwe_*` **0** times - `xnu_live_slot_cwe_*` has 3 in 533 -
    # so it always satisfied the baseline signature and it was only the gate that kept it out. The
    # census is kept because it is the evidence for this diagnosis either way.
    say "  UNREAD  this log carries payload output and carries no xnu_live_door_seq= record, so no"
    say "          clause above could be read:"
    say "            xnu_live_slot_cwe_ keys:  $(grep -a -c 'xnu_live_slot_cwe_' "$log" || true)"
    say "            xnu_live_door_seq:        $(grep -a -c 'xnu_live_door_seq=' "$log" || true)"
    say "            the window family:        sip=$(grep -a -c 'xnu_live_sip_seq=' "$log" || true)"
    say "          xnu_live_door_seq is published by __wrap_Idle_load_context on the earliest passes of"
    say "          every boot that reaches cpu_idle, so its absence is not a signature this project has"
    say "          read an arm by: it says the boot did not reach the idle at all, which is a fault"
    say "          *earlier* than either arm's and a different question from this block's"
    say "          **And this branch no longer holds a baseline.** Until 598 it did: the clause above"
    say "          was entered on xnu_live_slot_cwe_'s presence - 533's instrument, not the arm's"
    say "          signature - so 520, whose image predates that instrument, satisfied the baseline"
    say "          signature and landed here, under a paragraph that named the three states in prose"
    say "          while the control flow kept two. The lane is now door_seq's presence, so a baseline"
    say "          log is read by clauses (1)-(5) whether or not its image has the pair, and anything"
    say "          that reaches this line has lost the key itself"
    verdict_ok=0
  fi

  # ---------------------------------------------------------------------------------------------
  # The goal's own criterion: did the OS reach user mode, and did a driver answer it?
  # ---------------------------------------------------------------------------------------------
  #
  # **Every clause above scores the arm against the death point. This one scores it against the
  # goal.** 「起码要能进入操作系统，把基础驱动跑起来」 is a statement about a boot that reaches user
  # mode and about a driver answering a call made from it - and this project has had that reading in
  # hand since 504, which is where `entry_ramdisk.s`'s own header records it: the wrapper publishes
  # the word at the read's buffer *either side* of the call, so the pair says the driver moved
  # `MH_MAGIC` into a page whose previous content was the address `mmap` returned, and the control
  # open answers `ENOENT`. What did not exist until now is the reader: a grep for `xnu_live_open` in
  # this file returned **nothing**, so the one reading the goal actually asks for would have sat
  # unread in the same log the operator was handed. The gap was never that the evidence was missing.
  #
  # **It sits outside the arm branch on purpose.** The userland phase precedes the pop, so these keys
  # are published by both arms and by every archived baseline; a block inside a branch would be silent
  # for exactly the logs most likely to matter. That is 564's defect and 594's missing `else` one
  # layer out, and the cheap way not to repeat it is not to gate it.
  #
  # **And what it prints is a FLOOR, not progress.** All of it is in 520 and 533 - the whole userland
  # phase happens before the death - so a run has to *lose* these readings to be a regression, and
  # having them is not evidence that the boot got further than any boot has. The sentence that has to
  # survive into the report is the one naming the ceiling: what nobody has yet observed is the
  # machine *staying* up.
  # **And the one state this block does not run in, named because it was found by looking for it.**
  # Run against a log with **zero** `MI4IOS6_STAGE90` lines, the goal block prints nothing - because
  # `summarise_log` returned above, at the `n -eq 0` test, with "NONE. The log has no payload output
  # from this run". That is the one silence here that is deliberate and named: with no payload output
  # at all there are no readings, goal included, and a table of `absent`s would be a worse answer than
  # the sentence already on screen. Every log that carries payload output reaches this block, which is
  # why it is not gated on anything - and `syn-F` (survived, truncated before userland) is the state
  # that proves the difference: it has one payload line and the block prints its UNREAD there.
  local g_open_n g_read_n g_getpid_n g_exit_n g_wait_n g_ast_n
  local g_open_err g_open1 g_open2 g_read_ret g_read_nb g_read_before g_read_after g_read_buf
  local g_getpid_val g_exit_pid g_exit_rval g_wait_done g_wait_status g_wait_err
  local g_magic="" g_driver=0 g_pair=0
  g_open_n=$(ordered open_seq | grep -c . || true)
  g_read_n=$(ordered read_seq | grep -c . || true)
  g_getpid_n=$(ordered getpid_seq | grep -c . || true)
  g_exit_n=$(ordered exit_seq | grep -c . || true)
  g_wait_n=$(ordered wait_seq | grep -c . || true)
  g_ast_n=$(ordered ast_seq | grep -c . || true)
  # The pair, kept apart: the *first* open is the driver's, the second is 504's control, and a reader
  # that takes one of them answers about the control while claiming to answer about the driver.
  g_open_err=$(ordered open_error | tr '\n' ' ' || true)
  g_open1=$(ordered open_error | sed -n '1p' || true)
  g_open2=$(ordered open_error | sed -n '2p' || true)
  g_read_ret=$(ordered read_ret_lo | sed -n '1p' || true)
  g_read_nb=$(ordered read_nbytes | sed -n '1p' || true)
  g_read_before=$(ordered read_word_before | sed -n '1p' || true)
  g_read_after=$(ordered read_word_after | sed -n '1p' || true)
  g_read_buf=$(ordered read_buf | sed -n '1p' || true)
  g_getpid_val=$(ordered getpid_value | sed -n '1p' || true)
  g_exit_pid=$(ordered exit_pid | sed -n '1p' || true)
  g_exit_rval=$(ordered exit_rval | sed -n '1p' || true)
  g_wait_done=$(maxhex wait_done_seq)
  g_wait_status=$(ordered wait_status | sed -n '1p' || true)
  g_wait_err=$(ordered wait_error | sed -n '1p' || true)
  # The magic comes out of the fixture rather than being written here again - the same rule rung 1b
  # follows for `ENTRY_PARK_MIN_MS`. `entry_note_read`'s pair is only a reading *against* a known
  # value, and a second copy of that value in this file is the defect this project has paid for most
  # often. Unreadable means the comparison cannot be made, which is printed as such.
  if [[ -r $REPO_ROOT/stages/stage90/xnu_arm_boot/entry_ramdisk.s ]]; then
    g_magic=$(sed -n 's/^[[:space:]]*\.equ[[:space:]]\+MH_MAGIC,[[:space:]]*\(0x[0-9a-fA-F]*\).*/\1/p' \
               "$REPO_ROOT/stages/stage90/xnu_arm_boot/entry_ramdisk.s" | head -1 || true)
  fi
  if [[ -n $g_open1 && -n $g_open2 ]] \
     && (( 16#${g_open1#0x} == 0 )) && (( 16#${g_open2#0x} != 0 )); then
    g_pair=1
  fi
  if (( g_pair == 1 )) && [[ -n $g_read_ret && -n $g_read_nb ]] \
     && (( 16#${g_read_ret#0x} == 16#${g_read_nb#0x} )) \
     && [[ -n $g_read_after && -n $g_magic ]] \
     && (( 16#${g_read_after#0x} == 16#${g_magic#0x} )); then
    g_driver=1
  fi

  say ""
  say "  ---- the goal's own criterion: user mode reached, and a driver answering ----"
  say "    open    ${g_open_n} call(s), error in call order: ${g_open_err:-absent}"
  say "    read    ${g_read_n} call(s), ret_lo=${g_read_ret:-absent} of nbytes=${g_read_nb:-absent};"
  say "            buffer ${g_read_buf:-absent} held ${g_read_before:-absent} when the call was made"
  say "            and ${g_read_after:-absent} after it (the fixture's MH_MAGIC is ${g_magic:-unread})"
  say "    getpid  ${g_getpid_n} call(s), value ${g_getpid_val:-absent}"
  say "    exit    ${g_exit_n} call(s), pid ${g_exit_pid:-absent}, rval ${g_exit_rval:-absent}"
  say "    wait    ${g_wait_n} call(s), reaped at done_seq ${g_wait_done}, status"
  say "            ${g_wait_status:-absent}, error ${g_wait_err:-absent}"
  say "    ast     ${g_ast_n} record(s) - the kernel taking a user thread's AST and putting it back"

  if (( g_driver == 1 )); then
    say "  PASS  and this is 504's reading re-read here, in the log of a boot that got this far: the"
    say "        first open was answered by a driver (error ${g_open1}) while 504's control open was"
    say "        not (error ${g_open2}, the ENOENT a name devfs has no node for returns), and the"
    say "        read came back with ${g_read_ret} byte(s) whose first word is now ${g_read_after},"
    say "        the fixture's own magic - so a character device's read moved data into a user page."
    say "        **And it is the floor, not this run's progress**: all of it is in 520 and 533 too,"
    say "        because the whole userland phase happens before the death. Losing it would be a"
    say "        regression; having it says only that the OS still boots to pid 1's syscalls."
  elif (( g_open_n > 0 || g_read_n > 0 || g_getpid_n > 0 )); then
    # **This FAIL had three named causes and the state that matters was a fourth, which is why the
    # sequence is read before the values.** The three below are all *wrong values*: the driver's open
    # answered non-zero, the control answered 0 too, or the read's word did not change. But the
    # fixture makes its calls in a fixed order - open(driver), open(control), read, getpid, exit,
    # wait - so a log whose records simply **stop** is a boot that stopped there. That is a
    # *position*, and on an arm whose whole purpose is to find where the boot stops, it is the
    # reading the run exists to produce.
    #
    # **It was found by building the state, not by reading the branch.** With the log ending after
    # the driver's open (`xnu_live_open_seq=0x00000001`, `_error=0x00000000`, nothing further), the
    # block printed "the driver's open did not answer 0" one line under `open 1 call(s), error in
    # call order: 0x00000000` and two lines above `read 0 call(s)`: **all three named faults were
    # contradicted by the block's own numbers**, in the single reading 596 added because the goal
    # asks for it. A FAIL that names a fault its own table refutes is worse than no FAIL, because
    # the operator's next action is decided by the sentence and not by the table.
    #
    # So: presence first, values second. `g_stop` names the first call in the fixture's own sequence
    # whose record is not in the log, and it is built from `-z` tests and counts only - no arithmetic
    # on a value that may not be a number.
    local g_stop=""
    if   [[ -z $g_open1 ]]; then g_stop="the driver's open"
    elif [[ -z $g_open2 ]]; then g_stop="the control open"
    elif [[ -z $g_read_ret || -z $g_read_nb || -z $g_read_after ]]; then g_stop="the read"
    elif [[ -z $g_getpid_val ]]; then g_stop="getpid"
    elif [[ -z $g_exit_pid ]];   then g_stop="the child's exit"
    elif [[ -z $g_wait_status ]]; then g_stop="the wait"
    fi
    if [[ -n $g_stop ]]; then
      if (( live_trunc == 1 )); then
        # **609: the "POSITION" reading is a claim about the boot, and a full channel breaks it.** The
        # fixture's keys are published by the calls themselves, so an absent one normally means the
        # call did not happen - which is the position this project wants. On a truncated log the same
        # absence can be a note that was written and dropped at the cap, and the two are not
        # distinguishable from here. The branch below has always stated this caveat for a log with *no*
        # fixture record at all ("the same absence is what a log truncated before userland looks
        # like"); what 609 adds is that the same caveat applies to a log with *some* records, because
        # the truncation can fall in the middle of the fixture's sequence rather than before it.
        say "  UNREAD  and the fixture's sequence has no record of ${g_stop} - and this log's live"
        say "        channel is TRUNCATED, so the absence is not a position: this log has ${g_open_n}"
        say "        open(s), ${g_read_n} read(s), ${g_getpid_n} getpid, ${g_exit_n} exit, ${g_wait_n}"
        say "        wait record(s), and the channel stopped publishing at the cap, so the call after"
        say "        the last one recorded may have run with its note dropped. **That is not the same"
        say "        as the driver failing** - the values that ARE here are the ones printed above and"
        say "        they are not in dispute - and it is not the position either: it says where the"
        say "        recording stopped, not where the boot stopped"
      else
        say "  FAIL  and the fixture's sequence has no record of ${g_stop}: this log has"
        say "        ${g_open_n} open(s), ${g_read_n} read(s), ${g_getpid_n} getpid, ${g_exit_n} exit,"
        say "        ${g_wait_n} wait record(s), and it makes those calls in that order - so **what is"
        say "        missing here is a POSITION and not a driver fault**: the boot stopped before that"
        say "        call - on this arm that is the reading the run exists to produce. Read the arm's"
        say "        own clause above for where it was, and do not read this as the driver failing: the"
        say "        values that ARE here are the ones printed above, and they are not in dispute"
      fi
    else
      say "  FAIL  and every one of the fixture's calls is in the log, so the fault is in the values"
      say "        and not in the sequence: the driver's open answered ${g_open1} (must be 0), the"
      say "        control answered ${g_open2} (must not be 0), and the read returned"
      say "        ${g_read_ret:-absent} of ${g_read_nb:-absent} byte(s), leaving"
      say "        ${g_read_after:-absent} where the fixture's magic is ${g_magic:-unread} - 504 names"
      say "        these three cases and they are not the same fault"
    fi
  else
    say "  UNREAD  and none of the fixture's own syscalls is in this log, so the goal's first half"
    say "        is not read here at all. It is not a FAIL: the same absence is what a log truncated"
    say "        before userland looks like, and the keys are published unconditionally for the first"
    say "        four calls of each - so check the AST record and the OS console text (the boot's own"
    say "        exec of /sbin/launchd) before reading this as a boot that never got to user mode"
  fi
  say "  ---- and this criterion does not decide whether the machine stayed up. Every reading ----"
  say "  ---- above is a floor that 520 and 533 also meet; the ceiling is the arm's own clause, ----"
  say "  ---- and the run that matters is the one whose log has both. ---------------------------"
}

# The port the phone is on, resolved once and only when this file is going to touch the device: it
# needs `sudo dmesg`, and the summarise path must not. UNREAD when the log cannot supply it, which is
# the same state as an unreadable host log and is treated as such further down.
PHONE_PORT=""
if [[ -z $SUMMARISE_ONLY ]]; then
  PHONE_PORT=$(phone_port)
fi

if [[ -n $SUMMARISE_ONLY ]]; then
  step "summarising $SUMMARISE_ONLY"
  # The refusal names the absolute path it looked at **and** the directory it was resolved against, so a
  # reader who is told "no such log" can see which file that claim is about. Before 615 this line printed
  # the caller's own string, so `no such log: out/stage90/captures/533-....txt` was a sentence about a
  # 597 KB file that was sitting right there - a refusal that named the artifact for a fact about `$PWD`.
  [[ -f $SUMMARISE_ONLY ]] || die "no such log: $SUMMARISE_ONLY (looked for that path, resolved against ${INVOKE_PWD}; '$SUMMARISE_ONLY' is not a readable regular file)"
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

# --- 0. WHAT ARM IS THIS PRESS FOR, pinned before the gate reads anything ----------------------
#
# 654 section 6's open item (R4), and the last of the three repairs 660 named. The exposure it closes
# has two halves and they are different:
#
#   * **the wrong arm, consistently built.** `out/` holds a complete, self-consistent arm - image,
#     entry bin, config and manifest all agreeing - and it is not the arm this press was
#     pre-registered for. Every clause in the gate passes. Readiness row 4 names the arm *of the
#     bytes*, which is the right answer to a different question: it says what WOULD be sent, never
#     whether that is what was meant. On 2026-09-25 the armed launcher on this host was found pinned
#     to a superseded arm (`--set armed-seam-poc-a43304f2` against a live `ef0361a2...`) with a
#     `--allow-xnu-entry`-only flag line the gate refuses for the owed arm - three stale things in the
#     one file whose job is to spend the press, and nothing in this runner could see any of them.
#   * **the right arm, moved mid-window.** The gate is not atomic: it reads its five files in sequence,
#     and the device checks below can wait up to `FB_AMBIG_WAIT` seconds afterwards. A build landing in
#     that window changes the bytes between the gate's verdict and the send, and the gate's verdict
#     would be quoted for an image it never read.
#
# So this step does three things, and each is a *reading* rather than a convention:
#
#   1. **resolve what the bytes ARE**, from `revert-set.txt` - the recorded sets, matched by the
#      `stage90-qcdt.img` hash. Zero matches and two-or-more are both refusals that name what they
#      found, because the name has to come from a record that can be checked and the record's `role=`
#      sentences cannot supply it (three of them claim to be "the arm the next press sends", 667).
#   2. **require the caller to say which arm the press is FOR** (`--expect-arm`), and refuse unless the
#      two are the same string. Not a warning: a press is one shot and its bytes are the instrument the
#      reading is taken with, so an undeclared arm is a press whose result cannot be attributed.
#   3. **pin the image's hash** and re-check it immediately before the send and once after it, so
#      "what the gate read" and "what was sent" are joined rather than assumed equal.
#
# The pin is the *image* and nothing else, and that is the whole set on purpose: `stage90-qcdt.img` is
# the one file `fastboot boot` sends, so any rebuild that could change what this press does changes its
# hash, and a rebuild that does not change its hash did not change what this press does. Widening the
# pin to the other four files the gate reads would be a check that cannot fail for the reason that
# matters, which is the shape this project keeps paying for.
step "the arm this press is for"
IMAGE_SHA_PIN=""; IMAGE_BYTES_PIN=""; SET_ARM=""
REVERT_RECORD=$STAGE_DIR/revert-set.txt
# **The resolution is a tool and not a block here, and that is the one-definition rule applied to the
# thing this file most needs to get right.** `tools/resolve_arm_set.sh` is the single place that answers
# "which recorded set are these bytes" - it is what `tools/verify_press_ready.sh` calls and what
# `tools/rehearse_live_path.sh` calls to build the `--expect-arm` below. The first draft of this step had
# its own scan plus its own copy of the record's `set= ./ sha256= ./ file=` parser, which is how 667's
# readiness repair and this one would have drifted apart in opposite directions with both of them green.
if [[ ! -x $REPO_ROOT/tools/resolve_arm_set.sh ]]; then
  die "$REPO_ROOT/tools/resolve_arm_set.sh is not executable, so which arm the bytes in $OUT are cannot be resolved - and a run that cannot name the arm it sends is the run this check exists to stop. Nothing was sent"
fi
_resolved=""; _resolve_why=""
if _out=$("$REPO_ROOT/tools/resolve_arm_set.sh" "$OUT" 2>&1); then
  _resolved=$(printf '%s' "$_out" | head -1)
  SET_ARM=$(printf '%s' "$_resolved" | cut -f1)
  IMAGE_SHA_PIN=$(printf '%s' "$_resolved" | cut -f2)
  IMAGE_BYTES_PIN=$(printf '%s' "$_resolved" | cut -f3)
else
  _resolve_why=$_out
fi

if [[ -z $SET_ARM ]]; then
  # The refusal is the resolver's own sentence, quoted rather than paraphrased: a second wording of one
  # finding is a second definition of it, and the resolver's version names the hash it looked for and
  # the sets the record does have.
  say "REFUSING: this press has no name for what it would send."
  printf '%s\n' "$_resolve_why" | sed 's/^/          /'
  die "the arm of the bytes in $OUT could not be resolved, so nothing was sent. A rebuild rewrites image, config and manifest together, so the gate would accept such a tree - this is the check that does not (654 section 6, 660)"
fi

say "the bytes to be sent are the recorded set '$SET_ARM'"
say "  image  $IMAGE_SHA_PIN  ($IMAGE_BYTES_PIN bytes, the file \`fastboot boot\` sends)"

if [[ -z $EXPECT_ARM ]]; then
  if (( DRY_RUN == 1 )); then
    say "note: --expect-arm was not passed, so a LIVE run would refuse here. The bytes above are"
    say "      '$SET_ARM', and that is what --expect-arm=$SET_ARM would declare."
  else
    die "--expect-arm=<set> was not given, and this run would SEND an image. The press is one shot and the bytes it sends ARE the instrument its reading is taken with, so a run that does not say which arm it is for cannot be attributed afterwards. The bytes in $OUT are '$SET_ARM' (resolved from their hash just above) - if that is the arm this press was pre-registered for, re-run with --expect-arm=$SET_ARM. There is deliberately no default: a default is a name nobody typed, and a name nobody typed is how the wrong arm gets pressed"
  fi
elif [[ $EXPECT_ARM != "$SET_ARM" ]]; then
  say "REFUSING: this press was declared for '$EXPECT_ARM' and the bytes in $OUT are '$SET_ARM'."
  die "--expect-arm=$EXPECT_ARM does not match the arm in $OUT ($IMAGE_SHA_PIN, set '$SET_ARM'). Nothing was sent. This is the check separate launchers could not make: a stale caller names an arm that has been superseded, and the only symptom used to be a run that booted the wrong one"
else
  say "ok: declared arm '$EXPECT_ARM' == the recorded set of the bytes to be sent"
fi

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
  # **Four questions, in an order with no unreachable branch** - the 616 lesson, applied to a different
  # predicate. (1) adb lists it AND its state is one adbd can be commanded through -> the adb path. (2)
  # fastboot lists it -> the fastboot path. (3) adb lists it in a state adbd cannot be commanded through
  # AND fastboot does not have it -> a message that **names the state and says what to do about it**.
  # (4) neither -> the dark-device message. Every one of the four is reachable, and (3) is the one 619
  # adds: before it, that state fell into (1) and died sixty seconds later with a message about fastboot.
  #
  # **fastboot is asked before the bad-state message, and that order is deliberate.** It used to be that
  # a serial cannot be in adb and fastboot at once, so asking adb first could not mask a fastboot listing
  # - but "cannot" here rests on the two modes being exclusive, which is a fact about USB and not about
  # this script, and a branch that only survives because of a fact it does not check is the shape this
  # file keeps paying for. Asking fastboot second costs one command and removes the assumption.
  ADB_STATE=$(adb_state)
  if [[ $ADB_STATE == device || $ADB_STATE == recovery ]]; then
    MODE=adb
  elif sudo fastboot devices 2>/dev/null | grep -q "^$SERIAL"; then
    MODE=fastboot
  elif [[ -n $ADB_STATE ]]; then
    die "serial $SERIAL is listed by adb, but its state is '$ADB_STATE' - not 'device' or
       'recovery' - so adb cannot be commanded through it, and fastboot does not list it
       either. **Nothing was sent.** If the state is 'unauthorized', the host's key is not
       accepted on the device: accept the RSA prompt on its screen, then re-run. If it is
       'offline', adbd is wedged: wait, or power press. If it is 'no permissions', this
       host is missing its udev rule. If it is 'sideload', the device is in adb sideload."
  else
    die "serial $SERIAL not found in adb or fastboot. If the device is dark, it needs a
       power press: hold Power ~10-15 s, release, then press Power normally."
  fi
  say "found $SERIAL in $MODE (adb state: ${ADB_STATE:-unlisted})"
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
# **`$PREV_LOG` being empty has THREE producers, and until 588 every consumer named one of them.**
# The name is empty when there was no file to park, when the park failed, and under `--dry-run`. Step 2b
# says which out loud, but the two messages further down (the wait's, and the capture's) tested
# `-n $PREV_LOG` and so read all three as "the park failed", printing *"$LOGFILE is untouched, so it is
# still the run before this one's"* about a file that does not exist. Measured, not reasoned: the first
# end-to-end rehearsal of the success path ran against an absent `$LOGFILE` - which is the real
# `/tmp/cancro-last_kmsg.txt`'s state as this is written - and its exit-3 message asserted a previous log
# where step 2b had said "no log at ... yet". That matters because it is the one sentence that tells the
# operator what is on disk in the state where the log is still in DRAM and a power press is about to
# destroy it, which is why 562 and 564 exist. So the state is named once, here, and every consumer reads
# *it* rather than inferring from the empty string.
PARK=unknown
if [[ $DRY_RUN -eq 1 ]]; then
  PARK=dry
  say "would move $LOGFILE (the previous run's log) aside, so that the name holds this run's or nothing"
elif [[ -e $LOGFILE ]]; then
  PREV_LOG=$LOGFILE.prev
  _n=2
  while [[ -e $PREV_LOG ]]; do PREV_LOG=$LOGFILE.prev.$_n; _n=$(( _n + 1 )); done
  if mv "$LOGFILE" "$PREV_LOG" 2>/dev/null; then
    PARK=parked
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
    PARK=nopark
    say "WARNING: could not move $LOGFILE aside (a sticky /tmp and an identity that does not own it:"
    say "         511, 512). It will be REPLACED by this run's capture if the capture works, and it"
    say "         will look untouched if it does not - so in that case compare its sha256 against the"
    say "         one the gate printed above before reading any bracket out of it."
  fi
else
  PARK=nofile
  say "no log at $LOGFILE yet - so after this run, the name existing at all is the first check"
fi

# --- 3. boot the payload ----------------------------------------------------------------
step "boot"
# **The plan line is selected by the mode, and it was not.** It read `sudo adb -s $SERIAL reboot
# bootloader   # then fastboot boot, never flash` unconditionally, and printed it in the dry run (where
# no mode is read) and on the fastboot path - which is where the line is *false*: with the device
# already in fastboot the `adb` call is skipped entirely (see the `MODE == adb` test below), so the
# operator reads a command out of this run's own record that this run never issued. The press this
# phase is waiting on always takes that path - Vol-Down + Power lands in fastboot and adb lists
# nothing - so it is the *normal* case and not the corner. 520's rule, one layer out: a `say` line
# describing an action the run does not take is the same defect as one describing an image that does
# not exist. `${MODE:-}` because the dry run never sets it and `set -u` is on.
case ${MODE:-} in
  fastboot)
    say "fastboot boot -s $SERIAL $IMAGE"
    say "  (the device is already in fastboot, so this run issues no adb command at all)" ;;
  *)
    say "sudo adb -s $SERIAL reboot bootloader   # then fastboot boot, never flash" ;;
esac
if [[ $DRY_RUN -eq 0 ]]; then
  if [[ $MODE == adb ]]; then
    # **The `||` is not decoration: without it, a failing `adb` aborts the script under `set -e` and the
    # exit code the operator sees is adb's own**, for which this file's contract has no clause - the
    # defect `mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one` records. The state check
    # above means this should now be unreachable from `unauthorized`/`offline`, but "should be
    # unreachable" is what the check is, not a proof about adb, and a run that ends on a foreign status
    # says nothing about the device. `die` gives exit 1 with a sentence, which is what the contract
    # documents for a host-side failure.
    sudo adb -s "$SERIAL" reboot bootloader \
      || die "adb could not command $SERIAL (state '$ADB_STATE') to reboot into the bootloader.
       **Nothing was booted.** The device is where it was; re-run, or check \`sudo adb devices\`."
    for _ in $(seq 1 30); do
      sudo fastboot devices 2>/dev/null | grep -q "^$SERIAL" && break
      sleep 2
    done
  fi
  sudo fastboot devices 2>/dev/null | grep -q "^$SERIAL" \
    || die "device did not appear in fastboot"

  # --- the ambiguity guard, and it is here because the LINE BELOW ACTS ----------------------
  #
  # Until 616 every device-touching call in this file pinned the serial EXCEPT the one that
  # actually acts: the two `fastboot devices` checks above pin it, `adb -s "$SERIAL"` pins it, and
  # `fastboot boot` was bare. `fastboot` with more than one device listed does **not** refuse -
  # it picks one - so a second phone in fastboot at the same moment redirects the boot to that
  # phone, and this run then waits for a `usb 3-10` / `4a2fe00b` return that cannot come: a
  # **false non-return, on the one press this phase gets**.
  #
  # This is not hypothetical on this host, and it is measured rather than argued: `usb 3-3`
  # carries a second phone-class device (serial `33e80afe…`, 218 x `18d1:d001`, 12 x `18d1:d00d`
  # in the host log) that enters **fastboot** repeatedly. The Mi 4 has never appeared on 3-3
  # (1530 records of `4a2fe00b`, all on 3-10), so the two are separable - but only if the call
  # says which one it means.
  #
  # So the pin and the guard are both here: the guard refuses an ambiguous list instead of
  # letting `fastboot` choose, and `-s` makes the choice explicit even when the list is
  # unambiguous. A guard that only ran on the happy path would be the 613 shape, so it counts
  # the list rather than asserting non-emptiness.
  # **The count is the whole guard, and it is deliberately the only branch.** A first version also
  # refused when the single listed device was not `$SERIAL` - and that branch is UNREACHABLE: the
  # `grep -q "^$SERIAL"` two lines above already refuses that list, with `device did not appear in
  # fastboot`. The rehearsal's own `one-device-wrong-serial` state is what showed it: the cell
  # asserting the new message failed, because the older check had already fired. A branch nothing can
  # reach, carrying a comment that says what it does, is the defect this project has paid for most
  # (598's three-way test over a two-branch flow); the wrong-serial case is covered by the check that
  # was already there, and this one adds only what was missing.
  #
  # --- 618: the condition is stronger, and the guard waits before it refuses -----------------
  #
  # Both are changes to what 616 wrote, and both come from measurement rather than from a reading of
  # the code.
  #
  # *The condition* is now "exactly one device, and it is `$SERIAL`". At the moment this line is
  # entered the two are still equivalent - the presence check above has established that `$SERIAL` is
  # in the list, so "exactly one" implies "the one is `$SERIAL`" - and that equivalence is exactly why
  # the separate wrong-serial branch was unreachable. What breaks it is the wait below, which is new: a
  # wait can lose this phone and keep the stranger, and a bare count test would then read `1` and pass.
  # That case is reachable *only* through the wait, so it is folded into the one condition rather than
  # added as a second branch - one test, entered twice, and no branch that nothing can reach.
  #
  # *The wait* is here because the state this guard refuses is **usually transient on this host**.
  # Measured out of the host log, the second phone-class device's 13 fastboot windows are 12.9, 1.6,
  # 13.0, 3.3, 3.2, 3.1, 1946, 3.1, 11.9, 1.9, 9.8, 152.9 s, and one that began 2026-09-23 10:59:41
  # and was still open 47 min later. **Eleven of the thirteen are under 14 s.** So the guard as 616
  # wrote it refuses on the first look at a state that clears itself in seconds, and the cost of that
  # refusal is not zero to the operator: they have to notice it, work out which of the two devices
  # moved, and re-run. Waiting cannot turn a refusal into a wrong boot - the list is re-tested after
  # the wait by the same test, and a list that never settles is refused exactly as before - and it
  # cannot cost anything else either, because the phone is already in fastboot and nothing has been
  # sent: a `fastboot boot` that is never reached writes nothing.
  FB_LIST=$(sudo fastboot devices 2>/dev/null || true)
  if ! fastboot_pinned_only "$FB_LIST"; then
    say "note: \`fastboot devices\` does not list $SERIAL alone - it lists $(fastboot_list_count "$FB_LIST") device(s):"
    printf '%s\n' "$FB_LIST" | sed 's/^/            /'
    say "      waiting up to ${FB_AMBIG_WAIT}s for the list to settle; the other device on this host"
    say "      is usually in fastboot for seconds, but has also sat there for tens of minutes"
    _fb_t0=$(date +%s)
    _fb_deadline=$(( _fb_t0 + FB_AMBIG_WAIT ))
    while ! fastboot_pinned_only "$FB_LIST"; do
      # The same `test && break` shape the reboot loop above uses, and for the same reason: under
      # `set -e` a failing test in an `&&` list whose last command is `break` does not end the script.
      [[ $(date +%s) -ge $_fb_deadline ]] && break
      sleep 2
      FB_LIST=$(sudo fastboot devices 2>/dev/null || true)
    done
    if fastboot_pinned_only "$FB_LIST"; then
      say "      the list settled to $SERIAL alone after $(( $(date +%s) - _fb_t0 ))s"
    fi
  fi
  if ! fastboot_pinned_only "$FB_LIST"; then
    FB_N=$(fastboot_list_count "$FB_LIST")
    say "REFUSING: this step cannot act on a \`fastboot devices\` list that is not $SERIAL alone."
    say "          Measured on this host: \`usb 3-3\` carries a second phone-class device"
    say "          (serial 33e80afe…) that also enters fastboot, so a list like the one above is a"
    say "          state this machine really produces and not a hypothetical. The two"
    say "          \`fastboot devices\` checks above pass in this state - they ask whether $SERIAL is"
    say "          PRESENT, and when they ran it was - which is why the list is judged as a whole."
    printf '%s\n' "$FB_LIST" | sed 's/^/            /'
    die "fastboot did not settle to $SERIAL alone within ${FB_AMBIG_WAIT}s (it lists $FB_N device(s)); nothing was booted. Disconnect the other device, or wait for it to leave fastboot, then re-run - this is a refusal and not a failed run"
  fi

  # `fastboot boot` writes nothing to storage. This is the whole safety property of the
  # workflow, so it is a separate line that is never generated from a variable.
  #
  # `-s "$SERIAL"` and not a bare call: written into the command line as well as guarded above,
  # because the guard can only refuse a state it can *see*, and the pin still holds if the list
  # changes between the check and the call.
  #
  # --- the status is captured, and this is the one device call in the file that had no guard -------
  #
  # 616 pinned *this* call to the serial, 618 guarded the list it acts on, and 619 guarded the `adb`
  # call a few lines above - and this line, the one that actually boots the payload and spends the
  # press, was left bare. Under `set -e` a non-zero `fastboot boot` therefore ended the script with
  # **fastboot's own status**: no sentence from this file, and - because the gate's census counts
  # literal `exit N` in command position and nothing else (`preflight_boot_check.sh:1708`) - a code
  # the gate cannot see either. That is 584's defect ("a tool that could not read an artifact passed
  # its own status out as the gate's verdict") arriving in the runner, on the call that matters most.
  #
  # **The repair is deliberately NOT 619's, and the difference is the finding.** There, `|| die` was
  # right: a failed `adb reboot bootloader` means the boot did not happen. Here a non-zero status does
  # not establish that. `fastboot boot` **sends the image and then waits for the device to
  # acknowledge**, so a non-zero status can come from either side of the send - and which side is
  # **unmeasured**: this project has never observed a failing `fastboot boot` on this phone, and no
  # amount of reading this file can supply the number. The abort is therefore a *claim* - "it failed,
  # so nothing was sent" - and if the claim is wrong the abort discards sections 4 and 5, which are
  # the only things that produce a reading, after which the payload's log dies at the phone's next
  # power cycle: **the press is spent and the reading is lost, which is the worst outcome this file
  # has.** So the status becomes a value rather than a verdict, the wait and the capture run either
  # way (they are a bounded host-side wait and a read - neither can spend anything), and the
  # non-return verdict below is *conditioned* on it, because "the payload ran and the device did not
  # come back" is not a claim a boot call that did not report success can support.
  # --- R4, first half: the bytes about to be sent are the bytes the gate read --------------------
  #
  # This is the re-check 654 section 6 asked for, and the window it closes is real and not theoretical:
  # the gate ran several hundred lines ago and the device checks above may have waited up to
  # `FB_AMBIG_WAIT` seconds since. A build landing in that window replaces the image, and this run would
  # then quote the gate's verdict for bytes it never read. The pin is a hash of the file, so this is one
  # `sha256sum` and cannot be fooled by an mtime.
  #
  # **Refused, not warned.** Up to here nothing has been sent and the phone is already in fastboot, so
  # the cost of this refusal is the attempt and not the press - which is exactly the trade the check is
  # for. Below this line the cost inverts: `fastboot boot` spends the press on whatever bytes are there.
  _sha_now=$(sha256sum "$IMAGE" 2>/dev/null | cut -d' ' -f1)
  if [[ $_sha_now != "$IMAGE_SHA_PIN" ]]; then
    say "REFUSING: the image CHANGED between the gate's verdict and the send."
    say "          the gate read and accepted  $IMAGE_SHA_PIN"
    say "          the file is now             ${_sha_now:-<unreadable>}"
    die "something rebuilt $OUT while this run was in flight. Nothing was sent. Re-run so the gate reads the bytes it is about to vouch for, and note the standing rule this measures: do not build while a battery or a press is running (654)"
  fi

  BOOT_SENT_AT=$(date +%s)
  sudo fastboot boot -s "$SERIAL" "$IMAGE" || FB_BOOT_RC=$?

  # --- R4, second half: and the bytes sent were still those bytes when they left ----------------
  #
  # The third reading, and it is the one that makes the first two mean something: without it, "the file
  # is right just before the call" is a claim about a moment, and a build that lands *during* the call
  # is invisible. It cannot undo the send - the press is spent either way - so this is a **note** and not
  # a refusal, and its job is to stop the log being read as a reading of the arm it names. A `TREE`-like
  # state gets no verdict in its own direction (662), and after the send there is no direction left but
  # to say so.
  _sha_after=$(sha256sum "$IMAGE" 2>/dev/null | cut -d' ' -f1)
  if [[ $_sha_after == "$IMAGE_SHA_PIN" ]]; then
    say "the bytes sent were the bytes the gate read: $IMAGE_SHA_PIN, unchanged across the send"
  else
    say "**NOTE, and it is about this log's provenance rather than about the payload**: $IMAGE moved"
    say "  BETWEEN the send and this check - the gate read $IMAGE_SHA_PIN and the file is now"
    say "  ${_sha_after:-<unreadable>}. The payload that ran is the one that was in place at the call;"
    say "  whatever this run's log says about an arm, it is not evidence about the arm now on disk."
  fi
  if (( FB_BOOT_RC != 0 )); then
    say ""
    say "note: fastboot boot exited $FB_BOOT_RC, and **that is not a verdict about the payload**."
    say "  It sends the image and then waits for the device to acknowledge, so a non-zero status can"
    say "  come from either side of the send, and the value does not say which. The run continues to"
    say "  the wait and the capture because those are the only things that produce a reading - and a"
    say "  reading, once the phone's next power cycle happens, is gone. **If the device came back"
    say "  below, the payload ran** and this status was about the acknowledge. If it did not, the"
    say "  verdict is exit 1 and not exit 2 - the section below says why."
  fi
fi

# --- 4. wait for it to come back --------------------------------------------------------
step "waiting up to ${RETURN_TIMEOUT}s for the device to return"
say "(a bounded self-test should return on its own; a hang will not)"
RETURNED=0
RETURN_HOW=""
# Which test saw the return, when the return was seen outside the wait: `serial` or `port`. It is
# initialized here because the branch that reads it (below, after the two advance tests) needs an
# empty value to mean "neither fired", and `set -u` is on - `[[ -n $ENUM_ADVANCED ]]` on an unset
# variable is an error, not a false.
ENUM_ADVANCED=""
ENUM_BEFORE=$(serial_enum_count)
ENUM_BEFORE=${ENUM_BEFORE:-UNREAD}
# The port-keyed baseline, taken the same way and for the reason the note above `phone_port` gives:
# the serial test cannot see the phone in a mode that carries no serial, and 6 of the 1536
# enumerations on the phone's own port are exactly that. `PHONE_PORT` is UNREAD when the log cannot
# supply it, and then this counter is UNREAD too rather than zero.
PORT_BEFORE=$(port_enum_count)
PORT_BEFORE=${PORT_BEFORE:-UNREAD}
# A label rather than a `${PHONE_PORT:-...}` default carrying words, because an apostrophe inside a
# parameter-expansion default that itself sits in a double-quoted string ends the string early and
# the file stops parsing - measured on the first version of the line above, which said
# `${PHONE_PORT:-the phone's port}` and made `bash -n` report `unexpected EOF while looking for
# matching `}`" fifteen lines above the real site. Compute the prose once, here.
if [[ -n $PHONE_PORT ]]; then PORT_LABEL="usb $PHONE_PORT"; else PORT_LABEL="the port (not in the log)"; fi
PORT_ROLL_BEFORE=$(port_enum_rollup)
if [[ $DRY_RUN -eq 0 ]]; then
  say "(return criterion: serial $SERIAL in \`adb devices\`, or a new \`SerialNumber: $SERIAL\`"
  say " enumeration in the host log - adb alone is not it, see the note at serial_enum_count;"
  say " **or a new enumeration on $PORT_LABEL itself**, which is the reading that"
  say " sees a return by a mode with no serial - see the note above \`phone_port\`)"
  [[ $ENUM_BEFORE == UNREAD ]] && say "(the host log is unreadable here, so only adb can speak)"
  [[ $PORT_BEFORE == UNREAD ]] && say "(the phone's port is not in the log, so the port reading is absent rather than zero)"
  for _ in $(seq 1 $((RETURN_TIMEOUT / 3))); do
    # **The serial alone, and 619 deliberately does NOT tighten this one.** The question here is "did
    # the device come back", not "can adb command it", and a device listed `unauthorized` or `offline`
    # has come back - the SoC is running and the USB link is up. The verdict that fits is exit 3's
    # (returned, capture out of reach), which is where this leads, and tightening the predicate here
    # would turn a real return into "did not come back", i.e. exit 2 and a power press. Two call sites,
    # the same `grep`, opposite correct answers - which is why the state reader was added as a *second*
    # reader at the mode-detection site rather than by rewriting this line.
    if sudo adb devices 2>/dev/null | grep -q "^$SERIAL"; then
      mark_return "adb"; break
    fi
    if [[ $ENUM_BEFORE != UNREAD ]]; then
      _now=$(serial_enum_count)
      if [[ -n $_now && $_now -gt $ENUM_BEFORE ]]; then
        mark_return "host log, serial"; break
      fi
    fi
    # The port reading, and it is checked second on purpose: when both advance the serial is the
    # stronger statement and should be the one reported.
    if [[ $PORT_BEFORE != UNREAD ]]; then
      _pnow=$(port_enum_count)
      if [[ -n $_pnow && $_pnow -gt $PORT_BEFORE ]]; then
        mark_return "host log, port"; break
      fi
    fi
    sleep 3
  done
fi

if [[ $DRY_RUN -eq 0 && $RETURNED -eq 0 ]]; then
  ENUM_AFTER=$(serial_enum_count)
  ENUM_AFTER=${ENUM_AFTER:-UNREAD}
  PORT_AFTER=$(port_enum_count)
  PORT_AFTER=${PORT_AFTER:-UNREAD}
  PORT_ROLL_AFTER=$(port_enum_rollup)
  say ""
  say "bounded wait expired after ${RETURN_TIMEOUT}s. Evidence:"
  say "  adb:      serial $SERIAL not listed"
  say "  host log: $ENUM_BEFORE -> $ENUM_AFTER enumeration(s) of SerialNumber: $SERIAL"
  say "  the port $SERIAL is on (${PHONE_PORT:-not in the log}):"
  say "            $PORT_BEFORE -> $PORT_AFTER enumeration(s), any id"
  # **A port advance is a return, and this is the branch 617 added.** The serial-keyed test cannot
  # see the phone in a mode whose USB descriptors are empty - measured, 6 of the 1536 enumerations on
  # 3-10 - so a run that ends with the phone in such a mode used to fall through every test below and
  # be reported as **exit 2, "the device did not come back"**, which is the one exit code this
  # contract reserves for a hang and the one that tells the operator to press power. The device did
  # come back; the criterion could not see it. The ids are printed and the identity claim is left to
  # the operator, because the port is a fact about where the phone has been and not proof of who
  # enumerated: `05c6` is also the neighbour's vendor on `usb 3-3`.
  #
  # **And the two branches are ordered, not interchangeable**, which the first version of this block
  # got wrong: the port test was written first, and a normal return advances BOTH counts (the
  # re-enumeration is on the same port), so the port branch fired for every return and the serial
  # branch - the one whose message can say the device came back *with* its serial - became
  # unreachable. The order here matches the wait loop's for the same reason: when both advance, the
  # serial is the stronger statement and the port branch is left to do only the job it was added for,
  # seeing the return the serial test is blind to.
  #
  # The serial branch taking the `if` and the port branch the `elif` is what makes this a partition
  # rather than a preference: neither state below can be reported by the other, and the second is only
  # ever reached when the first found nothing.
  #
  # **And that claim is covered by cells rather than by the paragraph above**, which is not how it was
  # first written: reverting this order left the rehearsal green, because the only state reaching this
  # block with an advance was the port-only one. Two states in `tools/rehearse_live_path.sh` now pin it
  # - `return-after-last-poll` (a normal return, both counts advance, this branch must answer) and
  # `qdl-return` (the port alone, the `elif` must answer) - and reverting the order turns the first red
  # and leaves the second green.
  if [[ $ENUM_BEFORE != UNREAD && $ENUM_AFTER != UNREAD && $ENUM_AFTER -gt $ENUM_BEFORE ]]; then
    say ""
    say "REFUSING to call this a non-return: the host log shows the phone enumerating again"
    say "after the boot, so the device DID come back - it came back into a state adb cannot"
    say "reach, which is a capture failure and not a hang. Do not read this as exit 2."
    say "The log lives in the top of DRAM and survives until a power cycle, so if the phone"
    say "settles into Android, re-read it with:"
    say "  sudo adb -s $SERIAL exec-out 'cat /proc/last_kmsg' > $LOGFILE"
    ENUM_ADVANCED=serial
  elif [[ $PORT_BEFORE != UNREAD && $PORT_AFTER != UNREAD && $PORT_AFTER -gt $PORT_BEFORE ]]; then
    say ""
    say "REFUSING to call this a non-return: an enumeration appeared on ${PHONE_PORT}, the port"
    say "$SERIAL is on, after the boot. New vendor:product on that port, versus the baseline:"
    say "  before: ${PORT_ROLL_BEFORE:-none}"
    say "  after:  ${PORT_ROLL_AFTER:-none}"
    say "**This is the reading the serial test is blind to, and it is not automatically the phone.**"
    say "A \`05c6:*\` id here is what this phone itself presents when it enumerates with empty USB"
    say "descriptors - the host log carries six enumerations on ${PORT_LABEL}, each an"
    say "\`idVendor=05c6, idProduct=f006\` followed by \`Mfr=0, Product=0, SerialNumber=0\`, and each"
    say "five seconds from that same device's \`2717:0368\` \`SerialNumber: 4a2fe00b\` line - but"
    say "\`05c6\` is also the vendor of a different phone on \`usb 3-3\`. So read the ids above: an id"
    say "this phone has presented before is this phone back; an id it has not is a different device"
    say "and this run still produced no reading of yours."
    say "Either way the capture is out of reach and the log survives until a power cycle, so if the"
    say "phone settles into Android, re-read it with:"
    say "  sudo adb -s $SERIAL exec-out 'cat /proc/last_kmsg' > $LOGFILE"
    ENUM_ADVANCED=port
  fi
  # **Both branches above mean the same thing and end the same way, and the first version of this
  # block did not.** The port branch set `ENUM_ADVANCED=port` and then fell out of the `if`, straight
  # past the two tests below - neither of which can fire, because a port advance does not move the
  # serial count - and into the final "The device did not come back", i.e. **`exit 2`**. So the fix
  # for a false non-return printed the right refusal and then returned the one code it was written to
  # prevent, which is the same defect one layer in: a message and a status that disagree, and the
  # status is what the operator's next action reads. The park note is here rather than inside the
  # serial branch for the same reason: each branch above tells the operator how to re-read the log by
  # hand, and the name it reads *into* is the one step 2b may have parked, so *which file is at
  # `$LOGFILE`* is owed by both. It sat inside the serial branch only because until 617 that branch
  # was the only one; leaving it there would have made "the previous run's log is NOT at that name" a
  # property of which test happened to fire.
  #
  # The history the note itself carries is unchanged: 588 replaced a `-n $PREV_LOG` test that could
  # not tell "parked" from "failed to park", and 566 §3's producer attribution is why this message
  # matters - at the real `RETURN_TIMEOUT`, a phone that enumerates inside the wait leaves section 4
  # with `RETURNED` true and this block is never reached, so the operator who *does* reach it is the
  # one whose device was silent past the whole wait, and for them the earlier log's location is the
  # one thing the section 5 message says and this one did not.
  if [[ -n $ENUM_ADVANCED ]]; then
    case $PARK in
      parked)
        say "The previous run's log is NOT at that name: step 2b parked it before the boot, so reading"
        say "into $LOGFILE above creates this run's file beside it. The earlier one is at"
        say "  $PREV_LOG"
        say "- read it for the 2026-09-22 death, and never as this run's, whatever bracket it carries." ;;
      nopark)
        say "The previous run's log is still AT that name - step 2b could not move it aside (511/512),"
        say "so it and this run's capture share one path. If the hand retry below succeeds it has been"
        say "REPLACED, and the earlier bytes are then at the sha256 the gate printed; if it cannot read"
        say "at all the file is still the earlier one, and its sha256 is that same value." ;;
      nofile)
        say "There was no log at that name before this boot: step 2b said 'no log at ... yet', so"
        say "nothing there is an earlier run's and nothing there can be this run's except what the"
        say "hand retry below writes. A file present afterwards is this run's, whatever its content." ;;
      *)
        # Same as the block in step 5: not a dry-run (this one is inside `DRY_RUN -eq 0`), so the only
        # way here is that step 2b never set the state - a defect in this file, said as one.
        say "UNREAD  step 2b's park state was never set, so whether an earlier run's log is at"
        say "        $LOGFILE, at some .prev name, or nowhere is not established here - check by"
        say "        hand before spending the next power press on an unexplained reading." ;;
    esac
    exit 3
  fi
  # **The unreadable case is not exit 2, and the difference matters more than the code does.** Exit
  # 2's definition is "the payload ran and the device did NOT come back" - a device verdict, and the
  # one that spends the device, because it tells the operator to press power. This state has no
  # verdict: `adb devices` is empty and the comparison that stands in for it is the one that failed.
  # It is reported as **exit 1** - the code this file already uses for host-side failures that say
  # nothing about the device ("the gate refused, the device was not found", and 511/512's unwritable
  # `$LOGFILE`), which is what a host log that could not be read is. `serial_enum_count` could not
  # return UNREAD at all until the same step repaired it (`grep -c` prints `0` for empty input, so a
  # *failed* read read as a zero), so until now this branch was decoration over a state the file
  # claimed to detect and never did - and the value it produced instead (0 -> 0) fell through to the
  # non-return message below, i.e. the strongest reading in the file from no reading at all.
  if [[ $ENUM_BEFORE == UNREAD || $ENUM_AFTER == UNREAD ]]; then
    say ""
    say "UNREAD: the host's own USB log could not be read (dmesg returned nothing), so this run has"
    say "NO reading of whether the device came back - \`adb devices\` is empty, and the comparison"
    say "that stands in for it is the one that failed. **This is not a non-return and must not be"
    say "read as one: it is not a verdict.** Check by hand before pressing power:"
    say "  sudo dmesg | grep 'usb 3-10'   # the phone is usb 3-10, serial $SERIAL"
    say "The reading is: an enumeration on that port after the fastboot disconnect, with the"
    say "SoC reset behind it, is a return whatever adb said; a single dead second in fastboot"
    say "with nothing after it is a hang - and if it is a hang, the power press is what it needs."
    die "the host could not read its own USB log, so this run produced no reading of the device (exit 1, not 2 - see the section above)"
  fi
  # **The non-return verdict is conditioned on the boot call's own status, and this block is why the
  # status was captured rather than passed through.** Both of the tests below end in exit 2, whose
  # definition in this file's header is *"the payload ran and the device did NOT come back"* - a
  # verdict about the payload, and the one that spends the device, because it tells the operator to
  # press power. A `fastboot boot` that exited non-zero cannot support that claim: the image may never
  # have been sent. Reporting exit 2 here would send the operator to press power on a phone that is
  # probably still sitting in fastboot with nothing booted, and the press is the scarcest thing this
  # phase has. So the boot call's failure takes precedence over both tests, and the code is **exit 1**
  # - this file's own code for "a host-side failure that says nothing about what the payload would have
  # done", the same reading 511/512's unwritable `$LOGFILE` and the UNREAD case above get.
  #
  # It sits *after* the two return branches and *after* the UNREAD test on purpose: a return outranks
  # it (if the device came back, the payload ran and the failed status was about the acknowledge), and
  # the UNREAD case is exit 1 already with a finer thing to say. The evidence lines the block above
  # printed still stand and are what the operator reads alongside this.
  if (( FB_BOOT_RC != 0 )); then
    say ""
    say "The device did not come back, AND fastboot boot itself did not report success (exit $FB_BOOT_RC)."
    say "**Those two facts together are not the exit-2 state.** Exit 2 means *the payload ran and the"
    say "device did not come back*, and a boot call that did not report success cannot support the"
    say "claim that the payload ran - the image may never have been sent. So this is **exit 1**, this"
    say "file's code for a host-side failure that says nothing about the payload, and it is NOT a"
    say "reading that the payload ran or hung. Check by hand before touching anything:"
    say "  sudo fastboot devices   # if $SERIAL is listed, no boot was ever started"
    say "If it is listed, **the press was not spent** - the phone is in fastboot waiting for exactly"
    say "this command, and re-running costs nothing. If it is not listed and nothing enumerated on"
    say "$PORT_LABEL, do not press power on this file's word: read the host log by hand"
    say "  sudo dmesg | grep 'usb ${PHONE_PORT:-3-10}'"
    say "and treat a payload log that is present in DRAM as the reading, because the phone's next"
    say "power cycle destroys it."
    # `die` and not a literal `exit 1`: this is the same code as the UNREAD case above and the file
    # spells an exit-1 host-side failure that way, which keeps section 4's *literal* exit sites at
    # `2 3` and puts both exit-1 producers behind `die`'s own definition. That the gate's census had
    # to learn to read `die` to see either of them is the finding this step is about. The wording
    # deliberately does **not** reuse "the device did not come back", which is exit 2's headline and
    # the phrase three rehearsal cells forbid: this block must not read like the verdict it replaces.
    die "fastboot boot exited $FB_BOOT_RC and no return was seen, so this run has no verdict about the payload (exit 1, not 2 - nothing here says the payload ran; see the lines above)"
  fi
  if [[ $ENUM_AFTER -lt $ENUM_BEFORE ]]; then
    say ""
    say "the comparison fell - $ENUM_BEFORE -> $ENUM_AFTER enumeration(s) - which is the dmesg ring"
    say "buffer rotating rather than evidence of absence, so it is not a reading of the device"
    say "either. It is reported as exit 2 because that is the reading it cannot rule out; check"
    say "by hand before pressing power:"
    say "  sudo dmesg | grep 'usb 3-10'   # the phone is usb 3-10, serial $SERIAL"
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

# **The interval the press is actually read by, printed while the run still knows it.** Section 5 and
# the summary below both read the payload's LOG, and the log cannot contain this number: how long the
# device took to come back is a fact about the host's clock and the send, not about anything the
# payload wrote. For a self-test arm it is the answer (`stage90_main.c:1244-1254`: the countdown
# rebooting us at ~28 s, the bounded spin at ~90 s), so it is printed here, next to the exit code it
# must be read beside, rather than left to two clock readings in the operator's head.
#
# Printed only when a return was seen and only when the clock was taken: `BOOT_SENT_AT` is set
# immediately before the send, so `--dry-run` has neither and prints nothing - "not measured" must not
# render as a number.
if [[ $DRY_RUN -eq 0 && $RETURNED -eq 1 && -n $RETURN_AFTER_S ]]; then
  say ""
  say "the device came back ${RETURN_AFTER_S}s after this run called \`fastboot boot\` (seen via: $RETURN_HOW)."
  say "**Read this number beside the exit code, and for a self-test arm read it as the answer.**"
  say "  It is measured from the send, so it includes the transfer and the payload's own start-up"
  say "  (633 measured 1.1805 s of that prelude for the park arm) and it is therefore a little"
  say "  larger than the arm's own stated time. The candidate times for a self-test arm are far"
  say "  apart - for the arm in \`out/\`, ~28 s if the SoC's countdown fired and ~90 s if the bounded"
  say "  spin did - and the summary below prints both of them out of the arm's own log line."
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
    # **Four states, four sentences - and the third is the one that was wrong here (588).** This block
    # used to test `-n $PREV_LOG`, which is false for a *failed* park AND for a run that started with no
    # log at all, so a run in the second of those printed "it is still the run before this one's" about
    # a name step 2b had already said nothing was at. It is the message with the least margin for a
    # wrong sentence: the run is spent, the log is in DRAM, and the next power press destroys it.
    case $PARK in
      parked)
        say "$LOGFILE is absent and stays absent, because step 2b parked the previous run's log at"
        say "  $PREV_LOG"
        say "- so nothing at the name $LOGFILE can be this run's. Do not read that parked file as this"
        say "run's either: its bracket is the one 547 section 4 was derived from." ;;
      nopark)
        say "$LOGFILE is untouched, so it is still the run before this one's - do not read it as"
        say "this one's (step 2b could not park it, so its sha256 must still equal the one the gate"
        say "printed). It is also the name the hand retry above writes to, so a successful retry"
        say "replaces it." ;;
      nofile)
        say "$LOGFILE does not exist and did not before this boot either (step 2b: 'no log at ..."
        say "yet'), so there is nothing here to mistake for an earlier run's - and nothing here is"
        say "this run's until the hand retry above writes it. A file present afterwards is new." ;;
      *)
        # **Not a dry-run: this block is inside step 5's `else`, so a dry run cannot reach it** - the
        # first version of this case said it could, which is an unreachable branch claiming a state
        # (the defect 582 section 2 and 586 section 2 both name). `unknown` is `$PARK`'s initial value
        # and means step 2b never set it, which is a defect in this file rather than a state of the log
        # - so it is said as that, and the operator is not told anything about the file's provenance.
        say "UNREAD  step 2b's park state was never set, so whether $LOGFILE holds an earlier run's"
        say "        log, this run's, or nothing is not established by this file - read it by hand"
        say "        (does it exist, what is its mtime, and does it carry this run's payload banner)" ;;
    esac
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

  # --- and the log this run just captured is read here, not left to be remembered ---------
  #
  # The run opened by summarising the **previous** log (step "payload output", above) and then spent
  # the boot to produce this one; until now it ended at `done. Full log: <name>` and the reading was a
  # command the operator had to know to type. That is the asymmetry this project keeps removing - every
  # failure path in the wait section prints its reading in full, and the one path that costs a run and
  # *produces* a reading printed least. On this phase's runs the boot is a single chance (the payload's
  # log lives in the top of DRAM and a second boot overwrites it), so the reading is printed where it
  # is produced rather than left to be remembered. It is a read of a file already on disk: it touches
  # no device, cannot spend anything, and cannot change the capture.
  #
  # It is placed after the payload-line check on purpose: with fewer than 8 payload lines the block
  # above has just said the file is suspect, and the summary is then read under that warning rather
  # than instead of it. `--summarise` remains the way to re-read a log later, and the two paths print
  # the same table from the same function.
  say ""
  step "reading the log this run captured"
  summarise_log "$LOGFILE"
fi

# The marker table is the part that decides what a run *meant*, so it is a function with
# its own entry point (--summarise) rather than inline: it can then be tested against
# synthetic logs, and re-run on a log captured elsewhere, without touching the device.
say "done. Full log: $LOGFILE"
