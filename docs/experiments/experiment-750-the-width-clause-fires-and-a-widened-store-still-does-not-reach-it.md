# 750: the store census's width clause fires — and a *widened* store still does not reach it, for a reason that is now measured

**One build battery, two cells, driven against the rung-21 armed source. No device action of any kind** —
no `fastboot`, no `adb`, no press, no gate, no runner. The source was restored byte-for-byte after every
cell and a clean rebuild returned `out/` to the armed arm's own bytes; §6 is the evidence, not the claim.

**The one-line finding.** An item carried in the record since 2026-09-25 — *"the width clause of the store
census still never observed to fire on a WIDENED DEVICE store"* — is **half closed and half explained**.
**The clause fires** (cell W2, below, for the first time in the record). **A widening still does not reach
it**, and the reason is not luck: a widened store at `CLOCK_CONTROL 0x2C` becomes `str r6, #0x92C` with
`r6 = 0xf9824000` instead of `strh r10, #0x2C` with `r10 = 0xf9824900` — **the same absolute address
reached through the neighbouring window's base register** — and the census attributes a store to a window
by its **base register**, so the `core_mem` offset clause refuses before the `hc_mem` width clause is
consulted.

## 0. Why this was worth a battery rather than another line in a "carried unchanged" list

The item has been **carried unchanged through fifteen rungs** — 701, 703, 705, 706, 708, 709, 710, 711,
712, 713, 714, 717, 720 and every record block since — always in the same words, and always with the same
reason beside it: *the mutations that widened a store were refused by the AMB clause first*. **A check that
has never been observed to fire is a check whose coverage nobody has measured**, which is the project's own
rule and the headline of 747b. The item is not decoration: the census is what makes *"this arm writes
exactly these stores to these registers at these widths"* a property of the artifact rather than a
sentence, and the arm parked in `out/` is defended by it.

## 1. The item, verbatim, and its own stated reason

From `records/revert-set.txt`, the rung-4 block:

> * **M2, M2b, M4** - the store made 32 bits wide (`st_write8`'s own body, and a direct word store),
>   and the address aimed at a device megabyte this controller does not declare (`0xf9825000`):
>   all three REFUSED, but **by the AMB clause and not by the one each was written for** - a
>   mutated store changes GCC's register allocation, so the base register carries more than one
>   device high half and the outermost clause refuses first. **The width clause (`strb`) therefore
>   has NOT been observed to fire**, and the honest state of it is that its subject - a wider store
>   to 0x2F that still materializes the same base pair - is not a body this source produces. It is
>   recorded as owed rather than claimed as exercised.

**The item names its own falsifier**: *a wider store that still materializes the same base pair*. The
clause's own refusal message names the same case from the other side:

> 0x2C is 4-aligned and 0x28-0x2A are not, so a wider accessor at either would be refused by the alignment
> census for the wrong reason - **the width here is a property of the register and not of the address.**

## 2. The two cells

`/tmp/vprfix/falsifyW.sh`, both cells anchored on a **unique** string and both restoring the source before
the next one runs. Both edit the same site — `entry_storage_probe`'s second `CLOCK_CONTROL 0x2C` store
(`entry_storage.c:1228`, the `word_card` write, `sdhci.c:1305`'s `sdhci_writew` call) — and in **opposite
directions**, so the pair decides which of the two perturbations the clause can see.

| # | the perturbation | the mnemonic list moves | the offset list |
| --- | --- | --- | --- |
| **W2** | the `0x2C` store **narrowed**: `st_write16` → `st_write8` | `strb strh strh strb` → **`strb strb strh strb`** | unchanged |
| **W1** | the same store **widened**: `st_write16` → `st_write32` | `strb strh strh strb` → `strb str strb` | unchanged |

## 3. W2 — the width clause fires, for the first time in this record

    FAIL: entry_storage_probe's stores in the hc_mem window (through [sl r9 r3 r7 ]) are
    [strb strb strh strb ] and rung 7's record says the widths are strb strh strh strb - a byte
    at SOFTWARE_RESET 0x2F, two halfwords at CLOCK_CONTROL 0x2C, then a byte at POWER_CONTROL
    0x29 ... The offset check above cannot see this: 0x2C is 4-aligned and 0x28-0x2A are not ...

**Read the two lists and the clause order in the same message.** The `hc_mem` window still carries four
stores through the same four base registers (`[sl r9 r3 r7]`), and the mnemonic list is
`strb strb strh strb` — one `strh` became a `strb`. **The offset assertion is the clause immediately above
this one in the source and it did not fire**, which is a proof rather than an inference: the offsets were
still `47 44 44 41`, so **the address list did not move at all and only the width did.** That is exactly
the body the owed item said *"is not a body this source produces"*.

**So the item's central claim is refuted by measurement**: the clause **is** reachable, and the offsets it
is checked after are what make the pair `offset + mnemonic` two independent readings rather than one.

## 4. W1 — the widened store is refused by the *neighbouring window*, and the mechanism is exact

    FAIL: entry_storage_probe's stores in the core_mem window (through [r6 ]) are
    [120 0 120 120 268 268 2348 ] and rung 6's record ... says they are 120 0 120 120 268 268

**The store did not disappear and it was not mis-addressed — it changed window.** `core_mem`'s base is
`0xf9824000` and `2348` is `0x92C`; `0xf9824000 + 0x92C` is **`0xf982492C`**, which is `hc_mem + 0x2C`,
the same absolute address the unmutated store writes. The widened store simply stopped using
`hc_mem`'s base register:

| | base register | instruction | absolute address |
| --- | --- | --- | --- |
| unmutated | `r10` (`= 0xf9824900`) | `strh rX, [r10, #0x2C]` | `0xf982492C` |
| W1 (widened) | `r6` (`= 0xf9824000`) | `str rX, [r6, #0x92C]` | `0xf982492C` |

**And that is the general statement, not an anecdote about one cell: this census attributes a store to a
window by the BASE REGISTER the compiler chose, not by the address the store computes.** So a perturbation
that changes register allocation moves a store *between windows* — and the clause that refuses it is
whichever window it lands in. That is `[[mi4-one-value-two-definitions]]` **m718**'s shape (*the PROXY
decided which stores a window has*) surviving in the census after 718 was repaired, because 718's repair
changed how a **base set** is *reported* and not how a store is *attributed*.

**And this is why the AMB clause kept winning.** AMB fires when one register carries **two** device high
halves. W1 shows the *other* half of the same coin, which the record has never stated: a register that
carries exactly **one** device high half is enough to move a store to the wrong window, and the census will
refuse it **for the other window's offset list** — a true refusal, a correct refusal, and a refusal that
says nothing about the width.

## 5. What this closes, and what it does not

| the claim | before | now |
| --- | --- | --- |
| the width clause has never been observed to fire | carried as owed since 2026-09-25 | **CLOSED** — W2 fires it, with the offset list provably unmoved |
| *a WIDER store that materializes the same base pair* is not a body this source produces | recorded as owed | **the widening still does not produce one** — W1 reaches the `core_mem` offset clause instead, and §4 says why |
| the census's offset check and width check are two independent readings | asserted in the clause's own prose | **measured** — W2 moves the mnemonic and not the offset; W1 moves the window and reports an offset of `2348` |

**The precise sentence, and it is narrower than the item it replaces**: *the store census's width clause is
reachable and fires on a store narrowed at a 4-aligned register; a store WIDENED at the same register is
refused by the neighbouring window's offset clause first, because widening moves the store onto the other
window's base register — and both are refusals, so the image is safe either way, but only one of them is a
statement about width.*

## 6. The tree state, because a battery that builds writes through `out/`

`build_entry.sh` rebuilds the entry image, and **a build that FAILS leaves the mutated image in `out/`**
while `xnu_arm_entry-config.txt` still describes the last image that passed (the hazard the rung-4 block
measured). So the battery ends with a **clean rebuild on the restored source**, and every member was
re-checked against the record afterwards rather than trusted:

| member | live | the record |
| --- | --- | --- |
| `xnu_arm_entry.bin` | `46fe6737…` | `46fe6737…` |
| `xnu_arm_entry.elf` | `f44d7fe5…` | `f44d7fe5…` |
| `xnu_arm_entry-config.txt` | `b67c31df…` | `b67c31df…` |
| `xnu_arm_entry-sources.txt` | `5d565d31…` | `5d565d31…` |
| `stage90-qcdt.img` | `3f11e29b…` | `3f11e29b…` |

and `src/entry/entry_storage.c` is `38fb069c…`, byte-identical to the rung-21 armed base the battery
started from; `git status --porcelain` is empty; **both parks verify** (11 files, 6 manifest checks each,
in tree and in the export directory). **Nothing was left behind and no arm moved.** The battery refuses up
front if the live source is not the armed source, so it cannot measure a tree nobody armed.

## 7. What this document does not say

- **It does not say the rung-21 arm is stronger than its record claims.** The arm's own safety clause is
  `xnu_entry_746`'s access set and counts, which already carry the widths — the census this document
  exercises is `entry_storage_probe`'s window census, a different clause in a different function. What
  improved is the *record's* honesty about one of its own instruments.
- **It does not arm, move or press anything.** `armed-storage-46fe6737` is **ARMED AND NOT PRESSED**, no
  firer is armed, `out/` holds exactly the bytes it held before, and **no press may be spent without the
  operator's authorization.**
- **It does not rebuild the payload.** `./build.sh` was not run; only `src/entry/build_entry.sh`, which is
  reproducible from the tree, and the clean rebuild is what proves that on this occasion.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It closes a fifteen-rung-old
  owed item about a check, and that is the whole of what it claims.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist; 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
