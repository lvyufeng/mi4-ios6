# 814 — the CSD fields come off the assembled word: the rung-35 arm withdrawn and repaired before its press

**A HOST-SIDE STEP THAT MOVES BYTES, AND THE FIRST ONE IN THIS PHASE THAT DOES SO INSTEAD OF ONLY READING.**
It edits `src/entry/entry_storage.c`, rebuilds the value-34 entry image and the payload, withdraws the
arm 812 parked, parks the repair, and adds a rule to `tools/check_response_word_order.py`. **NO DEVICE
ACTION: no press, no runner, no gate run against hardware, no firer.** The arm in `out/` is
`armed-storage-47c657af`, parked and **not pressed**, and the press is the operator's.

---

## 1. What this withdraws, and why a withdrawal is the cheap outcome

`armed-storage-6cd6fae8` — rung 35, switch value 34, CMD9 — was **armed and never pressed**. 814 found a
defect in it. Because the press had not been spent, the repair costs one build cycle and one park, and
**nothing measured on hardware is lost**: the arm's park stays in `out/stage90/frozen/` as the record of
what was built, and `records/revert-set.txt` carries a line saying in as many words that it is **not an
arm to press**.

Had the press been taken first, the defect would have come back as a *reading* — a set of CSD field
cells with the wrong values — and every one of the cells the rung exists for would have needed a
second, corrected press to reinterpret.

## 2. The defect, in one sentence

`st_send_csd`'s six CSD field decodes applied the shifts and masks of the vendor's
`UNSTUFF_BITS(resp, start, size)` to the four **raw** `RESPONSE` registers.

`resp[]` in that macro is **not** the register file. It is the array `sdhci_finish_command` *assembles*:

```c
resp[i] = sdhci_readl(RESPONSE + (3-i)*4) << 8;
if (i != 3) resp[i] |= sdhci_readb(RESPONSE + (3-i)*4 - 1);
```

which is `resp[i] = (raw_i << 8) | (raw_{i+1} >> 24)` — **one byte further along**. So every field came
out one byte too high, and the two the rung's own outcome table turns on — `_csd_structure` and
`_csd_mmca_vsn` — were read off `raw0`'s **top byte, which is the one byte the assembler shifts out and
throws away**.

**The fact was already written down, one rung below.** Rung 18's own comment says of the CID: the four
raw words hold "that same 32-bit value **one byte further along**". The sentence existed and nothing
bound it to arithmetic — `[[mi4-a-claim-in-a-comment-is-not-a-check]]`, and the same shape 810 spent a
whole rung on for the *word order*.

## 3. The measurement that decides it, from an answer already on record

The rung-34 press published the CID's four **raw** words *and* its four **assembled** words, and 811 §4
decoded the second set into the phone's own `ro.serialno`. So the same four bytes can be decoded both
ways and one of the two answers is already known:

| field | vendor macro over the **assembled** words | the same macro over the **raw** words |
| --- | --- | --- |
| `manfid` `UNSTUFF_BITS(120,8)` | `0x45` — SanDisk | `0x00` |
| `oemid` `UNSTUFF_BITS(104,16)` | `0x0100` | `0x4501` |
| `prod_name[0..5]` | `"SDW16G"` | `" SDW16"` |
| `serial` `UNSTUFF_BITS(16,32)` | `0x4a2fe00b` | `0x014a2fe0` |

`0x4a2fe00b` is `ro.serialno` — 811 §4's cross-check, unaffected. `0x014a2fe0` is not a serial number
and is not even eight digits. **The raw convention is off by exactly one byte, on every field, and the
witness for it is the phone's own serial number.**

The same substitution on the body's own six expressions, run on those same four words as a stand-in
(the CID is not a CSD, so the *values* are meaningless; the *difference* between the two readings is
the measurement):

| body expression | as written (raw) | the vendor's (assembled) |
| --- | --- | --- |
| `structure` | `0` | `1` |
| `mmca_vsn` | `0` | `1` |
| `cmdclass` | `0x534` | `0x445` |
| `read_blkbits` | `4` | `7` |
| `c_size_mult` | `6` | `2` |
| `c_size` | `3268` | `1241` |

The reproduction is `$CLAUDE_JOB_DIR/tmp/unstuff_check.py` in the session that wrote this; the
arithmetic is the vendor's own macro transcribed (`mmc.h`'s `UNSTUFF_BITS`, including the
`__size + __shft > 32` cross-word OR that `C_SIZE` needs).

## 4. Why it had to be fixed rather than reported

The arm's own closing comment reads:

> `_csd_complete = 1` with `_csd_structure = 1` and `_csd_mmca_vsn = 4` is a CSD and CMD7 is next;
> `_csd_complete = 1` with a structure that is neither 1 nor 2 is **738's stale-word trap, one rung up**.

A body that reads its fields off the wrong bytes therefore does not merely publish wrong numbers. **It
publishes a misleading verdict** — and the verdict it would have published is the one that sends the
next rung after a phantom. The failure mode of this defect is not "the cells are noisy"; it is "the log
accuses the card of a trap it did not set".

That is `[[mi4-one-value-two-definitions]]`: one quantity (the assembled response word) with two
readings (assembled and raw), the code silently using the one it did not mean.

## 5. The fix

The four assembled words are bound to locals `a0..a3` and the six decodes read `a0`/`a1`/`a2`. The four
published `_csd_resp*` cells **are unchanged in value** — they were always the assembled expressions, so
no log key moved and no key was added. Three comment blocks gained the distinction, including the
rung's own `#if` header.

**No switch moved.** Measured against the withdrawn arm's build:

| record | lines that differ | which |
| --- | --- | --- |
| `xnu_arm_entry-config.txt` | **exactly 1** | the artifact's own hash |
| `xnu_arm_entry-sources.txt` | **exactly 2 of 28** | the artifact's own sha256, and `entry_storage.c`'s content hash |
| `stage90-build-config.txt` | **0 — byte-identical** | no payload switch moved |

The sources manifest differing on `entry_storage.c` alone is the statement that `build_entry.sh` did not
move and no source was added or removed.

**`src/entry/entry_storage.c` is the peer's lane by the tree's own agreement, and the reason it is
edited here is availability and not the lane.** The agreement names `mi4-ios6-1a` as the owner of the
runner and the write gate; `ListAgents` at the moment of this step lists this session
(`run-experiment-526`) and two peers, **neither of them `mi4-ios6-1a`**, so the "report it to their lane"
step has no live addressee and the site is fixed for the COST/AVAILABILITY reason. If `mi4-ios6-1a`
resumes, this section and the record block are what it must read before touching the file.

## 6. The class is now a refusal, measured in both directions

`tools/check_response_word_order.py` gained **rule 4**: a variable whose whole value is a bare
`st_read32(... RESPONSE + K)` for K in {8, 4, 12}, and an inline read at those offsets, may not be the
left operand of `>>`. **Offset 0 is exempt and deliberately so** — the vendor's short-response branch
reads `resp[0]` straight out of that register with no shift, so a right shift of an offset-0 read is the
driver's own shape and not this defect.

It was measured **before it landed**, in both directions, over the whole of `entry_storage.c` — 4,600
lines, seven response-reading functions:

| text | findings | RAW-FIELD |
| --- | --- | --- |
| `git show HEAD:src/entry/entry_storage.c` — the arm as parked | **7** | **7** — all in `st_send_csd` (the six decodes and the capacity line) |
| the working tree — the fix | **0** | **0** |

So the rule's width is the defect's width: it refuses the seven sites and nothing else in the file.
`--selftest` grew four fixtures for it (two clean, two refused) to thirteen, and its closing sentence now
**counts** its fixtures instead of naming a number — a typed count is the thing 675 §4 measured the cost
of.

This is the project's standing rule: prefer a check that stops the build over a true sentence in a
comment. The sentence describing this exact byte was already in rung 18's comment and was not enough.

## 7. Containment, member by member against the withdrawn park

| member | `armed-storage-6cd6fae8` | `armed-storage-47c657af` | |
| --- | --- | --- | --- |
| `stage90-build-config.txt` | `6c2b6038…` | `6c2b6038…` | **byte-identical** |
| `stage90_fixture.macho` | `52bc9c35…` | `52bc9c35…` | **byte-identical** |
| `xnu_arm_entry.bin` | `6cd6fae8…` 5,552,764 B | `47c657af…` 5,552,764 B | size unchanged |
| `xnu_arm_entry.elf` | `8d77ff3a…` 6,732,556 B | `8a8761a1…` 6,732,556 B | **size unchanged** |
| `xnu_arm_entry-config.txt` | `e35251b7…` | `a249ea8b…` | one line |
| `xnu_arm_entry-sources.txt` | `bd7570b0…` | `62e9d8e2…` | two lines |
| `stage90-qcdt.img` | `e0a1cb5c…` | `b703a0a5…` | |
| `stage90.bin` / `.elf` / `.img` / `SHA256SUMS.txt` | — | all move | |

**Two of eleven byte-identical and nine moving — and that shape is the point.** 812's correction was
comment-only and left **ten** of eleven identical; this step changes *emitted code*, so everything that
embeds or lists the entry image must move, and the only two members that do not are the two that do
neither. A step that changed emitted code and left ten identical would be the defect, not the proof.

The park verified **11 of 11** against the record (`tools/verify_revert_set.sh … --set=armed-storage-47c657af`,
exit 0, 7 manifest-member checks agreeing), readiness is **5 of 5 exit 0**, `make check` is **exit 0**,
and the gate accepts the tree under `--allow-xnu-entry`.

**And the build clause is the reading, not this section.** `xnu_entry_812` reads `st_send_csd` out of the
**linked image** and asserts its device accesses in program order — the four `RESPONSE` words at 0x1C,
0x18, 0x14, 0x10 then the three CRC bytes at 0x1B, 0x17, 0x13 with none at 0x0F. The fix touches no
device access at all, and the clause was re-run against the repaired body as part of this build.

## 8. What the repaired arm will read, and the goal

The outcome table is unchanged. 813 §6's prediction is the same one rung stronger:

* `_csd_complete = 0` with `_csd_pre_state = 2` — the row the evidence already points at: **the CMD3
  wall confirmed from inside the same log**, naming CMD3's acceptance as the next subject.
* `_csd_complete = 1` with `_csd_structure = 1` and `_csd_mmca_vsn = 4` — a CSD, and CMD7 next. **Before
  this step the arithmetic that produces those two numbers could not have produced them**, so this row
  is now a cross-check rather than a coincidence.

**THE GOAL IS NOT MET.** No transfer completes, no filesystem is reached and no mount is made, so
**TWRP-to-storage stays withheld**. Rung 35 is **armed and not pressed** under the name
`armed-storage-47c657af`, and **the press is the operator's.**
