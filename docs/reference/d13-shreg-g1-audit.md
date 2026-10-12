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