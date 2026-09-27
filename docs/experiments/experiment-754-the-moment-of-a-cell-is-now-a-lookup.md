# 754: the moment of a cell is now a lookup — the tool that makes m732 and m763 mechanical, and the four defects it had while being written

**Host-side only. No device action of any kind** — no `fastboot`, no `adb` to the device, no press, no
gate, no runner, no payload build, and `out/` untouched. One file changed in `src/` is **read**, never
written; the new file is `tools/read_storage_key_order.py`, and `Makefile` gains one line.

**The one-line finding.** The record's most-repeated defect class has two instances that are the *same
question asked of the same file* — **m732** (723 §3 read the rung-3/rung-4 `POWER_CONTROL` cells for a
moment that belongs to rung 7) and **m763** (748 §4 read `_reg_pwrctl_status_after` for a moment that
belongs to the power byte, while the key is published by the **reset** stage, which the probe calls
first). Both were found **by hand, one press apart**, and both are answerable from
`entry_storage.c`'s own call order without touching the device. **`tools/read_storage_key_order.py`
answers them**: for any `xnu_live_storage_*` key it prints the function that publishes it, the chain of
calls from `entry_storage_probe` that reaches that function, and the preprocessor guard on every hop —
and `--after` **refuses** a "read after X" claim the call order does not support. Its `--selftest`
re-derives both defects from the source alone and now runs as a clause of `make check`.

## 0. Why a tool and not another careful reading

Two experiments in a row ended with the same sentence: *a cell name describes a register; a cell is that
register at a time.* 749 then made its correction mechanical (751's `read_storage_commands.py` reads a
**capture**), and 753 did not — it wrote the correct moment into prose and left the next reader to
re-derive it. **Prose is what m732 already was.** The source states the order, in one place, exactly
once; a reader who has to hold it in their head is a reader who will get it wrong in the same direction
again.

## 1. What it reads, and what it prints

Nothing is compiled and nothing is executed from the payload. The tool parses three things out of
`src/entry/entry_storage.c`:

1. **the probe's own stage sequence** — the ordered `st_*()` calls in `entry_storage_probe`, each with
   the `#if` guard it sits under;
2. **the call graph** among the functions that file defines — so a key published three calls deep gets a
   path, not just a function name;
3. **every `ST_LIVE` that publishes a key**, literal or macro-generated.

    $ tools/read_storage_key_order.py
    # rung 20 (the image in out/ was built at this value; domain ... PROBE < 0 || ... PROBE > 20)
    the probe's stage calls, in source order (rung 20):
      :4050  st_standard_census       runs          STAGE90_XNU_STORAGE_PROBE >= 3
      :4063  st_driver_reset          runs          STAGE90_XNU_STORAGE_PROBE >= 4
      :4076  st_clock_census          runs          STAGE90_XNU_STORAGE_PROBE >= 5
      :4089  st_clock_set             runs          STAGE90_XNU_STORAGE_PROBE >= 6
      :4100  st_pwr_irq_arm           runs          STAGE90_XNU_STORAGE_PROBE >= 8
      :4113  st_power_set             runs          STAGE90_XNU_STORAGE_PROBE >= 7
      :4127  st_pwr_wait              runs          STAGE90_XNU_STORAGE_PROBE >= 9
      :4143  st_quiet_enable_probe    skipped       STAGE90_XNU_STORAGE_PROBE == 15
      :4162  st_resp_before           runs          STAGE90_XNU_STORAGE_PROBE >= 17
      :4176  st_cmd_path              runs          STAGE90_XNU_STORAGE_PROBE >= 11
      :4186  st_pwr_irq_after         runs          STAGE90_XNU_STORAGE_PROBE >= 8

    $ tools/read_storage_key_order.py --key _reg_pwrctl_status_after --key _pwr_status_after
      xnu_live_storage_reg_pwrctl_status_after: POINT
          entry_storage_probe[3855] -> st_driver_reset[4063] -> emit@784
      xnu_live_storage_pwr_status_after: POINT
          entry_storage_probe[3855] -> st_power_set[4113] -> emit@1388

**Those two lines are 753 in one screen**: the key 748 quoted is published at `4063`, and the key read
either side of the byte is published at `4113`, fourteen stages later. Neither number was in any
document.

**The rung is not restated, it is read.** The default is the value the image in `out/` was built at,
from `out/stage90/xnu_arm_entry-config.txt`; a `--rung` outside the ladder's declared domain is
**refused**, with the domain parsed out of the file's own `#error` guard — m735's repair, applied here.
And because the record has *two spellings* of rung, the out-of-domain refusal says which is which:

    $ tools/read_storage_key_order.py --rung 21
    REFUSED: --rung 21 is outside the ladder's declared domain
      (STAGE90_XNU_STORAGE_PROBE < 0 || STAGE90_XNU_STORAGE_PROBE > 20) ...
      NOTE: the record counts ORDINAL rungs - `20` is the 20th arm and the ladder's "rung 21" IS
      value 20. The value is the spelling the guards and every capture cell use.

**The rung is a name and the arm is a value (717), refused by name rather than answered wrongly.**

## 2. The validation: seven cases, both landed defects, and one deliberate refusal

`--selftest` re-derives the record's findings from the call order. It is not a smoke test — every case
is a claim some experiment already landed, so a failure means the tool contradicts the record:

    ok    _pwr_status_after                AFTER      m763: the power-status pair is read either side of the byte
    ok    _reg_pwrctl_status_after         BEFORE     m763: the key 748 quoted for "after the power byte" is
                                                      published by the RESET stage, first
    ok    _reg_power_control_after         BEFORE     m732: the rung-4 cell is read before rung 7 writes the byte
    ok    _reg_power_control               BEFORE     m732: as above for the rung-3 twin
    ok    _cmd1_resp                       AFTER      generated keys: the CMD0 census is published before CMD1's
    ok    _cmd1_inhibit_seen               AFTER      generated keys: the 12-key macro follows the 20-key one
    ok    _pwr_irq_at                      UNORDERED  a handler cell is an interval, so no point order exists

**The last case is the one that matters as much as the first four.** `st_pwr_irq` is not called by
anything; it is *registered* by `st_pwr_irq_arm` and entered when the line fires. The tool calls that an
**INTERVAL** and refuses to order it against a point — which is exactly the boundary 753 §5 had to state
in prose and 712 had to reason about: *the source cannot say when the line fires.*

## 3. The four defects the tool had while it was being written — all of them measured

**The tool exists to catch one defect class, and it produced four instances of that class before it was
correct. Each one is recorded here with the output it produced, because a reference tool that can lie
plausibly has to say how it lied.**

**(a) A macro-generated key truncated to half a name.** The first version's key pattern was
`ST_LIVE\(\s*"([^"]+)"`. The file publishes most of the ladder's command cells through a macro:

    entry_storage.c:3418   ST_LIVE("xnu_live_storage_cmd" tag "_ps_before", (r).ps_before);

so the pattern matched `"xnu_live_storage_cmd"` and **invented a key that is not a key** — half of a
name, reported as published at the macro's own line. It is the `m720`/`m696` shape in its purest form:
a name that occurs in the source and does not denote a cell. *Fixed by parsing the concatenation, not
the first literal.* (463 keys reported → 462 after the phantom was removed.)

**(b) Every function named `__attribute__`.** Fifteen definitions in this file put the return type and
the attributes on the lines above the name:

    entry_storage.c:2215   static __attribute__((noinline, noclone)) void
    entry_storage.c:2216   st_send_command(uint32_t opcode, uint32_t flags, uint32_t *out_raw)

The signature matcher is anchored at the end, and the leftmost thing that looks like `name(...)` in
`__attribute__((noinline, noclone)) void st_send_command(...)` is `__attribute__`. **Measured: 14 of the file's 31 definitions — including
`st_send_command`, `st_pwr_wait`, `st_resp_before` and `st_cmd_path` — were parsed under a name that
appears nowhere in the call graph**, so every edge into them vanished, and the probe's stage table showed
**seven of its eleven stages**: `st_pwr_irq_arm`, `st_pwr_wait`, `st_quiet_enable_probe`, `st_resp_before`
and `st_cmd_path` were all missing from it (`st_mode_sequence` and the rest survived only because they
are declared on one line). *Fixed by stripping `__attribute__((...))` before naming.* It is `m731`'s
shape arriving in the tool: one name, and a body that is not the one the name denotes.

**(c) Zero functions parsed, and a table that still printed.** The first draft cleared its
signature-accumulator on every blank depth-0 line as well as every statement end, so no signature ever
survived to its `{`. The output was `0 function(s) parsed, 463 distinct key(s) published.` — a
**plausible-looking header with an empty table under it**. It was caught because the tool prints the
count rather than the table alone, which is `mi4-silence-is-a-reading-only-if-success-is-silent` in its
constructive form: *a check that succeeds by printing nothing cannot be told from one that never ran —
so print the count, and make zero visible.*

**(d) The arguments read out of the copy whose string bodies are blanked.** The parser keeps two texts:
`raw` and a `clean` copy with every comment and string body blanked, so that a brace or a paren inside a
key cannot move the brace counter. The first version took a macro invocation's **extent** from `clean`
(correct) and its **argument text** from `clean` too (wrong), and a blanked string literal is still
delimiters around whitespace:

    ST_CMD_PUBLISH("0", c0);   at :3637        ST_CMD_PUBLISH("1", c1);   at :3702
    -> arguments read as  `" "` , c0            -> arguments read as  `" "` , c1
    -> both produce  xnu_live_storage_cmd _ps_before   (a space where the tag belongs)

**Measured: all 27 generated keys collapse, every one of them produced by two invocations** — the CMD0
census and the CMD1 census merge into one family of 27 keys, `489` keys are published instead of `516`,
and every `_cmd1_*` lookup then **refuses**, because no key of that name exists. *One key, two
producers, one name* — **m760's exact class, arriving in the tool that exists to catch it.** And the
failure is silent in the worst way: the tool would have reported *fewer* keys and refused the lookups,
which reads as *the ladder does not have those cells* rather than *my parser merged them*.

**What the four have in common is the argument for the self-test.** None of them raised an exception,
none produced an obviously wrong number, and two of them (`a`, `d`) printed a table that was internally
consistent. The self-test in §2 is what would have caught `b` and `c` immediately and `a`/`d` at the
first `--key _cmd1_...`.

## 4. The audit: 516 keys, and the 83 that are not in the rung-20 image at all

Over the whole file at rung 20 (the value the armed arm carries):

| | count | what it means |
| --- | --- | --- |
| keys published | **516** | 462 literals + 54 from two macros (`ST_CMD_PUBLISH` 20 keys, `ST_CMD_PUBLISH_12` 7, each invoked with tags `"0"` and `"1"`) |
| functions | **31** | every definition in the file, named correctly |
| **POINT** | **418** | a moment the call order fixes |
| **INTERVAL** | **15** | all of them in `st_pwr_irq` — published from an interrupt client, so **not point-orderable** |
| **DEAD at rung 20** | **83** | the `#if` guard is false, so the rung-20 image has no such key |
| UNKNOWN | **0** | no key on a path with a guard outside the rung vocabulary |

**The 83 are the interesting column**, and they are not one family:

    rca      38 key(s)   STAGE90_XNU_STORAGE_PROBE >= 11 && == 18
    nrsp     35 key(s)   STAGE90_XNU_STORAGE_PROBE >= 11 && == 19
    quiet    10 key(s)   STAGE90_XNU_STORAGE_PROBE >= 11 && == 15

**These are rungs 19, 20 and 16 — the three ordinal arms whose bodies were replaced rather than added
as the ladder climbed.** A reader holding a rung-20 capture and a sentence quoting `_nrsp_complete` is
quoting a cell that **cannot exist in that image**, and this is the same question m720 asked (*an absent
key has three producers: the branch did not run, the channel refused, the cap dropped it*) with a fourth
producer made mechanical: **the arm has no such key.** The tool answers it in one command instead of a
search of the `#if` guards.

**And it is live for the press the ladder is waiting on**: `_nidx_*` — the family rung 21's reading is
built from — resolves to guard `>= 20`, so it is live at the armed rung and dead at rung 20's
predecessors, which is the whole point of the arm.

## 5. What it does not do

- **It orders *publications*, not *events*.** A POINT cell can still be *about* an interval, and the
  tool cannot see that. The record already has the right way to pin an interval: **find a point that
  samples it.** 712's `_pwr_irq_calls_probe_end` is published by `st_pwr_irq_after` — a point at stage
  11 — and it reads the counter the handler maintains, which is why `_pwr_irq_calls = 0x1` beside
  `_pwr_irq_calls_probe_end = 0x0` is a real statement about *when the client ran*. The tool calls
  `_pwr_irq_calls_probe_end` a POINT and `_pwr_irq_calls` an INTERVAL, and **refuses to order them** —
  correctly, because the source does not know when the line fires and the archive does. **Source order
  narrows the question; it does not answer it.**
- **It does not check that a key's *value* means what a sentence says it means.** It knows only when a
  key is written. That is the whole of its claim.
- **It does not read captures.** `read_storage_commands.py` (751) does that. The two are complements:
  this one says where a cell sits in the image, that one says what the cell held.
- **It does not parse `#elif` exactly.** The file has none, and the tool reports UNKNOWN rather than
  guessing if one appears.
- **It does not arm, build, move, or press anything.** No press is owed, no firer is armed, `out/` is
  untouched, and **no press may be spent without the operator's authorization.** Rung 21 remains
  **ARMED AND NOT PRESSED**.

## 6. What it changes

1. **`make check` gains a fifth clause**, and it is the tool's own `--selftest`. It is a check rather
   than a fourth tool because **the tool was wrong four times while it was written**, twice with a
   plausible table and no exception. A reference tool that can lie like that has to carry a self-test,
   and the self-test has to run.
2. **The ask it publishes**, to be asked of every cell a document quotes for a moment: **which KEY,
   emitted by WHICH function, under WHICH rung guard — and does the probe call that function before or
   after the event?** `--after` is that question as a refusal.
3. **m732 and m763 stop being prose.** 749 turned its correction into a check on a *capture* (751);
   this turns both moments-defects into a check on the *source*, which is where the order lives.

## 7. What this document does not say

- **It does not say the record has more moments-defects.** It re-derives the two that are landed and
  gives the means to check the next sentence; **it claims no sweep of the record's prose**, which was
  not performed.
- **It does not change any reading.** 753's numbers stand and are the self-test's first four cases.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It makes the *reading* of the
  next press less able to be wrong, and that is the whole of what it claims.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist; 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
