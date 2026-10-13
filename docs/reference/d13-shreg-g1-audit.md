# D13 G1 — the iOS 7 shared-region geometry override (`0x2C000000` / `0x14000000`): by-value audit

**Why this document exists.** 996 measured that dyld's `shared_region_map_and_slide_np` (438) dies on the
20 B-vs-32 B cache-ABI wire (`map_file kr=4`, `map_slide ret=22`), so the shared-cache mapping never
happens and dyld halts on `image not found`. Doc `d13-shreg-frontier.md` §8 orders the repair **G3 (the
32 B wire) first, then G1 (the region geometry)**. This is the G1 audit: what the override is gated on,
what it will execute, what it breaks beyond dyld, and how an OFF build is proven byte-identical. It is
self-contained so `d13-shreg-frontier.md` can cite it; it was produced read-only (no tree edit) on
2026-10-12 and is the persistent copy of the audit message.

**Convention.** *MEASURED* = read by value this session (grep / `nm` / `arm-none-eabi-objdump` / a
`python3 -I` byte reader / `sha256sum`) and quoted. *INFERRED* = a code-path argument from those values
(no runtime observation exists yet, because G1 is not built). Every claim carries `file:line` or an image
address.

**Identity of the audited artifacts (MEASURED, 2026-10-12).**

| artifact | value |
|---|---|
| `out/stage90/xnu_arm_entry.elf` | sha256 `2af26d0a80d92cf86d3f8ac0a03f1ddb1a7a5feeff006433cbb249c9ba73d76a`, 7,380,384 B, mtime 21:11 — the 996 build (5 `IOS7SHREG` strings, 22 `IOS7WITNESS` strings, `IOS7LAB shared region` count **0**) |
| `osfmk/vm/vm_shared_region.c` | sha256 `f91c34608cf701c973e7fd9dfa59c7f91e1f1c6917598bc1339c5737e72e4605` (matches the frontier doc — all its citations to this file are still exact) |
| `osfmk/arm/status.c` | sha256 `d8f41a7b471dfde5b5b09cee64565ebcd8583540d0af0b4631e55467877c5524` |
| `osfmk/arm/pmap.c` | sha256 `89693d7f7fa99abca4d155d474dbcfe647f9631ffa13e6f1ea102eba521b6b3d` |
| `bsd/vm/vm_unix.c` | sha256 `516df2327cd113c3d916ef0346ef3d31dd5310f194d0c53319bf8fd9839c14f6` (996 probe staged; frontier doc's `c9b845a9…` is stale — see §5) |
| git | no G1/G3 staging; only `docs/reference/d13-dyld-*.md` modified + `tools/patch_d13_shreg_wire.py` untracked |

> **NOTE (2026-10-12, staged after this audit):** the tree now carries the G3-staged
> `bsd/vm/vm_unix.c` sha256 `4b3ec2b83aaf9e6ae3d22264085c100a57cf562de7a2b1b30ef967c3d42ed0b6`
> (997 build in flight). **Every `vm_unix.c` line number below is against the `516df232…`
> 996-probe pre-image** and shifts **uniformly by +4** in the staged file (the gate is
> `#if IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_SHREG_WIRE) && STAGE90_XNU_SHREG_WIRE)`;
> only the helper body added lines, staged `:105-109`): check_np `:1105→:1109`,
> copyin_mappings `:1181→:1185`, `_shared_region_map` `:1247→:1251`, `_shared_region_slide`
> `:1510→:1514`, 438 `:1553→:1557`; §7's branch `:1190-1219 → :1194-1223`, its second site
> `:1141-1146 → :1145-1150`.

---

## 1. Gate inventory and the exact minimal gate-line set

**MEASURED.** `grep -n 'IOS7\|ios7' osfmk/vm/vm_shared_region.c` returns exactly four lines: `:101`
(`#include <arm/ios7_compat.h>`), `:626`, `:629`, `:635`. The one gate block, in
`vm_shared_region_create`'s `case CPU_TYPE_ARM:` (:620-640):

```
622        base_address = SHARED_REGION_BASE_ARM;            /* 0x30000000 */
623        size = SHARED_REGION_SIZE_ARM;                    /* 0x10000000 */
624        pmap_nesting_start = SHARED_REGION_NESTING_BASE_ARM;  /* 0x30000000 */
625        pmap_nesting_size = SHARED_REGION_NESTING_SIZE_ARM;   /* 0x10000000 */
626  #if IOS7_COMPAT_PLATFORM
629      if (ios7_compat_vmabi_enabled()) {
633          base_address = 0x2C000000U;
634          size = 0x14000000U;
635          printf("IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000\n");
637  #endif
```

There is no `#else`. Both selectors are 0 on CANCRO: `IOS7_COMPAT_PLATFORM` =
`(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)` (`osfmk/arm/ios7_compat.h:5`; the build defines
only `BOARD_CONFIG_MSM8974_CANCRO`, `tools/build_xnu_arm_kernel.sh:202`), and
`ios7_compat_vmabi_enabled()` returns 0 in the `#else` arm (`ios7_compat.h:18-19`).

**Minimal open set — exactly two lines in this file:**

* `:626` → `#if IOS7_COMPAT_PLATFORM || (defined(<SW>) && <SW>)` (suffix spelling per the 992/993/994
  pattern; `defined()` first — `mi4-off-option-two-spellings`).
* `:629` → the runtime selector must also become true, via a helper added beside
  `ios7_traps_enabled()` (`ios7_compat.h:33-40`): `#if defined(<SW>) && <SW>` → 1, `#else` →
  `ios7_compat_vmabi_enabled()`. Changing only `:626` is **not** sufficient: the inner `if` would still
  return 0.

*The switch identifier `<SW>` is deliberately a placeholder: the key name (`SHREG_IOS7` vs `SHREG_G1` or
other) is not frozen — it is decided in the G1 draft (team-lead ruling 2026-10-12).*

**Nothing else on the create path needs opening (MEASURED).** The override writes only the four locals
consumed later: `vm_map_create(…, 0, size, TRUE)` `:665-667`, `mem_entry->size = size` `:681`,
`sr_base_address`/`sr_size` `:688-689`. The nesting pair stays `0x30000000`/`0x10000000`
(`:624-625` → `:690-691`) and **must not move** — the pre-nesting prefix branch depends on the
difference (§2). `osfmk/vm/vm_map.c`'s only IOS7 sites are `:11440`, `:11460`, `:11462`, `:11539`, all
inside `vm_map_remap` (`:11422`) = **G2**; the create/enter path (`vm_map_exec` `vm_map.c:8639-8647`) has
none and needs no edit.

**Companion file — hard requirement, same switch (MEASURED + INFERRED).**
`osfmk/arm/status.c:62` `#if IOS7_COMPAT_PLATFORM` + `:66`
`if (ios7_compat_vmabi_enabled()) return 0x2BE00000U;` is the *stack half* of the same ABI, behind the
same closed selector (`LAB_USER_STACK` used at `:90` `thread_userstack` and `:164`
`thread_userstackdefault`). It must be opened by the **same** switch, because otherwise the geometry
change breaks the stack for every process (§2 blast radius). Team-lead accepted this as a G1 hard
requirement on 2026-10-12.

---

## 2. Execution audit with G1 ON (`base 0x2C000000`, `size 0x14000000`)

All arithmetic below is MEASURED from the source values; the *outcome* (that a real 996-class press
reaches it) is INFERRED, and the console cannot contradict anything here (see the `kprintf` caveat).

**Enter parameters** (`vm_shared_region_enter` `:1221-1390`, read at `:1261-1265`): `sr_address =
0x2C000000`, `sr_size = 0x14000000`, `sr_handle` = the submap's named entry, `sr_pmap_nesting_start =
0x30000000`, `sr_pmap_nesting_size = 0x10000000`.

**Prefix branch `:1272-1305` (the doc's "pre-nesting prefix").** Condition
`sr_pmap_nesting_start > sr_address` = `0x30000000 > 0x2C000000` = **TRUE** under G1. Today it is
`0x30000000 > 0x30000000` = FALSE — a pure-constant comparison, so **this branch has never executed on
CANCRO**. It is *not* gated by any `#if`: it is driven entirely by the geometry values. Under G1 it
performs ONE `vm_map_enter_mem_object(map, &target_address = 0x2C000000, mapping_size = 0x4000000, …,
VM_FLAGS_FIXED, sr_handle, sr_offset = 0, …)` (`:1276-1287`) with **no `VM_MEMORY_SHARED_PMAP` tag** —
i.e. a real, un-nested mapping of the submap's first 64 MiB — then advances `sr_offset = 0x4000000`,
`sr_size = 0x10000000` (`:1303-1304`).

**Nested loop `:1311-1348`.** Iteration 1: `target = 0x30000000`, `mapping_size =
min(0x10000000, pmap_nesting_size_max) = 0x8000000`, tag `VM_FLAGS_FIXED |
VM_MAKE_TAG(VM_MEMORY_SHARED_PMAP)` (`:1326`) ⇒ the nested path. Iteration 2: `target = 0x38000000`,
size `0x8000000`. `sr_pmap_nesting_size` then reaches 0 and the loop exits with `sr_offset =
0x14000000`, `sr_size = 0`. **Tail `:1349-1382` is skipped** (`sr_size == 0`), and `assert(sr_size == 0)`
`:1383` holds **exactly** — no panic. Net: region = 64 MiB un-nested + 2 × 128 MiB nested
(128 L1 TTEs per chunk). `pmap_nesting_size_max` = `0x8000000` MEASURED in the image (symbol
`0x805f33a0` = `0x8000000`; source `osfmk/arm/pmap.c:209-210`).

**pmap primitives are real, and their panics cannot fire here (MEASURED).** `pmap_nest`
`pmap.c:4340-4437`, `pmap_unnest` `:4445-4485`, `pmap_expand` `:3113-3170`, `pmap_expand_ttb`
`:3050-3110`; the panic preconditions (`:4353` size==0, `:4357` `va_start != nstart`, `:4408`/`:4418`)
cannot be met at this call site (size `0x8000000` ≠ 0, `va_start == nstart == target`, 1 MiB-aligned).
**Caveat (MEASURED):** `pmap.c:409` `#define kprintf(args...)` is an unconditional no-op, so
`pmap_nest`'s entry print cannot appear in *any* capture — console regression can never testify about
whether nesting happened; only a probe in `vm_shared_region_enter` (an `IOS7SHREG`-style line) can.

**Blast radius — G1 is not a dyld-local change (MEASURED ordering + INFERRED consequence).**
`vm_map_exec` calls `vm_shared_region_enter` for **every** exec — `vm_map.c:8642`, return value
discarded `(void)`, called from `bsd/kern/kern_exec.c:916-919` with `fsroot = p->p_fd->fd_rdir`
(unconditionally, before any `vfexec`/spawn branch). So with G1 ON, `[0x2C000000, 0x40000000)` is
reserved in every new task map at exec time (empty submap ⇒ VA reservation, no pages). The measured exec
order is `load_machfile` `kern_exec.c:881` → `vm_map_exec` `:916` → `create_unix_stack` `:940`, and
`create_unix_stack` FIXED-allocates the stack at `trunc(user_stack − size)` (`:3622-3625`) with
`size = mach_vm_round_page(user_stack_size)`, `user_stack_size = MAXSSIZ` = 1 MiB
(`bsd/arm/vmparam.h:21`; set at `bsd/kern/mach_loader.c:1086` / `:1155`). Therefore:

* **status.c OFF + G1 ON:** `user_stack = 0x2FE00000` ⇒ FIXED target `0x2FDF0000`, *inside* the new
  window ⇒ `mach_vm_allocate` returns `KERN_NO_SPACE` ⇒ the ANYWHERE fallback `:3626-3641` relocates
  the stack (and rewrites `p->user_stack` `:3635-3640`) for **every** process — not just dyld.
* **status.c ON + G1 ON:** `user_stack = 0x2BE00000` ⇒ FIXED target `0x2BD00000`, below the window,
  clean.

This is why the status.c line is a hard requirement of the same arm, not a separate rung.

---

## 3. Interaction with the misparsed 20 B wire (G1 alone changes nothing for 438)

**MEASURED (996):** `IOS7SHREG map_slide pid=1 fd=0 count=4 mappings=0x2fd0b848 slide=0x3811000
slide_start=0x3f9bd000 size=0x00089000` / `IOS7SHREG map_file kr=4` / `IOS7SHREG map_slide ret=22`.

**The chain, by value:** `shared_region_copyin_mappings` computes `count * 20` (image `0x8041fe44-48`:
`add r1, r2, r2, lsl #2` = count×5, `lsl r2, r1, #2` = count×20) because
`mach_vm_address_t/_size_t/_offset_t` are `uint32_t` here (`osfmk/mach/arm/vm_types.h:107-113`), so
`m[0].sfm_size` reads the **high word of the 64-bit address** = 0 ⇒ `vm_shared_region_map_file` passes
`vm_map_round_page(0) = 0` as `initial_size` ⇒ `KERN_INVALID_ARGUMENT` at `vm_map.c:2344-2345`
(condition `initial_size == 0` at `:2344`, return at `:2345`) — *before* `target_address` is used
(`:2347`) ⇒ `_shared_region_map` folds it to `EINVAL` at `vm_unix.c:1453-1457` ⇒ 438 returns early at
`:1623-1628`. **G1's only reachable write into this path is `target_address = sfm_address −
sr_base_address` (`vm_shared_region.c:1088-1089`), and that line is never reached** ⇒ with G1 ON and
the wire still misparsed, 438 returns the *same* EINVAL 22 (INFERRED — code-path argument; G1 not
built). The order G3-then-G1 in doc §8 is therefore correct: G1 alone cannot move this needle.

**Contrast (wire fixed, G1 off), for the record:** `m[0]`'s relative target `0x2C000000 − 0x30000000 =
0xFC000000` cannot fit the submap `[0, 0x10000000)` ⇒ `KERN_NO_SPACE` (the range-fit `RETURN` at
`vm_map.c:1797`) ⇒ `ENOMEM` (`vm_unix.c:1450-1452`). `m[2]` would also fail, but at a *different* line:
the named-entry size check `vm_map.c:2380-2381` (`0x0DF46000 + 0x022ED000 = 0x10233000 >
mem_entry->size = 0x10000000`, size set at `vm_shared_region.c:681`). Under G1 (entry size
`0x14000000`) both fit.

---

## 4. Off-identity and the checker keys

**Property (MEASURED by construction + INFERRED for the image).** With `<SW> = 0`:
`#if IOS7_COMPAT_PLATFORM || (defined(<SW>) && <SW>)` is false exactly as today, the helper's OFF branch
returns exactly `ios7_compat_vmabi_enabled()`, and `status.c` falls through to `USRSTACK`. The compiler
sees the same constants ⇒ same code ⇒ **byte-identical image**.

* **Proof 1** (the established method): build with `<SW> = 0` and require `sha256` equality with the
  pre-patch build.
* **Proof 2** (checker keys, MEASURED today on the OFF image):
  * `strings out/stage90/xnu_arm_entry.elf | grep -c 'IOS7LAB shared region'` = **0** OFF / **1** ON.
  * ON literal, byte-exact from `:635`:
    `IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000` + `\n` (68 chars +
    newline; one `.rodata` copy).
  * ON immediates: `0x2C000000` and `0x14000000` in `vm_shared_region_create`; with status.c open,
    `0x2BE00000` where OFF has `0x2FE00000` (OFF MEASURED: `thread_userstackdefault` `0x8008fac4` is
    `movw r0,#0; movt r0,#0x2fe0`).

---

## 5. Staleness list (doc `d13-shreg-frontier.md`) and claims the code does not support

All readings MEASURED on elf sha `2af26d0a…` (post-996 relink).

**5a. Image addresses that moved (doc → current).** `sysent` `0x804e5f84` → **`0x804e6f84`** (+0x1000;
rows now `0x804e8b14` / `0x804e9894` — row **values** re-read unchanged: 294 `narg=1
call=0x8041fd84 arg_bytes=4`; 438 `narg=6 call=0x80420240 arg_bytes=24`) · 438 body `0x804201c0` →
**`0x80420240`** (+0x80) · `_shared_region_map` `0x8041fe10` → **`0x8041fe60`** · `_shared_region_slide`
`0x80420120` → **`0x804201a0`** · `shared_region_copyin_mappings` `0x8041fdf0` → **`0x8041fe40`**
(+0x50; the three instructions now `0x8041fe44` / `0x8041fe48` / `0x8041fe50`) · check_np base
`0x8041fd84` **unchanged**, but its copyout moved: doc's `0x8041fdc8: mov r2,#4; bl copyout` is now
`0x8041fdf8: mov r2,#4` / `0x8041fdfc: bl copyout` (+0x30). Unchanged: `vm_shared_region_map_file`
`0x80113cf4` (and `vm_shared_region_enter` = `0x80113fb8`).

**What this invalidates:** §1's `sysent`/body/helper image addresses and §2/§3's helper/copyout
addresses — a checker keying the old addresses now reads the wrong bytes. **What it does not
invalidate:** every row value, the ×20 arithmetic, the 4-byte copyout width, and every
`vm_shared_region.c` citation (that file's hash still matches the doc).

**5b. Source-line drift in `vm_unix.c`** (doc hash `c9b845a9…` → now `516df232…`, probe staged):
`shared_region_check_np` def `:1105` unchanged; `_shared_region_map` def `:1231` → **`:1247`** (+16);
`_shared_region_slide` def `:1485` → **`:1510`** (+25); 438 def `:1528` → **`:1553`** (+25). So §3's
internal exits are shifted; §4's 438 table lines are **+25** (chroot now `:1581-1584`, sliding_valid
`:1586-1596`, `count==0` `:1598-1604`, `count>8` `:1605-1615`, copyin `:1617`, call `:1622`, early
return `:1623-1628`); §4's `_shared_region_map` internals are **+16** (the no-region defect branch
doc `:1394-1402` → now `:1410-1418`; the kr→errno fold doc `:1418-1433` → now `:1443-1459`); §5's
`:1533` → `:1558`, `:1485-1525` → `:1510-…`.

**5c. Doc claims the code does not support.**

1. **§6 "mapping[2]'s end would not fit either" is arithmetically wrong.** From the doc's own measured
   table: `(0x39f48000 + 0x022ed000) − 0x30000000 = 0xC236000 < 0x10000000`, so m[2] *does* fit the
   submap on the current geometry. Its real second failure is the named-entry check
   (`vm_map.c:2380-2381`, §3 above). The conclusion "G1 must open" stands.
2. **§2's "3 mappings" model was falsified by 996** (`count=4`, `slide=0x3811000 ≠ 0`) — the doc
   pre-registered both as falsifiers and instructed "report, not reinterpret". The `m[0].size == 0`
   row and the ×20 arithmetic are count-independent and survive (`m[0].sfm_size` is the high word of
   the 64-bit address field).
3. **The doc header's `vm_unix.c` hash `c9b845a9…` is stale** (`516df232…`); `vm_shared_region.c`'s
   `f91c3460…` still matches.
4. **§8's "`vm_unix.c:1174-1204`" for the 32 B branch is stale** → now `:1190-1219`; selector def
   `:101-107`; second call site (294's 8-byte copyout) `:1141-1146`.

---

## 6. Do the 996 deltas change any G1 constant? No (team-lead question)

G1's constants are the ABI window (`0x2C000000`/`0x14000000`, `:633-634`) plus the untouched nesting pair
(`0x30000000`/`0x10000000`, `:624-625`). Nothing in `vm_shared_region_create` or
`vm_shared_region_map_file` derives either from syscall arguments (MEASURED: no other IOS7 site in the
file; none on this path in `vm_map.c`). Positively: the measured `slide_start = 0x3f9bd000` and
`slide_start + slide_size = 0x3FA46000` both lie inside `[0x2C000000, 0x40000000)` (0x5BA000 below the
top), whereas the default `0x10000000` window (top `0x3C000000`) would exclude them — so the deltas
**corroborate** `size = 0x14000000` rather than change it. `count=4` concerns G3 (the 32 B branch
accepts ≤ 8). `slide ≠ 0` arms doc §5's `mapping_to_slide` hazard: once wire + geometry are right, 438
proceeds to `_shared_region_slide`, and `mapping_to_slide` is only filled if one of the four mappings
carries `init_prot & VM_PROT_SLIDE (0x20)` (`vm_shared_region.c:1061-1085`); G3's per-mapping print is
what measures it.

---

## 7. For 997 = G3 (measured, ready) — and the boundary concern, pre-registered

The gated 32 B branch is **`vm_unix.c:1190-1219`** (doc's `:1174-1204` stale), selector definition
`:101-107` (`ios7lab_cache_abi()` → `ios7_compat_vmabi_enabled()`), entered from **two** sites:
`shared_region_check_np`'s 8-byte copyout `:1141-1146` and `shared_region_copyin_mappings` `:1190`.
Guards: `mappings_count == 0 || > 8` → EINVAL `:1198-1200`; per-mapping
`address >> 32 || size >> 32 || offset >> 32 || address + size > 0x40000000` → EINVAL `:1205-1208`;
per-mapping `printf("IOS7LAB cache mapping %u va=%08x size=%08x offset=%08x max=%x init=%x\n")`
`:1214-1216`.

**Boundary concern (recorded per team-lead, 2026-10-12):** the `:1207` guard hard-codes the G1 window
top `0x40000000`. If dyld's 4th mapping (a 996 delta vs the 3-mapping model) extends past it, G3 would
return EINVAL 22 — indistinguishable at 438's return value from the current wire failure. **997's
pre-registered signature (b) covers this:** *no* `IOS7LAB cache mapping` lines + `ret=22` ⇒ the boundary
guard fired; stop and remodel rather than press on (no extra action owed by this audit).

---

## 8. Post-997 addendum (2026-10-12, after press `armed-d13-518b6a94`)

**Baseline.** Sections 1–7 above were frozen as sha256
`d8339a247dc8f57b6ced6c37760d8856a8821c7bcba906d19f63ff692205f726`; this addendum is the single
post-freeze edit and retires nothing by silence — where a paragraph is superseded it is named.
Evidence: capture `out/stage90/captures/cancro-last_kmsg-997-armed-d13-518b6a94.txt`, sha256
`23bb574ba7795bd4205f76af83d2e4b0622ba8978db64d8f55a7ea07060215f1`, 22,757 lines. All readings below
MEASURED (re-derived by value from the capture bytes and the source).

**§3's contrast paragraph (`:165-170`) is RETIRED; the rest of §3 stands.** With the wire fixed (G3 ON)
and G1 off, 438 dies at the geometry — but by a different mechanism than the paragraph predicts.
Measured (`:4108-4109`): `IOS7SHREG map_file kr=1`, `map_slide ret=14`. By value: m[0]'s relative
target `start = 0x2cd9c000 − 0x30000000 = 0xFCD9C000`, `size = 0x0c266000`; `vm_map_offset_t` =
`uint32_t` (`osfmk/mach/arm/vm_types.h:111`) ⇒ `end = start + size` **wraps 32-bit** to `0x09002000` ⇒
the FIXED-path guard's `start >= end` clause (`vm_map.c:1843-1845`) fires ⇒
`RETURN(KERN_INVALID_ADDRESS)` at **`vm_map.c:1846`** ⇒ folded to **errno 14 (EFAULT)** at
`vm_unix.c:1448-1449` (G3-staged numbering; the fold block `:1447-1463`). **Retired with the
paragraph:** 996's primed `kr=3`/`KERN_NO_SPACE` at `vm_map.c:1797` and `ret=12` (ENOMEM) — `:1797` is
the ANYWHERE loop's range-fit, and this call passes `VM_FLAGS_FIXED | VM_FLAGS_ALREADY`
(`vm_shared_region.c:1124`; call `:1119-1130`), so with `VM_FLAGS_FIXED` it is **structurally
unreachable**; the paragraph's m[2]-fails-at-`:2380-2381` first-failure ordering is retired the same
way (m[0]'s wrap fires first; under G1 m[2]'s end `0x10233000` fits `0x14000000`, as `:169-170`
already noted). The paragraph's conclusion — geometry blocks 438, G1 must open — stands, now measured.

**§7's boundary concern is resolved: signature (b) did NOT fire.** All four `IOS7LAB cache mapping`
lines printed (`:4104-4107`); §7's `address + size > 0x40000000` guard (probe `:1205-1208`, staged
`:1209-1212`) passed for every record (max end `0x3d148000`). No remodel owed. Under G1 every
relative end ≤ `0x11148000 < 0x14000000` — constants frozen at the tree's own base `0x2C000000` /
size `0x14000000`, with `status.c`'s `0x2BE00000` stack half a hard same-switch requirement (§2).

**The contiguity over-claim dies here too (measured joints).** VA: m0 ends `0x39002000`, m1 starts
`0x39004000` — **gap `0x2000`**; the m1→m2 and m2→m3 joints are exact; last end `0x3d148000`. File
offsets `0, 0x0c266000, 0x0df46000` join exactly, then **gap `0x29A7000`** to `0x12bda000`; last ends
`0x12d51000`. (VA span `0x2cd9c000..0x3d148000`; file span 301.32 MiB.) Nothing downstream keys on
contiguity — the guard is per-mapping.

**§6 closed by measurement.** `VM_PROT_SLIDE (0x20)` appears only on **m[1]** (`init=max=0x23`) ⇒
`mapping_to_slide` = m[1]. The slide window `[0x3cf48000, +0x89000 = 0x3cfd1000)` is **m[2]'s exact
tail in both spaces** (VA `0x3ace4000+0x2264000`; file `0x0df46000+0x2264000 = 0x101aa000`, ending
`0x10233000 = m[2].offset+m[2].size`). The measured `slide=0xd9c000` is the **cache** slide (the value
`map_slide` received); the process slide this run is a different draw (`0xD2000`: pc `0x2bed30c4` =
`_dyld_fatal_error 0x2be010c4` + `0xD2000`).

**Gate lines verified against the staged tree (998 pre-flight, 2026-10-12, by value).** The live tree
implements §1's minimal open set exactly:

* Staged shas: `vm_shared_region.c` `482b428857f382b939bf44204c6a4fba5c6bed9876f74cfff850f3777e27b2d8`,
  `status.c` `da944439427d7258528fd790d5e261c4104f4e95eeef676deee71090d6e0aa72`,
  `ios7_compat.h` `58cc282364efec7ec7b9937fcdd7da7ecee6bfb8514c79c2c5882ac734630c6f`.
* The nested tree repo (`external/xnu-hd2-darwin13/.git`, HEAD `77fa165…`) commits bytes for both .c
  files identical to this audit's Identity-table pre-images (`f91c3460…` / `d8f41a7b…` — blob-hash
  equality), so `git diff` there is the complete staging delta: exactly the two 1:1 substitutions per
  file, **net-zero line counts** (`#if` widened at `:626`/`:62`; selector swapped to
  `ios7_shreg_geom_enabled()` at `:629`/`:66`). All this audit's `vm_shared_region.c` citations
  therefore remain exact in the staged tree.
* `ios7_compat.h` adds the helper at `:46-53` (block `:41-53`) between the 994-era
  `ios7_traps_enabled()` and `#endif`; its OFF arm returns exactly `ios7_compat_vmabi_enabled()` —
  §4's property, source-side. Tree-wide, the switch identifier occurs only at the header `:48` and the
  two `#if` sites (`:62`, `:626`); the helper name only at the header `:46` and the two call sites
  (`:66`, `:629`).
* Transcription correction in §1's abridged block (doc line `:55`): the closing `#endif` is at
  **`:638`** in both the pre-image and the staged tree, not `:637`. The load-bearing citations
  (`:626`, `:629`, `:633-635`) are exact.
* §4's empirical proof (the OFF rebuild-cmp, 212/212 objects, under 998's own switch set) is now
  **MEASURED — see §9** (`tools/stage_d13_shreg_geom.sh` landed 2026-10-12; the checker returns green
  on this tree, `--expect staged`).

**Operational note (repository state).** In the nested tree repo the whole multi-rung staging — 22
worktree-modified paths — is uncommitted against HEAD `77fa165…`; a `checkout`/`clean` there would
silently un-stage G3/994/G1 and everything else (recoverable by re-running the staging tools +
checkers). Recorded as a hazard, not a code fact.

## 9. Post-998 OFF-identity pass (2026-10-12) — §4's empirical proof, MEASURED

**Relation to §8.** §8 (frozen at `2265e40e7860cb79e377c3d75a2698e30f21158a54ce8cc5d1c6f389b28ba4a7`)
left §4's empirical proof OWED; this addendum measures it and updates that one bullet in place.

**Method (by value, scratch-isolated).** With `tools/stage_d13_shreg_geom.sh` landed, the OFF
rebuild-cmp was taken in an isolated copy (`/tmp/g1off/repo`; the live `out/` and the running 998
build were never touched — 0 `/mnt/.../out` path hits in both build logs). Same-path pool compile,
`STAGE90_XNU_SHREG_GEOM` **off in BOTH builds**, every other switch 998's own build carries held
fixed: the record's 15 keys + the 7 pins, collected exactly as `scripts/build_998.sh` collects them,
with the single omission of `-DSTAGE90_XNU_SHREG_GEOM=1`; plus every `STAGE90_XNU_*` environment
export build_998.sh sets except the GEOM one, with `STAGE90_XNU_HFS_ROOT_RW` read from the record.

* build #1 on the stripped tree (pre-images re-verified byte-exact `f91c3460…` / `d8f41a7b…` /
  `079c7a17…`) — 705 objects, rc 0.
* the REAL stage script (staged shas re-verified `482b4288…` / `da944439…` / `58cc2823…`).
* build #2, byte-identical command — 705 objects, rc 0.

**Result: 705/705 objects byte-identical, 0 differing — and every one was genuinely recompiled in
build #2** (mtime moved on all 705; 0 hash-changed), so the identity is over real compiles, not
reused objects. The seven `ios7_compat.h`-includer dirs reproduce §4's split exactly, 0 differing:
`osfmk/vm` 21 · `osfmk/arm` 19 · `osfmk/kern` 58 · `osfmk/ipc` 17 · `bsd/vm` 3 · `bsd/kern` 84 ·
`bsd/dev/arm` 10 = **212**. The two objects the switch would change are identical in both builds:
`osfmk_vm_vm_shared_region.o`
`98c1998aa79caeaf2fd3cd305395b7145875b153970fa05d754a45141953e654`, `osfmk_arm_status.o`
`28a3d47ccfea7d226127ce3abf8079bb5d15924a7a1ad85de8017423411c79c2`.

**Two build-requirement facts found on the way.** (a) The pool build REQUIRES `out/mach_headers_d13`
(`MIG_HEADERS`) already generated — a hand-run `tools/gen_mach_headers.sh` prerequisite; the device
table, manifest, device headers, option headers and `pseudo_inits` are regenerated BY the build
itself into its own roots. (b) The platform block hard-`#error`s if `STAGE90_XNU_CARD_TOTAL` /
`STAGE90_XNU_FULL_EXTENT` / `STAGE90_XNU_CARD_COW` = 1 without `STAGE90_XNU_EMMC_STRATEGY` = 1: any
build carrying the storage keys must also carry `HFS_ROOT_MEDIA` / `EMMC_STRATEGY` / `ROOT_FROM_CARD`
(998 does, via the record).

Full log retained at `/tmp/g1off/RESULT.txt` (`build1.log` / `build2.log`, sha/mtime snapshots)
until 998's press is judged.

## 10. Post-`kr=0`: what the silence does and does not imply (2026-10-13)

**Scope.** Audit-side reading of the 998 capture (`cancro-last_kmsg-998-armed-d13-dee822b0.txt`,
sha256 `0c558c533353c9abb3ebd6111773208253c47ab3acef993c12cdf7b09d8acec2`) against the staged tree
(`bsd/vm/vm_unix.c` sha256 `4b3ec2b83aaf9e6ae3d22264085c100a57cf562de7a2b1b30ef967c3d42ed0b6`) and
997's capture (sha256 `23bb574ba7795bd4205f76af83d2e4b0622ba8978db64d8f55a7ea07060215f1`). No tree
edits. The frontier doc's §10/§10c carry the build-side narrative; the timer measurement
cross-linked in point 4 is theirs.

**1. The silence is the TEXT channel; the record channel runs on — the two must not be conflated.**
998's console text ends at `:4109` (`IOS7SHREG map_file kr=0`), and the 997 halt chain is gone
(libbsm / IOS7WITNESS / `undefined instruction` / `dyld>` — 0 lines each), so the silence is not a
halt-with-a-name. But the capture has a second channel: `:4110`-`:20501` is 100% `xnu_live_*` records
(0 non-record lines in the whole span — checked), written in event order by the entry-side live
recorder. It shows the run did not end at `:4109`: the exec records sit at `:19374-:19377`
(`xnu_live_exec_seq=0x1`, `exec_caller=0x80036100`, `exec_who=0x1`, `exec_proc=0xc0c41cf8`; 997 has
the same block, `:21255-:21258`, same values), then 1,124 further record lines to `:20501` (1,128
counting the exec block) — storage reads continuing to a last `xnu_live_storage_drv_lba=0x00422963`
(`:20493`). **997's post-exec tail is name-for-name and order-for-order identical** over 1,125 lines
(998 `:19378-:20502` vs 997 `:21259-:22383`); the only value drift is 33 lines, all of it run-to-run
timing counters (`storage_dr_ticks` / `dr_polls` / `rd_data_ticks`) and entry-image addresses
(`sleh_pc` / `sleh_lr` / `sleh_recover`, `slot_ab_m4`) — no LBA, ordinal, seq or caller field
differs. So the kr=1-vs-kr=0 divergence never enters the record channel. Text-silence is the absence
of a NAME, not of progress (the m817 shape: a silent channel is a reading only if success is silent —
this channel prints probes and failures only).

**2. `kr=0` is a RETURN value at a one-call-site probe — and it is not the syscall's last word.**
The `IOS7SHREG map_file` printf lives in `_shared_region_map` (`vm_unix.c:1251`), immediately after
`kr = vm_shared_region_map_file(...)` returns (`:1425-1431`), under a `static … < 4` guard
(`:1433-1440`); the tree holds exactly one call site of `vm_shared_region_map_file` (`:1425`;
definition `osfmk/vm/vm_shared_region.c:969`). By value: (a) every print is a RETURN — the printed
call's mapping work COMPLETED, so the boot is not "stuck inside the map"; (b) the guard admits four
returns and exactly ONE exists — a second return would have printed and did not. (The frontier doc's
§10c carried this as "the SECOND call never returned"; it was corrected 2026-10-13, commit `8139194`,
to the single-invocation reading derived below — the two docs now agree.) The by-value
addition here, on the same bytes: the wrapping syscall (`shared_region_map_and_slide_np`, `:1556`)
prints an entry probe (`:1577-1581`; capture `:4104`) and an exit probe that fires on the SUCCESS
path too (`if (slide)` → `_shared_region_slide` → fall through to `done:` →
`printf("IOS7SHREG map_slide ret=%d")`, `:1651-1656`; 997 shows the failure-path twin `ret=14`,
source `:1629-1630`, capture `:4109`). 998 has NO `map_slide ret=` line at all: the single recorded
invocation — one entry probe (`:4104`), one `map_file` return (`:4109`), no second entry probe —
never reached its own exit print. Between `_shared_region_map`'s return (`:1626`) and that print the
only work in flight is `_shared_region_slide` (`:1636`, body `:1513-1554`), whose two named failure
prints (`slide_info initialization failed`, `:1525`; `Sanity Check failed for slide_info`, `:1543`)
are likewise absent. Minimal reading, no channel loss assumed: the stop lies inside the SLIDE half
of the first invocation. The text channel has no measured losses (the one measured "loss" in this
image, the `exec_done` records, is root-caused to the cap itself — point 4); locating the exact
blocking line is the shreg-hang lane's question, not this section's.

**3. What bounds the silence: the record cap and the watchdog — not the map.** (a) The record
channel stops at `xnu_live_capped=0x00004000` (`:20502`; cap 16,384) — its silence past that point
is the CAP, not a stop; 1,128 post-exec dyld-era lines ran before it. (b) 0 `xnu_live_wdt_*` / 0
`xnu_live_usb_*` / 0 `xnu_live_pcx` records ⇒ the handed-off kernel never idled ⇒ the resident pet
never ran ⇒ the SoC watchdog reset it un-fed. The "~28 s" is a RUNTIME/OPERATOR reading, not a
capture timestamp — `last_kmsg` carries none; it bounds when the run ENDED, not where it stopped.
Together, the silence licenses exactly this: "after a successful map return there is no named
failure, no named halt and no panic; the record channel is truncated at its cap; the run was ended
by the un-fed SoC watchdog." It does not license "the kernel stopped at `kr=0`", and it does not
locate the stop.

**4. What 999 owns — the discriminating measurement.** The 2026-10-13 timer-freeze measurement
(frontier doc §10c) is the binding constraint: `xnu_live_tick_irq` stops at ≤64 deliveries in ALL
six captures (987, 994-998), so nothing driven by `rtclock_intr` runs after ~4.8 s of mach time — a
deadline-based park after the freeze can never resolve, and "hung on a deadline" vs "hung in a spin"
are indistinguishable by outcome. 999's discriminating observable is therefore POST-FREEZE RECORD
GROWTH: does the record channel keep growing after the freeze (progress independent of
`rtclock_intr` — 997/998 are themselves evidence that it does, up to the cap) or stall at the
freeze. Counters to read together, 998's by value: `xnu_live_tick_arm_calls=0x1` (`:4112`) ·
`xnu_live_capped=0x4000` (`:20502`) · `xnu_live_storage_dr_ticks=0x855` (`:20500`) ·
`xnu_live_ostext_total=0x17ee` (`:20505`). Also owed per the frontier doc's §10: the cap raise +
post-256 dump thinning.

**The `exec_done` "loss" is root-caused to the cap (buildplumb 2026-10-13; verified here by value).**
The capture's 16,426 `xnu_live_` lines split exactly into **16,384 cap-counted + 42 direct** (38
direct in the head span, 0 in `:19378-:20501`, 4 post-cap — the tombstone and the final `ostext`
triple; the direct set is fixed by its tree sites, `entry_stubs.c:2398-2413`, `:2475`, `:2659-2664`,
`:2687-2688`, `:2700-2701`, `:2740`, `:2765-2767`, `:2786`, `:2796-2797`, and includes the
space-filled `xnu_live_ostext_at` record ending the 124,122-char line at `:4110`). Counted through
the exec block = 15,298 − 38 = **15,260**; `:19378-:20501` is 1,124 counted, 0 direct;
15,260 + 1,124 = **16,384 = 0x4000 exactly** — the tombstone's own value. So every counted write up
to the cap left a present line (zero silent counted writes before it), and the trio — whose calls'
premise, the load's return, the `:4100` printf proves — ran at counter ≥ 16,384: dropped by the
CAP, not by the console write path (whose two candidates §10(c) had already falsified). (buildplumb's
message counts 16,425 / 15,297 / 41 / 37 — each one line short, all four the same line: the
space-prefixed `xnu_live_ostext_at` record at `:4110` is invisible to a line-start name parse; the
derived 15,260 and the identity are unaffected.) The tombstone's displaced payload is the first counted call after the cap: by the trailing
read group's shape (84 records vs the **90** of the ten earlier groups — it is missing exactly
`storage_dr_ps_after`, `dat_line`, `data_inhibit`, `status_after`, `ena_after`, `done`, cf.
`:20412-:20417`) the slot is the driver tail's first missing key; buildplumb's stronger "the
tombstone IS the displaced `xnu_live_exec_done_seq`" additionally requires that read's tail to have
been abandoned, which the capture cannot separate. Fix in 999 (no readback probe owed): the trio
goes direct / cap-exempt at `entry_stubs.c:5012-5017` — corroborated here: the direct family
demonstrably survives the cap (the 6th `ostext` triple is post-cap, `:20503-:20505`).

## 11. The 999 slide-half ladder, audited by value (2026-10-13)

Scope, stated first: report #2's three-probe ladder inside `_shared_region_slide` (`vm_unix.c:1513-1554`),
the atime arbitration (lead's NO-WRITE vs report #2), the three count discrepancies plus the byte-match,
and the sleh/`trap.c` sharp question. "The elf" = `out/stage90/xnu_arm_entry.bin`, sha256
`dee822b0ca58afa1…` — re-verified THIS window (`xnu_arm_entry-config.txt` records the same hash; nothing
was rebuilt between 998's press and this reading), so every string fact below is the pressed image's own.
"The capture" = `cancro-last_kmsg-998-armed-d13-dee822b0.txt`, sha256 `0c558c53…`, 20,507 lines by
`wc -l`; the file also carries 63 raw `\r` bytes, so any reader that splits on `\r` (python text mode)
numbers 62 lines HIGH — every cite in this doc and the frontier is newline-based, and no value below
depends on which split produced it.

### 11.1 Claim 1 — the gate, the strings, the four lines (VERIFIED; two precisions)

**One flag, four sites, one function.** `:1574-1575`: `static unsigned int ios7shreg_slide_logged = 0;`
and `int ios7shreg_slide_probe = (ios7shreg_slide_logged++ < 4);` — block-scope inside
`shared_region_map_and_slide_np` (`:1556`), static storage; the same `ios7shreg_slide_probe` governs the
entry print (`:1577-1581`) and ALL THREE exit prints (`:1629-1630` map-failed, `:1645-1646` slide-failed,
`:1653-1654` done-fallthrough). The last two print the identical string `IOS7SHREG map_slide ret=%d` —
**one format string, three call sites** — so "ret=" in a capture cannot by itself say which of the three
fired; only program order can, and within one invocation exactly one can.

**Precision 1 — five strings, not six.** The elf carries exactly 5 `IOS7SHREG` format strings (`check_np
pid=`, `check_np ret=`, `map_file kr=`, the `map_slide` entry text, `map_slide ret=`); the four
`IOS7LAB cache mapping` lines are a different prefix (`IOS7LAB` = 14 strings in all). Present also:
`slide_info initialization failed with kr=%d` (1, `vm_shared_region.c:1525`) and `Sanity Check failed
for slide_info` (1, `:1543`). Absent: `Succesfully init slide_info…` (0) — the `#if DEBUG` block at
`vm_shared_region.c:1545-1550` did not compile ⇒ **the sanity path succeeds SILENTLY**. That is
load-bearing for the ladder's top rung: after `(c) kr=0`, the next possible texts are `Sanity Check
failed for slide_info` + `map_slide ret=14`, the `vm_shared_region.c:1564` panic's own message
(`mach_vm_protect` refused), or the `:1653-1654` `map_slide ret=0`. Silence between `(c)` and the exit
print is a reading only under this inventory (m817 shape).

**Precision 2 — the count.** 998 has exactly 4 `IOS7SHREG` lines: `:4102-4103` (`check_np`, the 294
probe's, ret=12), `:4104` (entry; `count=4 mappings=0x2bd81848 slide=0x1a86000 slide_start=0x3dc32000
size=0x00089000`), `:4109` (`map_file kr=0`). No exit print. The silence to be split is exactly the span
between the `:1438` print (inside `_shared_region_map` = `:1251-1511`, under its own block-scope
`static … < 4` at `:1433-1440`) and the first print site in program order, which is probe `(a)`.

### 11.2 The three probes — placement, argument types, one falsifier each

The body as the tree carries it: `:1521` `void *slide_info_entry = NULL;` · `:1522` `int error;` ·
`:1524` `if((error = vm_shared_region_slide_init(slide_size, entry_start_address, entry_size, slide,
sr_file_control)))` → `:1525` failure printf → `:1526` `goto done;` · `:1529` `slide_info_entry =
vm_shared_region_get_slide_info_entry();` · `:1530-1532` NULL → `error = EFAULT;` · `:1533-1535`
`copyin((user_addr_t)slide_start, slide_info_entry, (vm_size_t)slide_size);` · `:1537-1539` `if (error)
goto done;` · `:1541-1551` sanity (+DEBUG block) · `:1552-1554` `done: return error;`. Parameter types:
`slide` `uint32_t` (`:1514`), `slide_start` `mach_vm_offset_t`, `slide_size` `mach_vm_size_t`.

- **(a)** `:1523`, between `int error;` (`:1522`) and the init call (`:1524`) — a blank line; legal,
  and it precedes the first waitable statement. All three args in scope. On this armv7 build
  `mach_vm_offset_t`/`mach_vm_size_t` ARE `uint32_t` (`osfmk/mach/arm/vm_types.h:108-109`, the `#else`
  of the `__LP64__` split — the same file's "We target iOS 4.3" note), so the proposed `(unsigned)`
  casts are **identity, not truncation guards**; keep them only to match the `:1578-1581` house style.
  Proposed text: `printf("IOS7SHREG slide_enter slide=0x%x src=0x%08x n=0x%x\n", slide,
  (unsigned)slide_start, (unsigned)slide_size);` — 3 args, 3 conversions, types match.
  **Falsifier:** `(a)` prints with `src`/`n` different from `:4104`'s `0x3dc32000`/`0x00089000` — the
  call site `:1636-1641` passes `uap->slide_start`/`uap->slide_size` UNCHANGED, so any difference is a
  wiring/argument finding; stop and re-read the call.
- **(b)** `:1528`, between the init-failure block's `}` (`:1527`) and the getter (`:1529`) — a blank
  line; legal. Fires only when `vm_shared_region_slide_init` returned 0 (the failure branch prints
  `:1525` and `goto done`, never reaching `:1528`). `slide_info_entry` there is **necessarily NULL**:
  `:1521` is its only initializer, `:1529` its only assignment. **Keep it at `:1528`; do not
  relocate.** As a presence marker it is exactly the discriminator needed ("stopped inside
  `vm_shared_region_slide_init`" vs "past it"); the getter is a one-line return
  (`vm_shared_region.c:1541-1544`), so there is no stop candidate between `:1529` and the NULL check;
  and after a successful init the pointer is guaranteed non-NULL by value — the success path stores it
  inside `kmem_alloc` (`vm_shared_region.c:1502-1504`, allocator writes through
  `&slide_info.slide_info_entry`), sets `shared_region_completed_slide = TRUE` (`:1523`), returns `kr`
  (`:1533`); every other path returns `KERN_FAILURE`/`KERN_INVALID_ADDRESS` (`:1500`, `:1525`). A
  post-getter `%p` would print a non-NULL pointer every time — zero information. **Falsifier:** `(b)`
  AND `slide_info initialization failed with kr=%d` in the same run — mutually exclusive per call;
  that text set is two invocations or a mixed build, not one reading.
- **(c)** after `:1536` (the copyin `else`-branch's closing `}`) and before `:1537` (`if (error)`) —
  legal; fires on BOTH outcomes. Arg `error` is `int`; `%d` matches. Reading: once `(b)` has printed,
  the `:1531` EFAULT is impossible on this path, so `error` here is only 0 or the copyin's return.
  **Falsifier:** `(c) kr!=0` with NO `IOS7SHREG map_slide ret=%d` later in the text — for a failing
  slide half the `:1645-1646` print is unconditional (`:1643` undo, then print, then `return kr`), and
  this text channel has no measured losses (§10); treat as source/capture mismatch, not a stop.

### 11.3 The reading table — frozen

- **No `(a)`.** Stop before `_shared_region_slide`'s first statement: in `_shared_region_map`'s tail
  (`:1466-1510`) or the call/return boundary. The tail's steps by value: `vnode_lock_spin` (`:1468`),
  `v_flag |= VSHARED_DYLD` (`:1470`), the atime update `:1474-1480` (write-free — §11.4),
  `P_NOSHLIB` atomic (`:1482-1484`), then `done:` `vnode_put` (`:1493`), `fp_drop` (`:1498`),
  `vm_shared_region_deallocate` (`:1503`). The tree's own comment (`:1490-1491`) says `ubc_map()`
  still holds the vnode in the non-error case — `vnode_put` cannot drive an inactive/flush here. No
  step in the tail issues I/O; the only blocking candidates are waits (spinlock, the setattr's cnode
  lock), not I/O.
- **`(a)` only.** Stop inside `vm_shared_region_slide_init` (`vm_shared_region.c:1441-1534`). Its only
  waitable steps: the object lock (released `:1528`; map lock `:1530-1532`) and `kmem_alloc`
  (`:1502`) — no disk I/O in the function.
- **`(a)+(b)`.** Entered the copyin block; its only work is the `copyin` of `[slide_start, +slide_size)`
  = `[0x3dc32000, 0x3dcbb000)` — 0x89000 bytes, which the wire places as the LAST 0x89000 bytes of
  `m2` (wire extent `[0x3b9ce000, 0x3dcbb000)`), i.e. the cache file's slide-info region at
  `0x101AA000` (§11.6). Any storage delivery of these pages reads LBAs `[0x4A3838, 0x4A3C80)` — the
  draft's prediction, exact.
- **`(a)+(b)+(c) kr=0`.** Copyin completed. Remaining: `vm_shared_region_slide_sanity_check`
  (`vm_shared_region.c:1547-1590`) — `mach_vm_protect` (`:1559-1562`; failure panics with its own
  text, `:1564`), then a memory-only toc loop (`:1567-1576`; failure prints `No sliding bitmap entry…`
  `:1572`). Both outcomes print ⇒ "in sanity" = neither text printed; its only waiting step is the
  `mach_vm_protect`.
- **`(a)+(b)+(c) kr!=0`.** The copyin itself failed ⇒ `:1645-1646` `map_slide ret=14` must follow in
  the same text.
- No deadline anywhere in the half (the frozen timer enters only through the empty page pool; the
  draft's `vm_resident.c:4574` cite bounds that, not re-derived here).

### 11.4 ATIME — the lead's NO-WRITE reading is CORRECT, and stronger than stated; report #2 refuted by value

The chain, each line as the tree has it:

1. The only setattr on the path: `vm_unix.c:1474-1480` — `MNT_NOATIME` gate; `VATTR_INIT`; `nanotime`;
   `VATTR_SET_ACTIVE(&va, va_access_time)` ONLY; `vnode_setattr(vp, &va, vfs_context_current())`
   (`:1479`). The VATTR carries exactly one active field: `va_access_time`.
2. `hfs_vnop_setattr` (`hfs_vnops.c:1107`): `decmpfs_update_attributes` (`:1134`) — its whole body is
   under `if (VATTR_IS_ACTIVE(vap, va_flags))` (`decmpfs.c:786`) ⇒ for an atime-only VATTR it falls
   through to `return 0` without a single read (no xattr, no attribute fetch).
3. `check_for_tracked_file` (`:1142`) is explicitly skipped for the atime-only shape; the truncate
   block (`:1198-1221`) needs `va_data_size` — skipped.
4. `hfs_lock(VTOC(vp), HFS_EXCLUSIVE_LOCK)` (`:1222-1226`) — the only wait on the path, a sleepable
   cnode mutex; no disk. It is the documented reason the fast path exists ("…speeds up mmaps, in the
   rare case that they get caught behind a sync", `:1228-1234`).
5. **The fast path** `:1236-1239` — verbatim: `if (vap->va_active == VNODE_ATTR_va_access_time) {
   cp->c_touch_acctime = TRUE; goto out; }` — a dirty-flag on the in-core cnode, then a jump past
   everything below.
6. `out:` (`:1354-1387`): `hfs_unlock(cp)` (`:1363`), the `decmpfs_reset_state` block (`:1365-1384`;
   set only under a UF_COMPRESSED `va_flags` change — past the fast path), `return error` (`:1387`).
   **`hfs_update(vp, TRUE)` at `:1352` — the only catalog-write call in the function — is NOT
   executed**: the fast path jumps over it to `out:`.

**Verdict: no write is issued anywhere on the atime path; nothing on the map tail does disk I/O at
all.** Report #2's "the tail's only I/O — a metadata write delivered by the CNTVCT-polled ladder
(`entry_storage.c:8942`)" is refuted twice over: (i) the setattr is write-free by construction, with
the tree's own comment naming this exact scenario; (ii) the `:8942` cite is right AS A LINE in the
998-committed tree (`ST_LIVE("xnu_live_storage_dr_ticks", (uint32_t)stage90_cntvct_read() - t0);` —
the rung-45 DAT-line self-clear poll inside `st_data_reset`; 999's staged WDT edit moves it to
`:8984`) but wrong as a ROLE — `dr_ticks` measures a host-reset poll, it delivers nothing; the
delivery path for every recorded sector is the door `entry_storage_driver_read` (working tree
`:7700-7718`; committed tree `:7661`), whose `drv_*` records show it alive to the record tail. If
report #2 were right, the writing function would have to be `hfs_update` at `hfs_vnops.c:1352` — the
one line this path skips. **Corollary for 999:** a run with NO `(a)` cannot be explained by a hung
atime write; the remaining candidates on that rung are waits, not I/O.

### 11.5 The sleh / `trap.c` question — CONSISTENT as reported; two frontier wordings are each wrong

`trap.c` data-abort decider (`:644-712`), by value: `vm_fault(thread->map, …)` (`:657-661`); if that
fails, `vm_fault(kernel_map, …)` (`:667-671`); **only if BOTH fail**: `if (!thread->recover)` →
`panic_context(… "Kernel data abort" …)` (`:676-692`), else redirect (`pc = thread->recover`, I-bit
cleared, `recover = NULL`, `return`; `:693-708`). `return;` at `:712` is reached whenever EITHER fault
succeeded — the handler returns with no panic and no redirection. The decider's actual rule: an
unarmed kernel data abort panics only when the fault is UNSATISFIABLE; satisfiable ⇒ silent return.
Report #2's event 3 — `fsr=0x807` (data abort, write, translation), `far=0xcfdfe000`,
`recover=0x00000000`, `pc=0x80015fc8`, back trio `sleh_back=3 / redirect=0 / at_back=3`
(`:20182-20194`, `:20211-20213`) — returned un-redirected ⇒ by `:712` the fault was satisfied; **the
return is exactly what `trap.c` does; no panic is owed and none appears.** The two statements conflict
only when "panics on unarmed kernel data aborts" is read as "on ALL of them"; the tree says "on the
unsatisfiable ones". All three 998 events are kernel-mode (`sleh_user=0`, thr `0xc0bfa000`); all
three back trios read `redirect=0` (no redirect record anywhere in the capture), so events 1-2
(`recover` `0x80495670` / `0x80495bac`) were satisfied and retried too; `sleh_armed` = 2 (events 1-2;
event 3 had `recover=0` at entry). Line arithmetic: 3 events × 15 keys + 3 backs × 3
(back/redirect/at_back) + 2 `armed` = 45 + 9 + 2 = 56 — the block's exact size.

Frontier flags: §10's "8 `sleh` user-fault events" is wrong twice — the count is **3** (the 8 is
`SLEH_LIVE_MAX`, the recording cap) and the mode is **kernel** (`user=0` on all three); §10c's "all
`sleh_cpsr` 0x20000193" is 2-of-3 (event 3 reads `0x60000193`); the memory file
`mi4-998-g1-confirmed-map-file-kr0.md` carries the same "user-fault" wording and owes the same
correction.

### 11.6 The three counts, the byte-match, the span

**The families, by their tree sites.** `rd_*` (69 keys/group) — `st_read_single_block`
(`entry_storage.c:7294`; `rd_calls` `:7313`, `rd_lba` `:7518/:7524`). `drv_*` — the door
`entry_storage_driver_read` (`:7700`): `drv_calls`/`drv_lba` at the head (`:7705-7706`), then
`st_data_reset()` (`:7709`) and `st_read_single_block(1u)` (`:7710`), then the 5-key tail
`after_lba/get/w0/w1/held` (`:7712-7716`). `dr_*` — `st_data_reset` (`:8958`), 14 keys, whose body the
record tail shows complete at `:20412-20417` (`ps_after, dat_line, data_inhibit, status_after,
ena_after, done`).

**The counts and the reconciliation.** rd groups = 129; drv calls = 120 (119 with a full 5-key tail +
1 head-only); dr bodies = 130 with `dr_done` in 129. The multiset test over every recorded LBA (awk,
newline-based): rd − drv = {`0x1`…`0x8` ×1 each, `0x400002` ×2}; drv − rd = {`0x422963` ×1}. Mechanism:
**130 reads attempted** (130 resets); **129 completed** (the rd groups); **120 went through the door**,
of which **119 completed** and the 120th (`drv_lba=0x422963`, the last `drv` line, `:20492-20493`) was
cut at the cap inside its `dr` body — after `dr_timeout` (`:20501`; the poll had completed,
`dr_polls=0x104`, `dr_held_after=0`) and before `ps_after`, with its `st_read_single_block` never
reached (no rd group for that LBA); the remaining **10 went direct** (boot-rung sites call
`st_data_reset` + the read body without the door — LBAs `1..8` and two of the three `0x400002`
volume-header reads). Arithmetic closes exactly: 129 − 120 = 9 = 10 direct − 1 door-in-flight. Neither
"129" nor "120" is wrong; they count different families, and the "9" is the sum of two tails, not a
gap. (The tombstone `:20502` — `xnu_live_capped=0x00004000` — sits one counted write after `:20501`;
§10's displaced-payload analysis stands unmodified.)

**The byte-match (VERIFIED byte-for-byte; the strongest single integrity proof of the text channel).**
The four `:4105-4108` wire lines vs the cache header at rootfs.hfs offset 0x455d000 (re-parsed this
window; the on-disk mapping entries are 32-byte, uint64 fields — parse them that way): magic
`dyld_v1   armv7`; mappingOffset 0x68, mappingCount 3. m0 header `(0x2c000000, 0x0c266000, 0)` → wire
`va=0x2da86000 = 0x2c000000 + slide(0x1a86000)` EXACT, size/offset equal, prots 5/5 = 5/5. m1 header
`(0x38268000, 0x1ce0000, 0xc266000)` → wire `va=0x39cee000` EXACT, size/offset equal, prots wire 23/23
where the header holds 3/3 — `0x23 = 0x3 | 0x20`, and **0x20 is `VM_PROT_SLIDE`**
(`osfmk/mach/shared_region.h:117`; consumed `vm_shared_region.c:1061`): dyld's mark on the slid
segment, printed raw (`vm_unix.c:1218-1220` prints `wire[].max_prot`). m2 header
`(0x39f48000, 0x22ed000, 0xdf46000)` → wire `va=0x3b9ce000` EXACT, prots 1/1. The 4th line
`(0x3dcbb000, 0x177000, 0x12bda000)` is dyld's own extra mapping: offset = codeSignatureOffset, size =
fileSize − codeSignatureOffset, flags 1/1 — not a header row. The wire reproduces the file's header
losslessly under `+slide`, and the one "mismatch" (23 vs 3) is tree-defined semantics, not a wire
misparse. Two of my own earlier notes corrected here: the 4th line's offset field is
`codeSignatureOffset` (not localSymbolsOffset — that is 0x10233000/0x29a6a80 and is not in the wire),
and the "6 IOS7SHREG strings" count is 5.

**The span test.** Cache file first sector: 0x455d000/0x200 = 0x22AE8, + card partition base 0x400000
⇒ first LBA **0x422AE8**. End: fileSize = codeSignatureOffset + m3 size = 0x12bda000 + 0x177000 =
0x12D51000 (= 300.8 MiB — the frontier's "301 MiB") ⇒ 0x96A88 sectors ⇒ last LBA 0x422AE8 + 0x96A88 −
1 = **0x4B956F**. Recorded LBAs in `[0x422AE8, 0x4B9570)`: **ZERO** (min recorded 0x1, max 0x4F4987).
Report #2's span end 0x4B9550 is short of mine by 0x1F sectors (31; its derivation bounded the file
~15.9 KiB tighter); both ends lie below the first recorded LBA above the span (0x4F4987), so the
verdict is end-independent. **Self-correction, recorded:** my earlier suspicion that 0x4B9550 was
"miscalculated" rested on a stale fileSize (0x12E51000) from a mislabeled note; by value the end is
0x4B956F and the report's figure was essentially right. Interpretation, as the draft has it: the
record tail's last door LBA (0x422963) is 0x185 sectors BELOW the cache file's first sector and no
cache-file sector was recorded — consistent with the cap cutting the channel before the 438-era reads
(the 8192-byte header read at 0x422AE8 would be post-cap and invisible), NOT evidence that the reads
did not happen.

**The copyin source, two independent derivations (both exact).** VA-side: `slide_start − m2.base =
0x3dc32000 − 0x3b9ce000 = 0x2264000` (= 0x22ed000 − 0x89000, the last slideSize bytes of m2);
`+ m2.fileOffset 0xdf46000 = 0x101AA000` = the header's `slideInfoOffset`. File-side: `0x455d000 +
0x101AA000 = 0x14727000` ⇒ LBA `0x400000 + 0x22AE8 + 0x80D50 = 0x4A3838`, count `0x89000/0x200 =
0x448` ⇒ `[0x4A3838, 0x4A3C80)` — the draft's prediction, exact by both routes. (My prior-window
"Δ 0x200000 from slideInfoOffset" was an arithmetic slip of mine — the within-m2 offset is 0x2264000,
not 0x2064000; there is no discrepancy. Recorded.)

### 11.7 Flags for the frontier doc; the frozen pre-reg

- **Frontier §10** "8 `sleh` user-fault events" → 3 events, kernel-mode (`user=0`); the 8 =
  `SLEH_LIVE_MAX`. **§10c** "all `sleh_cpsr` 0x20000193" → 2-of-3 (event 3 `0x60000193`). **§10d/r2**
  span end 0x4B9550 → 0x4B956F by value (±31 sectors; verdict unaffected). The same "user-fault"
  wording in the 998 memory file.
- **Claim 2 (console cannot drop) — substance VERIFIED** as stated: `#define ENTRY_OS_BLOCK
  0x00020000u` (`entry_stubs.c:2510`), reserve+space-fill at `:2737-2739` with the block record
  `:2746`; the capture's `ostext_total` ends `0x17ee` (`:20505`), 6 heals, `ostext_block=0x00020000`
  (`:4111`), and `ostext_limited` occurs ZERO times; `:4110` is the 124,122-char fill line ending in
  the space-prefixed `xnu_live_ostext_at=0x00048296`. One cite corrected: the lead's ":2692-2698
  space-filled" — the fill loop is `:2738-2739` (inside `entry_os_reserve`); `:2692-2700` is the
  console-externs comment block.
- **Report #2's `entry_storage.c:8942`** — correct line, wrong role (§11.4); the delivery path to
  name is the door at `:7700-7718`.
- **Frozen for 999:** the three probes as texted in §11.2 ((b) KEPT at `:1528` — presence marker,
  entry necessarily NULL, no relocation), the reading table §11.3, the atime dispute resolved for the
  lead, the counts and byte-match recorded in §11.6, and the copyin-fault LBA prediction
  `[0x4A3838, 0x4A3C80)` now derived two independent ways. Readings 999 must NOT be judged on: the
  position of `map_slide ret=` (one string, three sites), the presence of a sanity success print
  (compiled out), and any line number from a `\r`-splitting reader (this file numbers 62 high).