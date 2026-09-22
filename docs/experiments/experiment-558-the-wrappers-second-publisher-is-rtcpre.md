# 558: the wrapper's second publisher is the rtcpre note, not the abort's rtcab

A host-side repair to `run_and_capture.sh`'s clause (3) and to two committed documents, no device and no
build. It is the project's oldest class in the place it is most expensive: **a key that names the wrong
site**, found by the peer session reading the same claim against the source, and verified here at both
call sites before the change rather than after.

## 1. Two sites, two structs, two key sets - and they were conflated

The exit wrapper's bracket has three publishers, and the middle one is `g_slot_rtcpre`:

| site | source | the struct | the keys |
| --- | --- | --- | --- |
| `__wrap_platform_cache_idle_exit`, before the real call | `entry_trace.c:1968` (or `:1972` in the capture arm) | `g_slot_pre` | `xnu_live_slot_pre_*` |
| the same wrapper, after it | `entry_trace.c:1973` `entry_slot_rtc_note(&g_slot_rtcpre, entry_tpidrprw())` | **`g_slot_rtcpre`** | `xnu_live_slot_rtcpre_*` |
| the same wrapper, after the real exit returns | `entry_trace.c:1978` | `g_slot_post` | `xnu_live_slot_post_*` |
| **the abort path** | `entry_stubs.c:1792` `entry_slot_rtc_note(&g_slot_rtcab, thread)` inside `entry_note_sleh` | **`g_slot_rtcab`** | `xnu_live_slot_rtcab_*` |

and the two structs are separate objects with the same nine-key shape
(`entry_stubs.c:6403-6415`), which is why the conflation is invisible in a grep for
`entry_slot_rtc_note`: **the two calls are one function with two structs, and only the struct decides
which key set is written.**

**The log already said so.** 520's own `/tmp/cancro-last_kmsg.txt`:

| key | records | what that is |
| --- | --- | --- |
| `xnu_live_slot_rtcab_calls` | **5** - `1,2,3,4,8` | the publish schedule (`n <= 4 \|\| (n & (n-1)) == 0`) applied to the run's **9 abort episodes** |
| `xnu_live_slot_rtcpre_calls` | **1** | one pass through the exit wrapper |
| `xnu_live_slot_pre_calls` | **1** | the same pass, one note earlier |
| `xnu_live_slot_post_calls` | absent | it never came back |

A per-pass counter cannot have five records while the two counters beside it in the same pass have one.
That is the reading 557 was already doing - its table lists `slot_ab_calls`/`rtcab_calls` as the
abort path's, "**not** per-pass" - and it did not go back to the reader's own clause, which was keyed on
it. **A corrected reading in one file does not correct the check in another**; that is what this step is.

## 2. What the wrong key would have printed, in the one direction it can be wrong

Clause (3)'s conjunction is `pre_calls >= 1 && rtcab_calls >= 1` with `post_calls` absent. `pre_calls >= 1`
already proves the wrapper was entered, so the `rtcab` term can only *subtract*:

| the log | `pre` | `rtcpre` | `rtcab` | old clause (3) | correct |
| --- | --- | --- | --- | --- | --- |
| 520's shape | 1 | 1 | 8 | DIED IN THE EXIT | DIED IN THE EXIT |
| **a pass with no abort** | 1 | **1** | **absent** | **DIED BEFORE THE EXIT** - "the rtcPop reading did not [publish]" | DIED IN THE EXIT |
| a log with aborts that never reached the wrapper | absent | absent | 8 | falls through to UNREAD | UNREAD |

So the failure mode is a **false localization**: the log carries `xnu_live_slot_rtcpre_calls=0x1` - the
signature the `_rtcpre` struct is *named for* - and the reader says the rtcPop reading never published.
`rtcab_rej=0` five times and `rtcab_pop` with three distinct values are the same fact seen from the other
side.

## 3. The test, with the old reader run against the state it is meant to refuse

State J is 520's real log with the single line `xnu_live_slot_rtcpre_calls=0x00000001` removed and
everything else untouched (`slot_rtcab_calls` still present, all five records). The pre-change file,
recovered with `git show f33345e:stages/stage90/run_and_capture.sh`, prints:

```
  DIED IN THE EXIT  pre_calls=0x00000001 and rtcab_calls=0x00000008 both published and
```

and the file as changed prints:

```
  DIED BEFORE THE EXIT  pre_calls=0x00000001 published but the rtcPop reading
        (slot_rtcpre_calls) did not, so the pass died between the two notes ...
```

The `0x00000008` in the old line is an abort count standing where a pass count belongs, printed with the
same confidence as the correct one - which is the shape worth recording: **the number was right and the
question was wrong.**

The other states are unchanged (A/B/C/D/E/F/G/H from 554-555, all re-run: `PREDICTED+533`, `FAIL` on a
moved `lr`, the 522 caveat, `PASS`+`FALSIFIER`, the unreadable pair, the other cache arm, the unreadable
cache arm, the earlier-episode log). `bash -n` clean; the gate re-run read-only, **exit 0**; its three
`exit` sites still re-derived at gate time.

## 4. The two documents that carried it, corrected in place with a note

Both are pre-registrations of a reading, so the *prediction* must not be edited silently - but the key
name is an instrument fact, not a prediction, and the three-way pattern is the same three sites in the
same order:

- **533 §5.2** (the frozen arm's own instructions to its reader): the table row and the caveat said
  `xnu_live_slot_rtcab_*`, corrected in place, with a `> Correction (558, before the run)` block naming
  both call sites and recording that the run had not happened when it was made.
- **540** (the reader's own doc): the sentence naming `entry_slot_rtc_note(&g_slot_rtcab)` and the
  four-row table's `rtcab_calls` row, corrected in place with the same kind of note.

Neither edit changes what 547 §4 or 533 §5 predict for the run.

**And this list was incomplete, in a way worth recording.** The census behind it was "the files I touched
in this phase", and that is the wrong predicate: [560](experiment-560-the-same-wrong-key-in-four-more-files.md)
(`35a450b`) found the same key in **546 §5, 547 §4, 554 §1 and 557 §4c** - all four written or edited
this phase, none of them grepped for the key. The enumeration that terminates is **every file that names
the key**, and `grep -rn 'rtcab' docs/ stages/stage90/` is the whole of it. This is the same defect one
turn further down than the one 558 exists to fix: 557 found `rtcab` was not per-pass and did not go back
to the check that consumed it; 558 fixed the check and did not go back to the prose; 560 is that omission,
found by the peer session rather than by me.

## 5. Reported not patched, and the report is now spent

> **Pointer (561): the gate has been repaired, so "still has it" below is true of `e362a28` and false of
> the tree.** The patch is [559](experiment-559-the-brackets-middle-key-was-named-wrong-in-the-one-block-that-governs.md)
> (`92f6aca`), by the file's owner; the quoted `echo` block is the gate's **pre-559** text and is kept
> here as the evidence the report was made on, not as a description of the current file. Read the two
> together. The rest of this section is unchanged, including the reason it was reported rather than
> patched.

**The gate's own bracket paragraph still has it, and now contradicts itself.** `preflight_boot_check.sh`
prints, at the `--allow-xnu-entry` capture-site block:

```
echo "  bracket (pre note -> rtc note -> the real exit -> post note; 538 read the three call sites out of"
echo "      these very bytes): xnu_live_slot_pre_calls, xnu_live_slot_rtcab_calls (the same site's rtcpre"
echo "      spelling is the other key set this image carries tables for) and xnu_live_slot_post_calls run"
echo "      in the same pass on one schedule and one gate - so pre without rtcab localizes the death"
```

Three things are wrong with it and the parenthetical is the worst: `rtcpre` is **not** "the same site's
other spelling" - the two are different sites, one in the wrapper and one in the abort handler - so the
sentence presents the reader's error as a supported alternative. And ~270 lines below it, the gate's
derived narration already says the right thing ("`pre` and `rtcpre` published with
`xnu_live_slot_post_calls` absent is what puts the death inside the exit"). That file is the peer
session's lane and is dirty in the shared worktree right now, so this is reported with the clause text
rather than patched across the boundary - the same discipline as 551 §4, 554 §5 and 557's message.

## 6. Two of the peer's other notes, recorded and not acted on

- **`rtcpre_sp` drift is the `_rin` refusal, not `_rej`.** `slot_pre_sp=0x8054fed0` against
  `slot_rtcpre_sp=0x00000000` with `slot_rtcpre_rej=0` means the refused path in play is the one the
  struct distinguishes for "a mapped `thr` whose own field is not" (`entry_stubs.c:6395-6396`), not the
  `thr`-outside-the-map one. Nothing in this block reads either counter, so it is recorded.
- **`FlushPoU_Dcache` has four call sites and `FlushPoC_Dcache` two more**, which is where the project's
  "six" came from. It cannot reach this reader: `exit_pop_lr_addr` disassembles only
  `platform_cache_idle_exit`'s address range (`start` … `start + size` from the symbol table), so the
  other three PoU sites and both PoC sites are outside it, and the awk pattern names the callee
  (`/<FlushPoU_Dcache>/`, which `FlushPoC_Dcache` cannot match).

## 7. Safety

No device action. `grep`/`sed` over `entry_trace.c`, `entry_stubs.c`, the two docs and the captured log;
`bash -n`; ten `--summarise` runs over logs in `/tmp`; one `git show` of the previous reader run against
state J; one read-only gate run (exit 0). Nothing written outside `docs/`,
`stages/stage90/run_and_capture.sh` and the memory files; no build run; no file under `out/` touched;
`flash` not used, nothing written to storage. Frozen pair untouched (`1daaf44e624563694e…` /
`f202f2465886aba6…`), and the device is off the bus awaiting a power press.
