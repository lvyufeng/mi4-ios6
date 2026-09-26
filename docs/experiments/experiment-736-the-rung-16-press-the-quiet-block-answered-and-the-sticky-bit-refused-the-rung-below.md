# 736: the rung-16 press — the quiet block answered, and the store's own sticky bit refused the rung below

**The press**: `armed-storage-quiet-c9738417` (`STAGE90_XNU_STORAGE_PROBE=15`, ordinal rung 16),
2026-09-26 **11:45:21–11:46:32 UTC**, **EXIT 0, 71 s**. Readiness **5 of 5** immediately before it (with
the real gate), **one** `preflight_boot_check.sh --allow-xnu-entry` (exit 0, 584 lines), **one**
`run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-quiet-c9738417`. `fastboot boot` only,
nothing flashed. The neighbour `33e80afe` was absent from `fastboot devices` at fire time and the arm was
re-hashed immediately before the gate (`41a3963b…`).

Capture: `out/stage90/captures/rung16-quiet-20260926-114635-last_kmsg.txt`, **632,608 B**, sha256
`ba0948d31e9ebccf5a9f586dd82a741675ca7245c70c86b4f797e5a1d6cfc80a`; with it the gate log, the run log and
the combined press log, all under the same timestamp.

## 1. The answer: (A) is refuted

The arm's own body, on a block that has never carried a command in this image's life:

    _quiet_calls         = 0x00000001      the body ran
    _quiet_status_before = 0x00000000      quiet at the store
    _quiet_enable_before = 0x00000000
    _quiet_sig_before    = 0x00000000      SIGNAL_ENABLE read and never written
    _quiet_wrote         = 0x00000001      INT_ENABLE <- read | SDHCI_INT_RESPONSE
    _quiet_status_after  = 0x00000000  <== THIS RUNG'S ANSWER
    _quiet_held          = 0x00008001      the write ALSO set bit 15 (SDHCI_INT_ERROR)
    _quiet_wrote_back    = 0x00000000      the restore wrote the value the body had read
    _quiet_status_post   = 0x00000000
    _quiet_readback      = 0x00008000      the restore did NOT clear bit 15

733 left two mechanisms standing: **(A)** a write to `INT_ENABLE 0x34` makes `INT_STATUS 0x30`'s RESPONSE
bit **read** 1, and **(B)** a completion the block was holding, re-latched when the enable came back
(726's sixth hypothesis). Rung 16 was built where only (B)'s precondition is absent, so (A) predicted `1`
and (B) predicted `0`.

**It read `0`.** A write to `0x34` does **not** make `0x30`'s RESPONSE bit read 1 — **(A) is refuted at
the one moment it could be tested**, and 730's reading of its own `0 → 1` transition is retired with it.
726's sixth hypothesis is the sole survivor: the bit appears when a **completed command's** status is
latched while the enable stands — which 733's arm had (two commands had run) and this one did not.

**And what this does not say, stated because a null reading is easy to oversell**: it does **not** by
itself *confirm* (B). A null reading cannot confirm a mechanism whose precondition is a completion. What
it does is remove the alternative, and it leaves (B) as the only reading consistent with both logs.

**The two bit-15 cells reproduced 730's and 733's measurement inside a single run**: the write set
`SDHCI_INT_ERROR` (`_quiet_held = 0x00008001` for a store of `0x00000001`) and the restore did not clear
it (`_quiet_readback = 0x00008000` for a write of `0x00000000`). Both values were pre-registered from
those logs, and both were right.

## 2. The cost: the store's own sticky bit refused the rung below, so no command went on the bus

Because `_quiet_readback` is `0x8000` and not `0`, `st_cmd_path`'s gate — whose whole job is *"`INT_ENABLE`
must hold nothing but RESPONSE"* — read `0x00008000` at its top and refused:

    _cmd_calls       = 0x00000001
    _cmd_int_enable  = 0x00008000      <== set by THIS RUN'S OWN quiet store, ~200 lines earlier
    _cmd_sig_enable  = 0x00000000
    _cmd_gate_kind   = 0x00000002      the gate's second reason: a bit other than RESPONSE was set
    _cmd_enabled_out = 0x00000001
    _cmd_refused     = 0x00000001
    _cmd_sent        = 0x00000000      nothing was issued
    _cmd_gated       = 0x00000000      the BETWEEN-COMMANDS gate never ran - the body returned above it
    _cmd_ps_before   = 0x01f80000      the controller is idle, before and after
    _cmd_ps_after    = 0x01f80000
    _cmd_done        = 0x00000000

**Every key published inside `st_cmd_path` is ABSENT, counted and not assumed** — `_cmd0_sent`,
`_cmd0_complete`, `_cmd0_status_any`, `_cmd0_any_polls`, `_cmd0_inhibit_seen`, `_cmd1_sent`,
`_cmd1_complete`, `_cmd1_resp`, `_cmd1_err`, `_int_status_before`, `_int_status_after`, `_ena_wrote`,
`_ena_held`, `_ena_status_pre`, `_ena_status_post`, `_ena_wrote_back`, `_ena_readback`, `_ena_restore_seq`
— **0 occurrences each**. So this press did **not** re-measure the command path, and the `0 → 1` pair 733
reported is nowhere in this log.

**The gate was right to refuse and the arm was wrong to set it up that way.** `INT_ENABLE` bit 15 is a
**durable** side effect of any write to `0x34`, and the rung-16 design placed its store *before* the gate.
733's arm is the proof of the ordering: its store sat **inside** `st_cmd_path`, after the gate, and its
commands went out.

**The safety contract held, and it is why this is a cost and not a loss**: the refusal happened before any
command, `_cmd_sent = 0`, `_cmd_ps_after` unchanged, `SIGNAL_ENABLE` never written, no `POWER_CONTROL
0x29`, no byte of the medium, nothing flashed — and the run came back with a log.

## 3. And the rung-15 narration's own sentence was wrong about what `_cmd_gate_kind = 2` would mean

The narration for this arm's rung-15 base said that a `_cmd_gated = 1` with `_cmd_gate_kind = 2` would be
*"a fact about the block that no earlier rung could see"*. The measurement says otherwise: it was a fact
about **this run's own store two hundred lines earlier**. The quiet body wrote `0x34`; bit 15 is set by a
write and not cleared by one — which 730 and 733 had **already measured**; the gate below then saw it.

That is m739's shape pointing the other way: the two-point reading was sound, and the sentence beside it
named the wrong producer. **The prediction is now corrected in the narration's own voice** rather than
left for the next reader to re-derive: a `_cmd_gate_kind = 2` says *some earlier write in this run left
bit 15 set*, and only a run with no earlier write to `0x34` can read it as a fact about the block.

## 4. What the run's ending was, and what the runner said

The log ends the way 730's and 733's do — the kernel's own `No errors detected`, the device back on
Android on its own — so the ending is the ladder's normal shape and not this press's news. The runner's
own verdict is **14 PASS / 2 FAIL**, and **both FAILs are the one known storage-arm criterion**
(`seam_sp=0x80557ec8 is not sleh_sp-8`, a seam-arm check a storage arm cannot satisfy — `694`, `697`,
`705` and `rung 12` all carry it), so neither is about this arm.

## 5. What the next rung is

**Two facts decide it and both are already in hand.**

1. **The design rule**: the quiet store must not precede the gate. Either it moves (and then the block has
   carried a command, which is what the quiet read exists to avoid) or the arm that wants `st_cmd_path`
   to run writes `0x34` only after the gate — 733's shape, already measured.
2. **The question the ladder was on before this detour**: CMD1 **was answered** on 733's press
   (`_cmd1_resp = 0x40ff8080`), and the push from there is `mmc_attach_mmc`'s next statements — CMD2
   (`mmc_all_send_cid`, the first 136-bit response), CMD3 `mmc_set_relative_addr`, CMD9 `SEND_CSD`, CMD7
   `SELECT_CARD`, and then CMD17 at LBA 1, where 531 §9's self-verifying reading lives (the eight bytes
   `EFI PART`, or an MBR ending `55 aa`).

**And the one thing this press did not answer, stated so it is not mistaken for it**: the command path was
**not** re-measured. Rung 17 that continues the driver must therefore be built so the command path runs.

## 6. What the press was worth, in one line

It spent one press, answered the question 733 could not (**(A) is refuted**), reproduced two pre-registered
bit-15 cells exactly, cost the rung below its run through a self-inflicted and now-named ordering defect —
and held every safety property, with nothing sent and nothing flashed.

**It does not meet the goal.** 「把基础驱动跑起来」/「起码要能进入操作系统」 is still unmet — no storage, no
filesystem, and the OS is not observed to reach userland — so **TWRP-to-storage stays withheld** (the clause
is conditioned on 「如果os已经能进去了的话」).
