# 738: the rung-17 press — CMD2 went on the bus, the block gave nothing back, and the response register is not a witness

**The press**: `armed-storage-cid-dc8d7002` (`STAGE90_XNU_STORAGE_PROBE=16`, ordinal rung 17),
2026-09-26 **14:41:56–14:43:13 UTC**, **EXIT 0**; the runner's own line is *"the device came back 28s
after this run called `fastboot boot`"*, and the interval from the gate log's own mtime to the captured
log's is **77 s** (measured here; the earlier presses' 71 s figures were the runner's own press-log
durations, so the two are on different footings and are not compared). Readiness **5 of 5, exit 0**
immediately before it, **one** `preflight_boot_check.sh --allow-xnu-entry` (exit 0, **585** lines), **one**
`run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-cid-dc8d7002`. `fastboot boot` only,
nothing flashed. The neighbour `33e80afe` was absent from **both** `fastboot devices` and `adb devices`
at fire time (0 occurrences).

Capture: `out/stage90/captures/rung17-cid-20260926-144313-last_kmsg.txt`, **628,500 B**, sha256
`ad9d77c92df2a7eb2134a01cf0c61d0d8bd9a19f1b14ada9d7bcb5add11b0a3e`; with it the gate log, the run log
and the combined press log, all under the same timestamp.

## 1. The ordering defect 736 found is repaired, and the exclusion held in the bytes

736's press spent itself on a store placed **above** `st_cmd_path`'s gate: bit 15 of `INT_ENABLE 0x34`
is a durable side effect of any write to it, the gate read `0x00008000`, and **no command went on the
bus**. Rung 17 is the ladder through value 15 *without* 15's body, and the four-place exclusion held:

    _cmd_int_enable  = 0x00000000      NOT 0x00008000
    _cmd_gate_kind   = 0x00000000      gate 1 did not fire
    _cmd_refused     = 0x00000000
    _cmd_sent        = 0x00000002      two commands, not zero
    _cmd_ps_after    = 0x01f80000      the controller idle, unchanged
    _quiet_* keys    = 0 occurrences   15's body is not in this image

So this press is the first since 733's on which the command path ran, and it re-measured the rung below
rather than inheriting 736's refusal.

## 2. CMD2 went on the bus, and the driver's own word is what the block holds

    _cid_calls       = 0x00000001      the body ran
    _cid_gated       = 0x00000000      the gate let it through
    _cid_op          = 0x00000002      mmc_all_send_cid's CMD2
    _cid_flags       = 0x00000007      MMC_RSP_R2 = PRESENT|136|CRC
    _cid_word        = 0x00000209      opcode 2 << 8 | RESP_LONG 0x01 | CRC 0x08
    _cid_word_read   = 0x00000209      THE BLOCK'S OWN COPY - the store took
    _cid_sent        = 0x00000001

`0x0209` is the decode this tree's **vendor** header demands and not upstream Linux's:
`sdhci.h:53` has `RESP_LONG 0x01` where upstream has `RESP_SHORT 0x02` at that position, and `INDEX`
(`0x10`) is correctly absent because `MMC_RSP_R2` carries no `MMC_RSP_OPCODE`. The three words this
ladder puts on `0x0E` — `0x0000` (CMD0), `0x0102` (CMD1), `0x0209` (CMD2) — are `_Static_assert`ed in
the source and asserted again by the build clause.

## 3. And the block gave nothing back

    _cid_complete      = 0x00000000      no INT_STATUS bit of any kind
    _cid_status_any    = 0x00000000      over the WHOLE register, not just RESPONSE
    _cid_polls         = 0x004da800      5,088,256 samples
    _cid_ticks         = 0x015f9651      23,040,593 ticks = 1.200 s at 19.2 MHz
    _cid_timeout       = 0x00000001
    _cid_err           = 0x00000000
    _cid_inhibit_seen  = 0x00000000      CMD_INHIBIT NEVER observed, first 1024 samples
    _cid_inhibit_last  = 0x01f80000      and not in progress at the last one
    _cid_ena_wrote     = 0x00000001      the enable WAS standing for the whole send
    _cid_ena_held      = 0x00008001      the block's own copy
    _cid_sig_enable    = 0x00000000      SIGNAL_ENABLE read, never written

**The enable was standing and it did not matter.** 733's press established that a command's status bit
is latched only while its enable stands — CMD0 completed inside rung 14's window (`_cmd0_complete = 1`)
and CMD1, sent with the window closed, was never reported (`_cmd1_complete = 0`). Rung 17 was built on
that rule and CMD2 ran **inside** the window. So the rule is necessary and **not sufficient**, and the
one thing the arm could not have predicted is that CMD2's `CMD_INHIBIT` was never seen at all, where
CMD0's was seen 537 times (`_cmd0_inhibit_seen = 0x219`) in the same run.

## 4. The 136-bit read returned CMD1's response word, and that is this press's finding

    _cid_raw3 (+0x10) = 0x00000000     )  the four raw words, word 0 first
    _cid_raw2 (+0x14) = 0x00000000     )
    _cid_raw1 (+0x18) = 0x80000000     )
    _cid_raw0 (+0x1c) = 0x0040ff80     )
    _cid_resp0        = 0x40ff8080     )  the driver's own read: (raw0 << 8) | byte at +0x0B
    _cid_resp1        = 0x00000000     )
    _cid_resp2        = 0x00000000     )
    _cid_resp3        = 0x00000000     )
    _cid_resp_short   = 0x00000000     )  the short-response read, for the pair

**`_cid_resp0 = 0x40ff8080` is bit-for-bit `_cmd1_resp` in the same log**, which 733 read from `RESPONSE
+ 0x10` (word 0) at CMD1's own moment. Read as one 128-bit big-endian register, the four words are
`0x0040ff80_80000000_00000000_00000000` — that same 32-bit value, **one byte further along**. So:

- **the four words are not a CID.** A 136-bit `ALL_SEND_CID` answer is not the previous command's OCR;
  no card was asked twice for the same 32 bits.
- **the register did move.** After CMD0 in this same run, `_cmd0_resp = 0x00000000` with
  `_cmd0_resp_read = 1`; after CMD1 it was `0x40ff8080`. So the register's content is not a constant,
  and something between CMD0 and CMD1 put that word there — the block's own default for an unanswered
  command, or a card. **This press does not separate those two**, and it is the first press that makes
  the question visible, because it shows the same word sitting in the register with **no completion
  latched and no `CMD_INHIBIT` ever seen**, and being re-presented in the long-response positions when
  a long response is asked for.

**What that does to the ladder, said plainly.** Since 733, `_cmd1_resp = 0x40ff8080` has been read as
*the card answered CMD1* — 733's index row calls it "exactly the reference word", 736 §5 built the push
from it, and **rung 17's own gate** (`c1.sent != 0 && (c1.resp & MMC_CARD_BUSY) == 0`) is read off it.
The gate's *logic* is still right — a status-bit gate refuses on every arm of this ladder — but the
*evidence* it consumes is weaker than the ladder has been treating it as. `0x40ff8080` has the shape of
a valid MMC OCR (bit 30 set, voltage window `0x00ff8000`), which is why it convinced; shape is not
provenance. **A read of `RESPONSE` on a block with no command in its past would settle it**, and that
read is a rung of its own — cheap, one register, no command, no new store class.

## 5. The rung below is inherited unchanged, and two owed readings came back

    _cmd0_word_read      = 0x00000000     )  733's own values, reproduced
    _cmd0_status_any     = 0x00000001     )
    _cmd0_any_polls      = 0x0000021b     )
    _cmd0_inhibit_seen   = 0x00000219     )
    _cmd0_complete       = 0x00000001     )
    _cmd1_word_read      = 0x00000102     )
    _cmd1_resp           = 0x40ff8080     )
    _cmd1_complete       = 0x00000000     )  CMD1 ran with the window CLOSED, as on 733
    _cmd1_ticks          = 0x015f9248     )
    _int_status_before   = 0x00000000     )  730's famous pair, reproduced
    _int_status_after    = 0x00000001     )
    _ena_host_version    = 0x00001102     <== 697's owed reading

`HOST_VERSION 0xFE` read as a halfword: vendor `0x11`, spec version `0x02` — a non-zero, non-`0xffff`
reading, taken at the one width `0xFE`'s alignment allows. It is 730 §d's SDHCI-3.0 hypothesis with a
version register beside it rather than an argument from layout, and it is a **reading to be read against
the 3.0 layout claim**, not a proof of it.

**One difference in the ending, recorded rather than dropped**: `_post_end_calls = 0x00000006` where
730, 733 and 736 all read `0x00000007`. Every other ending cell is unmoved (`_seam_post_end_ticks =
0x06ddd000`, `_seam_sctlr = 0x30c57879`, `_sleh_storm = 9`, `_post_cntfrq = 0x0124f800`), and this log's
last `_post_elapsed` (`0x06e848ea`) sits just above the trigger's floor exactly as 736's does
(`0x06de1e04`). `_post_end_calls` counts **idle-exit passes**, not time, so one fewer pass over the same
window is pass-count jitter and not this arm's news — but a reader comparing the four logs will see it,
and an unremarked difference is how a real one gets missed.

**And the pre-registered failure mode did not happen**: there is no `_irq_other_*` key in this log —
`SIGNAL_ENABLE 0x38` was read and never written, the SPI 123 → intid 155 line never rose, and the run
did not end at the dispatcher. That is now a measurement on a second arm rather than a reason.

## 6. The safety contract, and what the run's ending was

`_cid_ena_wrote = 0x1` and `_cid_wrote_back = 0x0` — two stores, both to `INT_ENABLE 0x34`, the second
the undo of the first, with `SIGNAL_ENABLE` read and never written. No new command class beyond CMD2, no
data path, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word, **no byte of the medium**. The
restore's own readback (`_cid_readback = 0x00008000`) reproduces the durable bit-15 finding a fourth
time: a write to `0x34` sets it and a write of `0` does not clear it.

The log ends the way 730's, 733's and 736's do — the kernel's own `No errors detected`, the device back
on Android on its own — so the ending is the ladder's normal shape. The runner's verdict is **14 PASS /
2 FAIL**, and both FAILs are the one known storage-arm criterion (`seam_sp=0x80557ec8 is not sleh_sp-8`,
which `694`, `697`, `705`, rungs 12 and 13 all carry), so neither is about this arm.

## 7. What the next rung is

**Two facts decide it and both are in hand.**

1. **The register is not a witness.** Read `RESPONSE 0x10..0x1C` with **no command in the block's
   past** — the same cheap, read-only shape as rung 3's census, no store, no new write class. If the
   register is zero there, then `0x40ff8080` was put there by CMD1 and 733's reading survives; if it
   already holds a value, then **the ladder's one piece of evidence that a card exists is retired**,
   and the whole push from CMD1 has to be re-derived from a different signal.
2. **`CMD_INHIBIT` is the discriminator nobody has used as one.** CMD0's `_cmd0_inhibit_seen = 0x219`
   says the block **started** it; CMD1's and CMD2's are both `0` over the same sampling window. A
   command whose inhibit is never observed and whose word reads back is a command the register file
   holds and the sequencer never took — and that reading is available in the log this press already
   wrote, for two commands.

And the question the ladder was on before rung 16's detour is unchanged: `mmc_attach_mmc` continues
past CMD2 with CMD3 `mmc_set_relative_addr`, CMD9 `SEND_CSD`, CMD7 `SELECT_CARD`, and then CMD17 at
LBA 1, where 531 §9's self-verifying reading lives (the eight bytes `EFI PART`, or an MBR ending
`55 aa`). **Rung 18 should be the read that decides whether any of it is addressing a card at all** —
otherwise the ladder spends presses collecting answers from a register.

## 8. What the press was worth, in one line

It spent one press, repaired 736's ordering defect and proved the four-place exclusion in the bytes,
put CMD2 on the bus with the driver's own word read back, took the block's answer (nothing — no
completion, no inhibit, over 1.200 s with the enable standing), and took two owed readings — and it
**weakened the ladder's one piece of evidence that a card exists**, which is worth more than the
answer it was built to collect.

**It does not meet the goal.** 「把基础驱动跑起来」/「起码要能进入操作系统」 is still unmet — no storage,
no filesystem, and the OS is not observed to reach userland — so **TWRP-to-storage stays withheld** (the
clause is conditioned on 「如果os已经能进去了的话」).
