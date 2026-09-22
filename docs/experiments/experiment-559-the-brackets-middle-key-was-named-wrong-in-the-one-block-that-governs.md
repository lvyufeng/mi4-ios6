# 559: the bracket's middle key was named wrong in the one block that governs

A host-side change to `stages/stage90/preflight_boot_check.sh`, no device and no build. Two
narration repairs, both of them a claim the bytes do not support, both of them in blocks the reader
of the next run's log takes as authoritative.

## 1. The bracket named `rtcab`, and `rtcab` is the abort path

The gate's `== which arm the entry image in out/ is, in words ==` block states the bracket the next
run's log has to be read through. It said:

> `xnu_live_slot_pre_calls`, **`xnu_live_slot_rtcab_calls`** (the same site's rtcpre spelling is the
> other key set this image carries tables for) and `xnu_live_slot_post_calls` run in the same pass on
> one schedule and one gate — so pre without **rtcab** localizes the death between the two notes, and
> pre and **rtcab** without post puts it inside `platform_cache_idle_exit`.

Three things are wrong with that, and the third is the one that matters:

1. **`rtcab` is not published by the exit wrapper.** It comes from `entry_stubs.c:1792`,
   `entry_slot_rtc_note(&g_slot_rtcab, thread)`, inside **`entry_note_sleh`** — the abort path. The
   exit wrapper publishes `g_slot_pre` (`entry_trace.c:1971`; `entry_slot_null_note` at `:1968` in
   this arm) and `g_slot_post` (`:1981`), and beside them `g_slot_rtcpre` at `:1973`. The
   entry-side companion of `pre` is `rtcpre`.
2. **So `rtcab` is a *storm* count, not an entry count.** Read out of `/tmp/cancro-last_kmsg.txt`
   (520's capture, the run this phase's death was measured on, re-read here rather than recalled):
   `xnu_live_slot_rtcab_calls` has **five** records — `1, 2, 3, 4, 8` — i.e. at least eight aborts,
   with three distinct `rtcab_pop` values and two distinct `rtcab_thr` values in the same five
   records. `xnu_live_slot_rtcpre_calls` has **exactly one**, and `slot_rtcab_rej = 0`.
3. **A bracket read off `rtcab` inverts the reading the arm exists to buy.** The item the next run
   turns on is whether the pass died *inside* `platform_cache_idle_exit`, and the answer is
   "`pre` and `rtcpre` published, `post` absent". Substituting a key that counts aborts means the
   criterion is satisfied by the death itself, on every run that dies anywhere — it would read the
   window as entered eight times and would put the death in the exit even when the notes say
   otherwise.

The name is corrected, and the block now says why: `rtcab` is named as *not* part of the bracket,
with its publisher, its real meaning, and the count from 520's own log, so a later reader cannot
re-derive the wrong pairing from the same proximity.

**This is one key name with two definitions, and the runner's was fixed first.** `run_and_capture.sh`
read `slot_rtcab_calls` in the same role until the peer's **558** (`e362a28`, experiment-558, "the
wrapper's second publisher is the rtcpre note, not the abort's rtcab"), and its comment now says so
in its own words (`:238-242`, "The second of the three is `rtcpre`, not `rtcab` … This block read
`slot_rtcab_calls` here until 558 and that was wrong"). That is this project's oldest defect — one
decision answered in N places, N − 1 of them uncompared — with N = 2 here: the runner and this gate.
552's rule applies to a key name exactly as it applies to a boot argument: the definition that
matters is the one the *source* writes, and neither reader could see the other. The gate's copy
outlived the runner's only because the gate's block is narration and nothing asserted it — which is
the same asymmetry 556 repaired for the reader's fallback address, one key name over.

## 2. An orphan sentence that 556's own edit left behind

556 replaced item (4) of the 526-era narration with the arm-decides-the-meaning text. The
replacement covered the criterion but not its trailing clause, so the block read

```
… a death read here by eye is read that way and not by this sentence. Any
later ending than 520's is progress.
```

The survivor is worse than redundant: 556's text says a death whose `lr` is the exit's own `bl`
return address **is 547 §4's prediction for this arm**, and the orphan says any later ending is
progress — the criterion the arm decides, stated as if the ending decided it. Removed.

The same sentence still stands in the 522 narrative block (`:1133`), and it is left there
deliberately: 522 is a *spent* arm with a historical reading, its arm is the enable-**on** one, and
547 §4 pre-registers the death only for the enable-off arm — so for 522 the sentence is not the
error it is here. It is reported rather than swept.

## 3. Tested, host-side only

- `bash -n` clean.
- The whole gate with `--allow-xnu-entry`: **exit 0**, GREEN, and the derived block prints the
  corrected bracket above the arm narration.
- `diff` against the gate's output before these two edits: three hunks, all intended — the bracket
  passage, the orphan's removal, and the **exit-site line numbers** (`:641/:653/:663` →
  `:644/:656/:666`). The last one is not a defect: the reader computes those numbers at gate time
  (552), and they moved because `run_and_capture.sh` was edited in the same window. A pinned number
  would have had to be edited here as well, which is the reason 552 made the gate compute them.

## 4. What is not changed

- **No artifact.** `out/stage90/stage90-qcdt.img` (`1daaf44e…`) and `out/stage90/xnu_arm_entry.bin`
  (`f202f246…`) are the frozen pair, unchanged; `./build.sh` was not run.
- **`run_and_capture.sh` is not edited from here.** It is another session's lane and 558 is already
  its fix for the same key; the gate's narration is brought to the same reading, not the reverse.
- **Only my own file is committed.** `git add` was by explicit path — `stages/stage90/preflight_boot_check.sh`,
  this file, and the `README.md` row — with `git branch --show-current` checked first, so nothing else
  in the shared worktree could be swept in. 557's collision, and 556's, are why that is stated rather
  than assumed. 558 was already committed and mirrored (`e362a28`) when this was written, so the tree
  this commit builds on is the peer's.

## 5. Safety

No device action, no build, no write to storage. Non-persistent `fastboot boot` remains the only
device verb this phase uses and this change does not run it; the worst case is a refusal to proceed.
