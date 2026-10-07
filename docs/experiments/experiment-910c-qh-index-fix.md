# The qh index is the register bit — a defect in 910b's EP1-IN, found in the 910c arm

Date: 2026-10-07. **A host-side finding, no device action.** While opening the next rung on the USB ladder
(the bulk-OUT mirror a later `adbd`-shaped rung needs), a re-derivation of the queue-head graph showed
that **910b's one bulk IN endpoint points the core at the wrong queue head**: `STAGE90_USB_ENUM_QH_IN1`
is `1`, which is EP1-**OUT**'s slot, and EP1-IN's is `17`. The constant is a claim in a comment that no
check recomputed, so the two parked arms that carry it (`880a3568` 910b, `77a51d33` 910c) would have
enumerated a bulk-IN endpoint that never moves a byte. Both are **unpressed** and this is a fix-only
change, so they are **rebuilt and re-parked**; the old parks are superseded, not deleted.

## 1. The fact, and its owner

The ChipIdea core indexes its endpoint **list** (the `ENDPOINTLISTADDR` array of queue heads) by the same
number as the endpoint's **register bit** in `ENDPTPRIME`/`ENDPTCOMPLETE`/`ENDPTSETUPSTAT`:

    index(ep N, dir) = N + (dir == IN ? hw_ep_max/2 : 0) = N + (dir == IN ? 16 : 0)   // hw_ep_max = 32

Three independent statements in the vendor tree agree, and none of them is a comment:

1. **`ci13xxx_udc.h:166`** — `#define ep0in ci13xxx_ep[hw_ep_max / 2]`, and `:165` `ep0out ci13xxx_ep[0]`.
   `hw_ep_max = DCCPARAMS.DEN * 2` (`ci13xxx_udc.c:316`); `ENDPT_MAX` is `32` (`ci13xxx_udc.h:23`), and
   the vendor comment gives `DEN = 16` physical endpoints (`ci13xxx_udc.c` §hw_ep_max). So ep0in = `[16]`.
2. **`ci13xxx_udc.c:2666`/`:2672`** — `isr_tr_complete_handler` walks `for (i = 0; i < hw_ep_max; i++)`
   with `mEp = &udc->ci13xxx_ep[i]` **and** `if (hw_test_and_clear_complete(i))`, and `:570`/`:775`
   `hw_test_and_clear_setup_status(i)` / `hw_test_and_clear_complete(i)` apply `ep_to_bit(i)` to the same
   `i` for the register bit. The **ep-array subscript and the register bit are the same number, `i`** —
   which is only true when the endpoint list is also indexed by `i`, because `ENDPOINTLISTADDR` is set to
   the ep-array's own base (`ep0out.qh.dma`, here the identity-mapped `g_usb_qh[0]`).
3. **`msm72k_udc.c:212`/`:186`** — the sibling driver (the same silicon) says it in words: *"endpoints are
   ordered based on their status bits, so they are OUT0, OUT1, … OUT15, IN0, IN1, … IN15"*, with
   `#define ep0in ept[16]`. Both drivers place ep0in's qh on register bit 16.

Therefore **EP0-IN is qh 16, EP1-IN is qh 17**, and `QH_IN1 = 1` names the second **OUT** slot.

## 2. What the arm did, and why it looked right

`entry_usb_enum.h` defined `QH_IN0 = 16` (correctly, cited to §10.8) beside `QH_IN1 = 1`, under a comment
that said "explained" rather than "checked": *"endpoint 1's qh; RX/TX are two halves of ONE ENDPTCTRL(n),
but the qh array is direction-split."* That sentence is true and it is the trap: it noticed the
direction-split and then did not apply it. `docs/experiments/experiment-910b-design.md:75` is the origin —
`QH_IN1 = 1u  /* the one enumerable IN endpoint's qh */` — and §10.8's own line (`…so it lands at
list + (hw_ep_max/2)*64`) computed the **16** for EP0-IN in the same paragraph, so the file held both the
rule and its violation six lines apart ([[mi4-one-value-two-definitions]]).

**The prime bit was right, and that is what hid it.** `STAGE90_USB_ENUM_EPBIT(EP_IN, 1) = 1 << (1+16) =
BIT(17)`, so 910b primed the *correct* register bit while building the qh at index `1`. The core, on
`ENDPTPRIME=BIT(17)`, fetches qh[17] — which the arm never wrote, so it holds the reset-zero `g_usb_qh`
neighbour — reads a terminated dTD, and moves nothing. The host's `read()` on EP1-IN NAKs forever. Nothing
in the log would look wrong: `xnu_live_usb_enum_configured=1`, `in_reads=0`, no fault. That is the whole
reason this needed a recomputation and not a press.

**Blast radius.** Any 910b/910c arm. 910b's verdict cell ("`lsusb -v` shows EP1-IN and a read returns the
magic") would have failed as a *silent* NAK, not a fault. 910c's stream (the same qh 1) likewise. 910a and
910a2 are untouched — they never build a qh graph. **No press consumed any of this**; the arms are parked,
not sent.

## 3. The fix

**One constant, and a recomputation that refuses it if it drifts again.**

- `src/entry/entry_usb_enum.h`: `STAGE90_USB_ENUM_QH_IN1` `1u` → **`17u`**, with the derivation and its
  owner at the definition, and `QH_IN0`'s comment corrected to state the rule once (`num + dir*ENDPT_MAX/2`).
- `tools/check_usb_enum.py`: a new clause **(1b)** recomputes every qh index from the owner's `ENDPT_MAX`
  (`ENDPT_MAX/2 = 16`) and the header's own `ENUM_EP_IN`: it requires `QH_OUT0 == 0`,
  `QH_IN0 == ENDPT_MAX/2`, `QH_IN1 == EP_IN + ENDPT_MAX/2`. A new selftest mutation
  (`the EP1-IN qh index loses its direction half`) restores `1` and is refused, so the battery is 6/6.
  The guard's docstring now names the index as a recomputed quantity, not only the struct offsets.

The immediate `17` reaches the linked image unchanged in width (a `movw`/`ldr`-literal), so the entry bin
is the **same 6,498,612 bytes**; only the value differs ([[mi4-stand-in-size-is-not-value]] in reverse —
same size, *right* value).

## 4. Why the parked arms are rebuilt rather than left alone

The standing rule is *a parked arm is a record, not a queue; a PARKED arm is never re-pressed*. It does not
say a parked arm is never **corrected** — and this is the one case where correctness wins:

- **The arms are UNPRESSED.** Nobody has sent `880a3568` or `77a51d33` to the device. The 8-hex addresses
  name artifacts, not facts about the device; correcting an unsent artifact costs nothing and loses no
  measurement.
- **The change is fix-only and switch-neutral.** `QH_IN1` is used by 910b's fixed-magic prime and 910c's
  stream prime and by nothing else; no arm *identity* (the switch set) moves — the entry-config diff is the
  artifact hash and bytes, and the bytes do not move either. The new 910b/910c arms differ from the old by
  exactly this constant.
- **The alternative is worse.** Leaving `1` in place would park an arm known not to work and would let a
  future 910d inherit a silent NAK as if it were a real result. The 677 precedent is the same posture: an
  unpressed arm with a proven defect is rebuilt, and the record says which is the live one.

**The corrected arms are NEW sets, and the old parks stay on disk.** `out/stage90/frozen/` keeps
`armed-storage-880a3568/` and `…-77a51d33/` untouched — they are the only record of the bytes the defect
was in, and a set name must keep hashing to its own member (`tools/check_set_name_rule.sh`), so
re-using the name for different bytes is refused by construction. The corrected arms are parked beside
them under their own names, taken from the corrected entry image's own hash:

    armed-storage-979ded08   = the corrected 910b   (STREAM=0; entry xnu_arm_entry.bin = 979ded08…)
    armed-storage-6deae815   = the corrected 910c   (STREAM=1; entry xnu_arm_entry.bin = 6deae815…)

`records/revert-set.txt` gains both sets' lines, and their `role=` text says plainly that they **supersede
`880a3568`/`77a51d33`**, which remain the defective arms. That is the project's standing posture —
a record is superseded, not edited — and it is the one that keeps both facts: the defect's bytes and the
fix's bytes.

**One consequence the parking has to respect.** The entry switch `STAGE90_XNU_USB_STREAM` is an *entry*
switch, but it moves the whole **link**: the stream arm's object is linked only when `STREAM=1`, so the
910b entry ELF is 7,711,612 bytes and the 910c one is 7,711,968 — **different lengths**, hence different
entry hashes and (task: confirm) different payload bytes. That is exactly the `6c2b6038` shell the
`stage90-build-config.txt` record cannot see through, and the reason the arm row joins the ELF reading to
the entry record rather than trusting either alone.

## 5. What this changes about the ladder, and the next rung

- **910b's press is now a press of a working enumerable endpoint.** Its verdict cell (a host reads
  `0x910b0910` off EP1-IN) is, for the first time, reachable.
- **910c's press** is now a stream that can actually drain: the cursor over the RAM console ring primes
  qh[17] and the host's `read()` returns the boot's own `xnu_live_*` lines.
- **The next rung (bulk-IN/OUT for a receive path)** now has a correct IN qh and — this is the reason the
  fix surfaced — a **direction fact to reuse**: an OUT endpoint N is at qh `N` (the half offset is 0), so
  EP1-OUT is qh `1`, the very slot the bug was mistaking for EP1-IN. A later rung that adds the bulk-OUT
  mirror will define `QH_OUT1 = 1` **by the same rule**, and the guard will check it.

**The honest status of the goal is unchanged.** This found and fixed a defect in an unpressed rung; it
does not mount anything, does not press anything, and does not move 「彻底能直接开机就运行xnu」. Residence
(909) is still the wall every USB channel goes dark behind, and the presses are still the operator's.