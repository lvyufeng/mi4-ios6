# 792: rung 31 — one bit out of CMD2's flag word, built and parked, and the last corner of a table the ladder already holds

**A BUILD, PARK AND RECORD. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO
FIRER.** `out/` moved: the arm in it was rung 30's and it is **spent**. One press spent by this step:
**none**. **NO PRESS IS AUTHORIZED AND NONE IS OWED** — the press path is the two commands readiness
prints, run by hand once each, and that is the operator's.

## 1. The reading this step acts on, and it came out of 791's census

791 put every CMD2 the ladder has ever sent beside every other one and found the immediate
`ERR | CRC` unique to the two arms whose CMD2 window carried the CRC enable. It then isolated the
correlate by putting the three commands that have been given a **wide** enable beside each other — and
the command word is a property of the **command**, not of the enable, so the three words are comparable
across arms:

| command | flags | word | response | CRC check in the word | what the wide window read |
| --- | --- | --- | --- | --- | --- |
| CMD1 `SEND_OP_COND` | `PRESENT` | `0x0102` | 48-bit | **off** | **completed**, bit 0 at poll 1,236, no error bit |
| CMD3 `SET_RELATIVE_ADDR` | `R1_NOIDX` | `0x030a` | 48-bit | **on** | `ERR | TIMEOUT`, **bit 17 never set** |
| CMD2 `ALL_SEND_CID` | `R2` | `0x0209` | **136-bit** | **on** | **`ERR | CRC` at poll 1, 17 ticks** |

**CMD3's word carries the same CRC check bit as CMD2's** — the `0x08` bit is set in both, and
`st_cmd3_noidx` publishes `_nidx_flags = 0x00000005` — so the CRC bit's presence is not explained by
the command's own CRC check. The one thing CMD2 has that no other command in this ladder has is a
**136-bit response request**. The missing corner of that table is 136-bit with the CRC check **off** —
and it is one bit.

## 2. The act: one constant, and it is written as a whole alternative

| | |
| --- | --- |
| command | `ALL_SEND_CID` (CMD2), opcode 2, argument 0 — unchanged |
| flags | `ST_MMC_RSP_R2_NOCRC` = `ST_MMC_RSP_R2 & ~ST_MMC_RSP_CRC` — **one bit out** |
| word | `0x0209` → **`0x0201`** |
| call site | `st_all_send_cid`'s one `st_send_command` — the same one |
| window / position / enable | rung 16's, **unchanged**: same `INT_ENABLE 0x34`, same five bits |

**No new address, no new width, no new megabyte, no new device and NO NEW KEY.** `_cid_flags` publishes
the value and `_cid_word` / `_cid_word_read` publish the folded word, so the entire change is readable
out of keys the ladder already carries. That is rung 23's and rung 30's shape one command over:
**ONE CONSTANT.**

**The two lines are written as a whole alternative rather than as a variable assigned in two arms**, so
at every value below 30 the source is the original text character for character and containment is
**structural** — and it is measured anyway, in §4.

**And dropping a CRC check from a 136-bit response is legal on the wire**: the card returns the CID
either way and the block simply does not check it, so an arm that completes here has **taken the CID**
and not merely removed a check. That is what makes its first row the largest single step available
without leaving early bring-up.

## 3. The clause reads the ARGUMENT, because a device surface does not move when a constant moves

The `-ge 16` clause group asserts what `st_all_send_cid` **reads and writes** — its device set, its
counts, its program order, its empty image side — and **none of those moves when a constant moves**.
`classify_body` reports ADDRESSES, and this arm changes an immediate in a register, which that clause
set cannot see at all.

So `src/entry/build_entry.sh` gained `xnu_entry_792`, which reads the one place the linked image is
unambiguous: **the argument-register setup immediately before the `bl` to `st_send_command`.** For a
four-argument call GCC emits `r3` first and `r0` last, so the last `mov r2, #imm` above that `bl` is the
flags word. The clause pins all three, and it fired on this build:

```
xnu_entry_792: rung 31's ONE CONSTANT, read out of the linked image at its call site: st_all_send_cid
loads [r0=#2 (ALL_SEND_CID), r1=#0 (no argument), r2=#3 (MMC_RSP_PRESENT | MMC_RSP_136 (word 0x0201))]
immediately before its bl to st_send_command (disassembly line 53 of the body at 0x8000de60)
```

— `r2=#3` at value 30 and `r2=#7` at value 29. **The expected value is DERIVED FROM THE SWITCH**, so
this is a **two-way** clause and not a rung-31 clause: a constant which leaked **down** the ladder is
refused by the same check that catches a rung which did not take effect. The opcode and the argument
are pinned alongside it, because this arm's whole claim is that they did **not** move.

**What the clause cannot see is covered on the other side of the call.** It reads the flags *argument*
and cannot read the command *word* the callee folds out of it, because `ST_SDHCI_CMD_FLAGS` runs inside
`st_send_command`. That side is carried by a `_Static_assert` pair in the source:

```c
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_ALL_SEND_CID, ST_MMC_RSP_R2_NOCRC) == 0x0201u, ...);
_Static_assert(ST_MMC_RSP_R2_NOCRC == (ST_MMC_RSP_R2 & ~ST_MMC_RSP_CRC), ...);
```

**One assertion on each side of the `bl`, and neither is a sentence in a comment.**

## 4. It contains rung 30, and the delta is NAMED rather than denied

Every new line sits inside `#if STAGE90_XNU_STORAGE_PROBE >= 30`, and the value-29 build from this same
source was compared against the **spent rung-30 park** `armed-storage-dfc4ae78`:

| member | value-29 build from this source | the spent rung-30 park | |
| --- | --- | --- | --- |
| `stage90-qcdt.img` | `8c86586a…` | `8c86586a…` | ✓ |
| `stage90.bin` | `f1c5e54c…` | `f1c5e54c…` | ✓ |
| `stage90-build-config.txt` | `6c2b6038…` | `6c2b6038…` | ✓ |
| `stage90.elf` | `96ea7684…` | `96ea7684…` | ✓ |
| `stage90.img` | `c71e1f4c…` | `c71e1f4c…` | ✓ |
| `stage90_fixture.macho` | `52bc9c35…` | `52bc9c35…` | ✓ |
| `SHA256SUMS.txt` | `58fad3cf…` | `58fad3cf…` | ✓ |
| `xnu_arm_entry.bin` | `dfc4ae78…` | `dfc4ae78…` | ✓ |
| `xnu_arm_entry.elf` | `2aae6a53…` | `2aae6a53…` | ✓ |
| `xnu_arm_entry-config.txt` | `25accb89…` | `25accb89…` | ✓ |
| `xnu_arm_entry-sources.txt` | `d51bf749…` | `f1162f01…` | **differs** |

**Ten of eleven are byte-identical, and the one that differs is the record of the SOURCE FILES — which
is by content.** Its diff is **exactly two lines of twenty-eight**, and they are exactly the two files
this step edited:

| file | was | is |
| --- | --- | --- |
| `build_entry.sh` | `5e9bca03…` | `07bce057…` |
| `entry_storage.c` | `6afc5d29…` | `cdf1056d…` |

**So the delta this arm carries at value 29 is not in the artifact at all**: the entry image, its ELF,
its switch record, the payload and its manifest are the rung-30 arm byte for byte, and the only member
that moved moved **because the source moved** — which is what that member is for. That is a stronger
statement than "all eleven identical": it **names** the delta instead of asserting there is none.

## 5. And the arm itself differs from rung 30 in TWO BYTES, in two instructions of one body

The value-30 entry image and the value-29 entry image are the **same size** (5,552,764 bytes) — and
770 measured that an unchanged size is **not** the claim that nothing moved — so the claim is measured
instead. `cmp -l` finds **exactly two differing bytes in the whole 5.5 MB image**, and they are two
instructions:

```
8000def8   mov r1, #7   ->   mov r1, #3     the _cid_flags PUBLISH's own argument
8000df08   mov r2, #7   ->   mov r2, #3     the FLAGS ARGUMENT to st_send_command
```

Two bytes in the entry bin and the same two in the entry elf (whose size is likewise unchanged at
6,732,404 bytes). **The rung-31 arm versus the spent rung-30 park, member by member:**

| member | rung 31 | vs rung 30 |
| --- | --- | --- |
| `stage90-qcdt.img` | `782c846f…` (8,572,928 B) | differs — **this is what `fastboot boot` sends** |
| `stage90.bin` | `98093d09…` | differs |
| `stage90-build-config.txt` | `6c2b6038…` | **byte-identical** to rungs 23–30, so no payload switch moved and `STAGE90_XNU_ENTRY` is 1 |
| `stage90.elf` | `63de2354…` | differs |
| `stage90.img` | `f49e242d…` | differs — a manifest member, not of the gate's own list |
| `stage90_fixture.macho` | `52bc9c35…` | **unchanged** across every arm since 574 |
| `SHA256SUMS.txt` | `0ccb3621…` | absolute-pathed; never checked with `sha256sum -c` |
| `xnu_arm_entry.bin` | **`f9613649…`** (5,552,764 B) | **the set name is this file's prefix**; +0 bytes |
| `xnu_arm_entry.elf` | `53316d0e…` (6,732,404 B) | +0 bytes |
| `xnu_arm_entry-config.txt` | `6f77b93a…` | **exactly two lines** differ from rung 30's: the probe number and the artifact's own sha256 |
| `xnu_arm_entry-sources.txt` | `8d915a1c…` | **exactly three lines**: that sha256 and the two source files above |

\(tools/verify_revert_set.sh out/stage90/frozen/armed-storage-f9613649 --set=armed-storage-f9613649`\)
reads **11 ok / 0 failed**, and every one of the eleven is `cmp`-identical between the live `out/` and
the park.

**And the gate's bound moved with the source rather than with a constant**: `preflight_boot_check.sh`
reads the ladder's own bound out of `entry_storage.c`'s `#if` guard, so raising it from 29 to 30
extended the range this record's value is checked against **without a number being written into the
gate**. `tools/report_int_enable_windows.py` still reports the five-bit word built at **four** sites and
reaching **four** stores.

## 6. The four rows, and what this changes about the next press

`armed-storage-f9613649` (`STAGE90_XNU_STORAGE_PROBE=30`, switch **value 30 = ordinal rung 31**), parked
at `out/stage90/frozen/armed-storage-f9613649` — eleven members, verified in place against the record.
Readiness resolves it by hashing the live `stage90-qcdt.img` and prints the two commands, and **no press
is spent without the operator's authorization**.

| row | reading |
| --- | --- |
| **`_cid_complete = 1`** | **THE CID IS TAKEN.** The driver order opens: CMD9 (CSD), CMD7 (select), CMD16 (block length), then data — the ladder stops asking what the block does with a malformed response and starts reading the card. |
| `ERR | TIMEOUT` (`0x00018000`) | the CRC latch needs the command's own CRC flag, and the real condition at CMD2 is a 136-bit response that never arrives — the subject moves to the card and to the power/clock path 784 gave a shape to. |
| **`ERR | CRC` (`0x00028000`) anyway** | **the strongest reading the arm can return**: the CRC bit would then not be about the command's CRC flag at all, which refutes 791 §4's isolation and puts the **136-bit path itself on the block**. |
| nothing over the full bound | the negative, read with the next window's own wide read beside it. |

**The control is in the same log**: `_cmd0_ticks` (4,901–4,909 across the seven arms carrying it, a
spread of 0.16 per cent) and CMD1's own completion at poll 1,236 bracket the new word from both ends, so
a completion in tens of ticks, a timeout at the bound and a CRC at poll 1 are distinguishable **without
a clock frequency**. `_cid_cmdlow_seen`, `_cid_ticks` and `_cid_polls` keep "the block never drove the
bus" apart from "the block drove it and no bit rose".

**And `0x40ff8080` is still not an answer on its own**: it stands in the `RESPONSE` registers on rungs
19, 29 and 30 whether the window was open or closed. On this arm the reading is `_cid_complete` and the
four-word 136-bit read beside it.

## 7. What this does not do

- **It spends no press and authorizes none.** The arm is armed and not pressed; whether it is pressed is
  the operator's decision, and no press is owed.
- **It does not say the card will answer.** Dropping the CRC check removes one way for the block to
  reject the response; it does not make a response arrive. Rows 2 and 4 above are as live as rows 1
  and 3.
- **It changes no register, no window, no width and no address outside `INT_ENABLE 0x34`.** The body's
  only writable register is unchanged, `SIGNAL_ENABLE 0x38` is still read and never written, and 784's
  rail reading stands: the eMMC's two rails are RPM resources with no `reg`, and no rung can power them
  with a store.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld.**

## 8. Owed, and named rather than left to be inferred

- **The press** — the arm is armed, the path is the two commands readiness prints, run by hand once
  each, and no press is spent without the operator's authorization.
- Unchanged from 790–791: a `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair around CMD1's own `RESPONSE`
  registers (COST); `c3.inhibit_timeout` not published for the `nidx` family (COST); the four
  `*_status_post` cells' masked reads (791 §5, COST); the `0x40ff8080` "the card ANSWERED" comment at
  CMD1's head (COST); the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303` (COST); a
  check that counts `ST_LIVE` sites per key (789 §4); the `_Static_assert` message's bit map at
  `entry_storage.c:2360` (789 §5); `_cid_ps_after`'s second producer (789 §2, COST); the set-comparison
  pad repair (779 §7); the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6);
  784's `rail_name` cell; and 783's window-scope check — landed at rung 30's window and not at the
  ladder's other windows.
