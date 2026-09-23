# 583: the same predicted word in a second key, and the exit has exactly one caller

582 derived the one address a correct idle frame holds at `slot+4` - **`0x8047c990`**, the return site of
the wrapper's `bl platform_cache_idle_exit` - and attached it to the seam's `b1`. Two consequences follow
from the frame's own arithmetic and from a call census, and both make the coming run's reading sharper
than 582 left it. Nothing is built or run; the arm is unchanged.

## 1. `b1` and `xnu_live_slot_pre_m4` are the same address, so the prediction lands twice

From the live arm, read off the image rather than reconstructed:

| site | instruction | effect |
| --- | --- | --- |
| `8047c964` | `str r4, [sp, #-8]!` | entry `sp` `X` -> `X-8`; `[X-8]` = `r4` |
| `8047c968` | `str lr, [sp, #4]` | `[X-4]` = the wrapper's own `lr` |
| `8047c96c` | `mov r3, sp` | `spv = X-8` - the register the capture is taken from |
| `8047c98c` | `bl platform_cache_idle_exit` | the real exit is entered with `sp = X-8` |
| `800462d4` | `push {fp, lr}` | `sp = X-16`, `[X-16]` = `fp`, `[X-12]` = **`lr`** |

and `STAGE90_SLOT_CAPTURE` reads `spv-16`, `spv-12`, `spv-8`, `spv-4` = `X-24`, `X-20`, **`X-16`**,
**`X-12`**, named `m16`, `m12`, `m8`, `m4`. So:

```
slot      = X-16 = slot_pre_m8      and    b0 = [slot]
slot + 4  = X-12 = slot_pre_m4      and    b1 = [slot+4]
```

**`b1` and `xnu_live_slot_pre_m4` name the same word.** The reader already prints both, side by side, as a
*reading* ("equal to `b0`/`b1` when the frame did not move between two passes") - and the capture is taken
**before** the real exit is called, so on a warm frame `pre_m4` is the *previous* pass's pushed `lr`, which
is the same address. **So 582's prediction lands in a second key that the older instrument already
publishes**: on 574's run, `xnu_live_slot_pre_m4` should also be `0x8047c990`, and it says so
independently of the seam.

That converts the existing "equal or moved" reading into two distinguished states rather than one:

| `b1` | `pre_m4` | reading |
| --- | --- | --- |
| `0x8047c990` | `0x8047c990` | the push's frame is in memory and the frame did not move - 546 §1's premise, measured |
| `0x8047c990` | something else | the push reached memory, but the **frame moved** between two passes - a fact about the idle loop, and now a *distinguishable* one, because `b1` is right |
| not `0x8047c990` | anything | the push's store did not put this code's return address in the word the `pop` reads |

Before the derivation the middle row and the last row were the same event ("the pair is unequal"); now they
are not.

## 2. And 576 §3's open question is decided, by a header that was already in the tree

576 §3 recorded 520's `xnu_live_slot_pre_m4 = 0x8047c974` and left open whether "the capture is taken
before the push, so those words are the previous occupant's, or the value at that address is not the
exit's `lr` at all". It is neither of the two as stated: **it is the old instrument's own frame.**
`entry_slot_capture.h` says so in its opening paragraph - 520's run read the slot through `entry_slot_note`'s
own call frame, and `0x8047c974` is "the instruction after the `bl <entry_slot_note>` at `0x8047c970`, i.e.
the note's *own saved `lr`*" - which is precisely the defect 521 was written to fix by capturing in the
caller before any call.

**The consequence for this session is the reason it is worth writing down: 520's log cannot be used to
falsify 582's prediction.** The word it carries is not a slot word at all, so `pre_m4` in that image is not
evidence about `0x8047c990` - in either direction. (The rehearsal input parked in `581/582-rehearsal/` is
synthetic and was built with that key replaced, which is why it does not carry the artefact.)

## 3. The exit has exactly one caller, so the prediction is unconditional

The census over the whole image: exactly **one** instruction reaches `platform_cache_idle_exit` -
`8047c98c: bl 800462d4 <platform_cache_idle_exit>` in `__wrap_platform_cache_idle_exit`. There is no second
caller that could push a *different* `lr` into the same slot, so `b1 = 0x8047c990` holds for every pass
through the seam, not only for one call site's worth of them. (If there were more than one, the prediction
would have to be a set of values keyed by caller, and the seam's own `lr == 0x800462dc` filter would not
have separated them - it names the *flush* call's return address, not the exit's caller.)

## 4. Safety

No device action, no build, no build input, no arm: one read of the image's own disassembly (the wrapper's
prologue, the capture macro's four offsets, the call census) and one read of a header already in the tree,
against a document (582) landed minutes earlier. The frozen 574 arm is byte-identical and unrun, the device
is off the bus, and the boot waits only on the user's power press.

TWRP stays withheld: 「如果os已经能进去了的话」 is unmet.
