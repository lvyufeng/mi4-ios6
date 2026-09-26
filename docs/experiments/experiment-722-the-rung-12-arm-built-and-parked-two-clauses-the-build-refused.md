# 722: the rung-12 arm built and parked — two clauses the build itself refused, one backtick in a refusal message, and a growth in `.text` that the fill absorbs whole

`STAGE90_XNU_STORAGE_PROBE=11`: the driver's own first command path — `sdhci_send_command` ported for
CMD0 and CMD1 — built, parked as **`armed-storage-cmd-892b8b68`**, recorded in
`stages/stage90/revert-set.txt`, and left in `out/` for the press. The pre-registration is
[experiment-721](experiment-721-the-rung-12-pre-registration-the-drivers-first-command-and-the-cards-first-answer.md);
this document is the build, and the press is [723](experiment-723-the-rung-12-press-the-command-went-out-and-nothing-latched.md).

**No device action in this step.** `fastboot boot` only, one press to follow, nothing flashed.

## 1. What the arm is, in the source

One `#if` block in `stages/stage90/xnu_arm_boot/entry_storage.c` (rungs 2 through 10 are its neighbours),
one call site in the probe, and one `case` in the ladder:

```c
#if STAGE90_XNU_STORAGE_PROBE >= 11
    ...
    static __attribute__((noinline, noclone)) void
    st_send_command(uint32_t opcode, uint32_t arg, uint32_t mmc_flags, struct st_cmd_result *r);
    static __attribute__((noinline, noclone)) void st_cmd_path(void);
#endif
```

```c
#if STAGE90_XNU_STORAGE_PROBE >= 11
    if (g_storage_mode_complete != 0u)
        st_cmd_path();
#endif
```

`st_send_command` holds every device write this rung makes and `st_cmd_path` holds the two calls, the
three gates and every key; the split is what lets `build_entry.sh` assert, offset by offset and width by
width, that **all three stores are in one body** and that the other body makes none. It is placed before
rung 8's tail count, so the probe's own `_mode_*`, `_clk_*`, `_rst_*` and `_pwr_*` records precede it and
the ending (690's clock) does not move.

The build is the 18-key switch set the pressed rung-10 arm carries, plus `STAGE90_XNU_STORAGE_PROBE=11`
and `STAGE90_ENTRY_ARM_CHANGE=1` (the arm-change guard). `build_entry.sh`'s ladder refusal now reads
`must be 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 or 11`, and `entry_storage.c`'s `#error` text carries the rung-11
sentence.

## 2. The build refused the first invocation twice, and both refusals were correct

**(a) `noinline` alone is not enough, and the symbol table said so.** The first build stopped at

```
FAIL: st_send_command is not in the linked image while STAGE90_XNU_STORAGE_PROBE=11
```

and `nm` on the intermediate ELF showed the reason: **`st_send_command.constprop.0` and no
`st_send_command` at all.** This function is called twice with constant arguments, so GCC's
interprocedural constant propagation specialised the whole body into a clone and dropped the original.
The clause was right to refuse: a clone is a *second* body with its own extent, and a clause reading the
original's window while the image's only command path lived in the clone would have described a function
no call reaches. The source now carries `noinline` **and** `noclone`, and the refusal message names the
third cause (renamed / inlined / **cloned**) rather than the two it had.

**(b) The image-side clause was written as an emptiness, and the emptiness is unachievable.** The second
build stopped at

```
FAIL: st_send_command's non-device memory accesses are [UNK:ldr UNK:str] ...
```

`classify_body` reports an access whose base register the body never materialized an address for as
`UNK:<mnemonic>`, and this function's result struct is reached through its **pointer argument** — which is
that kind of base and is the whole of its non-`sp` memory. The claim is now the named pair
`[[ "$stb_cmd_img" == "UNK:ldr UNK:str" ]]` with an empty address line, which still refuses the class the
clause is about (a symbol this rung never declared must be reached through a materialized base, and that
prints `IMG:` **with** an address). An assertion that cannot hold is not a check; the emptiness it wanted
would have been refused on every correct build from now on, which is the way a check dies quietly.

**(c) One backtick in a refusal message, and the shell ran it.** The first invocation also printed

```
./stages/stage90/xnu_arm_boot/build_entry.sh: line 30698: noinline: command not found
```

The refusing `layout_fail "..."` is a double-quoted string, and the sentence inside it spelled
`` `noinline` `` — so the shell command-substituted the word before complaining, and the message that
reached the operator had a hole where the explanation should have been (`and the second is what  exists to
prevent`). This is the same hazard the peer lane's gate carries as a named invariant, and it is worth
recording here rather than in the gate's file: **a refusal message is code too**, and the message that
explains a refusal is exactly the one nobody reads the shell expansion of. Escaped, then re-checked with
`bash -n`.

## 3. What the clauses then asserted, and it is the safety contract

```
xnu_entry_721: st_send_command's device accesses are [f9824908:str f982490e:strh f9824910:ldr
f9824924:ldr f9824930:ldr f9824930:str ] with counts [f9824930:ldr=3 f9824910:ldr=1 f9824924:ldr=5
f9824908:str=1 f982490e:strh=1 f9824930:str=1], its stores in order [f9824930:str f9824908:str
f982490e:strh ] and its image side the pointer-argument pair [UNK:ldr UNK:str]; st_cmd_path's are
[f9824924:ldr f9824934:ldr f9824938:ldr ] with NO store and an EMPTY image side, and it calls
st_send_command 2 time(s) - the driver's own first command: CMD0 (word 0x0000, no response read) then
CMD1 (word 0x0102, MMC_RSP_R3) with the completion polled out of the controller's own INT_STATUS and
the response read out of RESPONSE 0x10, and no data-path register, no POWER_CONTROL, no GCC word, no
core_mem word and no byte of the medium
```

Read as the four floors §4 of the pre-registration named: **the write set is three stores per call and
nothing else** (`INT_STATUS 0x30` W1C, `ARGUMENT 0x08` 32-bit, `COMMAND 0x0E` 16-bit, in that order);
**`POWER_CONTROL 0x29` is in no access at all** (the probe's own `hc_mem` window is unchanged at
`47 44 44 41`); **`INT_ENABLE 0x34` and `SIGNAL_ENABLE 0x38` are read and written nowhere** — which is
the act that could let the block raise SPI 123 → intid 155, the line this image hands to nobody; and
**no data-path register** (`TRANSFER_MODE 0x0C` is absent because `sdhci_set_transfer_mode` returns on
its first line for a data-less command, `sdhci.c:985-986`).

`.text` grows by **+448 bytes** (`__entry_text_size` 0x00512760 → 0x00512e20) and **`.data` (206,804
bytes) and `.bss` (379,784 bytes, 0x80547a80..0x805a4608) are byte-for-byte the same addresses and the
same sizes as the pressed arm's** — the rung adds no instrument state, which is what the two `EMPTY`
image-side claims are about. The entry bin, `stage90.bin` and `stage90-qcdt.img` are the *same lengths* as
the pressed arm's (5,536,380 / 6,031,876 / 8,556,544), because the growth is absorbed by the fill between
`.text`'s end and `__bss_start` (`mi4-linker-fill-term`): the sizes agree and the hashes do not.

## 4. The park, the record, and the two defects the rehearsal found in my own lane

The payload is `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh` (exit 0); without it
`STAGE90_XNU_ENTRY` is `0u`, the `bl` into XNU is compiled out and the arm cannot produce the log it
exists for.

**Parked** as `out/stage90/frozen/armed-storage-cmd-892b8b68` — 11 files, plain `cp -p`, the suffix being
the **entry bin's** own sha256 prefix (that is the convention every `armed-storage-*` park uses, measured:
`13366e08`, `1fc30bfe`, `00b28262` are all their entry bins' first eight hex). `tools/verify_revert_set.sh`
exits 0 against the park and against the live tree; the record's eleven lines carry `sha256` and `bytes`
for each member, with the manifest's member list pinned on the `SHA256SUMS.txt` line so criterion B is
closed over the record rather than read out of the live tree.

**`tools/rehearse_revert_set.sh` was red on two cells before this arm and is 38/0 after it. Both were my
lane's, and both are the same shape: a check that had stopped being about the tree.**

1. **A name in a record is true forever, so the cell failed on every arm after the one it named.** The
   `live-tree-matches-armed-set` cell asserted `--set=armed-sleepless-696a0f39` against the live tree, with
   a guard above it that only checked *that name was in the record*. It always was. So every park since
   696 left the cell printing FAIL while the bytes it described were correct, and a rehearsal with one
   permanent FAIL is a rehearsal whose real refusals are unreadable. The name is now the live tree's own
   answer, from `tools/resolve_arm_set.sh` — the one place that answers *which arm are these bytes*, which
   this file's own header says was created because that question had three answers (667/668). This was the
   fourth.
2. **`$OUT/captures/$_f` is a path *expression*, not a member.** The derivation of "the files the gate
   reads" grepped `\$OUT/[A-Za-z0-9_.-]+` and so matched the prefix `captures` inside the gate's
   `[[ -n $_f && -r $OUT/captures/$_f ]]` — a directory of archived runs, joined with an argument, which no
   revert set can hold. The `record-covers-gate` cell therefore printed FAIL about a record that covers
   everything the gate reads. A match followed by `/` or `$` is now dropped: it is a fragment of a longer
   expression, and reading it as a member is a claim about the extractor's scope rather than about the
   record.

## 5. Readiness, and the flags the press was given

`tools/verify_press_ready.sh` → **5 of 5, exit 0**, with the arm resolved from the bytes of
`stage90-qcdt.img`:

```
gate flags  --allow-xnu-entry
the run     ./preflight_boot_check.sh --allow-xnu-entry
            ./run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-cmd-892b8b68
```

The gate accepts the tree under those flags; the narration's own key names were read against the image's
`xnu_live_` string pool (46 names, 1 glob, every one a suffix of a string this image publishes).

## 6. What the arm is not

No data command, no block, no sector, no partition table, no filesystem, no mount, no DMA, no data-path
register, no interrupt enabled, no clock write, no `core_mem` write, no `POWER_CONTROL` write, no store
outside `hc_mem`'s command registers, and no byte of the card's storage. **TWRP-to-storage stays
withheld**: the goal's precondition (「如果os已经能进去了的话」) is unmet, and a card that answers CMD1
would not meet it either.
