# 702: the address was compared as a number — a build clause failed an image over a defect in its own reader

701's rung-4 code was written, the census clauses for its new `hc_mem` store were added, and the first
build **failed**:

```
FAIL: no branch to __wrap_Idle_load_context is inside cpu_idle_exit (0x8000e504..0x8000e38c):
      the pass that reached the wfi comes back through that site, and a run whose door-3 count has no
      exits to match it would leave the step's cross-check one-sided
```

**The range is inverted** — `0x8000e504` to `0x8000e38c` is a window with no instructions in it — and the
clause's own subject (`cpu_idle`, its exits, and the wfi's door) is not what rung 4 touches. What the rung
touched is a symbol's **spelling**. Everything else in this record follows from that.

## 1. The mechanism: a hex address that parses as a number is a number to `gawk`

`build_entry.sh`'s `sym_next` (and, identically, `next_global`) found "the symbol after this address" by
scanning `nm -n`'s output for the address:

```bash
sym_next() { arm-none-eabi-nm -n "$OUT/xnu_arm_entry.elf" \
             | awk -v s="${1#0x}" '$1 == s { p = 1; next } p && !d { print "0x" $1; d = 1 }'; }
```

`$1 == s` looks like a string comparison and is not one. `nm` prints addresses as eight hex digits, and
`s` comes from `-v`, so **both sides are *numeric strings*** whenever the spelling parses as a number —
and `8000e504` parses as `8000e504` = `8000 × 10^504` = **+inf**. Every address whose digits after the `e`
are all decimal digits is that same `+inf`:

```
8000e504   -> +inf        (e504: all decimal digits)
8000e314   -> +inf
8000e828   -> +inf
8001e348   -> +inf
8002e330   -> +inf
8000e58c   -> 8e+61       (the `c` is not a digit: the whole field is not a number, so it is a STRING)
8000e3e8   -> 8e+41
8000e38c   -> 8e+41
```

Measured on the rung-4 image:

```
$ arm-none-eabi-nm -n xnu_arm_entry.elf | awk -v s="8000e504" '$1 == s { n++ } END { print n }'
416
```

**416 lines.** `sym_next 0x8000e504` set its `p` flag on the first `+inf` address in the table, skipped
every other one, and printed the first line after them that is *not* `+inf` — `0x8000e38c`, the symbol
**before** the one it was asked about. `cpu_idle_exit`'s window became `[0x8000e504, 0x8000e38c)`, an empty
range, and the clause that consumes it reported a failure about the image.

## 2. Why it was invisible until rung 4 — two accidents, not a design

The reader had been right for every arm before this one, and both reasons are properties of the addresses
that happened to be asked about rather than of the reader:

* **the spelling.** 699's arm had `cpu_idle` at `0x8000e19c` and `cpu_idle_exit` at `0x8000e2b8` — `19c`
  and `2b8` are not decimal digits, so both were ordinary strings and `==` was the string comparison it
  looked like. The same function's window moved into the `e<digits>` shape (`0x8000e3e8` … `0x8000e58c`)
  only when this rung's code moved the symbol table by 4–0x24c bytes.
* **`next_global` was wrong the same way, everywhere.** It carries the identical `$1 == s` on its match
  line; on an address of that shape it sets its start marker on the wrong line and then, because its print
  condition is `a > sa` with `sa` left at `+inf`, returns **nothing**. That is the same wrong answer in the
  direction that reads as *"no symbol follows this function"* rather than as *a window with a hole in it* —
  a different message from the same defect.

**So rung 4 did not cause this.** It walked an existing reader into the input class the reader could not
handle, and the class is *reachable by any edit that shifts a symbol's address into it*. A defect that
needs a spelling to fire is a defect that waits.

## 3. The repair, and the control in both directions

```bash
sym_next() { arm-none-eabi-nm -n "$OUT/xnu_arm_entry.elf" | awk -v s="${1#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a == sv { p = 1; next }
    p && !d { print "0x" $1; d = 1 }'; }
```

`strtonum` returns a **number**, and a number on both sides is a numeric comparison with nothing left to
parse. The same change is applied to `next_global`. Measured on the rung-4 image, old against new:

| address asked about | `sym_next` old | `sym_next` new | `next_global` old | `next_global` new | the truth |
| --- | --- | --- | --- | --- | --- |
| `0x8000e504` | `0x8000e38c` **wrong** | `0x8000e58c` | `0x8000e38c` **wrong** | `0x8000e58c` | `8000e58c T cpu_init` |
| `0x8000e3e8` | `0x8000e504` | `0x8000e504` | `0x8000e504` | `0x8000e504` | `8000e504 T cpu_idle_exit` |
| `0x8000e19c` | `0x8000e1a8` | `0x8000e1a8` | `0x8000e27c` | `0x8000e27c` | `8000e1a8 t Lwordalignloop2` |

**Both directions.** The repaired readers agree with the old ones on every address that is not `+inf` —
i.e. the repair changes nothing except the case that was wrong — and the clause that had failed now reads
`cpu_idle (0x8000e3e8..0x8000e504)` and `cpu_idle_exit (0x8000e504..0x8000e58c)` with its exit site at
`0x8000e588(lr 0x8000e58c)`, which is the last instruction of the function.

The lesson is the one this project keeps re-learning in new clothes: **a build that stops is a reading
about the instrument before it is a reading about the artifact.** The clause's message named a plausible
failure (a door with no exits to match it) and the image was fine, and the only thing that distinguished
them was that `0x8000e504..0x8000e38c` is not a range.

## 4. Two more tool defects of the same step, both of them "the check said ok"

Neither of these is the same mechanism as §1, and both are recorded here because they were found by the
same step and both hid behind a verdict that was already `ok`.

**(a) A narration paragraph silently dropped by two unescaped quotes.** The rung-4 readiness paragraph was
written into `tools/verify_press_ready.sh` with the phrase `the record's "one store"` inside a
double-quoted shell string. The quote ended the string early, the rest of the sentence became a command
name, and bash reported `File name too long` **on stderr** while the assignment it was attached to
published the fragment it had already parsed. The naming row therefore printed `ok` with the arm named,
the name-check printed its reading (`37 name(s) and 1 glob(s)`, every one resolving), and **the whole
rung-4 cells paragraph was absent from the narration** — because the names it introduces were absent too,
so nothing it would have been checked against was checked. It was found by grepping the narration for a
sentence the file contains and the output did not (`0` hits), then confirming with an instrumented copy in
place: `DEBUG wst=[4] eq4=yes` and the same `File name too long` on line 707. The repair is the two quotes
escaped; the run after it has an **empty stderr**, the paragraph present, and `55 name(s) and 3 glob(s)`,
all resolving. *This is 700 §1's shape one file over*: a narration can be non-empty, wrong, and reported
`ok` — and here the check that would have caught it (the name census) was defeated by the same failure
that removed the names.

**(b) A record line whose prose became part of a field.** The rung-4 block in `stages/stage90/revert-set.txt`
is a list of `set=<name> sha256=… bytes=… file=… role=…` lines, and the last field of the manifest line is
`manifest_members=` … — which runs **to the end of the line**. Prose appended after the member list became
five filenames followed by a sentence, and `tools/verify_revert_set.sh` refused the set with
`the record's line 1254 has a field this script does not know: 'and'` — the tool working exactly as
designed, because an ignored field is how a typo in `sha256=` becomes a file nobody checked. The repair
moved the prose into `role=` and ended the line at the member list; the same run then reported
`VERIFIED: 11 file(s) … and 6 manifest-member check(s) agree with the set`. **A field boundary is a
reading, and a sentence after the last field is silently part of it.**

## 5. What this does not do

* **It does not change any artifact.** `build_entry.sh` is in the arm's 11-file set, so its repair does
  change the *sources* manifest — but it changes no byte of any built thing, and the rung-4 image built
  before the repair (which failed) and after it (which passed) are not comparable by hash: the first was
  never completed. The clean rung-4 build reproduces `00b28262…` on both of its runs.
* **It does not press anything by itself.** §4's repair was in a narration, and the press that followed
  (`703`) is the arm's own step.
* **It does not claim the class is exhausted.** The two other address comparisons of the same shape in
  `build_entry.sh` (`x467_word_at`, `§513`'s objdump line match) read a *single* line out of a
  `--start-address/--stop-address` window, so a mis-match there returns nothing and the owner refuses —
  and the fact that they are safe *for that reason and not by construction* is stated here rather than
  assumed away.

## 6. Owed

* **A reader for "the symbol after this address" exists in three spellings in this file** (`sym_next`,
  `next_global`, and the inline `next_global`-at-`-n` variant in `x467_word_at` is not one of them). Two
  are repaired and the third was never at risk; a future edit that re-derives the idiom from the old text
  reintroduces it. Named, not fixed.
* **Carried from 701**: the width clause of the store census has still not been observed to fire; the
  clock step (map `0xfc400000`, then `CORE_VENDOR_SPEC 0x10C` and `clk_set_rate`); what actually returns a
  run (8/17/24/27/24/27 s); and **TWRP-to-storage stays withheld**.
