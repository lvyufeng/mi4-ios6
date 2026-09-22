# 555: the death is read from the key, and the criterion is derived

The peer session's review of [554](experiment-554-the-reader-called-the-prediction-a-failure.md)'s
*criteria* (not of its finding, which they confirmed and told me not to drop: "547 §4 is the
pre-registered prediction and it stays the criterion; a reader whose printed criteria invert it is 540
being stale relative to 547"). Four of their five points are defects in 554's first version, one is
mine to concede, and one they read from my message rather than from the code. Host-side, no device, no
build.

## 1. The abort's registers are keys in the log, not text in the dump

554 compared against the **panic dump's** text - `grep -a -c 'lr: *0x800462dc'`. The image publishes
the same registers as first-class fields, and the log carries them
(`/tmp/cancro-last_kmsg.txt:8341-8350`, the fatal episode):

```
xnu_live_sleh_storm=0x00000009   xnu_live_sleh_seen=0x00000009
xnu_live_sleh_pc=0x04b79074      xnu_live_sleh_lr=0x800462dc
xnu_live_sleh_sp=0x8054fed0      xnu_live_sleh_cpsr=0x800000b3
xnu_live_sleh_fsr_frame=0x00000005   xnu_live_sleh_far_frame=0x04b79074
```

identical to the human dump at `:4006` - and **the same file holds an earlier, unrelated abort episode**
(`:7651`, `xnu_live_sleh_lr=0x8029231c`, `pc=0x800176c0`, `seq=2` of nine). So a whole-file match on the
dump text can be satisfied by the wrong episode, and a dump belonging to a sibling abort satisfies the
pattern exactly as well as the death does. `keyval` takes the **last** publication, which is the fatal
one - the episode that panics is the last one that happens - so the key binds the attribution and the
text cannot. The second half of their point is the padding: the dump line is
`r12:  0xde58b701  sp: 0x8054fed0  lr: 0x800462dc  pc: ...`, two spaces, so a single-space pattern
misses it *ambiguously*.

The block now reads `sleh_lr`, `sleh_pc`, `sleh_sp`, `sleh_seen` through the same `keyval` it already
uses elsewhere, and prints all four. Verified: every `xnu_live_sleh_*` and `xnu_live_pce_*` name the
block reads is in `out/stage90/xnu_arm_entry.bin` (`f202f246…`), one occurrence each.

**And the binding is demonstrated rather than argued.** State H below is 520's log with the *earlier*
episode's `lr` rewritten to the criterion and the fatal episode's moved away: the check FAILs, because
`tail -1` reads the fatal publication. Under 554's text grep the same log would have matched.

## 2. The criterion is derived at run time, and labelled when it is not

554 pinned `EXIT_POP_LR_LITERAL=0x800462dc` and argued its failure direction was noisy rather than
silent. The peer's answer is the project's own rule: **an address a check compares against is the same
object as an address a gate prints**, and a pinned `lr` is 520's defect one level down (a gate
narrating the previous image's hash out of a comment literal). They offered a fallback ("if deriving is
too much for a summariser, bind the literal to the entry bin hash … and refuse/label on mismatch"),
and 555 takes the derivation with the literal as a **labelled** fallback.

`exit_pop_lr_addr()` computes it with no pinned address anywhere in the arithmetic:

| step | what | where |
| --- | --- | --- |
| 1 | `platform_cache_idle_exit`'s **start and size** | the ELF's own symbol table (`-t`), `0x800462d4` / `0x6c` |
| 2 | its body, disassembled from those two numbers | `--start-address`/`--stop-address` |
| 3 | the address of the instruction **after** the `bl` to `FlushPoU_Dcache` | the next line that begins with a hex address |

and the answer identifies itself: the instruction at the derived address is
`movw r0,#4516 / movt r0,#32853` = the load of `up_style_idle_exit` at `0x805511a4`, which is what the
address is *for*. Assertions before returning: 4-byte aligned, inside `pmap_kernel_va`
(`[0x80000000, 0xFFFEFFFF]`), and not the function's own entry.

**It failed on its first run** - `derived: ` empty - and the cause is worth recording because it is
this project's oldest shape: 554's rule assumed the disassembler prints a *symbol line* for the address
after the `bl`, which `llvm-objdump` does (`800462dc <platform_cache_idle_exit+0x8>:`) and
`arm-none-eabi-objdump` does not. A rule that assumed one of them finds nothing under the other, and
"nothing" is exactly what it printed. The fix matches either form (the colon is stripped, not
required), and the comment says so.

Tested in both directions: with the ELF readable the derivation returns `0x800462dc`, and with
`OBJDUMP=/bin/false` or a missing ELF it returns failure and the caller prints
`*** PINNED LITERAL - the entry ELF could not be read, so this criterion is NOT derived ***` beside the
number. That is the peer's "refuse/label" applied to the failure path. Whole summarise against the
frozen ELF: **0.16 s**.

## 3. The `lr` test is the enable-off arm's criterion, and the log says which arm

The peer's cleanest catch. `0x800462dc` is the pop's `lr` **only in the arm where the exit's two
remaining `bl`s are skipped**: `InvalidatePoU_Icache` (`0x80046304`) and `flush_core_tlb`
(`0x80046308`) are taken unless `up_style_idle_exit != 0` *and* `ncpu < 2`, and if they are taken then
`lr` at the pop is whatever `flush_core_tlb` left - so a plain "lr != the criterion ⇒ FAIL" would print
FAIL for a run behaving exactly as *that* arm predicts. 554 had the pin's scope stated in prose; 555
asserts it **from the log**, because both selectors are published by name (549):
`xnu_live_pce_up` / `xnu_live_pce_ncpu` (520's log: `0x1` / `0x1`). Three outcomes: `up1`, `other`, and
`unread` when the keys are absent - and `other` and `unread` are both UNREAD with the reason printed,
not FAIL.

The *cell* (which enable) comes from `slot_cwe_set`'s `C` bit, and that decision moved up to the
extraction block so clause (1) can use it as well as clause (2).

## 4. 547 §4's third row is a falsifier, and it is now the loudest line

547 §4 predicts a **conjunction** - a pop death *and* a return. 554's clause (1) called the
no-pop-death case a PASS, with 547 §4's third row mentioned inside the PASS sentence. For the
enable-off cell that case is the falsifier that **forbids 535 being built as designed**, so it is its
own line now:

```
  FALSIFIER  and this log's pair is the enable-off cell's, whose prediction is that it
        DOES die at the pop - 547 section 4's third row. If the runner's exit code says
        the device returned, this falsifies 546's mechanism for this cell and 535 must
        not be built as designed. It is the loudest reading in this block
```

and the close points back at it. What 555 does **not** do is read the return axis: the summariser has
no exit code, and the block says so in both the predicted and the passing close - the runner's exit
status is the reading there (551: exit 3 is a capture failure, not a hang).

**And `pc` is not pinned**, per the peer: it is the popped data word (`pc = r11 & ~1`, `0x04b79074` in
520) and varies run to run. It is printed as corroboration with its shape named, never compared.

## 5. The one I conceded: clause (3)'s exemption is conditioned on both

The peer's condition - "clause (3) should stop setting `verdict_ok=0` only when the death is attributed
**and** a panic exists. `post` absent with no panic anywhere is genuinely loud - the bracket's absence
is informative precisely because all three publishers share one schedule and one gate (538), and it
should not be excused by an `lr` that nobody saw". 554 already satisfied this by construction
(`pop_named` is set only inside the `panics > 0` branch), and 555 prints the condition so the next
reader can see it is deliberate rather than accidental.

The one point they read from my *message* rather than from the code was the falsifier in §4 - 554 had
already added 547 §4's third row to the PASS branch's text after I sent that message. Their instruction
was still right: inside a PASS sentence is not where the loudest reading belongs.

## 6. Tested against nine states, three of them the new criteria's own refusal cases

Every state is 520's real captured log with keys injected; nothing below touched a device or a build.

| state | what it is | reader |
| --- | --- | --- |
| A | `_cwe_set` `C` clear, fatal `sleh_lr` = the criterion | `PREDICTED` + `ARM 533` + clause (3) agreement + "the two agree" |
| B | **A with the fatal `sleh_lr` key moved** to `0x8004c0ff` | `FAIL` … "is not the exit pop's own return address (0x800462dc)" |
| C | A with `_cwe_set` `C` set | `PREDICTED` + `ARM 522` + "**which 547 section 4 does not predict this for**" |
| D | the panic block removed, `slot_post_calls` injected | `PASS` + **`FALSIFIER`** + `PASS`×2 + the close pointing at it |
| E | `_cwe_set` unreadable | `PREDICTED` at (1) - the `lr` matched - then `UNREAD` at clause (2) and the `*` arm branch in the close |
| F | `xnu_live_pce_up` = 0 (**the other cache arm**) | `UNREAD` … "this block's criterion does not apply to this log at all" |
| G | both `pce` keys unreadable | `UNREAD` … "the arm that decides what lr at the pop IS cannot be read" |
| H | **an *earlier* episode's `sleh_lr` rewritten to the criterion, the fatal one moved away** | `FAIL` - `tail -1` bound to the fatal episode (§1) |
| I | A with `OBJDUMP=/bin/false` | `PREDICTED` with the criterion printed as `*** PINNED LITERAL … NOT derived ***` |

Also: `bash -n`; `--summarise` on 520's **unmodified** log, which prints no idle-window block at all
(the gate on that block is `xnu_live_slot_cwe_`, which no image before 522 published); the derived
address equals the literal `0x800462dc` in this build, which is a cross-check and not the criterion;
and the gate read-only, **exit 0**, with its three exit sites re-derived at gate time (`:629`, `:641`,
`:651` - moved again by this edit, which is what 552 made it do).

**A's other half is unchanged from 554 and is not re-tested here**: 554's §1 - the clause text that
printed FAIL for 547 §4's prediction - is the finding both documents exist for.

## 7. Still drift, still reported rather than patched

The gate's 526-era narration (`preflight_boot_check.sh`, the `--allow-xnu-entry` notes) still reads
"the arm to run is 526" and describes the `_cwe_*` pair as `_win` clear / `_set` **set**. That is
conditionally right - it is guarded by "where the write IS in the image", and 533 is the arm where it
does not apply - but the section around it describes a spent arm. Reported to that file's owner with
the clause text, the same discipline as 551 §4 and 554 §5.

## 8. Safety

No device action. `bash -n`; nine `--summarise` runs over synthetic logs in `/tmp` built from the
already-captured 520 log; one `--summarise` over the real log read-only; one function-level harness of
`exit_pop_lr_addr` including its two failure paths; one read-only gate run (exit 0). Nothing written
outside `docs/`, `stages/stage90/run_and_capture.sh` and the memory files; no build run; no file under
`out/` touched; `flash` not used, nothing written to storage. Frozen pair untouched
(`1daaf44e624563694e…` / `f202f2465886aba6…`), and the device is off the bus awaiting a power press
before 533 can run.
