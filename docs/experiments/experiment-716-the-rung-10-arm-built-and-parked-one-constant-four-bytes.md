# 716: the rung-10 arm built and parked — one constant, four bytes, and the tightest containment this project has recorded

**Date:** 2026-09-26. **Arm:** `armed-storage-pwrwait-aa2b051d` — `STAGE90_XNU_STORAGE_PROBE=9` with
**`STAGE90_XNU_PWR_WAIT_TICKS=384000`** (20 ms), i.e. 714's pressed arm with **one switch value moved
and nothing else**. Parked in `out/stage90/frozen/armed-storage-pwrwait-aa2b051d/` (11 files,
`tools/verify_revert_set.sh` exit 0) and recorded in `stages/stage90/revert-set.txt` (11 `set=` lines
plus the arm's own narration block). The pre-registration is experiment-715; the press it was built
for is experiment-717.

## 1. Why the arm is a constant and not a change

714 §3 could not separate two readings of the rung-9 press — (a) the IRQ was pending throughout the
spin and the mask delayed it, or (b) the controller really takes ~100 ms — **because that arm set its
bound and its mask to the same length by construction**: the correlation was a property of the
experiment, not of the machine. A second wait after the first decides nothing (both readings return at
once), and raising the bound decides nothing either (it moves with the mask). What separates them is a
run whose bound is **shorter than the mask**: under (a) the handler's arrival tracks the end of the
spin, under (b) it tracks the byte, and the two predictions are 80 ms apart. That is the whole rung,
and it is why this arm could be built by moving one number.

## 2. The containment, measured against the parked pressed arm

Against `armed-storage-pwrwait-8b824cac` (714's arm, pressed 00:32:39), by byte comparison of every
file of both sets:

| file | differing bytes | where |
|---|---|---|
| `xnu_arm_entry.bin` | **4** | offsets `0xD284`, `0xD28C`, `0xD420`, `0xD428` |
| `stage90.bin` | **4** | all four inside `stage90_xnu_entry_blob` (file offset `0x78a9c`), at blob-relative `0xD284` and `0xD420` — **nothing outside the blob in a 6,031,876-byte file** |
| `stage90.img` | **24** | those 4 + the boot header's 20-byte `id` field at 576–595, `dt_size` at 40 unchanged |
| `stage90-qcdt.img` | **24** | the same 24 and **nowhere else**; the 2,521,088-byte device tree after the payload is byte-identical |
| `stage90.elf` | 4 | inside the embedded blob; `.text` 6,030,666 / `.data` 1,208 / `.bss` 644,048 all unchanged, and `nm -S` output **identical as a whole** (465 symbols, same addresses, same sizes) |
| `xnu_arm_entry.elf` | 4 | inside `st_pwr_wait`; every section size unchanged (6,715,636 bytes) and `nm -S` **identical as a whole** — 26,540 symbols and **no symbol moved this step** |
| `xnu_arm_entry-config.txt` | **2 lines** | the artifact SHA256 field and `STAGE90_XNU_PWR_WAIT_TICKS=1920000` → `=384000` |
| `xnu_arm_entry-sources.txt` | **1 line** | the SHA256 field — **no source file changed this step** |
| `stage90-build-config.txt`, `stage90_fixture.macho` | 0 | byte-identical (as on every arm since 653) |

**The four bytes, and what they are.** Disassembled in `st_pwr_wait` (VA `0x8000d230`, 0x250 bytes,
which is where `0xD284` and `0xD420` both fall):

* `0x8000D284` `mov r1, #0x4C00` → `#0xDC00` beside `0x8000D28C` `movt r1, #0x1D` → `#0x05` — the
  **published** `xnu_live_storage_pwr_wait_bound` cell;
* `0x8000D420` `mov r9, #0x4C00` → `#0xDC00` beside `0x8000D428` `movt r9, #0x1D` → `#0x05` — the
  **loop's own** bound, the register `cmp r0, r9` at `0x8000D468` tests.

1,920,000 = `0x1D4C00`; 384,000 = `0x05DC00`. Both pairs are `mov` (**rotated immediate**) + `movt`,
**not** `movw` + `movt`.

**Two independent confirmations of the same four bytes fall out of the packaging**, and neither was
arranged: (1) the payload's differing offsets are `blob + 0xD284` and `blob + 0xD420` in **all three**
of `stage90.bin`, `stage90.img` and `stage90-qcdt.img` — the same blob-relative offsets as the entry
bin's own — because the payload embeds the entry bin unchanged and adds nothing else; and (2) the boot
header's `id` field, which is the legacy AOSP `sha1(kernel || kernel_size || ramdisk_size ||
second_size)` as little-endian words, was **reproduced from the file itself** rather than quoted:
`sha1(payload + pack('<III', 6031876, 0, 0))` equals the 20 bytes at 576–595 on **both** arms, so the
field is a function of the payload and changed for the reason it must. 713's sentence about that field
is confirmed here rather than corrected.

**This is a tighter containment than 713's for a structural reason, and the difference is worth
naming.** 713's arm sat **4 bytes outside the blob** because its entry image *grew* (`.bss` +0x40), so
the payload's own copy of the entry layout — `BSS_END` and the BSS length, two `movw`/`movt` pairs in
the payload's head — had to move with it. **A changed constant changes no size**, so there is nothing
outside: the two images that go to the device differ in the four bytes that carry the number, the 20
bytes of a hash that must change, and nothing else at all.

## 3. The build refused the first attempt, and that is the guard working

`build_entry.sh:875-920` compares the **18 arm keys** against the previous record and stops *before
writing anything in `$OUT`*. `STAGE90_XNU_PWR_WAIT_TICKS` is one of them, so this build needed
`STAGE90_ENTRY_ARM_CHANGE=1` — a deliberate change with a name attached. A blind `./build_entry.sh`
would have rebuilt 714's arm and every downstream check would have agreed with the result, because the
record and the manifest are written by the build itself. The recorded switch set for the invocation is
the 18 keys with `TRACE=1`, `REAL_ARM_INIT=1`, `SLOT_NULL=1`, `ISTACK_SEPARATE=0`, `IDLE_STACK=1`,
`SEAM_POC=1`, `POST_END_TICKS=115200000`, `STORAGE_PROBE=9`, `PWR_WAIT_TICKS=384000`; the payload side
is `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh`, whose own record is byte-identical to
714's park's.

The rung-9 clause read the new body as it read the old one and the reading is the switch's second
exercise: device accesses exactly `[f982492c:ldrh f98240e8:ldrb]` with counts `[ldrh=2 ldrb=4]` (the
minimum is 2 because the compiler may duplicate a read across the vendor's own two decode branches),
non-device accesses exactly `IMG:ldr IMG:str` over the four words `g_pwr_irq_calls`, `g_pwr_curr_state`,
`g_pwr_curr_io`, `g_pwr_irq_done`, the probe's single `bl` to it, and **the budget `384000` found in
its own body as a `mov`/`movt` pair** — so the record's number is in the artifact rather than in the
record. Rung 9's arm was itself constructed so this clause could be read (`noinline`, a body the
census can see); this arm is where the clause's *value* is asserted rather than its shape.

**And the clause's acceptance versus its message is a defect, recorded and not repaired (m724).** The
awk accepts `$3 == "movw" || $3 == "mov"` and its refusal text says "no `mov`/`movw` + `movt` pair",
but the **success echo** still says "carried by the `movw`/`movt` pair in its own body" — and on this
arm the pair is `mov` + `movt`. The check is right; the sentence a reader takes away names the other
encoding. It is a print, not a comparison, so nothing failed — which is the point: on this project a
wrong sentence is cheaper to record than to trust.

## 4. The readiness narration was naming the wrong arm, and the row was green (m725)

`tools/verify_press_ready.sh`'s rung-9 paragraph **hard-coded** `STAGE90_XNU_PWR_WAIT_TICKS` =
**1920000** and "**100 ms**" in two places. On this arm the record says `384000`, so the row whose
entire job is *"which question is the one press about to ask"* printed `ok` while naming **714's
arm's bound** for **716's** bytes. Two arms now share `STAGE90_XNU_STORAGE_PROBE=9`, and the narration
quoted only the rung — which is exactly the defect 698's repair was added for (a rung-3 arm narrated
as another arm, row green), one key over: the rung digit is no longer a name.

Three changes, and the third is the one that matters:

1. the paragraph now reads the value out of **the entry record** (`$ARM_CFG`) and writes
   `` `STAGE90_XNU_PWR_WAIT_TICKS=384000`, 20 ms `` — the number comes from the artifact's own record;
2. it says which arm that is: *"this record carries the value 714 PRESSED"* when the value is
   1920000, and otherwise *"**AND THIS ARM IS NOT THE ONE 714 PRESSED** …"* with the two readings the
   bound separates;
3. **a second check branch asserts that the sentence quotes the record's bound**, beside the existing
   "the narration must quote the rung the record carries" check and *not* folded into it (two keys,
   two failures, two sentences — an `elif` would report the rung's sentence for the bound's defect).

The check is falsifiable by exactly the edit it guards against, and that was **measured rather than
asserted**: with the literal `1920000` put back into the sentence, the run returns
`FAIL the arm is named by a reading … says STAGE90_XNU_PWR_WAIT_TICKS=384000 and the sentence above
writes no STAGE90_XNU_PWR_WAIT_TICKS=384000` and exit 1; restored, 5/5 exit 0. The tool's own
existing name check (56 names / 2 globs before this step, 58 / 2 after) could not have caught it —
that check compares *names* against the strings the image publishes and has nothing to say about a
value.

## 5. What the arm is not

The same limits as rung 9, unchanged and unchangeable by this step: no command, no sector, no
partition table, no mount, no device store beyond rung 7's power byte, and the write set is still
`_rst_stores=1` plus the four inherited `_writes=4`. **The goal is still not met and this arm does not
move it** — it is a reading about a wait, not about the medium. **TWRP-to-storage stays withheld.**
