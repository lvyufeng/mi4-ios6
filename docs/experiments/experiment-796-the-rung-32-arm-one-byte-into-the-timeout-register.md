# 796: the rung-32 arm — one byte into the register this ladder has never written, and the first repair it has ever made

**A BUILD, PARK AND RECORD. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO
FIRER.** `out/` moved: the arm in it is rung **32** and it is **ARMED AND NOT PRESSED**. One press
spent by this step: **none**. **NO PRESS IS SPENT OR AUTHORIZED HERE** — the press path is the two
commands readiness prints, run by hand once each, and that is the operator's.

| | |
| --- | --- |
| arm | `armed-storage-c1f89600`, `STAGE90_XNU_STORAGE_PROBE=31` (switch **value 31 = ordinal rung 32**) |
| boot image | `out/stage90/stage90-qcdt.img` sha256 `bda29ed1…`, 8,572,928 B |
| park | `out/stage90/frozen/armed-storage-c1f89600/`, **eleven members**, verified **11 ok / 0 failed** |
| the act | **one byte store to `TIMEOUT_CONTROL 0x2E`**, value `0x03`, in a window around CMD2 only |

## 1. The reading this step acts on, and it is 795's own

795 read rung 31's press and found the frontier is **not** CMD2 and **not** the card. It is a register:

| | |
| --- | --- |
| `grep -n 'ST_SDHCI_TIMEOUT_CONTROL' src/entry/entry_storage.c` | the define (`:212`) and **ONE READ** (`:4233`) — **no store, on any rung** |
| rung 31's own capture | `_nidx_tout_ctl = 0x00000000` — the **reset value** and the shortest bound the register can express |
| the vendor | writes it only from `sdhci_prepare_data` (`sdhci.c:827-828`), inside `if (data \|\| (cmd->flags & MMC_RSP_BUSY))` |
| every command this ladder sends | **data-less**, so that branch has never run here |

So the **665.2 µs** that CMD2 and CMD3 both hit — and that CMD1 completes **130 µs inside** — is this
block's own default, set by a register no rung has touched, and not a property of the card.

## 2. The act, and the two ends put on the value at compile time

**One byte store, `ST_SDHCI_TIMEOUT_CMD2 = 0x03u` = 5,321.6 µs.** The field is a power-of-two
multiplier of the reset bound, so:

| | 665.2 µs reset bound | the value's two ends |
| --- | --- | --- |
| `0x03` = 5,321.6 µs | **8.00×** | — |
| 795 §5's extrapolated 136-bit arrival, 1,048.2 µs | 5.08× **above** it | the assumption is the per-bit rate, and 795 §5 names it |
| the driver's own 1.200 s poll bound (`sdhci.c`'s `timeout = 10` in 100 ms units) | **225× below** it | at or above `0x0B` the ladder's own poll gives up first and the cell stops meaning anything |

Both ends are on the constant **at compile time**, in `entry_storage.c`:

```c
_Static_assert(ST_SDHCI_TIMEOUT_CMD2 >= 0x01u, "…");   /* 0 would write back the byte it found */
_Static_assert(ST_SDHCI_TIMEOUT_CMD2 <= 0x0Eu, "…");   /* the register's own maximum */
_Static_assert((ST_SDHCI_TIMEOUT_CMD2 & 0xF0u) == 0u, "…");
```

## 3. The window, and the one measurement it deliberately does not cover

Two bodies of their own — the shape 787 used for CMD1's window, so that `st_cmd_path`'s own device
surface stays rung 14's at the digit:

| body | address | size | device surface |
| --- | --- | --- | --- |
| `st_tout_open` | `0x8000de60` | 124 B | `ldrb r4, [r5, #46]` / `strb r3, [r5, #46]` with `mov r3, #3` |
| `st_tout_restore` | `0x8000e250` | 76 B | `strb r4, [r3, #46]` / `ldrb r1, [r3, #46]` |

`0x2E` is one byte below `SOFTWARE_RESET 0x2F` (rung 4 writes it) and one above `CLOCK_CONTROL 0x2C`
(rung 6 writes it) — **two registers this ladder has written** — which is why the clause asserts the
offset and the width and not merely the address.

**The window is the CMD2 interval, and CMD3 is deliberately outside it**, read out of the linked
image:

```
0x8000ed20  bl <st_tout_open>          the open
0x8000ed70  bl <st_tout_restore>       the close on the gate's REFUSAL path
0x8000edb8  bl <st_all_send_cid>       CMD2
0x8000edc8  bl <st_tout_restore>       the close on the CMD2 path
0x8000ede8  bl <st_cmd3_noidx>         CMD3
```

CMD3's 48-bit response fits inside 665.2 µs, so **CMD3 runs at the reset bound on this arm** and is
the independent test of 795 §6's protocol claim — a card enters IDENT only by accepting CMD2, and CMD3
is only accepted in IDENT. A window covering CMD3 would answer that question by giving CMD3 the same
help CMD2 is getting.

**The falsification is named and it is cheap: if CMD2 is made to complete and CMD3 *still* times out,
795 §6 is wrong.**

## 4. Six keys, and the one failure the rest of the log cannot show

| key | expected | what it is |
| --- | --- | --- |
| `_tout_calls` | `1` | the open ran |
| `_tout_was` | **`0`** | the byte the register held — **the reading that says no rung and no earlier consumer ever set it** |
| `_tout_wrote` | `3` | the constant |
| `_tout_held` | `3` | the readback |
| `_tout_wrote_back` | `0` | the byte put back |
| `_tout_readback` | `0` | the readback after the restore |

**`_tout_was = _tout_held = 0` is an arm whose store never landed, and every other cell in the log
would read exactly as rung 31's did.** It is the one failure a capture cannot otherwise show, which is
why §5 asserts the store out of the image rather than trusting the source.

## 5. The clause, and the duplicated tail it found

`src/entry/build_entry.sh` gained `xnu_entry_796`, which asserts against the **linked image**:

1. both symbols exist and are referenced from `st_cmd_path` — the open **exactly once**;
2. each body's device set is exactly `f982492e:ldrb f982492e:strb` — `TIMEOUT_CONTROL 0x2E` and
   nothing else, at byte width;
3. the byte's **value**, read out of the `strb` instruction's own source register and the `mov` above
   it, and compared against the constant the record names;
4. the **positions**: every open above CMD2, the **last** close below CMD2, every close above CMD3.

**The first draft demanded exactly one close and refused the build, and the refusal was right about
the source and wrong about the image.** `st_cmd_path`'s source calls `st_tout_restore` once, on the
line after the `if/else` that holds CMD2; **GCC duplicated that tail into both arms of the gate**, so
the linked image carries two closes — one on the path where the gate refused, one on the path where
CMD2 ran — and each path executes exactly one of them. The clause now checks the **count as a shape**
(1 open exactly; 1 to 3 closes) and the **positions over every line**, and the reason is written into
the clause itself rather than the assertion being silently relaxed. It is the same distinction 787
drew when it asserted the window's *interval* and not its statement.

**The value is read out of the instruction and not off a call, and the build is why**: `st_write8` is
a small static helper that GCC **inlines**, so a search for `bl <st_write8>` found no line at all in a
body whose device set `classify_body` had already resolved as `f982492e:strb`. That first draft
refused a body doing exactly what the arm says, because it looked for a **call** as the evidence of a
**store**.

## 6. Containment, measured on all eleven members

The **value-30** build from this same source — every line this step added that reaches the compiler at
value 30 is unchanged, and the two edits that are not guarded are preprocessor-only — was compared
member by member against the **spent rung-31 park** `out/stage90/frozen/armed-storage-f9613649/`:

| member | value-30 build vs the spent rung-31 park |
| --- | --- |
| `stage90-qcdt.img` | **SAME** |
| `stage90.bin` | **SAME** |
| `stage90-build-config.txt` | **SAME** |
| `stage90.elf` | **SAME** |
| `stage90.img` | **SAME** |
| `stage90_fixture.macho` | **SAME** |
| `SHA256SUMS.txt` | **SAME** |
| `xnu_arm_entry.bin` | **SAME** |
| `xnu_arm_entry.elf` | **SAME** |
| `xnu_arm_entry-config.txt` | **SAME** |
| `xnu_arm_entry-sources.txt` | **DIFF — and exactly two lines of the file's 28** |

**Ten of eleven byte-identical**, and the eleventh is the entry sources recorded **by content**, which
must move when the tool is edited: the two lines are the two files this step touched —
`src/entry/build_entry.sh` `07bce057 → 6f9d3c4b` and `src/entry/entry_storage.c` `cdf1056d → ea1d06c7`
— and the artifact's own hash line is **identical** there, because the value-30 entry bin reproduced
`f9613649a895…` byte for byte. The file set is identical, 23 files in both, so nothing was added or
removed under `src/entry/`.

**There is no artifact on which the two arms agree by accident.**

## 7. The arm's own delta, measured

| | value 30 | value 31 | |
| --- | --- | --- | --- |
| entry `.text` | 5,335,232 | 5,335,424 | **+192** |
| `xnu_arm_entry.elf` | 6,732,404 | 6,732,464 | **+60** |
| `xnu_arm_entry.bin` | 5,552,764 | 5,552,764 | **size UNCHANGED** |
| entry bin differing bytes | — | **593,023** | the whole tail of `.text` **shifted** by the insertion |
| `stage90-build-config.txt` | — | **byte-identical** to rungs 23–31 | no payload switch moved, `STAGE90_XNU_ENTRY` is 1 |

**770's rule — an unchanged size is not the claim that nothing moved — applies in both directions**:
the entry bin is the same *size* and differs in 593,023 bytes, which is an insertion and not 593,023
edits.

The eleven members of the new park, each verified in place against `records/revert-set.txt`:

| member | bytes | sha256 |
| --- | --- | --- |
| `stage90-qcdt.img` | 8,572,928 | `bda29ed1…` |
| `stage90.bin` | 6,048,260 | `c56b3116…` |
| `stage90-build-config.txt` | 682 | `6c2b6038…` |
| `stage90.elf` | 6,110,332 | `1048b4b5…` |
| `stage90.img` | 6,051,840 | `b35120d9…` |
| `stage90_fixture.macho` | 1,744 | `52bc9c35…` |
| `SHA256SUMS.txt` | 560 | `88e38972…` |
| `xnu_arm_entry.bin` | 5,552,764 | `c1f89600…` |
| `xnu_arm_entry.elf` | 6,732,464 | `ac2afdda…` |
| `xnu_arm_entry-config.txt` | 900 | `11f93b5a…` |
| `xnu_arm_entry-sources.txt` | 2,335 | `6b973e74…` |

`tools/verify_revert_set.sh out/stage90/frozen/armed-storage-c1f89600 --set=armed-storage-c1f89600`
→ **11 ok / 0 failed**, 6 manifest-member checks agree, and every one of the eleven is `cmp`-identical
between the live `out/stage90/` and the park.

## 8. The answer space, and the prediction is a number

| row | reading |
| --- | --- |
| `_cid_complete = 1` **and** any of `_cid_resp0` / `_cid_raw2` / `_cid_raw3` / `_cid_resp1..3` off its nine-arm value | **THE CID IS TAKEN** — 793 §1's row 1a. The nine-arm pattern is `_cid_raw0..3` = `0x0040ff80 / 0x80000000 / 0 / 0` and `_cid_resp0..3` = `0x40ff8080 / 0 / 0 / 0`, and a 128-bit CID cannot have three of four words zero. |
| `_cid_complete = 1` with that pattern **unchanged** | 793 §5's fifth row, now on an arm where the bound is not the suspect. |
| **`ERR \| TIMEOUT` again, at a LONGER tick count** | **refutes 795 §5's arithmetic, and says by how much** — the one row that is a measurement of a number rather than of a state. |
| `ERR \| CRC` returning | refutes 795 §2 and puts the immediate CRC back on the block, independent of the bound. |

**The control is in the same log**: `_cmd0_ticks` (4,901–4,909 across seven arms, 0.16 per cent) and
CMD1's own completion at poll 1,236 bracket the new number from both ends, so a completion in tens of
ticks, a timeout at a raised bound and a CRC at poll 1 are distinguishable **without a clock
frequency**.

## 9. What this does not do

- **It spends no press and authorizes none.** The arm is **armed and not pressed**, its park is
  intact, and the press is the operator's.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made,
  so 「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld**.
- **It does not move the ladder one command closer to a mount even if the CID is taken.** 794
  measured the image's whole command vocabulary is four opcodes (CMD0–CMD3, no CMD9 define at all),
  `st_cmd_path()` called once, and **no data path**. The distance is **two frontiers**: this bit, and
  the breadth gap — the bodies past CMD3 and a data path behind them.
- **It does not touch the PMIC, the rails, the clocks, the DLL, the pad register, or any address
  outside `TIMEOUT_CONTROL 0x2E`.** 784's rail reading stands: the eMMC's two rails are RPM resources
  with no `reg`.
- **It does not explain the 171.5 kHz**. 795 §10's residual — the effective bit rate the ladder's own
  numbers imply against rung 6's configured 400 kHz — is untouched, and this arm's arithmetic carries
  the assumption forward rather than resolving it.
- **It does not supersede rungs 16 through 31.** Their 665.2 µs readings stand; this is the first arm
  that does not run under that bound.

## 10. Owed, and named rather than left to be inferred

- **The effective bit rate, 171.5 kHz against rung 6's configured 400 kHz** (795 §10, 796 §9) — one
  reading of a clock or a counter, and the assumption this arm's arithmetic rests on.
- **A `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair around CMD1's own `RESPONSE` registers** (790 §5) —
  the one measurement that would turn `0x40ff8080` from a shared word into a reading.
- **`c3.inhibit_timeout` is not published for the `nidx` family** (790 §3c).
- **The `*_status_post` class** (791 §5) — four cells whose zero is an artifact of the enable they
  were read under; the repair is a read of `INT_STATUS` *before* each restore.
- **The `0x40ff8080` "the card ANSWERED" comment at CMD1's head** (787 §6) — unsupported, COST-owed to
  a build.
- Unchanged from 787–795: the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303`
  (COST); a check that counts `ST_LIVE` sites per key (789 §4); the `_Static_assert` message's bit map
  at `entry_storage.c:2360` (789 §5); `_cid_ps_after`'s second producer (789 §2, COST); the
  set-comparison pad repair (779 §7); the `rung_para` correction for values 12..23; the seam-address
  class; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell
  (782 §6); 784's `rail_name` cell; and 783's window-scope check — landed at rung 30's window and at
  this one, and **not** landed at the ladder's other windows.
