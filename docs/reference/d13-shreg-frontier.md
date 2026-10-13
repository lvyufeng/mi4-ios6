# D13 CANCRO — the shared-region syscalls (294 / 438): every failure exit, by value

**Why this document exists.** 995 read dyld's real halt message by measurement
(`out/stage90/captures/cancro-last_kmsg-995-armed-d13-4efcabae.txt:4111-4138`, len=103, both pointers
byte-identical): `Library not loaded: /usr/lib/libbsm.0.dylib ...`. `/usr/lib/libbsm.0.dylib` is
launchd's **first** `LC_LOAD_DYLIB`; it has no file on the volume but **is** in the dyld shared cache
(696 images). The minimal explanation is that **dyld's shared cache was never mapped**: `mapSharedCache`
failed and every cached dylib degraded to a file lookup, whose first miss halted dyld. dyld's two
shared-region entry points are BSD syscalls, both wired in this tree — so the **only** unknowns are the
runtime return values and the failure site. 996's probe measures them; this document is its map.

**Method.** Every value below is a machine reading: `python3 -I` (`struct.unpack`, `int.from_bytes`),
`grep -n`, `arm-none-eabi-nm`, `arm-none-eabi-objdump -d` — read **by value**, from the linked image
where the claim is about the image. Line numbers are of
`external/xnu-hd2-darwin13/xnu/` (the **build tree**) at the **996 pre-image** unless a passage says
otherwise: sha256 `c9b845a9…` = `bsd/vm/vm_unix.c` (996 probe staged → `516df232…`; 997 wire staged →
`4b3ec2b8…`, +4 more lines below each gate), `f91c3460…` = `osfmk/vm/vm_shared_region.c`,
`d8f41a7b…` = `osfmk/arm/status.c`, `079c7a17…` = `osfmk/arm/ios7_compat.h`.

**Image identity (2026-10-12 22:05).** Image addresses below are read from
`out/stage90/xnu_arm_entry.elf` sha256 `1bc1750c…` (7,380,384 B) — the **997 wire-ON** build
(`IOS7LAB cache mapping` = 1, `IOS7LAB shared region` = 0, `IOS7SHREG` = 5, `IOS7WITNESS` = 22) — and
move with every relink. The wire-OFF image this document's original addresses came from is kept as
`out/stage90/captures/994-xnu_arm_entry.elf`; the 996-elf (`2af26d0a…`) address deltas and the staged
files' line-number drift are tabulated in `docs/reference/d13-shreg-g1-audit.md` §§5a–5b (cited, not
duplicated).

## 1. The two syscalls as wired here

| | 294 `shared_region_check_np` | 438 `shared_region_map_and_slide_np` |
|---|---|---|
| master | `bsd/kern/syscalls.master:452` `{ int shared_region_check_np(uint64_t *start_address) NO_SYSCALL_STUB; }` | `:671` `{ int shared_region_map_and_slide_np(int fd, uint32_t count, const struct shared_file_mapping_np *mappings, uint32_t slide, uint64_t* slide_start, uint32_t slide_size) NO_SYSCALL_STUB; }` |
| generated row | `out/xnu_generated_d13/bsd/init_sysent.c:440` `munge_w, munge_d, _SYSCALL_RET_INT_T, 4` | `:649` `munge_wwwwww, munge_dddddd, _SYSCALL_RET_INT_T, 24` |
| image row (`sysent` @ `0x804e6f84`, stride 24, read at `sysent+no*24`; rows @ `0x804e8b14` / `0x804e9894`) | `narg=1 call=0x8041fd84 arg_bytes=4` | `narg=6 call=0x80420320 arg_bytes=24` |
| body | `shared_region_check_np @0x8041fd84` (`vm_unix.c:1105`) | `shared_region_map_and_slide_np @0x80420320` (`vm_unix.c:1528`) |
| helpers | — | `_shared_region_map @0x8041ff40` (`:1231`) · `_shared_region_slide @0x80420280` (`:1485`) · `shared_region_copyin_mappings @0x8041fe4c` (`:1181`) · `vm_shared_region_map_file @0x80113cf4` (`vm_shared_region.c:969`) · `vm_shared_region_enter @0x80113fb8` (`:1221`) |

`struct sysent` = `{int16 narg; int8; int8; ptr call; ptr munge32; ptr munge64; int32 ret; uint16
arg_bytes}` (`bsd/sys/sysent.h:42-53`) = 22 B, aligned to **24**. The two rows above are the "the
dispatch is the claim" check (`mi4-a-claim-in-a-comment-is-not-a-check`) the 996/997 checkers re-run.

## 2. The wire shape — PROVEN, and it is a 994-class defect *in the struct*, not in the arg list

**Arg list: clean.** dyld's wrapper at `0x2be0579c` builds the call at `0x2be058c8-0x2be058da`:
`r1 = fp(fd)`, `r2 = r6(count)`, `r3 = r4(mappings)`, then `str r5,[sp]` / `str r1,[sp,#4]` /
`str r0,[sp,#8]` = **(slide, slide_start, slide_size)**, `mov.w r0,#438`, `blx 0x2be1c200` (dyld's
`syscall` shim). Six one-word arguments — exactly the master's signature, exactly `munge_wwwwww`/24.
For 294 the wrapper `0x2be05774` is `mov r1,r0; … mov.w r0,#294; blx 0x2be1c200` — one pointer arg,
`munge_w`/4 is right.

**The mapping struct: 32 B on the wire, 20 B in this kernel.** By value in dyld:

* `add.w r0, r4, r3, lsl #5` (`0x2be057ba`) and `add.w r1, r0, sl` / `add.w r0, r1, fp, lsl #5`
  (`0x2be05148-0x2be0514c`) — **×32 stride**; the copy loop is `add.w r8, r8, #32` (`0x2be05176`)
  moving 8 words (`ldmia`/`stmia`, `0x2be0517e-0x2be051ac`).
* field use: `ldr r6,[r8]` (address), `ldr r4,[r8,#8]` (size), `ldr r2,[r8,#16]` (fileOffset),
  `ldr r1,[r8,#20]` (maxProt), `ldr r0,[r8,#28]` (initProt) — `0x2be05848-0x2be05870`.

By value in the **wire-OFF image** (the 994 control `out/stage90/captures/994-xnu_arm_entry.elf`, and
the 996 pool object `3d660224…`): `shared_region_copyin_mappings @0x8041fdf0` is

```
8041fdf4: add r1, r2, r2, lsl #2     ; count * 5
8041fdf8: lsl r2, r1, #2             ; count * 20        <- sizeof(struct shared_file_mapping_np) == 20
8041fdfc: mov r1, r3 ; b copyin
```

**In the current 997 wire-ON image the same function is @`0x8041fe4c` and the ×20 pair is gone —
the gated 32 B branch is what is linked:** `0x8041fe60: add r0, r1, r2, lsl #5` (bounds: user +
count*32), `0x8041fe7c: lsl r2, r9, #5` (copyin size = count*32), `0x8041ff28: add r6, r6, #32`
(per-record pointer stride), guarded by `sub r0,r2,#9; cmn r0,#8; bcc → EINVAL` (count 0…8) and, per
record, an `address>>32 | size>>32 | offset>>32` test and `address + size > 0x40000000`
(`0x8041fee8: rsbs r1, r4, #1073741824`). The 996-elf addresses of the ×20 insns were
`0x8041fe44`/`0x8041fe48`/`0x8041fe50` (audit §5a).

because `mach_vm_address_t`/`_offset_t`/`_size_t` are `uint32_t` here (`osfmk/mach/arm/vm_types.h:107-113`,
"We target iOS 4.3") while `vm_prot_t` is `int` → `{4,4,4,4,4}` (`osfmk/mach/shared_region.h:108-114`).

**The misparse, for the measured cache (3 mappings, `mappingInfo` read with `<QQQii` at
`/mnt/data/ios7-payload/v2/ios7/rootfs.hfs @0x455d000+0x68`: `0x2c000000/0x0c266000/0` prot 5,5;
`0x38268000/0x01ce0000/0x0c266000` prot 3,3; `0x39f48000/0x022ed000/0x0df46000` prot 1,1):** the
kernel copies 3×20 = **60 of the 96 bytes** (no overrun, no EFAULT — `count * 20 <= 96`) and reads

| kernel slot | address | size | file_offset | max | init |
|---|---|---|---|---|---|
| m[0] | `0x2c000000` | **0** | `0x0c266000` | 0 | 0 |
| m[1] | 5 | 5 | `0x38268000` | 0 | `0x01ce0000` |
| m[2] | 0 | `0x0c266000` | 0 | 3 | 3 |

m[0].size == 0 → `vm_map_round_page(0)` → `vm_map_enter_mem_object` returns **KERN_INVALID_ARGUMENT at
`vm_map.c:2344` (`initial_size == 0`)** → `_shared_region_map` folds it to **EINVAL(22)** at
`vm_unix.c:1428-1431` → 438 returns **22** on the first mapping, *before* any slide work. This is the
same defect family as 994 (`STAGE90_XNU_IOS7_TRAPS`) and the tree already carries its fix **gated**:
`vm_unix.c:1174-1204` is a 32 B-wire branch (`typedef char …wire_size[…==32?1:-1]`, per-mapping
`printf("IOS7LAB cache mapping …")`) reachable only when `ios7lab_cache_abi()` is 1 — i.e. never on
CANCRO today (the selector, `osfmk/arm/ios7_compat.h`, returns 0: neither `BOARD_CONFIG_QSD8250_LEO`
nor `BOARD_CONFIG_ARMPBA8` is defined by `tools/build_xnu_arm_kernel.sh:202`, and no `ios7lab_vmabi`
boot arg is parsed on that path).

**Prot values the kernel accepts.** `vm_shared_region_map_file` masks the user's words with
`VM_PROT_ALL` before the enter (`vm_shared_region.c:1113-1114`, `:1128-1129`), so `VM_PROT_ZF 0x10` and
`VM_PROT_SLIDE 0x20` (`shared_region.h:116-117`) never reach `vm_map_enter_mem_object`'s
`& ~VM_PROT_ALL` check (`vm_map.c:2340-2345`); that check therefore **cannot** fail from a wire word.
The wire's *raw* words do steer one branch before the mask: `if (init_prot & VM_PROT_ZF)` chooses
anonymous zero-filled memory (`:1053-1055`), and `if (init_prot & VM_PROT_SLIDE)` is what selects **the
one mapping that gets slid**, filling `mapping_to_slide` (`:1061-1085`).

## 3. 294 `shared_region_check_np` (`vm_unix.c:1105-1161`) — three exits

| file:line | condition | returns |
|---|---|---|
| `vm_unix.c:1126-1128` | shared region exists but `vm_shared_region_start_address` != 0 — **empty** (`sr_first_mapping == (mach_vm_offset_t)-1`, `vm_shared_region.c:842-844`) | **ENOMEM 12** |
| `vm_unix.c:1130-1138` | copyout to `uap->start_address` fails | EFAULT 14 (copyout's) |
| `vm_unix.c:1150-1153` | `vm_shared_region_get(current_task()) == NULL` — no region | **EINVAL 22** |

copyout width is `sizeof(mach_vm_offset_t)` = **4** in the wire-OFF image (`0x8041fdc8: mov r2,#4` /
`0x8041fdcc: bl copyout` — the 994 control; the 996 elf's was `0x8041fdf8`/`0x8041fdfc`, audit §5a).
The current wire-ON image widens this one site to the platform ABI's full 8 B
(`0x8041fe04: mov r2,#8` / `0x8041fe08: bl copyout`, the high word assembled as 0 — the wire checker's
clause 3), which is the same platform-gated width the source carries. dyld allocated 8 bytes for it
and zeroed both words (`0x2be04fd4-0x2be04fd8`), then reads **only the low word**
(`ldr.w r4,[r4,#156]`, `0x2be05020`), so **both widths** stay inside dyld's buffer and the
caller-visible bytes are identical — no defect at 294 either way.

**Reachability.** A region *does* exist on CANCRO: `vm_map_exec` calls `vm_shared_region_enter`
unconditionally (`vm_map.c:8642`, `fsroot = p->p_fd->fd_rdir` = NULL, `bsd/kern/kern_exec.c:916-919`),
and `vm_shared_region_lookup` **creates** on first use (`vm_shared_region.c:363-372`), keyed
`(CPU_TYPE_ARM, NULL, FALSE)`, with `SHARED_REGION_BASE_ARM 0x30000000` / `SIZE_ARM 0x10000000`
(`shared_region.h:67-70`) — the IOS7 geometry override (`vm_shared_region.c:626-638`,
`0x2C000000/0x14000000`) is behind `IOS7_COMPAT_PLATFORM` = 0. It is empty at first lookup, so
launchd's first call must read **12**.

## 4. 438 `shared_region_map_and_slide_np` (`vm_unix.c:1528-1605`) — every exit

| file:line | condition | `kr` / returns |
|---|---|---|
| `:1545-1548` | `p->p_fd->fd_rdir != NULL` (chrooted) | EINVAL 22 |
| `:1550-1559` | `vm_shared_region_sliding_valid` = KERN_FAILURE: a *different* slide after a completed one (`vm_shared_region.c:1400-1403`) | KERN_FAILURE 5 |
| `:1550-1560` | same slide as the completed one (`KERN_INVALID_ARGUMENT`, `vm_shared_region.c:1404-1414`) is rewritten to KERN_SUCCESS and `goto done` | **0 — success with nothing mapped** |
| `:1562-1568` | `count == 0` | 0 (silent success) |
| `:1569-1579` | `count > SFM_MAX_STACK (8)` | KERN_FAILURE 5 |
| `:1581-1583` | `shared_region_copyin_mappings` fails: EFAULT from `copyin`; in the gated 32 B branch also EINVAL for `count==0 \|\| count>8 \|\| user_mappings+count*32 > 0xFFFFFFFF` (`:1182-1184`) | EFAULT 14 / EINVAL 22 |
| `:1586-1589` | `_shared_region_map` != 0 — see below | its errno (early `return`, no undo) |
| `:1591-1602` | `slide != 0` and `_shared_region_slide` != 0; runs `vm_shared_region_undo_mappings(NULL,0,…)` first | EFAULT 14 / KERN_FAILURE 5 |

`_shared_region_map` (`:1231-1482`) errno exits, in call order: `fp_lookup` → EBADF 9 (`:1259`);
`fg_type != DTYPE_VNODE` → EINVAL 22 (`:1269`); `!(fg_flag & FREAD)` → EPERM 1 (`:1280`);
`vnode_getwithref` → VFS errno (`:1290`); `v_type != VREG` → EINVAL (`:1301`); `CONFIG_MACF` mmap check
(`:1311-1316`, MACF off here); `fd_rdir != NULL` → EINVAL (`:1321-1329`, unreachable — 438 already
refused chroot); `vp->v_mount != root_vp->v_mount` → EPERM (`:1331-1339`); `vnode_getattr` → errno
(`:1344`); `va_uid != 0` → EPERM (`:1353`); `vnode_size` → errno (`:1364`); `ubc_getobject` returns
NULL → EINVAL (`:1377`); **`vm_shared_region_get() == NULL` → `goto done` without setting `error`, so it
returns whatever the last call left = 0 = SUCCESS** (`:1394-1402` — a defect, visible only if the exec
path ever fails to create a region); then `vm_shared_region_map_file`'s kr folded to errno at
`:1418-1433` (**KERN_INVALID_ADDRESS→EFAULT 14, KERN_PROTECTION_FAILURE→EPERM 1, KERN_NO_SPACE→ENOMEM
12, KERN_FAILURE/KERN_INVALID_ARGUMENT/other→EINVAL 22**).

`vm_shared_region_map_file` exits (`vm_shared_region.c:969-1212`): `sr_root_dir != root_dir` →
KERN_PROTECTION_FAILURE (`:997-1007`) — **unreachable for a non-chrooted process** (both sides are
NULL); the `sr_mapping_in_progress` sleep loop (`:1014-1018`) blocks rather than fails; per mapping,
`vm_map_enter` (anonymous, `& VM_PROT_ZF`) or `vm_map_enter_mem_object` (file-backed) returns
KERN_NO_SPACE / KERN_INVALID_ADDRESS / KERN_INVALID_ARGUMENT — **KERN_MEMORY_PRESENT is tolerated as
"already mapped"** and resets `mappings[i].sfm_size = 0` (`:1142-1165`) so undo skips it. A file-backed
enter past `named_entry->size` is KERN_INVALID_ARGUMENT (`vm_map.c:2380-2381`); `initial_size == 0` is
KERN_INVALID_ARGUMENT (`:2344`).

**No MACH-O-specific attribute is consulted anywhere on this path**: the file checks are
`DTYPE_VNODE` + `FREAD` + `VREG` + `uid == 0` + same mount as root — exactly what an HFS vnode
provides. There is no `vnode_isreg`-beyond-`VREG`, no `ubc` size-vs-mapping check, no code-signing
consultation.

## 5. The slide machinery (`:1591-1602`, `_shared_region_slide :1485-1525`)

`mapping_to_slide` is a **stack variable in 438 that the callee may never write** (`:1533`,
`vm_shared_region.c:1079-1084` only fills it for a mapping whose `init_prot & VM_PROT_SLIDE`). The
measured cache has **no prot word with 0x20 set** (5,5 / 3,3 / 1,1), so on this wire `found_mapping_to_slide`
stays FALSE and `slide != 0` would feed **uninitialized stack** into `vm_shared_region_slide_init` as
`slide_info_size`: either "> SANE_SLIDE_INFO_SIZE (1 MiB)" → KERN_FAILURE + printf
(`vm_shared_region.c:1446-1450`) or a `kmem_alloc` of a garbage size. That is the state to reproduce
before drawing conclusions from any slide error. `_shared_region_slide` itself: `slide_info_init`
failure → printf `"slide_info initialization failed with kr=%d"` (`vm_unix.c:1496`); NULL
`slide_info_entry` → EFAULT; `copyin(uap->slide_start, …, slide_size)` → EFAULT; 
`vm_shared_region_slide_sanity_check` → EFAULT + `"Sanity Check failed for slide_info"` (`:1512-1514`).
`slide_init` requires a **non-internal** object (`object->internal == TRUE` → KERN_INVALID_ADDRESS,
`vm_shared_region.c:1499-1500`) and `kmem_alloc` success (`:1502-1526`). `slide_info.sr =
vm_shared_region_get(current_task())` deliberately leaks one reference (`:1511-1522`).

## 6. The pmap primitives — real, not stubs

`pmap_nest` (`osfmk/arm/pmap.c:4337-4434`) copies L1 (1 MiB TTE) entries from the subordinate pmap into
the grand one, expanding the subordinate with `pmap_expand`; `pmap_unnest` (`:4441-4485`) clears them.
Both return KERN_SUCCESS on their normal path and **panic** on their `va_start != nstart` /
unaligned-size preconditions — no `KERN_FAILURE` stub anywhere in the chain. `pmap_nesting_size_min/
max` are both `0x8000000` (`:209-210`), so the nested window is 128 MiB and `vm_shared_region_enter`
(`vm_shared_region.c:1311-1348`) maps the region in `pmap_nesting_size_max` chunks. Those primitives
are only reached from `vm_map_enter` for a `VM_MEMORY_SHARED_PMAP`-tagged, nested entry — i.e. from the
region *enter*, not from the 438 mapping path (which enters the region's submap, `memory_object_control_t`
-> `vm_named_entry->backing.map`), so a 438 failure never touches them.

**Geometry (G1), by value:** the cache's three mappings live at **`0x2C000000..0x3C236000`**, the
CANCRO shared region at **`0x30000000..0x40000000`**. `target_address = sfm_address - sr_base_address`
(`vm_shared_region.c:1088-1089`) is computed in 32-bit arithmetic: mapping[0] gives `0xFC000000`, far
outside the submap's `[0, 0x10000000)` — the range-fit `RETURN` (`vm_map.c:1797`) is KERN_NO_SPACE →
**ENOMEM 12** (folded at `vm_unix.c:1418-1433`). mapping[2]'s range **does** fit that submap
(`0x39f48000 + 0x022ED000 − 0x30000000 = 0x0C236000 < 0x10000000`); its real wall is the **named-entry
size check** (`vm_map.c:2380-2381`: `0x0DF46000 + 0x022ED000 = 0x10233000 > mem_entry->size`
`0x10000000`, set at `vm_shared_region.c:681`) → KERN_INVALID_ARGUMENT → EINVAL (this corrects the
earlier "mapping[2] would not fit either" — audit §5c1). Under G1 (entry size `0x14000000`) both fit.
So even with the wire fixed, **G1 must open** (base `0x2C000000`, size `0x14000000`, the
`IOS7_COMPAT_PLATFORM` create override) for the cache to land where dyld expects it.
**Correction (997, §9 — measured; the four mapped spans there are `0x2cd9c000..0x3d148000`):** on
the live call the block is the **FIXED** path's guard (`vm_map.c:1843-1846`: `start = va − sr_base`
= `0xFCD9C000`; `end = start + size` wraps in 32-bit; `start >= end`) returning `kr=1`
KERN_INVALID_ADDRESS → EFAULT 14 — not this paragraph's ANYWHERE-loop KERN_NO_SPACE at `:1797`.
Geometry conclusion unchanged; only the line and the kr differ.

## 6b. The slide is fixup-only — the geometry tension, resolved by value

996 measured `slide=0x3811000` (§7b DELTA 2), which raises arithmetic the model must survive: the cache
is 315,952,666 B = `0x12D50D1A`; unslid from `0x2C000000` it ends `0x3ED50D1A` (fits
`0x2C000000..0x40000000`), but that same span **plus** the slide would end `0x42561D1A` — past
`0x40000000`, impossible in 32-bit VA. By value, **no code on this path adds `slide` to an address**:

* Mapping addresses are used verbatim, region-relative: `target_address = mappings[i].sfm_address -
  sr_base_address` (`vm_shared_region.c:1088-1089`) — there is no slide term.
* `slide` is added only to **pointer words inside mapped pages**: `*ptr_to_slide += slide;`
  (`vm_shared_region.c:1629`, inside `vm_shared_region_slide(vaddr, pageIndex)`), whose only caller is
  the page-in slide (`vm_pageout.c:7948` in `vm_page_slide`; `pageIndex = (page->offset -
  slide_info.start)/PAGE_SIZE`).
* `slide_start` is a **user VA**: it is the *source of the kernel's copyin* of the slide-info blob —
  `copyin((user_addr_t)slide_start, slide_info_entry, slide_size)` (`vm_unix.c:1533`) — so
  `0x3F9BD000` must be mapped in dyld's space when the slide runs, and `slide_size` `0x89000` sits
  under `SANE_SLIDE_INFO_SIZE` = 1 MiB (`vm_shared_region.c:1393`).
* `slide_info.start/end` are **file offsets** of the slid mapping (`offset = start =
  mapping_to_slide.sfm_file_offset` at `vm_shared_region.c:1456`; `slide_info.start = offset`,
  `.end = start + size` at `:1508-1509`) — not virtual addresses.

The tension dissolves rather than forcing a re-model: the slid values are page **content**, and the
mapping addresses are whatever dyld sends. **Resolved by 997 (§9, measured):** the four `va` fields
ARE slid (m[0].va `0x2cd9c000` = the unslid base `0x2C000000` + that run's `slide=0xd9c000`
exactly), and the 32 B branch's bound does **not** fire — every slid span still ends below
`0x40000000` (m[3] ends `0x3d148000`); the wall is the 32-bit wrap of the *relative* target in
`vm_map`'s FIXED path (`vm_map.c:1846`), not an out-of-window address.

**`mapping_to_slide`, the §5 wire**: selected by `mappings[i].sfm_init_prot & VM_PROT_SLIDE`
(`vm_shared_region.c:1061`), **first match wins** (`found_mapping_to_slide`, `:1082`; later matches
only log). §5's "the measured cache has no `0x20` prot" premise came from the now-falsified
three-mapping model (§7b DELTA 1) — with `count=4`, 997's `init=` fields are what settle it. On the
slide path `_shared_region_slide` runs `slide_init` → the `object->internal` check → `kmem_alloc` →
`copyin` from `slide_start` → `vm_shared_region_slide_sanity_check` (`vm_shared_region.c:1548-1585`:
toc/entry indexes bounded by the copied header's counts; a `vm_protect` failure **panics** at
`:1564`; an index failure prints + EFAULT).

## 7. What the 996 probe will print, and what each reading means

The probe (`tools/patch_d13_shreg_probe.py`, `STAGE90_XNU_SHREG_PROBE`, in `bsd/vm/vm_unix.c` only)
prints, bounded to the first 4 calls of each syscall and fail-soft (`printf` only):
`IOS7SHREG check_np pid=%d out=0x%08x`, `IOS7SHREG check_np ret=%d`,
`IOS7SHREG map_slide pid=%d fd=%d count=%u mappings=0x%08x slide=0x%x slide_start=0x%08x size=0x%08x`,
`IOS7SHREG map_slide ret=%d` (all three returns), and — immediately after the call, because the kr is
not visible at 438's exit sites — `IOS7SHREG map_file kr=%d`.

* `check_np ret=12` → a region exists and is empty (the expected first call).
  `ret=22` → no region at all (vm_shared_region_enter failed) — a different, deeper defect.
  `ret=0` → something mapped the region before launchd's dyld ran.
* `map_slide count=3 … slide=0 ret=22` **with** `map_file kr=4` (KERN_INVALID_ARGUMENT) → the §2
  misparse, i.e. the wire is the first wall (994's family). `count!=3` or `slide!=0` would falsify the
  cache-header model and must be reported, not reinterpreted.
* `map_slide ret=12` (ENOMEM) **with** `map_file kr=3` (KERN_NO_SPACE) → the wire was not the wall
  (§2's misparse did *not* happen, e.g. the gated 32 B branch was live) and §6's geometry is.
* `map_slide ret=14/1` → the file/executable side (`copyin` or a vnode check) — the line numbers in §4
  name the candidates; `map_slide ret=5` → `count > 8` or a slide-init failure.
* **Falsification:** no `IOS7SHREG` bytes in the image ⇒ the switch did not reach
  `out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o` (the build only **links** pool objects — the `.o` must have
  been recompiled); no `IOS7SHREG` lines in the log with the bytes present ⇒ dyld never reached
  `mapSharedCache` (cross-check the witness's `cacheBase` reading).

## 7b. 996's RESULT — PRESSED `armed-d13-cea0c1c2` (the readings this document predicted)

Capture: `out/stage90/captures/cancro-last_kmsg-996-armed-d13-cea0c1c2.txt` (sha256 `26ee7e84…`; the
probe's lines at `:4101-4105`):

```
IOS7SHREG check_np  pid=1 out=0x2fd0b9d0 / ret=12
IOS7SHREG map_slide pid=1 fd=0 count=4 mappings=0x2fd0b848 slide=0x3811000 slide_start=0x3f9bd000 size=0x00089000
IOS7SHREG map_file  kr=4      IOS7SHREG map_slide ret=22
```

* `check_np ret=12` — the region exists and is empty: §3's first-call prediction, exactly.
* `map_file kr=4` + `map_slide ret=22` — **§2 CONFIRMED on hardware**: KERN_INVALID_ARGUMENT folded to
  EINVAL(22) at `vm_unix.c:1418-1433` is `vm_map_enter_mem_object`'s `initial_size == 0`
  (`mapping[0].size` = the high word of dyld's 32-bit `0x2C000000`) — the 32-vs-20 B misparse, before
  any slide work. The witness adds `cacheBase(aii+0x64) = 0`: the cache was never mapped.

**The two DELTAS — registered, not reinterpreted** (this document's own rule, §7):
1. **`count=4`, not 3.** dyld sends four 32 B mapping records (their `va/size/offset/max/init` print on
   997); §2's model read a **three**-record `mappingInfo` out of the card image. The *mechanism*
   (kernel copies N×20 of N×32) is unchanged — with N=4 it copies 80 of 128 bytes, and m[0].size is
   still `hi32(0x2C000000)` = 0.
2. **`slide=0x3811000`, not 0**, with `slide_start=0x3f9bd000` (`0x643000` below `0x40000000`, i.e.
   inside any window that holds the cache) and `slide_size=0x89000`. The slide path is live for the
   first time; how it composes with the geometry is resolved in §6b.

**The full halt message** (witness rev-3; len=103 including the NUL — earlier captures' `?` was the
witness's `\n`→`?` rendering): `Library not loaded: /usr/lib/libbsm.0.dylib` /
`Referenced from: /sbin/launchd` / `Reason: image not found`. `target copyinstr=996748` (`0xF34CC`)
is the **fault-address echo**, not the string pointer — the witness's `.Lcopystrfault` path does not
set `r0`, so that companion word reads back the faulting address; it must not be used as a pointer.

## 8. Rungs this document maps, in order (design, not authorised here)

1. **996 = measure** (this probe + the witness rev-3). No behaviour change; decides between the two
   stories above. **DONE — §7b:** §2's story was the right one; the two DELTAS are registered.
2. **G3 (the 32 B wire) — SHIPPED as `STAGE90_XNU_SHREG_WIRE` (997).** The 32 B branch
   (`vm_unix.c:1174-1204` in this document's base file; `:1190-1219` with the 996 probe staged —
   audit §5c4) is opened by the 994 pattern: each of the three
   `#if IOS7_COMPAT_PLATFORM` lines gains
   `|| (defined(STAGE90_XNU_SHREG_WIRE) && STAGE90_XNU_SHREG_WIRE)` (994's spelling,
   `mach_kernelrpc.c:39`) and the file-local `ios7lab_cache_abi()` body returns 1 under the same
   switch; the **global** selector is untouched (17 call sites, 12 files). Tools:
   `tools/patch_d13_shreg_wire.py` (pre-image `516df232…` → staged `4b3ec2b8…`) and its sibling
   checker `tools/check_d13_shreg_wire.py`. Line numbers throughout this document are the pre-image;
   the staged file shifts regions below each gate (+3/+4). **997's pre-registration:** the four
   `IOS7LAB cache mapping …` lines always print on a clean parse — their `va` fields are §6b's
   discriminator. If geometry blocks: `map_file kr=3` (KERN_NO_SPACE) + `map_slide ret=12` (ENOMEM);
   if instead the branch's own bound (`address+size > 0x40000000` → EINVAL) trips: **no `map_file`
   line** + ret=22 — the addresses are slid and 32-bit VA cannot hold them; stop and re-model before
   G1, do not widen anything.
   **MEASURED (2026-10-12 22:05, the build now in `out/stage90`):** `xnu_arm_entry.elf` sha256
   `1bc1750c…` carries the ON shape — the literal @`0x8053bcbe` referenced only in
   `shared_region_copyin_mappings` @`0x8041fea4` → printf `0x8041ff1c`; the three 32 B shapes
   @`0x8041fe60`/`0x8041fe7c`/`0x8041ff28`; the ×20 pair absent; 294's copyout `r2=8` @`0x8041fe08`;
   the per-mapping bound guard @`0x8041fee8`. `tools/check_d13_shreg_wire.py --expect on --tree
   <…/bsd/vm/vm_unix.c>` = **PASS** on this elf (all three clauses + the source state: 3 gated `#if`
   lines, 1 helper body gate, the mapping printf on line 1218 inside a gate region). The ON rehearsal
   object `3fa5c230…` (27,964 B; OFF `3d660224…` 27,628 B) is byte-equal to the pool object
   `out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o` as buildplumb rebuilt it (22:04) — the build's own
   receipt.
3. **G1 (geometry + stack) after G3** — DRAFTED (tools exist); **998 = GO, §9** — constants frozen by
   the 997 `va` readings, switch name `STAGE90_XNU_SHREG_GEOM` carried into the 998 wiring
   (buildplumb's lane). With G3 alone
   mapping[0] no longer dies at `initial_size == 0` but dies region-relative at `target_address =
   sfm_address - sr_base_address` (§6). G1 opens the create override **already in the tree** at
   `vm_shared_region.c:626-639` (`base 0x2C000000`, `size 0x14000000`) **together with its stack half**
   — `status.c:62-73`'s `ios7lab_user_stack()` (`0x2BE00000`), a hard requirement of the same arm:
   otherwise the FIXED stack allocation at `trunc(user_stack − 1 MiB)` lands inside the new window and
   *every* process's stack is relocated (audit §2). **Draft-v2 shape** (switch name **proposed, not
   frozen**: `STAGE90_XNU_SHREG_GEOM`): four 1:1 line substitutions in the two `.c` files plus one header helper
   — `#if IOS7_COMPAT_PLATFORM` → the 994 gate spelling in both; `if (ios7_compat_vmabi_enabled())` →
   `if (ios7_shreg_geom_enabled())` at both call sites; `static inline int ios7_shreg_geom_enabled(void)`
   added beside `ios7_traps_enabled()` (`ios7_compat.h:41`), returning 1 under the switch, the global
   selector otherwise. Tools: `tools/patch_d13_shreg_geom.py` (pre-images `f91c3460…`/`d8f41a7b…`/
   `079c7a17…` → staged `482b4288…`/`da944439…`/`58cc2823…`) and `tools/check_d13_shreg_geom.py`.
   **OFF-identity, MEASURED (rehearsal 2026-10-12 22:09):** the `.c` line counts never change, the
   header's own lines are never a `.c`'s `__LINE__`, and the helper's OFF branch expands token for
   token to `ios7_compat_vmabi_enabled()` — a same-path pre-then-staged compile of **all 212 objects
   in the seven `ios7_compat.h` includer dirs** (osfmk/vm 21, osfmk/arm 19, osfmk/kern 58, osfmk/ipc
   17, bsd/vm 3, bsd/kern 84, bsd/dev/arm 10) is byte-identical, and a read-only live-tree compile
   reproduces the repo's own 997 `osfmk_vm_vm_shared_region.o` exactly (`99c25c5b…` both). The draft's
   **v1 inline `#if` wrap FAILED** that test: `debug.h:264-266`'s
   `PANIC_LOCATION` (`# ex "@" __FILE__ ":" __LINE__`) moved this file's three `panic` sites to
   `:1568`/`:1696`/`:1700` (from `:1564`/`:1692`/`:1696`) while `.text` stayed identical — hence
   1:1-only. **ON evidence:** `IOS7LAB shared region` string 0→1; `mov r5, #738197504 ; 0x2c000000`
   and `mov sl, #335544320 ; 0x14000000` in `vm_shared_region.o` (`ea0ba7f8…` vs OFF `0de06f8f…`);
   `movt r0, #12256 ; 0x2fe0` → `movt r0, #11232 ; 0x2be0` at all three `status.o` sites
   (`df0f2cd8…` vs OFF `28a3d47c…`); non-target objects unchanged ON vs OFF. The enter path it relies
   on is real: `vm_shared_region_enter`'s pre-nesting prefix branch (`:1272-1305`) maps
   `[base, nesting_start)` — the 64 MiB `0x2C000000..0x30000000` — un-nested before the nested loop;
   `NESTING_BASE_ARM 0x30000000` / `NESTING_SIZE_ARM 0x10000000` (`shared_region.h:69-70`) complete
   the `0x14000000` total. By-value audit of the whole arm: `docs/reference/d13-shreg-g1-audit.md`.
4. **G2 (the `VM_FLAGS_RETURN_DATA_ADDR 0x100000` sites) and the slide** last: with the cache mapped
   the slide path is exercised for the first time, and §5's uninitialized `mapping_to_slide` is the
   first thing to measure (the probe's `slide=`/`size=` words and, on failure, `slide_info
   initialization failed with kr=` on the console).

## 9. 997's RESULT — PRESSED `armed-d13-518b6a94`: G3 CONFIRMED on hardware; the block is G1's geometry

Capture: `out/stage90/captures/cancro-last_kmsg-997-armed-d13-518b6a94.txt` (sha256 `23bb574b…`;
22,757 lines; the probe's lines at `:4101-4109`; the witness at `:4120-4147`; runner exit 0 — the
payload returned and reset, the USB ladder never reached `machine_idle`, so the last_kmsg channel
carried the console, not the EP1 stream). Switch set = 996's verbatim + `STAGE90_XNU_SHREG_WIRE=1`.

```
IOS7SHREG check_np   pid=1 out=0x2fd2b9d0 / ret=12
IOS7SHREG map_slide  pid=1 fd=0 count=4 mappings=0x2fd2b848 slide=0xd9c000 slide_start=0x3cf48000 size=0x00089000
IOS7LAB cache mapping 0 va=2cd9c000 size=0c266000 offset=00000000 max=5  init=5
IOS7LAB cache mapping 1 va=39004000 size=01ce0000 offset=0c266000 max=23 init=23
IOS7LAB cache mapping 2 va=3ace4000 size=022ed000 offset=0df46000 max=1  init=1
IOS7LAB cache mapping 3 va=3cfd1000 size=00177000 offset=12bda000 max=1  init=1
IOS7SHREG map_file   kr=1
IOS7SHREG map_slide  ret=14
```

**The four `va` lines are the point: the 32 B wire PARSES.** §8 item 2's fallback "no `map_file`
line" did not happen — the branch read all four records correctly: m0 ends `39002000` (a measured
`0x2000` VA gap before m1's `39004000`), then `+01ce0000=3ace4000`, `+022ed000=3cfd1000`,
`+00177000=3d148000` are exact; file offsets run `0, 0c266000, 0df46000`, jump `0x29A7000` to
`12bda000`, the last ending `0x12d51000`. *(Both gaps measured 2026-10-12 by the shreg-g1 lane's
by-value pass and re-derived here; an earlier version of this paragraph over-claimed perfect
contiguity. Neither gap changes any conclusion — the guard is per-mapping.)* Zero-lines
falsification retired: the define reached the compiled object, and the checker's ON clauses hold on
the pressed elf.

**Failure signature: (a), the pre-registered ALTERNATIVE inner return — kr=1, not kr=3.** `map_file
kr=1` (KERN_INVALID_ADDRESS) folds at `vm_unix.c:1447-1463` (the `case KERN_INVALID_ADDRESS: error
= EFAULT;` at `:1448-1449`) to EFAULT(14) → `map_slide ret=14`. By
value, the firing site is **`vm_map.c:1846`** in `vm_map_enter`'s **FIXED** path:
`start = m[0].va − sr_base = 0x2cd9c000 − 0x30000000 = 0xFCD9C000`, `size = 0x0c266000`; with
`vm_map_offset_t` = `uint32_t` (`osfmk/mach/arm/vm_types.h:111-113`), `end = start + size` wraps to
`0x09002000` and the guard `(start < effective_min_offset) || (end > effective_max_offset) ||
(start >= end)` (`:1843-1845`) trips on `start >= end`. **Correction to §8 item 2's forecast:** the
primed `kr=3`/KERN_NO_SPACE at `vm_map.c:1797` is the ANYWHERE loop's range-fit; this call passes
`VM_FLAGS_FIXED | VM_FLAGS_ALREADY` (`vm_shared_region.c:1119-1130`), so the FIXED-path guard fires
first — with `VM_FLAGS_FIXED` the `:1797` ANYWHERE return was structurally unreachable, and the
folded errno is EFAULT, not ENOMEM. It is the *sole* stopper: the FIXED-path prologue passed
(`initial_size 0x0c266000 ≠ 0`, `:2344-2345`; named-entry `0 + 0x0c266000 < 0x10000000`,
`:2380-2381`). Same conclusion — geometry blocks — different line; recorded, not reinterpreted.

**What the `va` fields answer (the §6b discriminator — addresses ARE SLID).** m[0].va
`0x2cd9c000` = `0x2C000000` (unslid base) + THIS run's `slide=0xd9c000` **exactly**; 996's
`0x3811000` was a different per-run draw. *(This `slide` is the CACHE slide — the value `map_slide`
received; the PROCESS slide this run is a different draw: pc `0x2bed30c4` = `_dyld_fatal_error`
`0x2be010c4` + `0xD2000`. Do not conflate the two.)* So dyld passes **already-slid** addresses; the kernel's
only relative arithmetic is `sfm_address − sr_base_address` (`vm_shared_region.c:1088-1089`), and
the slide stage is fixup-only — §6b resolved by measurement. **G1's constants are now frozen per §8
item 3's rule:** all four slid spans lie inside `[0x2C000000, 0x40000000)` (m[0] starts
`0x2cd9c000`; m[3] ends `0x3d148000`), and under the override `target = va − 0x2C000000` gives
m[0] `0xd9c000`..`0xd002000` and m[3] ending `0x11148000` — every mapping fits the `0x14000000`
window. **VM_PROT_SLIDE (0x20) appears — on m[1]** (`init=max=0x23`), not the last mapping: the
`mapping_to_slide` is the `0x01ce0000` piece at `va=0x39004000`, answering §5's open question.
`slide_start=0x3cf48000` + `size=0x89000` = `0x3cfd1000` = **m[3].va exactly** — it is the exact
TAIL of m[2] in both spaces (VA `0x3ace4000+0x2264000`, file `0x0df46000+0x2264000 = 0x101aa000`,
ending `0x10233000 = m[2].offset+m[2].size`) — a dyldtrace datum, recorded here.

**Everything else held.** `check_np ret=12` (§3's first-call reading, constant). Regression guards:
`VMHINT991` 2× at exec (`:4096-4097`), `IOS7LAB vm_allocate64 flags=1 result=0` (`:4110`). The halt
is unchanged — same 102-char `libbsm` message, both witnesses `len=103`, `r5 matches errorMessage`,
`cacheBase=0` (vacuous per dyldtrace), `r8` interior `len=0` again — because 438 still dies, now at
the geometry wall. 232× `udf` retry, `No errors detected` tail.

**Rung order, updated:** §8 items 1–2 DONE (997 CONFIRMED G3 on hardware). Item 3 is the next
build: **998 = G1 under `STAGE90_XNU_SHREG_GEOM`**, the draft-v2 shape with constants as frozen
above plus the hard `status.c` stack half; with it `map_file` should return 0 for all four mappings
and the slide stage runs for the first time (then dyld's halt, if any, moves past the shared cache
— item 4's `mapping_to_slide` question is now expected to be exercised with m[1]).

---

## §10 — 998 press result: G1 CONFIRMED (`map_file kr=0`); the halt chain is GONE

**Press** 2026-10-12, arm `armed-d13-dee822b0`, runner exit 0 (the SoC reset below brought the
device back, so the "resident rung does not return" path never fired). Capture
`out/stage90/captures/cancro-last_kmsg-998-armed-d13-dee822b0.txt` (sha256
`0c558c533353c9abb3ebd6111773208253c47ab3acef993c12cdf7b09d8acec2`, 20,507 lines); press log
`out/stage90/captures/press-998-20261012-232731.log`. USB console capture: empty.

**Pre-registration verdict (the frozen block in `scripts/press_998.sh:39`; deviations reported, not
reinterpreted).**
- POINT 1 ✓ the geom printf appears EXACTLY once (`:4098`): `IOS7LAB shared region
  0x2c000000..0x40000000; stack below 0x2be00000` (the `vm_shared_region.c:635` printf, first
  reached at launchd's exec).
- POINT 2 ✓ the four `IOS7LAB cache mapping` lines stay (`:4105-4108`) and **`IOS7SHREG map_file
  kr=0` for the first time in this series** (`:4109`). `map_slide` runs the success form (`:4104`:
  `slide=0x1a86000 slide_start=0x3dc32000 size=0x00089000`); 997's trailing `map_slide ret=14` and
  the §8 item 4 hook (`slide_info initialization failed with kr=`) are absent.
- (d) **not a new failure name — NO halt at all**: every marker of the 997 chain is gone from the
  text block — `libbsm` 0 lines, `IOS7WITNESS` 0, `undefined instruction` 0, `dyld>` 0. The console
  text ENDS at `map_file kr=0`.
- Guards held: `check_np ret=12` (`:4103`), `VMHINT991` 2× (`:4096-4097`); the stack falsifier
  (`Process 1 exec failed`) is absent. **Reported deviation:** the pre-registration's
  `IOS7LAB vm_allocate64 flags=1 result=0` line is ABSENT — 997 places it (`:4110`) after the
  failure's retire and before the fatal instruction, i.e. it is a failure-path artifact (the
  message-buffer alloc), not a boot invariant. Recorded as a deviation; not reinterpreted.

**The two walls that bound this reading.**
1. **The live-record channel capped at 16384 in the dyld era.** `xnu_live_capped=0x00004000`
   (`:20502`); the exec pair is live-record ~15295 (`:19374-19377`). What stays visible
   (`:19378-20501`) is the common prefix: 8 `sleh` user-fault events (thread `0xc0bfa000`) + storage
   reads + the late slot/rootmedia cluster. **997's record tail is name-for-name and LBA-for-LBA
   identical (last `drv_lba=0x00422963`)** — the kr=1-vs-kr=0 divergence never reaches the record
   channel; it lives only in the text block.
2. **The SoC watchdog.** The payload arms it before the jump (25 s / 3 s); its only feed is the
   resident pet on the idle-exit path (`entry_trace.c:3030`, `entry_wdt_pet` — the same site as the
   USB ladder). **0 `xnu_live_wdt_*`, 0 `xnu_live_usb_*`, 0 `xnu_live_pcx` records ⇒ the handed-off
   kernel NEVER idled ⇒ never fed ⇒ the SoC reset it ~28 s after the jump** — silent, no panic.
   (908's kernel idled 60931 times; this boot's dyld path never reaches idle.) The runner's canned
   wdt line ("no `xnu_live_wdt` map ⇒ RESIDENT off or predates 909") misreads a RESIDENT=1 image:
   absence here means the pet never RAN.

**New instrument defect (measured).** The `xnu_live_exec_done_*` records are absent while the SAME
wrapper's printf printed (`:4100` `mini4: the OS's own init load returned ... (caller 0x80036100)`);
in the pinned source the record call (`entry_trace.c:3776`) precedes the printf (`:3784`), and the
pressed `entry_trace.c` sha `0cd3f105…` == the pinned `xnu_arm_entry-sources.txt` entry. ⇒ the load
DID return; the three done-records were lost in the channel (candidate: the silent `g_live_state !=
1` refusal during the exec window — `g_live_refusals` is only published in the pre-jump report). The
runner's "the load did not return / look for the panic" verdict is contradicted by the wrapper's own
console line.

**Rung order, updated.** §8 items 1–3 DONE (997 = G3, 998 = G1; the geometry no longer blocks).
Next rung's givens — **(a) revised by measurement (2026-10-13)`:** the record stream costs **46.9 B/line**
(16,426 lines / 770,699 B); console bound 0x200000−12 = 2,097,140 B, 998 used 1,072,335 ⇒ headroom
1,024,805. But the useful unit is reads, not records: **one storage read = 69 `storage_rd_*` records
(all 69 keys exactly 129×) + ~20 driver/other ≈ 89 lines ≈ 4.2 KB**, and 998's whole run logged only
129 reads (mount + launchd's first pages). A cap raise alone (16384→24576) buys just **+92 reads**.
The multiplier is **thinning the per-read dump after `rd_ordinal > 256`** (keep
`calls/ordinal/lba/op/err/complete` ≈ 280 B/read ⇒ cap 24576 reaches ≈ 2,300 reads). Recommendation to
buildplumb: cap 24576 **and** thin-after-256. (b) decide the watchdog policy — WDT_HZ 32765, armed
25 s bark + 3 s bite (payload `STAGE90_HW_WATCHDOG_TIMEOUT_S`=25); the pet fires only on idle, so a
boot that never idles ends at the SoC's ~28 s whatever else it does. (c) the exec_done fix — **two
candidates falsified 2026-10-13 by disassembling the frozen 998 elf** (`out/stage90/frozen/
armed-d13-dee822b0/xnu_arm_entry.elf`): the call executes (0x8049de98 `bl entry_note_exec_returned`,
unconditional, before the printf tail-call; key pointers correct, `.rodata` 0x4b3368/380/398), and
refusal-by-state is impossible (`g_live_state` is monotone — set 2 only at `entry_stubs.c:2091`, 1
only at `:2396`, `entry_live_init` runs only at 0 — and the 1,128 records after the done point,
starting with the `sleh` emitter at `:1740`, prove state 1). Cap is also impossible (done point =
ordinal 15,299; cap marker = 16,423). The loss is inside the console write path at that instant (or a
same-instant overwrite); 999 should probe it by reading back `*sig`/`*size_p` after the done call,
stashing in `.bss`, and publishing from the next known-good writer.

## §10c — the timer chain stops at tick 64 in EVERY run (measured 2026-10-13)

**(d) the timer dies early — measured across all six captures (987, 994-998).** `xnu_live_tick_irq`
(published on powers of two from `msm8974_handle_interrupt`, `pe_msm8974.c:369`) reaches **max 0x40 =
64 and never 128** in every capture, so fewer than 128 timer-PPI deliveries ever occur in a whole
boot. The whole family stops together at 998 `:5475` — `tmr_enter_seq` 0x40, `tmr_qexp_seq` 0xc,
`tmr_enter_over` 0x8, `setpop_count` 0x40 — and no timer record of any kind appears again through
exec (`:19374`) and 1,128 dyld-era records to the cap (`:20502`). The last healthy window is the
storage power spin `st_pwr_wait` (`src/entry/entry_storage.c:1920-2065`): premise
`_pwr_wait_cpsr=0x80000113` (I=0) at `:5416`; ticks 8/16/32/64 publish during it (`:5415-:5463`);
tail at `:5475-5483` = `_cpsr_after=0x20000193` (I=1), `_polls=0x67400`, `_ticks=0x1d5767` (100 ms
CNTV), `_timeout=1`. Two models remain open — (A) the PPI/countdown is killed at the spin tail; (B)
the machine returns to I=1 and never unmasks again (post-exec `sleh_cpsr` records all read
`0x20000193`, the same masked value). Either way, **nothing driven by `rtclock_intr` runs after ~4.8 s
of mach time**, which (i) rules out the tick ISR as a watchdog pet site (a pet there stops petting
after ≤64 deliveries — wdt-policy and buildplumb informed 2026-10-13; the pet belongs in the
storage-read ladder, 129 reads ran post-freeze, plus the idle site), and (ii) is a candidate
mechanism for this section's own frontier: after `map_file kr=0` the SECOND
`vm_shared_region_map_file` call never returned (`vm_unix.c:1425`-`:1438` — the print fires only on
return), so a deadline-based park inside call #2 would now be unresolvable. Two forensics agents
(timer-freeze, shreg-hang) are discriminating the models and locating call #2's first blocking
point; the 999 probe set gains a model-discriminator observable once they report.