# 730: the rung-14 press — the completion was there all along, and `INT_ENABLE` was the mask

**PRESSED 2026-09-26 07:38:48–07:39:59 UTC.** RUNNER EXIT **0** — returned and captured, **71 s** end to
end. Readiness 5/5 (exit 0), one gate (exit 0, 569 stdout lines) and exactly one runner, all from the
same tree under `--allow-xnu-entry --expect-arm=armed-storage-completion-6f49e880`. One non-persistent
`fastboot boot`; nothing flashed; no byte of the medium touched. The neighbour `33e80afe` was off
**both** lists (`adb: [4a2fe00b device]`, `fastboot: []`) and was never signalled.

The capture is `out/stage90/captures/rung14-completion-20260926-074001-last_kmsg.txt`, **626,111 B,
sha256 `c5d0aeee6cfaadd1d029c65aaca1dcf900c42f1b021afd8d97ed3f464eb23d00`**, archived by hand (out/ is
gitignored and the runner does not archive its own capture). The record is the `armed-storage-completion-6f49e880`
press block in `stages/stage90/revert-set.txt`.

**The one cell the whole rung was built for is `xnu_live_storage_int_status_after = 0x00000001`.** All
fourteen of the arm's cells, in program order, exactly once each:

| # | cell | value | what it is |
| --- | --- | --- | --- |
| 1 | `_int_calls` | `0x00000001` | the body ran once |
| 2 | `_int_slot_status` | `0x00000000` | `SLOT_INT_STATUS 0xFC` (i) |
| 3 | `_int_pwrctl_status` | `0x00000000` | `CORE_PWRCTL_STATUS 0xDC` (i) |
| 4 | `_int_cmd_word` | `0x00000000` | `COMMAND 0x0E` (i) — the block still holds the driver's word |
| 5 | `_int_present` | `0x01f80000` | `PRESENT_STATE 0x24` (i) |
| 6 | `_int_present_inhibit` | `0x00000000` | its `CMD_INHIBIT` — no command in flight |
| 7 | `_int_status_before` | `0x00000000` | `INT_STATUS 0x30` with both enables clear |
| 8 | `_int_enable_before` | `0x00000000` | `INT_ENABLE 0x34` |
| 9 | `_int_sig_enable` | `0x00000000` | `SIGNAL_ENABLE 0x38` |
| 10 | `_int_enable_wrote` | `0x00000001` | `SDHCI_INT_RESPONSE` |
| 11 | **`_int_status_after`** | **`0x00000001`** | **the answer** |
| 12 | `_int_enable_held` | `0x00008001` | the readback — and bit 15 (§5) |
| 13 | `_int_enable_restored` | `0x00000000` | the restore's own value |
| 14 | `_int_enable_readback` | `0x00008000` | what the block kept |

## 1. The same command, the same register, one store between the two reads

Cells 7 and 11 are reads of `INT_STATUS 0x30` **at the same point in `st_cmd_path`** — immediately after
CMD0's publishes, before the gate — separated by exactly one store: cell 10, `INT_ENABLE 0x34 <-
0x00000001`. Cell 7 reads `0x00000000`; cell 11 reads `0x00000001`, which is
`SDHCI_INT_RESPONSE`, the Command Complete bit. Nothing else happened between them: the body makes no
other store, issues no command and waits on nothing.

So the hypothesis rung 13 left standing — *the status bit latches only if its enable is set* — is
**confirmed by construction**. The command's completion was pending in the block the whole time, and the
enable is what makes it readable.

## 2. Why rungs 11, 12 and 13 saw nothing

* `_int_enable_before = 0x00000000` and `_int_sig_enable = 0x00000000` **at the command's own moment**.
* The rung-12 census read the same two registers from an **earlier** moment in the same log and got the
  same answer: `_cmd2_int_enable = 0x00000000`, `_cmd2_sig_enable = 0x00000000`. Two readings, two
  moments, one before the command was issued and one after — **nothing in this image has ever set
  either enable**, which is why 726's `_cmd0_status_any = 0x00000000` over 5,088,256 reads and this
  log's `_cmd0_status_after = 0x00000000` are the same reading taken twice.
* `st_send_command` takes completion from a poll of exactly this register (`entry_storage.c:2209`:
  `r->complete = ((r->status_after & ST_SDHCI_INT_RESPONSE) != 0u) ? 1u : 0u;`), after clearing it
  write-1-to-clear (`:2119-2122`). **A poll of a masked register is a poll that cannot succeed**, and
  that is all rungs 11–13 were measuring.
* The two registers 728 §1 named as cheaper alternatives answered nothing: `_int_slot_status = 0` and
  `_int_pwrctl_status = 0`. Rung 3's and rung 8's readings of those registers are before-values in this
  same log (m737 corrected the two documents that claimed they had never been read at all), so the pair
  is a comparison and not an assertion.

## 3. The pre-registered failure mode did not happen, and the ending did not move

* **No `_irq_other_count` and no `_irq_other_iar` key exists in this log.** `SIGNAL_ENABLE 0x38` was
  read and never written, the SPI 123 → intid 155 line never rose, and the run did not end at the
  dispatcher. 728 §2's claim that the hypothesis is testable *without putting the line in play* is now a
  measurement rather than a reason.
* The ending is 726's ending, cell for cell: `_post_end_calls = 0x00000007`, `_sleh_storm = 0x00000009`,
  `_seam_post_end_ticks = 0x06ddd000`, `_seam_sctlr = 0x30c57879`, and the seam pair still reads as the
  CLEAN LINE 686 measured to be a DRAM reading.
* **Inherited cells are unchanged**: `_cmd0_word_read = 0x0000`, `_cmd0_sent = 1`, `_cmd_gated = 1` with
  **zero** `_cmd1_*` keys, and the whole `_cmd2_*` census as 726 left it. The only storage-key
  differences between the two logs outside the new family are per-run jitter —
  `_cmd0_polls` `0x004da800` → `0x004da400`, `_cmd0_inhibit_seen` `0x21b` → `0x219`,
  `_pwr_wait_ticks` `0x592` → `0x598`, `_clk_set_cc_polls` `0x11` → `0x12`. **No semantic cell moved.**
* The userland floor the runner prints (open/read/getpid/exit/wait, 504's reading re-read here) is met
  again. It is the floor: all of it is in 520 and 533 too, and having it says only that the OS still
  boots to pid 1's syscalls.

## 4. The one cell that came back the other way: bit 15 of `INT_ENABLE`

`_int_enable_held = 0x00008001` — the readback one instruction after the store carries **bit 15**, which
`_int_enable_before = 0x00000000` says was clear one body earlier. And `_int_enable_readback =
0x00008000` after the restore: **the write of 0 cleared bit 0 (so the store took) and left bit 15 set**.

So on this block bit 15 of `0x34` is (a) **not** a fixed-to-1 read-only bit — the before-value refutes
that — and (b) **not** clearable by writing 0 to the register.

Two readings are open and this press does not choose between them:

1. bit 15 is a **sticky latch** the block sets when an enabled status first pends, and it survives the
   enable going away;
2. bit 15 has **write-1-to-clear** semantics that a 0 write cannot reach — in which case the register's
   upper half is not an enable at all.

**The safety contract survived either way.** The arm's contract was *the store*, and
`_int_enable_readback` exists precisely so that the block's own answer is published rather than the
arm's intention (rung 12 added `_cmdN_word_read` for the same reason). The run then behaved exactly as
the untouched arm did, ending included.

**Owed, and cheap — one read each:**

* **read `INT_STATUS` again *after* the restore.** Reading 0 says the status is gated by the enable and
  the AND-model is complete; reading 1 says the bit is a **latch that outlives its enable**, which is a
  different block from the one the spec describes.
* **read `HOST_VERSION 0xFE` beside it.** The 32-bit combined `0x30`/`0x34`/`0x38` layout is the SDHCI
  **3.0** alternative to the 2.0 split pairs (0x30 normal status, 0x34 error status, 0x36/0x3A/0x3C
  enables). This press is strong evidence for 3.0 — a write to `0x34` changed what `0x30` reads — but
  the version register is the reading that names it, and it has never been read by this ladder.

## 5. What this does to the ladder, and it is the whole point of the press

**`_cmd_gated = 1` is no longer a statement about the block.** The gate is
`if (c0.sent == 0 || c0.complete == 0 || c0.err != 0)` (`entry_storage.c:2609`), and `complete` is the
poll. The poll read a masked register, so `complete` was 0 for a reason that has nothing to do with the
controller. **The next rung is the same command path with `INT_ENABLE` bit 0 set around it and the wait
reading a register that can now answer — with `SIGNAL_ENABLE` still at zero, which is the one thing this
press proved is safe.** The cell that says it worked is `_cmd_gated = 0` with `_cmd1_*` keys present
for the first time in this ladder, i.e. **the first command this line drives to a completion and the
first CMD1 it ever issues**.

Two constraints on that rung, both from this log:

* rung 11 already writes `INT_STATUS 0x30` write-1-to-clear (`:2120`), so the new arm adds **no new
  write class** — only the enable store and its restore;
* the enable must be **restored** before the run continues past `st_cmd_path`, and the restore's cell
  must be published, because §4 shows the block does not fully undo it.

## 6. The firer is gone, and that is a defect in the record rather than a choice

This press was fired by the two commands readiness prints, run directly, exactly once each. **No launcher
was armed**: `/tmp/g668/press-on-clear.v5.sh` — the firer whose bytes every press since 671 was fired by,
cited by **sixteen** experiment documents, the index and `revert-set.txt` fourteen times — **no longer
exists**. `/tmp/g668` and `/tmp/r654` (its `press.log`) are gone from this host and
`find / -name 'press-on-clear*'` returns nothing, so **the bytes that sha256 `7f23cae5…` stands for are
kept nowhere** in this repository, this export or the job tree.

A sha whose bytes are not kept is not a check — it is a claim about a file that can never be produced
again (`mi4-a-claim-in-a-comment-is-not-a-check`), and here the claim is about *the mechanism that fired
fifteen recorded presses*. The two commands readiness prints **are** the discipline (661 R1); the
launcher was a wrapper with its own rehearsal (the 27-row `rehearse-v3.sh` and `rehearse-wait.sh`
harnesses, 672 §4) that died with the same job tmp and so could not be re-run this step.

**Owed:** either recreate a launcher *with* its rehearsal and record its bytes, or record that the press
path is two commands and retire the launcher from the record. Either way the sixteen citations stay
where they are — this file's rule is that a later step supersedes a sentence rather than editing it.

## 7. What was verified

| what | how | reading |
| --- | --- | --- |
| readiness | `tools/verify_press_ready.sh`, run from the same tree immediately before the fire | **5 of 5, exit 0**; the arm found by hashing the live `stage90-qcdt.img`, the flag set `--allow-xnu-entry` derived from the arm's own switches |
| the gate | exactly one `preflight_boot_check.sh --allow-xnu-entry` | exit 0, **569** stdout lines |
| the runner | exactly one `run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-completion-6f49e880` | **exit 0**, 71 s, capture returned |
| the bytes sent | the runner's own declaration | `ok: declared arm 'armed-storage-completion-6f49e880' == the recorded set of the bytes to be sent`; 11/11 files of `out/` match; the other 22 recorded sets do not |
| the archive | `cp -p` by hand, then hashed | 626,111 B, sha256 `c5d0aeee…`, in `out/stage90/captures/` |
| the neighbour | both device lists before the fire | absent from adb and fastboot, never signalled |
| the device | `adb devices` after the run | `4a2fe00b device` — back and alive |

## 8. What the rung is not, and where the goal stands

The rung is **one body, two stores to one register, and fourteen readings**. It issues no command, writes
no `ARGUMENT`/`COMMAND`/`RESPONSE`, touches no data-path register, makes **no** `POWER_CONTROL 0x29`
store (that byte is not touched at all by this body), writes no GCC word and no `core_mem` word, and
moves no byte of the medium. `SIGNAL_ENABLE` is read and never written. `st_cmd_path`'s and
`st_cmd_census`'s clauses are rung 11's and rung 12's, unchanged.

**And the goal is still not met.** The ladder now knows *where this controller reports a completion* and
that the report was being masked — that is a real step, because the next rung can drive a command to a
completion for the first time. But XNU is not normally loaded, the OS has not been entered, there is no
partition table, no mounted volume and **no driver beyond the fixture**, so 「如果os已经能进去了的话」 is
not triggered and **TWRP-to-storage stays withheld**.

The pre-registration this fills is
`docs/experiments/experiment-728-the-rung-14-pre-registration-where-this-controller-reports-a-completion.md`;
the arm was built and parked in `docs/experiments/experiment-729-the-rung-14-arm-built-and-parked-where-this-controller-reports-a-completion.md`.
