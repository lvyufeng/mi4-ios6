# 939 — the 509 clause's `objdump | grep -q` SIGPIPE race, established and fixed (2026-10-08)

938 left the D13 entry build stopping at

    FAIL: bsdinit_task (0x8003405c..0x80035000) does not branch to __wrap_load_init_program (0x80499c28)

and the whole rung was spent on the obvious reading: that D13's boot thread reaches the loader from
somewhere other than `bsdinit_task`, or by a path that never goes through pid 1's startup. **That
reading is false.** The branch is there, at `0x800340fc`:

    800340fc:  eb1196c9  bl  80499c28 <__wrap_load_init_program>

inside `bsdinit_task`'s symbol range exactly as the clause wants. The clause was failing on a
**correct image**.

## The cause: `set -o pipefail` + `objdump | grep -q` = a SIGPIPE race

`build_entry.sh:25` sets `set -euo pipefail`. The clause read its range with

    arm-none-eabi-objdump -d … | grep -q "bl…<__wrap_load_init_program>"

`grep -q` **exits at the first match** and closes the pipe. `objdump` is still writing the other
~960 lines of the range, takes **SIGPIPE**, and dies with status 141. Under `pipefail` the pipeline's
status is the *rightmost non-zero* — so the pipeline reports **141**, `||` fires, and `layout_fail`
prints its sentence **even though `grep` matched**. Whether objdump has finished writing before grep
exits is a race on the two processes' speeds — which is why the same command run by hand, or the
whole build re-run, usually passed.

**How it was established.** The earlier rung that first met this sentence (511, below) recorded the
cause as *"not established"*: the identical command re-run 200× by hand matched every time. Here the
clause reproduced **deterministically** across two clean builds, so `set -x` was wrapped around the
bare pipeline. The trace showed the shell taking **exit 141 at the pipeline itself**, before the
`$?` capture line ran — objdump's SIGPIPE, not a missing branch:

    + arm-none-eabi-objdump -d …/xnu_arm_entry.elf --start-address=0x8003405c --stop-address=0x80035000
    + grep -q 'bl[[:space:]]\+80499c28 <__wrap_load_init_program>'
    (shell exits 141 here; `_st=$?` never runs)

Capturing the range into a variable first (`bsdinit_body=$(objdump …)`, then
`grep -q … <<<"$bsdinit_body"`) makes the clause pass — and makes the exit status `grep`'s alone.

## The same defect was already fixed twice in this file, under a weaker name

`build_entry.sh` already carried the identical fix at two other readers — the 511 clause
(`ast_taken_user` → `__wrap_bsd_ast`, `:29703`) and the `strings … | grep -q` reader at `:27287` —
each with a comment naming the SIGPIPE/`pipefail` race as *a precaution rather than a diagnosis*.
509's clause was the **last un-fixed straggler**: it is the only site of this shape that still piped
`objdump` straight into `grep -q`. This rung confirms the precaution's cause and removes the last one.

This is the class of [[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]: the pipeline's
status is not `grep`'s verdict — it is whatever the *shell* harvested from a pipeline in which the
producer died after the reader had already answered.

## The change

- **`src/entry/build_entry.sh`** (the 509 clause): capture the `bsdinit_task` disassembly into
  `bsdinit_body` and grep the herestring, exactly as 511 and 27287 already do.
- **`src/entry/build_entry.sh`** (the 511 comment): the cause it recorded as *"not established"* is
  now stated as established, with the `set -x` / exit-141 evidence and the race explanation.

## State after this rung

509's clause passes and the D13 build advances to the next wall:

    FAIL: activate_exec_state is not in the linked image - 510's clause needs the exec path's own
    function to read the call site out of

510 wraps `thread_setentrypoint`, whose only caller in 4570 is `activate_exec_state` in
`bsd/kern/kern_exec.c`. **D13's `kern_exec.c` has no `activate_exec_state`** (`grep` empty) — the
same class as 937/938: a 4570 call-site name read out of a tree that does not have it. That is its
own rung.

`make check` exits **0**.

## Provenance

`src/entry/build_entry.sh`. Host-side, reversible, **no press**.

- D13 builds: `/tmp/d13_entry_orig1.log`, `/tmp/d13_entry_orig2.log` (deterministic `does not branch`
  FAIL); `/tmp/d13_entry_build18.log` (the `set -x` trace, shell exit 141);
  `/tmp/d13_entry_build19.log` (509 passes, advances to 510).
- 4570 neutrality: the clause body is tree-independent (it reads the linked image, not the tree), so
  the captured-variable form is used on both trees and the checked property is unchanged; the 511
  comment edit is comment-only.
- `make check` exits 0.