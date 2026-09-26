# 740: the rung-18 press — the register WAS empty before any command, and the ladder's evidence survives

**The press**: `armed-storage-rb-05645d1a` (`STAGE90_XNU_STORAGE_PROBE=17`, ordinal rung 18),
2026-09-26 **15:58:25–15:59:37 UTC**, **EXIT 0**; the runner's own line is *"the device came back 29s
after this run called `fastboot boot`"*, and the interval from the gate log's own mtime to the captured
log's is **72 s** (measured here, on the same footing as 738's 77 s). Readiness **5 of 5, exit 0**
immediately before it, **one** `preflight_boot_check.sh --allow-xnu-entry` (exit 0, **594** lines),
**one** `run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-rb-05645d1a`. `fastboot boot`
only, nothing flashed. The neighbour `33e80afe` was absent from **both** `fastboot devices` and
`adb devices` at fire time (0 occurrences) and no stray runner, gate or watcher was running.

Capture: `out/stage90/captures/rung18-rb-20260926-155937-last_kmsg.txt`, **642,375 B**, sha256
`eb641e07223b74cad7651b841017fce399f517de890a517775bb5bde47769201`, **archived by hand** (the runner
does not archive — 730 §6); with it the gate log, the run log and the combined press log under the
same timestamp.

## 0. Two spellings of the rung number

The ladder counts the **value** of `STAGE90_XNU_STORAGE_PROBE` (this arm's clauses read `17 = 16 plus …`);
the commit subjects and the pre-registrations count **ordinal arms**; they part company at ladder value
9 (which has two arms), so **this arm's ladder value is 17 and its ordinal is rung 18**. 739 §0 is the
same sentence for the arm this press spent.

## 1. The answer: the register was EMPTY before any command

The arm's own body, `st_resp_before`, on a block that has never carried a command in this image's life —
called at the one moment the ladder is quiesced (rungs 2–9 measured the mode, the power and the clock)
and no command has ever been on its bus:

    _rb_calls        = 0x00000001      the body ran
    _rb_raw0 (+0x1C) = 0x00000000    )  the four raw words, word 0 first
    _rb_raw1 (+0x18) = 0x00000000    )
    _rb_raw2 (+0x14) = 0x00000000    )
    _rb_raw3 (+0x10) = 0x00000000    )
    _rb_resp0        = 0x00000000    )  the driver's own read: (raw0 << 8) | byte at +0x0B
    _rb_resp1        = 0x00000000    )
    _rb_resp2        = 0x00000000    )
    _rb_resp3        = 0x00000000    )
    _rb_resp_zero    = 0x00000001   <== THIS RUNG'S ANSWER
    _rb_done         = 0x00000001

**`_rb_resp_zero = 1`: `RESPONSE 0x10..0x1C` held nothing at all before any command.** 739 §3 registered
both outcomes and what each would buy; this is the one that buys the evidence back:

- the value `_cid_resp0 = 0x40ff8080` in the *same* log — read after CMD2, and 738 showed it bit-for-bit
  `_cmd1_resp` — is **not** a block default that was sitting there from reset. The register was read as
  zero and later read as `0x40ff8080`.
- the intermediate reading is in the same log too: after CMD0 the register read `0x00000000` with
  `_cmd0_resp_read = 0x00000001` (a reading, not an absence — 733's distinction), and after CMD1 it read
  `0x40ff8080` with `_cmd1_resp_read = 0x00000001`.
- so a command **moved** this register: `0 → 0 → 0x40ff8080`. **733's reading of `0x40ff8080` as CMD1's
  response survives, and 733 §'s "the card answered CMD1" is back on its feet.**

This is exactly the reading 738 §4 named as the cheap thing that should be done before the ladder builds
anything else on top: *"a read of `RESPONSE` on a block with no command in its past would settle it"*.
It settles it, and it settles it **for** the ladder.

## 2. And the twelve other cells came back exactly as pre-registered

A read-only body's worth is the state it reads, so each cell is compared with what 739 §7 predicted:

| cell | 739 predicted | measured | |
| --- | --- | --- | --- |
| `_rb_present` | `0x01f80000`-shaped, no command in flight | `0x01f80000` | ✓ the controller idle |
| `_rb_inhibit` | `0` | `0x00000000` | ✓ |
| `_rb_cmd_word` | `0x0000` | `0x00000000` | ✓ nothing has carried |
| `_rb_int_status` | `0` | `0x00000000` | ✓ |
| `_rb_int_enable` | `0x00008000`, or `0` | **`0x00000000`** | ✓ nothing above the gate wrote `0x34` |
| `_rb_sig_enable` | `0` | `0x00000000` | ✓ never written |

**`_rb_int_enable = 0x00000000` is the safety reading, and it is a reading rather than a promise.**
736's press is the measurement of what a store above `st_cmd_path`'s gate costs: rung 15's write set bit 15
of `INT_ENABLE 0x34`, the gate read `0x00008000` and **no command went on the bus**. This rung's body
stores nothing, bit 15 is **clear** at the pre-command moment, and the gate below read
`_cmd_int_enable = 0x00000000` and let the command path run (`_cmd_sent = 0x00000002`). The read-only
design is therefore verified in the bytes of a real run and not only by the build clause.

## 3. The ordering the build clause asserts is visible in the log

The clause refuses the arm whose call site is placed *after* `st_cmd_path`. The log shows the order
directly, in one file: the twelve `_rb_*` cells at lines 8673–8689 and `_cmd_calls` at 8690 — the
read-only body ran, published all twelve, and the command path was entered immediately after. So the
placement is a property of the artifact as well as of the source.

**And this is the first rung whose report is *finished before the commands start*.** rung 17's `_cid_*`
cells are at 8803 onward; had the read been placed below, every `_rb_*` value would have been a statement
about a block that had already answered three commands and `_rb_resp_zero` could not have meant what its
name says.

## 4. The exclusion held, and the rung below is inherited unchanged

    _cmd_int_enable  = 0x00000000      NOT 0x00008000
    _cmd_gate_kind   = 0x00000000      gate 1 did not fire
    _cmd_refused     = 0x00000000
    _cmd_sent        = 0x00000002      two commands, not zero
    _cmd_ps_after    = 0x01f80000      the controller idle, unchanged
    _quiet_* keys    = 0 occurrences   15's body is not in this image

`_cid_*` reproduced 738 **one for one**, which is what a rung that adds only a read should do:

    _cid_word       = 0x00000209   _cid_word_read = 0x00000209   _cid_op = 0x00000002
    _cid_flags      = 0x00000007   _cid_gated     = 0x00000000   _cid_sent = 0x00000001
    _cid_complete   = 0x00000000   _cid_status_any = 0x00000000  _cid_err  = 0x00000000
    _cid_polls      = 0x004da800   _cid_inhibit_seen = 0x00000000
    _cid_raw0       = 0x0040ff80   _cid_raw1 = 0x80000000   _cid_raw2 = _cid_raw3 = 0
    _cid_resp0      = 0x40ff8080   _cid_resp1 = _cid_resp2 = _cid_resp3 = 0
    _cmd1_resp      = 0x40ff8080   _cmd1_resp_read = 0x00000001
    _cmd0_status_any = 0x00000001  _cmd0_complete = 0x00000001  _cmd0_inhibit_seen = 0x219
    _cmd0_polls     = 0x0000021a   _cmd0_ticks = 0x00001328

**One difference was looked for and is not one.** `_cmd1_inhibit_last = 0x00f80000` here against
`0x01f80000` for CMD0 and CMD2 — but 738's log reads `0x00f80000` in the same cell, so it is a
reproduced property of the CMD1 window and not this arm's news. (Per 738 §5's discipline: an unremarked
difference between two logs is how a real one gets missed, so it was checked rather than left.)

**And the discriminator 738 §7 named is in this log unchanged**: `_cmd0_inhibit_seen = 0x219` says the
block **started** CMD0; `_cmd1_inhibit_seen = 0` and `_cid_inhibit_seen = 0` say it never showed a start
for the two commands whose response words this ladder has been trusting. That asymmetry is now a
measurement on two arms with the register's emptiness established on the third reading — and it is
sharper than it was, because the register's *content* is now known to be command-driven, which makes the
*absent inhibit* the remaining unexplained thing about CMD1 and CMD2 rather than a reason to doubt the
word.

## 5. The ending, and what the runner said

The log ends the way 730's, 733's, 736's and 738's do — the kernel's own `No errors detected`, the device
back on Android on its own — so the ending is the ladder's normal shape and not this press's news. Every
ending cell is unmoved (`_seam_post_end_ticks = 0x06ddd000`, `_seam_sctlr = 0x30c57879`,
`_seam_lr = 0x800492dc`, `_sleh_storm = 9`, `_post_cntfrq = 0x0124f800`,
`_post_end_calls = 0x00000006`, the same value 738 read); the seam's own four words are the clean pair
(`_seam_a0 = _seam_b0 = 0x800b5648`, `_seam_a1 = _seam_b1 = 0x8047fb04`).

**The pre-registered failure mode did not happen a fourth time**: `_irq_other_*` is absent (0
occurrences), `SIGNAL_ENABLE 0x38` was read and never written, the SPI 123 → intid 155 line never rose,
and the run did not end at the dispatcher.

The runner's verdict is **14 PASS / 2 FAIL**, and both FAILs are the one known storage-arm criterion
(`seam_sp=0x80557ec8 is not sleh_sp-8` — the criterion `694`, `697`, `705`, rungs 12, 13, 16 and 17 all
carry), so neither is about this arm.

## 6. What the next rung is

The detour that began at rung 16 is closed. **Both of 738 §7's deciding facts are now in hand:**

1. **The register IS a witness** — empty before any command, `0x40ff8080` after CMD1, and the same
   derivation at both times. The ladder's push from CMD1 does **not** have to be re-derived.
2. **`CMD_INHIBIT` is the one thing still unexplained about CMD1 and CMD2** — not as a reason to distrust
   the response word (that doubt is retired), but as the next question of its own: a command whose word
   reads back and whose response register moved, and whose inhibit was never observed, where CMD0's own
   inhibit was seen 537 times in the same window.

**So the driver's own continuation is the next rung**, and the register's being a witness is what makes it
worth walking: CMD2's `mmc_all_send_cid` returned CMD1's word rather than a 128-bit CID
(`_cid_resp0 = 0x40ff8080`, `_cid_resp1..3 = 0`, and the four raw words are that same 32-bit value one byte
further along), so the CID is still not in hand. `mmc_attach_mmc` continues past CMD2 with CMD3
`mmc_set_relative_addr`, CMD9 `SEND_CSD`, CMD7 `SELECT_CARD`, and then CMD17 at LBA 1, where 531 §9's
self-verifying reading lives (the eight bytes `EFI PART`, or an MBR ending `55 aa`).

**And the next rung should carry the inhibit reading beside the response reading**, because the two are
now a pair the ladder can compare rather than an unexplained asymmetry: a command whose response register
moves and whose inhibit is never seen is a block that is answering from its register file, and CMD0 is the
one counter-example the ladder already has.

## 7. What the press was worth, in one line

It spent one press, took the rung's answer (`_rb_resp_zero = 1` — **the register was empty before any
command**), produced all twelve pre-command cells as pre-registered — including `_rb_int_enable = 0`,
the safety reading that proves in the bytes that a read-only body above the gate costs the rung below
nothing — reproduced 738's `_cid_*` and `_cmd*` cells one for one, verified the build clause's ordering
claim in the log's own line numbers, and **retired 738's doubt about the ladder's only evidence that a
card exists**, which is what the rung was for.

**It does not meet the goal.** 「把基础驱动跑起来」/「起码要能进入操作系统」 is still unmet — no storage, no
filesystem, and the OS is not observed to reach userland — so **TWRP-to-storage stays withheld** (the
clause is conditioned on 「如果os已经能进去了的话」).
