# 816 — the vendor axis: a `#define`'s trailing citation is now checked against the header on disk

**A HOST-SIDE STEP THAT ADDS A CHECK. NOTHING WAS BUILT, NOTHING WAS SENT, NO BYTE UNDER `out/`
MOVED, AND THE ARM IS UNTOUCHED.** It extends `tools/check_line_citations.py` with a baseline for
the one vendor citation shape a mechanical property actually holds over, and that extension finds a
real wrong citation on its first run.

---

## 1. The gap 813 named, and the part of it that can be closed

813 section 7 closed with: **"A vendor citation is unchecked rather than checked-and-green, which is
why thirteen have now been found the slow way. That gap is the real defect; these five are its
symptoms."** 808's `check_line_citations.py` passes a vendor citation over BY NAME (`NOT-IN-REPO`)
because `external/` is untracked and git — the baseline 808 chose for *has this site moved* — has no
answer there.

**The git baseline has no answer, but the file does.** The vendor sources are on disk under
`external/android_kernel_xiaomi_cancro/`, and this project never writes to them. So a vendor citation
has a baseline: the header itself. What it does **not** have is a way to know what the citation
*meant* — which is why the axis is bounded to the one shape where the citing line says what it means.

## 2. The shape, and the two readings inside one line

```
#define ST_SDHCI_HOST_CONTROL2  0x3Eu   /* sdhci.h:79 - 16-bit, and the vendor reads it with sdhci_readw */
```

A `#define` whose trailing comment carries a citation has **two readings of the same site in the
same line**: the local NAME and the local VALUE, against a header whose line N is right there. The
anchor is the identifier the citation names, taken from both halves of the line — the local macro's
own name, that name with its `ST_` prefix stripped, and every ALL-CAPS identifier the comment writes
ahead of the citation (`ST_SET_DIV_MAX`'s line names the vendor's `SDHCI_MAX_DIV_SPEC_300` that way).

| verdict | what it says |
| --- | --- |
| `VENDOR-OK` | the anchor is `#define`d at the cited line, **or** the cited line defines a value equal to the local one |
| `VENDOR-ELSEWHERE` | the anchor is `#define`d at exactly one other line — **the refusal**, and the line is printed |
| `VENDOR-MULTI` | the anchor is defined at two or more lines — not decided here |
| `VENDOR-UNRESOLVED` | the anchor is defined nowhere and the cited line is not a `#define` whose value agrees |
| `VENDOR-NOFILE` | no `.h` with that basename under the vendor roots |

**The NAME reading is decided before the VALUE reading, and that order is the rule.** A name is an
identity; a value can coincide. So a citation whose anchor is `#define`d at exactly one other line is
refused *even when the cited line happens to define the same number* — there is a fixture for exactly
that case.

**A value that is an EXPRESSION is never evaluated.** `#define SDHCI... (1 << 11) - 2` is a fine
`#define`, and a tool that folded it and called the result agreement would be inventing a match. The
fixture uses `0x40u` against `(1 << 6)`: an evaluator would say OK and this tool must say
`VENDOR-UNRESOLVED`.

## 3. Measured in both directions, on this tree, before it landed

| text | `VENDOR-OK` | `VENDOR-ELSEWHERE` |
| --- | --- | --- |
| `src/entry/entry_storage.c` as committed | **58** | **1** — `:266` cites `sdhci.h:79`, and `SDHCI_HOST_CONTROL2` is at `sdhci.h:162` |
| the same file with `79` changed to `162` | **59** | **0** |

The rule's width is the defect's width: it refuses the one site and nothing else, over 60 define-shaped
citations across five headers (52 carried by the name reading, 7 by the value reading — the
re-spellings, `ST_SDHCI_SLOT_INT_STAT` for the vendor's `SDHCI_SLOT_INT_STATUS` and the whole
`ST_CMD_OP_*` family for the vendor's `MMC_*`).

**`SDHCI_HOST_CONTROL2` is `#define`d exactly once in the whole of `external/`** — at
`drivers/mmc/host/sdhci.h:162`; the other three `sdhci.h` files under that tree do not define it at
all. So the citation is wrong and not a resolution accident. That makes **fourteen** instances of this
class found the slow way, and this is the first found by a machine.

## 4. The check's own first run had the defect the check exists for

With the dedupe keyed on `(citing, cited, n)` the tool reported **49** vendor citations read and **no
refusal**. The classifier, driven directly over the same file, read **59** and one refusal.

The difference is the citation the axis was written for. `sdhci.h:79` is cited **twice** in
`entry_storage.c`: once in a prose comment at `:253`, and once in the `#define` at `:266`. The prose
line comes first, so `seen` kept it — the copy no axis can read — and dropped the readable one.

**One site, two readings, and the check kept the one it could not read.** That is
`[[mi4-one-value-two-definitions]]` committed by the checker, and it is the same shape as the
`MOVED / MOVED-AMBIGUOUS` split this tool already makes for in-repo citations. The key is now
`(citing, lineno, cited, n)`: per occurrence, so no site is dropped and no site is double-counted.

Also repaired in passing: the selftest's closing line reported a **typed** fixture count of 23. The
counted total is **22**. 675 section 4 measured the cost of a number written into the sentence that
reports it; the count is now taken at `say()` and a deleted fixture shows up as a smaller total.

## 5. What was fixed, what is owed, and why

**Fixed here — two citations, and they cost nothing.** 813 section 7's five wrong vendor citations
include two in `tools/verify_press_ready.sh` (`:757`, `:759`), both citing `sdhci.c:1169-1175` for a
sentence that reads *"the response is read only when the command asked for one"*. The guard is
`sdhci.c:1162` and the branch it opens ends at 1175, so both now cite **`sdhci.c:1162-1175`**.

813 assigned all five to the next build cycle for **COST**: *"Every one of them is a comment in
`entry_storage.c` or a narration belonging to another rung ... Correcting them means editing
`entry_storage.c`."* **That sentence is false for these two.** `tools/verify_press_ready.sh` is not
in the arm: the park's eleven members are all `out/` artifacts, and the record pins no hash of any
tool. Editing them costs a `bash -n` and a readiness re-run — **a cost claim that was never measured,
which is the shape m775 was recorded for.**

**Owed to the next build cycle, with the numbers, because the reason is COST and it is measured:**
`src/entry/entry_storage.c:266` (and the sentence at `:253`, which cites the same wrong line) must
read **`sdhci.h:162`**. Plus 813's four in the same file — `:2578` → `1162`, and `:2801`, `:2806`, `:3547`
→ `1162-1175`. **They are not fixed here because any edit to `entry_storage.c` reprices the armed
park**: `xnu_arm_entry-sources.txt` records that file's content hash, so a comment-only change moves a
park member and forces a withdrawal-and-repark of an arm that is **armed, unspent, and the operator's
to press**. That is 814's lesson applied in the other direction: a withdrawal is cheap *before* a
press and **never free while one is pending**.

## 6. What the axis covers, said as a number rather than as a promise

| | |
| --- | --- |
| citations the tool reads in its default scope | **673** over 3 files |
| of those, vendor citations the axis can read | **59** — all in `src/entry/entry_storage.c` |
| vendor citations still passed over by name | **518** |
| headers indexed by basename | **10,236** |
| `--all`, widened to `src/**/*.[ch]` | **5,018** citations over 1,102 files, 1m34s — the same as before, because a NOT-IN-REPO citation needs no history walk |

The default scope is the press path **plus `src/entry/entry_storage.c`**, for the vendor axis alone:
it is where the ladder's whole register table lives, it needs no history walk, and it costs 0.4 s for
207 citations — measured, not assumed. `--all` gained the `src/` sources for the same reason. The
axis reports itself **OFF** rather than green when no vendor root is present, because a check whose
data is missing and a check that found nothing read the same.

## 7. What this does not do, and the goal

It does not check the 518 vendor citations the axis cannot read, and it says so in the tool rather
than in a document. It does not check a prose citation at all — a function name cited with a range
passes whether or not the range is right, and no mechanical property over that shape was found that
does not also refuse correct body-line citations.

**THE GOAL IS NOT MET.** No transfer completes, no filesystem is reached and no mount is made, so
**TWRP-to-storage stays withheld**. Rung 35 is **armed and not pressed** under the name
`armed-storage-47c657af`, readiness is **5 of 5 exit 0**, `make check` is **exit 0** in 11.4 s, and
**the press is the operator's.**
