# 806: the rung-34 narration ran the idiom it quoted — the class's own check widened to `$(` by a measured counter-example

**A REPAIR TO THE PRESS PATH'S NARRATION, FOUND BY THE CHECK THAT OWNS THE CLASS AND NOT BY A RUN.**
No press, no device action, no runner, no firer, **no change to any armed bytes**. Three files move:
`tools/verify_press_ready.sh` (one character), `tools/check_backtick_messages.sh` (a second rule, a
selftest, and the measured width of both) and the `Makefile` (the selftest joins `make check`).

## 1. What was wrong, and it printed an error at the operator

The rung-34 readiness narration - the paragraph 804 added for rung 33, which is what the operator
reads immediately above the verdict table - quoted a shell idiom as code:

```
and \`x=$(... | grep ...)\` fails the same way when grep matches nothing
```

The **backticks are escaped and the `$` is not.** Inside a double-quoted shell string `\`` is a
literal backtick but `$(` is a live **command substitution**, so the tool ran `... | grep ...` and
printed, in this order:

```
tools/verify_press_ready.sh: line 1256: ...: command not found
...
VERDICT CHECK                              READING
```

and the sentence itself came out with its code span **deleted**:

> ... and `x=` fails the same way when grep matches nothing ...

**So the paragraph whose subject is a clause that "refused in silence twice" was itself printing a
sentence with a hole in it, four lines above a table that said `ok`.** That is m730/m752's class with
the escapes the other way round, and it is worth stating plainly why the two are the same defect: **a
backslash before a backtick quotes a NAME; it does not quote a `$(` written beside it.**

## 2. Why it is not a typo, and why the check that owns the class found it

`tools/check_backtick_messages.sh` exists for exactly this family - m730 (five unescaped backticks
in this same file's rung-14 narration), m743, m750, m752 (`src/entry/build_entry.sh`, nineteen prose
backticks, two observed to fire in a clause's own success report). Its header already says the thing
the defect turns on: inside a double-quoted string these characters are code, not punctuation.

**The class was one character wider than the check.** The check's rule was *an unescaped backtick in a
double-quoted string*, and this instance had **no unescaped backtick at all** - it had an unescaped
`$` inside a pair of correctly escaped ones. Running the check on the tree as 804 left it:

```
tools/verify_press_ready.sh:1256: 1 `$(` inside a backtick-quoted code span, in a double-quoted string
check_backtick_messages: REFUSED ... exit=1
```

**The check fired on the very paragraph that records the class it belongs to**, which is the shape
this project keeps meeting: the repair belongs to the class, not to the file where it was found.

## 3. The rule, and the width, which was fixed by measuring a counter-example

The new rule is *an unescaped `$(` **inside a `\`...\`` code span** in a double-quoted string is a
refusal*, and both halves of that phrasing were chosen against something measured:

- **Inside a span, not anywhere in the double quotes.** A narration line legitimately interpolates -
  `bad 'the park verifies...' "$(printf …)"` *computes* part of the sentence - so a rule against `$(` in
  double quotes would have refused correct code in eight places and been turned off. A code span is
  where a reader assumes quoting.
- **`$(`, not `$((`.** The widened check's first run refused a **second** site,
  `src/entry/build_entry.sh:32339`, and that site is **correct**. It reads
  ``\`cmp #${sxw_cimm:-none}\` ... or the folded \`cmp #$((SEAM_POST_END_RUN - 1))\` with \`bhi\``
  and its siblings on the line (`$SEAM_POST_END_RUN`, `${sxw_cimm:-none}`, `${sxw_caddr:-?}`) are
  interpolations the message is built from. Measured with the variable the script sets:

  ```
  SEAM_POST_END_RUN=18  ->  either `cmp #18` with `bcs`/`bhs`, or the folded `cmp #17` with `bhi`.
  ```

  which is the sentence's own intent. **So a code span in this project's messages is a quoted TOKEN,
  not a no-substitution zone** - arithmetic yields a VALUE and is written there on purpose, while a
  command substitution inside one runs a command from prose. That is m775's discipline read forwards:
  the rule was narrowed because something outside it was **read**, not because the check was
  inconvenient.

## 4. The selftest, and the three mutations that prove it

A rule whose refusals have never been observed is not a check, and this one's only positive was a
one-off run before the fix. So the check now carries **`--selftest`**, nine fixtures, and it is wired
into `make check` ahead of the tree scan:

| fixture | expected |
| --- | --- |
| m730: an unescaped backtick in prose | **refused** (2) |
| the same line with both escaped | passes |
| **m806: `$(` inside a code span** | **refused** (1) |
| m806 escaped: `\$(` inside a code span | passes |
| **the measured counter-example: `$((N - 1))` in a span** | **passes** |
| an interpolation outside any span | passes |
| a `$(` after the span closed | passes |
| a backtick inside single quotes | passes |
| a span inside single quotes | passes |

**And the selftest was itself mutated three ways, on copies, none of which may pass**: dropping the
`$((` exemption failed the counter-example fixture; dropping the span condition failed the two
outside-span fixtures; and disabling backtick detection failed the m730 fixture. Each returned
**exit 2** - a code reserved for *the check's own width is wrong*, so it is distinguishable from the
exit 1 that means *the tree has a defect*.

## 5. What it does not change

- **No armed byte moves.** The eleven members of `armed-storage-7341f5f6` are untouched and the park
  is not rewritten; neither `tools/verify_press_ready.sh` nor the check is a member of it, and neither
  is a build input of the entry image's manifest.
- **The arm is still ARMED AND NOT PRESSED**, and the press is still the operator's.
- Re-run after the repair: **readiness 5 of 5, exit 0**, with the narration printing its code span
  intact and **no `command not found` anywhere in the output**; `make check` exit 0.
- **The goal is not advanced.** No transfer completes, no filesystem is reached, no mount is made, and
  **TWRP-to-storage stays withheld**. The next rung is still CMD9's `MMC_RSP_R2`.

## 6. Owed, and named

- **One stated false positive**: in the `Makefile`, a `$` reaching the shell is written `$$`, so a
  correct `` \`$$( )\` `` code span is reported. It fired on the first draft of `make help`'s own new
  sentence; the sentence was **reworded rather than the rule widened**, and the limit is now in the
  check's header.
- **`src/entry/build_entry.sh:32339` is NOT owed** - it was measured and it is correct (§3). It is
  recorded here because a reader who runs the widened check on an older tree will see it refused.
- The 804 debt is unchanged and still open: **CMD9's `MMC_RSP_R2`**; the rung-33 press's unexplained
  `_post_end_calls` 7→8 / `6.005 s`→`8.009 s` pair; the stale line-number citations (798 §5, 799 §3,
  803 §8 name `:4785`/`:4702` while the gate is at `:4964` and the single CMD1 call at `:4808`); and
  **802 §7's owed check that every `docs/experiments/**` line citation still resolves**.
