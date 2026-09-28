# 812 — the driver's first command above CMD3: CMD9, and the argument the driver assigns itself

**A BUILD, A PARK AND A RECORD.** No press, no device action, no runner, no firer; `out/` was
rebuilt and a new arm is in it. **THE ARM IS ARMED AND NOT PRESSED AND THE PRESS IS THE OPERATOR'S.**
`armed-storage-6cd6fae8`, `STAGE90_XNU_STORAGE_PROBE=34` (switch VALUE 34 = ordinal rung 35), eleven
members parked at `out/stage90/frozen/armed-storage-6cd6fae8/`.

---

## 1. What this rung is

`mmc_attach_mmc`'s statement after `mmc_set_relative_addr` is `mmc_send_csd(card, card->raw_csd)`
(`mmc.c:1420`), which is `mmc_ops.c:296-302` — `mmc_send_cxd_native(card->host, card->rca << 16, csd,
MMC_SEND_CSD)` — with `cmd.flags = MMC_RSP_R2 | MMC_CMD_AC` (`mmc_ops.c:224`). `mmc.h:38` says the same
three things: `MMC_SEND_CSD 9` / `ac [31:16] RCA R2`.

So this rung is **the ladder's second 136-bit response and its first with a non-zero argument**: one
new command body, `st_send_csd` (`entry_storage.c:4534`), guarded `#if STAGE90_XNU_STORAGE_PROBE >= 34`,
putting the driver's own word `0x0909` (opcode 9, `RESP_LONG 0x01 | CRC 0x08`, no `INDEX`) on the bus
with argument `ST_MMC_RCA_1` and flags `ST_MMC_RSP_R2`, and one call for it on the line below rung 21's
CMD3 (`:5358`). It opens and closes the same five-bit `INT_ENABLE 0x34` window every command body above
it opens, publishes the same per-command cell set, and assembles the response in the driver's
word-3-first order. A new opcode define (`:2226`), a new `_Static_assert` on the folded word (`:2335`),
and the ladder's own bound raised `> 33 → > 34` (`:130`).

809 measured that CMD9 needs **no new mechanism** — its `R2` flags, its 136-bit assembler and its RCA
argument all exist because CMD2 built them — and 810 made the one part of it that could fail silently a
build refusal. This rung is the arm that tests whether that was true, and it is.

## 2. The argument, and a claim three documents carried that is false

**CMD9's argument is a constant the DRIVER assigns, not a word the card returned.** `card->rca` is
assigned by the driver at `mmc.c:1400` — ``card->rca = 1`` — nine lines above the CMD3 that carries it,
and `mmc_set_relative_addr` sends it as `card->rca << 16` (`mmc_ops.c:203`).

**An MMC card does not return an RCA to the host.** `mmc.h:32` gives CMD3 the response `R1`, which is
the 32-bit **card status**: `R1_CURRENT_STATE` is its bits 12:9 (`mmc.h:141`), `R1_READY_FOR_DATA` its
bit 8 (`mmc.h:142`), and **there is no RCA field in it at any offset**. (An SD card returns one, in R6
— and even there the field is bits 31:16.)

So a sentence the archive has been carrying is false. 811 §3 read `_nidx_resp = 0x00000500` as
*"RCA `0x0500`"* — i.e. it read bits 15:0 as a field that is not one — and 803's block carries the same
reading. **The same word under the vendor's own two macros is `R1_READY_FOR_DATA` set with
`R1_CURRENT_STATE = 2`**, and 2 is `R1_STATE_IDENT` (`mmc.h:149`) — the state a card is in *after*
CMD2 and *before* a CMD3 it has accepted.

This is not a matter of interpretation, because **the ladder's own published cell says it too**:
`_nidx_state` is `(c3.resp >> 9) & 0xF` (`entry_storage.c:4404` region) and reads **2** in the committed
rung-34 capture, while `_nidx_ready` is `(c3.resp >> 8) & 1` — the vendor's own `R1_READY_FOR_DATA` bit.
Two readings of one word — the `one value, two definitions` class — and the one with no basis in either
response format is the one the record used.

## 3. Why that is the frontier, and what this arm does about it

CMD9 is legal in **STBY** and **TRAN**, and not in **IDENT**. And this ladder has never had a CMD3 the
card took.

So the frontier this arm meets is not CMD9's mechanism; it is that **no arm has yet moved the card out
of IDENT**. (803's `_nidx_resp` shows the register did receive a new word during CMD3 —
`_nidx_resp_pre = 0x2fe00bb1`, the CMD2 leftover, against `0x00000500` — and the word it received says
IDENT. Whether that is a card that declined the new RCA, or the block eking out a word for a command the
card never processed, is not settled by any capture on record. **The state is settled; the cause is
not**, and this document says so rather than choosing.)

**This rung does not assume the precondition, and it does not gate on it either — it publishes it.**
`RESPONSE 0x10` is read **between CMD3 and CMD9** — a moment no rung has read at, which is rung 18's own
shape (`st_resp_before`) and not a new idea — and published raw and decoded: `_csd_pre_resp`,
`_csd_pre_state`, `_csd_pre_ready`, `_csd_pre_illegal`. `_csd_pre_state = 3` or `4` is the reading that
says the card was listening; `2` says it was not, and says so in the same log that reports what CMD9 did
with it.

A gate here would have been worse than no gate: it would turn *"the card is not ready for this command"*
into *"the command failed"* — the conflation 804's own repair is about, one rung down.

## 4. The four words are decoded with the vendor's own offsets

`mmc_decode_csd` (`mmc.c:147`) reads `csd->structure = UNSTUFF_BITS(resp, 126, 2)` **first** and returns
`-EINVAL` from `mmc.c:162` unless it is non-zero, then `csd->mmca_vsn = UNSTUFF_BITS(resp, 122, 4)`
(`mmc.c:165`). On the eMMC v4 path this card takes (`mmc.c:110` — the same `case 2/3/4` that carries the
CID's 32-bit serial), the CSD is v1.2, so:

> **`_csd_structure` must read 1 and `_csd_mmca_vsn` must read 4.**

Two values this image did not supply and could not have chosen — **the same free cross-check the CID's
`serial` gave against `ro.serialno`** (811 §4, `0x4a2fe00b`). A reader who assembled the four words in
ascending order, or dropped a byte, or OR'd a byte onto the last word, would not get 1 and 4.

The rest is `mmc.c:165-196` verbatim, each field written as the vendor's own call with its shift and
mask so a reader can check the arithmetic instead of trusting a helper: `cmdclass` at bits 95:84,
`read_blkbits` at 83:80, `C_SIZE` at 73:62 (which spans the `w2`/`w1` boundary and is written as the
vendor's own two-piece expression), `C_SIZE_MULT` at 49:47.

**`_csd_capacity_blocks` is expected to be a placeholder on this card, and that is stated rather than
left to be misread.** A 12-bit `C_SIZE` cannot express 16 GB, which is why `mmc_get_ext_csd`
(`mmc.c:201`) says in the vendor's own words that high-capacity cards store a "magic" size in the CSD
and take the real capacity from the EXT_CSD — which is CMD8, **809's wall**, named one rung early by
arithmetic.

## 5. The build clause is the reading, not this document

`build_entry.sh` gained `xnu_entry_812`, which reads the arm out of the **linked image**. Every number
in it was measured before it was written, and it asserts something 810's source check cannot:

```
r0 = 9        the opcode, SEND_CSD (mmc.h:38)
r1 = 65536    ST_MMC_RCA_1 - the value the DRIVER assigns (§2)
r2 = 7        ST_MMC_RSP_R2 = PRESENT | 136 | CRC, the driver's own flags for CMD9
```

and the body's device accesses **in program order, distinct**:

| # | access | what it is |
| --- | --- | --- |
| 1 | `f9824910:ldr` | `RESPONSE 0x10` — the **precondition** read, before the window opens |
| 2 | `f9824934:str` | `INT_ENABLE 0x34` — the window **opens** |
| 3 | `f9824934:ldr` | `INT_ENABLE` — `_csd_ena_held` |
| 4 | `f9824938:ldr` | `SIGNAL_ENABLE` |
| 5 | `f982491c:ldr` | `RESPONSE 0x1C` — **word 3**, `sdhci.c:1167`'s `(3-i)*4` |
| 6 | `f9824918:ldr` | `RESPONSE 0x18` — word 2 |
| 7 | `f9824914:ldr` | `RESPONSE 0x14` — word 1 |
| 8 | `f982491b:ldrb` | `RESPONSE 0x1B` — the CRC byte **one below** word 3 |
| 9 | `f9824917:ldrb` | `RESPONSE 0x17` — one below word 2 |
| 10 | `f9824913:ldrb` | `RESPONSE 0x13` — one below word 1 |
| 11 | `f9824930:ldr` | `INT_STATUS 0x30` — `_csd_status_post` |
| 12 | `f9824924:ldr` | `PRESENT_STATE 0x24` — `_csd_ps_after` |

**and there is no byte at `0x0F`** — `sdhci.c:1168`'s `if (i != 3)` gives the last word no byte, which is
a property visible in an address list and nowhere else. A byte at `0x0F` would be a **stale byte OR'd
onto word 0**; `0x0B` or `0x1F` would be the pairing off by one; an **ascending** order (0x10, 0x14,
0x18, 0x1C) would be the same eight accesses with the four words reversed. **All three parse, decode and
publish a plausible CSD-shaped number with no fault anywhere** — which is why the clause asserts the
**set** and the **order**, and not only the set.

**And the count `f9824910:ldr = 2` is the other half of the same claim.** `RESPONSE 0x10` is read
**twice** — once as the precondition, once as word 0. A compiler that **commoned** the two loads would
leave a count of 1 with the order list still reading correctly, and the arm would publish the card's
state taken at the wrong moment with nothing on the wire to show it. So the count is asserted too.

The clause also asserts the key strings `_csd_pre_state`, `_csd_op`, `_csd_structure` and
`_csd_mmca_vsn` are in the image (the precondition is a **register** and not a load, so the access list
cannot see it; the two decodes are the self-check), that the body's non-device side is **empty**, and
that `st_cmd_path` calls `st_send_csd` **exactly once** (zero is a switch no build reads, m720's shape;
two is two CMD9s in one boot).

Its first run printed each of these from the artifact, unchanged from the pre-measured expectation:

```
xnu_entry_812: ... st_send_csd (at 0x8000e768) loads [r0=#9 (SEND_CSD), r1=#65536 (ST_MMC_RCA_1),
r2=#7 (ST_MMC_RSP_R2)] ... in program order are [f9824910:ldr f9824934:str f9824934:ldr
f9824938:ldr f982491c:ldr f9824918:ldr f9824914:ldr f982491b:ldrb f9824917:ldrb f9824913:ldrb
f9824930:ldr f9824924:ldr] ... st_cmd_path calls it 1 time
```

## 6. The newest copy of one arithmetic, found by the check rather than by review

`tools/check_response_word_order.py` (810) counted **six** response-reading functions before this rung.
It now prints

```
note    src/entry/entry_storage.c:4534 st_send_csd() reads the response register, 8 access(es)
1 entry source(s) read the response register, across 7 response-reading function(s);
0 divergence(s) from the driver's word order
```

— **seven functions, zero divergences, and `st_send_csd` named without anyone telling the check it
exists.** So this rung's body agrees with the six above it **by construction** rather than by review,
which is what 810 §8 promised and what this rung is the first to live under.

**AND THE LINE NUMBER IN THAT QUOTE IS A CORRECTION, MADE WHILE WRITING THIS SECTION.** The number the
check printed when it found this rung's body was **`:4475`** — and `st_send_csd`'s definition is at
`:4534`, 59 lines below it. **Every line that tool printed was off, by up to 84 lines**, and it was off
for a reason that made it wrong in a way a reader could not see: `strip_comments` blanks comments while
keeping their line structure, but `statements()` then joins a logical statement, and a body that follows
a `#if STAGE90_XNU_STORAGE_PROBE >= N` and its documentation comment is joined **with** them — so the
reported "first line" was the top of that body's own comment block, not the body. It printed
`2612 3373 3612 3961 4111 4226 4475` for definitions at `2630 3432 3672 4023 4146 4310 4534`; each of
the seven is now exact (a count of newlines to the name group, so it survives the file growing above the
function), and the nine-fixture `--selftest` is unchanged at ok.

That is the class this document is about, one level up: **`path:line name()` is printed *as* a citation,
and a citation that does not resolve is not a citation.** It is also why three documents carried three
different numbers for one site — this doc's §6 said `:4475`, 812's own record block said `:4460`, and the
tree says `:4534` — which is `[[mi4-one-value-two-definitions]]` reached through a tool rather than
through a response register.

**AND THE SAME MEASUREMENT, APPLIED TO THE VENDOR TREE, CAUGHT EIGHT MORE.** Every citation this rung
makes into `external/android_kernel_xiaomi_cancro/` was re-measured against those files while writing
this section, and eight of them were wrong:

| written | measured | what it names |
| --- | --- | --- |
| `mmc.c:1416` | **`mmc.c:1420`** | `mmc_send_csd(card, card->raw_csd)` — §1's whole premise |
| `mmc.c:150` | **`mmc.c:147`** | `static int mmc_decode_csd(...)` |
| `mmc.c:152` | **`mmc.c:162`** | the `-EINVAL` the structure check returns |
| `mmc.c:165-186` | **`mmc.c:165-196`** | the decode body's range |
| `mmc.c:195` | **`mmc.c:201`** | `static int mmc_get_ext_csd(...)` |
| `sdhci.c:1165` | **`sdhci.c:1167`** | `SDHCI_RESPONSE + (3-i)*4` |
| `sdhci.c:1169` | **`sdhci.c:1168`** | `if (i != 3)` |
| "eight lines above it" | **nine lines** | `card->rca = 1` (1400) to `mmc_set_relative_addr` (1409) |

They are corrected **in the source comments and in the narration as well as here**, because the same
sentence was written in four places and a citation is a claim wherever it sits. `mmc.c:1400`, `mmc.c:1409`,
`mmc.c:83`, `mmc.c:120`, `mmc.h:32/38/141/142/149`, `mmc_ops.c:203/214/224/296/301` and the
`sdhci.c:1165-1172` / `1169-1175` ranges were re-measured and are correct as written.

**AND NOTHING IN `make check` COULD HAVE CAUGHT THEM, WHICH IS WHY THEY SURVIVED A RUNG.** 808's
`check_line_citations.py` reports **NOT-IN-REPO 165** of the 237 citations in the live arm's own prose:
`external/` is not tracked, so `git` — the baseline 808 chose for "has this site moved" — has no answer
for it, and a vendor citation is *unchecked* rather than checked-and-green. **The gap is recorded rather
than closed here**, because closing it means a different baseline (the vendor tree's own file content, or
a pinned revision) and that is a step of its own.

## 7. Containment: the value-33 build from this same source

**Measured, not asserted.** 812's edits were rebuilt from the *same* source with only the switch
changed — `STAGE90_XNU_STORAGE_PROBE=33` (switch value 33 = ordinal rung **34**) — and the resulting
`out/` compared, member by member, against the spent rung-34 park
`out/stage90/frozen/armed-storage-7341f5f6/`:

| member | park `armed-storage-7341f5f6` | value-33 rebuild from 812's source | |
| --- | --- | --- | --- |
| `xnu_arm_entry.bin` | `7341f5f6…` 5,552,764 B | `7341f5f6…` 5,552,764 B | **identical** |
| `xnu_arm_entry.elf` | `6f66302f…` 6,732,528 B | `6f66302f…` 6,732,528 B | **identical** |
| `xnu_arm_entry-config.txt` | `b5568d2d…` | `b5568d2d…` | **identical** |
| `stage90-build-config.txt` | `6c2b6038…` | `6c2b6038…` | **identical** |
| `stage90_fixture.macho` | `52bc9c35…` | `52bc9c35…` | **identical** |
| `stage90.bin` | `d15fd4e7…` | `d15fd4e7…` | **identical** |
| `stage90.elf` | `e9eb4db1…` | `e9eb4db1…` | **identical** |
| `stage90.img` | `85b549e5…` | `85b549e5…` | **identical** |
| `stage90-qcdt.img` | `b9319777…` | `b9319777…` | **identical** |
| `SHA256SUMS.txt` | `d8093e4f…` | `d8093e4f…` | **identical** |
| `xnu_arm_entry-sources.txt` | `ce929b8c…` | *moves* — see below | **2 of its 28 lines**, and its own hash is a function of the tree rather than of the arm |

**Ten of eleven members are byte-identical, and the eleventh is the manifest that is *supposed* to
move.** `xnu_arm_entry-sources.txt` is not part of the image — it records the sha256 of each source the
entry build read — so it moves exactly when a *source* moves, which is 812's whole edit. The two lines
it moves are precisely the two files 812 touched:

```
8c8
< 82af78ed…  build_entry.sh          # the xnu_entry_812 clause (§5)
---
> (a hash of build_entry.sh as it stood at that measurement)
21c21
< 7b55789a…  entry_storage.c         # the CMD9 body, define, assert and call (§1)
---
> (a hash of entry_storage.c as it stood at that measurement)
```

**Line 5 — the manifest's own `STAGE90_XNU_ENTRY_SHA256` — is not in that diff**, i.e. the value-33
rebuild reproduces the spent park's embedded entry hash as well as its bytes. So the statement is
sharper than "10 of 11 agree": **every byte of every artifact is identical, and the only file with a
`<`/`>` pair is the record of which sources were read.**

That is what containment has to mean for a cumulative ladder: the edits are inside whole-`#if`
alternatives, so **no spent arm's emitted code moves**, and the one file that moves is the one whose job
is to say the source moved.

**AND THE MEASUREMENT WAS TAKEN THREE TIMES, ON THREE SUCCESSIVE SOURCE STATES, BECAUSE §6 CORRECTED THE
SOURCE TWICE WHILE THIS DOCUMENT WAS BEING WRITTEN.** The tool fix and the eight vendor citations each
edited `build_entry.sh` and `entry_storage.c` again, so the value-33 build was re-run after each:
**every time, the same ten of eleven members came back byte-identical and the same one file moved on the
same two lines.** Only that file's *own* hash changed — `927f36a3…` → `609172e7…` for the two
re-measurements, and the arm's own manifest hash `cc06c705…` → `bd7570b0…` across the same edits — and it
changed **because the manifest is a hash of the sources and the sources were edited**, which is the
manifest doing its job rather than a containment that failed. That is why the row above says *moves*
instead of quoting one of those numbers as though it were the arm's: **the claim being made is about the
emitted artifacts, and not one byte of them moved.** The eight rows above are the last re-measurement.

**The containment build leaves `out/` at value 33, so `out/` was rebuilt at value 34 afterwards.** A
reader who finds this record beside an `out/` whose entry image is `7341f5f6…` has found the
containment build's output, not the arm; the arm in `out/` is `6cd6fae8…` and both have been parked.

## 8. The arm's own delta, and its reproducibility

Measured against the spent rung-34 park `out/stage90/frozen/armed-storage-7341f5f6/`:

| member | rung 34 (spent) | rung 35 |
| --- | --- | --- |
| `xnu_arm_entry.bin` | `7341f5f6…` 5,552,764 B | `6cd6fae8…` 5,552,764 B — size unchanged |
| `xnu_arm_entry.elf` | `6f66302f…` 6,732,528 B | `8d77ff3a…` 6,732,556 B — **+28 bytes** |
| `xnu_arm_entry-config.txt` | `b5568d2d…` | `e35251b7…` — **two lines move**: the artifact's own hash, and `STORAGE_PROBE 33 → 34` |
| `xnu_arm_entry-sources.txt` | `ce929b8c…` | `bd7570b0…` — **three of its 28 lines move** (the `STAGE90_XNU_ENTRY_SHA256` line and two of its 23 file entries) |
| `stage90-build-config.txt` | `6c2b6038…` | **byte-identical** — no payload switch moved |
| `stage90_fixture.macho` | `52bc9c35…` | **byte-identical** |
| `stage90-qcdt.img` | `b9319777…` 8,572,928 B | `e0a1cb5c…` 8,572,928 B |
| `stage90.bin` / `.elf` / `.img` / `SHA256SUMS.txt` | — | all move |

The four that move without being the entry image are the four that **embed or list** it: the qcdt the
press sends carries the entry image inside it. `stage90-build-config.txt` being byte-identical is the
statement that **no payload switch moved** — the payload rebuild is the embedded entry image alone.

**AND THE ARM IS REPRODUCIBLE, MEASURED FIVE TIMES.** The value-34 entry build was run **five** times —
once before `xnu_entry_812` existed, once after it, and three more during this document's own correction
pass (§6) — and **all five runs produced the same two artifacts**, `xnu_arm_entry.bin` = `6cd6fae8…` and
`xnu_arm_entry.elf` = `8d77ff3a…`, with `xnu_arm_entry-config.txt` unchanged at `e35251b7…` and the
payload members unchanged with them. **The inputs that are not compiled changed twice in that window** —
a shell message's backticks and eight vendor citations, both in comments or in strings — and the bytes
did not move: `entry_storage.c` and `build_entry.sh` are read by the preprocessor and by `objcopy`, not
linked. **A build clause that moved the bytes it reads would be the very defect it exists to catch**, one
level up; it does not. And the one file that *did* move each time is `xnu_arm_entry-sources.txt`, whose
job is exactly to record that a source changed — `cc06c705…` → `bd7570b0…` — which is the manifest
answering the question the gate asks it (`§7`).

## 9. What the press would decide, and each outcome names the next act

* `_csd_complete = 1` with `_csd_structure = 1` and `_csd_mmca_vsn = 4` — **a CSD**, and CMD7 is next in
  the driver's order.
* `_csd_complete = 1` with a structure that is neither 1 nor 2 — a 136-bit register read that is **not**
  a response to this command: 738's stale-word trap, one rung up.
* `_csd_complete = 0` with `_csd_pre_state = 2` — the wall is where §3 says the measurement already
  points: **CMD3 has never been accepted**, the card is in IDENT, and CMD9 is a command it is not
  obliged to answer.

**The third is the row this arm's own evidence predicts**, and the arm is built to say so out loud
rather than to report it as a failure of CMD9.

## 10. What this does not do, and the goal

It does not transfer a block, it does not reach a filesystem and it does not mount anything. It does not
settle *why* the card is in IDENT — §3 states the state and leaves the cause open. And it does not claim
CMD9 will be answered: it claims that **if** it is, the answer is checkable from outside the image, and
that **if** it is not, the log says which precondition failed.

**THE GOAL IS NOT MET.** No transfer completes, no filesystem is reached and no mount is made, so
**TWRP-to-storage stays withheld**. Rung 35 (`armed-storage-6cd6fae8`, switch value 34) is **ARMED AND
NOT PRESSED**, and **the press is the operator's**.
