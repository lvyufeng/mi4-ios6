# 785: the last masked window — rung 23's widening, applied at the two windows that carry the first three commands

**HOST-SIDE ONLY SO FAR. ONE ARM BUILT AND PARKED, NO DEVICE ACTION, NO PRESS, NO RUNNER, NO FIRER.**
`out/` WAS touched — that is what building an arm does — and the arm it now holds is
`armed-storage-5fca6210` (`STAGE90_XNU_STORAGE_PROBE=28`), **ARMED AND NOT PRESSED**. The arm it
replaced, `armed-storage-d5d98738` (rung 28), was already parked and verified and is **not lost**: it
is `out/stage90/frozen/armed-storage-d5d98738/`, eleven members, and the new arm strictly CONTAINS it,
measured twice (§5). Every other park and every capture are byte-identical before and after. One press
spent by this step: **none**.

## 1. The reading this step acts on, and why it needed an act rather than a sentence

783 read the **scope** of 766's repair and found it had reached one of three command windows. 765 §1
had established the rule — this controller's `INT_STATUS` is a **gated view**, so a status bit is
visible only while its enable stands — and 766 had acted on it in `st_cmd3_noidx`, the **last** command
this ladder sends. The two windows that carry the **first three** commands were left writing the lone
`ST_SDHCI_INT_RESPONSE` (`0x00000001`).

The consequence is not cosmetic. On the rung-28 arm, two of the ladder's five commands report
`_cmd1_status_any = 0` and `_cid_status_any = 0` over their full 1.2 s windows — and **those zeros are
readings about the MASK, not about the block.** `CMD_TIMEOUT` is a *command-level* bit, so its absence
from a count taken on a one-bit enable is exactly what the mask produces. CMD1 is `SEND_OP_COND`: the
one command an eMMC must answer for this ladder to move at all.

## 2. The act: one constant, at two windows, and nothing else

| window | function | build | store | before | after |
| --- | --- | --- | --- | --- | --- |
| CMD0, CMD1 | `st_cmd_path` | `src/entry/entry_storage.c:4351` | `:4353` | `0x00000001` | `0x000F0001` |
| CMD2 | `st_all_send_cid` | `:3120` | `:3122` | `0x00000001` | `0x000F0001` |
| CMD3 | `st_cmd3_noidx` | `:3993` (rung 23) | `:3998` | `0x000F0001` | **unchanged** |

`ST_SDHCI_INT_ENABLE_CMD = 0x000F0001` is `SDHCI_INT_RESPONSE` (0) with `SDHCI_INT_TIMEOUT` (15),
`SDHCI_INT_CRC` (14), `SDHCI_INT_END_BIT` (13) and `SDHCI_INT_INDEX` (12) — the five-bit word rung 23
already put in the third window, **not a new constant**.

**No new store, no new register, no new width, no new window, no new megabyte.** Both windows have
stored to `INT_ENABLE 0x34` since rungs 14 and 16; the store COUNT is unchanged, the windows are the
same `#if >= 14` / `#if >= 16` blocks with the same restore calls, and `SIGNAL_ENABLE 0x38` is still
read and never written.

**And no new key.** The enable word each window stores was already published as
`xnu_live_storage_ena_wrote` and `xnu_live_storage_cid_ena_wrote`; **both strings are present in the
rung-28 park's own entry ELF** (measured, one each). What changes is the **value** one existing key
carries — which is why the arm's own constant is read out of the log rather than argued from this
source. *This sentence is a correction: the draft of the arm's own narration called
`_cid_ena_wrote` "a NEW KEY for this rung", which is false, and the measurement above is what
retired it — deliberately the same class this project keeps catching, a claim in prose that one
command over an artifact refutes.*

## 3. What the two windows' code and data actually moved

Measured against the rung-28 park's own `xnu_arm_entry.elf` (`arm-none-eabi-nm -S`):

| symbol | rung-28 park | this arm | note |
| --- | --- | --- | --- |
| `st_all_send_cid` | `0x8000ddc4` +`0x370` | `0x8000ddc4` +`0x374` | +4 bytes: the five-bit constant costs one more instruction than `orr` with a small immediate |
| `st_cmd_path` | `0x8000e4a8` +`0x7a8` | `0x8000e4ac` +`0x7a0` | −8 bytes; the pool/rescheduling of a function whose two branches now differ |
| `st_cmd3_noidx` | `0x8000e134` +`0x374` | `0x8000e138` +`0x374` | size unchanged, moved by 4 — **770's lesson: an unchanged size is not the claim that nothing moved** |
| `entry_storage_probe` | `0x8000ee84` +`0x177c` | `0x8000ee80` +`0x177c` | size unchanged, moved by 4 |
| `entry_epilogue` | `0x80004948` +`0xf3c` | `0x80004948` +`0xf3c` | **identical address and size** |
| `entry_seam_flush` | `0x80480b50` | `0x80480b50` | **identical** — so the seam clause's `STAGE90_XNU_SEAM_LR` is the same address, and no page was crossed |

And `xnu_arm_entry.bin` is **5,552,764 bytes in both arms**, with the gate's seam clause agreeing.

## 4. The answer space, per window, and it is four rows

With the five bits enabled, `_status_any` becomes a count of polls in which **any** status bit stood.
Read against that window's own `_any_polls`:

| reading | what it says |
| --- | --- |
| `_status_any` still 0, `_any_polls` large | the block set **NO bit of `INT_STATUS` at all** during that command — not the completion and not `TIMEOUT`. This is rung 22's silence (764 §4) reproduced at a window that **could** have shown a timeout, and it is the row that would make the ladder's most-quoted negative a reading about the block at last |
| bit 15 (`TIMEOUT`, `0x00008000`) alone | the command was issued, held the line, and the block's own response timeout fired — the sticky bit rung 23's `_nidx_status_pre` (`0x00018000`) already carries for CMD1, now read at the window that **issued** it rather than through a later latch |
| CRC (14) / END_BIT (13) / INDEX (12) | the card answered and the answer was malformed |
| bit 0 alone | the completion bit is visible and the command finished |

**A negative is read only where the line moved** (771's rule, one window over): `_cmd1_cmdlow_seen` and
the `_inhibit_seen` cells are read **beside** the status counts, so "nothing was ever driven onto the
bus" and "the block drove the line and no bit of the gated view rose" stay distinguishable — and a zero
count is a reading only if the sampler outran the bus, which is why `_cmd1_polls` / `_cmd1_ticks` are
read beside it.

## 5. It contains rung 28, measured twice on the two artifacts that matter

The result depends on **how** the two branches are written: as whole `#if`/`#else` pairs, not as a
conditional expression, so a value below 28 compiles to rung 14's and rung 16's exact lines. Measured
from this same source:

| value-27 artifact | built | equals |
| --- | --- | --- |
| `stage90-qcdt.img` | `93026ca1…`, 8,572,928 B | the parked `armed-storage-d5d98738`'s own line |
| `xnu_arm_entry.bin` | `d5d98738…`, 5,552,764 B | that park's own member |

So the edit is **surgical**: nothing outside the `>= 28` guards moved. The consequence is that the
value-28 arm is rung 28 **plus one constant per window** and nothing else — **one press answers rung
28's `_pad_raw` four-row fork AND this arm's two windows, and rung 28 need never be pressed on its
own.**

## 6. A cost paid in the same build: the census tool counted one name as three producers

`tools/report_int_enable_windows.py` keyed its assignment map by the local **name** of the word it
stores. Three functions now each declare a local `ena`, so the map collapsed them into **one** producer
list and printed `st_cmd_path`'s store as backing three words — a name with two producers, m739's
shape. The map is now keyed by `(func, name)`.

The repaired census reads, over the whole file and every value:

```
the FIVE-BIT word (`0x000f0001`) is built at 3 line(s):
    st_all_send_cid:3120 (>= 28), st_cmd3_noidx:3993 (>= 22), st_cmd_path:4351 (>= 28)
the ONE-BIT word (`0x00000001`) is built at 2 line(s):
    st_quiet_enable_probe:3016 (>= None), st_cmd3_noidx:3995 (>= 22)
and the FIVE-BIT word reaches 3 store(s), all in:
    st_all_send_cid, st_cmd3_noidx, st_cmd_path
```

At **value 28** the only one-bit build left is `st_quiet_enable_probe:3016`, which holds the
read-modify-write that closes its own window and **carries no command**; `st_cmd3_noidx:3995` is the
`#else` arm of the rung-23 widen and is not built; `st_set_relative_addr` and `st_cmd3_noresp` are
compiled only at values 19 and 20 and are absent from this arm entirely. **So 783's scope defect is
closed rather than re-described** — and 783 named a *window-scope check* as owed; what this step lands
is the census plus this paragraph, **not a build-time clause**, and that distinction is recorded.

## 7. Landed, and the park

| artifact | state |
| --- | --- |
| `src/entry/entry_storage.c` | the two `#if >= 28`/`#else` pairs; the guard's bound raised to `> 28`; the `#error` text extended |
| `records/revert-set.txt` | a `# 785:` block, eleven `set=` lines for `armed-storage-5fca6210` |
| `out/stage90/frozen/armed-storage-5fca6210/` | **eleven members**, copied with `cp -p` and compared to live `out/` file by file with `cmp` (all agree) |
| `tools/verify_press_ready.sh` | a `rung_para 28` paragraph and an `elif [[ $wst == 28 ]]` arm sentence, so the new rung is narrated by **its own value** and the row reads the arm by a reading rather than by memory |
| `tools/report_int_enable_windows.py` | the `(func, name)` keying of §6 |

The member hashes, hashed **in place in the park** by `tools/verify_revert_set.sh
out/stage90/frozen/armed-storage-5fca6210 --set=armed-storage-5fca6210`: **11 ok / 0 failed**, plus
"every one of the 5 member(s) its own manifest names is in the set". `tools/verify_press_ready.sh`
then reads **5 of 5, exit 0** — live arm is the recorded arm, the park verifies, the gate accepts the
tree under `--allow-xnu-entry`, the arm is named by a reading, and the press catcher is usable. The
press path it prints is:

```
./preflight_boot_check.sh --allow-xnu-entry
./run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-5fca6210
```

The payload's own switch set did not move: `stage90-build-config.txt` is **byte-identical** to the
rung-28 park's member (`6c2b6038…`, 682 B, `STAGE90_XNU_ENTRY=1`). The entry's config differs in
exactly two lines (`STAGE90_XNU_STORAGE_PROBE` 27→28 and the artifact's own
`STAGE90_XNU_ENTRY_SHA256`), and the entry's *sources* record differs in exactly two lines — that
same sha256, and **one** source file's content hash, `src/entry/entry_storage.c`.

## 8. What this does not do

- **It does not make the card answer.** It makes one window's silence *readable*. If `_status_any` is
  still 0 beside a large `_any_polls`, the press has bought a stronger negative and not a driver.
- **It does not touch the power candidate 784 gave a shape to**, and no store in it can — the rails
  are RPM resources with no `reg` at all.
- **It does not add a key, a cell or a count.** Its whole delta is one constant's value at two sites.
- **It does not close 783's owed build-time window-scope clause**; §6 is the reading, and the clause is
  still owed and still named.
- **No press is spent by this step.** The arm is armed and the press is the operator's.

## 9. Owed, and named rather than left to be inferred

- `src/entry/entry_storage.c:302-303`'s mis-citation and the four `5,088,000`s — **COST-owed, carried
  again by this build and still not paid**.
- The set-comparison pad repair (779 §7, pre-registered, not built); the `rung_para` correction for
  values 12..23; the seam-address class; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; the 737 window
  paragraph (paid by the rung-28 build); `fdt_nodes`'s lack of a synthetic FDT cell (782 §6); 784's
  `rail_name` cell; and **783's window-scope clause as a clause**.
- **The `#if`-scope claim is checked by hand here.** "Every new line sits inside `>= 28`" is read off
  the diff; the containment measurement of §5 is what makes it safe, but there is no clause that
  refuses a future edit which puts a line outside the guard. That is the same class as 783's owed
  check and it is named rather than implied.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, and **times out because the card does not answer** —
and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
