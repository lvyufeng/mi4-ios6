# 783: the mask the ladder found was lifted at one of three windows — CMD1's and CMD2's still run with the one-bit enable

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was not touched and no entry source was edited, so the armed rung-28 arm
(`armed-storage-d5d98738`, `STAGE90_XNU_STORAGE_PROBE=27`), every park and every capture are
byte-identical before and after. One press spent by this step: **none**. This step is a reading of the
**repository's own source** — not of a gitignored input — and it is computable by anyone with a clone.

## 1. The sentence this step follows, and where it stops

765 §1 measured that the vendor's own `sdhci_init` writes **eleven** bits (`0x01FF0003`) into
`INT_ENABLE 0x34`, and drew the conclusion the rung-23 arm was built on:

> if the latch is enable-gated, a poll over a register whose error-enable half is zero cannot see an
> error bit however many samples it takes, **and the ladder has NEVER ONCE set an error enable.**
> … `_status_any = 0` is cited in five documents as *the block said nothing*; it measures *no enabled
> bit was latched*.

Rung 23 acted on it: the `st_cmd3_noidx` window writes `ST_SDHCI_INT_ENABLE_CMD = 0x000F0001` — the
completion with TIMEOUT, CRC, END_BIT and INDEX — and the rung-24 press confirmed the rule, because the
CMD3 row that had read `_status_any = 0` over 5,088,256 polls now read **`0x00018000`** with a break at
poll 1789 and a 665.2 µs timeout signature.

**What has never been written is the scope.** The ladder issues **three** response-demanding commands,
each with its own enable window, and the widening was applied to **one** of them.

## 2. The window census, computed from the source and now in `make check`

`tools/report_int_enable_windows.py` (added here) enumerates every store to `0x34` and resolves each
one's word **by name** through the assignment that builds it — never by line number, because the
assignment and the store are different lines by construction. Its own output, at the current rung:

| window | function | command(s) | word written |
| --- | --- | --- | --- |
| `:4317` | `st_cmd_path` | **CMD0, CMD1, CMD2** | `int_enable \| ST_SDHCI_INT_RESPONSE` → **`0x00000001`** |
| `:3110` | `st_all_send_cid` | **CMD2** | `int_enable \| ST_SDHCI_INT_RESPONSE` → **`0x00000001`** |
| `:3981` | `st_cmd3_noidx` | CMD3 | `ena` → **`0x000f0001`** (built at `:3976` under `#if >= 22`) |

and the summary line the tool prints:

```
the FIVE-BIT word (0x000f0001) is built at 1 line(s): st_cmd3_noidx:3976 (>= 22)
the ONE-BIT word (0x00000001) is built at 2 line(s): st_quiet_enable_probe:3016 (>= None), st_cmd3_noidx:3978 (>= 22)
and the FIVE-BIT word reaches 1 store(s), all in: st_cmd3_noidx
```

`st_set_relative_addr` (rung 19) and `st_cmd3_noresp` (rung 20) also write the one-bit word; both are
rungs the armed arm does not contain, and they are in the census so the table is the whole reading.

## 3. What that means for the armed arm's press

**In the rung-28 arm, CMD1's and CMD2's windows still run with `INT_ENABLE = 0x00000001`.** So:

- `_cmd1_status_any = 0` and `_cid_status_any = 0` over their full 1.2 s windows are **still the masked
  readings 765 §1 described** — on this arm the mask was lifted for the last command and left standing
  for the two that run first.
- **CMD1 is the one that matters most for the goal**: it is `SEND_OP_COND`, the command an eMMC must
  answer for the ladder to move at all, and the ladder has spent many presses on its 1.2-second silence.
  Its window is the one whose error half is still zero.
- **The press the operator is holding would answer the pad question and the CMD-line counts and would
  leave the CMD1 timeout question exactly where rung 21 left it.** That is not a reason not to spend it
  — row 1 of the pad fork closes a candidate on its own — but it is the reason to know, before the press,
  that the arm asks two questions and not three.

**And the fix is one constant wide in the same sense rung 23 was.** `st_cmd_path`'s store and
`st_all_send_cid`'s store would each take `int_enable | ST_SDHCI_INT_ENABLE_CMD` under a new `#if`
threshold, and then **one press answers the pad question, the CMD-line counts, the CMD3 row and the
CMD1/CMD2 latch question together.** It is a build, which moves `out/` and invalidates the armed arm, so
it is the operator's decision and not this step's.

**Is the CMD1 question already answerable from the archive, making the rebuild unnecessary?** Answered
here, cell by cell, and the answer is **no**:

- `_cmd1_status_any = 0` — a masked read (§2's rule), uninformative.
- `_cid_stale` (INT_STATUS as found at CMD2's entrance, read after CMD1's poll gave up) reads
  `0x00000000` on the rung-24 capture. On this block a bit latches only while its enable stands, and
  CMD1's window had the error half at zero — so a zero here is **consistent with a timeout that could
  not latch** and separates nothing.
- `_nidx_status_pre = 0x00018000` (bit 15 + bit 16) is read at CMD3's entrance, i.e. after **CMD2's**
  window as well, and the record already calls it UNSEPARATED. It does not reach back to CMD1.

So the ladder cannot say whether CMD1 armed and fired its own response timeout, and **no press of the
armed arm can say it either.**

## 4. What was landed, and what it refuses

| artifact | what it is |
| --- | --- |
| `tools/report_int_enable_windows.py` | the census of §2: the assignments that build a word (with the `#if` threshold that guards each), and every store to `0x34` with what it writes — resolved by name, printed as a table |
| `make check` | runs the census and its `--selftest`; so **the scope of every widening is printed on every check** rather than being something a reader has to reconstruct from three functions |

**One refusal, and it is about a defect rather than a choice**: a store to this register whose value is a
**bare literal** is refused, because a literal there is one value with two definitions and the second is
the one nobody re-derives when the first moves. Four self-test cells pin it (a named word passes, a
literal and a cast-named word are classified apart, an unknown constant passes as *not a word this tool
knows* rather than being silently taken for a word).

**And one defect of this tool's own, found by running it, is worth recording because it is the class the
project keeps finding.** The first version classified a store by *substring*: `'int_enable' in expr`. But
`int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE` **contains** `int_enable`, so every command window was
called a passthrough and the census printed **zero** five-bit windows — a tool built to report the scope
of a widening, reporting that the widening had not happened. The repair is per-operand: split on `|`,
strip casts, and ask what each operand is. That is the same shape as 778's count defect and 779's board
constant: **a reading that is a property of the instrument.**

## 5. What this does not do

- **It does not change the arm.** Not a cell, not a store, not a byte; the guard, the pad fork and the
  CMD-line counts are what 782 left them.
- **It does not say the CMD1 row's silence is a timeout.** It says the reading that would have shown one
  was not enabled, which is weaker and is the whole point: *the controller had nothing to report* and
  *the controller's error reporting was switched off* are indistinguishable on that window.
- **It does not propose a rung and does not authorize a build or a press.** The rebuild that would
  widen all three windows moves `out/`, and that is the operator's call.
- **No press is spent and none is authorized.**

## 6. What the next press is, in the two shapes it can take

| | what it answers | what it costs |
| --- | --- | --- |
| **press `armed-storage-d5d98738` as it stands** | `_pad_raw` as a four-row reading (782); `_pad_write_skipped`/`_writes`/`_wrote`/`_after`/`_after_match`; `_cmd1_cmdlow_seen`, `_cid_cmdlow_seen`, `_cmd0_cmdlow_seen`, `_cid_inhibit_after`; the CMD3 row and rung 23's window | one press; the CMD1/CMD2 latch question stays open |
| **build one arm that widens all three windows, then press it** | all of the above **plus** whether CMD1 and CMD2 arm and fire their own response timeouts — the masked reading made visible on the two rows it has always hidden | one build (**moves `out/`**, so the armed arm is superseded unspent) and one press |

**Both are one press.** The second is a strictly larger experiment for a build the operator already owns
in the sense that the change is one constant at two stores, and it is put here so the decision is made
with the scope known rather than discovered after the log comes back.

## 7. Owed, and named rather than left to be inferred

- **The scope of the widening is now printed and not enforced.** A check that refuses a build whose
  window scope differs from the scope the arm's own record declares is the next tightening; it needs the
  scope written down somewhere the check can read, and this step deliberately does not invent that place.
- Unchanged from 775–782: the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303`
  (COST-owed, to be carried by a build); the set-comparison pad repair (779 §7, pre-registered, not
  built); the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; the 737 window paragraph (paid by the rung-28 build);
  and `fdt_nodes`'s lack of a synthetic FDT cell (782 §6).

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, and **times out because the card does not answer** —
and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
