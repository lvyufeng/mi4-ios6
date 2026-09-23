# 570: the key the build side added before the record carried it

535's arm adds a switch. `STAGE90_XNU_SEAM_POC` is written by `build_entry.sh`, it selects
`--wrap=FlushPoU_Dcache` in the trace link, and until this change `stages/stage90/preflight_boot_check.sh`
had never heard of it. The change lands the key in the gate: the required-key list, the 0/1 variant
derivation, and a narration block for both of its values.

**This is a gate change only.** No image, no `out/`, no device, no arm: 568's pair stays spent and parked.
It is filed as an experiment because it is the difference between the next run's gate refusing on the
record it is supposed to describe and the same gate narrating a variant it cannot name.

## 1. The trap is the converse clause, and it is a different clause than it looks

Two clauses in the `ENTRY_CFG_KEYS` section can be broken by a switch the build side adds, and they fail
in opposite directions:

(i) the required-key loop at `:371-378` `fail`s when **the record lacks a key the list names**, and its
message names the key:

> `… has no $_k line - this gate prints the entry image's variant by name, and a record without that key
> would let a run go out with a switch nobody recorded.`

(ii) the converse clause `_unshown` at `:387` reads every `STAGE90_*` key **out of the record** and
`fail`s on any key the gate does not print, in its own words:

> `$ENTRY_CFG carries key(s) this gate does not print: … - a switch recorded on the build side and not
> shown here is a switch the next run would go out with unread; add it to ENTRY_CFG_KEYS above`

535's key would have tripped **(ii)**, not (i): the moment `build_entry.sh` wrote
`STAGE90_XNU_SEAM_POC=1` into the record, the gate would have refused the run. That refusal is upstream of
the device step, so it is safe — and it is the correct refusal, because a run whose arm the gate cannot
name is a run no one has read. But it is *blocking*: it would have stopped 535 at the gate with a message
about the gate's own bookkeeping, on the one boot window 557 measured.

The peer session said exactly this, and said whose lane it was: *"**Not my lane and not edited by me:
`preflight_boot_check.sh`**"* — and stated the semantics their side had already written: the interception
is the **presence** of a wrapper (no branch inside one), it acts at exactly one call site (the call whose
return address is `0x800462dc`), everything else is handed straight through, and the readings are the
`xnu_live_seam_*` keys — the site's `calls`/`lr`, the slot before (`b0`,`b1`) and after (`a0`,`a1`), and
`other`/`other_lr` for the calls that were not the seam. Those are the facts the narration below prints;
they were folded in rather than paraphrased into something the gate could have got subtly wrong.

## 2. The ten sites

`STAGE90_XNU_SEAM_POC` is now named in, in the file's own order:

1. `:338-341` — the comment that counts the variant keys: "the **six** that *are* the variant … SLOT_NULL,
   EXIT_POC_FLUSH, IDLE_CACHE_ENABLE, ISTACK_SEPARATE, IDLE_STACK, SEAM_POC";
2. `:351` — the sentence that counts the list's history: "The list was nine, then twelve, and is thirteen,
   and each addition grew it by enumerating rather than by reading";
3. a new commented paragraph at `:360` — "**The thirteenth is 535's, and it arrived from the build side.**";
4. `ENTRY_CFG_KEYS` — the key itself;
5. `:376` — the refusal message, whose parenthetical now names SEAM_POC as the sixth of "the six variant
   keys … exactly the ones a display filter written around the artifact keys drops in silence";
6. `:370`'s neighbourhood — the "(a *tenth* when the list held nine; a **fourteenth** now)" count;
7. `:397` — the comment about how the six are read (`grep -c` of the whole `KEY=` prefix);
8. the `for _vk` list at `:417-418`;
9. `STAGE90_XNU_SEAM_POC)          V_SEAM_POC=$_vv ;;` in the derivation `case` at `:433`;
10. `:434`'s "these **six** are switches".

plus the new `=1`/`=0` narration block at `:495-519`, placed between the `EXIT_POC_FLUSH` block and the
`ISTACK_SEPARATE` block so the file reads in the same order it prints.

The counts in (1), (2), (6) and (10) are the part that is easy to get wrong, and getting them wrong is
invisible: a stale "five" beside a six-element list is a claim in a comment, not a check, and it is also
the file's most-repeated defect class in miniature — one number with several definitions. All four were
changed in the same pass, and the two that had *already* drifted were caught on diff review rather than by
anything that stops a build: `:351` still said "nine and is twelve" and `:370` still said "a thirteenth
now" after the first edit pass had added the key. There is no check for prose counts; the only defence is
reading the diff, which is why the numstat below is part of the proof and not paperwork.

## 3. The narration says what the arm is, and what it is not

`=1` prints the site, the operation, and the readings:

> `the seam (SEAM_POC=1): the interception IS in this image … the wrapper acts at exactly one: the call`
> `whose return address is 0x800462dc - the bl FlushPoU_Dcache *inside* the real`
> `platform_cache_idle_exit (547 section 5's seam), AFTER that function's push {fp, lr} and BEFORE its`
> `pop {fp, pc} … the readings are the xnu_live_seam_* keys … other/other_lr for the calls handed through`

and closes with the sentence that keeps 535's run from being read through the wrong table:

> `**Read this run against 565 section 3's table and not against 547 section 4's enable cells: this arm is`
> `a state change at the seam itself, so its proof is the death's shape - recovered, or still at the pop -`
> `and not the value of a key.**`

`=0` prints the other side and, following 569, refuses to let an absent flag read as an absent seam:

> `the seam (SEAM_POC=0): NO interception of this image's own … The exit's call is untested by such a run:`
> `this is the arm 568 already ran (533's configuration), so a record with this key at 0 is a re-run of`
> `that arm and not a test of 535`

The `=1` block also states what the flag is **not**, because 569 exists for precisely that confusion: it
is not `EXIT_POC_FLUSH`, whose flush is the first statement of the exit *wrapper* and therefore runs
before the real exit's `push {fp, lr}` — "one name for the two arms is one value with two definitions".

## 4. Rehearsal: five record states, run against the file's own bytes

The gate's own run cannot be the proof right now (section 5), so the five states were rehearsed directly.
Lines `367-519` were extracted **from the edited file** — not from a copy of what I meant to write — into
a harness that supplies only what the surrounding script supplies (`fail`, `ENTRY_CFG`, `actual_sha`,
`actual_bytes`, the six `V_*`), and the harness was run against five records built from today's real
`out/stage90/xnu_arm_entry-config.txt`:

| record | exit | what it printed |
|---|---|---|
| today's, no `SEAM_POC` line | 1 | `has no STAGE90_XNU_SEAM_POC line` — names the key |
| today's + `SEAM_POC=0` | 0 | the `=0` seam narration |
| today's + `SEAM_POC=1` | 0 | the `=1` seam narration |
| today's + `SEAM_POC=1` twice | 1 | `defines STAGE90_XNU_SEAM_POC more than once: one value with two definitions` |
| today's + `SEAM_POC=2` | 1 | `are not 0 or 1: these six are switches` |

Both narration branches were observed, each in a run where the other branch cannot fire — the states are
mutually exclusive, so neither reading is a special case of the live gate's path. Both new refusal paths
were observed too, and both name the key rather than the line number.

Two harness defects are worth recording because both would have read as a pass:

- **The first extraction cut the range at `:513`, mid-block.** With the trailing `fi` missing, bash hit
  EOF while reading the `if`, so the `SEAM_POC=0` and `=1` cases exited **2** with *no* narration — and my
  grep for the narration text matched nothing, which is the only reason I looked. A truncated extract of a
  shell file is a stand-in of the right size and the wrong shape: it parses far enough to run every clause
  before it, so the refusals still fire correctly and the file looks like it works. The range now ends on
  a real block boundary (`:519`), and `bash -n` on the harness is checked before it is used.
- **The second pass dropped `actual_sha` from the preamble**, and all four non-refusing cases died on
  `actual_sha: unbound variable` — an exit 1 that is not a gate refusal. Supplying the seed values fixed
  it; the tell was that the `dup` record printed *two* `SEAM_POC` lines and was still not refused. A
  rehearsal that fails for its own reasons has to be distinguished from one that fails for the clause's.

## 5. What the change costs, and why the green is deferred

**Cost, stated plainly: the key is now required of the record, so the gate refuses today's 12-key record
until the build side writes the thirteenth.** That is the fail-safe direction — the refusal is upstream of
the device step and names the key — and it is the same property the other five variant keys already have,
all of which are written by the same `build_entry.sh`. It is also the point: 535's image is being linked
with a `--wrap` that changes what a call inside the exit *does*, and a gate that would boot that image
without its record naming the arm is the gate this project has spent a dozen experiments making noisy.

**The full-gate green is deferred.** It was attempted on the frozen tree and returned **`GATE EXIT=1`** —
not at any clause this change touches, but at the later entry-blob cross-check:

> `REFUSING: /mnt/data/mi4-ios6/out/stage90/stage90.bin does not contain /mnt/data/mi4-ios6/out/stage90/xnu_arm_entry.bin at all`
> `… the image does not carry the arm …/xnu_arm_entry.bin holds, byte for byte`

That is the peer session's **mid-build** state, read off mtimes and then off `ps`: `xnu_arm_entry.bin`
`00:56:32` is newer than both the record (`18:46:12`, still twelve keys) and the payload (`17:53:14`),
because `build_entry.sh` rewrites the entry blob before `./build.sh` re-embeds it — and `ps` showed
`bash ./build_entry.sh` running while this was being written. The gate *cleared the entire
`ENTRY_CFG_KEYS`/`_unshown` region* on that same run, which is itself evidence about this change: the
required-key loop, the converse clause and the derivation all agreed with the twelve-key record in front
of them, and only the blob check downstream refused.

So the honest state of the proof is: `bash -n` clean, five rehearsal states as tabled, and the whole-gate
`EXIT=0` **owed** — to be taken once the build settles, before any device action. It is owed to the same
run that will also be the first to see the thirteenth key *in* a record, which is the state this change
exists for.

## 6. State

- **535 is in the peer session's lane and is being built now.** Its arm is a per-level CSSELR PoC
  invalidate wrapped on `lr == 0x800462dc`; it costs one non-persistent `fastboot boot`, and its proof is
  the death's shape, not a key's value.
- **533's pair (`1daaf44e…` / `f202f246…`) is spent**, parked under `out/stage90/captures/533-*`, and
  568's run is 547 section 4 row (i): dies at the idle exit's `pop {fp, pc}`, log present, device returns.
- **535 does not touch the idle-exit address and does not touch `up_style_idle_exit`** (its arm is still
  `=1`), so the gate's two-copy complement clause is not in play for this run.
- **TWRP-to-storage stays withheld.** `如果os已经能进去了的话` is the same unmet clause as
  `起码要能进入操作系统`: the boot reaches pid 1 and does not survive the idle pass.
