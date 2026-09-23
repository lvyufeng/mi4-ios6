# 582: the return site is in the wrapper, and `b1` now has a derived value to be measured against

546 section 1's premise has a wrong value, and correcting it turns the coming run's most important word
from "does it look plausible" into a **predicted address read out of the image**.

This is a reader change plus one measurement. No device action, no build, no arm touched: the entry image
is still the frozen 574 arm (`151425c4…` / `3bc72605…`), the gate is green on it (`EXIT=0`, 0 UNREAD), and
the phone is off the bus.

## 1. `cpu_idle` is not the return site, and 546 section 1 says it is

546 section 1 states the frame's second word "names `cpu_idle`, and the `pop` is the return into it",
reading Apple's source (`osfmk/arm/cpu.c:155-157`: `platform_cache_idle_enter(); cpu_idle_wfi(...);
platform_cache_idle_exit();`). That is true of the **unwrapped** link and false of every image this
project has built since 517, because 517 added `--wrap=platform_cache_idle_exit`: the caller is
`__wrap_platform_cache_idle_exit`, and the exit's `pop` returns **into the wrapper**, which then runs its
own tail (`entry_slot_null_note`, the pcx note, `entry_note_pcx` at `0x8047c9c0`).

Measured on the frozen 574 arm, from the image's own disassembly:

```
8047c98c:	bl	800462d4 <platform_cache_idle_exit>     <- the wrapper's call
8047c990:	movw	r0, #248	; 0xf8                  <- the return site = slot+4's correct value
```

and 535's parked arm has the same two instructions at the same addresses. So the value a correct frame
holds at `slot+4` is **`0x8047c990`**, an address inside the wrapper's own extent
`0x8047c964..0x8047c9c4` - **and that address appears nowhere in this repository's prose**, because until
now nothing compared against it. A document premise with no check behind it, in the one word the arm was
built to read.

## 2. What the reader did instead, and why a range is not a value

The `b1` line asked only whether the word looked like kernel text:

```sh
if [[ $seam_b1 =~ ^0x80[0-9a-f]{6}$ ]] && (( seam_b1 < 0x80600000 )); then   # PASS
```

and 520's own log shows what that admits: `xnu_live_slot_pre_m8 = 0x80553520` is a **stale stack word**
and it passes that test, as would any other address the idle thread's stack happens to hold - which is
precisely the wrong-value case this arm exists to detect. A test that a plausible-but-wrong value passes
is a test that cannot answer the question it is printed beside.

`exit_caller_lr_addr()` (new, beside `exit_pop_lr_addr`) derives the one address from the ELF: the symbol
table gives `__wrap_platform_cache_idle_exit`'s start and size, the body is disassembled, the `bl` to the
real exit is found **by name tail** (`<[^<>]*platform_cache_idle_exit>` excluding `<__wrap_…>`, the rule
`exit_pop_lr_addr` had to learn the hard way in 535), and the answer is asserted to be **inside the
wrapper's own extent** - so a derivation that had found the wrong `bl` fails rather than returning a
number. Verified: `0x8047c990` on the live arm, `0x8047c990` on 535's parked arm, empty on an unreadable
file.

The line now has **four reachable states**, each measured (`582-b1-five-states.txt`):

| `b1` | printed | what it says |
| --- | --- | --- |
| `0x8047c990` | **PASS** | memory held *this* frame's word - 546's premise, with its value corrected |
| `0x80553520` | **READING** | kernel text but not this image's return site: the word the pop reads is the idle stack's own stale content. **The old range test printed PASS on this value** |
| `0x800462dc` | **READING** | same, on another real kernel address |
| `0x33f1c1b5` | **READING** | not kernel text at all - the window's store had not reached memory (520 died on this value) |
| (no decoder) | **UNREAD** | the return site could not be derived |

**And the UNREAD branch was unreachable in the first version of this edit**, which is the same defect one
level down: with no decoder `caller_lr` is empty, and a `b1` that looks like kernel text fell into the
"kernel text but not this image's return site" arm and printed a *comparison* whose comparison address was
empty. It was caught by rehearsing that branch (`OBJDUMP=/nonexistent-objdump`, `b1=0x8047c990`) rather
than by reasoning - the "could not derive" test now runs first, and the reading's own comment says so.

## 3. The prediction this puts on the coming run

`b0`/`b1` are the two words read at the seam, *after* the exit's `push {fp, lr}` and with `SCTLR.C` clear -
so they are **uncached reads of memory**, and the push's stores went to memory. Therefore:

> **On 574's run, `b1` is predicted to be `0x8047c990`** - whatever the arm's `b`-versus-`a` comparison
> comes out as, in *both* the `AS DESIGNED` and the `THE L1 FLUSH WRITES IT BACK` branches, because the
> flush changes what memory holds *after* `b` was read.

That makes the run's reading answerable in two independent ways instead of one: clause (5)'s pair says
whether Apple's L1 flush wrote a stale line back over the push, and this line says whether what the push
wrote was in memory at all. `b0` has no derived expectation (it is the caller's `fp`, whatever it is) - so
this is one predicted word, stated as one.

If `b1` comes back as anything else, the reading is `546 section 1's premise for this cell` being false in
a way that needs no interpretation: the push's own store did not reach the word the pop reads, which is
news about the window rather than about the arm.

## 4. Regression, and safety

- **520's real log: `0 FAIL`, `0 UNREAD`, exit 0** - unchanged (that image predates the seam keys, so the
  block is gated out by design, which is the property 564 documented).
- **All six reachable outcomes of section 4 and step 5 re-run after this edit and unchanged**, exit code
  for exit code: `fail 1`, `silent 1`, `fall 2`, `none 2`, `late3 3` (section 4), `late2 3` (step 5) -
  the same file's other paths, exercised with its final version rather than reasoned about.
- **The full success path on the 574-shaped synthetic log still exits 0**, and the new line fires inside
  the run: `READING b1=0x80017330 … (0x8047c990)`. That input is a rehearsal stub, **not** a capture, and
  the fact that the new check reads it as "not this image's return site" is the point - the old range test
  passed that value.
- **The gate is green** (`EXIT=0`, 0 UNREAD).
- No device action, no `fastboot`, no `adb`, nothing written to storage, no build input touched: one
  reader edit (a new derivation plus the line's branches), one derived measurement, four rehearsals in
  `/tmp`, two `--summarise` reads of logs already on disk. The frozen arm is untouched and unrun.

**The boot still waits only on the user's power press.** TWRP stays withheld:
「如果os已经能进去了的话」 is unmet.
