# 787: the third enable window — the interval CMD1 has been running in, with an enable that could not see it, since rung 14

**A BUILD, PARK AND RECORD. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO
FIRER.** `out/` moved: the arm in it was rung 29's, and it is **spent** and parked. One press spent by
this step: **none**. **NO PRESS IS AUTHORIZED** — the press path is the two commands readiness prints,
run by hand once each, and that is the operator's.

## 1. The reading this step acts on, and it came out of rung 29's own capture

786 read rung 29's press and found, **by the capture's own line order rather than by reading the
source**, that 785's claim *"`st_cmd_path`'s window carries CMD0 and CMD1"* is false: the rung-14
restore's log line — `_ena_wrote_back = 0x00000000` — stands at capture line 8789, before
`_cmd1_status_any` at 8832. The window therefore closes **before** the between-commands gate, and
CMD1 has run, on every arm from rung 14 on, under rung 14's `int_enable`, which is **zero**:

| key | rung 29's value | what it is here |
| --- | --- | --- |
| `_ena_wrote_back` | `0x00000000` | the restore stores zero |
| `_ena_readback` | `0x00008000` | and reads back only bit 15, which no write clears |
| `_cmd1_status_any` | `0x00000000` | over the whole window |
| `_cmd1_polls` | `0x004da000` = **5,087,232** | the poll's own count |
| `_cmd1_timeout` | `0x00000001` | the arm's own 1.2 s bound |
| `_cmd1_complete` | `0x00000000` | the completion bit, not enabled, never seen |
| `_cmd1_cmdlow_seen` | `0x0000028a` = **650** of the first 1024 samples | the CMD line **did** move |

**CMD1 is `SEND_OP_COND`** — the command an eMMC must answer for this ladder to move at all, and the
question 764 §5 left open. A poll that cannot see the bit it polls for reports exactly its own bound.
So the row read since rung 16 as *the block raised nothing at CMD1* is a row **about this ladder's own
mask**, and CMD1 is the last command window in the ladder for which that is still true.

## 2. The act: two more stores to the same `INT_ENABLE 0x34`, in an interval the ladder has been running CMD1 in all along

| | |
| --- | --- |
| register | `INT_ENABLE 0x34` — the same one rungs 14, 16 and 22 write |
| value | `ST_SDHCI_INT_ENABLE_CMD` = `0x000F0001` — rung 23's five bits, **not** the vendor's eleven |
| stores added | **two** (one open, one close), both to `0x34` |
| `SIGNAL_ENABLE 0x38` | still **read and never written** |
| new address / width / megabyte / device / command | **none** |

The two ends are bodies of their own — `st_cmd1_enable_open` (`src/entry/entry_storage.c:2979`,
called once from `st_cmd_path` at `:4533`) and `st_cmd1_enable_restore` (`:3008`, called once at
`:4589`) — which is what keeps `st_cmd_path`'s **own** device surface at rung 14's and keeps every
rung-14 clause in `build_entry.sh` true at the digit rather than relaxed. **The open returns the word
it stores and the close puts that same word back**, so `_c1_ena_wrote == _c1_ena_wrote_back` is a
**reading** and not two evaluations of one expression.

**Each body's device surface is `INT_ENABLE 0x34` and nothing else**: one store and one readback, and
**no read of `INT_STATUS 0x30` at either end** — which is rung 14's own restore's shape. That is why
this arm adds **no new status cell at all**: what it changes is what the CMD1 cells the ladder
**already carries** can mean — `_cmd1_status_any`, `_cmd1_err`, `_cmd1_complete`, `_cmd1_timeout`, and
`_cmd1_stale` / `_cmd1_status_after`, which are `st_send_command`'s own entrance and exit reads of
`0x30` and are published for every command. **That is rung 23's shape one command over.** The four
keys the two new bodies publish are the window's own state: `_c1_ena_wrote`, `_c1_ena_held`,
`_c1_ena_wrote_back`, `_c1_ena_readback`.

## 3. The answer space, four rows, and the first is the one that moves the ladder

| row | reading |
| --- | --- |
| **bit 0 (`SDHCI_INT_RESPONSE`) visible** | **CMD1 completed and the block latched it.** The completion latch is a hardware event the enable only gates the *view* of — so on this row the 1.2 s "timeout" every arm from rung 14 on has reported was **this ladder's own mask**, and CMD1 may have been completing all along. This row changes what the next arm is: the ladder stops asking what is wrong with the card and starts reading CMD1's own answer. |
| `ERR \| TIMEOUT` (`0x00018000`) | the block armed its **own** response timeout and the card did not answer within it — the reading CMD3 already carries (`_nidx_status_any = 0x00018000` beside `_nidx_status_pre = 0x00028000`) — which puts the subject on the card and on the power/clock path 784 gave a shape to. |
| CRC, END_BIT or INDEX | the card answered and the answer was malformed. |
| **nothing at all**, all five bits enabled | the strong negative: the block set no bit of `INT_STATUS` during a command it was given. |

## 4. And the `ERR | TIMEOUT` row is *not* already excluded — this arm's second question

786 §4 read the CMD2 and CMD3 rows (`ERR | CRC`, `ERR | TIMEOUT`) as showing that bit 15 **tracks**
the error bits, and concluded that CMD1's row *would not have moved at all*. But `_ena_readback =
0x00008000` in rung 29's **own** capture says bit 15 **stands** during CMD1 — no write clears it —
and **both** of 786's rows come from arms on which the specific error bit was **enabled**. Two
readings are open, and this window separates them:

- **(a)** bit 15 rises with any error whatever its own bit's enable — then no error occurred at CMD1
  and only bit 0 (the completion) can be new;
- **(b)** bit 15 rises only when the specific error bit is enabled — then **CMD1 may have timed out
  invisibly on every arm from rung 14 on**, and 786 §4's sentence is too strong.

**So a second reading is bought by the same press, and 786's narrowing is not the final word about
this window.** If the press comes back with an error bit, 786 §4 is refuted and must be superseded in
`records/revert-set.txt` and `tools/verify_press_ready.sh`.

## 5. A negative is read only where the line moved

`_cmd1_cmdlow_seen = 0x0000028a` = **650** of the first 1024 samples on rung 29's own arm, so the CMD
line **did** move during CMD1 there, and a zero status count beside it is a statement about the block
rather than about a bus nobody drove. `_cmd1_polls` and `_cmd1_ticks` are read **beside** the count,
because a sampler slower than one bit period could miss a transmission and report a zero (771's rule,
one window over). Nothing in this arm changes those cells.

## 6. And no cell of this arm is read as an answer from the card

`0x40ff8080` stands in the `RESPONSE` registers **after a command that timed out** — rung 29's
capture carries `_nidx_resp_pre = 0x00000000` then `_nidx_resp_post = 0x40ff8080` beside
`_nidx_status_any = 0x00018000` — the image never **writes** those registers, and rung 19's capture
records the same word **before** CMD3 was put on the bus. So a non-zero `_cmd1_resp` is **not**
evidence that the card answered. The comment at the head of CMD1 says of exactly this value *"the
card ANSWERED"*; that inference is unsupported and is **COST-owed** to this arm's successor, because
correcting a comment forces a build.

## 7. The clause is the reading, not a sentence in this file

`src/entry/build_entry.sh` gained `xnu_entry_787`, which asserts **against the linked image**, for
each of the two new bodies:

1. the symbol exists and is referenced **exactly once** from `st_cmd_path` — zero callers would drop
   the static and leave `INT_ENABLE` carrying the five bits with nothing writing it back;
2. its device set is exactly `[f9824934:ldr f9824934:str]` — a store and its readback, and **no
   `SIGNAL_ENABLE`, no `INT_STATUS`, no other address**;
3. its image-side memory accesses are **empty**;
4. **the open's call falls before the second `st_send_command` and the close's after it** — asserted
   against `stb_path_cmd_ln[1]`, the linked image's own disassembly line.

The fourth is 785's defect turned into a refusal: 783 and 785 both believed `st_cmd_path`'s window
covered CMD1, and 786 refuted it out of the capture's line order. **A window is an interval, and the
interval this rung's window must cover is asserted as a position in the linked image rather than
stated in a comment.** It fired on this build:

```
xnu_entry_787: ... the interval between its ONE call to st_cmd1_enable_open (disassembly line 313,
a body whose device set is [f9824934:ldr f9824934:str ]) and its ONE call to st_cmd1_enable_restore
(line 459, [f9824934:ldr f9824934:str ]) - so the enable stands across CMD1 on line 323 ...
```

## 8. It contains rung 29, measured on **all eleven** park members

Every new line sits inside `#if STAGE90_XNU_STORAGE_PROBE >= 29`. Measured rather than argued — this
step rebuilt both values from the same source and compared **every member**, not two artifacts:

| member | value-28 build | the parked rung-29 arm (`armed-storage-5fca6210`) |
| --- | --- | --- |
| `stage90-qcdt.img` | `a9fa3703…` | `a9fa3703…` ✓ |
| `stage90.bin` | `f4d7c949…` | `f4d7c949…` ✓ |
| `stage90-build-config.txt` | `6c2b6038…` | `6c2b6038…` ✓ |
| `stage90.elf` | `0cdf035d…` | `0cdf035d…` ✓ |
| `stage90.img` | `26caefae…` | `26caefae…` ✓ |
| `stage90_fixture.macho` | `52bc9c35…` | `52bc9c35…` ✓ |
| `xnu_arm_entry.bin` | `5fca6210…` | `5fca6210…` ✓ |
| `SHA256SUMS.txt` | `444b3fd0…` | `444b3fd0…` ✓ |

**Eleven of eleven, differing in none.** The delta this arm carries is the `>= 29` lines and nothing
else, and there is no artifact on which the two arms agree by accident.

The arm itself, `armed-storage-dfc4ae78` (`STAGE90_XNU_STORAGE_PROBE=29`, switch **value 29 = ordinal
rung 30**):

| member | value-29 build | note |
| --- | --- | --- |
| `stage90-qcdt.img` | `8c86586a…` (8,572,928 B) | **this is what `fastboot boot` sends** |
| `stage90.bin` | `f1c5e54c…` | raw payload |
| `stage90.elf` | `96ea7684…` | the gate reads symbols out of this |
| `stage90.img` | `c71e1f4c…` | a manifest member, not of the gate's own list |
| `xnu_arm_entry.bin` | `dfc4ae78…` (5,552,764 B) | **the set name is this file's prefix** |
| `xnu_arm_entry.elf` | `2aae6a53…` (6,732,404 B) | **+76 B**, and it carries `st_cmd1_enable_open` at `0x8000ddc4` and `st_cmd1_enable_restore` at `0x8000de18` |
| `xnu_arm_entry-config.txt` | `25accb89…` | **exactly two lines** differ from rung 29's: the probe number and the artifact's own sha256 |
| `xnu_arm_entry-sources.txt` | `f1162f01…` | **exactly three lines**: that sha256, `build_entry.sh` `eb9b6c7f→5e9bca03`, `entry_storage.c` `7c8800c3→6afc5d29` |
| `stage90-build-config.txt` | `6c2b6038…` | **byte-identical to rungs 23–29**, so no payload switch moved and `STAGE90_XNU_ENTRY` is 1 |
| `stage90_fixture.macho` | `52bc9c35…` | unchanged across every arm since 574 |
| `SHA256SUMS.txt` | `58fad3cf…` | absolute-pathed; never checked with `sha256sum -c` |

`tools/verify_revert_set.sh out/stage90/frozen/armed-storage-dfc4ae78
--set=armed-storage-dfc4ae78` → **11 ok / 0 failed**, and every one of the eleven is `cmp`-identical
between the live `out/stage90/` and the park.

**And the census moved with the arm, read rather than asserted**:
`tools/report_int_enable_windows.py` now reports the five-bit word built at **four** sites and
reaching **four** stores — `st_all_send_cid`, `st_cmd1_enable_open`, `st_cmd3_noidx`, `st_cmd_path` —
where before this step it read three. Its assignment map is keyed by `(function, name)`, which is the
repair 785 made in the same build.

## 9. A build defect this step hit, recorded because it is cheap to hit again

The step's first entry-build chain produced **zero-byte switch files** and two failed builds whose
error (`'g_live_state' undeclared` in `entry_stubs.c`) named the symptom and not the cause: the
script derived `sw_entry_28.sh` from `sw_entry_28.sh` with `sed … > …`, and a shell truncates the
redirection target **before** `sed` reads it. The chain now refuses an empty or short switch file
before invoking the build, and asserts the file carries the value it is named for. The build this
document records is the one that followed that repair.

**And a second, caught before it cost a build**: the first draft of the two new bodies read
`INT_STATUS 0x30` once at each end (`_c1_status_pre`, `_c1_status_post`), which would have failed the
clause above — `classify_body` counts `0x30` as a device access, and the expected set is `0x34` and
nothing else. The reads were dropped and **the clause was not relaxed**: `_cmd1_stale` and
`_cmd1_status_after` already carry those two moments, so the arm loses no reading and gains a
one-register device surface.

## 10. What this does not do, and what it does not say

- **It does not say CMD1 failed, and it does not say it succeeded.** No press has been spent on this
  arm; the four rows above are the answer space and none is filled.
- **It does not reach the goal.** No filesystem is reached and no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld**.
- **It does not touch the PMIC, the rails, the clocks, the DLL, the pad register, or any address
  outside `INT_ENABLE 0x34`.** 784's rail reading stands unchanged: the eMMC's two rails are RPM
  resources with no `reg`, and no rung can power them with a store.
- **No press is spent and none is authorized.**

## 11. Owed, and named rather than left to be inferred

- **The `0x40ff8080` "the card ANSWERED" comment at the head of CMD1** — unsupported (§6), COST-owed
  to this arm's successor.
- **786 §4's "CMD1's row would not have moved at all"** is narrowed by §4 above and must be
  superseded if the press returns an error bit.
- Unchanged from 784–786: the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303`
  (COST-owed, to be carried by a build); the set-comparison pad repair (779 §7, pre-registered, not
  built); the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6);
  784's `rail_name` cell; and 783's **window-scope check** — landed here as a clause at **this**
  window, and **not** landed at the ladder's other windows.
