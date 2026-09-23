# 577: the arm cannot write the slot, and now the build says so instead of the argument

574 section 2 stated the safety argument for the arm that is one power press away from being booted, and
then said which part of it was **not** asserted: *"that the body contains no store to the slot … a store
is not distinguishable from the frame's own pushes and the live-channel writes by opcode alone"*. That
was true of the check as it stood and it is the weakest link in the argument, because a store to the slot
writes the very two words the exit's `pop` is about to read - the object 546's mechanism is about. This
step closes it: the slot's register is identified **from the body itself**, and no store may use it.

Host-side only. `build_entry.sh` is a build input, so the entry image was rebuilt to prove the clause
does not move it: the bin is byte-identical at `151425c4…`, and the gate is green after the change
(`EXIT=0`, 0 UNREAD). The parked 574 arm is still the arm - what changed is what stops the build.

## 1. The identification, and why it is not a convention

The arm is entered with the slot in `r0` (the `naked` trampoline's `mov r0, sp`), which is why the naive
form of this check - "no store whose base is `sp`" - finds nothing on either arm: the stores that matter
name a **copy** of the address, not `sp`. So the slot's registers are identified from the body:

1. **a load base used with both offset 0 and offset 4** - that is the arm's pair of reads, which *are* the
   slot by definition (`_b0`/`_b1` and `_a0`/`_a1` are `[slot]` and `[slot+4]`); `sp` is excluded, because
   the frame is not the slot;
2. **closed under `mov rX, rY`** - `mov r5, r0` copies it, and 535's restore stores through the copy.

Measured on both parked bodies, same registers, opposite verdicts:

| image | slot register(s) | stores to them | what that is |
| --- | --- | --- | --- |
| `574-xnu_arm_entry.elf` (the measurement arm) | **r0, r5** | **0** | the claim, now a reading |
| `535-xnu_arm_entry.elf` (the operation arm) | r0, r5 | **2** - `8047ca74: str r7, [r5]` and `8047ca78: str r6, [r5, #4]` | the two restore stores, on purpose |

**The second row is the check's falsification, and it is why this is a check rather than a claim.** A
clause that has never been seen to fail is a clause nobody can trust, and the image that makes this one
fail already existed in `out/stage90/captures/` - so the negative control cost nothing but the awk.

## 2. What the clause asserts, and the body it reads

In `build_entry.sh`'s measurement branch, over the same disassembly the arm's other clauses read:

```
slot-register(s)=2  stores-to-slot=0
```

and it refuses three ways: a store to the slot (the operation arm wearing this one's name), **no slot
register identified at all** (a body whose reads cannot be located is a body whose stores cannot be
checked - and the store check would then pass by finding nothing, which is the silence-is-not-a-reading
defect [[mi4-silence-is-a-reading-only-if-success-is-silent]]), and the two counts are printed together
because the second is meaningless without the first.

That closes 574 section 2's list. The arm's safety now rests on four properties read out of the image,
none of them from prose: `--wrap` in the link with 4 sites redirected and 0 direct; no
`FlushPoC_DcacheRegion` call and **no coprocessor instruction of any kind** in the body; the same four
publications; and **no store to the slot**.

## 3. The peer session is adding the same property on the gate side, and the two are not duplicates

`preflight_boot_check.sh` is being extended (in the working tree as this is written, not by this step)
with a section that reads the body out of the image and narrates it, and its `SEAM_MEASURE` paragraph has
already been corrected from "no store of any kind" - which was **false** of the body, whose prologue and
counting words are stores - to "no store **to the slot**". That correction and this clause are the same
finding reached from two sides, and they are not duplicates: the gate's copy is the thing a person reads
immediately before touching the device, and this one is the thing that **stops the build**. A build that
carried the store would never reach a gate - which is the wanted order, and the reason the property is
asserted in both places rather than one.

## 4. Safety

No device action. One build-input edit (`build_entry.sh`), one `build_entry.sh` run to prove the artifact
does not move (`151425c4…` and `3bc72605…` unchanged; the sources manifest rewritten to match, which is
what the gate hashes), one read-only gate run (`EXIT=0`, 0 UNREAD), and the two disassembly measurements
in section 1 against artifacts already parked under `out/stage90/captures/`. Nothing was flashed, nothing
was written to storage, no payload was rebuilt.

## 5. State

- The arm is unchanged and unrun: entry `151425c4…`, elf `3bc72605…`, qcdt `914f45ac…`, parked under
  `out/stage90/captures/574-*`.
- **Owed before the boot: the user's power press** - the device is off the bus entirely and the gate is
  green, so nothing else stands between this arm and its one boot.
- TWRP stays withheld: 「如果os已经能进去了的话」 is unmet.
