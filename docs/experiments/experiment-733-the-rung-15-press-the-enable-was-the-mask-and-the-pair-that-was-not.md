# 733: the rung-15 press — the enable was the mask, CMD1 went out and the CARD ANSWERED, and one predicted cell failed in a way that re-reads 730

**PRESSED.** `armed-storage-enable-953ad0f6` (`STAGE90_XNU_STORAGE_PROBE=14`, rung 15 by the ordinal
this project's subjects count), **2026-09-26 10:12:21–10:13:31 UTC, runner EXIT 0, 70 s**. Readiness
5/5 exit 0, **exactly one** gate (exit 0 / 591 lines) and **exactly one** runner, `fastboot boot` only
and nothing flashed, `33e80afe` verified off the bus immediately before the gate, the arm's bytes
re-hashed as `73d4a8f3…` immediately before the gate. Capture
`out/stage90/captures/rung15-enable-20260926-101331-last_kmsg.txt`, 636,013 B, sha
`3c8418bd35aa33bd920258d77fd1de784cd640f2a2dfbf93d38503dfbdd5ef7c`. The device came back on its own
and the run's ending did not move.

## 1. The arm's own cells all hit

| cell | predicted | measured |
| --- | --- | --- |
| `_cmd_gate_kind` | `0` | `0x00000000` |
| `_ena_wrote` | `1` | `0x00000001` |
| `_ena_held` | `0x00008001` | **`0x00008001`** |
| `_ena_status_pre` | `0` | `0x00000000` |
| `_ena_host_version` | ≠ 0, ≠ 0xffff | `0x00001102` |
| **`_cmd0_complete`** | **`1`** | **`0x00000001`** |
| `_cmd0_status_any` | ≠ 0 | `0x00000001` |
| `_cmd0_any_polls` | small | `0x0000021b` (539) |
| **`_cmd_gated`** | **`0`** | **`0x00000000`** |
| `_cmd_sent` | 2 | `0x00000002` |
| **`_cmd1_sent`** | present | `0x00000001` |
| `_cmd1_resp` | the R3 shape | **`0x40ff8080`** |
| `_cmd1_resp_voltage` | non-zero window | `0x00ff8000` |
| `_cmd1_rsp_present` | — | `0x00000001` |
| `_cmd1_word_read` | — | `0x00000102` |
| `_cmd1_err` | `0` or `0x00010000` | `0x00000000` |
| `_ena_restore_seq` | `1` | `0x00000001` |
| `_ena_wrote_back` | `0` | `0x00000000` |
| `_ena_readback` | `0x00008000` | **`0x00008000`** |
| `_ena_status_post` | `0` or `1` | `0x00000000` |
| `_int_status_before` / `_after` | `0` / **`0`** | `0x00000000` / **`0x00000001`** |

**The arm's own cell is `_cmd_gated = 0`, and it is there with `_cmd1_*` keys beside it.** The cell
that says the enable was the mask is **`_cmd0_complete = 1`**, and the arm's own numbers date it:
`_cmd0_inhibit_seen = 0x219` (537 of the poll's samples saw `CMD_INHIBIT` set) and
`_cmd0_inhibit_last = 0x01f80000` (bit 0 clear — the command finished), so the poll was in progress on
the CMD line and then stopped; `_cmd0_status_any = 1` is first seen at `_cmd0_any_polls = 0x21b` — **the
poll's LAST sample**, two samples after the inhibit last read clear. **So the RESPONSE bit appeared at
the command's own completion and not at the enable's store**: the enable stood from before the send, and
the poll's first 538 samples read `0` with it standing.

**And then the arm did the thing it was built to make possible: CMD1 went out for the first time in this
line's life, and the CARD ANSWERED.** `_cmd1_sent = 1`, `_cmd1_rsp_present = 1`, and `_cmd1_resp =
0x40ff8080` is **exactly the reference word the pre-registration named** — bit 30 set (ready) with the
voltage window `0x00ff8000` in bits 23:15. `_cmd1_err = 0`. `_cmd1_word_read = 0x00000102`, where rung
13 measured CMD0's word reading back as `0x0000`: the block held CMD1's own word. CMD1's own poll
(`_cmd1_complete = 0`, `_cmd1_timeout = 1`, `_cmd1_status_any = 0`) is **expected** and is not a
failure: CMD1's send happens *after* the window has closed, so its poll ran masked — which is the same
statement this press just proved about CMD0, read one command further along.

**The hazard the pre-registration named did not occur.** There is no `_irq_other_*` key of any kind, and
the run's ending is byte-for-byte the previous arms' shape: `_post_end_calls = 7`,
`_sleh_storm = 9`, `_seam_post_end_ticks = 0x06ddd000`, `_seam_sctlr = 0x30c57879`, `_seam_lr =
0x800492dc`, and `slot_post_calls` reached 4 — the same progress 730's log recorded. So `SIGNAL_ENABLE`
at zero was the right way to buy this answer.

## 2. The one cell that failed, and why its failure is the finding

**`_int_status_after = 0x00000001` where section 5 of the pre-registration predicted `0`.** The
prediction's reasoning was that CMD0's own poll consumes the RESPONSE bit with its write-1-to-clear, so
by the time `st_int_report()` reads the register the bit is gone and its own store would leave the
register at zero. The prediction was wrong, and the log says exactly where.

The three readings that decide it, all from this one capture and all in the same body:

    _ena_status_post   = 0x00000000   INT_STATUS, read at the WINDOW's close, after CMD0's poll
    _int_status_before = 0x00000000   the same register, read by st_int_report, later
    _int_status_after  = 0x00000001   the same register, after ONE store

and the store between the last two is read out of the source rather than inferred: `st_int_report` does
`st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, ST_SDHCI_INT_RESPONSE)` — **an absolute `0x00000001`,
not a read-modify-write** — and that is the only write between `_int_status_before` and
`_int_status_after`. `_int_enable_held = 0x00008001` is the block's readback of it.

**So on this arm the pair `_int_status_before` → `_int_status_after` goes 0 → 1 across one store to
`INT_ENABLE 0x34`, at a moment when the block was not holding a completion** — the window's close and
`st_int_report`'s own first read both said so, one after the other.

**That re-reads 730.** 730's headline inference was *"the completion was there all along and the enable
was the mask"*, and its **whole evidence** was that same pair: `_int_status_before = 0x00000000` →
`_int_status_after = 0x00000001` with one store between them. This press measures the identical 0 → 1
transition, across the identical store, **on a register that had just been read as clear twice**. The
pair therefore does not by itself separate *a completion the enable revealed* from *an effect of writing
`0x34`*. 730's conclusion is not refuted — this press's own `_cmd0_complete = 1` is better evidence for
it, measured inside the poll that previously read nothing for 5,088,256 samples — but **730's stated
evidence is ambiguous, and the honest record says so.**

**Two mechanisms remain, and this press does not settle which.** (A) A write to `0x34` makes `0x30`'s
RESPONSE bit read 1. (B) The block holds a completion pending and re-latches it into `0x30` when the
enable returns — 726's *sixth hypothesis*, which 730 adopted and which **this press does not put down**.
The counter-evidence to the naive form of (A) is in this same log and is not yet reconciled with it:
with the enable standing from before the send, CMD0's poll read `INT_STATUS = 0` for its first 538
samples (`_cmd0_any_polls = 0x21b`). **That is the next rung's question**, and the discriminating
reading is cheap: read `INT_STATUS 0x30` after a store to `0x34` on a block that has been quiet — no
command sent since the previous clear — which separates a store's effect from a completion's.

## 3. Cost, safety and what this press is worth

**One gate, one runner, one `fastboot boot`, nothing flashed, one arm on the bus.** The runner's exit is
**0** — the device returned and the log was captured — and the device came back on its own, so no
watchdog and no power press were involved. `33e80afe` was confirmed off the bus immediately before the
gate; the arm's bytes were re-hashed immediately before the gate as well, because a press is the one
moment a stale reading is unrecoverable.

**What the goal thinks of it: 「把基础驱动跑起来」 is closer and still unmet.** Two commands now run to
completion on the bus with a real card on the other end of them and a real R3 answer in the log — but
there is still **no storage**: no data line has moved a byte, no driver is registered as a storage
driver, and the OS does not mount anything. **TWRP-to-storage stays withheld** (the clause is
conditioned on 「如果os已经能进去了的话」). The frontier reading is unchanged, so this press did not
advance the boot; it advanced the storage arm.

**The ladder's next rung is named by §2**, and it is a read, not a new command class.
