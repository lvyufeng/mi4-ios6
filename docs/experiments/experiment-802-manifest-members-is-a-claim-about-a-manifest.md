# 802: the check 801 owed — `manifest_members=` is a claim about a manifest, so it is now compared against one

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** No `sudo`, no `adb`, no `fastboot`. **`out/` was not touched** — every byte of `out/stage90/` and
of the park `out/stage90/frozen/armed-storage-ce2f589c/` is identical before and after, and the arm is
still **ARMED AND NOT PRESSED**. One press spent: **none**. **No press is authorized — the press is the
operator's.**

801 found a false field in the armed arm's own record and corrected it, and named the repair it was not
making: *"a check that compares `manifest_members=` against the manifest it describes, in BOTH
directions"*. This step is that check. **It is the difference between a field that is right today and a
property that is checked.**

## 1. Why neither existing check could see 801's defect

`tools/verify_revert_set.sh` already had two manifest-member checks, and 801's record satisfied both:

| check | reads | closes | 801's field |
| --- | --- | --- | --- |
| **6a** | the record's `manifest_members=` field | the FIELD over the SET — `field <= set` | **satisfied** — both names it carried are `file=` lines |
| **6b** | the target's manifest on disk | the MANIFEST over the SET — `disk <= set` | **satisfied** — all five names are `file=` lines |

**Both legs compare something to the SET. Nothing compared the field to the manifest.** A field that is
**short of** the manifest therefore passes both, which is exactly what 801's record was: it named
`stage90_fixture.macho,stage90.img` — the first and the last of the canonical five — and every line of
the verifier printed `ok` while thirteen predecessor sets named all five.

**So this is a missing relation rather than a weak assertion**, and that is why it is a third check and
not a widening of either leg.

## 2. The refusal, and why it is one rather than a note

The gate verifies the manifest with `sha256sum -c` (`preflight_boot_check.sh:139`), and `sha256sum -c`
opens **every path the manifest names**. A member named only inside the manifest is therefore *read by
the gate*. A reader who trusts the field — which is what the field is **for** — reverts the field's
members and gets **a red gate with every recorded hash matching**.

The two directions are refused with **different wording, because they have different causes**:

| direction | reading | where a reader goes |
| --- | --- | --- |
| the manifest names a member the **field omits** | the field was truncated or never finished | fix the field |
| the field names a member the **manifest omits** | the field describes a manifest that is not here | the field is stale, or the manifest is |

The second direction has one name that recurs and is worth stating: **`SHA256SUMS.txt` is a `file=` line
of every set and never a member of its own member list**, because a manifest that lists itself records
the hash of the empty file the redirect truncated. A field naming it is a field describing a manifest
that no longer exists.

`out/stage90/frozen/armed-storage-ce2f589c` verifies clean under the new check: **7 manifest-member
checks** (5 for 6a, 1 for 6b, 1 for 6c), exit 0.

## 3. Both directions falsified, as cells, and neither is visible to 6a or 6b

The project's rule is that a refusal never shown to fire is a claim, so the rehearsal
(`tools/rehearse_revert_set.sh`) gained three cells — and the third is the one that keeps the second
honest:

| cell | fixture | asserts |
| --- | --- | --- |
| `field-short-of-manifest` | the fixture record's field truncated to 3 names | exit 1, `the record's field for 'fixture' does not:` |
| `field-extra-name-in-set` | the field set to `SHA256SUMS.txt,` + the ten | exit 1, `names member(s) this directory's manifest does not: SHA256SUMS.txt` |
| `field-extra-6a-satisfied` | the same record | 6a printed its `ok` line for `SHA256SUMS.txt`, **so the refusal above is 6c alone** |

**The third cell is not decoration.** `field-extra-name-in-set` passes only if the refusal comes from
6c; if 6a also fired, the cell would be green on a message it is not about. It is a preflight with its
own refusal rather than a `cell`, because the property is the **absence** of 6a's FAIL, and `cell`
asserts an exit code and a phrase — neither of which can express "this other check stayed quiet".

**Battery total: `41 ok, 0 failed`, exit 0** (was 38; the three new cells).

## 4. And the first draft of that preflight was wrong in a way this project has a name for

It was written the obvious way:

```sh
if bash "$TOOL" "$MAN" --record="$EXTRAF" 2>&1 | grep -q 'PATTERN'; then
```

**and it failed while the pattern matched.** The tool exits 1 by design under this record — 6c is
refusing — and `rehearse_revert_set.sh` runs `set -o pipefail`, so the **pipeline's** status is the
tool's and the `if` never sees grep's. The status being read belonged to a different producer than the
one the sentence named: the project's own *a status is a verdict only if its producer delivered one*,
one door over. The repair captures the output and then greps it, which is also what `cell` does.

**It is recorded here rather than quietly fixed because the failure mode was a cell that would have gone
red for a reason nothing in its own text named** — the same shape as the three cells 801 repaired.

## 5. The header's own partition, updated to four legs

The verifier's header carried a table of which check reads what, written when the split was three legs
(5, 6a, 6b). It now carries four, with 6c's row reading *"the target, against the record's FIELD"* and
its outcome *"the record's `manifest_members=` field DISAGREEING with the manifest it claims to describe
— in either direction"*. The `--help` cell asserts the header's **last** line, and the new text is
inserted **before** the Usage section, so the truncation check is unaffected.

## 6. What this does not do

- **It spends no press, authorizes none, and changes no byte of `out/` or of any park.** The arm is
  still `armed-storage-ce2f589c`, armed and unspent; the park verifies **11 ok / 0 failed** and all
  eleven live members are `cmp`-identical to it.
- **It builds nothing.** No entry image, no payload, no rebuild.
- **It does not touch the record.** 801's corrected field was already right; this step is what makes it
  *checked*. The record's bytes are unchanged by this step.
- **It reaches no further than the revert set.** It says nothing about the ladder, the arm's own bytes,
  or the press.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld**.

## 7. Owed, and named rather than left to be inferred

- **The rung-33 press** — `armed-storage-ce2f589c`, armed, parked, readiness 5/5, and the operator's.
- **`entry_storage.c`'s inverted CMD2 gate** — the driver proceeds when bit 31 SETS; the ladder's gate
  proceeds when it is CLEAR. Source, and it belongs to the build the press makes possible. The line has
  **moved** to `:4911` (was `:4785` in 798) because the ladder grew, and `st_send_command`'s single CMD1
  call is at `:4808` (was `:4702`) — **the citations in 798 §5, 799 §3 and 800 §10 name the old line
  numbers**, which is the citation class this record already tracks.
- **A check that every site in `docs/experiments/**` citing a line number still resolves** — not owed by
  801 or by this step, but the citation drift above is the second instance in three steps and it is now
  named rather than noticed.
- **`entry_storage.c`'s `>= 30` guard** (798 §4) — it should be `== 30`, or the record must state that
  value 31 is a strict containment of value 30. COST-owed to a build.
- **796 §1/§3/§8's sentences about `0x0209`** (798 §4) — false; COST-owed to a build.
- **The six live-prose corrections of 797 §1** — the 1.200 s attribution, COST-owed to a build.
- **`mmc_select_voltage`'s window** (799 §2, 800 §9) — deliberately not ported, and the reason is
  written where the arm is.
- **`TIMEOUT_CONTROL`'s real scope** (798 §2) — unmeasured on this ladder.
- **The effective bit rate, 171.5 kHz against rung 6's configured 400 kHz** (795 §10) — untouched.
- **A `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair** around CMD1's own `RESPONSE` registers — owed since
  787 §6.
- Unchanged from 787–801: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST);
  `c3.inhibit_timeout` for the `nidx` family (COST); the `*_status_post` class (791 §5); the four
  `5,088,000`s and the mis-citation at `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE`
  sites per key (789 §4); the `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5);
  `_cid_ps_after`'s second producer (789 §2, COST); the set-comparison pad repair (779 §7); the
  `rung_para` correction for values 12..23; the seam-address class; `run_and_capture.sh`'s
  `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6); 784's `rail_name` cell;
  and 783's window-scope check.
