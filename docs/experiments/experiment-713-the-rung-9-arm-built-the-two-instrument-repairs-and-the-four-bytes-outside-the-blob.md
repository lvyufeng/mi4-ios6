# 713: the rung-9 arm built and parked — the two instrument repairs the build forced, the four bytes outside the blob, and the owed press

**Date:** 2026-09-26. **Arm:** `armed-storage-pwrwait-8b824cac` (`STAGE90_XNU_STORAGE_PROBE=9`,
`STAGE90_XNU_PWR_WAIT_TICKS=1920000`), parked in `out/stage90/frozen/armed-storage-pwrwait-8b824cac/`
(11 files, every hash verified against the build directory it was copied from). **Not pressed.** The
entry build exits 0 and prints its own reading (`xnu_entry_712`); the payload was rebuilt with
`STAGE90_XNU_ENTRY=1` and every artifact was then measured byte by byte against the pressed rung-8
park. Nothing in this document is a prediction: every number below was taken off the built files.

712 pre-registered the rung; this is the build of it, and the build is where two instruments turned
out to be wrong — one of them wrong in the direction that had been **passing**, which is the
interesting direction.

## 1. The arm, in one paragraph

Rung 9 is 710's arm plus `sdhci_set_power`'s next statement after rung 7's byte —
`host->ops->check_power_status(host, REQ_BUS_ON)` (`sdhci.c:1371-1372`). The vendor implementation is
a **cache read of two driver-side fields plus an unbounded `wait_for_completion`**
(`sdhci-msm.c:2179-2210`), so the rung ports the predicate (with the vendor's own already-satisfied
branch *taken*, not skipped: on that branch the vendor resets the completion and does not wait,
`:2203`) and replaces the block with a **bounded tick poll whose end condition is the controller's
own `CORE_PWRCTL_CTL & BUS_SUCCESS`** — because the probe runs inside Apple's cache-off idle-exit
window (`xnu_live_seam_sctlr = 0x30c57879`) while the handler may run with the caches on, so an
image-side flag written by one can sit in L1/L2 while the other's direct read is answered by DRAM
(one flag, two definitions). The image-side flag is still written and still published (`_wait_done`)
— **the disagreement between the two is the reading.** `CLOCK_CONTROL 0x2C` is read as a **halfword**
either side of the wait, which is 711 section 3's open question answered by construction. The waiter
is `noinline` — the mirror of rung 6's `always_inline` — because what a clause must read about it is
its bounded *shape*: the budget constant, the ack it spins on, and the probe's single call to it. The
new switch `STAGE90_XNU_PWR_WAIT_TICKS` defaults to 1,920,000 (100 ms at this device's own
19,200,000 Hz), `#error`s outside `[1, 19200000]`, and **0 is refused rather than read as "no wait"**
because rung 8 *is* the no-wait arm. Full rationale in experiment-712; this document is about what
happened when it was built.

## 2. The instrument finding: a linear scan cannot know a register's value across a branch

The rung-8 clause has classified `st_pwr_irq` since 710 by disassembling its `nm -S` window and
reporting every device access it finds, tracking each address as a `movw`/`movt` pair per base
register in program order. It passed on the rung-8 arm. On the rung-9 build it reported **three
accesses at `f98200e9` and `f9820a0d`** — addresses in the `0xf982_0xxx` window that no arm of this
ladder has ever written and that appear nowhere in the source.

They are not real, and the disassembly says exactly why:

```
8000d0fc:  bne   8000d1fc        <- the tail is DUPLICATED across this branch
   ...                             (the first copy materializes both halves)
8000d1fc:  movt  r3, #0xf982      <- only the HIGH half is re-materialized at the target
```

The low half is a `mov r3, #0x4000` from two blocks earlier and is *still* in `r3`, so the compiler
had no reason to re-emit it — and the last low half the linear scan had seen for `r3` was a stale
`mov r3, #1`. `0xf982` + `0x1` is `0xf9820001`, and with the offsets the tracker then applied it
printed `f98200e9`. **A linear scan cannot know a register's value across a branch**: the register
file is not part of the instruction stream, and any tracker that assumes program order for a value
the compiler is entitled to keep across a control transfer will eventually invent an address.

**The repair is a new address model, not a wider expected set.** Widening the handler's declared
addresses to include `f98200e9` would have made the clause pass while destroying the only property it
has. What the classifier does now:

* **a pair is FRESH only when both halves were materialized since the last control transfer**, where
  the control-transfer set is `b`/`bx`/`blx`, `ldm*`/`pop` with `pc`, `ldr`/`ldrd` with `pc`, and
  `mov pc`. A fresh pair yields exactly one candidate address and no hypothesis is needed.
* **a stale pair is ENUMERATED** over every half the body ever materialized for that base register,
  against the record's **DECLARED** device set — and accepted only when **exactly one** candidate is
  declared. Zero declared candidates is refused as `NODECL-<hi>-<off>:<mn>`; more than one as
  `AMBI-<hi>-<off>:<mn>`. The two refusals are the point: the classifier never picks.
* **mnemonics are normalized by dropping the condition suffix and keeping the width**, so `strne` and
  `str` are one store reached on two paths while `str` and `strb` remain one form at two widths.

The failure mode this replaces was a **false positive that would have been silenced by widening a
record** — the class this project pays for most often, caught this time before the press.

## 3. The second repair: the waiver set was vacuous

The rung-8 clause's image side asserts that the handler's non-device accesses are exactly the four
`.bss` words it is allowed to touch, by resolving each address to a symbol in the **linked** image and
refusing anything that is not one of the four. The resolution's first draft read the four names and
then marked **every** `.bss` symbol waived — so the `not-waived(<sym>)` direction, the entire reason
the widening exists, could never fire. A clause whose refusal is unreachable is a clause that cannot
fail, and it passed every build it was run on.

It was caught by a smoke test in which `g_pwr_irq_arms` — the handler's own neighbour, one word away
at `0x805541b0` — was reported as **waived**. The reader now builds the `.bss` map and the `END` block
builds the waiver set from the recorded names only; re-tested, `8030b0:str=not-waived(g_pwr_irq_arms)`
fires, and the comment records both the defect and that the check is exercised on every branch in the
build log. **A refusal that has never been observed to fire is not yet a check** — this is the same
lesson as `mi4-a-claim-in-a-comment-is-not-a-check` and it cost nothing here only because the clause
was smoke-tested before the arm was parked.

## 4. One implementation, two windows

Rung 9 needed a **second** body classified: `st_pwr_wait` is `noinline`, so it is its own symbol with
its own `nm -S` extent, and the probe's census cannot see inside a function it does not contain. The
tempting move was a second copy of the rung-8 clause's classifier. Instead the classifier and the
resolver are each written **once** — `classify_body()` (an awk program taking a body and a declared
device set, printing the device set, its counts, the image-side class set and the resolved addresses)
and `resolve_img_addrs()` (taking a waiver list and the classifier's fourth line, resolving each
address to a `.bss` symbol in the linked image) — and **both** the rung-8 and the rung-9 clauses call
them. The two windows therefore cannot drift apart in how they read an instruction, which is the
`mi4-one-value-two-definitions` failure this project meets most often, avoided here by construction
rather than by discipline.

## 5. What the build asserts about rung 9

`build_entry.sh` gains the ladder's `9) ;;` case (and its message opens to "0, 1, 2, 3, 4, 5, 6, 7, 8
or 9"), the `STAGE90_XNU_PWR_WAIT_TICKS` switch with a token-only test (its *range* is
`entry_storage.c`'s `#error` and is deliberately not repeated in the shell), and the switch's three
plumbing sites — `ENTRY_ARM_KEYS`, the config resolver's `case`, and the config writer — so that
`xnu_arm_entry-config.txt` carries it. The define goes on `entry_storage.c`'s compile line **only**,
because the wait is that file's.

The new clause (`if [[ $STORAGE_PROBE -ge 9 ]]`) reads `st_pwr_wait`'s own `nm -S` window and asserts:

* its **device accesses are exactly `[f98240e8:ldrb f982492c:ldrh]`** — `CORE_PWRCTL_CTL 0xE8` read as
  a byte and `hc_mem + CLOCK_CONTROL 0x2C` read as a halfword — **and nothing else**, so a `str` or a
  `strb` anywhere in the window fails the set equality rather than a separate assertion. The ack the
  poll waits for is the *handler's* store; a waiter that wrote `CTL` would be answering its own
  completion.
* **counts as minimums**: `f982492c:ldrh >= 2` and `f98240e8:ldrb >= 2`. The set alone cannot see a
  source that dropped one of the two readings either side of the wait, which is the property the rung
  exists for. Built values: `[f982492c:ldrh=2 f98240e8:ldrb=4]` (the compiler duplicates the poll's
  byte read across the vendor's own decode paths).
* its **non-device accesses are exactly `IMG:ldr IMG:str`**, resolved to the four `.bss` words
  `g_pwr_irq_calls`, `g_pwr_curr_state`, `g_pwr_curr_io`, `g_pwr_irq_done` — the same four rung 8's
  clause names, because the waiter and the handler are the two halves of **one handshake**. Built:
  `[805541a8:ldr 805541ac:ldr 805541b0:str 805541b0:ldr 805541a4:ldr]`, all four resolved.
* **exactly one `bl` to `st_pwr_wait` in the probe** — the rung's placement contract, and also what
  keeps the body in the image at all (an unreferenced static is dropped).
* **the budget appears in the linked artifact as a `movw`/`movt` pair on the same register**, because
  a switch whose value never reaches the image is `mi4-off-option-two-spellings` with a shorter fuse:
  the record would say `STAGE90_XNU_PWR_WAIT_TICKS=1920000` and the code would be bounded by
  something else. `movw`/`movt` is how GCC `-mcpu=cortex-a15` materializes it; a build that reached
  for the literal pool instead **refuses**, deliberately — a clause that cannot see the number it is
  asserting must not pass.

One earlier attempt at this clause is worth recording as a refusal that worked: the expected device
string was first written in **ascending address order**
(`f98240e8:ldrb f982492c:ldrh`) while the classifier prints **program order**. The source was right
and the record was wrong, and the build refused. The record now carries program order with a comment
saying so.

**And the lane's `./build.sh` has a live defect, recorded rather than silently worked around.**
`./build.sh` **silently drops `#define STAGE90_XNU_ENTRY 1`**, emitting `0u` — measured by `diff`
against the park: `5d4 < #define STAGE90_XNU_ENTRY 1` / `7a7 > #define STAGE90_XNU_ENTRY 0u`. The
payload's macro export has no trailing space, so the writer's last pattern never matches it. This
arm's payload was therefore built as `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh`, after
which `stage90-build-config.txt` is **byte-identical to the pressed rung-8 park's** — which is the
evidence that the workaround reproduced the pressed arm's configuration exactly. It is a doc/message
item for the lane that owns `build.sh`, not a silent re-do.

## 6. The arm, measured

Against the pressed rung-8 park (`armed-storage-pwrirq-df4d38e5`):

| file | bytes | sha256 |
|---|---|---|
| `stage90-qcdt.img` | 8,556,544 | `6dbd579c…1ad7` |
| `stage90.bin` | 6,031,876 | `cb441cc1…19f1` |
| `stage90-build-config.txt` | 682 | `6c2b6038…547b` — **byte-identical to the rung-8 park** |
| `stage90.elf` | 6,093,948 | `d6f4cb3d…ee3ef` |
| `SHA256SUMS.txt` | 560 | `f82b0544…6800` |
| `xnu_arm_entry.bin` | 5,536,380 | `8b824cac…047d` |
| `xnu_arm_entry.elf` | 6,715,636 | `8772cac9…3f57` |
| `xnu_arm_entry-config.txt` | 910 | `ba004d56…178f` — **the one record that names this arm** |
| `xnu_arm_entry-sources.txt` | 2348 | `ff418458…c4d3` |
| `stage90.img` | 6,035,456 | `11979187…d90a` |
| `stage90_fixture.macho` | 1,744 | `52bc9c35…c97b` — **byte-identical** |

**Containment.** `stage90.bin` differs from the pressed arm in 644,008 bytes: 644,004 inside
`stage90_xnu_entry_blob` (file offset 494,236 = `0x78a9c`; first differing byte 495,192 = blob+956,
last 5,997,985 = blob+5,535,749) and **four outside it**. `stage90.img` and `stage90-qcdt.img` differ
in 644,028 = those 644,008 plus the boot header's own 20-byte `id` field at 576–595, which is
`sha1(kernel, ramdisk=0, second=0)` and therefore must change — **and in nothing else**. The
2521088-byte device-tree blob after the payload is **byte-identical**, and the header's `dt_size` word
at offset 40 is the extension's own length, identical in both arms.

**The four bytes are not stray; they are the payload's copy of the entry image's regenerated
layout.** Disassembled in both payloads they are two instructions, each `+0x40`:

* VA `0x4ac40`/`0x4ac41`: `movw r3, #0x45c8` → `#0x4608`, paired with the unchanged `movt r3, #0x805a`
  — the constant `0x805a45c8` → `0x805a4608`, the entry image's own `STAGE90_XNU_ENTRY_BSS_END`.
* VA `0x4ad04` and `0x4ad18`: `movw r2/r1, #0xcb48` → `#0xcb88`, `movt …, #5` unchanged — the pair
  `0x0005cb48` → `0x0005cb88`, the entry image's **BSS length** (`BSS_END − BSS_START`), handed to
  `memset`.

Both are `+64`, exactly the entry image's `.bss` growth, and `out/stage90/xnu_arm_entry.h` **has been
regenerated** by `build_entry.sh` (`BSS_START 0x80547a80`, `BSS_END 0x805a4608`, `BIN_BYTES 5536380`),
so the payload was built against the new layout and the constants it carries agree with the image it
hands over. The header is a gitignored build byproduct and is in **neither** park's 11 files — the
pressed rung-8 park lacks it too, and is not retrofitted; what this arm's record says instead is the
measurement above, which is strictly stronger than carrying the file.

**The payload's own layout did not move, and that is measured over all of it.** `.text` 6,030,666,
`.data` 1,208, `.bss` 644,048 — identical in both arms — and `nm -S` of the two payload ELFs compares
**equal as a whole** (name, address and size for every one of its 465 symbols), which is what makes
"the only out-of-blob difference can be a layout constant" a fact rather than a hope.

**The entry image, section by section.** `.text` 5,317,728 → 5,318,464 (+736: `st_pwr_wait` is 592
bytes, `st_pwr_irq` grew +108, and the rest is alignment); `.data` 206,512 unchanged; `.bss` 379,720 →
379,784 (+64 — the number the payload's four out-of-blob bytes carry). 26,540 symbols against the
rung-8 arm's 26,536: **four new names and none gone** — `st_pwr_wait` (`0x8000d230`, `+592`),
`g_pwr_curr_state` (`0x805541a8`), `g_pwr_curr_io` (`0x805541ac`) and `g_pwr_irq_done` (`0x805541b0`),
i.e. the waiter and the three words it publishes beside `g_pwr_irq_calls` (`0x805541a4`, unmoved).
`st_pwr_irq` is `0x8000cff8` `+460` → `+568` (the +108 its tail's three publications cost);
`entry_storage_probe` moved `0x8000d1c4` `+5940` → `0x8000d480` `+5948` (the `+8` of the `bl` to the
waiter). Over the 26,536 shared symbols the address deltas are `+0` for 21,016, `+64` for 3,776,
`+752` for 1,126, `+704` for 335, `+16` for 184, `+708` for 79, `+12` for 13, `+736` for 4, `+700` for
1, and **two symbols moved backward** (`−672` and `−736`) — the linker re-lays out every rung, and the
backward pair is why the deltas are measured rather than assumed monotone.

**The seam did not move, the second rung running for which that is true.** `entry_trace.c`'s
`STAGE90_XNU_SEAM_LR` and `run_and_capture.sh`'s literal are both still `0x800482dc` and were **not**
re-pinned, because `platform_cache_idle_exit` (`0x800482d4`, `+108`) and
`__wrap_platform_cache_idle_exit` (`0x8047ead8`, `+120`) are at the addresses the rung-8 arm had them
at.

**The two source manifests.** `xnu_arm_entry-sources.txt` differs in exactly three of its lines — the
entry SHA256 field, `build_entry.sh` (`2478d3e9` → `2ea248de`) and `entry_storage.c` (`aadf08bc` →
`17ec162d`) — and keeps its 2348 bytes. `xnu_arm_entry-config.txt` differs in exactly three lines (the
entry SHA256, the rung digit 8 → 9, and the **new** `STAGE90_XNU_PWR_WAIT_TICKS=1920000` line) and
grew 875 → 910 bytes.

One measurement defect was made and caught while taking these numbers, and it is the cheapest kind:
the `dt_size` word was read out of a hex dump as `0x00782600` — a value that appears nowhere in the
image — because a dump is **bytes** and not a number until you say which end. Little-endian, those four
bytes are `0x00267800`, the extension's own length. Any address or constant reported from a dump in
this document was read in the endianness the format uses.

## 7. The owed press

Readiness first (`tools/verify_press_ready.sh`), which finds the arm by hashing the live
`stage90-qcdt.img` and prints the flag set and the set name in its header; then **exactly one**
`preflight_boot_check.sh <printed flags>` and **exactly one**
`run_and_capture.sh <printed flags> --expect-arm=armed-storage-pwrwait-8b824cac`; then one
non-persistent `fastboot boot` through `/tmp/g668/press-on-clear.v5.sh <OWED-SET> <BUDGET-S>`, which
derives its own flags and lets readiness decide whether to fire. The runner does **not** archive the
capture (`/tmp/cancro-last_kmsg.txt`) — it is archived by hand into `out/stage90/captures/`. `33e80afe`
is unplugged before firing; `fastboot boot` only, never flash.

The cells to read, in the order they answer the rung:

1. **`_wait_timeout` beside `_wait_ctl_after`.** `0x01` is the controller acking the BUS_ON request
   (`BUS_SUCCESS`), `0x00` is the ack not arriving; `_wait_timeout` = 0 with `_wait_ctl_after` = `0x01`
   is the handshake completing inside the budget, `_wait_timeout` = 1 is the poll spending its whole
   bound and going on. `_wait_polls` and `_wait_ticks` are the wait's own cost (`_wait_bound` =
   1920000 is the switch itself).
2. **`_wait_ctl_after` beside `_wait_done` and `_wait_calls` — the two-memory cell.** Device ack with
   the flag **clear** is a driver-side completion that did not cross the cache boundary; device ack
   with the flag **set** is the handshake complete on both sides; `_wait_ctl_after` = `0x00` with
   `_wait_calls` ≥ 1 is a handler that ran and did not ack; `_wait_calls` = 0 with a nonzero
   `_irq_other_count` is the event going to nobody, which is 709's press exactly.
3. **`_wait_cc_before` / `_wait_cc_after` — 711 section 3's answer.** The same register as a halfword
   either side of a hundred milliseconds: equal halves say the bit two boots disagreed about is the
   machine's state, a half that changes across the wait says it is a race with the driver's own clock
   block. Either reading is a fact about the machine, which two runs could not give.
4. **`_wait_reset`.** `1` means the vendor's already-satisfied branch was taken and the completion was
   **reset** (`:2203`), so `_wait_polls` = 0 and `_wait_ticks` = 0 are correct and not a dead loop;
   `_wait_cpsr` is published from **both** paths so the log always names which one ran.

The hazard this press carries is the wait's own cost, and it is bounded by construction: the poll
always ends, but a log whose last keys are `_wait_bound` / `_wait_ticks` is a run whose ending clock
was spent on the wait. **No device store was added by this rung**: the write set is still
`_rst_stores=1` plus the four inherited `_writes=4` plus rung 7's power byte — **no command, no
sector, no partition table, no mount**, and every `_wait_*` key is a register and never the medium.

## 8. Owed, and unchanged

* **On the storage path:** the card's rail (nothing in this image powers a supply); the RCG rate write
  (705/706 measured 192 MHz while the driver believes 400 kHz); rung 6's two stores to
  `hc_mem + 0x10C` with the readback cell; the `CLOCK_CONTROL` bit-1 race 711 section 3 left open —
  **this arm's two halfword readings are the cell that answers it**; and the command path
  (`sdhci_send_command`), where "the card answers" begins.
* **Carried unchanged:** what actually returns a run; the ending's first store faulting
  (`0x0fa0065c`); the width clause of the store census never fired on a WIDENED DEVICE store; the 691
  §5 `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the 684-owed
  runner clause; `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane tripwire
  repairs; BIT(29) of `_clk_ahb_cbcr`; the seam address pinned in two files (this arm did not move
  it); and **the gate's narration of `STAGE90_XNU_STORAGE_PROBE`, now eight rungs short (2 through
  9)** — peer lane, by message and never by edit.

**The goal is still not met.** No command, no sector, no partition table, no mount, no driver beyond
the fixture — so **TWRP-to-storage stays withheld**.
