# 749: `inhibit_seen = 0` has TWO producers, and the ladder has been reading it as one

**Read from the archived captures and the committed documents. No device action of any kind** — no
`fastboot`, no `adb` to the device, no press, no gate, no runner, **no build**, and `out/` untouched, so
the arm parked there (`armed-storage-46fe6737`, rung 21) is exactly as it was left.

**The one-line finding.** 746 section 4b and 747 section 7 read `inhibit_seen = 0` as **the block declining
the command**, and 747 section 7 makes that the first branch of rung 21's threeway — *the rule is
CONFIRMED*. **The archive holds two commands with `inhibit_seen = 0`, and their own logs say they are not
the same outcome.** `xnu_live_storage_cmd1_inhibit_last` reads `0x00f80000` and
`xnu_live_storage_cid_inhibit_last` reads `0x01f80000` — **one bit apart, and that bit is bit 24, the CMD
line's own signal level** — in **five independent boots**, and it is the **only** cell in any capture that
carries `0x00f80000`. Beside it, the same two windows differ in the `RESPONSE` register: CMD1's gained
`0x40ff8080` where the register had read `0x00000000` after CMD0, and CMD2's gained nothing at all.

**So `inhibit_seen = 0` is a key with two producers and one name.** Rung 21's first branch, as written,
would confirm a rule on a reading this ladder's own archive shows a command of a *different* kind also
produces. **Everything needed to separate them is already published by the arm that is already parked**,
so this changes the *reading* of the next press and requires no rebuild.

## 0. Why this was worth reading out of the archive rather than spending a press on

The arm parked in `out/` is rung 21: `STAGE90_XNU_STORAGE_PROBE=20`, the driver's own CMD3 with its
response demand back and its `INDEX` bit out, word `0x030A`. Its pre-registration is 747, and 747
section 7's branch table is what turns its log into a verdict. **A payload does not rebuild (408)** and a
press is capped at 28 s, so a branch whose stated condition can be satisfied by two different outcomes is
the most expensive kind of defect this project has: it spends the one resource that does not come back on
a verdict that the readings do not support. This document reads the condition against the archive.

## 1. Two committed documents, adjacent rungs, in direct contradiction

**740 (the rung-18 press) says the card answered CMD1, and says it in those words:**

> so a command **moved** this register: `0 -> 0 -> 0x40ff8080`. **733's reading of `0x40ff8080` as CMD1's
> response survives, and 733 section's "the card answered CMD1" is back on its feet.**
>
> This is exactly the reading 738 section 4 named as the cheap thing that should be done before the ladder
> builds anything else on top: *"a read of `RESPONSE` on a block with no command in its past would settle
> it"*. It settles it, and it settles it **for** the ladder.

733's own words, one press earlier:

> **CMD1 went out for the first time in this line's life, and the CARD ANSWERED.** `_cmd1_sent = 1`,
> `_cmd1_rsp_present = 1`, and `_cmd1_resp = 0x40ff8080` is **exactly the reference word the
> pre-registration named** — bit 30 set (ready) with the voltage window `0x00ff8000` in bits 23:15.

**And 746 section 4b (the rung-20 press's own correction, commit `340a888`) writes CMD1's row as
"NEVER STARTED":**

| command | word | flags | `inhibit_after` | `inhibit_seen` | outcome |
| --- | --- | --- | --- | --- | --- |
| CMD1 `SEND_OP_COND` | `0x0102` | `0x02` | **0** | **0** of 5,088,256 | **NEVER STARTED** |
| CMD2 `ALL_SEND_CID` | `0x0209` | `0x09` | (cell absent) | **0** of 5,090,304 (778) | **NEVER STARTED** |

and names the reading that produces it:

> `inhibit_seen = 0` is not "no inhibit was observed" — it is **the block declined the command**

**Neither document cites the other.** 746 section 4 — the section immediately before 4b, in the same
document — still calls `0x40ff8080` "CMD1's `0x40ff8080` still sitting in word 0", i.e. treats it as
CMD1's. **The two readings cannot both be true of CMD1**: a block that declines a command does not thereby
cause the card to answer it. 747 section 7 then inherited 746b's side, because 746b is the document that
sets the next subject.

**This is `[[mi4-one-value-two-definitions]]`, and it is the same offence 746b was written to repair.**
746b's own headline is *"the ladder has been calling two different outcomes by one name for three
presses"*. **It merged CMD1 and CMD2 into one name while doing so.**

## 2. The discriminator the ladder has never read: `PRESENT_STATE` bit 24

`inhibit_last` is published as **the whole `PRESENT_STATE 0x24` word** (`entry_storage.c`, the sampler
inside `st_send_command`'s poll: `r->inhibit_last = st_read32(... + ST_SDHCI_PRESENT_STATE)`), and
746's table decodes only **bit 0** of it. Bit 24 is the CMD line's signal level — the SDHCI spec's
*CMD Line Signal Level*, and this project's own decode: 748 section 3 reads the same word's `bit 24 CMD
line`.

**Every command the ladder has driven, in every capture that carries the cell:**

| command | capture(s) with the cell | `inhibit_last` | bit 24 (CMD line) |
| --- | --- | --- | --- |
| CMD0 `GO_IDLE_STATE` `0x0000` | rung 13, 14, 15, 17, 18, 19, 20 | `0x01f80000` | **high** |
| **CMD1 `SEND_OP_COND` `0x0102`** | **rung 15, 17, 18, 19, 20** | **`0x00f80000`** | **LOW** |
| CMD2 `ALL_SEND_CID` `0x0209` | rung 17, 18, 19, 20 | `0x01f80000` | **high** |
| CMD3 rung 19 `0x031a` (`_rca_`) | rung 19 | `0x01f80001` | high |
| CMD3 rung 20 `0x0300` (`_nrsp_`) | rung 20 | `0x01f80000` | high |

**And `0x00f80000` occurs in exactly one cell in each of those five logs** — `grep -oE
'xnu_live_[a-z0-9_]+=0x00f80000'` returns that one key and nothing else. It is not a value the block
reports for anything else at any other moment.

**And CMD1's own window brackets it**: `_cmd1_ps_before = 0x01f80000` and `_cmd1_ps_after =
0x01f80000`, both with bit 24 **set**, in all five. So the line was high before the store, is low at the
last sampled moment inside the poll, and is high again at the end of the 1.2 s bound.

## 3. The like-for-like pair, which is the whole argument

**CMD0's and CMD3's rows cannot be compared with CMD1's, and saying so is part of the finding.** Their
polls exited on `INT_STATUS`, so their "last sample" is the moment of their **completion** — a different
moment in the transmission. Using them as evidence that "the line reads high while a command is in
flight" would be reading a cell that names a *register* for a *time* (m732's shape, which 748 section 5
already had to use this same archive to fix once).

**CMD1 and CMD2 are the like-for-like pair, and they were built to be.** Both are `inhibit_seen = 0`
rows. Neither ever latched `INT_STATUS` (`_cmd1_status_any = _cid_status_any = 0`), so neither poll exited
early: both ran the full 1.2 s bound, and both therefore took the **same** 1024-sample window, at the
same position (~473 ns per poll, so ~484 us after their own store), with the same sampler and the same
`polls <= ST_CMD_INHIBIT_SAMPLES` condition.

| | CMD1 `0x0102` | CMD2 `0x0209` |
| --- | --- | --- |
| `inhibit_seen` of the first 1024 samples | **0** | **0** |
| `polls` (full bound) | `0x004da400`/`0x004da800`/`0x004db000` | `0x004dac00` |
| `inhibit_last` | `0x00f80000` | `0x01f80000` |
| **bit 24, the CMD line** | **LOW** | **high** |
| `ps_before` / `ps_after` | `0x01f80000` / `0x01f80000` | `0x01f80000` / `0x01f80000` |
| `RESPONSE` in this window | **`0x00000000` -> `0x40ff8080`** | **`0x40ff8080` -> `0x40ff8080`** (unmoved) |

**Same window, same sampler, same `inhibit_seen` — one bit and one register apart.** CMD1's window has a
bus event in it (the line moved, and the response register gained a well-formed 32-bit word) and CMD2's
has none. **That is not one outcome with one name.**

**What is measured here, and what is inference — said plainly, because the two are what this document is
about.**

- **Measured**: the five `inhibit_last` values, the five `ps_before`/`ps_after` pairs, the `RESPONSE`
  readings `0x00000000` (after CMD0) and `0x40ff8080` (after CMD1) with their `_resp_read = 1` flags, and
  `_cid_resp0` bit-for-bit equal to `_cmd1_resp` with all four `_cid_raw*` words published beside them.
- **Measured**: that `0x00f80000` occurs in one cell of each capture and no other.
- **Inference**: that CMD1 is therefore *taken and answered* rather than *declined*. **This document does
  not settle that**, and deliberately: the honest statement is that the archive has **two** candidate
  readings of CMD1 — 733/740's *the card answered* and 746b's *declined* — **and that they disagree**, so
  neither can be used as a premise until a reading separates them.
- **A limit that must be stated**, since it is the one that would make this document the defect it names:
  `inhibit_last` is **one sample per command**, so it cannot show a line that toggles. CMD1's sample sits
  at ~484 us and every other command's sits at its own end. **The only comparison this document rests on
  is CMD1 against CMD2**, for the window-length reason in the table's preamble. The other three rows are
  context and are labelled as such.

## 4. What this does to rung 21, and it does it before the press is spent

**747 section 7's threeway, as written:**

| 747's stated condition | 747's verdict |
| --- | --- |
| `_nidx_inhibit_seen = 0` with `_nidx_word_read = 0x030A` | the block **declined** the command. **The rule is CONFIRMED** |
| `_nidx_inhibit_seen > 0` with `_nidx_complete = 0` | taken and never finished; the rule's `INDEX` half is **KILLED** |

**The first row's condition is satisfied by both of section 3's rows, and its verdict is true of only one
of them.** If rung 21 returns the CMD1 shape, 747 would print *the rule is CONFIRMED*, and the reading
would in fact be *this arm reproduces the ladder's other unexplained outcome* — a different finding, and
the more interesting one.

**The refinement, and it costs nothing: the arm already publishes every cell it needs.** Rung 21's body
(`st_cmd3_noidx`) was written to publish, at the two moments it owns, the four raw `RESPONSE` words
(`_nidx_raw_pre0..3`, `_nidx_raw_post0..3`), the one word its three cells share (`_nidx_resp_pre`,
`_nidx_resp_post`), `_nidx_resp_moved`, `_nidx_resp_is_arg`, and — through `c3.inhibit_last` —
`_nidx_inhibit_last`, the whole `PRESENT_STATE` word, **bit 24 included**. So:

- **branch 1 ("declined", the rule confirmed)** should require **`_nidx_inhibit_seen = 0` AND
  `_nidx_resp_moved = 0` AND bit 24 of `_nidx_inhibit_last` SET AND bit 24 of `_nidx_ps_after` SET** —
  i.e. the CMD2 shape: the word was latched, the line never moved, and the register never changed.
- **if instead bit 24 is CLEAR, or `_nidx_resp_moved = 1`,** the run is the **CMD1 shape**, and the
  correct reading is *the block took the word, the bus moved, and no completion latched* — which is
  730's `INT_ENABLE`-mask subject and not the `INDEX` bit at all.
- **and the pair must be read against `_nidx_polls`**, because the sampler's last sample is the
  completion's moment on an arm that completes: a row that completed early is not comparable to a row
  that ran the bound, which is exactly section 3's preamble.

**No rebuild is needed, and that is not a convenience — it is what keeps the parked arm valid.** The arm
in `out/` is the one this press sends, and 747's own record ties its identity to the entry image's hash.
**This document therefore changes nothing in `src/`, `scripts/`, or `out/`; it changes the table a
reader applies to a log that does not exist yet.** 747 is a pre-registration and a historical document;
per the project's standing rule it is **superseded rather than edited**, and this section is the
supersession.

## 5. A reason to expect the rule to be refuted, offered as a prediction and not as a finding

**This is spec knowledge and not a file in this tree, and it is labelled that way on purpose** — the
project's rule is to read the vendor's file rather than the memory of the upstream one, and this
paragraph has no vendor file under it, so it is a prediction with a falsifier rather than evidence.

In the SDHCI command register, the two bits rung 21's rule turns on are the **receive-side** ones:

    SDHCI_CMD_CRC    0x08    -> the spec's "Command CRC Check Enable"   (vendor sdhci.h:47)
    SDHCI_CMD_INDEX  0x10    -> the spec's "Command Index Check Enable" (vendor sdhci.h:48)

and the vendor's own decode (`sdhci.c:1131-1143`) ORs them in **only after the response type is chosen**,
as `|=` lines with no branch around the command's issue. **Neither bit can prevent a command from being
sent**: they tell the block to *check* the response it receives — bit 4 compares the response's index
field against the command index and reports a Command Index Error if they differ, and bit 3 checks the
response CRC. **Nothing in that mechanism makes `0x030A` unsendable while `0x031A` is sendable.**

So the prediction, registered here so that it can be wrong in public: **if rung 21 comes back
CMD2-shaped, the observation is real but its explanation is not the `INDEX` bit** — an implementation may
do anything, and the ladder would then be measuring an undocumented refusal — **and the reading that would
make a refusal credible is `_nidx_resp_moved = 0` with the `CMD` line never leaving high and
`_nidx_status_any = 0`**, which is the CMD2 shape and not the CMD1 one. **The falsifier of this
paragraph is a CMD2-shaped rung 21 with a mechanism in the vendor's tree**; the ladder has not looked for
one, and this document does not claim there is none.

## 6. What this document does not say

- **It does not say CMD1 was answered.** Section 3's inference box is explicit: this document says the
  archive's two readings *disagree*, and that the disagreement is what makes 747 section 7's first branch
  unsafe. It takes no side, and it names the cell that would take one.
- **It does not say 746b is wrong about CMD2.** CMD2's row is the one with no bus event, and 746b's
  reading of *that* row is untouched. What is wrong is the **merge**.
- **It does not say the `INDEX` rule is refuted.** Section 5 is a prediction from the specification, and
  the specification is not this block. The rule remains the only explanation of the five rows the ladder
  has, and rung 21 remains the right experiment.
- **It does not move the arm, rebuild anything, or authorize a press.** `out/` is untouched,
  `armed-storage-46fe6737` is **ARMED AND NOT PRESSED**, no firer is armed, and **no press may be spent
  without the operator's authorization.**
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It removes a way for the next
  press's reading to be wrong, and that is the whole of what it claims.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist; 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
