# 571: 535 is built - the seam is hooked, the arm is a pure addition, and its own build blinded two derivations

535 is built and verified host-side, and it has not been run: this is the **pre-registration**, written
after the build and before the one `fastboot boot` it costs, so the arm's readings are fixed before any
of them exists. 533's pair is spent and parked (`out/stage90/captures/533-*`), 568 read its log, and
565 section 1's ordering - run, read, *then* build - has been satisfied rather than skipped.

Two things came out of the step that are not about the arm's hypothesis and are worth the numbering on
their own: the build is provably a **pure addition** to the frozen image (section 3, because this
project's memory says a 590 KB diff must be explained and not accepted), and the arm's own
`--wrap` **silently blinded two derivations of the address the run is read against** - one in the
runner (fixed here, section 5) and one in the gate (owed to the peer session, section 6).

## 1. What the arm is, read out of the built image rather than described

| what | value | where it comes from |
| --- | --- | --- |
| switch | `STAGE90_XNU_SEAM_POC=1` | the thirteenth key in the entry record |
| the seam | `platform_cache_idle_exit`'s own `bl FlushPoU_Dcache`, `0x800462d8` → returns `0x800462dc` | 546 section 4; the gate derives the same address from this ELF |
| the interception | `--wrap=FlushPoU_Dcache`, so all **4** call sites of that routine enter `__wrap_FlushPoU_Dcache` (`0x8047cb9c`, 12 B) | the build clause counts 4 redirected and **0** left direct |
| the body | `entry_seam_flush` (`0x8047c9c4`, 0x1D8 = 472 B) | new text, `noinline` so its call is in the image and countable |
| the filter | `lr != 0x800462dc` → straight through to `__real_FlushPoU_Dcache()`, counted | 546 section 4's four-caller problem |
| the operation | `FlushPoC_DcacheRegion(slot, 8)` = Apple's `Clean and Invalidate d-cache region to PoC`, `0x8004589c` | the build clause reads its body as 1 x `cr7,cr14,{1}` with 0 clean-by-MVA and 0 invalidate-by-MVA |

The wrapper is a `naked` three-instruction trampoline (`mov r0, sp` / `mov r1, lr` / `b
entry_seam_flush`) because at the `bl` **the stack pointer is the slot**: `push {fp, lr}` at
`0x800462d4` wrote the two words at `[sp, sp+8)` and the `pop {fp, pc}` at `0x8004633c` reads them back
with no `sp` change between (546 section 1). A compiler prologue would push below `sp` and lose the
address; `b` rather than `bl` means the arm adds no return address of its own.

The four steps, each named with what it is for, are in `entry_trace.c:2039-2091` and are not repeated
here. In one line: `dsb` then the two words **as memory holds them** (`_b0`/`_b1`), Apple's own
PoC-flush-by-operand next (unchanged), then the PoC clean-and-invalidate of the slot's eight bytes, the
same two words again (`_a0`/`_a1`), then those two words **restored** from the first reading.

**Why a region clean-and-invalidate with a restore, rather than the whole-cache PoC flush 546 section 4
named.** The window's stores are DRAM-only (`SCTLR.C` is clear throughout it), so a whole-cache *clean*
would write the cache's stale copies over **every** address the window wrote - the per-CPU fields the
enter zeroes and every frame the window pushes, not just the slot. A whole-cache *invalidate* is worse
the other way: it discards dirty lines whose memory copy is older, and the slot's line is exactly such
a line for its neighbours. An MVA operation is defined to the Point of Coherency, so it reaches
whichever levels hold the line without needing XNU's operand-encoded level or a geometry from
`proc_reg.h` (the two questions 544/545 leave open); its clean half writes the neighbours back before
discarding the line, and the one address whose cache copy is *newer* than memory - the slot the window
just pushed - is restored by step 4. It writes no control register, so 522's hazard (`SCTLR.C` outside
the coherency domain, the two cells that produced no log) is not available to this arm whatever else it
does.

## 2. The switch is the interception, not a flag in an always-linked hook

`STAGE90_XNU_SEAM_POC=0` means **no wrapper at all**: `build_entry.sh` adds `--wrap=FlushPoU_Dcache` and
compiles `entry_trace.c`'s block only when the switch and the trace are both on. So a build with the
switch off cannot show the arm's functions in the image and cannot be read as "the arm ran and did
nothing" - the pair of clauses that check the wrapper's presence in both directions is what makes that
structural rather than a comment.

## 3. The build is a pure addition, measured (the 590 KB that had to be explained)

533's image and 535's are the same size (5519996 B) and differ in **589,959 bytes (172,034 words)**,
which is far more than 484 bytes of new text and is exactly the shape this project's measurement
defects file says not to accept on trust. It is decomposed and accounted for:

| measurement | value | reading |
| --- | --- | --- |
| new code | `entry_seam_flush` 472 B + `__wrap_FlushPoU_Dcache` 12 B = 484 = **0x1E4**, at `0x8047c9c4` | the arm, at the start of the wrappers' region |
| `.text` | 0x50e5a0 → **0x50e840** (+0x2A0) | 484 B of code + 188 B of re-padding: 484 is not a multiple of the alignment of the objects after it |
| `.data` / `.sysctl_set` / `.init_array` | **identical address and size** | the growth is absorbed by the gap before `.data` at `0x80510000` |
| `.bss` | +0x10 | the arm's three statics (`g_seam_calls`, `g_seam_live`, `g_seam_other`) |
| symbols that moved | 1,225, **all** at or above `0x8047c9c4`, deltas +0x1E4/+0x1E8/+0x1E0/+0x2A0/+0x2AC | the relaid-out entry objects; the mixed values are those re-paddings |
| Apple's own symbols | `panic 0x8003c5ec`, `sleh_abort 0x80454168`, `arm_init 0x8000cff8`, `platform_cache_idle_exit 0x800462d4`, `platform_cache_idle_enter 0x80046238`, `FlushPoU_Dcache 0x80045874`, `FlushPoC_DcacheRegion 0x8004589c` - **byte-identical in both** | no Apple code moved |
| differing words **below** the insertion | 23,747 = 22,648 `movw` immediate halves + 351 `movt` halves + 748 literal-pool address words | every one of them is an absolute address into the moved region: the 748 literals move by exactly +0x1E4, and the `movw`/`movt` pairs resolve to `target + 0x1E4` (e.g. `0x80485f24` → `0x80486108`) |
| the only changed non-code symbols below the insertion | `__entry_text_size`/`__entry_data_fileoff` +0x2A0 (= the new `.text` size) and `__entry_data_filesize`/`__entry_data_size` −0x2A0/−0x290 | the entry image's own geometry records, which *describe* the artifact and are supposed to move |

So the image is a pure addition: new code at one address, the entry's own objects relaid out after it,
and every other difference an address constant pointing into the relaid-out part. **No Apple
instruction and no data content changed.**

## 4. The readings the run will be read by, pre-registered

Every value below is published by the arm itself, and the reader (`run_and_capture.sh`, clause (5))
prints all of them with the three states separated (PASS / FAIL / UNREAD) rather than letting an absent
key read as zero.

| key | prediction for this run | what it decides |
| --- | --- | --- |
| `xnu_live_seam_calls` | **>= 1** (schedule `<=4` then powers of two, like `entry_slot_publish`) | the seam was entered at all. 557 measured this window entered **once** per boot, so a printed 4 or 8 is a *finding*: the arm changed how long the boot lives |
| `xnu_live_seam_lr` | **`0x800462dc`** | the hook was entered at the seam and not at one of the routine's three other callers |
| `xnu_live_seam_other` (+`_other_lr`) | **>= 3** | the filter is measured, not asserted: "the hook never ran" and "it ran and rejected this site" stay different readings (526) |
| `xnu_live_seam_sp` | **= the abort's own `sp`** (`0x8054fed0` on 568's run) | the arm read the *right object*: 546 section 1's slot is the address the `pop` reads |
| `xnu_live_seam_b1` | **a kernel-text address** (546 section 1: the frame's own `lr` names `cpu_idle`) | the window's store reached memory - 546's premise, measured from the near side |
| `xnu_live_seam_a1` | **= this pass's `xnu_live_slot_rtcpre_pop`** (`0x33f1c1b5` on 568's run) | **546 section 3's mechanism seen before the `pop`**: the dirty stale line's copy of the slot's `lr` word is the deadline the exit published, written out by the clean half |
| `_a0 == _b0` and `_a1 == _b1` | *the falsifier* | the line was **not dirty**, so the clean had nothing to write out and the `pop`'s wrong value comes from somewhere this operation does not reach |

**The mechanism prediction and the frontier prediction are separate, and they can come apart:**

| outcome | reading |
| --- | --- |
| the boot **survives the idle pass** (no `sleh_abort` panic, runner exit 0, item (4) says more user-mode progress) | 535's arm retires the epilogue it was built for. The frontier moves for the first time, and the next questions are the *next* idle pass and then storage/TWRP |
| dies at the `pop` (`sleh_lr == 0x800462dc`), and the pair came back **changed** to the `rtcpre_pop` value | the mechanism is confirmed and the repair is **incomplete**: memory was restored and the line discarded, so a `pop` that still reads a stale word says the invalidate did not survive the distance to the `pop` (the four instructions between Apple's re-enable and the `pop` are the obvious next object) |
| dies at the `pop`, pair **unchanged** (`_a0 == _b0`) | 546 section 3's mechanism is **falsified as the whole story** for this cell: the line was clean, so the `pop`'s wrong value came from a level or an object this operation does not reach |
| dies at a **different** `lr` | a new fault. 535's own two stores to the exit's frame slot and the clean's line write-back are two new candidates since 533, and the reader now says so instead of offering 522's wrapper store as the only one |
| no log, no return | the arm hung the boot: the one cell that costs a power press (517's), and the reason this doc exists before the run |

## 5. The runner: clause (5), the arm-naming fix, and the derivation the arm blinded

**Three changes, all in `run_and_capture.sh`, none of them a build input, so no rebuild and the frozen
image is untouched:**

1. **Clause (5)**, a new block that prints the seam's readings and the verdicts in section 4 and
   participates in the block's `verdict_ok`. It is *self-selecting* like the SCTLR-pair block above it
   (they are published by this arm and by no earlier image, and the entry image publishes no build
   marker - clause (2)'s own note), so a log without the keys is a log from an image without the arm
   and silence is the right reading for it.
2. **Naming the image, not just the cell.** The SCTLR pair decides the *cell* (both 533's and 535's
   images leave the window as Apple left it), and the old text printed `ARM 533's arm` for it - which
   since 535 is a *cell* name and not an image name. The arm's own counter is what tells the two apart,
   so the clause now says which image the log is from when `xnu_live_seam_calls` is present, in the
   three places that name it: clause (2), clause (1)'s panic-free branch (where printing "535 must not
   be built as designed" over 535's *success* would score the arm's win as its falsification), and the
   closing `=>` chain.
3. **`exit_pop_lr_addr`'s pattern.** This is the one the arm blinded, and it is a real defect of the
   "one value, two definitions" class: 535 reaches the seam through `--wrap=FlushPoU_Dcache`, so the
   disassembly reads `bl 8047cb9c <__wrap_FlushPoU_Dcache>`, and the pattern
   `/<FlushPoU_Dcache>/` matches **nothing** in that line. Measured on the frozen image: the derivation
   returned empty and `--summarise` printed `*** PINNED LITERAL - the entry ELF could not be read, so
   this criterion is NOT derived ***` - i.e. in the one run whose whole question is that call, the
   criterion stopped being derived from the image. The pattern is now `/<[^<>]*FlushPoU_Dcache>/`: any
   wrapper prefix, because the rule is about the routine and not about the spelling the linker chose.
   Re-measured after the change: `lr criterion: 0x800462dc (derived from …/xnu_arm_entry.elf at run
   time)`, and clause (5)'s `seam_lr` comparison is against the derived address and not a literal.

## 6. The gate's copy of the same derivation is UNREAD, and the boot waits for it

`preflight_boot_check.sh` has its own `_pop_lr_from_elf` with the same literal pattern, and on this
image it takes its `elif [[ -z $DERIVED ]]` branch:

```
== the address run_and_capture.sh's shape test compares against ==
UNREAD - arm-none-eabi-objdump found no 'bl <FlushPoU_Dcache>' inside platform_cache_idle_exit in
…/xnu_arm_entry.elf, so this clause could not derive the address it compares the reader's fallback
against. … the comparison below did NOT happen.
```

The gate still exits **0** - the branch prints UNREAD rather than failing - and that is the point worth
recording: **a green gate whose 556 binding did not run is not the same as a green gate whose binding
held**, and the difference is one `grep` of the output. It was the only UNREAD in the run. The peer
session owns that file; the fix asked for is the same one-line pattern change plus a wrap-aware count
for the clause's `FLUSH_N` sentence (which reads 2 on this image and those two are inside
`entry_seam_flush` - `__real_FlushPoU_Dcache` is an alias for the same address - not the four callers
the sentence is about). **The boot is held until that clause derives on this image**, because 556's
binding is the property this arm's death is read against and the run costs the one window 557 measured.

## 7. The rehearsal, on the real 568 log

Clause (5) was rehearsed the way 566 section 3b rehearsed the previous reader: 568's real capture
(3931 payload lines, real brackets, a fatal episode *and* an earlier unrelated one) with only the
arm's own keys appended, which is exactly what this image adds. Six variants, each in a state where the
others cannot fire, and the measured output:

| variant | what was changed | where it must land | measured |
| --- | --- | --- | --- |
| A | the predicted reading (`b1` a text address, `a1 == rtcpre_pop`, `sp` = the abort's sp) | every PASS, then `STALE LINE, WRITTEN OUT` | as designed |
| B | `a` equal to `b` | `CLEAN LINE` (the section 4 falsifier) | as designed |
| C | 568's log unchanged (no seam keys) | clause (5) silent; clause (2) says the image is the earlier one | as designed |
| D | A with the `sleh_abort` lines removed | clause (1)'s panic-free branch prints `RESULT` (535's success), **not** `FALSIFIER` | as designed |
| E | `seam_lr` = another call site (`0x80046284`) | `FAIL` on the identification, and the block's `verdict_ok` goes 0 | as designed |
| F | `seam_sp` = the wrapper's own `sp` (`0xc819bf68`, from 568's own log) | `FAIL` - the arm's slot is not the `pop`'s slot | as designed |

**What the rehearsal does not cover**, stated so it is not read as coverage: the values in A, B, E and
F are synthetic in everything except the two taken from 568's log (`rtcpre_pop`, the fatal `sp`), and
nothing here exercises the arm itself or the live channel's publish schedule - only the reader's
branches, on the log the run is expected to produce.

## 8. Safety

One non-persistent `fastboot boot` of `out/stage90/stage90-qcdt.img` (`3d8720c4…`), gated by
`preflight_boot_check.sh --allow-xnu-entry` then `run_and_capture.sh --allow-xnu-entry`. `flash` is not
used anywhere; nothing is written to storage; a brick is impossible by construction. The arm writes no
control register, sets no `SCTLR` bit, and its only stores are two words of the caller's own frame slot
through the addresses its `push` used. **TWRP stays withheld**: the goal clause 「如果os已经能进去了的话」
is unmet until a boot survives the idle pass, and 568 section 5 says the last one did not.

## 9. State and what this step owes

- **Built and parked:** `out/stage90/captures/535-*` - entry `12684433…` (5519996 B), elf `4f7d8c28…`,
  qcdt `3d8720c4…` (8540160 B), config `97679b66…`; the record has thirteen keys and
  `STAGE90_XNU_SEAM_POC=1`; 533's and 520's artifacts are untouched beside them.
- **Owed to the peer session:** the gate's `_pop_lr_from_elf` pattern and the `FLUSH_N` sentence
  (section 6). The boot waits for it.
- **Owed at the next build, and deliberately not fixed now:** the seam block's doc comment cites
  `docs/experiments/experiment-569-*.md`, and 569 is this peer's step - this doc is 571. The citation is
  one comment line inside `xnu_arm_boot/`, which the gate hashes, so correcting it is a *rebuild*, and
  565 section 1's ordering is what forbids doing that between the build and the run. It is corrected in
  the follow-up build that this run's reading calls for, and until then the comment names the wrong
  document.
