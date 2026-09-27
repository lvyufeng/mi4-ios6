# 788: the four-arm enable matrix — bit 15 stands in `INT_ENABLE` on every arm and still never appears in `INT_STATUS` on a narrow one

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was **not** touched and no entry source was edited, so the armed rung-30 arm
(`armed-storage-dfc4ae78`, `STAGE90_XNU_STORAGE_PROBE=29`) is byte-identical before and after this
step. One press spent: **none**. **No press is authorized.**

Every number below is read out of a capture already on disk under `out/stage90/captures/`. Nothing is
inferred from the source, and no cell was computed from a cell: each is the key as the log prints it.

## 1. The question, and why it is the frontier's second half

787 §4 named two readings of what a widened window can do and could not choose between them without a
press:

- **(a)** bit 15 (`SDHCI_INT_ERROR`) rises with any error whatever its own bit's enable — then no
  error occurred at CMD1 and only bit 0 (the completion) can be new, and 786 §4's sentence stands;
- **(b)** bit 15 rises only when the specific error bit is enabled — then **CMD1 may have been timing
  out invisibly on every arm from rung 14 on**, and 786 §4's sentence is too strong.

786 §4 inferred (a) from two rows. Both of those rows — and this is the whole difficulty — were taken
on arms whose enable **carried** the error bit (`_nidx_status_any = 0x00018000` and `_cid_status_any =
0x00028000`), so they cannot distinguish *bit 15 tracks the errors* from *bit 15 appears when the error
appears because both were enabled*. The archive needs a case where bit 15 is enabled and no error bit
is. **It has six of them, and this step reads them.**

## 2. The control: the same window, the same command, four arms, and one word changed

### The CMD2 window (`st_all_send_cid`), five arms

| capture | ordinal / value | enable written | `INT_ENABLE` readback | polls | `_cid_status_any` | `_cid_err` | `_cid_timeout` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `rung17-cid` | 17 / 16 | `0x00000001` | `0x00008001` | 5,089,280 | **`0x00000000`** | 0 | **1** |
| `rung19-rca` | 19 / 18 | `0x00000001` | `0x00008001` | 5,088,256 | **`0x00000000`** | 0 | **1** |
| `rung21-noidx` | 21 / 20 | `0x00000001` | `0x00008001` | 5,088,256 | **`0x00000000`** | 0 | **1** |
| `rung24-cmdline` | 24 / 23 | `0x00000001` | `0x00008001` | 5,091,328 | **`0x00000000`** | 0 | **1** |
| `rung29-2win` | 29 / 28 | `0x000f0001` | `0x000f8001` | **1** | **`0x00028000`** | `0x00020000` | **0** |

### The CMD3 window (`st_cmd3_noidx`), four arms

| capture | ordinal / value | enable written | `INT_ENABLE` readback | polls | `_nidx_status_any` | `_nidx_err` | `_nidx_timeout` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `rung21-noidx` | 21 / 20 | `0x00000001` | `0x00008001` | 5,087,232 | **`0x00000000`** | 0 | **1** |
| `rung22-dllcensus` | 22 / 21 | `0x00000001` | `0x00008001` | 5,093,376 | **`0x00000000`** | 0 | **1** |
| `rung24-cmdline` | 24 / 23 | `0x000f0001` | `0x000f8001` | 1,789 | `0x00018000` | `0x00010000` | 0 |
| `rung29-2win` | 29 / 28 | `0x000f0001` | `0x000f8001` | 1,493 | `0x00018000` | `0x00010000` | 0 |

**`0x00008001` is bit 15 (`SDHCI_INT_ERROR`) standing beside the one-bit command enable, read out of
the block, on all six narrow rows.** `_status_any` is `st_send_command`'s own field: the **first
non-zero `INT_STATUS 0x30` of any kind**, read raw with no mask
(`src/entry/entry_storage.c:2635-2637`). So `_status_any = 0x00000000` beside a poll count of ~5.09
million means **every one of those reads returned zero**.

## 3. The measurement

Six narrow arm-windows, each with bit 15 enabled for its whole duration, each reading `INT_STATUS`
zero for its whole duration:

```
CMD2: 5,089,280 + 5,088,256 + 5,088,256 + 5,091,328   = 20,357,120 zero reads
CMD3: 5,087,232 + 5,093,376                           = 10,180,608 zero reads
                                                       -----------
                                                       30,537,728 zero reads
```

**About 30.5 million reads of `INT_STATUS` on this block, with `SDHCI_INT_ERROR` standing in
`INT_ENABLE` the whole time, and bit 15 appeared in none of them.** On the two arms where the enable
also carried the specific error bits, the same window produced `ERR | CRC` and `ERR | TIMEOUT` — and
produced them at the **first** poll.

**So 786 §4's sentence is not what the archive shows.** *`SDHCI_INT_ERROR` is enabled on every arm and
tracks the error bits, so a zero `_status_any` on a narrow arm was never blind to the existence of an
error* is an inference from two rows that cannot distinguish tracking from co-latching; the six rows
above are the case it did not have, and they go the other way. **The measured statement is: a standing
bit 15 in `INT_ENABLE` is not sufficient for bit 15 to appear in `INT_STATUS` on this block.**

The companion statement follows and is the one that matters: **786 §4's *the widening buys identity,
not existence* must be withdrawn as stated.** What the archive supports is that at these two windows
the widening is what makes an error appear at all.

## 4. The one thing this does not separate, named rather than left

The measurement above is about **bit 15**, and about it alone. It does not decide whether an error
*condition* existed during the narrow windows:

- If the enable gates only the **visibility or latching** of a condition the block had already
  reached, then the narrow windows were blind to real errors and (b) holds outright.
- If the enable is what makes the block **run the check** at all (CRC checked only with CRC enabled),
  then no error existed on the narrow arms and the six zeros are expected under both readings.

**The archive's evidence leans to the first, and one cell is why**: on the widened arms the error bit
is present at the *first* poll — `_cid_any_polls = 0x00000001` — and at CMD3 `_nidx_status_pre` is
already `ERR | TIMEOUT` **at the instant the enable store lands**. A condition that is reported within
~240 ns of the enable being written is not a condition the enable's own check produced by running; it
is one that was already pending. That is suggestive and **it is an argument, not a measurement** —
`_cid_stale` (§5) is read microseconds earlier and does *not* yet carry the CRC. **This is the
residual, and it is the one piece a press could settle cheaply at rung 30.**

## 5. A second finding, and it refutes a prediction of the same shape

`_cid_stale` is the one key that makes this matrix directly checkable at CMD2's **entrance** — it is
`INT_STATUS 0x30` as `st_send_command` finds it for CMD2, read *after* `st_all_send_cid`'s own store
to `INT_ENABLE` (`src/entry/entry_storage.c:3189`, the store; the read is at `:2560` inside the callee).

**777 corrected a false sentence with a prediction: "by 736's measurement that store raises bit 15 by
itself, so `_cid_stale >= 0x00008000` on EVERY run and the ZERO row does not exist"** — directing a
reader to take `== 0x00008000` as *nothing but this arm's own store* and anything above it as
*something this arm did not put there*.

**The only measurement that exists reads `_cid_stale = 0x00000001`** (rung 29; it is the only arm
carrying the key, because rungs 25–28 were strict containments and were never pressed). Bit 0 is set
and **bit 15 is not**. So 777's prediction is refuted by the first arm that could test it, and by the
same mechanism §3 measures: **what the CMD2 window's own enable store leaves in `INT_STATUS` is the
completion bit, not the error bit.** `_cid_clear_after = 0x00000000` beside it is consistent — the
clear was a write-back of `0x1`, so bit 0 was cleared and nothing else needed clearing.

## 6. What this changes about the next press, and nothing else

- **Rung 30's `_cmd1_status_any` is the first CMD1 reading in this ladder that is a reading about the
  block rather than about the mask.** On every arm from rung 14 on, CMD1 ran under
  `INT_ENABLE = 0x00008000` — bit 15 alone — and §3 says that configuration reports nothing.
  `_cmd1_status_any = 0`, `_cmd1_stale = 0` and `_cmd1_status_after = 0` on every arm carrying them
  are therefore **not** three independent zeros; they are one masked reading taken three times.
- **A zero at rung 30 is now the strongest negative the ladder can produce**, and a non-zero is a
  named error: five bits are carried by the window, so the six-arm control above says the enable is
  not the reason a bit would be missing.
- **`ERR | TIMEOUT` is a live row.** §3 removes the reason 786 §4 gave for striking it, and §4 names
  what would still make it uninformative.
- **Nothing about the arm changes.** It is the same `armed-storage-dfc4ae78`; this step moved no byte
  of `out/`, no source line, and no park member. The value of the press went up; its cost did not.

## 7. What this does not do

- **It spends no press and authorizes none.**
- **It does not decide §4's residual.** The two readings of what an enable *is* — a gate on
  visibility or a switch on the check — are both still admissible, and the cell that would separate
  them is not in the archive.
- **It does not explain the CRC's timing**, which 786 §7 already named as an anomaly: the CRC is
  reported at the first poll after the command word, and at rung 6's 400 kHz a 136-bit R2 response
  takes ~340 µs. That anomaly is untouched here and remains owed.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made,
  so **TWRP-to-storage stays withheld**.

## 8. Owed, and named rather than left to be inferred

- **786 §4's sentence** — *the widening buys identity, not existence* — is refuted by §3 and is
  superseded here; the superseding text is carried into `tools/verify_press_ready.sh`'s rung-29
  paragraph, which is the narration the next press prints.
- **777's `_cid_stale >= 0x00008000`** prediction is refuted by §5 and is carried to the same place.
- **The CMD2/CMD3 error bits' timing** (§7, 786 §7): the CRC at poll 1 of a window whose command
  cannot have completed. Named, not decided, and it is the cheapest thing a future arm could pin.
- Unchanged from 787: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST-owed to the
  next build); the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303`; the
  set-comparison pad repair (779 §7); the `rung_para` correction for values 12..23; the seam-address
  class; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell
  (782 §6); 784's `rail_name` cell; and 783's window-scope check — landed at rung 30's window and
  **not** landed at the ladder's other windows.
