# 846 — the rung-49 press: **A PROTECTIVE MBR — AND A DECODE DEFECT IN THE ARM ITSELF**

**A PRESS**, made under the standing instruction. One gate exit 0, one runner exit 0; `fastboot boot`
only, nothing flashed, nothing written to storage; `33e80afe` absent from **both** device lists hand-checked
immediately before the gate; **the phone returned** (`/tmp/cancro-last_kmsg.txt` 662,245 B, runner exit 0).
Arm **`armed-storage-ee0ab2e6`** (`STAGE90_XNU_STORAGE_PROBE=48`, rung 49) is now **SPENT**. Capture
`out/stage90/captures/rung49-mbrpt-20260930-103845-last_kmsg.txt` 662,245 B `0656f5ea…`.

---

## 1. The partition walk ran, and it reads like a table

`_pm_called = 1`, `_pm_sig = 0xAA55`, `_pm_sig_ok = 1`, `_pm_entries = 4`, `_pm_done = 1` — the parse ran
on the sector rung 48 left in memory and the signature re-check passed, so the four records **are** a
table. Entry 0 was non-empty (`_pm_nonzero = 1`), `_pm_index = 0`, `_pm_nz_active = 0`. The first
non-zero **type byte** is `_pm_nonzero_type = 0x000000EE`.

`0xEE` at offset 4 of partition record 0 is the **EFI protective-partition type code** — the single entry a
GPT disk keeps in its MBR to reserve the space the GPT itself occupies. So the medium is not merely "MBR
formatted": it is **a GPT disk wearing a protective MBR**, which is exactly what rung 47's *"LBA 1 is not a
GPT header"* did **not** prepare a reader for, and what rung 48's zeroed boot code was consistent with.
**This type byte is correctly measured** (see §3 for why type is the one field the arm got right).

## 2. The arm's own numbers are wrong, and the class is this project's most-repeated one

The two 32-bit cells the arm published are **not the fields they name**:

| cell (as published) | value | what it actually is |
| --- | --- | --- |
| `_pm_nonzero_first` = `_pm_p0_first` | `0x0001FFFF` | **not** the first LBA |
| `_pm_nonzero_len` = `_pm_p0_len` | `0xFFFF0000` | **not** the length |

The two MBR 32-bit fields **straddle words**, and my module read single words. The record: each 16-byte
entry at byte `446 + 16k`, so the entry's `first-LBA` field is at record offset 8 = **byte `454 + 16k`** and
its `length` field at offset 12 = **byte `458 + 16k`**. `454 = 4·113 + 2` and `458 = 4·114 + 2`: **neither
is word-aligned**, so each field is the **high half of one word joined to the low half of the next**:

```
first_LBA = ((w[113+4k] >> 16) & 0xFFFF) | ((w[114+4k] & 0xFFFF) << 16)
length    = ((w[114+4k] >> 16) & 0xFFFF) | ((w[115+4k] & 0xFFFF) << 16)
```

My module published `first = w[113+4k]` and `length = w[114+4k]` — **one whole word each, starting one
word too early.** That is why `_pm_p0_first` came back as `0x0001FFFF`: the low 16 bits are the **CHS-last**
garbage (`0xFFFF`) and the high 16 are the first LBA's *low* half (`0x0001`). A reader who took
`0x0001FFFF` for the first LBA would place partition 0 at sector **131071** instead of **1**.

**THE ERROR WAS IN THE COMMENT, AND THE COMMENT IS THE PROJECT'S OWN WARNING.** The arm doc (845 §2) and the
module header both asserted the two 32-bit fields *"ARE word-aligned … the standard's own gift"* — a claim
in a comment, bound to nothing (**[[mi4-a-claim-in-a-comment-is-not-a-check]]**). I confused *"the field's
first byte is at a 4-aligned absolute offset"* (`452` — which is itself wrong: it is `454`) with *"the field
fits inside one word."* `452` is `4·113` and *looks* aligned; `454` is the field, and it straddles. The
type byte came out right only because **byte 450 happens to be at `(w[112] >> 16)`** — I read it at its
correct absolute byte by coincidence of arithmetic, while reading `first`/`length` one word early. **One
value, two definitions: the word `w[113]` is one thing to a reader who indexes words and another to the
field the MBR standard defines.**

## 3. What the sector actually holds once the straddle is applied

Re-derived from the arm's **own two raw words** (the arms read the sector correctly; only the labels are
wrong), entry 0 is:

| field | byte | value | meaning |
| --- | --- | --- | --- |
| status | 446 | not `0x80` (`_pm_p0_active = 0`) | not marked bootable |
| type | 450 | **`0xEE`** | **EFI protective partition** |
| first LBA | 454 | **`1`** = `(w113>>16) \| ((w114&0xFFFF)<<16)` | **where a GPT header lives** |
| length | 458 | `0xFFFFFFFF` (sectors) | the protective "cover the rest of the disk" size |

So the medium's LBA 0 is **an EFI protective MBR whose single entry points at LBA 1** — and rung 47 already
read LBA 1 and found it is **not** `EFI PART`. **That tension is the frontier**: a protective MBR that
points at a GPT header, over a sector that is not a GPT header, is either a stale protective MBR or a GPT
that was overwritten. **Only a re-read of LBA 1 (or LBA 2, the first usable sector) settles it** — the
protective entry's `first = 1` is the address to go to.

## 4. What this press did and did not establish

- **ESTABLISHED**: the medium is MBR-partitioned (rung 48) with a **non-empty 4-entry table** whose first
  entry carries the **`0xEE` protective type** at LBA 0 (rung 49). The partition walk, its signature
  re-check, and the "no device access" property all held.
- **NOT ESTABLISHED — because the arm got it wrong**: the entry's first LBA and length *as cells*. They are
  recoverable from the raw words (§3) but the arm did not publish them, and **no build clause caught it** —
  `xnu_entry_844` proved the body touches no device, the signature is re-checked, and the call site is
  ordered, but **nothing asserted the byte offsets of the fields it decodes**. That is the gap the next arm
  closes as a build refusal.
- **THE GOAL IS NOT MET** — no partition's own sector read, no filesystem, no mount; TWRP-to-storage stays
  withheld.

## 5. The standing seam reading, unchanged

`seam_lr = 0x8004c2dc` **did not move**; `abort_entries = 0`, `xnu_entry_failures = 0`, `slot_post_calls`
up to `0x8`. The goal's own floor is met as in 504/520/533 (pid 1's syscalls, the fixture's `0xfeedface`
read back), and `xnu_live_post_elapsed = 0x09199d73` = **7,951.8 ms** — the machine stayed up and idled.

**NO FIRER IS ARMED AND NO PRESS IS OWED.** The frontier is **a corrected partition decode plus the first
partition's own sector**, and the next arm is not yet built.