# 725: the rung-13 arm built and parked — the seam constant's third move, a merged `strd` the clause refused, a check that could only fail, and one cell of the rehearsal that was about the artifact's size

`STAGE90_XNU_STORAGE_PROBE=12`: the register state at the instant of the command plus the command's own
return path, built, parked as **`armed-storage-instant-e8dc64f7`**, recorded in
`stages/stage90/revert-set.txt`, and left in `out/` for the press. The pre-registration is
[724](experiment-724-the-rung-13-pre-registration-the-instant-of-the-command-and-the-return-path.md).

**No device action in this step.** No press; `fastboot boot` only, one press to follow, nothing flashed.

## 1. What the arm is, in the source

Three bodies in `stages/stage90/xnu_arm_boot/entry_storage.c`, all inside the rung-11 block's `#if`:

```c
#if STAGE90_XNU_STORAGE_PROBE >= 12
static __attribute__((noinline, noclone)) uint32_t st_cmd_census(void);   /* ten reads, NO store */
#define ST_CMD_PUBLISH_12(tag, r)  ...                                     /* the seven new cells */
#endif
```

and six edits inside `st_send_command` / `st_cmd_path`:

- a **read-back of `COMMAND 0x0E`** immediately after the store (`_cmdN_word_read`);
- **`PRESENT_STATE`'s `CMD_INHIBIT` read immediately after the store** (`_cmdN_inhibit_after`) and
  **sampled over the poll's first 1024 iterations** (`_cmdN_inhibit_seen`, `_cmdN_inhibit_last`);
- **the first non-zero `INT_STATUS` of any kind** (`_cmdN_status_any`, `_cmdN_any_polls`), beside
  rung 11's narrower `RESPONSE` poll;
- **an unconditional `RESPONSE 0x10` read** (`_cmdN_resp_read`), with `_cmdN_rsp_present` still carrying
  the driver's own condition — so a zero response is a reading and not the absence of a read;
- the poll's break condition widened to the driver's own second arm (`sdhci_cmd_irq`, `sdhci.c:2867-2876`);
- **`st_cmd_census()` first, and its one refusal** — `POWER_CONTROL`'s bus-power bit clear means the
  command is not issued.

`struct st_cmd_result` gains seven `uint32_t` fields **unconditionally**, so the struct has one
definition; it is a stack object in `st_cmd_path`, so `.data` and `.bss` do not see it — measured below.

The build is the 18-key switch set the pressed rung-12 arm carries, plus `STAGE90_XNU_STORAGE_PROBE=12`
and `STAGE90_ENTRY_ARM_CHANGE=1`. `build_entry.sh`'s ladder refusal now reads `… , 11 or 12`, and
`entry_storage.c`'s `#error` carries the rung-12 sentence.

## 2. The build refused the first invocation three times, and each refusal was correct

**(a) The seam constant moved a third time, and the clause caught it — and the prose around it had been
stale through the previous two.** The first invocation stopped at

```
FAIL: the exit's call to FlushPoU_Dcache is at 2147783384 and returns to 2147783388, while
      entry_trace.c's STAGE90_XNU_SEAM_LR is 0x800482dc
```

The rung's 5,440 bytes of entry-side code pushed the entry group past the next page boundary, so the
exit's `bl` is at **`0x800492d8`** returning to **`0x800492dc`** — the third move of the same kind
(`0x800462dc` → `0x800472dc` on 696, → `0x800482dc` on 708, → now). **The disassembly decided the
direction again**, and the same pass re-read every other address the seam prose pins: the window
`[0x80049240, 0x8004933c)`, the four callers `0x80048d08` / `0x80049284` / `0x800492d8` / `0x800493bc`,
`CleanPoU_Dcache 0x800487a8`, `CleanPoC_Dcache 0x8004875c`, `FlushPoC_Dcache 0x80048828`, the `pop` at
`0x8004933c`, and `bsdinit_task`'s call site `0x8004ceb4`. **Every one of those prose addresses was still
carrying its 696-era value through two moves the `#define` itself had already absorbed** — the paragraph
explaining the constant was the second place the address lives and the only one nothing checked. Both
*copies* of the constant moved together this time: `entry_trace.c`'s `STAGE90_XNU_SEAM_LR` and
`run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`, which the gate compares against each other and the live ELF.

**(b) The image-side clause was written for the wrong mnemonic set, and GCC was the reason.** The second
invocation stopped at

```
FAIL: st_send_command's non-device memory accesses are [UNK:ldr UNK:str UNK:strd] ...
```

The two new cells `status_any` and `status_any_polls` are adjacent 32-bit fields assigned in one basic
block, so GCC merged the two stores into one **`strdeq`** on the same base. The classifier reports it as
a third access of the same pointer argument, and the expected set is now rung-dependent: rung 11's
`[UNK:ldr UNK:str]`, rung 12's `[UNK:ldr UNK:str UNK:strd]`. **A merged store is a *narrower* reading of
the same two cells**, which is why the clause names it rather than tolerating a wildcard.

**(c) The census's own store check could only fail — and that is the one worth recording.** The third
invocation stopped with **no message at all**: exit 1 at line 30806. The line was

```bash
stb_cen_stores=$(printf '%s\n' $stb_cen_dev | grep -E ':str|:strh|:strb' | tr '\n' ' ')
```

and `grep` **exits 1 when it matches nothing** — which is the *good* case here, the census having no
store. Under this script's `set -euo pipefail` the assignment took that status and ended the build
**before** the emptiness assertion could pass. **A check that can only fail is not a check**: the
extractor's status must not be the assertion's status, and the extractor is `awk` now (which always
exits 0). This is m702's shape — an assertion the tool cannot produce a pass for on a correct artifact —
reached through the shell rather than through the classifier, and it failed *silent*, so the reading a
reader would have taken from `exit 1` with no output was "the build is broken" rather than "the check is".

## 3. What the clauses then asserted, and the containment it buys

```
xnu_entry_721: st_send_command's device accesses are [f9824908:str f982490e:ldrh f982490e:strh
f9824910:ldr f9824924:ldr f9824930:ldr f9824930:str ] ... its stores in order [f9824930:str
f9824908:str f982490e:strh ] and its image side the pointer-argument pair [UNK:ldr UNK:str UNK:strd];
st_cmd_path's are [f9824924:ldr f9824934:ldr f9824938:ldr ] with NO store and an EMPTY image side,
and it calls st_send_command 2 time(s)
xnu_entry_724: st_cmd_census's device accesses are [f9824924:ldr f9824929:ldrb f982492c:ldrh
f9824934:ldr f9824938:ldr fc4004c0:ldr fc4004c4:ldr fc4004c8:ldr fc4004d0:ldr fc4004d4:ldr ] ...
NO store, an EMPTY image side and one call from st_cmd_path
```

Read as the four floors 724's §4 named: **the write set is rung 11's three stores in rung 11's order and
nothing was added** (the only new access in the command body is `COMMAND 0x0E` read back, 16-bit);
**the census makes NO device store at all** — asserted first, because a body justified as instrumentation
is the easiest place for a store to hide, and the offsets one byte from the ones it reads are
`POWER_CONTROL 0x29` and the two interrupt-enable registers; **`st_cmd_path` still makes no store**; and
`INT_ENABLE`/`SIGNAL_ENABLE` are read in two bodies and written in neither.

Measured against the pressed rung-12 park, from the two ELFs:

| section | rung 12 (pressed) | rung 13 | |
|---|---|---|---|
| `__entry_text_size` | `0x00512e20` | `0x00514360` | **+5,440 B** |
| `.data` | `0x326b0` | `0x326b0` | **same size** |
| `.bss` | `0x5cb88` (379,784 B) at `0x80547a80` | `0x5cb88` at `0x8054ba80` | same size, **+0x4000 in address** |

**This rung adds no instrument state**, which is what the census's `EMPTY` image-side claim is about, and
the `.bss` address moving by a page while its size does not is the entry group's own growth, not a state
change. The entry bin, payload and qcdt are **larger** than the pressed arm's (5,552,764 / 6,048,260 /
8,572,928 against 5,536,380 / 6,031,876 / 8,556,544), unlike rung 12's — this rung's addition does not fit
in the `.text`-to-`__bss_start` fill, so the image and everything after it moved.

## 4. The park, the record, and the rehearsal cell that was about the artifact's size

The payload is `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh` (exit 0, one build at a time and
no firer was armed). **Parked** as `out/stage90/frozen/armed-storage-instant-e8dc64f7` — 11 files, plain
`cp -p`, the suffix being the **entry bin's** own sha256 prefix (`e8dc64f7…`). `tools/verify_revert_set.sh`
exits 0 against the park, and against the live tree with `--set=armed-storage-instant-e8dc64f7`; the
record's eleven lines carry `sha256` and `bytes` for each member with the manifest's member list pinned on
the `SHA256SUMS.txt` line.

**`tools/rehearse_revert_set.sh` went red on one cell, and the cell was measuring the arm's *size
distribution* rather than the property it names.** `live-tree-vs-the-other-set` runs the verifier on the
live tree with **every** set selected and asked the output for `stage90-qcdt.img hashes to` — which is the
verifier's **second** refusal form: a file whose recorded **size** disagrees is refused *before* it is
hashed, and `hashes to` is printed only when the sizes agree and the bytes do not. Every arm before this
one happened to share `stage90-qcdt.img`'s size with some other set in the record, so the hash form always
appeared and the cell stayed green **while it was really asserting that two arms had the same size**. This
arm's qcdt is 8,572,928 bytes and no recorded set shares it, so the size refusal fired for every set, the
needle became unproducible on a **correct** refusal, and the cell printed FAIL. The needle is now
`FAIL  stage90-qcdt.img `, which both refusal forms carry and no `ok` line does — the cell is about *the
file being refused* rather than about *which of the two checks refused it first*. Rehearsal back to
**38 ok, 0 failed**.

## 5. Readiness, and the flags the press will be given

`tools/verify_press_ready.sh --live out/stage90` → **5 of 5, exit 0**, after two rows refused and were
repaired in this step's own lane:

```
gate flags  --allow-xnu-entry
the run     ./preflight_boot_check.sh --allow-xnu-entry
            ./run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-instant-e8dc64f7
```

Both refusals were the rows working: row 4 first refused because `tools/verify_press_ready.sh` has **no
paragraph for rung 12** (a narration can be wrong and still non-empty, which is all that row used to ask),
and then refused again because the new paragraph quoted the rung and not the arm's **bound** — the second
key that names an arm, which is 715 §5's rule and the reason the paragraph writes
`STAGE90_XNU_PWR_WAIT_TICKS=384000` out of the record rather than a literal. The narration's own key names
were then read against the image's `xnu_live_` string pool.

## 6. What the arm is not

No data command, no block, no sector, no partition table, no filesystem, no mount, no DMA, no data-path
register, no interrupt enabled, **no new store anywhere**, no `core_mem` store, no GCC store, no
`POWER_CONTROL` write (it is *read*, at the command's own moment), no write to any RCG word, and no byte
of the medium. **TWRP-to-storage stays withheld**: the goal's precondition (「如果os已经能进去了的话」) is
unmet, and nothing in this rung can meet it — the rung's whole value is that the next one will know which
of five things it is about.
