# 800: the rung-33 arm — the driver's own CMD1 loop, ported at last

**A BUILD, PARK AND RECORD. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO
FIRER.** `out/` moved: the arm in it is rung **33** and it is **ARMED AND NOT PRESSED**. One press
spent by this step: **none**. **NO PRESS IS SPENT OR AUTHORIZED HERE** — the press path is the two
commands readiness prints, run by hand once each, and that is the operator's.

| | |
| --- | --- |
| arm | `armed-storage-ce2f589c`, `STAGE90_XNU_STORAGE_PROBE=32` (switch **value 32 = ordinal rung 33**) |
| boot image | `out/stage90/stage90-qcdt.img` sha256 `2c9c2492…`, 8,572,928 B |
| park | `out/stage90/frozen/armed-storage-ce2f589c/`, **eleven members**, verified **11 ok / 0 failed** |
| the act | **the driver's own control flow**: derive an OCR from CMD1's response, then send CMD1 up to 100 times, 10 ms apart, exiting when bit 31 **SETS** |
| new keys | **eight**, `_opcond_*` — of which `_opcond_sends` and `_opcond_busy_seen` are the two the arm exists for |

## 1. The reading this step acts on, and it is 798 §5's

798 read rung 32's press and found the frontier is not the block's timeout register and not the card.
It is the **command sequence**, and the finding is a comparison of two files:

| | |
| --- | --- |
| `mmc_ops.c:141-166` | `for (i = 100; i; i--)` … `if (cmd.resp[0] & MMC_CARD_BUSY) break;` (`:157`) — **proceeds when bit 31 SETS**, up to 100 sends, `mmc_delay(10)` between them |
| `entry_storage.c:4702` | `st_send_command(ST_CMD_OP_SEND_OP_COND, 0u, ST_MMC_RSP_PRESENT, &c1);` — **one send, no loop** |
| `entry_storage.c:4785` | `if (c1.sent != 0u && (c1.resp & ST_MMC_CARD_BUSY) == 0u)` — **proceeds when bit 31 is CLEAR** |
| the measurement | `_cmd1_resp = 0x40ff8080` has bit 31 clear, so **the driver's loop would have retried 99 more times**; this ladder went on to CMD2 |

**So CMD2 has been put on the bus, since rung 16, for a card that has never reported power-up
complete** — which is what the 665.2 µs is: the block's fixed response window expiring over a silent
card.

## 2. And the two steps between the driver's two calls do not exist in the image

799 established this by grep rather than by impression. `mmc.c:1923` is `mmc_send_op_cond(host, 0,
&ocr)` — **argument 0, and by `mmc_ops.c:148-150`'s `if (ocr == 0) break;` a SINGLE PASS by the
driver's own design**. `mmc.c:1951` is `host->ocr = mmc_select_voltage(host, ocr)` — the card's OCR
window intersected with the host's. `mmc.c:1359` is `mmc_send_op_cond(host, ocr | (1 << 30), &rocr)` —
**the looped call**, with an argument derived from the first response and bit 30 set.

Anchored and case-sensitive over `src/entry/`: `select_voltage` occurs once and **in a comment** (the
note at `entry_storage.c:4724`) with no define, no body and no call; the CMD1 argument is the literal
`0u`; `1u << 30` occurs once as `ST_HC_DLL_RST` (`:258`, a different register). **Bit 30 has never
been set in any command word this image sends.**

## 3. The act, and it is two bodies of their own

The shape 787 and 796 used, so that `st_cmd_path`'s own device surface does not move by one offset:

| body | address | size | device surface |
| --- | --- | --- | --- |
| `st_op_cond_arg` | `0x8000de18` | 96 B | **EMPTY** |
| `st_op_cond_loop` | `0x8000de78` | 320 B | **EMPTY** |

both `noinline, noclone`, each called from `st_cmd_path` and nowhere else. **Their device sets are
measured out of the linked image and not asserted from the source**: the derived argument is a function
of the word the caller passes and of nothing the block holds, and the loop's only device act is the
command it sends, which belongs to `st_send_command` — whose own body and own clause are unchanged, and
whose `st_cmd_path` call count is still asserted to be exactly two (the loop's `bl` is inside the
loop's body, not in `st_cmd_path`).

**No store to any register is added by this rung**, and no address is added either: the only width, the
only register class and the only command opcode are the ones rung 16 already had.

## 4. Four constants, and every one of them is asserted out of the image

| constant | value | source |
| --- | --- | --- |
| `ST_OP_COND_OCR_CLR` | `0x0000007Fu` | `mmc.c:1943-1948`'s `ocr &= ~0x7F`, emitted as `bic #127` |
| `ST_OP_COND_BIT30` | `0x40000000u` | `mmc.c:1359`'s `(1 << 30)`, emitted as `bic`/`orr` |
| `ST_OP_COND_SEND_MAX` | `100u` | `mmc_ops.c:145`'s `for (i = 100; i; i--)` |
| `ST_OP_COND_DELAY_TICKS` | `192000u` | `mmc_ops.c:164`'s `mmc_delay(10)` = 10 ms at 19.2 MHz |

The first three are asserted as immediates in the linked bodies by `xnu_entry_799`; the fourth is
carried as a `mov`/`movt` pair (`#60928`, `movt #2` → `0x2EE00` = 192,000) and is not asserted, which
is named here rather than left as an impression.

## 5. Eight new keys, and the answer space is four rows

| key | what it is |
| --- | --- |
| `_opcond_calls` | the argument body ran |
| `_opcond_from` | the word it derives from — `c1.resp` |
| `_opcond_arg` | the word it derived — **the arm's only number** |
| `_opcond_loop_calls` | the loop ran |
| `_opcond_sends` | **how many sends it took, 1..100** — the cell no arm has carried |
| `_opcond_busy_seen` | **whether bit 31 ever set** — 798 §5's own test, run more than once for the first time |
| `_opcond_last_resp` | the last response word |
| `_opcond_ticks` | the loop's own cost in ticks |

| row | reading |
| --- | --- |
| `_opcond_busy_seen = 1` on send `k ≤ 100` | **the card finishes power-up, and this ladder has been sending CMD2 too early since rung 16** — and `k` is a number no arm has carried |
| never sets over the full 100 | the card is not powering up on this path at all — the subject is **power and clock** (784's rails are RPM resources with no `reg`) and **not** the command sequence |
| `_opcond_last_resp` **differs** between sends | the block is reading something that moves with each CMD1, which separates a real OCR from a static pattern **without** needing to know which it is |
| byte-identical every send, and equal to `_cmd1_resp` | `0x40ff8080` is a static pattern and 790 §5's discount extends to every arm |

## 5a. The prediction is a number, and it is sharper than the rung that made it possible

`_cmd1_resp = 0x40ff8080` already has bit 30 **set** and its low seven bits **zero**, so

```
(0x40ff8080 & ~0x7F) | (1 << 30)  ==  0x40ff8080
```

**The derivation is a no-op on this card's own word, and `_opcond_arg` should equal `_opcond_from`
exactly.** A value that differs says the derivation moved something the response did not already
satisfy — which is the only way this rung can report on its own first body at all. 799 §4 named
`_opcond_sends` as "the only cell the ladder does not already carry"; this is the second, and it is a
cell whose *expected value* is known before the press rather than after it.

## 5b. The cost, named rather than discovered

The worst case is 100 sends plus 99 ten-millisecond delays. At the measured 665.2 µs per timed-out
command that is **about 1.06 s added to the storage probe**, spent inside `entry_storage_probe` at the
OS's idle-exit, where the run's own deadline is 6,000 ms (`_post_elapsed`). A run that spans the whole
loop is a run whose window shrank by that much, and `_opcond_ticks` is the cell that measures what it
actually cost. **This is the first rung whose cost is a second of bus time rather than a bounded
number of reads**, and it is the reason the loop's own length is published.

## 6. The clause found two defects in its own first drafts

`src/entry/build_entry.sh` gained `xnu_entry_799`, guarded `if [[ $STORAGE_PROBE -ge 32 ]]` — 65 lines
on the first insert, and the final form asserts:

1. both symbols exist and are called **exactly once each** from `st_cmd_path`;
2. each body's device set is **empty**;
3. **the four constants**, as immediates in the bodies (`#127`, `#1073741824` in the argument body;
   `#100`, `#101` in the loop);
4. the loop carries **exactly one** `bl <st_send_command>`;
5. **the exit test's direction** — exactly one sign-conditioned branch (`blt`/`bmi`) leaving the loop,
   which is what `(resp & 0x80000000) != 0` compiles to;
6. the **positions**: arg before loop, both after CMD1, both before CMD2.

**(a) `objdump -d` annotates every immediate with its own comment field.** The instruction that clears
the low seven bits reads `bic  r3, r3, #127  ; 0x7f`. The first draft matched `#127` **anchored at end
of line**, which objdump never emits, and the clause **refused a build that did exactly what the arm
says** — loud, because the pattern sits in a `||`-refusal, and it would have refused every future
value-32 build too. The repair **strips objdump's comment field rather than loosening the anchor**,
because a loosened anchor is the check that also matches `#1274`.

**(b) The probe is the LAST of `st_cmd_path`'s two sends, not the first.** The first draft took the
first `bl <st_send_command>` — which is CMD0's line — so the assertion was true and the sentence
printed beside it would have named the wrong command. **That is the m-class this project keeps
catching: a true assertion wearing a false label.** The last is taken now, and the `== 2` assertion
further down is what makes "the last" unambiguous: the echo now reads *"both after CMD1 … on line
323"* rather than *"both after the probe on line 125"*.

**(c) The exit test's direction is asserted because it is 798 §5's whole subject.** `blt`/`bmi` are the
forms GCC emits for a test of the sign bit; a `bne` or a `bcc` there would be a test of some other bit,
which is exactly the inversion 798 found in the image's own CMD2 gate. **A differently-lowered test
refuses the build on purpose** — the form is the compiler's, so a fourth spelling stops for a human to
read it rather than passing on an assertion that no longer means anything.

## 7. Containment, measured on all eleven members of the rung below

The **value-31** build from this same source was compared member by member against the **spent rung-32
park** `out/stage90/frozen/armed-storage-c1f89600/`:

| member | value-31 build vs the spent rung-32 park |
| --- | --- |
| `stage90-qcdt.img` | **SAME** (`bda29ed1…`) |
| `stage90.bin`, `stage90-build-config.txt`, `stage90.elf`, `stage90.img` | **SAME** |
| `stage90_fixture.macho`, `SHA256SUMS.txt` | **SAME** |
| `xnu_arm_entry.bin` | **SAME** (`c1f89600…`) |
| `xnu_arm_entry.elf` | **SAME** |
| `xnu_arm_entry-config.txt` | **SAME** |
| `xnu_arm_entry-sources.txt` | **DIFF — and exactly two lines of the file's 28** |

**Ten of eleven byte-identical**, and the eleventh records source files **by content**: the two lines
are `build_entry.sh` and `entry_storage.c`, the two files this step touched, with the artifact's own
hash line identical and the file set identical at 23 files. **There is no artifact on which the two
arms agree by accident.**

## 8. The arm's own delta, measured

| | value 31 (spent rung 32) | value 32 (this arm) | |
| --- | --- | --- | --- |
| entry `.text` (`size -A`) | 5,335,424 | 5,335,680 | **+256** |
| `xnu_arm_entry.elf` | 6,732,464 | 6,732,528 | **+64** |
| `xnu_arm_entry.bin` | 5,552,764 | 5,552,764 | **size UNCHANGED** |
| entry bin differing bytes | — | **603,185** | an insertion, not 603,185 edits |
| `stage90-build-config.txt` | — | **byte-identical** (`6c2b6038…`) to rungs 23–32 | no payload switch moved |

**The `.text` grew 256 and the ELF only 64, and that is not a contradiction**: 192 bytes of the growth
were absorbed by page alignment, which is 796 §7's own rule that an unchanged size is not the claim
that nothing moved — and it applies here in the other direction.

The eleven members of the new park, each verified in place against `records/revert-set.txt`:

| member | bytes | sha256 |
| --- | --- | --- |
| `stage90-qcdt.img` | 8,572,928 | `2c9c2492…` |
| `stage90.bin` | 6,048,260 | `9d28a319…` |
| `stage90-build-config.txt` | 682 | `6c2b6038…` |
| `stage90.elf` | 6,110,332 | `8555da4a…` |
| `stage90.img` | 6,051,840 | `9fb01326…` |
| `stage90_fixture.macho` | 1,744 | `52bc9c35…` |
| `SHA256SUMS.txt` | 560 | `e005ea99…` |
| `xnu_arm_entry.bin` | 5,552,764 | `ce2f589c…` |
| `xnu_arm_entry.elf` | 6,732,528 | `7baef0f6…` |
| `xnu_arm_entry-config.txt` | 900 | `e13914e0…` |
| `xnu_arm_entry-sources.txt` | 2,335 | `2eafd2f8…` |

`tools/verify_revert_set.sh out/stage90/frozen/armed-storage-ce2f589c --set=armed-storage-ce2f589c`
→ **11 ok / 0 failed**, 3 manifest-member checks agree, and `tools/resolve_arm_set.sh out/stage90`
resolves the live `out/` to this set.

## 9. What this does not do

- **It spends no press and authorizes none.** The arm is **armed and not pressed**, its park is intact,
  and the press is the operator's.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld**.
- **It does not port `mmc_select_voltage`.** 799 §7 named that as *"the arm's own design question"* and
  the answer this step took is the simpler one: **the card's own OCR passes through the driver's mask
  and bit 30, with no intersection against the host's windows** — because the image has no
  representation of the host's available windows at all, and inventing one is a different arm from
  porting a control flow. On this card the two derivations coincide (§5a), so the choice is not
  load-bearing for the press and **is named rather than hidden**.
- **It does not bridge the breadth gap.** 794 measured the image's whole command vocabulary is CMD0–CMD3
  with no CMD9 define and no data path; this rung adds no opcode.
- **It does not touch the PMIC, the rails, the clocks, the DLL or the pad register.** The arm's device
  surface is `st_send_command`'s, unchanged.
- **It does not repair `entry_storage.c:4785`.** The gate's direction is still inverted against the
  line it cites. **This rung makes that gate's premise measurable and does not move it** — which is the
  cheapest ordering: the press says whether the loop ever sees bit 31 before anything is built on it.
- **It does not decide whether `0x40ff8080` is a real OCR.** Row 4 is that discount's row.

## 10. Owed, and named rather than left to be inferred

- **`entry_storage.c:4785`'s inverted gate** — the driver proceeds when bit 31 SETS. The repair is
  source and belongs to the build the press makes possible, not to this one.
- **`entry_storage.c:3328`'s `>= 30` guard** (798 §4) — it should be `== 30`, or the record must state
  that value 31 is a strict containment of value 30. COST-owed to a build.
- **796 §1/§3/§8's sentences about `0x0209`** (798 §4) — false; COST-owed to a build.
- **The six live-prose corrections of 797 §1** — the 1.200 s attribution, COST-owed to a build.
- **`mmc_select_voltage`'s window** (§9) — deliberately not ported, and the reason is written above.
- **`TIMEOUT_CONTROL`'s real scope** (798 §2) — unmeasured on this ladder.
- **The effective bit rate, 171.5 kHz against rung 6's configured 400 kHz** (795 §10) — untouched.
- **A `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair** around CMD1's own `RESPONSE` registers — owed since
  787 §6, and now the cell this rung's own `_opcond_*` neighbourhood would sit beside.
- Unchanged from 787–799: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST);
  `c3.inhibit_timeout` for the `nidx` family (COST); the `*_status_post` class (791 §5); the four
  `5,088,000`s and the mis-citation at `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE`
  sites per key (789 §4); the `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5);
  `_cid_ps_after`'s second producer (789 §2, COST); the set-comparison pad repair (779 §7); the
  `rung_para` correction for values 12..23; the seam-address class; `run_and_capture.sh`'s
  `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6); 784's `rail_name` cell;
  and 783's window-scope check.
