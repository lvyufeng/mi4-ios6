# 573: the clause that was quiet on the image it exists to bind

`stages/stage90/preflight_boot_check.sh`'s 556 clause compares the address `run_and_capture.sh` attributes
a pop death by against the address the exit's own `bl FlushPoU_Dcache` returns to, derived from
`out/stage90/xnu_arm_entry.elf`. On the image 535 built it derived **nothing** and said so in a line that
starts `UNREAD` — and the gate **exited 0**. A clause whose whole job is to bind the reader's criterion to
this image compared nothing, and the exit code did not say so.

**This is a gate change only, plus one hard stop.** No image, no `out/`, no device, no arm. 535's pair
stays spent and parked. It is filed as an experiment because the *next* arm reaches the same call through
the same wrapper (572 section 7 recorded this clause as owed for exactly that reason), and because the
defect is this project's own recurring shape in its purest form: a check that goes quiet, on a green run.

## 1. The callee's spelling is a property of the link, not of the routine

`--wrap=FlushPoU_Dcache` rewrites every call to that routine into a call to `__wrap_FlushPoU_Dcache`. So
in a wrapped link the exit's call disassembles as

```
800462d8:	bl	8047cb9c <__wrap_FlushPoU_Dcache>
```

and the string `<FlushPoU_Dcache>` is **not in that line**. The derivation's rule was
`infn && /<FlushPoU_Dcache>/ && /bl/`, so it matched no line inside `platform_cache_idle_exit`, `DERIVED`
came out empty, and the clause took its `-z $DERIVED` branch: print `UNREAD`, exit 0. That is 569's and
549's shape one layer up — *silence is a reading only if success is silent*, and here success was the
printed `ok:` line while the failure printed a line whose first word reads like a note.

Measured on the frozen 535 image (`out/stage90/xnu_arm_entry.elf`, entry `12684433…`, `5519996` bytes):

| pattern | count | what it is |
|---|---|---|
| `bl <FlushPoU_Dcache>` | **1** | `8047ca58`, the **wrapper's own body** tail call to `__real_FlushPoU_Dcache` |
| `bl <__wrap_FlushPoU_Dcache>` | **4** | the four callers: `0x80045d08`, `0x80046284`, `0x800462d8`, `0x800463bc` |
| any wrapper prefix | **5** | the union of the two above |

The first row is the one to be careful with. The count this clause used to print was **1**, not the
callers, and it never printed at all on this image because the *derivation* rule needed the same string;
the number was the wrapper's own body and would have been read as "the call sites" by anyone who saw it.
A single count with one spelling is one value with two definitions (558): "sites that reach this routine"
and "sites that spell the bare callee" are different sets the moment a wrapper exists, and before 535 they
were the same set by accident.

## 2. The five sites

In the file's own order:

1. **the rule** — `<FlushPoU_Dcache>` → `<[^<>]*FlushPoU_Dcache>`, so the pattern is about the
   **routine** and accepts any wrapper prefix;
2. **the counts** — one `FLUSH_N` split into `FLUSH_SITES` (any prefix) and `FLUSH_WRAP`
   (`<__wrap_FlushPoU_Dcache>` exactly), so whether `--wrap` is in the link follows from a measurement
   rather than from a claim;
3. **the empty branch** — the `-z $DERIVED` branch became three-way: no decoder on `PATH` → `UNREAD`
   (exit 0, a property of *this shell*); a decoder that printed nothing → `UNREAD` citing 549 (exit 0,
   the same); a **full** disassembly with no such call in the function → `fail`, exit 1, because that is
   a finding about the image and the run after it would attribute a death by an address that names no
   instruction;
4. **the narration** — the `ok:` line now prints which of the two mechanisms this image uses: the
   `FLUSH_WRAP > 0` half says every caller arrives at one wrapper and that what separates this seam from
   the other callers is the return-address filter *inside* the wrapper, "and not the call site, which is
   the separation the unwrapped arms had and this one does not"; the other half is the pre-535 sentence,
   now conditional;
5. **the header comment** — the paragraph that asserted the caller is the only thing that separates this
   seam now records that 535 moved it, and why the printed line says which mechanism instead.

The branch in (3) is the part that is not narration: it converts the **state the wrapped arm used to rest
in** — quiet, exit 0 — into a stop, and it only stops when the decoder actually produced a disassembly.
`grep -c ''` was used for the line count rather than `wc -l` deliberately: `grep -c` always exits 0, so
the `|| true` arm of that pipeline is provably dead and there is no second producer of the `-z` state.

## 3. Proof

```
bash -n stages/stage90/preflight_boot_check.sh                        clean
git diff --numstat -- stages/stage90/preflight_boot_check.sh          62  12
```

The `12` deletions are the two `echo` lines the empty branch replaced plus the `FLUSH_N` line, and the
two comment lines the header edit rewrote — a replacement with no suffix loss, which on a file whose
regions are read by line number is the thing to check rather than assume.

**The four branches were rehearsed from the file's own bytes**, not from a copy of what I meant to write:
lines `1736-1859` were extracted into a harness that supplies only what the surrounding script supplies
(`fail`, `RUNNER`, `OUT`), and the harness was run four times with `STAGE90_OBJDUMP` pointed at four
different things:

| case | `STAGE90_OBJDUMP` | exit | what it printed |
|---|---|---|---|
| A | a stub decoder that prints a disassembly with no such call in the function | **1** | `REFUSING: … decodes to 5 lines and shows no 'bl <...FlushPoU_Dcache>' inside platform_cache_idle_exit …` |
| B | a name that is not on `PATH` | 0 | `UNREAD - … is not on PATH, so nothing in … could be decoded here` |
| C | a stub that exits 0 printing nothing | 0 | `UNREAD - … exited without printing anything … which is 549's decoder` |
| D | the real `arm-none-eabi-objdump` and the real ELF | 0 | `ok: the reader's criterion and this image agree - 0x800462dc … 5 bl site(s) - 4 of them through the *wrapper* …` |

A is the branch that did not exist before this change — before it, that state printed the same `UNREAD`
as B and C and exited 0. Each case is a run in which the other three cannot fire, because the four
conditions are decided by four disjoint inputs.

**The live gate, both readings, and they are read by which section refused:**

- At `01:06:56`, on the settled 535 build, the run printed the `ok:` line above with **0 `UNREAD` lines
  in the whole run** and exited **0** — the repaired text printed by the same run that exited 0, not a
  rehearsal standing in for it. (That is the green 570b recorded; the artifact hashes there pin it.)
- On the tree as this lands, the gate exits **1** — at `== the entry image's own sources ==`, which is
  **line 537 of the file, long before this clause at 1781** — and its message names the two files that
  differ: *"the entry image is not the build of these sources: build_entry.sh entry_trace.c"*. **Neither
  of them is this file.** So that red is the peer session's mid-edit working tree (their next arm is
  staged in `xnu_arm_boot/`), it is upstream of everything here, and it says nothing about this change
  either way — it is not evidence for the change and it does not block it. What it does mean is that the
  clause could not be exercised on this tree at all today, which is why the rehearsal above is the proof
  and the `01:06:56` run is the green.

## 4. A harness that fails for its own reason, again

The first rehearsal of the four branches returned exit 1 with **zero** refusals in *all four* cases —
including the real-decoder case that had just passed inside the live gate. That is a rehearsal that fails
for its own reasons, and it is the same class 570 section 4 recorded twice: the harness was missing
seeds. `ENTRY_ELF=$OUT/xnu_arm_entry.elf` and `_GATE_OD=${STAGE90_OBJDUMP:-…}` sit at `:1736-1737`,
which is **before** the extracted range, so under `set -u` every case died on `ENTRY_ELF: unbound
variable`. The fix was to start the extraction at `:1736`, so those two assignments come from the file's
own bytes too. The tell was that the exit code was 1 while no refusal had printed — the same tell as
`dup` printing two keys and not being refused in 570.

Worth stating as the general rule, because this is the third time: **a rehearsal harness is a stand-in
for the surrounding script, and a stand-in can be the right size and the wrong value** (570 section 4,
556). Its exit code is part of the reading, so a case is only observed when its exit code and its text
agree with the branch under test.

## 5. State

- **535 ran and did not come back** (572): entry `12684433…`, one non-persistent `fastboot boot`, the
  device hung and needing a power press. Its pair is spent and parked under `out/stage90/captures/535-*`.
- **The next arm (572 section 6) is the same interception with no operation** — same filter, same two
  reads, then `bl __real_FlushPoU_Dcache`. It reaches the same call through the same wrapper, which is
  why this clause matters for it: the gate now derives `0x800462dc` for a wrapped link instead of going
  quiet, so the reader's criterion is cross-checked again on a record that no longer has to be read as
  `UNREAD`.
- **TWRP-to-storage stays withheld.** `如果os已经能进去了的话` is the same unmet clause as
  `起码要能进入操作系统`: the boot reaches pid 1 and does not survive the idle pass — and 535 moved a
  returning death into a non-returning one, which is further from the goal, not closer to it.
