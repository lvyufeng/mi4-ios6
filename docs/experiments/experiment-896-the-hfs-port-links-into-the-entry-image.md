# 896 — the HFS+ port links into the entry image (895's step)

**A HOST-SIDE BUILD + A PARK. No device, no press (yet).** It does what 895 named as the only thing
between the tree and a mountable root: it builds the HFS+ port into the **entry image**, so
`vfstbllist` carries an HFS row **before mockfs** and `vfs_mountroot` reaches `hfs_mountroot`.

Arm **`armed-storage-59ec5fd6`** — entry bin `59ec5fd6f61f005d2e784aafb92012390d96fe3d2c19febcfd7be2e6db6c03ec`,
6,498,948 B (was 6,109,852 at 894). Switches **unchanged from rung 61**: `STAGE90_XNU_STORAGE_PROBE=60`,
`MOUNT=1`, `HFS_ROOT_MEDIA=1`, `EMMC_STRATEGY=1`, `ISTACK_SEPARATE=0`.

## 1. What 895 left

894's press cleared the ENOEXEC but mounted `md0` (mockfs-on-memory) with
`rootmedia_strategy_served` **absent**. 895 read the linked image and named the cause: the entry
image's `vfstbllist` was exactly `devfs / mockfs / routefs`, with **no `hfs_vfsops` and no
`hfs_mountroot`** — because the 874 root row is compiled by the **port** switch
(`STAGE90_HFS_ROOT`, into `bsd/vfs/vfs_conf.o`) and the pool was built **HFS-off**. The port was built
into the *platform* objects (885) and never the *entry image*.

## 2. The build

`tools/stage_hfs.sh` was already applied (`check_hfs_staged.sh` exit 0). The one missing build is the
**pool** with the port on:

```
STAGE90_XNU_HFS=1 XNU_KERNEL_CONFIG=STAGE90_XNU \
  XNU_MASTER_LOCAL=$PWD/tools/xnu_config/boot/STAGE90_XNU.local \
  XNU_KERNEL_EXTRA_DEFINES='-DSTAGE90_XNU_HFS_ROOT_MEDIA=1 -DSTAGE90_XNU_EMMC_STRATEGY=1' \
  ./tools/build_xnu_arm_kernel.sh
```

`HFS_PORT=1` compiles the 37 files into `out/xnu_kernel_obj/` and defines `STAGE90_HFS_ROOT=1`
(`build_xnu_arm_kernel.sh:525`), so `bsd_vfs_vfs_conf.o` gains the HFS row. The entry link globs the
whole pool (`build_entry.sh:27706`, ≥300 floor), so the 36 HFS objects (plus `vfs_journal.o`) enter
**with no edit to `LINK_OBJS`**. Entry bin **6,498,948 B** — 389,096 B larger, well inside the 16 MB
window (`ENTRY_SIZE`). The platform objects must carry the two defines too
(`build_xnu_arm_kernel.sh --platform-only … XNU_KERNEL_EXTRA_DEFINES='-DSTAGE90_XNU_HFS_ROOT_MEDIA=1 -DSTAGE90_XNU_EMMC_STRATEGY=1'`),
or `xnu_entry_882`/`xnu_entry_888` refuse a two-script disagreement.

**The seam did NOT move** (`STAGE90_XNU_SEAM_LR` stays `0x8004d2dc`): the entry group precedes the pool,
so the pool's new text does not move the exit, and the shims object's `.text` (1,268 B) did not cross a
page. One fewer repair than the plan allowed for.

## 3. Three link collisions, and the defect class

The first real kernel link of the port failed with **`multiple definition of`** three symbols — the
integration surface 869/871/873 never reached, because the probe linked against its own stand-ins and
885 linked the port only into the platform objects. A `comm` of every HFS object's defined globals
against the rest of the pool gives exactly these, and no others:

| symbol | 2050 defines | 4570 now also defines |
|---|---|---|
| `flush_cache_on_write` | `hfs_readwrite.c:94` (+ `SYSCTL_INT(_kern, …)`) | `vfs_subr.c:9836` (a `static`, but its `sysctl__kern_flush_cache_on_write` is not) |
| `root_unmounted_cleanly` | `hfs_vfsops.c:1271` (`SYSCTL_INT(_vfs_generic, …)`) | `vfs_subr.c:3919` (same node name) |
| `cp_key_store_action` | `hfs_cprotect.c:79/1745` | `vfs_cprotect.c:301` (4570 **moved** it) |

Two are **generated SYSCTL node names** (the macro concatenates `sysctl_##parent##_##name`); the third
is a function 4570 relocated into the shared VFS. This is `mi4-one-value-two-definitions` — 4570 and
2050-HFS both name one thing — and the port already answers it the same way elsewhere
(`cp_is_valid_class` in `hfs_cprotect_port.h`): a **scoped rename**, defined once in the port's
one-definition file `src/shims/hfs/hfs_port_force.h`. The forced header reaches only HFS files, and
each token is read in exactly ONE staged file (verified by grep), so the renames are safe and 4570's
own symbols are untouched. The HFS knobs move to `kern.stage90_hfs.*` rather than shadowing 4570's.

## 4. The gate's census met a data table (`mi4-measurement-defects`)

With the row and the closure linked, the **gate** refused: *"the entry image's reach into the
watchdog's page is either real or unread"*. The cause is a measurement defect, not a hazard: the
gate's flat word scan (three encodings, over `[0xf9010000, 0xf901ffff]`) matched a bare `0xf9011f7f`
at VA `0x805296f0` — a word **inside `cjk_bitmap`**, 2050's 0x28d8-byte CJK coverage table
(`hfs_encodings.c`). It is **DATA**: a named `OBJECT` symbol, reached by no `ldr`, materialising no
address. The scan could not tell it from a literal-pool word because the entry's linker script folds
`.rodata` into the output `.text` (`entry.ld`), so a data table and a literal pool are the same
PROGBITS section. **`readelf` still separates them by symbol**, so the census now excludes every named
`OBJECT` span before counting — which is the property the paragraph actually claims (*no **code**
carries the page*) rather than "no word of any kind". The exclusion is re-derived from the artifact, so
a table that grows or moves is excluded wherever it lands; the witness (the GIC one nibble below) and
the payload positive control are unaffected, and both still read non-zero — the scan is demonstrably
able to see.

## 5. The readings

- `xnu_entry_895`: **"vfstbllist carries an HFS row at word 17, with hfs_mountroot at +8 and BEFORE
  mockfs — so vfs_mountroot reaches it first."**
- `nm` on the linked ELF: `hfs_mountroot T 0x802bbd40`, `hfs_vfsops D 0x8061b3dc`; **304** defined HFS
  symbols (BTree, Catalog, Unicode, cnode) — no HFS stub.
- The entry bin carries the port's bytes; `xnu_arm_entry-sources.txt` (2335 B, unchanged size) lists
  the build's sources. `check_hfs_staged`, `check_hfs_root_blob`, `check_hfs_root_arm_split`, and
  `make check` all exit 0.

## 6. Park and readiness

`out/stage90/frozen/armed-storage-59ec5fd6/` (11 members) and the 11 `set=…` lines in
`records/revert-set.txt`. `verify_revert_set` → VERIFIED (11 files, 7 manifest checks);
`check_set_name_rule` → the name is the entry bin's own hash; `verify_press_ready --set
armed-storage-59ec5fd6` → **5 of 5**.

## 7. What this does and does not close

The entry image can now **mount HFS+**: the reader is linked, the row is before mockfs, and the volume
disk 0 serves carries `/sbin/launchd` (the same 8192-byte blob the ramdisk uses, `build_hfs_root_image.sh`).
**But the volume is the RAM blob `g_stage90_root_hfs`, not the real eMMC, which carries no HFS+ volume
(880).** So this closes *"is the port in the image that mounts, and can it mount"* — the step 895 named
— and **NOT** "XNU mounts the real eMMC". The eMMC-driver join stays a separate track. **THE GOAL IS
NOT MET.**

**Falsification:** if a press of this arm still mounts `md0`, the row is not being taken and the next
question is the static-table order, not the port — recorded here rather than papered over.