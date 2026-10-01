# 867 — THE LAST CLAUSE, MAPPED: THE STRATEGY THAT MOVES A BYTE OFF THE CARD

**No build, no park, no arm, no device touched, nothing sent.** A map of the one clause the standing
goal still leaves open — **XNU's own eMMC driver** — written host-side from the source and from
captures already in hand. **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: the goal's last clause is to make the OS itself move a byte off the card. XNU
4570.1.46 **ships no storage driver at all** — six XNU trees under `external/`, none with an
`IOStorageFamily`/`IOBlockStorageDevice`/`AppleSDHC`, and the only block device in the whole kernel is
`bsd/dev/memdev.c`. The payload's storage ladder has *already* done the hardware half on this device —
it powers the card, sends `CMD0/1/2/3/7/8/13/17`, and **reads real sectors off the eMMC** (the GPT magic
`0x20494645`/`0x54524150` confirmed at LBA 1 in `rung51-lba1gpt-20260930-120713`). What remains is the
**join**: `src/platform/stage90_root_media.c` is a `bdevsw` whose `strategy` serves the RAM disk or ONE
**staged** sector — the ladder reads the card exactly once, at mount time, and copies — and the clause is
to make that `strategy` **issue a read per block**. **It is a `call`, not a new sub-module, and it cannot
move the image's size** (two new bytes + an unexported LBA accessor, the symbol addresses unchanged). But
its **correctness** rests on the storage ladder's selection, and rungs **57–60 are built, parked and
UNPRESSED** — so building it now would stack a fifth unwashed premise on four. **The next information is
one press.**

## 1. What XNU ships, and what it does not

The Explore sweep of `external/` returns the clause in one line: **no SD/MMC/SDHCI host-controller driver
exists in any of the six XNU trees.**

| tree | storage driver |
| --- | --- |
| `xnu-4570.1.46` | none — `iokit/Families/` is `IONVRAM` + `IOSystemManagement`; `bsd/dev/arm/` is 18 files, no storage; the only block device is `bsd/dev/memdev.c` |
| `xnu-2050.18.24`, `apple-xnu-rel-2050`, `xnu-upstream` | none — `iokit/Drivers/` is four Apple platform kexts |
| `distribution-iOS-ios-613`, `distribution-iOS-rel-iOS-6` | none |

The only SDHCI source in the repository is the **vendored Linux/Android kernel**:
`external/android_kernel_xiaomi_cancro/drivers/mmc/host/sdhci-msm.c` (3,359 lines) and its siblings
(`sdhci.c`, `sdhci-pltfm.c`, `msm_sdcc.c`, `mmc/card/block.c`, `mmc/core/core.c`). 529 §5 priced the
clause as "**3,359 lines of driver before any of it is an XNU `bdevsw`**" — and that count is a count of
the *vendor's* file, not of the work, because the payload has already transcribed the parts of it this
device needs (below).

## 2. What the payload has already proven on this device (the pressed half)

`src/entry/entry_storage.c` (10,256 lines) is a rung ladder — `STAGE90_XNU_STORAGE_PROBE` — that has, on
hardware, over rungs 0–56:

- found the two controller windows (`ST_CORE_MEM_BASE 0xf9824000`, `ST_HC_MEM_BASE 0xf9824900`), the GCC
  clock block (`0xfc400000`) and its SDCC1 branches, the TLMM pad (`0xfd512044`), and **both IRQs** —
  `pwr_irq` SPI 138 = intid 170, the data/command IRQ SPI 123 = intid 155;
- run the vendor's mode sequence (rung 2), `SOFTWARE_RESET 0x2F = RESET_ALL` (rung 4), the first clock
  set at 400 kHz (rung 6), the first `POWER_CONTROL 0x29` byte (rung 7), and the vendor's power-IRQ
  handler as a client of this image's own dispatcher (rung 8);
- sent `CMD0/1/2/3/7/8/13`, read `EXT_CSD` (rung 38 — "the ladder's first data phase"), and **read the
  medium itself** with `CMD17` (rung 43) — walking the GPT header, the entry array, and decoding
  ext4/f2fs superblocks (rungs 52–57).

The reading that proves the join is needed and that the hardware is not the obstacle is in the rung-51
capture: `xnu_live_storage_rd_w0 = 0x20494645` (`"EFI "`) and `rd_w1 = 0x54524150` (`"PART"`) — **the GPT
magic actually read off LBA 1 of the real eMMC**, with `rd_cap = 0x01d5a000` sectors from `EXT_CSD`.

## 3. Where the two meet — and the exact shape of the join

`src/platform/stage90_root_media.c` (720 lines) registers a BSD `bdevsw` (`entry_root_media_register`) and
answers the mount path's ioctls. Its own header is explicit (line 15):

> **It is NOT the eMMC driver and it moves no byte of the eMMC.** … The data half is the eMMC driver's job
> (529 section 5, the SDHCI work) and is where this stand-in is replaced.

The `strategy` today (`st_media_strategy`, line 324) does exactly one thing with a request: it picks a
`base`/`len` from `st_medium_disk_base`/`st_medium_disk_bytes` and `bcopy`s. For **disk 0** the base is
`g_stage90_ramdisk` (the entry image's own RAM disk — the root). For **disk 1** the base is
`st_medium_virt`, a **single 512-byte sector** that rung 59 read off the card and `entry_root_media_stage`
copied in. **Neither path consults the card at strategy time.**

**The join is therefore not a new driver object, and that is what makes it tractable.** The card's read
path is compiled into the same image and already runs the ladder; `stage90_root_media.c` and
`entry_storage.c` already call each other by `extern` (`9887: entry_storage.c` declares
`entry_root_media_stage`; `entry_trace.c` declares `entry_root_media_register`). The clause is:

> **give `st_media_strategy` a way to call the ladder's own read (`st_data_reset()` +
> `st_read_single_block()`) for the block it was handed, instead of copying a staged sector.**

That is **the same pair rung 59's `st_read_selected` already calls**, argument for argument. The shape is a
function call plus the addressing arithmetic the strategy already does (`off = buf_blkno(bp) * 512`), not
a kext, not an `IOMedia`, not a thread.

### 3.1 Why it must not be built yet (the binding constraint)

**Its correctness is the storage ladder's selection, and that selection is unwashed.** The strategy would
issue `CMD17` at an LBA derived from the block number the mount path asks for; whether that LBA is the
selected partition's depends on rungs 57 (walk the array, select the large extent), 58 (name it), 59
(bind it, `_sel_held`) and 60 (`mi_mdev = 0`, so the strategy is reached at all). All four are **parked
and unpressed**. The phase-status record states the rule directly:

> **Do not build rung 60 on three unwashed rungs.** … the storage ladder is **cumulative** … If rung 57's
> press answers otherwise, rungs 58 and 59 are built on a wrong premise.

A driver rung would inherit 57–60 **and** add its own premise — a fifth unwashed premise on four. The
device is not the obstacle; **the unwashed premise is.** One press (rung 57's frontier, which is the
oldest unwashed rung and the base of the chain) unstacks it.

### 3.2 The size and the build-clause facts, measured host-side

- **`build_entry.sh` binds the entry image at fixed addresses**; `xnu_arm_entry.bin`'s size is what the
  park names. The driver call adds **one `bl`** to `st_media_strategy` (to an unexported accessor) and
  **changes no static's layout** (the existing statics stay). The one-`bl` bound is the same class the
  recorded rungs reported ("size-neutral against rung 56's"); whether the linked image lands a byte or
  four shorter is a fact for the build itself, not this map.
- The new accessor (`st_read_lba`) is `static` and unexported, exactly as **`st_read_lba` is today** (it is
  a cell the ladder publishes, not a symbol). **No `extern` needs adding**, so no symbol moves.
- The build clause belongs to the **same class as `xnu_entry_864`**: read the **linked** image and require
  `st_media_strategy` to make the ladder's call — `st_data_reset` **and** `st_read_single_block` present
  in its body, and the `bcopy` path still present for the RAM-disk unit.

## 4. What is genuinely open, priced honestly

The clause is **two things**, and only one is hardware:

1. **The strategy issues the read** (§3) — a call, gated by the ladder's selection. **This is the goal's
   clause**, and it is what a driver rung would build.
2. **A filesystem.** Even with the strategy moving real bytes, **no XNU tree links an ext4/f2fs/HFS
   driver** (`nm` shows only `mockfs_vfsops`, `devfs_vfsops`, `routefs_vfsops`). 865 §3 measured that
   porting 2050's HFS+ is **eight small drifts and one droppable file** (`tools/hfs_port_probe.sh`,
   `CONFIG_PROTECT=0`: 34/36) — so a filesystem is tractable, but it is **not** the driver clause and does
   not move it.

The honest statement of where the goal stands: the OS **enters** (process 1 execs, `getpid_value = 1`,
rung 56's press), the hardware **runs** (card powered, clocked, sectors read), and the **last clause** —
the OS's own `strategy` moving a byte off the card — is **mapped but gated on one press.**

## 5. What this document changes

- **NOTHING IS BUILT, NOTHING IS ARMED.** No `bdevsw` edit, no ladder rung, no park. The parked arms
  (57/58/59/60) are **records, not a queue** and are not touched.
- **The map is the deliverable**, and its load-bearing sentence is the binding constraint: the driver
  clause is a *call*, so it cannot be blocked by size or ABI — it is blocked because **its correctness is
  the selection, and the selection is one press from being known.**
- **THE GOAL IS NOT MET.**

## 6. Safety

Unchanged and untouched: **no device, no `fastboot`, no switch, no press.** `fastboot boot` only, never
flash; the parked arms are records and not a queue. This document performs no build and no
`make check`-affecting edit; it is prose in `docs/experiments/`.