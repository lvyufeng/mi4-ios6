# 810 — the word order the driver owns, made a refusal instead of a sentence

**A CHECK, AND IT IS THE FIRST THING IN THIS PROJECT THAT CAN REFUSE A 136-BIT RESPONSE ASSEMBLED
WRONG.** No press, no device action, no runner, no firer, no build, and **nothing under `out/` was
written**: the eleven members of `armed-storage-7341f5f6` are untouched, so the arm stays **ARMED AND
NOT PRESSED** and the press is the operator's. `tools/check_response_word_order.py` is new,
`Makefile` wires it into `make check` with its selftest, and `make check` is **exit 0**.

---

## 1. What the record said, and what measurement did to it

809 §4 wrote the trap down and left it as advice: *"A builder who read the four words in order would get
a WRONG 128-bit value that still LOOKS like a CSD: no error, no fault, just a number that decodes to
nonsense … a new arm must CALL that assembler and not re-derive it."* That is a claim in a document. The
class it belongs to is `mi4-a-claim-in-a-comment-is-not-a-check`, and the repair is the same one this
project applies every time: **make the property structural.**

And 809's own count was low. It named **two** sites — *"the 136-bit assembler at `:3531-3544` and again
at `:3639-3654`"*. The scanner finds **six response-reading functions and 32 accesses**, in **three**
shapes:

| function | first line | accesses | shape |
| --- | --- | --- | --- |
| `st_send_command` | `:2572` | 2 | the conditional read: the offset-0 word, under `#if >= 12` and again under the `#else` |
| `st_all_send_cid` | `:3333` | 7 | **the four-word assembly** (`:3531-3544`), 4 reads + 3 CRC bytes |
| `st_resp_before` | `:3572` | 7 | the same four-word assembly again (`:3639-3654`) |
| `st_set_relative_addr` | `:3921` | 4 | **the word-0 pair**, twice — `:3995`/`:4042` |
| `st_cmd3_noresp` | `:4071` | 4 | the word-0 pair again — `:4118`/`:4162` |
| `st_cmd3_noidx` | `:4186` | 8 | the four raw words as a freshness witness, twice — `:4283`/`:4388` |

So the arithmetic is defined **five** times, not twice, and `:3995`'s own comment says the intent out
loud: *"the driver's own word-0 derivation, so that this reading, rung 17's `_cid_resp0` and rung 18's
`_rb_resp0` are one arithmetic at three times rather than three derivations that agree."* That is the
`one value, two definitions` class caught in the act of being careful — five definitions, currently in
agreement, and the next rung is the one that would add a sixth.

## 2. The ground truth, read out of the vendor's own file

`external/android_kernel_xiaomi_cancro/drivers/mmc/host/sdhci.c:1162-1172`:

```c
	if (host->cmd->flags & MMC_RSP_PRESENT) {
		if (host->cmd->flags & MMC_RSP_136) {
			/* CRC is stripped so we need to do some shifting. */
			for (i = 0;i < 4;i++) {
				host->cmd->resp[i] = sdhci_readl(host,
					SDHCI_RESPONSE + (3-i)*4) << 8;
				if (i != 3)
					host->cmd->resp[i] |=
						sdhci_readb(host,
						SDHCI_RESPONSE + (3-i)*4-1);
			}
```

Three things are load-bearing and **none of them is visible in the register's name**:

1. `(3-i)*4` — the words are read **word 3 first**: 12, 8, 4, 0.
2. `<< 8` — each word is shifted left by a byte.
3. `if (i != 3)` and `-1` — the CRC-stripped byte comes from the address **one below** the word, and the
   **last** word takes no byte at all.

The rules are read off those two expressions and not invented: a 32-bit offset in `{0,4,8,12}`, an
8-bit offset in `{3,7,11}`, the word-3-first order, and the byte one below its own word.

## 3. Why this cannot be left to a reading

Every wrong version is silent. Read the words ascending and you get the response with its words
reversed — a CID whose manufacturer field is a serial fragment. Drop the byte-below OR and each word
loses its low 8 bits: the 128-bit value still parses, and it is still wrong by exactly those bytes.
OR a byte onto the **last** word — which needs no byte — and the low byte of word 0 is a stale value
from the register above it. **There is no fault, no error and no cell that says "wrong"**: the ladder
would publish a number, a reader would decode it, and the decode would be nonsense with nothing to
compare it against. That is the shape of defect this project makes structural rather than documented.

## 4. The rule

The scanner strips comments, joins each logical statement, splits the file into functions, and then
applies three conditions:

* **OFFSET.** A `ST_SDHCI_RESPONSE` access may name a 32-bit offset in `{12, 8, 4, 0}` and an 8-bit
  offset in `{11, 7, 3}`. The last word is written `ST_SDHCI_RESPONSE` with no offset, which is the
  vendor's own `+ (3-i)*4` at `i = 3`.
* **WORD 3 FIRST, ONCE PER COMMAND.** The driver assembles `resp[]` once, when a command has completed.
  So the reads inside one **stimulus segment** — the function's response reads, broken wherever a
  `st_send_command(...)` appears — must walk the offsets **strictly descending**, and reading the *same*
  offset twice inside one segment is refused for the reason `st_cmd3_noidx`'s own comment gives: *"two
  readings of one address at one moment would be a number no cell could tell from the other."*
* **THE BYTE IS THE ONE BELOW ITS WORD.** In `<word> << 8 | <byte>`, the byte's offset must be exactly
  one less than the word's; the word is either an inline `st_read32(... RESPONSE + K)` or a variable
  whose most recent assignment in the same function is that read. **This is what makes `i != 3`
  structural**: an OR onto the offset-0 word has no offset `-1` to name, so it cannot be written without
  taking a byte that belongs to another word — which is exactly what the refusal names.

## 5. Two narrowings, each made by a measured counter-example and not in advance

The first two runs of the tool refused **four correct sites**. The rule moved, not the code — the
discipline 806 fixed one character of.

* **Comments are stripped before anything is read.** A definition is recognised by the last `name(...)`
  before a `{`, and this file's comments are full of parenthesised prose. A comment ending in a brace
  was therefore read as the definition of a function called **`29`** — the text `rung 29 (` is enough —
  which reset the enclosing body part way through and produced two `PAIR` refusals against correct code
  at `:3539` and `:3647`. The comments are prose *about* the arithmetic; the check is about the
  arithmetic.
* **A preprocessor directive ends a segment.** `st_send_command` reads the offset-0 word at `:2771`
  under `#if STAGE90_XNU_STORAGE_PROBE >= 12` and again at `:2776` under the `#else`. Those are
  **alternatives** and only one is ever compiled, so a repeated offset across a `#if`/`#else` boundary is
  not a second reading of one address — which is the whole of what the rule is about.

Both are in the tool's header, named with the site that produced them, because a rule narrowed for a
reason nobody wrote down is a rule the next reader widens again (m775).

## 6. The refusals are demonstrated, not asserted

Five mutations were applied **to copies of the file** — never to the tree — and each was refused in
place, with the enclosing function and line the mutation landed on:

| mutation | verdicts | the refusal |
| --- | --- | --- |
| the first two words read 8 then 12 | `ASCENDING`, `PAIR` | *`:3532 + 12u is read after + 8u in the same command segment`* |
| the CRC byte taken from `+ 12u` instead of `+ 11u` | `OFFSET`, `PAIR` | *`read8 reads + 12u, which is not one of 11/7/3`* |
| the **last** word OR'd with the byte at `+ 3u` | `PAIR` | *`the byte at + 3u is OR'd onto the word at + 0u`* |
| an offset the register does not have (`+ 13u`) | `OFFSET`, `PAIR` | *`read32 reads + 13u, which is not one of 12/8/4/0`* |
| the offset-0 word read twice in one segment | `REPEAT` | *`the driver assembles resp[] once per completion`* |

The **unmutated file is clean**, so the five refusals are the mutation and not the file. The second
column matters as much as the third: a mutation is caught by the rule that exists for it *and* by the
pairing rule, because a wrong word order is also a wrong pairing.

## 7. The tool's own width is testable

`--selftest` holds nine fixtures — the four shapes this tree actually holds (the vendor's loop
transcribed; the inline word-0 pair; the four raw words with no OR at all), and each of the five ways
the arithmetic goes wrong — and it returns **exit 2** for a failed selftest, a code reserved for *the
check's own width is wrong* so that it is distinguishable from the **exit 1** that means *the tree has a
defect*. 806 reserved the same code for the same reason.

The four clean fixtures are not decoration: three of them are copies of shapes that exist in
`entry_storage.c` today, so a future widening of this rule that starts refusing the tree it runs on
fails the selftest rather than the build.

## 8. What it does not do

* It does not say the card answers CMD9. It says the **arithmetic in the image's source** has one word
  order and it is the driver's; whether a CSD comes back is a press's answer.
* It does not check the payload sources or the vendor checkout. `src/entry/*.c` is the scope, because
  that is the tree the entry image is built from and the tree the gate binds the arm to by content.
* It does not refuse anything in the historical record, and it has nothing to say about a citation
  (`check_line_citations.py` owns that) or a count (`check_count_citations.py` owns that).
* It does not make the five definitions one definition. **It makes them agree and keeps them agreeing**,
  which is the strongest thing a check can do to a value that is defined five times; the collapse is a
  rung's job, and the rung that would do it is the one that adds the sixth.

## 9. What it changes for the next rung, and the goal

CMD9 (`SEND_CSD`, `ac R2`) is the next rung and 809 measured that it needs no new mechanism — the R2
flags, the assembler and the RCA argument all exist because CMD2 built them. **What was a sentence in
809 is now a refusal in `make check`:** an arm that builds the CSD in ascending order, or forgets the
byte below, or ORs the last word, fails the build instead of publishing a plausible number. That is the
only part of the next rung that could have failed silently, and it no longer can.

**No press, no device action, no build, and nothing under `out/` was written.** The six sites are
printed by name on every run, so the check reports what it read rather than only what it refused.

**THE GOAL IS NOT MET.** No transfer completes, no filesystem is reached and no mount is made, so
**TWRP-to-storage stays withheld**. Rung 34 (`armed-storage-7341f5f6`, switch value 33) remains **ARMED
AND NOT PRESSED**, and **the press is the operator's**.
