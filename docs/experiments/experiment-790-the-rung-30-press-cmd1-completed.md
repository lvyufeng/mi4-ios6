# 790: the rung-30 press — CMD1 COMPLETED, and the 1.2 s "timeout" every arm since rung 14 reported was this ladder's own mask

**ONE PRESS SPENT, AND IT WAS AUTHORIZED BY THE OPERATOR BEFORE THE GATE WAS RUN.** One gate (exit 0),
one runner (EXIT 0), one `fastboot boot`, nothing flashed, nothing written to storage, no reboot.
`out/` moved: the arm in it is rung 30's and it is now **spent**. This step builds nothing and edits
no source.

| | |
| --- | --- |
| arm | `armed-storage-dfc4ae78`, `STAGE90_XNU_STORAGE_PROBE=29` (switch **value 29 = ordinal rung 30**) |
| boot image | `out/stage90/stage90-qcdt.img` sha256 `8c86586a…` |
| fired | **2026-09-27 23:43:48 – 23:45:03 UTC** (75 s gate-to-capture), EXIT 0 |
| gate | `scripts/preflight_boot_check.sh` with the flags readiness printed — **exit 0, exactly one run, 606 lines** |
| runner | `scripts/run_and_capture.sh` with the same flags and `--expect-arm=armed-storage-dfc4ae78` — **exactly one run** |
| capture | `out/stage90/captures/rung30-c1win-20260927-234510-last_kmsg.txt`, 636,238 B, sha256 `af710630a189b0dad84965482b1fd6b80e841144c8020b5f8135d88e09e97bd3` |
| archived by hand | `…-last_kmsg.txt`, `…-gate.log`, `…-run.log` |
| `33e80afe` | absent from `adb devices` **and** `fastboot devices` at fire time |

**The runner's verdict is 7 PASS / 1 FAIL / 3 UNREAD — eleven criteria, printed TWICE in one log** (the
gate's reading of the log as it stood at gate time, which is the previous capture, and the runner's of
this one). Counting log **lines** gives 14 / 2 / 6 and is the arithmetic 756 recorded as wrong; the
criteria are named, not counted. The single FAIL is the known `seam_sp … is not sleh_sp-8` criterion and
the three UNREADs are the known absent-key ones — **identical to rung 29's verdict, so this press added
no failure.**

## 1. The headline, and it is 787 §3's first row and no other

`_cmd1_complete = 1`. `_cmd1_status_any = 0x00000001` — **bit 0, `SDHCI_INT_RESPONSE`, and no error bit
of any kind.** The poll broke on the driver's own condition at poll 1,236 instead of running its bound.

| key | rung 29 (window **closed**) | rung 30 (window **open**) |
| --- | --- | --- |
| `_cmd1_complete` | `0x00000000` | **`0x00000001`** |
| `_cmd1_status_any` | `0x00000000` | **`0x00000001`** |
| `_cmd1_any_polls` | `0x004da000` = **5,087,232** | **`0x000004d4` = 1,236** |
| `_cmd1_ticks` | `0x015f9e16` = 23,045,142 | **`0x00002823` = 10,275** |
| `_cmd1_timeout` | `0x00000001` | **`0x00000000`** |
| `_cmd1_err` | `0x00000000` | `0x00000000` |
| `_cmd1_stale` | `0x00000000` | `0x00000001` |
| `_cmd1_clear_wrote` / `_cmd1_clear_after` | `0x00000000` / `0x00000000` | `0x00000001` / `0x00000000` |
| `_cmd1_cmdlow_seen` | `0x28a` = 650 of 1024 | `0x28b` = 651 of 1024 |
| `_c1_ena_wrote` / `_c1_ena_wrote_back` | *(key did not exist)* | **`0x000f0001` / `0x000f0001`** |
| `_c1_ena_held` / `_c1_ena_readback` | *(key did not exist)* | **`0x000f8001` / `0x000f8001`** |

**The four rows of 787 §3, read against what came back:**

- **bit 0 visible — this is the row, and it is the one 787 said changes what the next arm is.** CMD1
  completed and the block latched it.
- `ERR | TIMEOUT` (`0x00018000`) — **not** this row.
- CRC / END_BIT / INDEX — **not** this row. With all four error bits enabled the block raised **none**
  of them.
- nothing at all — **not** this row.

**So the stall was this ladder's own mask, at the one command window where the ladder has been reading
"the block raised nothing" since rung 16.** The `_c1_ena_held = 0x000f8001` and `_c1_ena_readback =
0x000f8001` are the block's own copy of the word the window wrote and put back — the same value rungs 23
and 24 read at CMD3 and CMD2 — and `_c1_ena_wrote == _c1_ena_wrote_back == 0x000f0001` is a **reading**,
not two evaluations of one expression, which is what 787 §2 built the two bodies to make it.

## 2. The control that makes §1 a measurement and not a coincidence

`_cmd0_ticks` — the same boot, the same bus, the same clock, the one command this ladder has ever seen
complete with **no response demand** — reads:

| | `_cmd0_ticks` | `_cmd1_ticks` | ratio |
| --- | --- | --- | --- |
| rung 29 | `0x1327` = 4,903 | `0x015f9e16` = 23,045,142 (its bound) | — |
| **rung 30** | **`0x132a` = 4,906** | **`0x2823` = 10,275** | **2.094** |

**CMD0's duration reproduces across two arms to three ticks — 0.06 per cent.** So the tick base is stable
between the arms, and a 2.09× ratio at CMD1 is not timer noise.

**CMD1 takes 2.09 times as long as CMD0.** CMD0 is one 48-bit command frame with nothing coming back;
CMD1 is the same shape with `RESP_PRESENT` set. **A ratio near 2 is what a command plus its response
looks like beside a command alone** (one extra frame gives exactly 2.00; the 0.094 residual is the
overhead the two commands do not share).

**This is an argument and not a proof, and the model is named rather than hidden**: with two points,
`t = N × frame + overhead` cannot be solved for `frame` and `overhead` separately, so the ratio is
consistent with "+1 frame" without fixing the frame length, and a block whose completion latency simply
differs for a response-demanding word would also produce a ratio above 1. What the ratio does rule out is
a CMD1 that took *no* time at all or the same time as CMD0. **Named, and it is the cheapest thing a
future arm could pin.**

## 3. The companions, and the two cells that did not move

**(a) CMD2 is unchanged — every cell but the inhibit.** `_cid_status_any = 0x00028000` (`ERR | CRC`) at
`_cid_any_polls = 0x00000001`, `_cid_sent = 1`, `_cid_complete = 0`, `_cid_err = 0x00020000`,
`_cid_ticks = 0x00000011` — **byte for byte what rung 29 read.** The CMD1 window left no trace on CMD2's
own status. **The frontier moved one command down, and it did not move at CMD2.**

**(b) CMD3 was NOT SENT on this arm — and rung 29 sent it.** `_nidx_sent = 0`, `_nidx_word = 0`,
`_nidx_arg_wrote = 0`, `_nidx_polls = 0`, `_nidx_status_any = 0`, `_nidx_ticks = 0`, while
`_nidx_calls = 1` and `_nidx_gated = 0` — so `st_cmd3_noidx` **was called** (its window opened:
`_nidx_ena_wrote = 0x000f0001`, `_nidx_ena_held = 0x000f8001`, `_nidx_status_pre = 0x00028000`) and the
call into `st_send_command` **returned before it stored anything**.

**The cause is readable, and it is the driver's own guard.** `st_send_command`
(`src/entry/entry_storage.c:2545-2558`) waits for `SDHCI_CMD_INHIBIT` to clear with the driver's own
bound (`sdhci.c:1096`, `timeout = 10`), and on the bound expiring it does:

```c
if (r->inhibit_timeout != 0u) {
    r->ps_after = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
    return;                                  /* sent stays 0: no command was issued */
}
```

**The evidence it is THAT return and not the latch guard two dozen lines below** (§`clear_after & CMD_MASK`)
is `_nidx_clear_after = 0x00000000`: the second guard returns **only** when `clear_after` is non-zero, so
a zero there rules it out by construction. The latch guard's return would also have `_nidx_clear_wrote`
set, and `st_cmd3_noidx` publishes no `_nidx_clear_wrote` at all.

**The block was still inhibited by CMD2, ten milliseconds after CMD2's window closed.** And that is where
rung 29 and rung 30 differ:

| key | rung 29 | rung 30 |
| --- | --- | --- |
| `_cid_inhibit_after` | `0x00000000` | **`0x00000001`** |
| `_cid_inhibit_last` | `0x01f80000` | **`0x01f80001`** |
| `_cid_inhibit_seen` | `0x00000000` | **`0x00000001`** |
| `_cid_ps_after` (two producers) | `0x01f80000` / `0x00f80000` | **`0x01f80001` / `0x00f80001`** |

**So on rung 30, CMD2 wedged and stayed wedged**, and the difference is the *only* thing about CMD2 that
changed. **A cell this arm cannot explain**, and it is named rather than smoothed over: the two arms'
CMD2 status words are identical and their command words are identical, so the inhibit's persistence is
not a consequence of anything this log measures. The candidate is the state CMD2 was entered in — on rung
30 CMD1 had actually completed, on rung 29 it had "timed out" — and no cell in either capture
discriminates that from a bare timing difference.

**(c) One cell that would have made (b) directly readable is not published.** `st_cmd3_noidx`'s publisher
block (`:4091-4116`) carries `c3.timed_out` as `_nidx_timeout` but **not** `c3.inhibit_timeout`. So the
reason for the refusal must be inferred from `_nidx_clear_after` as above, rather than read. **That is
owed.**

## 4. A new `PRESENT_STATE` word, and it is the one 789 §1's census was missing

789 §1 took a census of all 148 `PRESENT_STATE` words in the archive and found exactly **three** values,
in which `CMD_INHIBIT` (bit 0) and the CMD line's level (bit 24) never appeared together:

| word | 789's count | inhibit | CMD line |
| --- | --- | --- | --- |
| `0x01f80000` | 142 | 0 | HIGH |
| `0x01f80001` | 5 | **1** | HIGH |
| `0x00f80000` | 1 | 0 | **LOW** |

**Rung 30 adds a fourth: `0x00f80001` — `CMD_INHIBIT` SET and the CMD line LOW at one instant.** It is
`_cid_ps_after`'s **first** producer (the block's view at the end of CMD2's poll), and it is the first
word in the archive that says *the block is holding a command in progress while its own CMD line is being
driven down* — the in-flight signature 789 §1 could not find, and 789 §3 could only approach from one
window later.

**789 §3's wedge is therefore measured on a second arm, and it reads stronger on this one.** 789 had to
reach into CMD2's post-window read to find the low line; here the block reports the low line *inside* the
poll, with the inhibit bit beside it.

## 5. `_cmd1_resp` is `0x40ff8080` on every arm, including the two whose window was closed

| arm | `_cmd0_resp` | `_cmd1_resp` | CMD1's window |
| --- | --- | --- | --- |
| rung 19 | `0x00000000` | `0x40ff8080` | closed |
| rung 29 | `0x00000000` | `0x40ff8080` | closed |
| **rung 30** | `0x00000000` | `0x40ff8080` | **open** |

**The value is identical on all three.** So it is *not* this arm's new reading, and 787 §6's warning holds
in the stronger direction: a non-zero `_cmd1_resp` is not evidence the card answered — **and it is now
also not evidence that anything this arm changed had any effect on the response registers.**

**What the sequence does say, and it is enable-independent.** `_cmd0_resp = 0` is a reading (`_cmd0_resp_read
= 1`), taken before CMD1. `_cmd1_resp = 0x40ff8080` is a reading, taken after CMD1's poll. The ladder never
writes the `RESPONSE` registers. **So the word entered them during CMD1 — on all three arms, including the
two whose poll could not see the completion.** That is a second, structural support for §1's row from a
cell that does not depend on any `INT_ENABLE` bit, and rung 19 puts a third point on it: its
`_rca_resp_pre = 0x40ff8080` is read **before** CMD3 is on the bus.

**And it still does not make `0x40ff8080` an answer.** The register is a shared word that three different
commands read the same value out of, the ladder never brackets CMD1's own registers with a fresh pre/post
pair — `_cmd1_raw_pre*` / `_cmd1_raw_post*` **do not exist**, unlike CMD2's `_cid_raw0..3` and CMD3's
`_nidx_raw_pre*` / `_nidx_raw_post*` — and `_cmd1_resp_voltage = 0x00ff8000` is OCR bits 23:15 all-ones,
which is a legal full voltage window and equally what a static pattern looks like. **The pre/post pair
around CMD1 is the cheapest addition a future arm can make, and it is owed.**

## 6. 786 §4 and 787 §4: one refuted, one confirmed for the wrong reason

**786 §4's conclusion** — *"CMD1's row would not have moved at all"* — is **refuted**. The row moved, from
zero to bit 0.

**787 §4's candidate (b)** — *"bit 15 rises only when the specific error bit is enabled, so CMD1 may have
been timing out invisibly on every arm from rung 14 on"* — is **refuted by this press**: the window
carried all four error bits enabled (`_c1_ena_held = 0x000f8001`) and CMD1 produced **no error bit**. There
was no invisible timeout.

**787 §4's candidate (a)** reached the right consequence — *"then no error occurred at CMD1 and only bit 0
(the completion) can be new"* — but **on a mechanism 788 §3 had already refuted**: (a) says bit 15 rises
with any error whatever its own enable, and 788 §3 measured 30,537,728 zero reads with bit 15 standing in
`INT_ENABLE`. **So the account that actually fits is neither (a) nor (b) as written: it is 788 §3's — the
narrow window was blind — carried to the CMD1 window.**

**Which leaves 786 §4 right about the outcome and wrong about the reason**, and it is worth saying plainly
because it is the third time this ladder has reached a correct conclusion from a mechanism that a later
arm killed.

## 7. What the next arm's subject is

**The question is no longer why CMD1 stalls — it does not.** It is **why CMD2 gets an immediate CRC while
CMD1 completes**, and this press hands the next arm a contrast it did not have:

| | CMD1 | CMD2 |
| --- | --- | --- |
| command | `SEND_OP_COND` (1) | `ALL_SEND_CID` (2) |
| flags | `PRESENT` = `0x02` | `PRESENT \| 136 \| CRC` = `0x07` |
| word | `0x00000102` | `0x00000209` |
| outcome | **completed, poll 1,236, no error** | `ERR \| CRC` at poll **1**, `_cid_ticks = 0x11` |
| response demand | 48-bit, **no CRC** | 136-bit, **CRC** |

Three things differ and only three: the **response length** (48 vs 136 bits), the **CRC check** (off vs on),
and the **opcode**. CMD2's error arrives at poll 1 — 17 ticks, ~0.9 µs after the command store, in a window
whose 136-bit response cannot possibly have been shifted — which is 786 §7's anomaly and 788 §7's owed
item, and it is now the cheapest thing in the ladder to pin.

## 8. What this does not do

- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld.**
- **It does not send CMD3** (§3b). The ladder's last command did not run on this arm.
- **It does not read a card answer** (§5). The completion is established; the response word is not.
- **It does not touch the PMIC, the rails, the clocks, the DLL, the pad register, or any address outside
  `INT_ENABLE 0x34`.** 784's rail reading stands: the eMMC's two rails are RPM resources with no `reg`.
- **It builds nothing and edits no source.**
- **No press is authorized.** Rung 30 is spent and no further arm is armed.

## 9. Owed, and named rather than left to be inferred

- **`c3.inhibit_timeout` is not published for the `nidx` family** (§3c) — the cell that would make CMD3's
  refusal directly readable instead of inferable. COST-owed to a build.
- **A `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair around CMD1's own `RESPONSE` registers** (§5) — the one
  measurement that would turn `0x40ff8080` from a shared word into a reading. COST-owed to a build.
- **The CMD2/CMD3 CRC's timing** (§7, 786 §7, 788 §7) — an error reported at poll 1 of a window whose
  response cannot have been shifted. Named three steps running and still undecided; §7 makes it the
  next arm's subject.
- **Why CMD2's inhibit persisted on this arm and cleared on rung 29** (§3b) — two identical CMD2 status
  words, one different inhibit tail, and no cell that discriminates.
- **786 §4's conclusion and 787 §4's (a) and (b)** — §6 supersedes all three as statements; the
  superseding text is carried into `tools/verify_press_ready.sh`'s rung-30 paragraph, which is the
  narration the next press prints.
- Unchanged from 787–789: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST); the four
  `5,088,000`s and the mis-citation at `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE`
  sites per key (789 §4); the `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5);
  `_cid_ps_after`'s second producer (789 §2, COST); the set-comparison pad repair (779 §7); the
  `rung_para` correction for values 12..23; the seam-address class; `run_and_capture.sh`'s
  `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6); 784's `rail_name` cell;
  and 783's window-scope check — landed at rung 30's window and not at the ladder's other windows.
