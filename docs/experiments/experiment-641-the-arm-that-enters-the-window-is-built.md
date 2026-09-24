# 641: the arm that enters the window is built, and it is one switch from the armed one

638 pre-registered a reading of the seam's `_b1`/`_a1` pair "for an arm that enters the window
(`IDLE_NO_SLEEP=0` with `SEAM_MEASURE=1`), **which no build has carried**". 640 repeated the claim as
"The arm that would read the pair is one no build has carried", adding that "the only seam arm ever
built was 535's, which was `SEAM_POC=1`". **Both sentences are wrong about builds**, and this step
measures the arm that contradicts them, parks it where it will survive, and separates the claim that
was false from the one that was only mis-worded: **no build has carried it is false; no *run* has
carried it is true, and `experiment-594` already says so.**

Nothing was built, booted or edited. Every number below is a read of two ELFs and two payloads on the
host.

## 1. The arm, and where it was found

`/tmp/r594/frozen-payload/` holds a complete payload set whose record says:

```
STAGE90_XNU_ENTRY_SHA256=151425c40c48cb1a417de1e4c570cb812f5b07a2e45aa41b04421c63fe34d746
STAGE90_XNU_ENTRY_BYTES=5519996
STAGE90_ENTRY_TRACE=1
STAGE90_ENTRY_REAL_ARM_INIT=1
STAGE90_XNU_SLOT_NULL=1
STAGE90_XNU_EXIT_POC_FLUSH=0
STAGE90_XNU_IDLE_CACHE_ENABLE=0
STAGE90_XNU_ISTACK_SEPARATE=0
STAGE90_XNU_IDLE_STACK=1
STAGE90_XNU_SEAM_POC=0
STAGE90_XNU_SEAM_MEASURE=1
STAGE90_ENTRY_CHECKPOINT=(unset)
STAGE90_ENTRY_CHECKPOINT_SKIP=(unset)
STAGE90_ENTRY_CHECKPOINT_AFTER=(unset)
STAGE90_XNU_IDLE_NO_SLEEP=0
```

`diff` against the **armed** record (`out/stage90/xnu_arm_entry-config.txt`, entry `696a0f39…`) is
**two lines**, and one of them has to differ:

```
4c4
< STAGE90_XNU_ENTRY_SHA256=151425c40c48cb1a417de1e4c570cb812f5b07a2e45aa41b04421c63fe34d746
---
> STAGE90_XNU_ENTRY_SHA256=696a0f39128c3fd4501dc32b1c4e04bb6b612988ce8a028bb21487c8e82a5674
18c18
< STAGE90_XNU_IDLE_NO_SLEEP=0
---
> STAGE90_XNU_IDLE_NO_SLEEP=1
```

So the arm 638 asks for and the arm that is armed are the **same build with one switch moved**, and it
is the switch 594 §1 exists for: `IDLE_NO_SLEEP=1` compiles 514's repair out of `__wrap_poll`, and
514's repair is the only thing in either image that clears `SIGPdisabled`.

## 2. The discriminating measurement, and both of its outcomes

`tools/check_idle_window_unreachable.py`, run on the two entry ELFs — the armed one in `out/` and the
park's:

| image | lines | exit | section (D) the clearer | verdict |
|---|---|---|---|---|
| `out/stage90/xnu_arm_entry.elf` (`7f80c2cb…`, armed) | 46 | 0 | `(none)` | `the window is UNREACHABLE in this image` |
| park `xnu_arm_entry.elf` (`3bc72605…`, `IDLE_NO_SLEEP=0`) | 44 | 0 | `0x8047be64  in __wrap_poll` | `the window is reachable EXACTLY ONCE in this image` |

The unreachable report's own paragraph, verbatim:

> Nothing in it clears SIGPdisabled, the bit is set at init, the IPI sender is gated by the same bit,
> and there is one CPU - so the gate never opens and `platform_cache_idle_exit`'s `pop {fp, pc}` is
> never executed. This is the sleepless arm's claim, and it is a property of the image and not of the
> arm's intent.

And the reachable one's:

> One `bl cpu_signal_handler_internal` at 0x8047be64 in `__wrap_poll` - the repair 514 added. Nothing
> else can clear the bit, so the gate opens on the pass after that call and the window is entered once
> per boot, which is the baseline's measured shape (520 and 533 each publish one
> `sip_seq`/`pce_seq`/`wfi_seq` record against 16 door records).

**The whole diff between the two reports is the image path, section (D) and the verdict paragraph.**
Sections (A) `0x8000d97c` / `0x8000dad4 b __wrap_Idle_load_context`, (B) the window's only entry
`0x8000da3c` `__wrap_platform_cache_idle_exit` 208 bytes past the gate, (C) the gate `0x8000d96c bl
__wrap_SetIdlePop`, and (E) the four `bl rtclock_intr` sites are identical in both. **Both outcomes
were observed on real images, so the tool is a discriminator and not an instrument blind to the state
it reports on** — the failure mode 638 §4 and 640 §5 spend their paragraphs on.

## 3. The two payloads differ in the entry and in nothing else

Both parks' `stage90.bin` are 6015356 B. Locating the entry by content, not by arithmetic: the entry
bin's first 64 bytes appear at offset **494100** in each, and its last 64 bytes end at **6014096**
(494100 + 5519996 = 6014096, and 5519996 = `STAGE90_XNU_ENTRY_BYTES`). That leaves a **1260-byte
suffix** — `/defaults`, `debug`, `/chosen`, `consistent-debug-root`, … — i.e. a device-tree-shaped
blob.

| region | armed park vs 574 park |
|---|---|
| prefix `[0:494100)` | **identical** |
| entry `[494100:6014096)` | differs — and each park's region hashes to **exactly the entry its own record names** |
| suffix `[6014096:)` | **identical** |

- armed park entry region → `696a0f39…`; its record claims `696a0f39…` ✓
- 574 park entry region → `151425c4…`; its record claims `151425c4…` ✓

`stage90-build-config.txt` and `stage90_fixture.macho` (`52bc9c35…`) are also byte-identical between
the two parks, and a `grep` for every arm switch in either `stage90-build-config.txt` returns nothing:
**the entry's switches live only in `xnu_arm_entry-config.txt`.** So two parks are one payload build
with two entries swapped, and the diff in `stage90.bin` is confined to the entry.

**The armed park is the control, and it is what caught the extractor** — see §5.

## 4. The seam instrument is in both arms, and its hook is a tail branch

`__wrap_FlushPoU_Dcache` is **three instructions** in both:

```
mov  r0, sp
mov  r1, lr
b    <entry_seam_flush>
```

and `entry_seam_flush` is 115 instructions, **byte-for-byte the same body in both arms** — same
opcodes, same constants, same order — at `0x8047c9a4` armed and `0x8047c9c4` in the park. The four
`bl __wrap_FlushPoU_Dcache` sites sit at **identical addresses in both images**, so the `0x20` is
introduced somewhere between them and this symbol, not in the seam itself. It is reached from
`bl __wrap_FlushPoU_Dcache`, which has **four sites in both arms** —
`cache_xcall`, `platform_cache_idle_enter`, `platform_cache_idle_exit` (`0x800462d8`), and
`cache_xcall_handler`.

So `SEAM_MEASURE` is **not** what the two arms differ in. The instrument is compiled into both; what
differs is **reachability of the idle-exit site**, which is the gate 640 §2 traced. This is worth
stating because the natural reading of "the armed arm carries the seam in its measure form" is that the
measure switch is the variable — it is not.

## 5. Two measurement defects of my own, both caught by a control, both recorded

**(a) "The entry is the last `ENTRY_BYTES` of the payload" is false, and the suffix is why.** I
extracted `tail -c 5519996` and got `6ab1399f…` for the 574 payload and `01dd8f0f…` for the armed one
— both disagreeing with their records. The second is the **control**: I knew the armed payload's entry
had to be `696a0f39…`, so a disagreement there meant my extractor was wrong, not the park. The real
layout is `[494100 prefix][entry][1260 suffix]`: total − `ENTRY_BYTES` = prefix **+ suffix**, and the
1260-byte error is exactly the suffix. A boundary recomputed from a record instead of measured from
the bytes is one quantity with two readings — "the payload's tail" and "the entry" — which is this
repository's most-repeated defect class.

**(b) `bl entry_seam_flush` has zero sites in both arms, and the hook is live in both.** The wrapper
tail-branches (`b`), so a census of `bl` to the hook's own symbol sees nothing. My first run reported
`0 site(s)` for both arms and I nearly read it as "the seam is dead in both" — the same shape as
reading silence as a reading. §4 is the corrected instrument: census the *wrapper's* callers, and read
the wrapper's body.

Both are the project's most-repeated class, and both were caught by the same discipline: **run the
instrument on an input whose answer you already know.**

## 6. What was parked, and where

`/mnt/data/mi4-ios6-export/arm-574-idle-no-sleep-0/` — outside the repo, outside `/tmp`, outside the
job directory. The only previous copies were `/tmp/r594/frozen-payload/` and
`$CLAUDE_JOB_DIR/tmp/frozen-574/`, neither of which survives a cleanup, and this is the only artifact
that can produce 638's pair.

| file | sha256 |
|---|---|
| `xnu_arm_entry.bin` | `151425c40c48cb1a417de1e4c570cb812f5b07a2e45aa41b04421c63fe34d746` |
| `xnu_arm_entry.elf` | `3bc726056dfb90eeee4fbcb5afcfec32b8b356a187576a8f63ecaaad8af188c6` |
| `xnu_arm_entry-config.txt` | `5073b0c4972bf91364851552262a3bbd757a5643a0f23eb1416d4a6b7675c851` |
| `xnu_arm_entry-sources.txt` | manifest, build moment 2026-09-23 04:56 |
| `stage90.bin` | `0f108392b9b20fc025311ea0b71c2a0cec5e8760e7e39867f68994cf5e2092a3` |
| `stage90.elf` | `c869a319028d324b429c77b63b498ae35b56fa4d39771db9ae82ee1a261fa35f` |
| `stage90.img` | `8274b1c4ae2dfc5ea317e17a0cf3f6e024e3bf7eaafdef964cabc5b66d75f339` |
| `stage90-qcdt.img` | `914f45ac7098341b25c027f71e8551d11c9b13f4f40d3bd9e470714435e2939a` |
| `stage90_fixture.macho` | `52bc9c357068b791f777bb9abd2e879fa5b0b27364267220b01215dc5faec97b` |

Every copy was verified against its source, and the park carries its own `SHA256SUMS.park.txt` with
park-relative names, verified in place → all OK. `914f45ac…` is the hash `experiment-575` calls the
frozen 574 pair.

**And one warning this park taught, because it will bite the next reader.** The park also holds the
copied `SHA256SUMS.txt` from the live tree, whose paths are **absolute and address
`/mnt/data/mi4-ios6/out/stage90/`**. Run inside the park, `sha256sum -c SHA256SUMS.txt` reads the
**live tree** and reports four `FAILED` — the live images have since been replaced by the armed build.
Nothing in the park failed. **A manifest is a claim about a directory and it carries no directory**;
this is the same reason the project's rule is *never verify a park with `sha256sum -c`*, and the reason
to write the manifest the park can actually be checked with.

## 7. Built, parked, unrun — and why the distinction is not bookkeeping

`experiment-594` §1: "The frozen 574 arm is untouched (`914f45ac…` / `151425c4…` / `3bc72605…`) and
**unrun**." No log for it exists.

So 638 and 640 are right that the pair has never been **read**, and wrong that the arm has never been
**built**. The distinction has teeth because of what a reader does next: 638's banner and 640 §4 both
tell the next reader that this arm is still to be built, and the prescribed way to get one is
`build_entry.sh` — which, **run with the catcher armed, silently swaps the armed image** (636,
hardware-run safety gate). A reader who follows the sentence literally spends the owed press on
nothing. That is the operative harm, and it is why the fix here is a corrigendum and a park rather than
a build.

Both documents now carry a dated corrigendum (`experiment-638` §7, `experiment-640` §9) that leaves the
original sentences visible and quotes the measurement. 638's pre-registration is **unread, not
falsified**, and stands as written for the arm in the park.

## 8. What this does not do

* **It does not run anything, and it does not move the owed press.** The armed image is untouched
  (`out/stage90/stage90-qcdt.img` is still `60063c47…`, still **UNRUN**). The owed press still sends
  the sleeper arm, and 640's reading of it is unchanged: no `xnu_live_seam_*` key, and a silent clause
  (5) is the preamble's sentence rather than a verdict.
* **It does not boot the parked arm, and it is not a claim that the parked arm would produce the
  pair.** It is built and its window is reachable; whether `_b1`/`_a1` actually appears is what a run
  decides, and that is 638 §3's pre-registration, not this step's measurement.
* **It no longer says the parked payload would gate clean — that has since been measured, and it
  does.** §9 ran `preflight_boot_check.sh` on the park's bytes in a scratch copy: the sources clause
  passes (20 of 20 files, none the manifest does not name, bound to `151425c4…`) and the gate exits
  **0**. The caveat that stood here — never through the gate, and a source edit since 2026-09-23 could
  make the sources clause refuse — is answered on both halves. The remaining refusal this arm meets is
  the **freshness sweep**, an mtime artifact, and §9 names how to avoid it.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The OS is still not observed
  booting, and **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.
* **It does not touch `out/`, the runner, the gate, or the peer's files.** No file in `stages/`,
  `tools/` or `out/` was modified; the only repo writes are the two corrigenda and this document.

## 9. The parked arm through the gate, measured 2026-09-24

§8's caveat said this arm had never been through `preflight_boot_check.sh`. §6 and §7 answered only the
**sources** half of it, by hand, with the gate's own pipelines. This section takes the gate's verdict
itself — on a **copy**, never in `out/`.

**Method.** Under the job's temp directory, a copy of the tree
(`cp -a stages tools docs Makefile README.md` plus `cp -a out/stage90`), then the park's ten payload
artifacts written over the copy's, then `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` run
in the copy. Mtimes are preserved on purpose: the gate's freshness sweep compares source mtimes against
the image's own, so a copy made with `git archive` (which does not preserve them) makes the *control*
refuse — which is how the first attempt at this measurement was spent.

| run | bytes in `out/stage90/` | `SHA256SUMS.txt` | mtimes | result |
|---|---|---|---|---|
| **control** — the copy as-is | armed | live | fresh | **exit 0, 626 lines** |
| naive substitution | **park** | live (absolute paths) | park's (01:33) | exit 1, 31 lines — **freshness sweep** |
| **faithful arming** | **park** | **park's** | refreshed | **exit 0, 536 lines, no refusal** |

The control reproduces the live tree's own gate run line-for-line (626 lines, arm line at 55), so the
other two rows are readable as differences from it.

**The middle row's refusal is the freshness sweep, not the sources clause**, and it is an mtime
artifact of a different build session. It names `macho_fixture.c`; the fixture that file produced is
**byte-identical** between the park and the live build (`stage90_fixture.macho` `52bc9c35…` and
`stage90-build-config.txt` `6c2b6038…` on both sides), and the live fixture was regenerated at
05:02:43 during the armed build, after this park's 01:33 payload. In the gate's own words, *"a
checkout or a master mirror bumps an unchanged file's mtime"* — which is exactly what this is.

**With a manifest that describes its own directory and fresh mtimes, the park gates clean.** The
sources clause prints, of this park's entry bin:

```
== the entry image's own sources ==
the entry image is the build of xnu_arm_boot/ as it stands: 20 file(s), every one matching the
manifest, and none the manifest does not name
```

and the `== which arm the entry image in out/ is, in words ==` block reads `IDLE_NO_SLEEP=0` and prints
what 638 §3 pre-registered for this arm, verbatim: *"514's one repair IS in this image … **the window
whose `pop {fp, pc}` is this phase's frontier IS ENTERED**, so everything below … is narration about
THIS arm, and its keys are expected PRESENT."* That block is **18 lines** here against **113** for the
armed arm; the two runs differ in five hunks and 162 diff lines, the rest of them the config line and
the manifest paths.

**Two substitutions were necessary, and neither is free — arming this park by hand needs both:**

1. **The manifest must be the park's own.** The park also carries a copy of the **live**
   `SHA256SUMS.txt`, whose paths are absolute `/mnt/data/mi4-ios6/out/stage90/…`. Used as-is it
   verifies the live tree *from inside the park* — a check about a different directory than the one
   under test, and one that reads OK while the artifacts it is supposed to cover are the park's. The
   park-relative `SHA256SUMS.park.txt` is the one that belongs at `out/stage90/SHA256SUMS.txt`.
2. **The mtimes must be fresh.** An arming that preserves them (`cp -a`, `mv`, `tar -p`) is refused by
   the freshness sweep, because `macho_fixture.c`'s 05:02 mtime is newer than this park's 01:33
   payload. An ordinary `cp` is not refused. The refusal is false in content and real in effect: it
   would spend the press on nothing.

**A corollary for the live gate, and for 640's repair.** The seam sentence *"for this arm
`xnu_live_seam_calls` is present"* is printed by **both** runs — it is **true of this park** and
**false of the armed arm**, whose window is unreachable (640 §2). So the contradiction 640 reports is
a single-arm falsity, and the guard its repair adds (`if [[ $V_IDLE_NO_SLEEP -eq 1 ]]`) is exactly the
discriminator rather than a blanket suppression. Nothing about that repair's timing changes: it is
still staged and unlanded, and the press it waits behind is unchanged.

**What this section does not do.** It does not boot the park, and *"it gates clean"* is not *"it
boots"*: whether the `_b1`/`_a1` pair appears is 638 §3's pre-registration and a run's to settle. It
does not arm the park — `out/stage90/` still holds the sleeper image (`60063c47…`, **unrun**), and a
park written into `out/` by hand is not the same act as a gate run in a scratch copy. What it changes
is one thing only: the press *could* now be spent on the arm that can answer 638 §3's question, at the
cost of an arm swap that 636 measured to be **silent**. That choice is the operator's.

## 10. Safety

No device action, no boot, no build, no `fastboot`, no `adb`, **nothing written to storage**. The
commands were `sha256sum`, `diff`, `tail`/`head`/`cmp`-style reads, `nm` and `objdump` on two frozen
ELFs, and one host-only run each of `tools/check_idle_window_unreachable.py` (exit 0 on both) — none of
which touches the device. No file under `out/` was written: the park was made by **reading** `out/` once
as a control and copying only from `/tmp/r594/`. `fastboot boot` only - never `flash` - so no outcome of
this step can write to storage, and nothing here arms, fires or re-arms the catcher.

§9 added two host-side runs of `preflight_boot_check.sh --allow-xnu-entry` (the gate is a reader: it
never runs `fastboot` or `adb`) and one `cp -a` of `out/stage90` — a **read** of `out/`, with every
byte written under the job's temp directory. `out/stage90/stage90-qcdt.img` is `60063c47…` before and
after, the catcher's relay and armed watcher were not signalled, and the repo tree was clean at the
end of the step.
