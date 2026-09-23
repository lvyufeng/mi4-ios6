# 579: the seam's own body, read out of the image and not out of the record

**(Numbering: this was drafted as 577. The peer session's build-side clause — "the arm cannot write the
slot, and the build now says so" (`750d006`) — took that number at `01:55`, while this section was being
rehearsed, and it is the narrower half of the same finding. This document was then renumbered to 578, and
to **579** when the peer's pre-registration for the arm *after* this one (`447ffdd`, "the operation's two
halves, and the routine whose name lies") took 578 in the same window. The number and the filename moved;
nothing else did. The three landed documents are complementary on purpose, see §6.)**

**This is a gate change only.** No image, no `out/`, no rebuild, no device, no arm: the record `out/`
carries is the `01:54` one, the entry image in it is unchanged, the device has been off the bus since
535's boot, and nothing here touches `fastboot`, `adb`, or storage. It is filed as an experiment because
it is the first clause in this gate whose subject is a **compiled body read out of the ELF**, and because
the reason it is written the way it is (a call, not a census) is itself a measurement.

## 1. What the gate could read before: the record alone

Two clauses already decide which arm the boot is about to run:

- `:519-546` **narrates** the arm from `xnu_arm_entry-config.txt` — a text file the *build* writes — and
  every sentence of that narration is a claim about `entry_seam_flush`'s compiled body: "the interception
  IS in this image and 535's operation is NOT" for one arm, "the operation is Apple's own
  `FlushPoC_DcacheRegion` over the slot's eight bytes" for the other.
- `:464-465` (575) **refuses** the pair `SEAM_POC=1` *and* `SEAM_MEASURE=1`, since `SEAM_ON =
  SEAM_POC | SEAM_MEASURE` (`build_entry.sh:429`) puts the `--wrap` in the link for either switch and
  `entry_trace.c:437-438` `#error`s on both at once, so that record describes an image that cannot exist.

Both read the same text file. The gate's only artifact-facing statement about the seam is the
record-to-bin clause, which binds the record to the entry image **by hash** — and a hash says nothing
about what is *in* the artifact. A record can be internally consistent and describe an image that is not
the one in `out/`. The property that actually licenses this boot (the operation is absent from the
measurement arm and present in 535's) was checked in two places: by the build, where it runs, and in
574's prose table. 575's own lesson is that a property enforced only where the build runs is unchecked
everywhere the build does not run — and the gate is the place a person reads immediately before spending
the one window.

So the section reads that one property out of the ELF. It is the third place it is answered and the only
one at gate time. It is not a replacement for 575's pair clause: that clause reads the record's two keys
against each other, this one reads the record against the artifact, and neither is total alone.

## 2. The property, and why the stop is on a call and not on a census

Measured on the three bodies that exist, by `arm-none-eabi-objdump -d --no-show-raw-insn` (the gate's own
`$_GATE_OD`), scoped to the `entry_seam_flush` label. The tool matters and is not interchangeable: the
host's plain `/usr/bin/objdump` is built without the ARM target and answers
`can't disassemble for architecture UNKNOWN!`, which is why the gate names a decoder that may be absent
and has an UNREAD branch for it rather than assuming one.

| | `out/` (574's arm) | 535's parked ELF | 533's parked ELF |
|---|---|---|---|
| `entry_seam_flush` present | yes | yes | **no** |
| instruction lines | **115** (`0x1CC`) | **118** (`0x1D8`) | — |
| stores (`str` + `strd`) | 8 (5 + 3) | 10 (7 + 3) | — |
| coprocessor class (`mrc`) | 1 | 1 | — |
| `mcr` alone | 0 | 0 | — |
| loads (`ldr` + `ldrd` + `pop`) | 24 (14 + 9 + 1) | 24 | — |
| `bl FlushPoU_Dcache` | 1 | 1 | — |
| `bl FlushPoC_DcacheRegion` | **0** | **1** | — |
| `dsb` | 1 | 3 | — |

The instruction counts check against 574's own byte figures: `0x1CC = 115 x 4`, `0x1D8 = 118 x 4`. The
two bodies are built from the **same eighteen mnemonics**, and the total `bl` count is **14 in both** —
so no mnemonic census separates them; what separates them is the `bl`'s **target**. `out/`'s body calls
`entry_live_ready` x3, `entry_live_write` x10 and `FlushPoU_Dcache` x1; 535's calls the first two at 3 and
**9** and then `FlushPoC_DcacheRegion` x1 and `FlushPoU_Dcache` x1. Five counts do move between the
bodies — one fewer `movw`, one fewer `movt`, one more `mov`, two more `str`, two more `dsb`, and
115 + 3 = 118 — and two of those are the operation's own, which is why 577's build clause can assert the
store property from the body at all. But a count is a number this arm and an arm that open-coded the same
opcode would share, while the callee names the arm. The site, verbatim from `diff` of the two normalised
bodies:

```
 dsb	sy
-ldr	r9, [r0]           # the two words read before the interception, out of r0
-ldr	r8, [r0, #4]
+ldr	r7, [r0]
+ldr	r6, [r0, #4]
 bl	80045874 <FlushPoU_Dcache>
-ldr	r7, [r5]           # and the two read after it, out of r5
-ldr	r6, [r5, #4]
+mov	r1, #8             # the operation: FlushPoC_DcacheRegion(r5, 8) - one whole line
+mov	r0, r5
+bl	8004589c <FlushPoC_DcacheRegion>
+dsb	sy
+ldr	r9, [r5]
+ldr	r8, [r5, #4]
+str	r7, [r5]           # and the two pre-read words restored
+str	r6, [r5, #4]
+dsb	sy
```

That is why the stop is `_POC_N` (`bl ... <FlushPoC_DcacheRegion>`), and it is also why the counts are
printed but called a reading:

- **The census claims in the docs are pattern answers, not class answers.** 574 §2's table gives the
  measurement body "**14 loads, 0 coprocessor instructions**, 0 calls to `FlushPoC_DcacheRegion`" and
  prints the test beside the second number: `$3 ~ /^mcr/`. Measured on both parked bodies, `^mcr` finds
  **0** while `^mrc` finds **1** — the body carries `entry_sctlr`'s `15, 0, sl, cr1, cr0, {0}`, a *read*
  of SCTLR and not a cache operation. The published number is the answer to `mcr`, and the words around
  it are "coprocessor instructions". The 14 is likewise the `ldr` subset of 24. Neither is a false
  witness to the seam — the arm's safety does not rest on them — but a claim phrased as a census is one
  a census refutes, and this gate's check must not be one.
- **A `bl` census would miss a call this seam does not divert.** Both bodies reach the real routine
  **two** ways — measured on both: a tail branch (`8047ca2c: b 80045874 <FlushPoU_Dcache>`, past the
  wrapper's own `add sp, sp, #32` epilogue) and a `bl` (`8047ca58`) — at the *same* two addresses in
  either body, so this is not a property of 535's arm. Under `--wrap` the callee is renamed and the call
  the seam passes through can be a tail branch; `_n_call` counts `bl` only, so the printed
  `FlushPoU_Dcache=1` is a count of the `bl` form, and the ok-line says so rather than calling it "Apple's
  flush as the exit calls it" — which is what an earlier draft of this section said, and the disassembly
  of both bodies is what corrected it.
- **And the check itself is over a name, which is its limit.** A body that reached the same opcode
  another way — calling something else that cleans, or open-coding the `mcr` — would carry no
  `bl FlushPoC_DcacheRegion` and would read here as though no operation were behind the seam. Nothing in
  this image does that (measured: the body's `bl` targets are exactly the four names above), and 578 §3
  assigns that hazard to the *next* arm's own build clause, where it can be asserted by construction
  rather than read by name after the fact. It is written down here because the clause's finding is a
  statement about a *callee*, not about the cache's state.

The callee check is also the same pairing the build already asserts by construction
(`build_entry.sh`'s `__builtin_unreachable()` side of the `SEAM_MEASURE` switch), which is what makes it
the right thing to read back: a switch→body pairing asserted at build time and read back at gate time is
one property with two readers, not two properties.

## 3. The clause, site by site

`stages/stage90/preflight_boot_check.sh`, section `== the seam's own body, read out of this image and not
out of the record ==`, `:1915-2024` (110 lines, the last section in the file):

| line | what it is |
|---|---|
| `:1916-1932` | the commented paragraph: what the gate read before (the record), and that this is the same seam 575's pair clause refuses a record for — "that clause reads the record's two keys against each other, this one reads the record against the artifact, and neither is total alone (a record can be internally consistent and wrong)" |
| `:1933-1947` | the commented paragraph on why the callee and not a census, with the `mcr`/`mrc` measurement and the tail-branch distinction; ends with the **four** UNREAD branches (three of them properties of *this shell*, the fourth a parse that did not fit) and states that only the four record-versus-body disagreements stop the gate |
| `:1948-1952` | `_SEAM_FN=entry_seam_flush`, and four helpers: `_seam_stream` (one disassembly into a string), `_n_instr`, `_n_mnem`, `_n_call` |
| `:1953-1956` | **UNREAD** — no `$ENTRY_ELF`: "the arm narrated above rests on the record alone. A record is not the artifact" |
| `:1957-1960` | **UNREAD** — `$_GATE_OD` not on PATH: "a property of *this shell* and not of the image (549's shape)" |
| `:1962-1971` | the stream read once, `_NAME_N`, the awk-scoped `_BODY`, `_BODY_N`, and `_REC_SEAM = SEAM_POC or SEAM_MEASURE` — the **OR**, matching `build_entry.sh:429`, because `SEAM_POC=0` no longer means "no interception" (575) |
| `:1972-1975` | **UNREAD** — the decoder printed nothing: 549's decoder that answers by printing nothing, "a failure to read the image, not a finding about it" |
| `:1976-1980` | **UNREAD** — `entry_seam_flush` is named in a full disassembly and this parse extracted no body: "a parse that did not fit its own shape, not an absent seam … reported as unusable rather than as a zero" |
| `:1981-1982` | **finding** — the record names a seam and the image has no such symbol: the record names an arm whose body is not in this image, and "a record written beside the wrong image passes that clause and stops here instead" |
| `:1983-1984` | **finding** — the record names no seam and the image carries one: the "NO interception of this image's own" paragraph would be printed over an image that has one, and that paragraph's whole content is that the flush inside the real exit is Apple's own |
| `:1985-1991` | ok — both are zero: "**That the image carries no seam at all is measured here rather than inferred from those two keys**", while *which* unseamed arm it is still rests on the other keys |
| `:1993-2005` | the four counts, and the census echo, which ends: "Those counts are a reading and not the check: the check is on the call that names the operation, because the two bodies are built from the same mnemonics and the same `bl` count - what differs between them is which routine a `bl` targets" |
| `:2005-2013` | `SEAM_MEASURE=1`: **finding** if `_POC_N != 0` — "the record says 535's operation is NOT behind this seam and the image says it is … that rule applied to a run of *this* image would invert the meaning of the very pair the arm exists to produce: the run would come back and be read as the other arm, which is the worst of the four states this clause tells apart"; otherwise the ok line |
| `:2014-2021` | `SEAM_POC=1`: **finding** if `_POC_N < 1` — "a boot of this image would spend the window on the arm that was already spent, and its log would be read against 535's expectations instead of the inverted rule the measurement arm is worth"; otherwise the ok line |

Every refusal and every UNREAD names the test (disassemble it by hand, or rebuild the entry image with
the switch this arm really needs) rather than presuming which of the two sides is stale, and every
refusal ends with 575's sentence: **nothing is rebuilt by this refusal, and nothing should be** — the
frozen pair embeds this entry image.

Two prose corrections ride along in the narration above, and they are the ones this section is the
pointer for: the `SEAM_MEASURE=1` branch no longer says the arm makes "no store of any kind" (it makes
no store **to the slot**, and the census below counts its eight), and it now says what the reading rule
needs to be applied at all — a log with no `xnu_live_seam_*` keys has no `b`/`a` pair, so the inverted
rule is **not applied**, rather than read as "working as designed" for the want of an unequal pair.

Two further edits are this section's own prose, and both were made because a measurement refuted the
draft: the comment block's count of the UNREAD branches said *three* where the code has four (three are
properties of the shell, the fourth is a parse that did not fit — the sentence enumerated the three and
then introduced the fourth under the same heading), and the census echo claimed the two arms "share every
mnemonic here", which the mnemonic census supports and the callee census refutes: the shared thing is the
mnemonic **set** and the `bl` **count**, and the difference is the target. Both are prose, both are in
comments or echo text, and neither is checkable by anything — which is the same class of defect 570 §2 and
575 §2 recorded four and seven times over, and the reason the numbers in this file are re-read rather than
trusted.

## 4. Proof

```
bash -n stages/stage90/preflight_boot_check.sh                       clean
git diff --numstat -- stages/stage90/preflight_boot_check.sh         125  3
```

The region was re-extracted after every edit — including the last two prose edits — and compared byte for
byte with a fresh `sed` of the file before the rehearsals that quote it (`cmp` clean, `:1915-2024` = 110
lines), because this file's regions are read by line number.

**Ten states of the new section, driven through a harness that supplies only what the surrounding
script supplies (`ENTRY_CFG`, `ENTRY_ELF`, `_GATE_OD`, `V_SEAM_POC`, `V_SEAM_MEASURE`, `FLUSH_WRAP`,
`fail`):**

| # | record | image | exit | what it printed |
|---|---|---|---|---|
| M1 | `0` / `1` | the live entry ELF | **0** | the census (115/8/1/24, `FlushPoC_DcacheRegion=0`) + "ok: the measurement arm's record and its body agree" |
| M2 | `1` / `0` | 535's parked ELF | **0** | the census (118/10/1/24, `FlushPoC_DcacheRegion=1`) + "ok: 535's arm's record and its body agree" |
| M3 | `1` / `0` | 533's parked ELF (no such symbol) | **1** | "has no `entry_seam_flush` symbol in it at all … the record names an arm whose body is not in this image" |
| M4 | `0` / `1` | 535's parked ELF | **1** | "the record says 535's operation is NOT behind this seam and the image says it is … the run would come back and be read as the other arm" |
| M5 | `0` / `1` | `/nonexistent/…` | **0** | UNREAD — no ELF |
| M6 | `0` / `1` | the live entry ELF, `_GATE_OD=objdump-not-installed` | **0** | UNREAD — no decoder on PATH |
| M7 | `0` / `1` | the live entry ELF, a decoder that prints nothing | **0** | UNREAD — 549's decoder |
| M8 | `0` / `1` | a full disassembly that names the symbol only as a call target | **0** | UNREAD — named, no body extracted |
| M9 | `0` / `0` | 533's parked ELF | **0** | "no seam in the record and none in the image … measured here rather than inferred from those two keys" |
| M10 | `0` / `0` | the live entry ELF (carries the seam) | **1** | "records no seam … and … carries `entry_seam_flush`" |

Both ok branches, both UNREAD pairs, all four findings, and the both-zero reading were observed, each in
a run where the others cannot fire (M1/M2/M3/M4/M10 differ in the record, the image, or both, and
M5-M8 differ in *this shell* rather than in the inputs).

**M8's first form is the defect the rehearsal caught**, and the final file was re-measured with it as an
eleventh state rather than left as a story: written as a well-formed disassembly with a two-instruction
body under the symbol — which is *not* a parse failure at all — the clause read it, counted 2
instructions, and printed

```
  entry_seam_flush, read out of this image: 2 instructions, 0 stores, 0 coprocessor
  instruction(s), 0 loads; it calls FlushPoU_Dcache=0 and FlushPoC_DcacheRegion=0. …
  ok: the measurement arm's record and its body agree - the interception is in the link
  (FLUSH_WRAP=4) and entry_seam_flush calls FlushPoC_DcacheRegion 0 times …
```

with exit **0**. The reworked M8 makes the name appear only as a call target. What that leaves uncovered
is stated rather than papered over: **a decoder that answers wrongly is not covered by any UNREAD
branch** — §2's last bullet is the same hole seen from the other side — and the clause's trust boundary
is `$_GATE_OD`, which is one more reason the record-to-bin hash binding stays and the build asserts the
same property.

**Four record states through the whole gate**, run against one frozen instant of `out/` (record
`01:54:18`, manifest `01:54:19`, all 20 source files verified matching it, bin `151425c4…` = the record's
hash, ELF `3bc72605…`) rather than against the live tree — see the defect note below. The tree the gate
ran in is a copy of the repo with `out/stage90/{xnu_arm_entry.bin,.elf,-sources.txt}` pointed at that
snapshot, so a peer's build finishing mid-rehearsal cannot move what is being read:

| case | record | gate exit | where it stopped |
|---|---|---|---|
| A | the real one, `SEAM_POC=0` / `SEAM_MEASURE=1` | **0** | nowhere; 23 sections, sources clause green ("20 file(s), every one matching the manifest, and none the manifest does not name"), and this section's census and ok line are the **last** section printed |
| B | `SEAM_POC=1` / `SEAM_MEASURE=0` over the measurement ELF | **1** | this section (`:2016`), the direction that would spend the window on the spent arm |
| C | both `0` over the seam-carrying ELF | **1** | this section (`:1984`), "records no seam … and … carries `entry_seam_flush`" |
| D | both `1` | **1** | **575's pair clause** (`:464`), upstream of this section — as designed: the impossible record is refused before any narration of an arm |

**A defect in the rehearsal itself, the second one worth recording.** The first whole-gate runs refused
case A at `== the entry image's own sources ==`, not at anything in this change: the rehearsal tree had
been pinned to the `build_entry.sh` the manifest was written with earlier (`db338ff3…`), while the peer's
tree — and the manifest the live `out/` carries — had moved to `ffee5f5d…`. The repair was not a new pin
but a better rehearsal: **rehearse against a snapshot of one instant** (record, manifest, bin and ELF
copied together, source tree copied whole) instead of against a pinned commit plus a live `out/`. A
rehearsal that can be invalidated by someone else's build finishing is not evidence about the artifact.

**The live gate**, re-run on the live tree with the final file: `EXIT=0`, `0 UNREAD`, 23 sections, the
sources clause green, and this section printed **last** (output line 457) —

```
  entry_seam_flush, read out of this image: 115 instructions, 8 stores, 1 coprocessor
  instruction(s), 24 loads; it calls FlushPoU_Dcache=1 and FlushPoC_DcacheRegion=0
```

— followed by the ok line, and then `image: …/stage90-qcdt.img` / `booted, never flashed, so no outcome
of this run can write to storage.` Nothing was written and no device was touched: the gate's own last two
lines, on its own account, are the check that this is a run of a *record reader* and not of the payload.

## 5. The live gate, read by section

`EXIT=0` with **0 UNREAD** is the whole gate, and this section is the last one it prints (`:457` of the
output, with only the image/booted pair after it), so the green here is a reading of this change rather
than an inference from a green elsewhere (570 §5's rule, in its easier direction). The record it read is
the `01:54` one, which the peer's rebuild rewrote with the same switches, so both the record and the
artifact are the ones this change was rehearsed against — `entry 151425c4…` / `elf 3bc72605…` /
`qcdt 914f45ac…`, the peer's letter, my snapshot and the live tree agreeing on all three, re-read from the
live tree at the moment of writing rather than carried forward.

## 6. State

- **The arm is unchanged and still unrun.** The one boot window 557 measured is still unspent, and the
  device has been off the bus since 535's boot — no `usb 3-10` enumeration — so the next run is still
  the user's power press to spend. Nothing here touched the device: no `fastboot`, no `adb`, no `flash`,
  nothing written to storage.
- **Not a duplicate of 577 or 578, on purpose.** 577 makes the *build* assert the property 574 §2 could
  not: no store to the slot, identified from the body itself, refusing three ways including "no slot
  register identified at all". 578 pre-registers the *next* arm, and measures the hazard this clause
  cannot see (a routine whose name lies about which half it does). This clause is the **gate-time** reader
  of the record-versus-artifact question, and the three cover different failures: the build stops an image
  carrying the store from ever existing; the gate stops a *record* whose description is not the image in
  `out/` from licensing a run; the pre-registration decides the next arm's shape before its run comes
  back. All three places, because the property is one and the readers are only total together.
- **TWRP-to-storage stays withheld.** `如果os已经能进去了的话` is the same unmet clause as
  `起码要能进入操作系统`: the boot reaches pid 1 and does not survive the idle pass, and 535 moved a
  returning death into a non-returning one, which is further from the goal, not closer to it.
