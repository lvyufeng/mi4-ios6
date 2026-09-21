# Experiment 499 — the clause that reads a citation asked for the right number and never the wrong one

Date: 2026-09-21
Hardware: none — host-side. Every reading here is from the two files the last step changed and from
the check that reads them. No device run, and no image byte.
Artifacts: `tools/check_timer_line.py`, `stages/stage90/xnu_platform/MSM8974Timer.cpp`

**Result: 498 added a clause to refuse a line-number citation that points at the wrong line, and the
clause could not see the citations it was added for. The block that grounds the frame's offsets cites
the device's kernel thirteen times; six of those numbers are wrong, one of them points at a blank line,
and the step that wrote the block named exactly two of the six in its commit message and fixed neither
in the place where they are.**

This is the third step in a row whose first finding is a defect *of the previous step's own check*, and
it is the family the memory files keep: 497's mutation that was accepted, 498's half-fact that compared
two values and never the key, and now a clause that asks whether the right number appears anywhere and
has nothing to say about a wrong number beside it.

## What the clause was, and what it could see

498's claim 1 ends with this loop, and the comment above it states the intent plainly — "the citation
is checked like a number in the code":

```python
line = facts.arch_timer[:facts.arch_timer.index(needle)].count("\n") + 1
for source, who in ((facts.timer_src, "`MSM8974Timer.cpp`"),
                    (facts.payload_src, "`stage90_main.c`")):
    if not any(form in source for form in ("arch/arm/kernel/arch_timer.c:%d" % line,
                                           "`:%d`" % line)):
        failures.append(...)
```

Two consequences follow from that shape, and both were measured rather than reasoned about:

  - **It is a search of the whole file for one string.** The number is derived from the kernel's own
    text, so it cannot go stale — but a *second*, stale citation of the same call elsewhere in the file
    is invisible to it, and the file it was pointed at has exactly that shape: the corrected pair lives
    in one comment and the block that justifies the offsets is another comment six hundred lines above.
  - **It knows about two of the thirteen numbers in that block.** The other eleven cite the kernel's
    register defines, and nothing in the step's check read a define's citation at all.

## The demonstration, run against the tree that has the defect

A comment-only edit puts `:631` back where 498 committed it, in the block that grounds the offsets:

```
 *       :631 timer_base = of_iomap(frame, 0);              <- the frame's *first* reg
```

`git show HEAD:tools/check_timer_line.py` — the committed check, unmodified — prints its success line
and **exits 0** on that tree. The same tree, read by the check this step writes:

```
FAIL: the timer's line and its frame are not the ones this step says:
      `MSM8974Timer.cpp` cites `of_iomap(frame, 0)` at line 631 of the device's kernel, and that line
      says `pr_err("arch_timer: no physical timer irq\n");` - an offset justified by a citation that
      points at a line which does not contain the name it is cited for is justified by nothing
```

And the same for one of the four the step never suspected — `QTIMER_FREQ_REG` is at `:65`, and the block
says `:66`:

```
      `MSM8974Timer.cpp` cites `QTIMER_FREQ_REG` at line 66 of the device's kernel, and that line says
      `#define QTIMER_CNTP_TVAL_REG		0x028`
```

`:66` is a *real line of the right file* that defines a *different register*, which is why the number
looked arbitrary rather than wrong to every reader so far: the citation named a plausible neighbour, and
the offsets the block is about are all correct — compared name by name against the same kernel by the
first half of this same claim, which was green throughout.

## The block, read as a block

The new clause binds a citation to the name written beside it on the same line, in either shape the
block uses, and requires that name to be on the line the citation names:

```python
for cited, at, line, start in line_citations(source):
    qualified = re.search(r"[\w./-]$", line[:start]) is not None
    if qualified and "arch_timer.c" not in line[:start]:
        continue
    named = [a for a in anchors if a in line]
    if not named:
        ...  # a bare reference: it must name one of the two calls the block quotes
        continue
    for a in named:
        if not (1 <= cited <= len(kernel_lines)) or a not in kernel_lines[cited - 1]:
            failures.append(...)
```

`anchors` is the eleven kernel-side names this claim already compares *values* of — `QTIMER_CNTP_LOW_REG`
through `QTIMER_CNTV_TVAL_REG`, `ARCH_TIMER_CTRL_ENABLE`/`IT_MASK`/`IT_STAT` — plus the two call texts
`of_iomap(frame, 0)` and `irq_of_parse_and_map(frame, 0)`. **Seventeen citations are read this way**, and
the count is published as a note, because a clause that can match nothing must say how many things it
matched.

Three details of that rule are themselves findings:

  - **A citation that carries a path belongs to the file the path names.** The block also writes
    `msm8974.dtsi:161-167` two lines below the call citations, and the first version of the bare rule
    read its `161` as a line of the device's *kernel* and refused a correct file. A rule scoped to "the
    block" is not scoped to "the citations this claim is about"; the `qualified` test is that scope.
  - **The block is located by our own file's shape** — the kernel's path alone on a comment line — and a
    property of the writer is a claim like any other. The first version of the block finder looked at the
    whole line rather than at the text after the comment marker, so the leading `*` counted as a space,
    the block was never found, and the bare-citation half was **dead code that read as a passing
    clause**. Nothing in the run said so. What exposed it was the note's count, added for a different
    reason; the guard that now stops it going quiet again is a failure when a source cites the kernel by
    path and no block opens with that path.
  - **The bare reference is checked at all** because it is the shape the wrong number had: the line
    below the call citations says "that kernel applies them to the frame's first `reg` region (`:631`)"
    — no name on the line, a reference back to the call quoted eight lines above, and it said `:631`
    after 498 "corrected" the pair. A citation with no name beside it is the one a reader trusts most.

## The thirteen, and which of them anyone had noticed

| cited | the block says | the kernel's line actually holds | the number that line is |
| --- | --- | --- | --- |
| `:60` | `QTIMER_CNTP_LOW_REG` | `#define QTIMER_CNTP_LOW_REG		0x000` | — |
| `:61` | `QTIMER_CNTP_HIGH_REG` | `#define QTIMER_CNTP_HIGH_REG		0x004` | — |
| `:62` | `QTIMER_CNTV_LOW_REG` | `#define QTIMER_CNTV_LOW_REG		0x008` | — |
| `:63` | `QTIMER_CNTV_HIGH_REG` | `#define QTIMER_CNTV_HIGH_REG		0x00C` | — |
| `:64` | `QTIMER_CTRL_REG` | `#define QTIMER_CTRL_REG			0x02C` | — |
| `:66` | `QTIMER_FREQ_REG` | `#define QTIMER_CNTP_TVAL_REG		0x028` | **`:65`** |
| `:66` | `QTIMER_CNTP_TVAL_REG` | `#define QTIMER_CNTP_TVAL_REG		0x028` | — |
| `:67` | `QTIMER_CNTV_TVAL_REG` | `#define QTIMER_CNTV_TVAL_REG		0x038` | — |
| `:50` | `ARCH_TIMER_CTRL_ENABLE` | *(blank line)* | **`:51`** |
| `:51` | `ARCH_TIMER_CTRL_IT_MASK` | `#define ARCH_TIMER_CTRL_ENABLE		(1 << 0)` | **`:52`** |
| `:52` | `ARCH_TIMER_CTRL_IT_STAT` | `#define ARCH_TIMER_CTRL_IT_MASK		(1 << 1)` | **`:53`** |
| `:631` | `of_iomap(frame, 0)` | `pr_err("arch_timer: no physical timer irq\n");` | **`:623`** |
| `:637` | `irq_of_parse_and_map(frame, 0)` | `return ret;` | **`:629`** |
| `:631` | *(no name on the line)* | `pr_err("arch_timer: no physical timer irq\n");` | **`:623`** |

Six citations and a seventh number are wrong, out of fourteen; the three control-bit citations are each
off by one, so the block that justifies `IT_MASK`'s meaning cited the define above it and the block that
justifies `ENABLE` cited a **blank line**. 498's commit message says:

> this step's two citations pointed eight lines away from the calls they named (631/637 for 623/629) and
> nothing parsed them: a citation that is never read is prose.

Two of the seven were noticed while that message was being written. The correction landed in a *second*
comment — the runtime block at the driver's line 868, which is where the corrected `623`/`629` pair sits
and which satisfied the new clause on its own — and the block the numbers belong to kept all seven. The
other four, the control bits and the frequency, were never suspected by anyone, and no reader could have
found them by reading: three of them name real neighbouring defines and one names a blank line.

## The claim: eight claims and eighteen mutations

`tools/check_timer_line.py` goes **8 claims / 16 mutations → 8 / 18**, all refused in `--selftest` and in
the build. The two new mutations reproduce the two *kinds* of wrong citation rather than one instance of
each:

  - `the_grounding_block_cites_the_line_before_the_call` restores `:631` where the block now says `:623`.
    Its docstring records that this is the mutation the clause was written for **and that 498's clause
    accepts it**, which was measured with 498's own script rather than argued.
  - `the_grounding_block_cites_another_define_line` restores `:66` for `QTIMER_FREQ_REG`. This is the
    kind that reads as plausible: a real line of the right file, defining a different register.

The existing `the_citation_points_at_another_line` mutation — which moves the long form
`arch/arm/kernel/arch_timer.c:623` to `:624` — **had to be kept and is now refused by the new half rather
than the old one**: the corrected `623` appears bare in the block, so removing the long form no longer
removes every `623` from the file. It was accepted for one run in the middle of this step, which is what
made the scope of the old clause visible.

## The artifact did not move

The edit to `MSM8974Timer.cpp` is comments only, and that is proved by rebuilding rather than asserted:
the four-step build runs green (`XNU_KERNEL_CONFIG=STAGE90_XNU` and `XNU_MASTER_LOCAL` exported once at
the top, the 498 lesson) and every artifact hashes to 498's value —

    out/stage90/xnu_arm_entry.bin   7c3dbe7c215b6f1b7a310667238fb8a5f985cd7436bfc4232df0cf3a05cb56d1
    out/stage90/stage90.bin         13a1ee837c17fd7f5ee6d30b3179594fe2475945fc48e5c30a0a6de432ec0c2d
    out/stage90/stage90.img         c05455905bd921da731e4cf87efaf13ccef0fe0bb60d690a61c27c0486c8434b
    out/stage90/stage90-qcdt.img    39e0d1c64bc1bae7323b90792fa560c78a5a1e40fa90e060834d26d43859709b

with 498's `.text` 5270560, `.bss` `0x8053ba80..0x805943a8` (362792), 27 undefined and 59 wraps — the
same numbers 497's subtraction left and 498 did not move. `build_entry.sh`'s two runs of the strengthened
check both pass in that build (9 claims / 57 mutations for `check_irq_routing.py`, 8 / 18 for this one).

No device run is owed: the image is the one 498 ran twice, byte for byte.

## What is owed

  - **The other citations in these files are still unchecked.** `MSM8974Timer.cpp` cites
    `IOService.cpp:6337`/`:6290`, `IODeviceTreeSupport.cpp:137-138`, `:221-225`, `:422-431`, `:434-447`,
    `:467-487`, `:489-501`, `:510-518`, `:790` and its own tree's `msm8974.dtsi:153-167`, and every one
    of them is read by a human and compared by nothing. The rule that would cover them is the one this
    step wrote, pointed at another file: a citation is checked when the file it names ships in this
    repository, and that is a table of `(our file, the cited file, the anchor regex)` — worth a step, and
    it is the same clause three more times.
  - **`check_irq_routing.py` and `check_driver_catalogue.py` have citations of their own** and no
    equivalent clause; they were not read for this step.
  - **The rule binds a citation to the name on its line**, which is the shape both of our files use. A
    citation written with its name on the *next* line is not read by it, and the failure mode of that is
    silence rather than a wrong answer — the count in the note is the only signal, and it is a signal to
    a reader rather than to a check.
  - Unchanged from 498: **arming the line** — the distributor's enable/target/priority for intid 40 and
    the frame's `CTRL.ENABLE`; the `AckC`/EOI question for a level line; the ISR's return path into
    `rtclock_intr`'s world; the registry's capacity stop, still never taken. From 497: the
    conditional-clause mutation that needs two images; the unregister guard's asymmetry; a timeout that
    outlives its asker; `out/xnu_asm_obj` unstamped with its configuration. From 498: `/timer`'s second
    definition (partly delivered); the other device nodes; the two services the catalogue answers with
    nothing; `MSM8974RootResource`'s `state0 = 0`; a name/class reader wider than eight characters; the
    release as a reading; `vm_fault`; 488's flag-list; 490's frames band; `xnu_live_dec_same`.

## Safety

This step touches no device, enables nothing, and changes no image byte — the entry image is the one 498
ran twice through `preflight_boot_check.sh --allow-xnu-entry` and `run_and_capture.sh --allow-xnu-entry`,
non-persistent `fastboot boot` only. The one file it edits is a driver's *comments* and a build-time
check, and the rebuild that proves it is host-side.
