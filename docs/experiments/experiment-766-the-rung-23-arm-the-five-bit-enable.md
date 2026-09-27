# 766: the rung-23 arm — the window's enable word widened from one bit to the five 765 §4 pre-registered, built and parked

**Host-side only. No device action, no press, no gate against a device, no runner, no firer.**
`out/` **was rebuilt** — the arm in it is now this one — and that is the whole of the device-side risk
this step carries: nothing was sent to the phone. The measurements are the two build logs, the two
switch records, `nm`/`size` on the two entry ELFs, the clause `build_entry.sh` prints when it accepts
this rung, `tools/verify_press_ready.sh` **5 of 5 exit 0**, `tools/verify_revert_set.sh` **11 of 11** and
`make check` **exit 0**.

**One press spent, zero presses owed.** `armed-storage-c3007c37` — the rung-22 arm, pressed
2026-09-27 08:03:28–08:04:41 UTC — is still parked and intact. The arm in `out/` is
**`armed-storage-3a92aa52`, ARMED AND NOT PRESSED**, and spending it is the operator's decision.

## 1. What the arm is: one constant, two reads, and nothing else moved

765 §1 measured the distance and 765 §2 turned it into the narrowest true form of the ladder's headline
negative: `_status_any = 0` over 5,093,376 polls is *no **enabled** bit was latched* as much as *the
block said nothing*, because this ladder has enabled exactly **one** status bit in its whole history
while the vendor's own `sdhci_init` enables **eleven**. 730's press already measured, on this block, that
a status bit is latched only while its enable stands. §4 of that document pre-registered the arm that
separates the two readings; **this is that arm, built to that pre-registration.**

**The change is one constant.** `st_cmd3_noidx` writes `ST_SDHCI_INT_ENABLE_CMD` = **`0x000F0001`** into
`INT_ENABLE 0x34` where rung 21 wrote `int_enable | SDHCI_INT_RESPONSE`:

| bit | name | in rung 21's window | in rung 23's |
| --- | --- | --- | --- |
| `0x00000001` | `SDHCI_INT_RESPONSE` | yes | yes |
| `0x00010000` | `SDHCI_INT_TIMEOUT` | no | **yes** |
| `0x00020000` | `SDHCI_INT_CRC` | no | **yes** |
| `0x00040000` | `SDHCI_INT_END_BIT` | no | **yes** |
| `0x00080000` | `SDHCI_INT_INDEX` | no | **yes** |
| `0x01000000` | `SDHCI_INT_AUTO_CMD_ERR` | no | no — *deliberately* |
| `0x00F00000` | `BUS_POWER` + the `DATA_*` four | no | no — *deliberately* |

The vendor's own set is `0x01FF0003` and **this window is five of its eleven**, which is exactly what
765 §4 pre-registered: `AUTO_CMD_ERR` is a failure of the block's own auto-command mechanism and the
`DATA_*` half is about a transfer this rung never starts, so neither is a fact about the card, and
enabling them would widen the answer space with bits that name none of the four outcomes.

**Everything else about the window is rung 21's**: the same position inside `st_cmd3_noidx` (after
`st_cmd_path`'s gate and after CMD0/CMD1/CMD2), the same opcode (3), the same argument (`0x00010000`),
the same flag word (`0x030A`), the same **two stores, both to `INT_ENABLE 0x34`**, the same
`SIGNAL_ENABLE 0x38` read and never written, and the same restore from the value the gate authorised.

**And two reads that no rung has taken**, both pre-registered in 765 §4:

- **`_nidx_status_pre`** — `INT_STATUS 0x30` read as a **word**, between the enable store and its
  readback, with the wider mask in place and no command of this arm's on the bus. Its partner is the
  cell rung 21 already publishes: `_nidx_clear_after` is the same register read after
  `st_send_command`'s own write-1-to-clear, so the pair says whether this arm's command starts from a
  clean latch.
- **`_nidx_tout_ctl`** — `TIMEOUT_CONTROL 0x2E` read as a **byte**, after the restore. The register no
  rung has ever read. The vendor writes it only from `sdhci_prepare_data` (`sdhci.c:827-828`) under
  `if (data || (cmd->flags & MMC_RSP_BUSY))`, so on the vendor's own path a data-less command leaves it
  at its reset value — **a non-zero reading here would be a finding** — and `0x00` is the field's
  *longest* setting rather than the absence of a timeout, which the narration says out loud so no reader
  takes `0` for "the block cannot time out".

**The primary answer needs no new machinery at all.** `st_send_command`'s poll already publishes the
**raw, unmasked** `INT_STATUS` word it first saw non-zero in (`entry_storage.c:2362-2365`) over about
5.09 million readings; the mask appears only in the poll's *break* condition and the pre-send staleness
gate. So the same poll that has read zero on five presses is the poll that will read `0x00010000` if the
card is silent and the block knows it.

**File: `src/entry/entry_storage.c`.** One constant, one `#if >= 22` around the enable word, two
`#if >= 22` reads, six `_Static_assert`s, and prose. `src/entry/build_entry.sh` gained a value-dependent
shape for the existing rung-21 clause (§4 below).

## 2. The four-row answer space, and each row's next act

765 §4's table, restated because it is what the press would be spent on:

| `INT_STATUS`'s error half afterwards | reads as | the next act |
| --- | --- | --- |
| **`0x00010000` alone** | **the card did not answer and the controller knew it** — and that one bit refutes the whole *this controller runs a command and never sets its status* reading of rungs 13–22 | the subject becomes the **card and the bus**: bus width, the CMD/DAT pads, the clock at the card's pins, the board file's `qcom,pad-*` settings |
| `0x00020000` / `0x00040000` / `0x00080000` | **the card answered and the word was malformed** — a live card on a mis-sampled or mis-configured bus | the subject is **sampling and bus width**: `HOST_CONTROL 0x28`'s width bits, and the DLL question 761 §5 closed |
| **nothing, with the enables up** | the strong form of the negative: a command taken, started and held 1.2 s that never armed its own response timeout — a statement about the block's internal command path | the subject is the **block**: `_nidx_tout_ctl`'s value, `CLOCK_CONTROL`'s divider at the moment of the command, and whether `PRESENT_STATE`'s bit 24 ever moves for a response-demanding word |
| bit 15 only, or the same `0x00008000`-shaped value as before | the sticky bit is a property of the IP and not a report | 765 §2's open observation closes with no news, and the third row's subject is the one to take |

**The failure mode is a diagnosis and not a lost device**, and it is the same one rungs 17–22 carry: a
delivery on this block's shared SPI 123 line ends the run at the dispatcher as `_irq_other_count = 1` /
`iat = 155`, an ending this image already reads and survives (709 did exactly that on intid 170).

## 3. The build, and the four refusals it cost — one of which was the arm's own value

| invocation | refused by | what it was |
| --- | --- | --- |
| 1 | `entry_trace.c`'s own `#error` | `STAGE90_ENTRY_TRACE` unset; the pressed arm carries `1`, and the probe is called from the wrapper that only exists when the trace is on |
| 2 | `entry_trace.c`'s own `#error` | `STAGE90_XNU_SEAM_POC` unset; the pressed arm's post-end deadline is timed against an operation, and there is no operation without it |
| 3 | **the compiler, on one of this step's six new `_Static_assert`s** | see below |
| 4 | **the clause this step had just made value-dependent** | see below |

**The third refusal was the arm's own text being wrong, and it is worth recording exactly.**
`(ST_SDHCI_INT_ENABLE_CMD & ST_SDHCI_INT_CMD_ERR) == ST_SDHCI_INT_CMD_ERR` is **false by
construction**, because `ST_SDHCI_INT_CMD_ERR` is `0x010F0000` and therefore **carries `AUTO_CMD_ERR`
(`0x01000000`) — the very bit this window excludes.** The assertion was written to say *the three
card-visible error bits must all be in*, and it was written against the wrong constant. The literal
`0x000F0000u` replaced it. That is the assertion doing its job on the first build rather than on a press.

**The fourth refusal is the more interesting one and it is a defect the step introduced and then caught
in itself.** The existing rung-21 clause was made *value-dependent* by turning its four expected sets
into variables computed from `$STORAGE_PROBE`, and the body was then handed to the classifier as

    classify_body "$stb_nidx_body" $stb_nidx_decl        # unquoted

— where the original had been `classify_body "$stb_nidx_body" "f9824910 f9824914 …"`, a **quoted**
literal. `classify_body` takes its declared-address list as **one positional argument** (`local body=$1
decl=$2`), so the unquoted expansion split nine addresses into nine arguments and `declset` held
**one**. The refusal that came back was nine `NODECL-f982-…` keys and one resolved address, and it looked
like a body that had moved. It had not: run standalone on the same dump with the same nine addresses the
classifier resolves the body **perfectly**, and the head/stripped slices match the expected rung-23
shapes exactly. **Re-quoting the argument was the whole repair** — and `bash -n`, the gate and `make check`
are all blind to it. See §7.

The entry build then exits **0**, printing `xnu_entry_766`, and the payload build follows with

    STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

**and not the bare `STAGE90_XNU_ENTRY=1` this step first tried.** The bare form is not read (switches
reach the compiler through `STAGE90_EXTRA_CFLAGS` here), the build exits **0**, and the config it writes
says `#define STAGE90_XNU_ENTRY 0u` — **an image that never jumps into XNU and a payload whose config
disagrees with the arm it is supposed to be.** It was caught by diffing the new
`stage90-build-config.txt` against the parked arm's before parking anything.

## 4. The value-dependent clause, and what it makes structural

Rungs 21, 22 and 23 share **one body** and differ in one constant, so there is no symbol to exclude a
mixture by the way rung 19's and rung 20's clause excludes theirs. What carries the shape instead is
that **every expected set in the clause is computed from `$STORAGE_PROBE`**, as two branches:

    stb_nidx_decl / set_want / cnt_want / order_want / h_want / s_want  =  rung 21's, by default
    if (( STORAGE_PROBE >= 22 )); then
        stb_nidx_decl += " f982492e"                 # TIMEOUT_CONTROL 0x2E
        stb_nidx_cnt_want   → f9824930:ldr 1 → 2     # _nidx_status_pre
        stb_nidx_order_want → … f982492e:ldrb …
        stb_nidx_h_want     →  0x30 read in the head
        stb_nidx_s_want     →  0x2e read in the tail, between the restore and the readback
    fi

**A source whose rung-23 guard were widened from `>= 22` to `>= 21` — which would silently re-shape the
arm already pressed as rung 22 — is refused at value 21 *and* at value 20, the two arms it would have
replaced.** That is the exclusion, expressed as arithmetic on the value rather than as a symbol check.

**The clause accepted this arm with these readings**, each one a measurement of the linked image rather
than of this document:

| assertion | value |
| --- | --- |
| device accesses, sorted | `f9824910:ldr f9824914:ldr f9824918:ldr f982491c:ldr f9824924:ldr f982492e:ldrb f9824930:ldr f9824934:ldr f9824934:str f9824938:ldr` |
| counts | `… f9824924:ldr=1 f982492e:ldrb=1 f9824930:ldr=2 f9824934:ldr=2 f9824934:str=2 f9824938:ldr=1` |
| program order, distinct | `… f9824934:str f9824930:ldr f9824934:ldr f9824938:ldr f982492e:ldrb f9824924:ldr` |
| **stores** | **exactly `f9824934:str` — two, both to `INT_ENABLE 0x34`, unchanged from rung 21's** |
| head (before its `st_send_command`) | `f982491c:ldr f9824918:ldr f9824914:ldr f9824910:ldr f9824934:str f9824930:ldr f9824934:ldr f9824938:ldr` |
| stripped tail (after it) | `… f9824934:str f9824930:ldr f982492e:ldrb f9824934:ldr f9824924:ldr` |

The two slices are what make "the wide enable is standing before the command" and "the restore precedes
`_nidx_status_post`, `_nidx_tout_ctl` and `_nidx_readback`" properties of the bytes rather than
sentences: the head proves the `0x30` read sits between the store and the readback, and the tail proves
the restore is still the **first** store to appear after the command, so the three cells taken after it
are taken with the enable **closed**.

**The enable word's own value is carried by six `_Static_assert`s and not by any clause**, because a
clause can read addresses and widths and a body's accesses and `bl` sites carry no immediate. Together
they say: the word is `0x000F0001`; `RESPONSE` is in it; `TIMEOUT` is in it; all four command-level
error bits are in it; `AUTO_CMD_ERR` is **not**; and `BUS_POWER` with the `DATA_*` four is **not**.

## 5. The arm, measured — and nothing before it moved

| reading | rung-22 arm (`c3007c37`) | rung-23 arm (`3a92aa52`) |
| --- | --- | --- |
| `STAGE90_XNU_STORAGE_PROBE` in the entry record | `21` | **`22`** |
| every other entry switch | — | **identical** (the record diffs in exactly two lines: this one and the artifact hash) |
| `st_cmd3_noidx`, `nm -S` | `0x338` = 824 B | `0x364` = **868 B** (**+0x2C = +44 B**) |
| `entry_epilogue` address | `0x80004948` | **`0x80004948`** (unmoved) |
| `STAGE90_XNU_SEAM_LR` | `0x800492dc` | **`0x800492dc`** (the build's own seam check, exit 0) |
| entry `.text` | 5,334,536 | **5,334,600** (+64 B) |
| `xnu_arm_entry.bin` size | 5,552,764 | **5,552,764** — unchanged, the growth fitted the padding |
| payload switch set | — | **byte-identical** to the pressed arm's (`6c2b6038…`, unchanged) |

**Every other entry-source-derived file differs by hash and not by content class**, which is what the
build's own manifest clause is for: `xnu_arm_entry.bin` `3a92aa52…`, `xnu_arm_entry.elf` `0132c4d1…`,
`xnu_arm_entry-config.txt` `5f8e264f…`, `xnu_arm_entry-sources.txt` `999f4c00…`. The payload artifacts
that embed it move with it: `stage90.bin` `9e9af746…` (6,048,260 B), `stage90.elf` `27b20815…`
(6,110,332 B), `stage90.img` `b498cc68…` (6,051,840 B), `stage90-qcdt.img` `ce4bf867…` (8,572,928 B).
`stage90_fixture.macho` is unmoved at `52bc9c35…`.

**Verified host-side:** the park is `out/stage90/frozen/armed-storage-3a92aa52`, **11 members**,
`cmp`-identical to `out/stage90/` for every one, and `tools/verify_revert_set.sh … --set=armed-storage-3a92aa52`
returns **11 ok / 0 failed**; `tools/verify_press_ready.sh` **5 of 5, exit 0**, and it resolves the live
arm to this set by hashing the live `stage90-qcdt.img`; the gate is **exit 0** under
`--allow-xnu-entry`; `check_set_name_rule` now reads **32 sets**; `check_payload_config_entry` reads
`STAGE90_XNU_ENTRY 1` across **32** parked arms; `make check` **exit 0**. The two commands readiness
prints, and the only ones that may be run, are

    ./scripts/preflight_boot_check.sh --allow-xnu-entry
    ./scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-3a92aa52

**Nothing has been pressed, and no firer is armed.** The runner does not archive the capture; the
archive into `out/stage90/captures/` is by hand.

## 6. And the site 763 §5 recorded as owed was paid by this step

763 §5 left `src/entry/build_entry.sh`'s comment on 408 **owed** and named the exact condition for
paying it: *"the next step that rebuilds the entry image — the rung-23 arm, or anything else that moves
`src/entry/` — should carry this one sentence with it."* This is that step, so it is paid here rather
than carried again.

The comment had said a payload rebuild **does not reproduce**, which is 760's refuted claim. 408's 48
bytes are `kernel_size` **6,015,356** with `STAGE90_XNU_ENTRY=1` against **6,015,308** without it — **a
switch, not a nondeterminism**. The correct instruction is stronger and checkable: the build is
reproducible from the tree *and its switch set*, and what a careless `./build.sh` costs is that
`STAGE90_XNU_ENTRY`'s **default is OFF** — so a plain rebuild produces an image that never jumps into XNU
and overwrites the armed bytes in `out/`, after which the arm exists only in its park. **And this step
demonstrated the trap live**: the bare `STAGE90_XNU_ENTRY=1` invocation produced exactly that image.

## 7. What this document does not say

- **It does not spend the press.** `out/` holds this arm **armed and not pressed**; the press is the
  operator's decision and the two commands above are the whole path to it.
- **It does not claim the wider mask will answer.** It says the answer is *readable* — which is 765's
  finding and not this step's.
- **It does not re-open 764.** The DLL answer stands and its path stays closed.
- **It does not move any guard on a rung that has been pressed.** Rung 21's `>= 20` call site and rung
  22's `>= 21` census are untouched; the new code is entirely inside `#if >= 22`. The arm carries rung
  22's DLL census as well, deliberately, exactly as rung 22 carried rung 21's command.
- **It does not add the bit-24-LOW count** that 765 §4 named as *one new sample, not a new cell*. That
  count lives inside `st_send_command`'s poll — the one body **every** command in this ladder shares — so
  adding it would move rung 11's clause for every rung, to answer a question `_nidx_inhibit_last` already
  gives one sample of. **Deferred, and named here so the deferral is a decision and not an omission.**
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. What it buys is that the next
  press is spent on a question the ladder has never asked.

## 8. New instance — **m774: a shape variable that was quoted when it was a literal and unquoted when it became a variable**

The rung-21 clause passed its declared-address list as a **quoted literal** to a function whose second
parameter is **positional**. This step replaced that literal with a variable and passed it **unquoted**,
so nine addresses arrived as nine arguments and the function took the first. The clause refused the
build — nine `NODECL-` keys — and the refusal *read like a body that had moved*, which is the wrong
diagnosis and would have sent the next reader into the disassembly of a body that was already correct.

**Shape to suspect first: an edit that changes a literal into an expansion at a call site where the
callee takes a positional argument.** The two forms **look identical in a diff** to a reader skimming
for the variable's name, and `bash -n` cannot see the difference because the difference is arg count at
runtime. **The test: after replacing a literal with a variable, count the words the callee will
receive** — `"$var"` is one argument however many spaces it holds, `$var` is not. Related:
[[mi4-one-value-two-definitions]] (the literal and the variable are one value with two readings, and the
*reader* of that value is where they part), [[mi4-a-claim-in-a-comment-is-not-a-check]] (the clause was
right; the call into it was not), [[mi4-measurement-defects]] (a refusal whose text names the wrong
subject is a measurement defect in the refusal).

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a
response-demanding command and never completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so
**TWRP-to-storage stays withheld.**
