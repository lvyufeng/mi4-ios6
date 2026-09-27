# 777: the cell 775 built cannot take the reading it was built for — and the row that was never in its answer space

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER. NOTHING IN
`out/` MOVES: this step builds nothing.** `out/` still holds **`armed-storage-d5d98738`**
(`STAGE90_XNU_STORAGE_PROBE=27`, ordinal rung 28), **ARMED AND NOT PRESSED**, and the armed arm is
**byte-identical before and after this step** — the finding below is about a *sentence*, and the sentence
is correctable without touching the artifact.

This is a re-reading of a claim 775 made about a cell that **is in the armed arm**, checked against the
three `file:line`s the claim rests on. It is 774's shape: no register is re-measured, one claim is.

## 1. What 775 claimed, in its own words

775 §2 (`docs/experiments/experiment-775-the-rung-27-arm-the-rest-of-the-cmd2-result.md:42-45`) and the
gate's narration for value 26 (`tools/verify_press_ready.sh`, the `wst == 26` branch and `rung_para 26`):

> **`_cid_stale` is that reading taken one command earlier.** It is `INT_STATUS` as found at **CMD2's
> entrance** — the first read of that register since CMD1's poll gave up 1.2 seconds earlier — and the
> interval it sits in contains **no write to `0x34` at all**. A non-zero there is CMD1 doing something
> after its poll ended; a zero is CMD1 doing nothing.

The claim has two halves: a **reason** (*no write to `0x34` in the interval*) and an **answer space**
(*non-zero* vs *zero*). **The reason is false, and it is the reason that was doing the work.**

## 2. The three facts, each at its own line

**Fact 1 — `st_all_send_cid` writes `0x34` before it calls `st_send_command`.** The body's first act is the
window's store (`src/entry/entry_storage.c:3110`):

```c
3110:    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
3111:    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
3113:    ST_LIVE("xnu_live_storage_cid_sig_enable",
3114:            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));
3119:    st_send_command(ST_CMD_OP_ALL_SEND_CID, 0u, ST_MMC_RSP_R2, &c2);
```

**Fact 2 — `_cid_stale` is `c2.stale`, and `c2.stale` is read *inside* `st_send_command`, after its own
inhibit gate** (`:2560`), i.e. **after** the store at `:3110`:

```c
2558:    /* The latch: read, written straight back (write-1-to-clear), read again. */
2560:    r->stale = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
2561:    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS, r->stale);
```

**Fact 3 — a write to `0x34` sets bit 15 by itself, and nothing clears it.** This is 736's own press,
recorded in the entry source at `:3076-3077` and published on the rung-24 capture as
`_cid_ena_held = 0x00008001` for a store of `0x00000001`:

> *bit 15 (`SDHCI_INT_ERROR`) is set by **any** write to that register and is NOT cleared by one*

## 3. What follows, and it is one line

Facts 1 and 2 put the cid body's own `0x34` store **inside** the interval 775 says is free of one. Fact 3
says that store raises bit 15 in `INT_STATUS`.

**So `_cid_stale ≥ 0x00008000` on every run of this arm, by construction — and 775's pre-registered answer
space collapses: the `zero` row does not exist.** The cell cannot read zero; it never could; and the arm
was built and armed with a narration that tells the operator a zero would mean *CMD1 did nothing*.

**What the cell can still do, and it is worth keeping.** The boundary moves up by one bit:

| `_cid_stale` | reading |
| --- | --- |
| `0x00008000` **exactly** | nothing but the cid body's own store is in the register — **CMD1's poll wrote `0x34` nowhere**, so there is nothing else to explain |
| anything **above** `0x00008000` | something was latched that this arm's stores did not put there — the row 775 wanted, now with an operand that is not reachable by accident |

**And the correction is one bit wide for a reason worth stating**: `_nidx_status_pre` — the *other* cell
built for this question — is read at `:3994`, also after its body's own store at `:3984`, and the rung-24
press reads it as `0x00018000`. **The `0x00008000` half of that word is the store's, and the record already
reads it that way** (`ERROR | TIMEOUT` with the ERROR half attributed to the store). So the nidx row is
internally consistent; it is 775's cid claim, which put no such discount on its own cell, that over-reaches.

**What did NOT survive is the reason, and it is the reason that must not be repeated.** The sentence *the
interval it sits in contains no write to `0x34` at all* is false as written. A reader who takes it would
expect a clean zero on a run with no CMD1 activity, and would read the guaranteed `0x00008000` as evidence
of one — **a false positive produced by the arm's own store**, which is the worst shape a cell in a
two-row answer space can have.

## 4. Where the correction lands, and where it cannot

| site | corrected here? |
| --- | --- |
| `tools/verify_press_ready.sh` — the `wst == 26` branch and `rung_para 26` | **yes**, and this is the copy the operator reads at the press |
| `docs/experiments/experiment-775-…md` §2 | **yes**, with this document named as the correction |
| `docs/experiments/README.md`'s 775 row | **yes** |
| `records/revert-set.txt`'s `# 775` block | **yes** |
| `src/entry/entry_storage.c:3167` | **no — owed, and for 771's COST reason** |

**The entry source is left standing on purpose.** Its sentence is the same false reason, in the block that
publishes the eleven fields. Correcting a comment there **forces a build**, and the next build should be an
arm that carries the correction rather than a comment that spends one — which is the rule 771's own owed
item was left under and 776 finally paid. **It is named as owed here rather than left to be noticed.**

**And 770's rule is what makes the other four edits legitimate**: *a narration of a spent arm is corrected
and an act is not.* No act changed — the value of `_cid_stale` on any future press is exactly what it
would have been. **What changed is a sentence that would have been read as a measurement.** This arm is
**armed and unspent**, so the correction arrives before the press rather than after it, which is the only
ordering in which it is worth anything.

**Nothing in `out/` moves.** The armed arm, its park, the payload and the entry image are bit for bit what
they were; only prose about them changed. `make check` and `verify_press_ready.sh` are re-run to say so.

## 5. The census this step took, because it pre-registers the next arm and costs nothing

While checking the claim the four publish blocks were counted field by field against `struct st_cmd_result`
(28 fields, `entry_storage.c:2435-2487`), by reading the bodies rather than by grepping key names — the
CMD0/CMD1 rows are published through the `ST_CMD_PUBLISH`/`ST_CMD_PUBLISH_12` macros, whose names a grep
cannot see:

| row | publishes | missing |
| --- | --- | --- |
| CMD0 / CMD1 (`st_cmd_path`, the macros) | **28 of 28** | — |
| CMD2 (`st_all_send_cid`, `:3101-3236`) | **27 of 28** | `status_after` |
| **CMD3 (`st_cmd3_noidx`, `:3938-4094`)** | **18 of 28** | `ps_before`, `inhibit_before`, `inhibit_polls`, `inhibit_ticks`, `inhibit_timeout`, `stale`, `clear_wrote`, `status_after`, `rsp_present` |

**The row that is missing the most is the row that carries the most.** CMD3 is the only command in this
ladder's whole history that raised `CMD_INHIBIT` for **all 1024 samples** (`_nidx_inhibit_seen = 0x400`)
*and* fired its own response timeout (`_nidx_status_any = 0x00018000`) — and the nine fields it drops
include the **entire inhibit gate** the driver runs before any command is put on the bus. Two of those nine
are the ones that decide something:

- **`_nidx_inhibit_timeout`** — a `1` would mean the gate gave up and **the command was never sent**
  (`sent` stays 0 and `st_send_command` returns at `:2556`), which is a different row of 771's answer space
  and would explain `inhibit_seen = 0x400` without the card or the block being at fault at all. `_nidx_sent
  = 1` and `_nidx_word_read = 0x030A` already say it did not happen on the rung-24 press — **but only
  because a later cell says so, and the cell that says it directly has never been printed.** That is
  m736's shape exactly: a conclusion drawn from a neighbouring key when the key itself is one publish line
  away.
- **`_nidx_inhibit_polls` / `_nidx_inhibit_ticks`** — the gate's own wait, which is what makes a `0` in
  `inhibit_seen` readable at all. 772's rule for the `cmdlow_seen` counts applies verbatim: a count is read
  beside the window it was taken over, never alone.

**And the one genuinely new reading is not a publish at all.** For the CMD2 row, the cell that *would*
separate the cid body's own store from what was already latched is a read of `INT_STATUS 0x30` taken at the
**top** of `st_all_send_cid`, before `:3110` — the reading `_cid_stale` was designed to be and, by §2, is
not. The CMD3 row has no such cell either (`_nidx_status_pre` is itself post-store). **Both are one read of
an address each body already reaches**, so the arm that carries them adds no new device address — only a
count, which every clause in `build_entry.sh` asserts explicitly and would have to be updated to say so.

**That arm is pre-registered here and NOT built.** `out/` must not move while a press is armed, and 776's
own run ended with the rung-28 arm ready to fire. It is named so the next build can carry it: **value 28,
ordinal rung 29**, strictly containing rung 28, its answer space being the `entrance`-vs-`pre` pair per
row — `entrance == pre` meaning this arm's own store changed nothing, `pre > entrance` meaning the store
contributed exactly its bit 15, and either row read beside the corrected `_cid_stale`.

## 6. Verification

| gate | reading |
| --- | --- |
| the three facts | `entry_storage.c:3110` (the store), `:2560` (`stale` read after the gate), `:3076-3077` (736: any `0x34` write sets bit 15) — each read in full, in this tree |
| the armed arm | unchanged: `xnu_arm_entry.bin` `d5d98738…`, 5552764 B, `STAGE90_XNU_STORAGE_PROBE=27` |
| `make check` | exit 0 |
| readiness | **5 of 5**, exit 0, and the corrected narration is what it prints |
| the park | `out/stage90/frozen/armed-storage-d5d98738`, unchanged, `tools/verify_revert_set.sh` **11 ok / 0 failed** |

**Owed, unchanged and named rather than left to be inferred:** the `rung_para` correction for values 12..23
(partly paid by 776); the seam-address class — *a kernel address pinned in an entry source* — whose repair
is a link order; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL` structural repair; and **now
`entry_storage.c:3167` too**, the same false reason in the one place this step could not correct without
spending a build.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist and 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
