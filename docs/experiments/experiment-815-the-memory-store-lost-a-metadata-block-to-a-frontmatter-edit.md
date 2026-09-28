# 815 — the memory store's own frontmatter: a `metadata:` block lost by a frontmatter edit, and three descriptions over budget

**A HOST-SIDE STEP ON THE MEMORY STORE, NOT ON THE LADDER. NOTHING WAS BUILT, NOTHING WAS SENT, NO
BYTE UNDER `out/` MOVED.** It repairs the frontmatter of two memory files, gives five more a missing
`metadata.type`, moves three over-budget descriptions into their own files' bodies, and **widens the
sweep that was supposed to catch all of this**. No device action of any kind.

---

## 1. The defect, and it is mine, one step back

814's memory pass rewrote the frontmatter of the two live-state files with this shape:

```python
close = next(i for i in range(3, len(lines)) if lines[i] == "---")
desc, body = lines[3:close], lines[close:]
```

`lines[3:close]` is **the description *and* the `metadata:` block that followed it** — the block sat
between the last description line and the closing fence. The new frontmatter was written as
`lines[:3] + new_description + "---"` with the body appended, so **the metadata block was replaced
along with the description**, silently, in an edit that was supposed to touch prose only.

Measured after the fact: **48 of 50 memory files keep `metadata:` inside the frontmatter. The only
two that did not were exactly the two that edit had rewritten** — `mi4-hardware-run-safety-gate.md`
and `mi4-stage90-phase-status.md`, the two most load-bearing records in the store.

## 2. Why the standing sweep did not catch it

The sweep parses the frontmatter and asserts that `description` is a string. **It asserts nothing
about `metadata`.** Both damaged files still parsed and both descriptions were intact, so the sweep
printed `FRONTMATTER CLEAN` — a green verdict about a field it never read, on a file that had lost a
different one.

That is `[[mi4-silence-is-a-reading-only-if-success-is-silent]]` one step over: the check was not
silent, it was **narrow**, and a narrow check reads exactly like a wide one that passed. m695 broke
`description`, a field *inside* the swept region; this broke `metadata`, a field *outside* it.
**A sweep is only as wide as the field it reads.**

## 3. The residual that found it

The deleted block's own tail was left behind **in the body**, as bare column-0 text: `metadata:`
followed by `  modified: ...`, at **`mi4-hardware-run-safety-gate.md:4228`** and
**`mi4-stage90-phase-status.md:1034`**. Every later scan that looks for frontmatter — `awk
'/^metadata:/'`, a `split('---')`, a heading grep — reads that fragment as the real thing. That is
how the loss was noticed: a stray `metadata:` sitting below 4,000 lines of prose.

## 4. The repair, and what could not be repaired

* Both files got a `metadata:` block back. The phase status's values were **recovered verbatim from
  the body copy** — the moved region had carried them along, including its `originSessionId`
  (`b99a9e3f-…`). The safety gate's `originSessionId` was **not recoverable anywhere and is omitted
  rather than invented**.
* The two stray body fragments were **indented four spaces**, so they render as code and can never
  again be mistaken for frontmatter. **Nothing was deleted**; each fragment is still there.
* **Five further files** carried a proper `metadata:` block with `node_type` but **no `type:`** — a
  pre-existing gap, not this edit's — and were given one: `feedback` for the defect classes
  (`a-claim-in-a-comment`, `generator-output-kinds`, `not-absent-its-build-output`), `project` for
  the state records (`phase4-xnu-arm-build`, `xnu-compile-state`).

## 5. Three descriptions had grown past the budget the index states for itself

The index's own rule is one short line per memory with the detail in the file. Three files were
carrying their whole history in `description:`, which is the budget that *loads* on every recall
that touches them:

| file | before | after | the body section it moved to |
| --- | --- | --- | --- |
| `mi4-measurement-defects.md` | 48,917 chars | 719 | `## The frontmatter description as it stood on 2026-09-28, verbatim` |
| `mi4-one-value-two-definitions.md` | 28,160 | 751 | same |
| `mi4-a-claim-in-a-comment-is-not-a-check.md` | 22,475 | 702 | same |

**Total description bytes across the store: 153,312 -> 55,932.** The move is **verbatim** and
partition-checked: every non-empty line of each old description is still in its file.
`mi4-stage90-phase-status.md` had already been through this on 2026-09-27 and is untouched here.

The index itself was over its read budget by 635 bytes, so six hooks were shortened — the
measurement-defects lead (1,849 -> 617 bytes, its instances moved into the file, where they already
were) and the compaction history, which is now `[[mi4-memory-index-compactions]]`.

## 6. The sweep is the deliverable, and its width is the whole point of this step

**Widened: it now asserts the keys it expects to keep, not only the scalar it wrote.**

```sh
cd /home/lvyufeng/.claude/projects/-mnt-data-mi4-ios6/memory && python3 -c "
import io,glob,yaml
for p in sorted(glob.glob('*.md')):
    if p=='MEMORY.md': continue
    L=io.open(p,encoding='utf-8').read().split('\n')
    end=[i for i,l in enumerate(L) if l=='---'][1]
    d=yaml.safe_load('\n'.join(L[1:end])) or {}
    m=d.get('metadata') or {}
    wide = isinstance(d.get('description'),str) and d['description'].strip() and m.get('type')
    print(('ok   ' if wide else 'FAIL ')+p)
"
```

Measured on the repaired store: **50 files, 0 failing.** And the rule for the *edit* is the same
rule: **an edit that replaces a region of the frontmatter must assert the keys it means to keep**,
not just the text it means to write — `assert 'metadata:' in new_fm` before writing, the way m695's
repair asserts the inserted text carries no apostrophe *before* the insertion rather than after the
parse failure. m695 and this are the same mistake at two widths: **an edit whose blast radius is
assumed from its intent.**

## 7. What this does not do, and the goal

It does not touch the ladder, the arm, the record or the press path. It creates no check in the
repository — the store has no CI and the sweep is a command a person runs, which is stated here
rather than implied.

**THE GOAL IS NOT MET.** No transfer completes, no filesystem is reached and no mount is made, so
**TWRP-to-storage stays withheld**. An arm is **armed and not pressed** — rung 35,
`armed-storage-47c657af` — and **the press is the operator's.**
