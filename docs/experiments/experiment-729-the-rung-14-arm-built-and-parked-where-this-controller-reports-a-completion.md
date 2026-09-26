# 729: the rung-14 arm built and parked — where this controller reports a completion, and two false sentences in the two documents before it

`armed-storage-completion-6f49e880` (`STAGE90_XNU_STORAGE_PROBE=13`) is built, parked, recorded and
**ARMED — no press has been taken from these bytes**. Host-side except for two builds; the device was
not touched, nothing was flashed, and **no firer is armed**. The pre-registration is
[728](experiment-728-the-rung-14-pre-registration-where-this-controller-reports-a-completion.md) and the
question is [726](experiment-726-the-rung-13-press-the-command-ran-and-the-status-register-said-nothing.md)
section 3's.

```
entry image   xnu_arm_entry.bin  sha256 6f49e880a4670a98…   5552764 bytes   STAGE90_XNU_STORAGE_PROBE=13
payload       stage90-qcdt.img   sha256 d53312233074191e…   8572928 bytes   (this is what fastboot boot sends)
park          out/stage90/frozen/armed-storage-completion-6f49e880   (11 files)
record        stages/stage90/revert-set.txt — set=armed-storage-completion-6f49e880
readiness     tools/verify_press_ready.sh -> 5 of 5, exit 0
rehearsal     tools/rehearse_revert_set.sh -> 38 ok, 0 failed
```

## 1. What the rung does, and where it stands

Rung 13 refuted all five of 724 section 2's hypotheses and left a sixth the first five did not contain:
**this controller runs a command to completion and does not set its interrupt-status register.** The
command reached the CMD line (`_cmd0_inhibit_after = 1`, `_cmd0_inhibit_seen = 0x21b`), the inhibit
released when it finished (`_cmd0_inhibit_last` bit 0 clear), and `_cmd0_status_any` was `0x00000000`
over 5,088,256 reads taken *across* that window. The SDHCI normal-interrupt path is
`INT_STATUS → INT_ENABLE → SIGNAL_ENABLE → the line`, so a block built as *the status bit latches only if
its enable is set* produces exactly that log.

The arm is one new body, `st_int_report` (`noinline`, `noclone`), called **immediately after CMD0's
publishes and before the gate between the commands** — 728 section 1's constraint, which rung 13's own
log supplies: `_cmd_gated = 1` says the gate fired and `st_cmd_path` returned before CMD1, so a block
placed after the commands would sit on a path this machine has never taken. The call is unconditional; a
reader qualifies `_int_status_after` against `_cmd0_sent` in the same log rather than trusting an `if`.

| cell | reads | the two answers it separates |
|---|---|---|
| `_int_calls` | — | 1 ⇒ the body ran |
| `_int_slot_status` | `hc_mem+0xFC` (halfword) | non-zero ⇒ the completion was in the standard file's *other* status register all along |
| `_int_pwrctl_status` | `core_mem+0xDC` | non-zero ⇒ the vendor path answered for this command too |
| `_int_cmd_word` | `hc_mem+0x0E` (halfword) | `0x0000` again ⇒ the block still holds the driver's word |
| `_int_present`, `_int_present_inhibit` | `hc_mem+0x24` | `CMD_INHIBIT` clear ⇒ nothing is in flight |
| `_int_status_before` | `hc_mem+0x30` | the baseline, taken with the enables **clear** |
| `_int_enable_before`, `_int_sig_enable` | `hc_mem+0x34`, `0x38` | both 0 ⇒ the gate this rung writes through was closed |
| `_int_enable_wrote` | — | `0x00000001`, the `RESPONSE` bit |
| **`_int_status_after`** | `hc_mem+0x30` | **the rung's answer**, taken with the enable set one instruction earlier |
| `_int_enable_held` | `hc_mem+0x34` | the block's own copy of the enable — *the store took* |
| `_int_enable_restored`, `_int_enable_readback` | `hc_mem+0x34` | `0` and `0` ⇒ the arm put the gate back |

**The write set is two stores, both to `INT_ENABLE 0x34`, the second of them the restore.**
`SIGNAL_ENABLE 0x38` is read and **never written** — which is the half of the pair this rung does not
touch, and the reason the hypothesis is testable without putting the line in play. No new command, no
`ARGUMENT`/`COMMAND`/`RESPONSE` write, no data-path register, no `POWER_CONTROL` (0x29 is not touched by
this body at all), no GCC word, no `core_mem` write and no byte of the medium. The pre-registered failure
mode stays a diagnosis: if the block does not AND the two enables the line rises and the run ends at the
dispatcher as `_irq_other_count = 1` / `_irq_other_iar = 155` — an ending 709's press already read and
survived on intid 170, bounded by the payload's own armed watchdog.

## 2. The build: three invocations, two refusals, and 728 section 5's prediction refuted

**(a) The switch guard is the second place the ladder lives, and 725 recorded the same pair moving.**
`build_entry.sh:667` refused `13` before the compiler saw it: `STAGE90_XNU_STORAGE_PROBE must be 0, 1,
2, … or 12`. That `case` list and `entry_storage.c`'s `#error` are two files' statements of one ladder,
and both move in the step that raises the rung.

**(b) The new clause's program-order comparison refused, and both strings printed identically.**
`classify_body` prints its first line with a trailing space after each entry; `read -r` strips trailing
IFS whitespace. So `$stb_int_dev` carries no trailing space while the `tr`-built `$stb_int_set` does. On
that build the sorted set, the counts **and** the store list all agreed — the only difference was a byte
no message shows. The comparison now spells `${stb_int_dev% }` and says why, rather than relying on the
difference being invisible.

**(c) THE SEAM CONSTANT DID NOT MOVE, AND 728 SECTION 5 SAID IT WOULD.** 728 section 5 opens *"The seam
constant will move a fourth time. Every rung since 696 has pushed the entry group past the next page
boundary"* and names 725's `0x800492dc` as its base. It did not move:

| | pressed rung-13 arm | this arm |
|---|---|---|
| `xnu_arm_entry.bin` bytes | 5552764 | **5552764** |
| `platform_cache_idle_enter` | `0x80049238` | **`0x80049238`** |
| the exit's `bl FlushPoU_Dcache` | `0x800492d8` | **`0x800492d8`** |

**The entry bin is byte-identical in *size***, and the exit call is at the same address: the rung's
~1.1 KB landed inside the entry group's alignment slack. What moved is everything from the new body's own
address up to `arm_init` — `st_int_report` now occupies `0x8000d978`, where `st_cmd_path` used to be, and
`st_cmd_path` moved to `0x8000daa0`, `entry_storage_probe` to `0x8000e0e0`, `arm_init` to `0x8000f82c` —
and nothing above it. So both copies of the constant stay where they are, and **a prediction that a
constant must move is a prediction about the linker's padding, which no clause asserts and no arithmetic
in this project sees.** 725's prose is left where it is: it is about 724's arm, and the value it names is
this arm's value too.

## 3. What the build now asserts about the new body — and it is more than the two rungs below assert

`st_int_report` is emitted **before** `entry_storage_probe` (`nm` puts it at `0x8000d978`), and the
width-vs-offset census reads the window `[sym_addr entry_storage_probe, next_global)` — which starts *at*
the probe. So **this body's widths are checked nowhere unless this rung's own clause checks them**, and
the clause is written to: it asserts the exact `<address>:<mnemonic>` set, where `f98249fc:ldrh` and
`f982490e:ldrh` are the two offsets no 32-bit access may reach (`0x0E` is not 4-aligned; `sdhci.h:239`
declares `0xFC` 16-bit). Measured:

```
program order, distinct   f98249fc:ldrh f98240dc:ldr f982490e:ldrh f9824924:ldr f9824930:ldr
                          f9824934:ldr f9824938:ldr f9824934:str
counts                    f9824930:ldr=2  f9824934:ldr=3  f9824934:str=2  f982490e:ldrh=1
                          f9824924:ldr=1  f9824938:ldr=1  f98240dc:ldr=1  f98249fc:ldrh=1
stores, program order     f9824934:str          image side   EMPTY
calls from st_cmd_path    1
```

**The counts are asserted and not echoed, which is new for this ladder.** Rungs 11 and 12 echo theirs.
The sorted set cannot see a *dropped* read — `classify_body` prints the distinct accesses in
first-appearance order — and the two multiplicities this rung's experiment *is* are `INT_STATUS` read
exactly twice (the baseline with the enables clear, and the answer with the `RESPONSE` bit set one
instruction earlier) and `INT_ENABLE` read exactly three times and written exactly twice. The comparison
sorts first because `classify_body` prints counts by iterating an awk hash — 706's reason: an ordered
comparison of an unordered extractor would be a reading that could change under an unrelated edit.

The interleaving is **not** asserted, and deliberately: every access here goes through a `volatile`
lvalue and the writer adds a `dsb sy`, so *the status read is after the store and before the restore* is
C's ordering rule for volatile accesses and not a property of GCC's block layout. What a compiler can do
is drop an access or add one, and that is what the set, the counts and the store list refuse.

## 4. Two sentences in the two documents before this one are false, and the measurement is what refuted them

726 section 3 and 728 section 2 both write that **`SLOT_INT_STATUS 0xFC` is a register this ladder has
never read at all**. It is read: rung 3's `st_standard_census` reads it as a halfword
(`entry_storage.c:642`) and **726's own log carries the value** —
`xnu_live_storage_reg_slot_int_status=0x00000000`. `CORE_PWRCTL_STATUS 0xDC` is in the same position: rung
2's mode sequence reads it, rung 8's handler reads it, and 726's log carries `_pwr_irq_status32=0x02`.

The true statement is **stronger**, and it is the one this rung needed: *neither has ever been read at a
moment when there was a completion to report*. And because rung 3's and rung 8's readings are in the same
log as the new cells, the pair is a comparison and not a claim. This is the shape of
[[mi4-one-value-two-definitions]] **m732**, one reader over: both sentences named a register but were
really about a **time**, and a register that has been read once, at the start of the boot, is not a
register that has been read when the question is asked.

**And 728 section 5's second prediction named the wrong clause.** It says the first build will be refused
by *"`st_cmd_path`'s clause, which asserts no store at all"*. It was not: `st_cmd_path`'s own device set
and its no-store clause are about **`st_cmd_path`**, and the new store lives in a body of its own, which
is exactly the design 728 section 5's own prose then describes. `st_cmd_path`'s clauses are rung 11's,
unchanged, and its device set is still `[f9824924:ldr f9824934:ldr f9824938:ldr]` with no store. The
refusal that actually fired was the trailing-space artifact in section 2(b). One document, two
predictions, both wrong, and **they were wrong in the same direction**: 728 reasoned about the rung from
the record's own sentences rather than from `nm` and the disassembler, which is the class
[[a claim in a comment is not a check]] names.

## 5. What was verified

All four, in this order, on the live tree:

* `tools/verify_revert_set.sh out/stage90/frozen/armed-storage-completion-6f49e880
  --set=armed-storage-completion-6f49e880` → **VERIFIED, 11 files**, plus the 6 manifest-member checks.
  The park was made by plain `cp -p` of the eleven files; **no `sha256sum -c`** anywhere, because the
  parked `SHA256SUMS.txt` carries absolute paths into the live `out/`.
* `tools/rehearse_revert_set.sh` → **38 ok, 0 failed**, exit 0. 727's repair of m734's cell (a cell that
  asserted a *size* where it named a *hash*) holds on this run, first try, with no red cell.
* `tools/verify_press_ready.sh` → **5 of 5, exit 0**. Row 4 refused once before passing, and the refusal
  was correct: no paragraph for the value `13` existed, and every rung this file narrates is narrated by
  its own value. The paragraph added in this step is the fourth of this file's own rung narration to be
  written by a reading rather than by remembering, and it too is bounded where the file's older prose is
  not — it names the arm's own bound through `$wpt_txt`, not a number typed here.
* the gate itself, run by readiness as its third check: **exit 0** under `--allow-xnu-entry`. 727's
  ladder-bound clause reads the bound out of `entry_storage.c`'s own `#if` and accepts `13` with no edit
  to that clause at all — **which is the whole point of having made it parse the bound instead of
  restating the rungs**: eleven rungs of this file grew by one hand-kept line each, and this one grew by
  none.

And one host-side tool that is **red before and after, with the same violations**, checked rather than
assumed because a new row is exactly what this file's ordering rule is about:
`tools/check_experiment_index.py` reports **7 order violations in 678 rows** at `HEAD` (lines 504 and
508-512 ascending *before* the peak, 582 descending *after* it) and **the same 7 in 679 rows** now —
504 and 508-512 unchanged, and the descending one moved **582 → 583** by the single line this step
inserted above it. So the new row adds a row and no violation. 727 recorded the same seven, and the
seven themselves are **not** this step's subject: they are a plateau and a second ascension inside the
stage90 column from long before, and the checker's own header says the descending half is history while
a second ascension is not — it is red, it is recorded, and it is left alone.

## 6. What the rung is not, and where the goal stands

It is not a new command; it is not a claim that the completion exists (`_int_status_after = 0` is a
result); it does not make the enable persistent (the restore is the last store and
`_int_enable_readback` is its cell); and it does not put the storage line's goal any closer by itself.

**The goal is still not met.** XNU is not 正常加载, the OS has not been entered, no basic driver is
running, and 「如果os已经能进去了的话」 is not triggered — so **TWRP-to-storage stays withheld**. What
moved is one question down the same line: *this controller completes a command and does not set its
status register* becomes *does it latch the status bit only when its enable is set, and if not, where
does it report the completion*. The arm is armed and the press is owed.
