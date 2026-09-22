# 530: the payload can already supply the root device, and the HFS+ row is a re-derivation rather than a copy

**A host-side reading: no device, no build, no switch.** It answers the two questions 529 section 8 left
open — *"whether the payload can set `rootdev` safely, and where"* and *"which it can reach, and with what
it must have already registered (the `bdevsw` and its `devfs` node)"* — and it prices the HFS+ port against
the two structures a row must match.

In one paragraph: **the seam between the number and its use is nine instructions wide, contains no call at
all on the success edge, and therefore cannot be hooked — but the payload does not need it, because the
number's only supplier is already wrapped.** `bsd_init` inlines `setconf()`; in the built image the whole
retry loop is twenty instructions at `0x80049b68`–`0x80049bac` whose only calls are `IOFindBSDRoot`,
`printf`, `strlcpy` and `vfs_mountroot`, and the value `vfs_mountroot` consumes is written by
*`IOFindBSDRoot`* (`mov r2, r5` with `r5 = 0x8055af2c = rootdev`) and by nothing else on the success edge.
So the point to reach is inside the supply — and there the payload is already present twice:
`__wrap_mdevadd` (`0x8047daac`) and `__wrap_mdevlookup` (`0x8047db1c`) are linked, and
`*root = mdevlookup(xchar)` (`IOKitBSDInit.cpp:467`) *is* the `dev_t`. A wrapper that returns a `dev_t`
of its own — major included — roots the system on a block device the payload registered itself, with **no
edit to `bsd_init` and no I/O Kit**. Two consequences follow that a reader would get wrong: the console
will say **`BSD root: md0`** while printing *the payload's* major, because `rootName` is built from the
boot variable and the numbers from the `dev_t`; and `rd=` is not optional — without it the wrapper never
runs, and `rd=mdX` for a device that is not configured **panics** (`:489`) rather than falling through. On
the port side, the row is **not copyable**: 2050's HFS row carries `VFS_THREAD_SAFE_FLAG`, a `#define`
local to 2050's *own* `vfs_conf.c:76` that **exists nowhere in 4570**, so the flag word must be
re-derived; and the numbers move in a way that punishes a plausible "fix" — the row's `vfc_typenum` is the
**historic** `17`, while `VT_HFS` is `16` in *both* trees.

## 1. The seam, in the built bytes rather than in the source

529 §8 fixed the order from the source: `setconf()` at `bsd/kern/bsd_init.c:940`, `IOFindBSDRoot` inside
it, `vfs_mountroot()` as the very next call at `:946`. The question is what lies *between* them, and the
source cannot answer that — `setconf` is `static` and the compiler decides. So: disassemble the shipped
image.

```
$ arm-none-eabi-objdump -d --start-address=0x80049190 --stop-address=0x80049e30 \
      out/stage90/xnu_arm_entry.elf        # bsd_init
```

The retry loop's body, which is `setconf()` plus `vfs_mountroot()` inlined and nothing else:

```
80049b38:  movw  r4, #0xaed4 ; movt r4, #0x8055   ; r4 = 0x8055aed4  rootdevice
80049b3c:  movw  r5, #0xaf2c ; movt r5, #0x8055   ; r5 = 0x8055af2c  rootdev
80049b40:  movw  sl, #0xebec ; movt sl, #0x8059   ; sl = 0x8059ebec  mountroot
80049b48:  movw  r9, #0x2955 ; movt r9, #0x8049   ; r9 = 0x80492955  "sd0a"
80049b54:  add   r6, sp, #252                     ; r6 = &flags   (a stack slot)
80049b60:  mov   r8, #0x6000000                   ; r8 = makedev(6, 0)

80049b68:  mov   r0, r4          <-- loop top
80049b6c:  mov   r1, #32
80049b70:  mov   r2, r5                           ; &rootdev
80049b74:  mov   r3, r6                           ; &flags
80049b78:  bl    8021cfd0 <IOFindBSDRoot>
80049b7c:  cmp   r0, #0
80049b80:  beq   80049ba8                         ; success: skip the fallback
80049b84:  mov   r1, r0                           ; ---- error edge only ----
80049b88:  movw  r0, #0x2908 ; movt r0, #0x8049
80049b90:  bl    8003a9b4 <printf>                ; "setconf: IOFindBSDRoot returned an error (%d);
80049b94:  mov   r0, r4                           ;  setting rootdevice to 'sd0a'.\n"
80049b98:  mov   r1, r9
80049b9c:  mov   r2, #32
80049ba0:  str   r8, [r5]                         ; rootdev = makedev(6, 0)
80049ba4:  bl    8000f014 <strlcpy>               ; ---- end error edge ----
80049ba8:  str   fp, [sl]                         ; mountroot = NULL   (fp holds 0)
80049bac:  bl    801ce100 <vfs_mountroot>
80049bb0:  cmp   r0, #0
80049bb4:  beq   80049c00                         ; mounted -> leave the loop
80049bb8:  mov   r1, r0
80049bbc:  mov   r0, r7                           ; 0x804928bd "cannot mount root, errno = %d\n"
80049bc0:  strb  fp, [r4]                         ; rootdevice[0] = '\0'
80049bc4:  bl    8003a9b4 <printf>
80049bc8:  movw  r1, #0x6700 ; movt r1, #0x8052   ; r1 = 0x80526700  boothowto
              ldr/orr/str                         ; boothowto |= RB_ASKNAME
              b     80049b68                      ; back to the loop top
```

Four things are measured here, not inferred from the source:

1. **The success edge is one instruction.** `beq 80049ba8` skips the entire error arm; the whole window
   between the number being produced and being used by `vfs_mountroot` is `str fp, [sl]` — and that store
   is `mountroot`, not `rootdev`.
2. **There is no call in the window.** A payload hook is a `bl`; the window has none. Reaching a
   `setconf`-to-`vfs_mountroot` point would mean *inserting* an instruction into Apple's `bsd_init`,
   which is a patch, not a wrap. **That candidate point is not reachable by any wrapper.** 529 §8 listed
   two candidates; one of them is now eliminated.
3. **The loop back-edge targets `0x80049b68`, after the register setup**, so a retry does not reload
   `r4`/`r5`/`sl`/`r8` — the addresses are loop-invariant and the state that changes is the state in
   memory (`rootdev`, `rootdevice`, `boothowto`) plus whatever the callees changed. A failed mount
   therefore re-enters `IOFindBSDRoot` **with the same arguments**.
4. **The only writer of `rootdev` in the boot path is inside `IOFindBSDRoot`** (`r2 = &rootdev`), plus the
   inlined error fallback at `0x80049ba0` which is Apple's own `makedev(6, 0)`. So "set `rootdev`" has
   exactly one reachable meaning: **change what `IOFindBSDRoot` returns.**

## 2. And that supplier is already wrapped

`nm` on the same image:

```
8047daac T __wrap_mdevadd
8047db1c T __wrap_mdevlookup
80287190 T mdevadd
8028747c T mdevlookup
```

Both wrappers exist in the build this project ships — they were added for a different question (the
`mdev` census) and they are exactly the lever this one needs. `IOFindBSDRoot`'s memory-device exit:

```c
	if((rdBootVar[0] == 'm') && (rdBootVar[1] == 'd') && (rdBootVar[3] == 0)) {
		...
		if(xchar >= 0) {
			*root = mdevlookup(xchar);          /* IOKitBSDInit.cpp:467 */
			if(*root >= 0) {
				rootName[0] = 'm'; rootName[1] = 'd'; rootName[2] = dchar; rootName[3] = 0;
				IOLog("BSD root: %s, major %d, minor %d\n", rootName, major(*root), minor(*root));
				*oflags = 0;
				goto iofrootx;
			}
			panic("IOFindBSDRoot: specified root memory device, %s, has not been configured\n", rdBootVar);
		}
	}
```

`*root` is `rootdev`, and its value is `mdevlookup`'s return value. **A `__wrap_mdevlookup` that returns
a `dev_t` the payload built — `makedev(payload_major, unit)` — supplies the root device.** No I/O Kit
object, no `IOMedia`, no edit to Apple's `bsd_init`, no new row: `vfs_mountroot` will `bdevvp` that number
and hand it to whichever `vfc_mountroot` accepts it.

### The two things this makes misleading

**The name is not the number.** `rootName` is composed from `rdBootVar[2]` — the *boot variable* — and the
numbers printed beside it come from the `dev_t`. So the console line for a payload-supplied root is

```
BSD root: md0, major <payload's major>, minor <unit>
```

and a reader taking `md0` from that line as evidence about which driver answered is reading the wrong
half of the sentence. This is `mi4-one-value-two-definitions` in the console: one line, two sources.
`rootName` also travels on — `bsd_init` passes `rootdevice` to `IOSecureBSDRoot(rootName)`
(`bsd_init.c:1106`, `:1111`), which re-parses `rd`/`rootdev` itself (`IOSecureBSDRoot` at
`IOKitBSDInit.cpp:664`) and hands the string to the platform expert's `SecureRootName`.

**`rd=` is a precondition, and the failure modes are not gentle.** `rdBootVar` is filled by
`PE_parse_boot_argn("rd", …)` **else** `PE_parse_boot_argn("rootdev", …)` **else** `rdBootVar[0] = 0`
(`:396-400`), and the memory-device branch requires the literal three-character shape `md<hexdigit>\0`
with `'A'-'F'` folded through `xchar & ~' '` (`:457-465`). Without it, `__wrap_mdevlookup` is never
called. With it and a device that is not there, the `panic` above runs — there is no fall-through. Note
also that a *later* exit, the `Apple_HFS`/`IOMedia` wait at `:530-566`, is the default when neither the
memory device nor a BSD name matches.

## 3. A hypothesis that died: the retry loop does not leak memory devices

Section 1's fourth measurement says `IOFindBSDRoot` is re-entered on every failed mount attempt, and this
file's RAMDisk branch calls `mdevadd`:

```c
	if(!didRam) {                                     /* IOKitBSDInit.cpp:440 */
		didRam = 1;
		if((regEntry = IORegistryEntry::fromPath("/chosen/memory-map", gIODTPlane))) {
			data = (OSData *)regEntry->getProperty("RAMDisk");
			if(data) {
				uintptr_t *ramdParms = (uintptr_t *)data->getBytesNoCopy();
				(void)mdevadd(-1, ml_static_ptovirt(ramdParms[0]) >> 12, ramdParms[1] >> 12, 0);
			}
			regEntry->release();
		}
	}
```

`mdevadd(-1, …)` picks the first free slot, and its own overlap test is a `panic`
(`bsd/dev/memdev.c:574`), so a second call with the same base looked certain to either panic or walk the
table. **It cannot**, because `didRam` is a **file-static** (`IOKitBSDInit.cpp:346`, `static int didRam =
0;`) — the branch runs once per boot, not once per attempt. The leak that the loop makes look inevitable
is guarded. Recorded because the retry loop is real and the guard is four lines above the call: a reader
who finds the loop and the `mdevadd` separately will predict a defect that is not there.

The panics that *are* live in `mdevadd` are the ones on its own contract: overlap (`:574`, `:576-594`
region), bogus explicit `devid >= NB_MAX_MDEVICES` (`:583`), re-adding an explicit devid (`:586`), and
exhaustion of the table — **`NB_MAX_MDEVICES` is 16** (`memdev.c:125`).

## 4. What a real block device must register — the shape, demonstrated

`memdev` is this tree's **only** block device, so it is the template for the eMMC driver 529 §5 priced.
Its registration, in `mdevadd`:

```c
	if(mdevBMajor < 0) {                                            /* memdev.c:590 */
		mdevBMajor = bdevsw_add(-1, &mdevbdevsw);            /* :591  any free major */
		if (mdevBMajor < 0) { printf("mdevadd: error - bdevsw_add() returned %d\n", mdevBMajor); return -1; }
	}
	if(mdevCMajor < 0) {
		mdevCMajor = cdevsw_add_with_bdev(-1, &mdevcdevsw, mdevBMajor);   /* :598 */
	}
	mdev[devid].mdBDev = makedev(mdevBMajor, devid);                     /* :602 */
	mdev[devid].mdbdevb = devfs_make_node(mdev[devid].mdBDev, DEVFS_BLOCK,   /* :607 */
	                        UID_ROOT, GID_OPERATOR, 0600, "md%d", devid);
	...
	mdev[devid].mdCDev = makedev(mdevCMajor, devid);
	mdev[devid].mdcdevb = devfs_make_node(mdev[devid].mdCDev, DEVFS_CHAR,
	                        UID_ROOT, GID_OPERATOR, 0600, "rmd%d", devid);
```

`bdevsw_add` is declared at `bsd/sys/conf.h:309` and implemented in `bsd/kern/bsd_stubs.c:161`, with its
own comment on the index rule at `bsd_stubs.c:125`: *"index = 1; /* start at 1 to avoid collision with
volfs (Radar 2842228) */"* — the parameter is the **major to prefer**, and `-1` means "first free".
So the registration a new driver needs is four calls in this order (`bdevsw_add`, optionally
`cdevsw_add_with_bdev`, `devfs_make_node` for the block node, `makedev` for the number), and the
`devfs_make_node` name is what a `rootdevice` string would match. It is worth stating plainly because it
is the one part of 529 §5's "absent" driver that is *not* absent: **the interface it must attach to is in
the tree, with a worked example, and the payload can call it** (`__wrap_mdevadd` proves the call path is
live).

## 5. The row is a re-derivation, not a copy

529 §3 found 2050's row; §8 asked what it costs. The answer is a comparison of the two structures the row
initializes.

### `struct vfstable` — grew by one trailing field

| | 2050 (`bsd/sys/mount_internal.h:299`) | 4570 (`:303`) |
| --- | --- | --- |
| fields 1–12 | `vfc_vfsops`, `vfc_name[MFSNAMELEN]`, `vfc_typenum`, `vfc_refcount`, `vfc_flags`, `vfc_mountroot`, `vfc_next`, `vfc_reserved1`, `vfc_reserved2`, `vfc_vfsflags`, `vfc_descptr`, `vfc_descsize` | **identical** |
| field 13 | — | `struct sysctl_oid *vfc_sysctl;` |

2050's HFS row has **12 initializers**; 4570's rows have **13** (`{ …, NULL, 0, NULL}`). A 12-element
initializer for a 13-field struct is legal C, leaves `vfc_sysctl` zero — and zero is **correct**, because
`vfs_init` *writes* it (below). So the trailing field is not a port cost. It is a reading hazard only.

### `struct vfsops` — same size, two reserved slots renamed

Counted off the definitions rather than reasoned about:

```
2050: 1 vfs_mount … 14 vfs_setattr, 15 void *vfs_reserved[7]      -> 14 + 7 = 21 pointer slots
4570: 1 vfs_mount … 14 vfs_setattr, 15 vfs_ioctl, 16 vfs_vget_snapdir,
      17..21 vfs_reserved5 … vfs_reserved1                        -> 16 + 5 = 21 pointer slots
```

Field-for-field identical through 14. **The struct is the same size**, and 4570 reinterprets 2050's
`vfs_reserved[0]`/`[1]` as `vfs_ioctl`/`vfs_vget_snapdir`. 2050's `hfs_vfsops` initializer
(`bsd/hfs/hfs_vfsops.c:7763-7778`) is positional and covers exactly 14 fields plus a `{NULL}` tail, so it
compiles against 4570's struct, lands every named field correctly, and leaves the two new slots NULL.

**And NULL there is safe, not a crash.** Both accessors refuse:

```c
int VFS_IOCTL(struct mount *mp, u_long command, caddr_t data, int flags, vfs_context_t context)
{
	if (mp == dead_mountp || !mp->mnt_op->vfs_ioctl)
		return ENOTSUP;
	return mp->mnt_op->vfs_ioctl(mp, command, data, flags, context ?: vfs_context_current());
}
int VFS_VGET_SNAPDIR(mount_t mp, vnode_t *vpp, vfs_context_t ctx)
{
	if ((mp == dead_mountp) || (mp->mnt_op->vfs_vget_snapdir == 0))
		return(ENOTSUP);
	...
}
```

(`bsd/vfs/kpi_vfs.c:374-397`.) Their only callers are the three snapshot operations
(`VFSIOC_MOUNT_SNAPSHOT`/`REVERT`/`ROOT` at `bsd/vfs/vfs_syscalls.c:955`, `:11698`, `:11953`) plus
`vfs_cprotect.c:296` — none of which is on the boot path. So the two NULL slots are a **refusal on an
unreachable path**, which is the same verdict 529 reached for `IOMedia`: absent, and not load-bearing.

### The flag word is where the copy breaks

| macro | 2050 | 4570 |
| --- | --- | --- |
| `VFC_VFSLOCALARGS` | `0x002` | `0x002` |
| `VFC_VFSGENERICARGS` | `0x004` | `0x004` |
| `VFC_VFSNATIVEXATTR` | `0x010` | `0x010` |
| **`0x020`** | **`VFC_VFSDIRLINKS`** | **`VFC_VFSCANMOUNTROOT`** |
| `VFC_VFSPREFLIGHT` | `0x040` | `0x040` |
| `VFC_VFSREADDIR_EXTENDED` | `0x080` | `0x080` |
| `VFC_VFS64BITREADY` | `0x100` | `0x100` |
| **`VFC_VFSTHREADSAFE`** | **`0x200`** | **does not exist** |
| `VFC_VFSNOMACLABEL` | `0x1000` | `0x1000` |
| `VFC_VFSVNOP_PAGEINV2` | `0x2000` | `0x2000` |
| `VFC_VFSVNOP_PAGEOUTV2` | `0x4000` | `0x4000` |

2050's HFS row's flag word is

```c
VFC_VFSLOCALARGS | VFC_VFSREADDIR_EXTENDED | VFS_THREAD_SAFE_FLAG | VFC_VFS64BITREADY
                 | VFC_VFSVNOP_PAGEOUTV2 | VFC_VFSVNOP_PAGEINV2
```

Six tokens, five of which 4570 defines. The sixth, `VFS_THREAD_SAFE_FLAG`, is **not a tree-wide macro at
all** — it is a `#define` *inside 2050's own `vfs_conf.c`*:

```c
#define VFS_THREAD_SAFE_FLAG VFC_VFSTHREADSAFE /* Only defined under CONFIG_VFS_FUNNEL */
...
#define VFS_THREAD_SAFE_FLAG 0                  /* line 78, the CONFIG_VFS_FUNNEL=0 arm */
```

and it appears **nowhere** in `external/xnu-4570.1.46`. A copied row is therefore a **compile error**
(`use of undeclared identifier`), not a silent difference — which is the good outcome, and it is loud
enough that the port cannot get this wrong by inattention. The same word is dropped from 4570's own NFS
row, which is 2050's minus exactly that token: the flag means nothing after the VFS funnels were removed.
The port's flag word is 2050's with that one token deleted.

The `0x020` row is the *latent* one. It is a value collision, and it is inverted in the dangerous
direction: a copied row that carried `VFC_VFSDIRLINKS` would be read as
**`VFC_VFSCANMOUNTROOT`** — it would silently become a mountroot candidate. 2050's HFS row does not set
`0x020`, so this port is unaffected; any *other* row copied from 2050 is not.

## 6. Registration order is the check — and it is checked twice

529 §7 found the ordering comment printed twice at the wrong row. The mechanism is stronger than a
comment. `vfs_init` (`bsd/vfs/vfs_init.c`) builds the linked list **from the static array, in order, up to
the first row with a NULL `vfc_vfsops`**:

```c
	maxtypenum = VT_NON;
	for (vfsp = vfsconf, i = 0; i < maxvfsslots; i++, vfsp++) {
		struct vfsconf vfsc;
		if (vfsp->vfc_vfsops == (struct vfsops *)0)
			break;                                   /* vfs_init.c:430 */
		if (i) vfsconf[i-1].vfc_next = vfsp;          /* :432  the chain is the array order */
		if (maxtypenum <= vfsp->vfc_typenum)
			maxtypenum = vfsp->vfc_typenum + 1;
		...
		if (vfsp->vfc_vfsops->vfs_sysctl) {           /* :444  reads the ops vector */
			... vfsp->vfc_sysctl = oidp; sysctl_register_oid(vfsp->vfc_sysctl);   /* :454, :456 */
		}
		(*vfsp->vfc_vfsops->vfs_init)(&vfsc);         /* :459  EVERY registered row */
		numused_vfsslots++; numregistered_fses++;
	}
	maxvfstypenum = maxtypenum;                       /* :464 */
```

Four consequences for a ported row:

- **Placement is a check, not a convention.** A row after the two `{NULL, "<unassigned>", …}` sentinels
  at the end of 4570's array is **never registered at all** — the walk stops at the first NULL
  `vfc_vfsops`. So "put HFS before mockfs" is necessary (529 §7's ordering) *and* "put it before the
  sentinels" is necessary for it to exist.
- **`vfs_init` is called on every registered row**, so the port's `hfs_init` runs during boot:
  `hfs_chashinit()`, `hfs_converterinit()`, `BTReserveSetup()` and five lock-group allocations
  (`hfs_vfsops.c:2776-2800`). Not a no-op, and it must work in this kernel.
- **`vfs_sysctl` must be valid**, because `vfs_init` dereferences `vfsp->vfc_vfsops->vfs_sysctl`. 2050's
  positional initializer puts `hfs_sysctl` at field 12, which is `vfs_sysctl` in 4570 too — so this is
  satisfied by construction.
- **`vfsconf` is the list**; `vfs_mountroot`'s walk (529 §4) then admits a row if `vfc_mountroot != NULL`
  **or** `VFC_VFSCANMOUNTROOT` is set, and calls `(*vfc_mountroot)(mp, rootvp, ctx)` in the first case.
  2050's row has a non-NULL `hfs_mountroot`, so it is a candidate without the new flag — the two trees
  agree on this row's shape even though 2050 has no `VFC_VFSCANMOUNTROOT` at all.

## 7. The port's actual size: the directory is not the file list

529 recorded "61 files / 68,285 LOC". Measured again, and decomposed:

```
$ find external/xnu-2050.18.24/bsd/hfs -type f | wc -l          -> 62
$ find external/xnu-2050.18.24/bsd/hfs -name '*.c' | wc -l      -> 36
$ find external/xnu-2050.18.24/bsd/hfs -name '*.h' | wc -l      -> 25
$ find … -name '*.[ch]' -exec cat {} + | wc -l                  -> 68285
```

So 68,285 LOC is exact, and it is the **36 `.c` + 25 `.h`** (62 files minus the `Makefile`). But the
directory is not what gets built. `bsd/conf/files` selects:

```
$ grep 'optional hfs$' external/xnu-2050.18.24/bsd/conf/files | wc -l   -> 36
     21 bsd/hfs
      7 bsd/hfs/hfscommon/BTree
      2 bsd/hfs/hfscommon/Catalog
      4 bsd/hfs/hfscommon/Misc
      1 bsd/hfs/hfscommon/Unicode
      1 OPTIONS/hfs
```

**35 sources plus the `OPTIONS/` line**, and one `.c` — `hfs_quota.c` — is selected by a *different*
condition: `bsd/conf/files:471  bsd/hfs/hfs_quota.c  optional quota`. (It is not needed for the ops
vector: `hfs_quotactl`, the row's field 5, is defined in **`hfs_vfsops.c:2349`/`:2355`**, not in
`hfs_quota.c` — checked because the reverse would have been a link error.) `QUOTA` is not answered by this
project's device table (`out/device_table.txt`, 11 conditions, no `quota` row), so 2050's `#if QUOTA`
blocks compile out and that file is simply not needed.

### The `#if HFS` guard, and why the row can vanish silently

`vfs_conf.c`'s HFS row and all 36 file lines sit behind `#if HFS`. `HFS` is **not defined anywhere by
hand** in 2050 — no `#define HFS` exists in the tree. It comes from the generated options header, and the
mechanism is the chain `OPTIONS/hfs … optional hfs` → `mkheaders` → `hfs.h`:

- `SETUP/config/mkmakefile.c:342` is the only place `OPTIONS/` is recognised; an `OPTIONS/` line creates
  an **internal pseudo-device entry** (`tdev.d_flags++`, `:377-381`) whose slave field decides the macro.
- `:377-404` uppercases the name (`allCaps`), matches it against the option list, and sets
  `tdev.d_slave = 1` if found — which is what makes `mkheaders` emit the symbol **as 1**; **not found
  emits it as 0**.
- The naming is by the part after `OPTIONS/`: `out/xnu_options/RELEASE/devfs.h` contains exactly
  `#define DEVFS 1`.

So the port adds `OPTIONS/hfs … optional hfs` to 4570's `bsd/conf/files` and `HFS` becomes 1. **If it does
not, the header says `#define HFS 0` and the row and all 36 files disappear without a diagnostic** —
`mi4-off-option-two-spellings` exactly: `#define X 0` is not "off" to `#ifdef X`, and here it is not "off"
to `#if X` either, it is *false*, which is correct and *silent*.

**And 2050's `config/MASTER` contains no `hfs` line.** The declaration is the `OPTIONS/` line in
`conf/files` itself. That is the 440 lesson with the roles swapped: there, `*/conf/files` *tested* and
`config/MASTER` *declared*, and a grep in the testing file that returned 0 was a measurement of the wrong
file — here a grep in `config/MASTER` returns 0 and says nothing at all.

### The build really does consume `conf/files`

Not a side note: `tools/build_xnu_arm_kernel.sh:27` states it and `:162` does it —

```
LS_MESSAGE=$("$TOOLS_DIR/xnu_config/list_sources.py" "$CONFIG" --write "$MANIFEST") || { … }
```

— so the manifest this project links is generated by `list_sources.py RELEASE` **from Apple's `*/conf/files`
lists**. The port's file list is therefore load-bearing in this project's own build, not just in Apple's:
adding the `optional hfs` rows *is* how the 35 objects enter `out/xnu_arm_manifest.txt`.

## 8. The numbers: `VT_HFS` is 16, and the row's 17 is not a mistake

Apple's own comment block in `bsd/sys/vnode.h:95-114` numbers the `enum vtagtype`, and 4570 states its own
values:

```c
enum vtagtype	{
	/* 0 */   VT_NON,
	/* 1 reserved */ VT_UFS,
	/* 2 - 5 */ VT_NFS, VT_MFS, VT_MSDOSFS, VT_LFS,
	/* 6 - 10 */ VT_LOFS, VT_FDESC, VT_PORTAL, VT_NULL, VT_UMAP,
	/* 11 - 15 */ VT_KERNFS, VT_PROCFS, VT_AFS, VT_ISOFS, VT_MOCKFS,
	/* 16 - 20 */ VT_HFS, VT_ZFS, VT_DEVFS, VT_WEBDAV, VT_UDF,
	/* 21 - 25 */ VT_AFP, VT_CDDA, VT_CIFS, VT_OTHER, VT_APFS
};
```

Read off the definition under Apple's own `/* 16 - 20 */` marker: **`VT_HFS` is 16 in 4570** — and 16 in
2050 as well (`VT_HFS` sits after `VT_UNION` there, so the insertion of `VT_MOCKFS` in 4570 shifted
nothing at that point). Yet 2050's HFS **row** carries the literal **`17`**, and 2050's devfs row carries
`19` while 2050's `VT_DEVFS` is `18`. The resolution is the field's own comment — `/* historic filesystem
type number */` — and it is confirmed by the one thing that would otherwise be inconsistent: 4570's
`enum fs_type_num` carries `FT_NFS = 2` and `FT_DEVFS = 19`, i.e. it **preserved the historic numbers**
that 2050's rows used, not the `VT_*` tags. So:

- the port keeps **`17`**, and adds `FT_HFS = 17` to 4570's `enum fs_type_num` — **free**, since that
  enum holds only `2`, `19`, `20`, `21`, `22` and `0x6D6F636B`;
- a "tidy-up" that replaced `17` with `VT_HFS` would write `16` and be **wrong**;
- `maxtypenum` becomes `max(17, 22) + 1 = 23`, and `vfc_typenum` 17 is what `vfs_init` will use for the
  sysctl node (`SYSCTL_STRUCT_INIT(_vfs, vfsp->vfc_typenum, …)`, `vfs_init.c:446`).

## 9. What this does not decide

- **Whether the payload should wrap `mdevlookup` or add a row.** Wrapping is measurable now and needs no
  edit to Apple's tree; a row is the general solution and needs the 35 objects compiled and the `HFS`
  option answered. The wrapping route is also what makes the *next* step evidence-bearing rather than
  speculative: it would root the existing mockfs root at a payload-owned **fake** `dev_t` whose
  `bdevsw` answers `DKIOCGETBLOCKSIZE`/`DKIOCSETBLOCKSIZE`/`DKIOCGETBLOCKCOUNT` from a buffer, which
  exercises `vfs_mountroot` → `bdevvp` → `vfs_init_io_attributes` (all `VNOP_IOCTL` on the device vnode,
  `vfs_subr.c:3158-3352`) with no eMMC driver at all.
- **The eMMC driver's shape.** 529 §5 priced SDHCI-proper; §4 above shows the interface it attaches to,
  not the driver.
- **Whether `hfs_init`'s allocations and `BTReserveSetup()` work in this kernel.** Read, not run.
- **Where the volume comes from.** Unchanged from 529 §7: the goal answers it with TWRP, which stays
  withheld.

## 10. Safety

No device was touched, no build ran, no switch was set, and no file outside `docs/` was written. **526
stays built, gated, frozen and unrun**, and the phone is off the bus: `lsusb` shows only
`18d1:d001` (the Halium LeLeco `zl1` at `usb 3-3`, a different port and serial — a false positive for this
phone), `fastboot devices` and `adb devices` are both empty, and the last `usb 3-10` event in
`/var/log/kern.log` is 522's `18d1:d00d` serial `4a2fe00b` disconnecting at 14:14:46 on 2026-09-22. A
power press is owed before 526 can run.
