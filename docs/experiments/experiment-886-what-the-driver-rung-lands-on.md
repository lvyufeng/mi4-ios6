# 886 — WHAT THE DRIVER RUNG LANDS ON: ITS SITE, AND THE BUILD REFUSAL THAT CURRENTLY HOLDS IT

**A host-side map: no build, no park, no arm, no device, nothing sent.** It reads the tree to fix exactly
where 867 §3's join would land. **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 867 §3 mapped the last clause as a *call* — give `st_media_strategy` the ladder's own
`st_data_reset()` + `st_read_single_block()` for the block it is handed — and 884 washed the selection
premise that gated it. But a map is not a landing site, and reading the tree to fix one turns up **two
structural facts the map did not state**: (1) `stage90_root_media.c` is compiled into **both** the entry
image **and** the XNU kernel's platform objects, so the join lands in `src/platform/stage90_root_media.c`
next to `st_media_strategy` and to an entry-side accessor — **not** inside `entry_storage.c`; and (2) the
tree **already contains a hard build refusal for exactly this join** — `build_entry.sh:33509`, reached at
every `STAGE90_XNU_STORAGE_PROBE >= 58`, refuses to link an entry image whose `st_media_strategy` calls
`st_read_single_block`. So the driver rung is not merely *gated on a press*; it is **a deliberately-named
boundary that must be widened on purpose**, and the widening is the rung's real decision. There is **one
device action** in the whole plan, it is **operator-gated**, and there is **no host-side-only shortcut**:
any build of it overwrites `out/stage90/` and so the staged rung-58 press.

## 1. The landing site: `stage90_root_media.c`, and it is in both builds

`stage90_root_media.c` is compiled by **`src/entry/build_entry.sh`** (as
`$OUT/xnu_arm_entry_storage.o`, the `STAGE90_ROOT_MEDIA_OBJ` the entry link requires) **and** by
**`tools/build_xnu_arm_kernel.sh:1416`** (the kernel's platform-objects block). `st_media_strategy` is
therefore a body in **the entry image the payload embeds**, and the driver rung edits *that* file.

The edit is 867 §3's, now located:

- **in `st_media_strategy`** — replace (or add alongside, for the ladder-backed unit) the
  `bcopy(base + off, …)` at `stage90_root_media.c:496` with the ladder's own pair
  `st_data_reset(); st_read_single_block(1u);` reading the block `buf_blkno(bp)` names. The addressing
  arithmetic (`off = buf_blkno(bp) * ST_MEDIA_BLOCKSIZE`) is already there.
- **a new entry-side accessor** — the ladder's read body reads whatever LBA `st_sector_for_read` left in
  its own `static uint32_t st_read_lba`. To read *block N* rather than the one selected superblock, the
  rung needs a ladder-internal setter, exported to the platform file (this is the "unexported LBA
  accessor" 867 §3.2 named). `entry_storage.c` publishes `st_read_lba` as a *cell*, not a symbol; the rung
  adds the setter call.

**No size or ABI obstacle**: 867 §3.2 measured one `bl` added and no static's layout changed. What this
step adds is that the file is in **both** builds, so the same edit moves the entry image **and** the
kernel platform object — a fact a size argument that looked at only one of them would miss.

## 2. The boundary that must be widened, on purpose

`src/entry/build_entry.sh:33509` (inside the block opened by `if [[ $STORAGE_PROBE -ge 58 ]]`):

```bash
grep -qE "bl.*<st_read_single_block>" <<<"$sm_body" &&
    layout_fail "st_media_strategy (at $sm_addr) calls the ladder's \`st_read_single_block\`. **The
    strategy must serve the ALREADY-STAGED bytes and never reach the device**: a strategy that read the
    card would be a transfer on the OS's own schedule, with the card left in whatever state that transfer
    ends in - the reading the whole storage ladder exists to keep OFF the boot path. Nothing is rebuilt by
    this refusal"
```

and, beside it, line 33513's claim that `st_media_strategy` "calls NEITHER `st_read_single_block` NOR
`entry_root_media_stage`."

**This refusal is not a bug and not an omission** — it is a *deliberate invariant* with a stated reason:
the whole storage ladder exists to keep card transfers **off the OS's boot schedule**, and a strategy that
issued `CMD17` would put one there, leaving the card in whatever state that transfer ends. The driver rung
**is** the act of deciding that invariant no longer holds for the ladder-backed unit. So the rung is not
"add a `bl` and press": it is **widen the boundary, with the reason written down**, and keep the
RAM-disk unit's `bcopy` path intact (the current clause checks *both*, so the widening must be
per-unit-aware rather than a blanket deletion).

This is the same class as `mi4-a-claim-in-a-comment-is-not-a-check` in reverse: here a real property was
made a **build refusal**, so the driver rung cannot be built by accident — which is why 867 §3.1's
"unwashed premise" and this refusal are **two independent gates** and **both** must be opened.

## 3. The single device action, and its hazard

The ladder has **never issued a read on the OS's schedule**; every read it has made (rungs 43–58, 858's
press) was on the ladder's *own* schedule — one boot, one idle exit, one read. A driver strategy would
issue **one read per block the filesystem asks for**: many transfers, on demand, on the OS's schedule.

- **The prime read is bounded** — whatever the strategy's first `buf_blkno` is, it is a number the ladder
  can be told to read *once*, on the ladder's own schedule, exactly as `st_read_selected` does today. That
  is the operator press.
- **What that press measures**: the bytes at the block the *filesystem* (mockfs walking the RAM-disk root,
  or an HFS volume) asks for — the first reading, in the whole project, of the OS's own read path
  returning real medium bytes. `_rootmedia_served` incrementing with `_rootmedia_strategy_medium` naming
  the unit is the cell.

## 4. There is no host-side-only shortcut, and this is the honest part

The current live arm `armed-storage-5936b246` **is the staged rung-58 press**, and `out/stage90/` holds the
**only copy** of it that has ever existed. `build_entry.sh` hardcodes `OUT=out/stage90` (line 38, no
environment override), and the driver rung edits `stage90_root_media.c`, which is in the entry image — so
**building the driver rung overwrites the staged press**. There is therefore **no host-side build of the
driver rung that does not clobber the press**: the two are mutually exclusive on this tree, exactly as
884 §4 required (driver + port first, reformat last) — and the press comes first.

What *can* be done host-side, and is done here, is the **map plus the pinned sites plus the test plan** —
so that when the operator presses, the driver rung is a one-session build against a known plan.

## 5. What this changes

- **The driver rung's landing site is fixed**: `src/platform/stage90_root_media.c` (`st_media_strategy`,
  line 496), plus one exported LBA setter in `src/entry/entry_storage.c`. Both files are in **both** the
  entry and the kernel build.
- **A second gate is named**: `build_entry.sh:33509` is a hard refusal that the rung must widen, with its
  own reason, **without** deleting the RAM-disk `bcopy` path. The two gates are independent — a press
  washes 867 §3.1's premise; the refusal is a build-time decision neither a press nor a doc can make.
- **The contention is stated**: no host-side build of the rung can coexist with the staged press.
- **NOTHING IS BUILT, ARMED, PARKED OR SENT.** The parked arms are records, not a queue. **THE GOAL IS
  NOT MET.**

## 6. Safety

No build, no device, no `fastboot`, no `adb`, no park, no arm. Read-only tree inspection. The staged arm
`armed-storage-5936b246` stays parked and unpressed, verified intact this session (5/5 readiness).
**THE PRESS IS THE OPERATOR'S.**