# 811 — the rung-34 press: the pre-registered row came back, and the card agreed with the bootloader

**A PRESS.** One gate exit 0 / 610 lines, **one runner EXIT 0**; `fastboot boot` only; nothing flashed,
nothing written to storage, no reboot commanded; `33e80afe` absent from both device lists. The phone
came back into Android 10 unattended. Capture
`out/stage90/captures/rung34-cidgate-20260928-144115-last_kmsg.txt`, 636,949 B, sha `7bccf28f…`.
Nothing the arm owns moved: `xnu_arm_entry.bin` is still `7341f5f6…`, `stage90-qcdt.img` still
`b9319777…`.

---

## 1. The arm

`armed-storage-7341f5f6`, switch **value 33 = ordinal rung 34** — the CMD2 gate given the word the
driver actually tests. Built and parked by 804 (`b122509`), eleven members verified. The one condition
that changed: `(c1.resp & ST_MMC_CARD_BUSY) == 0u` (the PROBE's word) became `op_busy != 0u` (the
LOOP's result, the value rung 33 computes and throws away).

## 2. Row 1 of the four, and it is the row that matters

804 pre-registered four rows and named row 1 as the expected one. It came back, and the two cells it
added are published and read exactly as stated:

```
xnu_live_storage_cid_gate_word = 0xc0ff8080
xnu_live_storage_cid_gate_busy = 0x00000001
xnu_live_storage_cid_gated     = 0x00000000
```

`_cid_gate_word` **is** `_opcond_last_resp` (`0xc0ff8080`) exactly, and it is off `_cmd1_resp`
(`0x40ff8080`) by exactly the busy bit, `0x80000000` — 804's own signature for row 1. The gate tested
the loop's word, and the word 803 measured on one boot at rung 33 is the word the gate now reads. Row
4 — `_cid_gate_busy = 0` beside `_cid_gated = 0`, which 804 named *impossible by construction* — did
not occur. The gate passed, CMD2 went out, and the CID came back.

## 3. Every rung-33 cell reproduced

Measured cell by cell across the two captures:

| | |
| --- | --- |
| storage cells at rung 33 | 495 |
| storage cells now | 497 |
| keys lost | **0** |
| keys added | **2** (the two in §2) |
| values identical | **474** |
| values moved | 21 |

Twenty of the 21 are the free-running quantities a boot cannot repeat: poll counts, clock ticks, and
the two absolute power-IRQ timestamps (`_pwr_irq_at`, `_pwr_wait_t0`). The twenty-first is
`_cid_inhibit_last` `0x00f80000 → 0x01f80000` — **one bit**, bit 24, the CMD line's level in
PRESENT_STATE. That is a bus-line reading taken at a slightly different instant, and the poll's own
verdict is unchanged at both rungs (`_cid_inhibit_seen = 0`, `_cid_inhibit_timeout = 0`). It is named
rather than left in the moved list, because a reading that moved is the kind of thing this record does
not let hide.

**And the decision cells are byte-identical.** `_cmd1_resp = 0x40ff8080`, `_cmd1_resp_busy = 0` — the
probe's word with bit 31 clear, which is the reading that made the *old* gate pass, and the reason
nine rungs of `_cid_gated = 0` were uninformative. `_opcond_last_resp = 0xc0ff8080`,
`_opcond_busy_seen = 1`, `_opcond_sends = 2`. And all nine CID cells: the four raw words
(`0x00450100`, `0x53445731`, `0x3647014a`, `0x2fe00bb1`), the four the 136-bit assembler derives
(`0x45010053`, `0x44573136`, `0x47014a2f`, `0xe00bb100`), and `_cid_resp_short = 0x2fe00bb1`. Plus
`_cid_complete = 1`, `_cid_err = 0`, `_cid_status_any = 0x1` (RESPONSE, no error bit), and
`_nidx_resp = 0x00000500` (RCA `0x0500`).

## 4. A reading that is new: the card agreed with the bootloader

The 128-bit CID assembled by the driver's word-3-first arithmetic is

```
450100534457313647014a2fe00bb100
```

The vendor's own decode path — `mmc_decode_cid` (`mmc.c:83`), cases 2/3/4 at `:120`, the eMMC v4 path
this card takes — reads `card->cid.serial = UNSTUFF_BITS(resp, 16, 32)`, i.e. bits `[47:16]`. That is
**`0x4a2fe00b`**, and `4a2fe00b` is **this phone's own serial number**: `adb devices` lists it as the
serial, and the phone itself reports `ro.serialno = ro.boot.serialno = 4a2fe00b` with
`ro.boot.bootdevice = msm_sdcc.1`.

So the bootloader read the same field out of the same card, through its own code, before this image
existed — and the value the ladder assembled agrees with it. **That is an independent cross-check of
the quantity 810 just made a build refusal**, and it is the strongest statement available about the
arithmetic short of a second card: a reader who assembled the four words in the wrong order, or
dropped a byte, would not reproduce the phone's own serial number. (The product-name field decodes to
`DW16G` under the SanDisk manufacturer id `0x45` — 803's `SDW16G`, reached the same way.)

## 5. The return is the same return

The device came back **28 s** after `fastboot boot`. The rung-33 press measured **28 s** on the same
criterion; rung 32 measured **26 s**. The runner's own note gives this arm's two candidates as ~28 s
if the SoC's watchdog countdown fired and ~90 s if the bounded spin did — so **28 s is the countdown**:
the reset is the hardware watchdog, the same net and the same interval as the press before it.

The runner's criteria read **7 PASS / 1 FAIL / 3 UNREAD** (printed twice in one log, so counting
*lines* gives 14 / 2 / 6 — 756's arithmetic). **The one FAIL is the known `seam_sp` criterion, the same
FAIL at rung 33** (`seam_sp=0x80557ec8 is not sleh_sp-8`), so **this press added no failure**; the
three UNREADs are the same `SLOT_NULL` absences (`_slot_pre_calls`, `m8`/`m4`) the last press reported.

## 6. What it means

804's repair is **behaviour-preserving on this card**: the gate now reads the word the driver reads, it
passes on the same boot the old gate passed on, and every decision cell of rung 33 reproduces to the
bit. That was 804's row 1, and it is why 804 said the repair is what makes CMD9 safe to build on.

**Row 2 did not occur.** `_cid_gated = 1` with reason 2 — the loop never seeing bit 31, so the repair
*changed* the outcome — is therefore still untested, and this document states it as untested rather
than as absent. The card's boot-to-boot readiness remains a question for a future press, not a claim.

## 7. What this does not do, and the goal

It does not transfer a block. The ladder has sent CMD0, CMD1 (twice), CMD2 and CMD3 and has read a CID
and an RCA; **no data line has ever carried anything**, and no command above CMD3 has ever been sent.

**THE GOAL IS NOT MET.** No transfer completes, no filesystem is reached and no mount is made, so
**TWRP-to-storage stays withheld**. The arm is **SPENT**.

**THE NEXT RUNG IS CMD9** (`SEND_CSD`, `ac R2`): 809 measured that it needs no new mechanism — its R2
flags, its 136-bit assembler and its RCA argument all already exist because CMD2 built them — and 810
made the one part of it that could fail silently a build refusal. **No press is authorized or pending
for it.**
