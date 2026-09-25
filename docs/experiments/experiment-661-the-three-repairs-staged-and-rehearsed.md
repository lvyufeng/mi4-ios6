# 661: the three repairs 660 named, staged and rehearsed in `/tmp` — because the tree is closed

660 named four repairs and recorded that none could be made while a watcher is armed. This step does the
part of them that needs no window: **each repair is written and rehearsed outside the tree**, so that when a
window opens the change is mechanical rather than designed, and so that the design itself is measured today.

Nothing was built, no device was touched, and **no file in 660 §5's closure was edited** — every artifact
here lives under `/tmp/g661`, and the tree's only change is this document and one index row.

The three, in the order of how much they close:

| # | repair | file it belongs to | rehearsed how | state |
| --- | --- | --- | --- | --- |
| R1 | **test** readiness in the firing path instead of `say`ing it | `/tmp/r654/press-on-clear.sh` | the extracted decision block against both exit codes | ready (§1) |
| R2 | **the gate reads the owed arm's identity** | `preflight_boot_check.sh` | four directions, one PASS + three REFUSE | ready (§2) |
| R3 | **anchor the storage-symbol patterns** | `preflight_boot_check.sh` | 9/9 storage-shaped positives caught (the current pattern gets 8/9), 0 on both real images | ready (§3) |
| R4 | 654 §6, the runner-side pin | `run_and_capture.sh` | — | still a design note |

## 1. R1 — a verdict that is printed is not a verdict that decides

660 §3 found the shape: the watcher captures the readiness tool's status into a `say` and calls the gate on
the next line, with no `if` between. The patch is three lines, and the line that matters is *where the capture
happens* — `PIPESTATUS` is overwritten by the very next command, so it is read on the line immediately after
the pipeline:

```bash
tools/verify_press_ready.sh --set armed-seam-poc-a43304f2 \
                            --park out/stage90/frozen/armed-seam-poc-a43304f2 2>&1 | tail -14
ready=${PIPESTATUS[0]}        # captured here, before anything else runs
say "readiness exit=$ready"
if (( ready != 0 )); then
  say "readiness REFUSED the press - NOT firing, no gate and no runner ran. Rows above say why."
  exit 1
fi
```

`/tmp/g661/press-on-clear.v2.sh` is the whole watcher with that block in place (`bash -n` clean), and the
block alone was extracted and rehearsed against both exit codes — because a branch that only ever sees one
value is 630's problem one level in:

```
READINESS EXIT=0 -> block exit=0 |   say: carrying on to the gate
READINESS EXIT=1 -> block exit=1 |   say: readiness REFUSED the press - NOT firing
```

**Why this is the repair that belongs in the watcher rather than in the gate.** The gate is where the bytes
are sent, so it must eventually carry the identity check (R2) — but the readiness tool is the only thing that
compares `out/` against *the arm the record says this press is owed for*, and until R2 lands, R1 is what makes
that comparison able to stop a fire.

## 2. R2 — the gate clause that asks *which arm*, not *is this consistent*

660 §3's hole: the manifest is rewritten by the same build that writes the image, so image + config +
manifest move together and every existing clause is satisfied by construction. The clause below instead reads
`stages/stage90/revert-set.txt` — **whose own header says it is written by hand in the step that measured the
set and never by the build**, *"a record the build writes agrees with itself and constrains nothing"*, which
is 660 §3's finding stated by the record a year earlier. The question it asks:

> do the bytes in `out/stage90/` equal, file for file, **one** of the sets recorded by hand?

* A rebuild of **unchanged** sources passes, and that is correct rather than a loophole: `build_entry.sh` is
  byte-for-byte reproducible, so such a rebuild produces the *same* hashes. A rebuild of a **changed** source
  produces a set nobody recorded, and is refused.
* A set **mixed** from two arms is refused even though every individual file matches some set — which is
  exactly what the readiness tool's `--set` argument prevents by name and the gate cannot.
* The refusal names the near-miss (`10/11, differs: xnu_arm_entry.bin`), so the operator sees *how* it is
  wrong and not only *that* it is.

The implementation is `/tmp/g661/identity.sh` (40 lines, using the same `sha256sum` the rest of the chain
uses), and all four directions were measured on real bytes:

| case | `armed-seam-poc-a43304f2` | verdict |
| --- | --- | --- |
| **the real `out/`** | **11/11** | **PASS** — and the other two sets read 2/11, so the match is discriminating and not a tautology |
| one byte flipped at offset 100 of `xnu_arm_entry.bin` | 10/11, differs: `xnu_arm_entry.bin` | REFUSE |
| `xnu_arm_entry.bin` swapped for `armed-sleepless-696a0f39`'s | 10/11, differs: `xnu_arm_entry.bin` | REFUSE |
| `stage90-qcdt.img` removed | 10/11, differs: `stage90-qcdt.img` | REFUSE |

**The positive control is the row that matters**, and it is the one this session's own m668 says to take
first: the clause is only worth landing if it can say *equal* — and the 2/11 readings of the other two sets
show the comparison is doing work rather than passing everything.

**One property to keep in mind when it is landed, and it is not a defect:** this clause pins the arm the press
sends, so a *legitimate* new arm must be recorded in `revert-set.txt` **before** its press. That is the
project's existing discipline (the record is written in the step that measured the set), now enforced by the
gate rather than by memory.

## 3. R3 — anchoring the storage-symbol patterns, measured both ways

658 §3(a) found the gate's symbol tripwire blind to the entry image, and found that widening its scope
unchanged would refuse a clean payload: the current pattern hits **25** symbols in `xnu_arm_entry.elf` and
**all 25 are false positives**, because `ufs` matches `bufsize`/`bpf_bufsize`/`nkdbufs`/`ubc_upl_maxbufsize`
and `partition` matches `IODTNVRAM`'s `getNVRAMPartitions`/`readNVRAMPartition`/`writeNVRAMPartition`.

The rule that separates them is a **delimiter**, not a word boundary — and the difference is measured, not
argued. `\b` treats `_` as a word character, so `\bpartition` **misses** `bdev_partition_scan` (the shape this
project's own storage references would take). The rule that works is *"not preceded by a letter or digit"*:

```
(^|[^A-Za-z0-9])(sdcc|emmc|nand|mmc|ufs|flash|partition)
```

| symbol list | hits | note |
| --- | --- | --- |
| a synthetic positive control, 9 storage-shaped names (`sdcc_msm8974_init`, `emmc_read_block`, `nand_read_page`, `flash_write_page`, `ufs_init`, `mmc_host_alloc`, `bdev_partition_scan`, `g_storage_partition_count`, `sdcc_probe`) | **9/9** | every one caught |
| the same names under `\b…` anchoring | 7/9 | **misses both snake_case `_partition` names** |
| under the **current** unanchored pattern | 8/9 | and the one it misses is `mmc_host_alloc`: `\bmmc\b` cannot match it, because `_` is a word character — so the current pattern is weak on a true positive as well as noisy on 25 false ones |
| `out/stage90/xnu_arm_entry.elf` (27387 symbols) | **0** | the **25** false positives are gone |
| `out/stage90/stage90.elf` (568 symbols) | **0** | unchanged, as it must be |

**And the gap that remains, named with a measurement rather than in prose:** a **camelCase** storage name is
still missed — `AppleSDXCController_probe` matches nothing here, because the rule needs a delimiter and
camelCase has none. The symbol tripwire is a *heuristic*, which the address checker's own docstring already
says of itself (*"the real guarantee is … the mapping"*), and the honest statement of R3 is that it removes 25
false positives and one whole class of true negatives while leaving another class uncaught — not that it makes
the check complete.

## 4. What this does not do

* **It does not land anything.** All three repairs live under `/tmp/g661`; the tree's only change is this
  document and its index row. Landing them is what waits for a window with no watcher, and the texts above are
  what makes that landing mechanical.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot was observed; `poll_seq` still
  stops at 2 and `slot_post_calls` is still absent.
* **It does not rehearse R1 end to end** — the decision block was extracted and exercised, not the whole
  watcher (which cannot be, while the real one is armed: running v2 would create a second watcher, and there is
  exactly one press).
* **It does not close R4.** 654 §6's runner-side pin stays a design note.
* **It does not put R2's clause inside the gate's own structure** — no line number is chosen, no `fail`
  message is wired into the gate's section order. That is deliberate: the clause's *question* is now measured,
  and where it goes in the file is a decision to make against the file, in the window.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 5. Safety

No device action: no `fastboot`, no `adb`, nothing sent anywhere, **nothing written to storage**. Every
artifact is under `/tmp/g661`: two `nm` dumps, the symbol `cells`/`poscontrol` fixtures, the watcher's v2 copy,
the decision-block rehearsal, `identity.sh` and four copies of `out/`. **No file in 660 §5's closure was
edited**, no build was run, and the arm still verifies against the `armed-seam-poc-a43304f2` park. `fastboot
boot` only, never `flash`.
