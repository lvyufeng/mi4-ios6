# 968 — the COW writable root, BUILT and PARKED (armed-d13-7107b998) (2026-10-09)

966 found that the HD2 iOS7 lab's writable root is a **block-layer RAM copy-on-write** — `leo_cow.c`,
82 lines of SoC-free plain C, already sitting in the XNU tree we build — over a read-only base. This rung
builds the same mechanism at **our** block device: a page-granular RAM shadow over the **mi4 card unit**,
so iOS userspace's first writes to `/private/var` can be served without ever writing the base volume.

It is a **host-side arm, PARKED.** No device was touched building it. **PRESS IS THE OPERATOR'S** — a park
is not a press.

## 1. What was wrong with 965c alone

965c mounts the real 896 MiB HFSX rootfs read-only. That mount is correct, but a **read-only** root cannot
serve iOS userspace: `/private/var` is written from the first seconds. Closing that needs either real
writes to the card (905's `HDD_WRITE`, destructive, and refused by the write gate for `userdata`) or a COW.
The COW is strictly better for a first boot: **the base is never written**, so every write is reverted by a
power cycle and the card head stays byte-exact.

## 2. The mechanism, implemented

`src/platform/stage90_root_media_cow.c` (NEW, self-contained) is the page shadow, a mi4-side twin of
`leo_cow.c`'s four functions:

- `st_cow_read_sector(sector)` — resident page's shadow, else read the base through the bound read door.
- `st_cow_prepare_write(first, count)` — for each touched page not yet resident, **read the whole 4096-byte
  page from the read-only base into a free arena slot first**, so the unwritten sectors keep their original
  bytes; capacity is proved *before* any base read.
- `st_cow_write_prepared_sector` — write 512 B into the resident shadow; mark dirty.

`st_cow_init` **accepts a non-page-multiple length** (`userdata` = 26,582,225 sectors, not a multiple of 8)
and the module handles the partial tail page. A `STAGE90_COW_SELFTEST` block (compiled only under that macro)
carries a `main()` with 30 checks, including the **base-never-written** safety assertion and a 9-sector
partial-tail test.

`src/platform/stage90_root_media.c` (the platform module) holds the arena and wires the shadow into
`st_media_strategy` — `B_READ` routes through the shadow, `B_WRITE` through `st_cow_prepare_write` +
`st_cow_write_prepared_sector` — and publishes the arm marker and the refusal counter:

```
#define ST_COW_ARENA_PAGES  512u                  /* 2 MiB of shadow */
#define ST_COW_HASH_SLOTS  1024u
static uint8_t       st_cow_arena[ST_COW_ARENA_PAGES * 4096u];
static st_cow_entry  st_cow_entries[ST_COW_HASH_SLOTS];
static uint32_t      st_cow_pending[ST_COW_ARENA_PAGES];
static st_cow        st_cow_shadow;
...
int entry_root_media_card_cow_arm_on(void) { return 1; }   /* line 249, under #if STAGE90_XNU_CARD_COW */
```

A write to an unsupported page increments `xnu_live_rootmedia_cow_refused` and returns `ST_COW_NO_SPACE` →
`ENOSPC` — **never a silent drop, never a base write.** So a run whose arena is too small *says so*, and the
arm's adequacy is a **measured** quantity, not a claimed one.

## 3. The arena is a floor, and the boundary moved

`ST_COW_ARENA_PAGES=512` (2 MiB) is a conservative floor. The HD2's own budget was `cap=1405 pages` with
**observed usage `used=2`** — so the HD2's *ceiling* is not mi4's *need*. The mi4 arena is sized from the
floor up, and the refusal counter is what proves it was big enough.

The real cost is to XNU's early free region, and it is **measured**: the platform module's `.bss` is linked
into the **entry image** (`STAGE90_ROOT_MEDIA_OBJ`, `LINK_OBJS` in `build_entry.sh`), so the static arena
raises `bss_end` → `ENTRY_ARGS_OFFSET` → `topOfKernelData`, and `arm_vm_init.c` sets
`avail_start = topOfKernelData + 10 pages`. Under the 16 MiB window:

| arm | `bss_end` | `topOfKernelData` |
|---|---|---|
| 965c (no COW) | `0x8065a960` | `0x80800000` |
| 968 (COW arena) | `0x8085f1a0` | `0x80A00000` |

So XNU's free region went **~8 MiB → ~6 MiB.** That is the arena's true price, and it is paid in the image,
not on the card. (The comment in `stage90_root_media.c` republishes exactly these numbers; a comment-only
correction left the object byte-identical `9e0f487f…`, so the park below stands.)

## 4. What the arm needs that is NOT on the module — the D13 rw clear

The COW serves writes; it does **not** make HFS mount read-write. `hfs_mountroot` calls
`hfs_mountfs(rvp, mp, NULL, 0, context)` and the ivar latches `MNT_RDONLY` once. The one host-side edit that
clears it is **905's** guarded `vfs_clearflags(mp, (u_int64_t)MNT_RDONLY)` immediately before that call
(`tools/hfs_patch_root_rw.py`).

That edit lives in the **untracked, re-provisionable `external/` tree** — so leaving it as a hand edit is the
`[[mi4-self-written-record-is-not-a-constraint]]` class: a re-checkout would produce an arm that mounts
read-only while its record says writable. Fixed by making it a **staged** edit with a re-derivation check:

- `tools/stage_d13_root_rw.sh` — thin, named wrapper beside `stage_d13_board.sh` / `patch_d13_memory_total.py`;
  runs the idempotent patcher on the D13 tree (guards that `$XNU` is really D13).
- `tools/check_d13_root_rw_staged.sh` — re-derives the property (present, **guarded**, **adjacent** to the
  ROOT `hfs_mountfs` call) and refuses drift. In `make check`. Tested rc=0 on the real tree, rc=1 with the
  guard removed (negative control).
- **`STAGE90_XNU_HFS=1` is the 4570 HFS port's switch and MUST stay 0 on D13** — D13's HFS is native
  (`bsd/conf/files` `optional hfs`). Setting it force-includes the 4570 port's `hfs_port_force.h` and its
  `cprotect` shims, which **collide** with D13's native `bsd/sys/cprotect.h` (`cprotect`/`cp_wrap_func`
  redefinition). The rw clear is threaded instead as the **raw define**
  `XNU_KERNEL_EXTRA_DEFINES='-DSTAGE90_HFS_ROOT_RW=1'` to the pool build, because D13 has no `HFS_PORT` and so
  `build_xnu_arm_kernel.sh`'s `HFS_PORT`-gated `-DSTAGE90_HFS_ROOT_RW=1` line does not fire.

## 5. The arm

| switch | value | why |
|---|---|---|
| `STAGE90_XNU_TREE_D13` | 1 | D13 (iOS 7), not 4570 |
| `STAGE90_XNU_HFS_ROOT_MEDIA` | 1 | root served as an HFS volume |
| `STAGE90_XNU_EMMC_STRATEGY` | 1 | the card unit is the ladder's block device |
| `STAGE90_XNU_ROOT_FROM_CARD` | 1 | root device = the card unit |
| `STAGE90_XNU_HFS_ROOT_RW` | 1 | `hfs_mountroot` clears `MNT_RDONLY` |
| `STAGE90_XNU_CARD_COW` | 1 | **this rung** — the RAM shadow serves the card unit's writes |
| `STAGE90_XNU_HDD_WRITE` | **0** | no base write; the COW reads the base through the READ door |
| `STAGE90_XNU_CARD_TOTAL` / `_FULL_EXTENT` | 1 | the card's whole capacity / full-extent read |

Note `HDD_WRITE=0` **with** `CARD_COW=1` is the deliberate pair: the card unit's `B_WRITE` is accepted and
served by the shadow, while every *other* unit (RAM blob, staged sector) still refuses with `EROFS`. The
`xnu_entry_905` messages were updated to say so instead of the old blanket "every unit refuses a write".

**Park:** `out/stage90/frozen/armed-d13-7107b998/`, 11 members; entry bin sha256
`7107b998cc5c5d2f4baa7da37fbfc88652fb8396958074dc52e7629130a4f243`, bytes 6331476; payload
`stage90.bin` `4caa752a…`; qcdt `stage90-qcdt.img` `52714827…`. `records/revert-set.txt` carries the
`set=armed-d13-7107b998` block.

## 6. Host-side verification (all green, no device)

- `make check` rc=0, including the newly-wired `check_d13_root_rw_staged.sh`.
- `resolve_arm_set` resolves `armed-d13-7107b998`; `check_set_name_rule` rc=0; `verify_revert_set` rc=0.
- `verify_press_ready.sh` 5/5 rc=0.
- `nm out/stage90/xnu_arm_entry.elf | grep entry_root_media_card_cow_arm_on` present; the linked clause
  greps `xnu_live_rootmedia_cow_refused` from the ELF.
- `check_undef_handler.py --selftest` + `--split` ok (this run also fixed a latent bug: `elf_words` read the
  `SHT_NOBITS` `.bss` as file bytes and ran 4 bytes past EOF — skipped when `_type == 8`).

### 6b. The runner reads the arm — the fifth unread family, closed

The arm's **entire adequacy claim** is three published keys in `st_media_strategy`:
`xnu_live_rootmedia_cow_wr_blocks` (served, on every write), `_cow_used_pages`/`_cow_dirty_pages` (the live
footprint), and `_cow_refused` (**only** on a write the arena could not hold). Until this rung
`scripts/run_and_capture.sh` read **none** of them — the `[[mi4-911-runner-now-reads-all-goal-clauses]]`
class, one rung after the USB ladder (963). A press of `armed-d13-7107b998` would have been silent about
whether a single write was served.

- The runner gains a COW block inside the root-media block, guarded on `_cow_wr_blocks` (absent on every
  non-968 arm, so those logs stay byte-identical — verified against the pre-edit runner's output on a real
  capture: `152cf1b4…` both sides). It prints `WRITABLE ROOT MET` when writes were served and no refusal,
  and `COW WRITE REFUSED` (naming the 2 MiB arena and that no write in the log proves a writable mount)
  when `_cow_refused` is present.
- `tools/check_runner_cow_family.py` (NEW, in `make check`) makes it a refusal, both directions: the block
  must read **both** markers (a presence-only check misses the failure branch), and every
  `xnu_live_rootmedia_cow*` key it names must be published by `stage90_root_media.c`. Four selftest
  mutations (served marker dropped, refused marker dropped, invented key, no block) are all refused.
- `scripts/press_968.sh` (NEW) is the recipe: 965c's real-rootfs medium (the COW's base **is** that image),
  the 968 arm hash, and the `--expect-arm` — steps 2/3 through the project's own gate + runner, never a raw
  fastboot, never a flash.

**The lane caveat.** `run_and_capture.sh` has been "another session's lane" before; the durable rule
(`[[mi4-file-lanes]]`) is to resolve with `ListAgents` first and, if the other party is not running, to
edit rather than invoke a lane rule that enforces nothing. Measured: this session is
`emmc-mount-write-verify`, and neither `mi4-ios6-1a` nor `run-experiment-526` is running — so the edit is
made here, and its cost is nil (the runner is not a build input of the parked image, unlike
`build_entry.sh`).

## 7. What it does NOT do

It does **not** make iOS boot. It removes the read-only wall that would stop userspace the moment it writes
`/private/var`. The mount (965c) and the ladder's read path are the rungs below; the userspace boot is the
rung above. It is **non-destructive and reversible** — a power cycle reverts every write — which is why the
HD2 marks its volume `VOLATILE=1`. Because the base is never written, this arm carries no brick risk to the
card, and it needs no journal.

Nothing was pressed: **PRESS IS THE OPERATOR'S.** Only `fastboot boot`, never flash; `33e80afe` unplugged.

## 8. Provenance

- Mechanism source: `external/xnu-hd2-darwin13/xnu/iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/leo_cow.{h,c}`
  (82 lines), `IOS7LeoHFSFile.cpp` (393). Follows [[mi4-966-hd2-writable-root-is-a-cow]].
- Built: `src/platform/stage90_root_media_cow.c` (NEW), `src/platform/stage90_root_media.c` (arena + wiring),
  `src/entry/build_entry.sh` (`CARD_COW` parse/refusals/keys/record/`nm` marker), `scripts/preflight_boot_check.sh`
  (`ENTRY_CFG_KEYS` + grounded detection), `Makefile` (rw check), `tools/stage_d13_root_rw.sh` +
  `tools/check_d13_root_rw_staged.sh` (NEW), `tools/check_undef_handler.py` (bss fix).
- Follows [[mi4-965-real-rootfs-is-hfsx]], [[mi4-913-ios7-rebase-decision]], [[mi4-956-d13-entry-window-ceiling]]
  (the 16 MiB window that the arena's `.bss` competes with). Device unmodified.