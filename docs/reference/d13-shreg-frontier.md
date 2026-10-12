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
`external/xnu-hd2-darwin13/xnu/` (the **build tree**, sha256 `c9b845a9…` = `bsd/vm/vm_unix.c`,
`f91c3460…` = `osfmk/vm/vm_shared_region.c`).

## 1. The two syscalls as wired here

| | 294 `shared_region_check_np` | 438 `shared_region_map_and_slide_np` |
|---|---|---|
| master | `bsd/kern/syscalls.master:452` `{ int shared_region_check_np(uint64_t *start_address) NO_SYSCALL_STUB; }` | `:671` `{ int shared_region_map_and_slide_np(int fd, uint32_t count, const struct shared_file_mapping_np *mappings, uint32_t slide, uint64_t* slide_start, uint32_t slide_size) NO_SYSCALL_STUB; }` |
| generated row | `out/xnu_generated_d13/bsd/init_sysent.c:440` `munge_w, munge_d, _SYSCALL_RET_INT_T, 4` | `:649` `munge_wwwwww, munge_dddddd, _SYSCALL_RET_INT_T, 24` |
| image row (`sysent` @ `0x804e5f84`, stride 24, read at `sysent+no*24`) | `narg=1 call=0x8041fd84 arg_bytes=4` | `narg=6 call=0x804201c0 arg_bytes=24` |
| body | `shared_region_check_np @0x8041fd84` (`vm_unix.c:1105`) | `shared_region_map_and_slide_np @0x804201c0` (`vm_unix.c:1528`) |
| helpers | — | `_shared_region_map @0x8041fe10` (`:1231`) · `_shared_region_slide @0x80420120` (`:1485`) · `vm_shared_region_map_file @0x80113cf4` (`vm_shared_region.c:969`) |

`struct sysent` = `{int16 narg; int8; int8; ptr call; ptr munge32; ptr munge64; int32 ret; uint16
arg_bytes}` (`bsd/sys/sysent.h:42-53`) = 22 B, aligned to **24**. The two rows above are the "the
dispatch is the claim" check (`mi4-a-claim-in-a-comment-is-not-a-check`) the 996 checker re-runs.

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

By value in the **linked image**: `shared_region_copyin_mappings @0x8041fdf0` is

```
8041fdf4: add r1, r2, r2, lsl #2     ; count * 5
8041fdf8: lsl r2, r1, #2             ; count * 20        <- sizeof(struct shared_file_mapping_np) == 20
8041fdfc: mov r1, r3 ; b copyin
```

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

copyout width is `sizeof(mach_vm_offset_t)` = **4**; the image proves it (`shared_region_check_np`
`0x8041fdc8: mov r2,#4; bl copyout`). dyld allocated 8 bytes for it and zeroed both words
(`0x2be04fd4-0x2be04fd8`), then reads **only the low word** (`ldr.w r4,[r4,#156]`, `0x2be05020`), so
both the 4-byte and the platform-gated 8-byte copyout stay inside dyld's buffer — no defect at 294.

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
outside the submap's `[0, 0x10000000)` — with `VM_FLAGS_FIXED|VM_FLAGS_ALREADY` that is a mount of
KERN_NO_SPACE → **ENOMEM 12**, and mapping[2]'s end would not fit either. So even with the wire fixed,
**G1 must open** (base `0x2C000000`, size `0x14000000`, the `IOS7_COMPAT_PLATFORM` create override) for
the cache to land where dyld expects it.

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

## 8. Rungs this document maps, in order (design, not authorised here)

1. **996 = measure** (this probe + the witness rev-3). No behaviour change; decides between the two
   stories above.
2. **G3 (the 32 B wire) first, only if 996 shows the §2 misparse**: the branch already exists at
   `vm_unix.c:1174-1204` but is keyed on `ios7lab_cache_abi()`; opening it for CANCRO is a one-line
   gate decision (`ios7lab_cache_abi` → true under a `STAGE90_XNU_*` switch), or a board-neutral
   32 B-wire adapter mirroring 994's traps — same shape, same risk, and the checker must show the
   `IOS7LAB cache mapping` lines appearing.
3. **G1 (geometry) after G3**, because with G3 alone mapping[0] no longer dies at `initial_size == 0`
   but dies at `0xFC000000` (§6). G1 is the `IOS7_COMPAT_PLATFORM` create override, which is also what
   makes `vm_shared_region_enter`'s pre-nesting prefix branch (`vm_shared_region.c:1272-1305`, `0x30000000
   - 0x2C000000` = 64 MiB mapped un-nested) necessary.
4. **G2 (the `VM_FLAGS_RETURN_DATA_ADDR 0x100000` sites) and the slide** last: with the cache mapped
   the slide path is exercised for the first time, and §5's uninitialized `mapping_to_slide` is the
   first thing to measure (the probe's `slide=`/`size=` words and, on failure, `slide_info
   initialization failed with kr=` on the console).