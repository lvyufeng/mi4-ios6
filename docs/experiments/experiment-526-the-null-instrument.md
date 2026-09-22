# 526: the idle window's readings, replaced by a counter

**The arm is a metrology null: 522's image with 521's capture taken out of the idle exit's wrapper.** No
state change is added and none is removed - 522's `entry_idle_cache_enable` is still in the enter wrapper
and 517's exit-side flush is still out of the image - so a run of this arm holds the treatment constant and
varies only the measuring apparatus. That is what makes its outcome readable in both directions: if the
device comes back, the readings cost something; if it does not, the cost is the state change.

**The run has not happened. The device is off the bus** (last `usb 3-10` event: 522's `18d1:d00d` device 88
disconnecting at 14:14:46 on 2026-09-22) **and needs a power press before it can.** The gate is green and
the image is frozen.

## 1. Why a null, and why this null

522's run produced a ledger rather than a reading (experiment 522 section 3.3.1):

| run | idle window's state | capture in the exit wrapper | returned? |
| --- | --- | --- | --- |
| 519, 520 | `SCTLR.C = 0` throughout | 520's shape: the publisher read `[sp-16, sp)` itself | **yes**, inside 180 s, dying at the `pop` |
| 521 | `SCTLR.C = 0` throughout, **+ the L1 & L2 clean-and-invalidate** | **521's shape: the caller loads the four words, twice** | **no** |
| 522 | **`SCTLR.C` set again at the window's near end**, flush out | 521's shape | **no** |

Two things follow from that table and one of them is the reason this arm exists:

* **521's section 4 reading - "flag-off runs come back, flag-on runs do not" - is falsified.** 522 is
  flag-**off** by construction and did not come back. The flag was never the discriminator.
* **The capture is the one thing that separates the returning runs from the non-returning ones.** It is in
  521's image and in 522's, and in no image that has come back; 521 and 522 differ from each other only in
  *which* state change they carry, and 520 carries neither a state change nor that capture's shape.

So the apparatus and the treatment are confounded, and the way to separate them is to hold the treatment
and remove the apparatus - which is what a null instrument is for. **It is not the arm "520 with fewer
readings"**: 520 has neither 521's capture nor any state change, so it cannot say which of the two mattered.
This arm has 522's state change and no capture, so its two outcomes are both informative:

* **it returns** → the readings (or a store of theirs) cost something. The next arm bisects them: the two
  loads *before* the call to the real exit or the two *after* it, since the first pair runs before Apple's
  `push {fp, lr}` has filled the slot and the second runs after the `pop` has consumed it.
* **it does not return** → the cost is the state change, and specifically 522's `SCTLR.C` write, since the
  exit-side flush is out of this image and 520 shows the window is otherwise survivable.

## 2. What the image contains, read off it rather than asserted of the source

`STAGE90_XNU_SLOT_NULL=1` is the arm; the default is 0, which is the instrument the last two runs carried.

The exit wrapper in this image (`0x8047c974`, the same address as 522's), disassembled:

| address | instruction | what it is |
| --- | --- | --- |
| `0x8047c974` | `str r4, [sp, #-8]!` | the frame: 8 bytes, and it is the slot's address |
| `0x8047c978` | `str lr, [sp, #4]` | its second half |
| `0x8047c97c` | `mov r3, sp` | **one** read of `sp` off the register, kept |
| `0x8047c988` | `bl entry_slot_null_note` | site 1: the count, before the call |
| `0x8047c998` | `bl entry_slot_rtc_note` | 519's reading 5, unchanged |
| `0x8047c99c` | `bl platform_cache_idle_exit` | the real exit, called once |
| `0x8047c9a8` | `bl entry_slot_null_note` | site 2: the count, after the call |
| `0x8047c9d0` | `b entry_note_pcx` | 516's exit-side record, unchanged |

and between them **no** negative-offset load and **no** store into a slot table, where 522 has eight of each
off `sp` into `g_slot_pre`/`g_slot_post`'s `pend_*` words at offsets 32/36/40/44. The `mov r3, sp` stays even
though the counter does not need the address: it keeps this image's frame, its stack traffic and its
instruction positions as close to 522's as the arm can be while the capture is gone, so the two images can be
compared instruction by instruction rather than by a diff of their sources - and the clause holds both arms
to it (one `mov r?, sp`, one 8-byte decrement, one restore).

What the two sites publish is `xnu_live_slot_pre_calls` and `xnu_live_slot_post_calls` and nothing else:
same `entry_slot_publish` schedule (`<= 4`, then the powers of two), same `entry_live_ready()` gate. The four
word keys and `_sp` are absent from a null run's log, and that is the reading: **a count present with the
words absent says the site ran and published no words**, which 519's section 11's decision rule needs to
stay distinguishable from "the site never ran" (key absent). This is also why the null does not simply call
the real publisher with a stale table: it would republish the previous pass's four words as if they were
this pass's.

## 3. What was checked rather than intended

**The switch reaches the file it names.** The same tree, built with `STAGE90_XNU_SLOT_NULL=0`, reproduces
522's frozen image **byte for byte**: `13d771938336fe65ccaf3dee833c14c0a9280d5365eda3f5c1e74cc6e6948825`.
That is the strongest form of the claim this arm rests on - the two images differ by the capture and by
nothing else - and it is the check `mi4-off-option-two-spellings` asks for, done the only way that cannot be
fooled: by rebuilding and comparing the file.

The image built for this arm: `7819cddb50d5a341332aa89d4b6f0479e098e1a19f7d46cc9a2df3a1875ff3e4`, entry bin
**5,519,996** bytes (the same as 521's and 522's), payload `stage90.img`
`1c9491195e94c09113f834051ccb2f41b740ea7a04eaac136de20ed640d4457d`. A second build of the same switch
reproduces the same hash.

**The clause was refusal-tested against the state it exists to refuse.** The two `awk` programs the clause
uses are run verbatim against the *capture-bearing* wrapper - 522's image, which is what `SLOT_NULL=0`
produces - and their output is the refusal:

| matcher | against 522's wrapper | what 526's clause expects |
| --- | --- | --- |
| note counts | `entry_slot_note` **2**, `entry_slot_null_note` **0** | 0 and 2 |
| capture loads | **8** negative-offset loads, offsets `16 12 8 4` twice | 0 |
| table stores | **8** stores, offsets `32 36 40 44` twice | 0 |
| the pair's shape | `bl entry_slot_note` at `0x8047c9ac` and `0x8047c9f0`, the real call at `0x8047c9c0` | two null sites straddling the call |

so every one of 526's predicates fires on the image it is meant to reject, and none of them fires on the
image it is meant to accept. This is the repository's rule for a new check - *run it once against the state
it is meant to refuse* - and it is also where two defects were found, both in the check and both caught by
the build:

1. **The count was computed after the assertion that reads it.** The first build of this arm printed
   `entry_slot_note 0 time(s) and entry_slot_null_note 0 time(s)` and stopped: the field the assertion
   tested was filled by an `awk` that runs forty lines *below* it, so `${sxw_null:-0}` was the empty
   variable's default and the image was refused for a reason that did not exist. The count moved into the
   `awk` that runs before the assertion - one value, one definition, and defined early enough to be read.
2. **The pre/post straddle assertion was 520's, unconditionally.** With the capture gone there are no
   `entry_slot_note` calls, so its `first < real < last` test read `0 < x < 0` and refused a correct image.
   It is now the arm's own question - two null sites straddling the call - because that shape is also part
   of what the arm is: one site, or two on one side, would be a different instrument whose count could not
   say whether a pass reached the call.

Neither defect could have reached hardware: both stop the build. Worth recording all the same, because both
are the class this project keeps meeting - a check whose *shape* was copied from the previous step's arm
without being re-read against this one's.

## 4. What the run is to be read for, written before it happens

1. **Does the device come back at all.** This is the experiment rather than a criterion of it, and the
   host's own USB log is the reading if it does not (a return is `18d1:d00d` -> `2717:0368` -> `18d1:4ee7`
   inside ~20 s; a hang is one dead second in fastboot with nothing after it).
2. **If it returns**: `xnu_live_slot_pre_calls` and `xnu_live_slot_post_calls` present and rising, with
   `xnu_live_slot_pre_sp`, `_pre_m16`, `_pre_m12`, `_pre_m8`, `_pre_m4` and the `_post_*` five **absent** -
   the null ran and published a count. Absence of the `_calls` keys is the other reading (the site never
   ran), and the two must not be read as one.
3. **The enable's own readings**: 522's block in `run_and_capture.sh --summarise` fires on
   `xnu_live_slot_cwe_*`, which this image still carries - `_win` with `C` clear, `_set` with it set and
   `_calls >= 1`. A run of this arm therefore answers 522's question too, from the same log.
4. **The ending**: 520's `sleh_storm 9` at the same `pc` would say the mechanism is neither the capture nor
   the re-enable, and any later ending than 520's is progress.

## 5. What this does not decide

It does not decide which of the readings costs anything: if the arm comes back, the bisection (before the
call, after the call) is the next arm and not this one. It does not give the idle exit a mechanism - 526
removes an apparatus; it does not name an instruction. And it cannot say anything at all if the run hangs,
because a non-return still takes the log with it: that is the phase's real cost, it is why 523's console
item matters beyond the console, and it is unchanged by this step.

What comes after 526 is 523's phase, not another cache step: the OS's root is an 8 KB Mach-O stub and its
filesystem is a mockfs with one fixture file, so 「挂载存储」 has not begun - and per 523 and 524 there is no
HFS in this tree and the console's last link is off because `uart_initted` is zero.

## 6. Safety

Unchanged and untouched. This step is a build and a gate: **no build script is edited while it runs, no
device was touched in this session, and nothing is ever flashed** - the failure mode of a run remains a hang
that needs a power press, which is what 521's and 522's runs produced. `fastboot boot` only, one
non-persistent boot per run, through `preflight_boot_check.sh --allow-xnu-entry` then
`run_and_capture.sh --allow-xnu-entry`, with the gate green and printing `7819cddb50d5a341...` computed from
the image file itself.
