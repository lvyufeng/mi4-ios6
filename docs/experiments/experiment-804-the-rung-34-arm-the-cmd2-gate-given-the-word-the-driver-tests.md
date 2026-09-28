# 804: the rung-34 arm — the CMD2 gate given the word the driver actually tests

**A BUILD, A PARK AND A RECORD.** No press, no device action, no runner, no firer. `out/` was rebuilt and
the new arm is in it: **`armed-storage-7341f5f6`, `STAGE90_XNU_STORAGE_PROBE=33` — switch value 33 =
ordinal rung 34.** The step edits `entry_storage.c` (one condition and two cells), `build_entry.sh` (the
`xnu_entry_804` clause), `records/revert-set.txt` and the readiness narration.

| | |
| --- | --- |
| arm | `armed-storage-7341f5f6`, `STAGE90_XNU_STORAGE_PROBE=33` (switch **value 33 = ordinal rung 34**) |
| park | `out/stage90/frozen/armed-storage-7341f5f6/` — 11 members, verified **11 ok / 0 failed** |
| readiness | **5 of 5, exit 0** |
| `make check` | exit 0 |
| entry image | `xnu_arm_entry.bin` sha256 `7341f5f6…`, 5,552,764 B |
| boot image | `stage90-qcdt.img` sha256 `b9319777…`, 8,572,928 B |
| press | **none. The arm is ARMED AND NOT PRESSED, and no press is authorized.** |

## 1. What changed, and it is one condition

`entry_storage.c`'s CMD2 gate tested

```c
if (c1.sent != 0u && (c1.resp & ST_MMC_CARD_BUSY) == 0u) {   /* the PROBE's response */
```

and now tests

```c
if (c1.sent != 0u && op_busy != 0u) {                        /* the LOOP's result */
```

`op_busy` is the value `st_op_cond_loop` has returned since rung 33 — and which the rung below **computes
and throws away** with a `(void)op_busy;` cast. The two arms call the same bodies, in the same order, at
the same place in the driver's own sequence. **What changes is which word decides whether the second of
them runs.** The split is `#if >= 33` / `#else` **inside rung 16's own guard**, so a value below 33
compiles the old condition byte for byte.

**This is the rung 800 named as the separate one, in the source.** 799's own comment above `op_busy`
reads: *"Gating CMD2 on the loop's answer is the driver's semantics and it is a SEPARATE rung — one change
per rung is this ladder's own discipline."* 803 is what made it a measurement rather than a tidy-up.

## 2. Why the old condition is wrong rather than backwards

`mmc_send_op_cond` returns after **one** CMD1 by the driver's own design — `mmc_ops.c:148-150`'s
`if (ocr == 0) break;` — so the probe's word is exactly what a card that has not finished power-up
answers: **the OCR with bit 31 clear.** `(c1.resp & MMC_CARD_BUSY) == 0` is therefore **true**, and the
old gate **passes**.

> **It fires on the one reading that means "come back later" and passes on the reading that means "not
> ready yet".**

That is why `_cid_gated` read 0 on every arm from rung 16 through rung 32, and why all nine put CMD2 and
CMD3 on the bus at a card that was not listening. The driver's own condition is `mmc_ops.c:157`'s
`if (cmd.resp[0] & MMC_CARD_BUSY) break;` — **the loop leaves when bit 31 sets** — and that is what
`op_busy` is.

## 3. The press that measured it

803's numbers are the two halves in one log: `_opcond_sends = 2` with `_opcond_busy_seen = 1` and
`_opcond_last_resp = 0xc0ff8080`, against `_cmd1_resp = 0x40ff8080` — **the same OCR differing by exactly
the busy bit** — and then `_cid_complete = 1` with a real SanDisk `SDW16G` CID that nine arms could never
read. **So the two words are not two readings of one thing: they are answers to two different questions,
and the gate was asking the one the driver does not.**

## 4. Two new cells, published on both paths

| key | what it is |
| --- | --- |
| `xnu_live_storage_cid_gate_word` | the word the gate tested — read out of the **loop's** own result struct |
| `xnu_live_storage_cid_gate_busy` | the test's own result, printed beside its source (the shape `_cmd1_resp_busy` already has four lines above) |

On a row where the card became ready the word differs from `_cmd1_resp` by exactly the busy bit; on a row
where the loop never saw the card out of reset the two are **equal** and `_cid_gated` is 1 — **which is
the row the old gate could not produce at all.** `c1.sent != 0` is kept so `_cid_gated_reason` still
separates 1 (CMD1 never reached the bus) from 2 (it did, and the loop never saw the card out of reset);
**reason 2's meaning is what moves.**

## 5. The build clause is the reading, not this document

`build_entry.sh` gained `xnu_entry_804`, which reads three numbers out of the **linked image** in the
window between `st_cmd_path`'s call to `st_op_cond_arg` and its call to `st_all_send_cid`. **Every one of
them was measured against the spent rung-33 park before the clause was written, and every one of them
fails there.**

| | value 32 (`armed-storage-ce2f589c`) | **value 33 (this arm)** |
| --- | --- | --- |
| sign-conditioned branches | **1** — `bge` at `8000eea8` | **0** |
| loads from the **probe's** word | **1** — slot `[sp, #188]` | **0** |
| loads from the **loop's** struct | **0** | **1** — `[sp, #300]` |
| the two new key strings | **0 / 0** | **1 / 1** |

**Both slots are derived and not typed.** The probe's word's slot is the `ldr r0, [sp, #N]` that *feeds*
`st_op_cond_arg` (both images load `[sp, #188]` there), and the loop struct's base is the `add r1, sp, #N`
that materialises `&c1b` before the `st_op_cond_loop` call (both images say 224). A frame that shifts
moves the numbers with it and the check does not go blind.

**The `bge` is the whole point.** `(x & 0x80000000) == 0` is a *sign* test, so the compiler lowers it to a
signed comparison against zero — and the value-32 image carries exactly one inside this window, at
`8000eea8`, **the branch that passes when the card reports it has not finished power-up.** The repaired
condition tests a 0/1 flag and lowers to `cmpne`/`bne`: no sign test anywhere in the window.

**And the `#else` branch is the containment.** The old condition is preserved *verbatim*, not
re-derived — which is what makes the value-32 measurement below a measurement rather than an argument.

## 6. The clause refused in silence twice before it printed a number

Both are 692's class — a failure reported by the wrong producer, or by none — and both are worth the
sentence because **neither printed anything at all.**

- **`(( _off >= gw_loop_off )) && gw_hi=$((gw_hi + 1))` as the last command of a `while` body.** A bare
  arithmetic command returns 1 when the comparison is *false*, and this script runs `set -e` — so the
  first offset **below** the base ended the build with no message. It is an `if` now.
- **`x=$(... | grep ...)` with no `|| true`.** `grep` exits 1 when it matches nothing, and **`grep -c`
  exits 1 when the count is zero — which is this clause's *good* case.** The command substitution's status
  became the assignment's, and `set -e` ended the build. Every pipeline in the clause now carries
  `|| true` and the emptiness is handled below as a refusal with a cause.

## 7. Containment, measured on all eleven members of the rung below

The value-32 build **from this same source** is **ten of eleven members byte-identical** to the spent
rung-33 park `out/stage90/frozen/armed-storage-ce2f589c/`:

| member | value-32 build vs the spent park |
| --- | --- |
| `xnu_arm_entry.bin` | **SAME** — `ce2f589c…`, the entry image reproduced byte for byte |
| `stage90-qcdt.img` | **SAME** — `2c9c2492…` |
| `stage90.bin`, `stage90.elf`, `stage90.img`, `stage90_fixture.macho` | **SAME** |
| `stage90-build-config.txt` | **SAME** — `6c2b6038…` |
| `xnu_arm_entry-config.txt` | **SAME** |
| `SHA256SUMS.txt` | **SAME** |
| `xnu_arm_entry-sources.txt` | **DIFFERS — exactly two of its 28 lines**: `build_entry.sh` and `entry_storage.c`, the two files this step edited |

## 8. The arm's own delta, measured against the spent rung-33 park

| | rung 33 | **rung 34** |
| --- | --- | --- |
| entry `.text` | 5,335,680 | **5,335,744** (+64) |
| `xnu_arm_entry.elf` | 6,732,528 | 6,732,528 — **size unchanged** |
| `xnu_arm_entry.bin` | 5,552,764 | 5,552,764 — **size unchanged, 546,395 bytes differ** |
| `stage90-build-config.txt` | `6c2b6038…` | **byte-identical** — no payload switch moved |
| `stage90-qcdt.img` | 8,572,928 | 8,572,928 |
| `xnu_arm_entry-config.txt` | — | **two lines move**: the artifact's own hash, and `STORAGE_PROBE` 32 → 33 |

**And the arm is reproducible**: the value-33 build was run twice — once before the containment build and
once after it — and **both runs' eleven members are byte-identical to this park**, so the park is the
build of the tree in front of it and not a lucky copy.

## 9. Four rows, and what each would mean

1. **`_cid_gated = 0` with `_cid_gate_busy = 1`, `_cid_gate_word = 0xc0ff8080`, and every rung-33 cell
   reproduced** (`_cid_complete = 1`, the SanDisk CID, `_nidx_resp = 0x00000500`). **The expected row:
   the repaired gate reaches the same place, and the repair is behaviour-preserving on this card** —
   which is what makes it safe to build CMD9 on.
2. **`_cid_gated = 1` with `_cid_gated_reason = 2`** — the loop never saw bit 31 this time, so the repair
   *changed* the outcome and **the card's readiness is not reproducible boot to boot.** That row makes
   the fix load-bearing rather than tidy, and this is the first arm that can tell the two apart.
3. **`_cid_gate_word` equal to `_cmd1_resp`** — the loop's struct is being read at the wrong moment, or
   the two cells are one word wearing two names (the m-class this project keeps catching).
4. **`_cid_gate_busy = 0` beside `_cid_gated = 0`** — **impossible by construction**, since the gate tests
   exactly that flag, and therefore a refutation of this arm's own claim rather than a reading.

**And the control is in the same log**: `_cmd1_resp`, `_cmd1_resp_busy` and every `_opcond_*` cell are
published by the rungs below unchanged, so the two words can be compared in one capture without trusting
this document.

## 10. What this does not do

- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made.
  794 §1's breadth gap (four opcodes, **no CMD9 define at all**, no data path) is the whole distance.
- **It adds no store, no address, no width, no megabyte, no device and no key of any device register.**
  The rung-33 arm's two bodies keep their empty device sets, `st_cmd_path`'s device surface is rung 14's
  unchanged, and `st_send_command` is still called exactly twice from it. `xnu_live_storage_writes` does
  not move.
- **It does not add CMD9.** `SEND_CSD` is the next command in the driver's order and the breadth gap's
  first body, and **this rung is what makes it reachable** — but it is the next rung and not this one.
- **No press is authorized by this step.** The arm is armed and not pressed, and the press is the
  operator's.

## 11. Owed, and named rather than left to be inferred

- **CMD9's `MMC_RSP_R2`** — the next command, and the breadth-gap body this rung unblocks.
- **A cell for the rung-33 press's unexplained pair** — `_post_end_calls` 7 → 8 and `6.005 s → 8.009 s`.
- **The stale line-number citations** — the repaired gate is at `:4964` and the single CMD1 call is still
  at `:4808`; 798 §5, 799 §3 and 803 §8 name `:4785` and `:4702`, which were already stale before this
  step and are now staler. **802 §7's owed check** — every `docs/experiments/**` line citation still
  resolving — is now more valuable, not less: this step moved the gate again.
- **`entry_storage.c`'s `>= 30` guard at the CMD2 flags site** — it should be `== 30` (798 §4). COST.
- **796 §1/§3/§8's `0x0209` sentences** and **797 §1's six 1.200 s attributions** — COST.
- **`TIMEOUT_CONTROL`'s real scope** (798 §2) — unmeasured, and less urgent now that the 665.2 µs it was
  thought to bound was never a timeout.
- **The effective bit rate, 171.5 kHz against rung 6's configured 400 kHz** (795 §10) — untouched.
- Unchanged from 787–803: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (**now vindicated,
  and still a comment, so the check is still owed**); `c3.inhibit_timeout` for the `nidx` family (COST);
  the `*_status_post` class (791 §5); the four `5,088,000`s and the mis-citation at
  `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE` sites per key (789 §4); the
  `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5); `_cid_ps_after`'s second producer
  (789 §2, COST); the set-comparison pad repair (779 §7); the `rung_para` correction for values 12..23;
  the seam-address class; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic
  FDT cell (782 §6); 784's `rail_name` cell; and 783's window-scope check.
