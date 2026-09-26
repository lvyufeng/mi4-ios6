# 719: the rung-11 arm built and parked — one mechanism, one cell, and a growth in `.text` only

**Date:** 2026-09-26 01:03–01:13 UTC. **Arm:** `armed-storage-pwrwait-unmask-13366e08` —
`STAGE90_XNU_STORAGE_PROBE=10` with **`STAGE90_XNU_PWR_WAIT_TICKS=384000`** (20 ms), i.e. 716's arm
with the CPU's `I` bit cleared around the poll and restored afterwards. Parked in
`out/stage90/frozen/armed-storage-pwrwait-unmask-13366e08/` (11 files,
`tools/verify_revert_set.sh` exit 0) and recorded in `stages/stage90/revert-set.txt` (11 `set=` lines
plus the arm's own narration block). The pre-registration is experiment-718; the press it was built
for is experiment-720.

## 1. What the arm is, in the source

One mechanism in the function rung 9 introduced, guarded so that the rung below still builds its own
arm byte for byte:

```c
} else {
    ST_LIVE("..._reset", 0u);
#if STAGE90_XNU_STORAGE_PROBE >= 10
    mrs cpsr_saved, cpsr          /* the state to restore, read first */
    cpsie i                       /* I only; F is untouched, the mode is untouched */
#endif
    mrs cpsr, cpsr                /* _wait_cpsr - the premise, and the arm's value */
    <the poll, unchanged: the same CTL byte, the same 1024-read batch, the same bound>
#if STAGE90_XNU_STORAGE_PROBE >= 10
    msr cpsr_c, cpsr_saved        /* the mask back on, from the SAVED register */
    mrs cpsr_after, cpsr          /* _wait_cpsr_after - the state given back */
#endif
}
```

In the linked body (`st_pwr_wait`, 0x8000d230, **0x284** bytes — it was 0x250): `0x8000d430 mrs sl,
CPSR` / `0x8000d434 cpsie i` / `0x8000d438 mrs r1, CPSR` … `0x8000d498 msr CPSR_c, sl` / `0x8000d49c
mrs r1, CPSR`. One register carries the saved state through, and the reading that checks the restore
is adjacent to the instruction it is about.

Two source files changed by content, and both are named in the sources manifest:
**`entry_storage.c`** (the mechanism, the new cell, the two `#if` guards, and the ladder `#error`
extended to 10) and **`build_entry.sh`** (the `10)` case and its sentence in the refusal, plus the
comment above it). The entry ELF: `.text` 5,323,240 → **5,323,272 (+32)**, `.data` 206,804 and `.bss`
379,784 **unchanged**, and the whole symbol-name set identical — so no function and no word moved
except the ones the growth pushed.

## 2. The build refused the first invocation, and this time the refusal was in the shell

`build_entry.sh` carries its own rung ladder (`case "$STORAGE_PROBE" in 0) … 9) ;;`), and the first
invocation was refused by it:

```
STAGE90_XNU_STORAGE_PROBE must be 0, 1, 2, 3, 4, 5, 6, 7, 8 or 9, not [10]
```

— which is the ladder's third spelling: the rung digit is a `#if` in `entry_storage.c`, a `case` in
`build_entry.sh`, and (from rung 9) a line in the payload's switch record. The source's own `#error`
and the shell's `case` are both needed, and neither substitutes for the other: the shell's refusal
fires before anything is written, and the source's covers a build that reaches the compiler another
way. The second invocation refused on `STAGE90_ENTRY_TRACE`, because the arm's switches are part of
the invocation and `TRACE=1` is not the default — the same shape 716 §3 recorded for its own build.

The arm was built with the 18-key switch set the parked rung-10 arm carries, plus the new value:
`STAGE90_ENTRY_ARM_CHANGE=1`, `TRACE=1`, `REAL_ARM_INIT=1`, `SLOT_NULL=1`, `ISTACK_SEPARATE=0`,
`IDLE_STACK=1`, `SEAM_POC=1`, `POST_END_TICKS=115200000`, **`STORAGE_PROBE=10`**,
`PWR_WAIT_TICKS=384000`. Then the payload, in one build of its own:
`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh` (m721: one build at a time, and no build
while a firer is armed — none was).

## 3. The containment, measured against the pressed rung-10 arm

`armed-storage-pwrwait-aa2b051d` is the arm below this one; every count below is a byte comparison of
the two sets.

| file | differing bytes | where |
|---|---|---|
| `xnu_arm_entry.bin` | 556,511 | the whole region the code moved through — this is a **code** arm, not a constant arm, so the growth shifts everything after 0x8000d230 |
| `stage90.bin` | 556,511 | **all of them inside `stage90_xnu_entry_blob`** (5,536,380 B at file offset 494,236) — **ZERO differing bytes outside the blob** |
| `stage90.img` | 556,531 | those 556,511 + **exactly the 20 bytes at 576–595**, the boot header's legacy-AOSP `id` field |
| `stage90-qcdt.img` | 556,531 | the same 20 in the header and the same 556,511 in the blob, and **nowhere else**; the 2,521,088-byte device-tree tail is byte-identical and `dt_size` at 40 is unchanged; size unchanged at 8,556,544 |
| `stage90.elf` | 556,511 | all inside the embedded blob; `.text` 6,030,666 / `.data` 1,208 / `.bss` 644,048 **all unchanged** and **`nm -S` equals the pressed arm's output as a whole**, `stage90_xnu_entry_blob` still at VA 0x80a9c size 0x547a7c |
| `xnu_arm_entry.elf` | — | same file size (6,715,636); `st_pwr_wait` 0x8000d230 size **0x284** (was 0x250), `entry_storage_probe` 0x8000d480 → **0x8000d4b4** with its own size unchanged at 0x173c, `st_pwr_irq` unchanged at 0x8000cff8 size 0x238 |
| `xnu_arm_entry-config.txt` | **2 lines** | the artifact SHA256 field and `STAGE90_XNU_STORAGE_PROBE=9` → `=10`; the file keeps its 910 bytes |
| `xnu_arm_entry-sources.txt` | **4 lines** | the SHA256 field and the two files that changed by content (`build_entry.sh` 2ea248de→d5521cfe, `entry_storage.c` 17ec162d→f2f30db6); 28 lines / 23 file lines, file keeps its 2,348 bytes |
| `stage90-build-config.txt`, `stage90_fixture.macho` | 0 | byte-identical, as on every arm since 653 |

**The id field is a function of the payload, reproduced from both files rather than quoted**:
`sha1(payload ‖ pack('<III', 6031876, 0, 0))` equals the 20 bytes at 576–595 on this arm
(`9717a9e1…`) and on the pressed one (`d444ee40…`), so the field changed for the reason it must and
for no other.

**ZERO BYTES OUTSIDE THE BLOB, AND THE REASON IS STRUCTURAL.** 713's arm had four, because its entry
image **grew its `.bss`** and the payload's own copy of the entry layout — `BSS_END` and the BSS
length, two `mov`/`movt` pairs in the payload's head — had to move with it. This arm's growth is in
`.text` only (32 bytes), and those two constants are functions of `.bss` alone, so they do not move.
It is the tighter of the two containments *and it cost more bytes inside the blob*, which is the point
worth keeping: a code arm cannot be small, and what a containment claim is about is the region
**outside** the thing that changed.

**The seam did not move**: `platform_cache_idle_exit` is 0x800482d4 and `STAGE90_XNU_SEAM_LR`
0x800482dc in both arms, so `entry_trace.c`'s constant and `run_and_capture.sh`'s literal were **not**
re-pinned — the fourth rung running for which that is true (the owed list still carries the note that
the seam address is pinned in two files).

## 4. No build clause needed a change, and that is a fact about the classifier

The rung-9 clause over `st_pwr_wait`'s body ran on this arm unchanged and printed:

```
xnu_entry_712: st_pwr_wait's device accesses are [f982492c:ldrh f98240e8:ldrb] with counts
[f982492c:ldrh=2 f98240e8:ldrb=4], its non-device accesses are [IMG:ldr IMG:str] at
[805541a8:ldr 805541ac:ldr 805541b0:str 805541b0:ldr 805541a4:ldr], entry_storage_probe calls it
1 time(s), and its budget 384000 is carried by the movw/movt pair in its own body
```

— the same two device addresses at the same widths, the same `ldr`/`str` pair, **the same four `.bss`
words**, the same single call, the same budget. There are two reasons and both are worth naming,
because the next arm that adds a cell will want to know which it is:

1. **`cpsie`/`msr`/`mrs` are not memory accesses.** The classifier reads loads and stores; a cp15
   transfer has no memory operand, so the mechanism adds nothing to either set.
2. **A published cell is a CALL to `entry_live_write` with a string pointer.** The four `.bss` words
   the clause resolves are the ones this body touches *directly* (`g_pwr_irq_calls`, `g_pwr_curr_state`,
   `g_pwr_curr_io`, `g_pwr_irq_done`); `_wait_cpsr_after` is a `bl` with a `.rodata` string. So a new
   cell changes no census, no waiver and no clause.

The clause's success echo still says "the `movw`/`movt` pair" while the pair is `mov` + `movt` on this
arm too — m724, recorded in 716 §3 and unchanged, because it is a print and not a comparison.

## 5. The readiness narration, and the name check that came free

`tools/verify_press_ready.sh` gained a rung-10 arm paragraph (the mechanism, why 717's measurement
made it the only shape left in this fixture, and the cells it flips) and a rung-10 consequence block
(the premise pair, the outcome pair, the flag/ack comparison, the negative below the CPSR, and the
hazard that is the *context* rather than the bound); `$wst == 10` was added to the list that carries
the rung-1 hazard paragraph, and the bound check already applied because it is keyed on the value.

**The narration's own `xnu_live_*` name check verified the new cell for free**: it reads every `_name`
the prose writes against the strings the entry image publishes, and its line on this arm reads *"the
43 name(s) and 1 glob(s) it writes were read against out/stage90/xnu_arm_entry.elf's own key strings,
and every name is a suffix of an `xnu_live_` string this image publishes"*. `_wait_cpsr_after` is one
of them, so the readiness row is also the check that the new cell reached the artifact — which is the
one thing a build clause could not have told us here, because a published cell is invisible to the
census (§4).

## 6. A count with two definitions, found while writing the record (m-arm, recorded not repaired)

The rung-10 record says the entry ELF "has 26,540 symbols"; `nm` on the same parked file prints
**26,636 lines**. Both are right: 26,540 is the count of **distinct names**, 26,636 the count of `nm`
**entries**, and the 96 in between are duplicated names (`.LANCHOR0` appears 7 times;
`L_telemetry_needs_record`, `L_kdebug_enable`, `L_intstack_top` and `L_gVirtBase` 6 times each). Both
arms measure 26,540 distinct / 26,636 entries, so **no symbol was added or lost this step** — and a
reader comparing the new count against the recorded one would have read a 96-symbol jump that is not
there. The defect is not the number; it is that the record did not say which of the two it was.

## 7. What the arm is not

The same limits as rungs 9 and 10, unchanged: no command, no sector, no partition table, no mount, no
new device store (the write set is still `_rst_stores=1` plus the four inherited `_writes=4` plus rung
7's power byte), and not one byte written to the medium. `cpsie i` is a mask coming off for a bounded
spin and **not a sleep**: the three arms the vendor's threaded handler exists for
(`sdhci_msm_setup_vreg`, `setup_pins`, `set_vdd_io_vol`) remain absent, and this image still has no
thread to yield to.

**The goal is still not met and this arm does not move it** — it is a reading about a wait, not about
the medium. **TWRP-to-storage stays withheld.**
