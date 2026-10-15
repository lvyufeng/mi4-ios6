# 1012 — upstream diff of the WRITE-fault path (our tree vs the canonical HD2 port and Apple Darwin-13)

Scope: `leo_enter` / `pmap_vm_prot_to_page_flags` / the `vm_fault` path. Host-side only: no device, no
builds. Every claim carries a `file:line` or a URL. Where our tree matches upstream, that is stated as
the finding.

Sources compared:

| tag | path / URL |
|---|---|
| OURS  | `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c`, `.../vm/vm_fault.c` (our build tree — **not** a reference) |
| UP-HD2| https://raw.githubusercontent.com/HTC-Leo-Revival-Project/xnu/gary_codeunfuckery/osfmk/arm/pmap.c and `.../osfmk/vm/vm_fault.c` |
| APPLE13 | `external/xnu-2050.18.24/osfmk/vm/vm_fault.c` (+ `external/xnu-upstream`, `external/apple-xnu-rel-2050` — byte-identical to each other). **These trees ship no `osfmk/arm/` at all** — only `x86_64/` and `i386/` pmaps — so the ARM pmap has no pristine Apple counterpart on disk. |
| DARWIN17 | `external/xnu-4570.1.46/osfmk/vm/vm_fault.c`, `.../osfmk/arm/pmap.c` (later Apple: "did Apple fix a strip?") |

---

## 0. HEADLINE

**Our `leo_enter` cold path and `pmap_enter_options` are byte-for-byte the upstream HD2 port, and
`vm_fault.c` is byte-for-byte Apple Darwin-13 except the six 1012 WRSTRIP probes. There is no
hand-edit and no missing Apple fix on this path.** The WRITE-loss is not a source difference.

**And the dispatch the whole 1012 rung assumes does not exist in the built kernel.** `leo_enter`
is gated `#if BOARD_CONFIG_QSD8250_LEO` (`osfmk/arm/pmap.c:3181`), but the D13 kernel is built with
`-DBOARD_CONFIG_MSM8974_CANCRO=1` (`tools/build_xnu_arm_kernel.sh:202`) and **nothing in the tree,
the build, or the per-file `component_defines.sh` output ever defines `BOARD_CONFIG_QSD8250_LEO`.**
The linked object proves it: `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o` carries **zero** `leo_*`
symbols and zero of the leo-only panic strings. D13 runs Apple's stock `pmap_enter_options` body,
not `leo_enter`. See §4 — this is the strongest candidate and it contradicts `docs/1012` and
`tools/check_1012_capture.py:20` directly.

---

## 1. `pmap_vm_prot_to_page_flags` — IDENTICAL

`osfmk/arm/pmap.c:1095` (OURS) vs UP-HD2 `pmap.c:1094`. Comparison was done by stripping comments and
all whitespace and comparing the resulting character streams of the extracted function body:
**identical**. The APX logic is the same in both:

```c
    if (prot & VM_PROT_WRITE)
        pte &= ~(L2_ACCESS_APX);    /* APX-bit, RW? */
    else
        pte |= (L2_ACCESS_APX); /* APX-bit, R-? */
```

`prot & VM_PROT_WRITE` ⇒ APX cleared ⇒ writable leaf. Nothing here can lose WRITE that upstream does
not also lose. (Bit values: `L2_ACCESS_APX (1<<9)`, `L2_ACCESS_USER (1<<5)`, `L2_ACCESS_PRW 0x10`,
`L2_SMALL_PAGE 0x2`, `L2_ADDR_MASK 0xfffff000` — `osfmk/arm/pmap.h:214-227`, same in both.)

**Verdict: cannot explain the lost WRITE.**

---

## 2. `leo_enter` / `leo_pte_build` — IDENTICAL

Per-function extraction + whitespace/comment/brace-insensitive comparison of OURS vs UP-HD2:

```
[IDENTICAL] leo_enter        ([IDENTICAL] leo_pte_build
[IDENTICAL] leo_mapping_find    [IDENTICAL] leo_mapping_add
[IDENTICAL] leo_mapping_remove  [IDENTICAL] leo_mapping_wire
[IDENTICAL] leo_node_get        [IDENTICAL] leo_node_free
[IDENTICAL] leo_physical_wire_refresh   [IDENTICAL] leo_remove_range
[IDENTICAL] leo_change_wiring   [IDENTICAL] pmap_remove_range
[IDENTICAL] pmap_protect        [IDENTICAL] pmap_page_protect
```

`leo_pte_build` in both is the same seven lines (`pmap.c:1639-1643` OURS):

```c
    value  = ((pn << PAGE_SHIFT) & L2_ADDR_MASK) | L2_SMALL_PAGE;
    value |= pmap_vm_prot_to_page_flags(map, prot, wired, 0);
    if (va == _COMM_PAGE_BASE_ADDRESS) value |= L2_ACCESS_USER;
    if (map == kernel_pmap && va == IOS7LAB_COMMPAGE_ALIAS)
        value |= L2_ACCESS_USER | L2_ACCESS_APX | L2_NX_BIT;   /* compat alias: forced read-only */
    return value | pmap_get_cache_attributes(pn);
```

The only whole-file differences between OURS and UP-HD2's `pmap.c` are (a) a handful of
`#if BOARD_CONFIG_QSD8250_LEO` widened to `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO ||
(defined(STAGE90_XNU_COMMPAGE_ALIAS) && …)` (`pmap.c:205, 2848, 3258, 3450`), (b) `kprintf` → `printf`
plus four `IOS7LAB commpage alias` lab strings around `pmap.c:2860-2880`, and (c) `struct leo_boot_range
{ start, end }` compacted from two fields to two declarations. **None of these sits on the WRITE
path.** (a) *widens* a commpage-alias clause; (b) is print-only; (c) is a struct spelling.

**Verdict: `leo_enter` is upstream-identical. If it ran, it would build a writable leaf for a
write-bearing `prot` — exactly as `docs/1012` assumes. The loss is NOT in this function.**

---

## 3. `vm_fault.c` — BYTE-IDENTICAL to Apple Darwin-13 except the 1012 probes

With comments removed, whitespace collapsed, and braces dropped, OURS vs APPLE13 `vm_fault.c` has
**exactly 7 non-equal hunks, all of them the WRSTRIP instrumentation**, ratio 0.994:

- `extern void entry_note_abt_wr(...)` declaration (`vm_fault.c:105-108`)
- site=1 (twice: before and after `*protection &= (~VM_PROT_WRITE)` at `vm_fault.c:1893`)
- site=2 (`vm_fault.c:1923`, before `*protection &= ~VM_PROT_WRITE` at `:1925`)
- site=5 (`vm_fault.c:2584`, before `PMAP_ENTER_OPTIONS` at `:2590`)
- site=3 (`vm_fault.c:3868`, before `prot &= retry_prot` at `:3874`)
- site=4 (`vm_fault.c:3882`, before `prot &= ~VM_PROT_WRITE` at `:3889`)

The UP-HD2 `vm_fault.c` diff is the *same set* — so our tree's only divergence from upstream on the
fault path is the measurement probes themselves, each wrapped in
`#if defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP`. With the switch off, the file is Apple's
text. No hand-edit, no missing fix.

**Verdict: cannot explain the lost WRITE as a source difference.**

---

## 4. The dispatch — `leo_enter` IS NOT IN THE BUILT KERNEL (strongest candidate)

`docs/1012` and `tools/check_1012_capture.py:20` both assert *"This tree builds
`BOARD_CONFIG_QSD8250_LEO`, so `pmap_enter_options` DISPATCHES to `leo_enter`."* That premise is false
for the D13 build.

Evidence, hardest first:

1. **The compiled object.** `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o` (mtime 2026-10-15 00:32, newer
   than `osfmk/arm/pmap.c` at 2026-10-12 15:27) has **no** `leo_*` symbol:
   ```
   $ nm out/xnu_kernel_obj_d13/osfmk_arm_pmap.o | grep leo      → (nothing)
   $ strings … | grep -c 'tracked mapping|unsupported old PTE|preinit user|PV reserve|bootstrap adopt' → 0 each
   ```
   The leo-only panic strings (`"pmap unsupported old PTE"`, `"pmap tracked mapping missing"`,
   `"pmap preinit user mapping"`, `"pmap PV reserve exhausted"`, …) are all absent, while Apple's
   stock strings *are* present: `"pmap_enter_options: null pv_list"` (`pmap.c:3329`) and
   `"Mapping alias chain exhaustion…"` (`:3410`) — both in the `#else` branch.

2. **The disassembly.** `arm-none-eabi-objdump -d` of that object's `pmap_enter_options` shows the
   Apple path: it calls `pmap_expand`, walks `pv_head_table`, computes `pai`, and does the
   `eor r0,r5,r7,lsr#12` hash-index preamble — none of which `leo_enter` has. `leo_enter` is a
   ~13-line straight-line function with `PMAP_LOCK`/`leo_boot_pair_add` and no `pmap_expand` loop of
   that shape.

3. **The macro is never defined.** `grep -rn "define BOARD_CONFIG_QSD8250_LEO"` over `external/` and
   `src/` → nothing. The only board define the build adds is
   `COMP_DEFINES_EXTRA=(-DBOARD_CONFIG_MSM8974_CANCRO=1)` (`tools/build_xnu_arm_kernel.sh:202`), and
   the per-file define derivation (`tools/xnu_config/component_defines.sh osfmk`) emits no board
   macro. `tools/stage_d13_board.sh:9` states outright that the port chose a *new* board name
   precisely **not** to reuse `QSD8250_LEO`.
   Confirmed at the preprocessor level: `clang -E -DBOARD_CONFIG_MSM8974_CANCRO=1 … pmap.c` contains
   **0** occurrences of `leo_enter`; the same with `-DBOARD_CONFIG_QSD8250_LEO=1` contains 2.

4. **The gate is not widened.** Unlike the commpage-alias clauses (§2a), the `pmap_enter_options`
   dispatch at `osfmk/arm/pmap.c:3181-3182` is gated on the **bare**
   `#if BOARD_CONFIG_QSD8250_LEO` with no `MSM8974` or `STAGE90_*` alternative:
   ```c
   #if BOARD_CONFIG_QSD8250_LEO
       return leo_enter(pmap,va,pa,prot,wired);
   #else
   ```
   So with the D13 board macro, the `#else` (Apple) body is what compiles.

**Why this explains the symptom.** Apple's stock body (§5) *does* rebuild the leaf from `prot` via
`pmap_vm_prot_to_page_flags` and would clear APX for a write-bearing `prot` — so a naive reading says
it should also go writable. But the stock body only reaches that rebuild when
`(old_pte & L2_ADDR_MASK) == (pa << PAGE_SHIFT)` (`pmap.c:3246`); otherwise it goes through the
pv-list churn to `EnterPte` (`:3430`). Crucially the *instrumentation plan* is built on the wrong
function: `--wrap=pmap_enter_options` will observe `prot` at entry, and 1012's decode of the `(E′)`
row as "`leo_pte_build` built a read-only leaf from a read-only `prot`" attributes the leaf to a
function that **is not in the image**. Any conclusion about `leo_enter`'s behaviour from the 1012 press
is therefore about code the device did not execute.

**Verdict: the strongest candidate. It cannot be settled by the 1012 press as designed.** The fix is
to make the instrumentation observe the *actual* dispatch — e.g. a by-value check that
`__wrap_pmap_enter_options` calls a symbol named `pmap_enter_options`-body (as it already does) **and**
that the linked image contains Apple's `null pv_list` string while containing no `leo_` symbols — or,
if `leo_enter` genuinely is the intended path, to widen `pmap.c:3181` to the D13 board macro and
rebuild. Either way the 1012 decode must not name `leo_pte_build`.

---

## 5. Apple's stock `pmap_enter_options` (what D13 really runs)

`osfmk/arm/pmap.c:3185` onward. Two leaf-rebuild sites, both identical in intent to `leo_pte_build`:

- in-place re-protect at `pmap.c:3252-3257` (when the PTE already maps the same PA) —
  `template_pte |= pmap_vm_prot_to_page_flags(pmap, prot, wired, 0)`
- new-mapping at `pmap.c:3441-3443` (`EnterPte`) — the same call.

Both end at `enter_options_done: pmap_flush_tlbs(pmap, va, va + PAGE_SIZE)` (`pmap.c:3472`). So the
stock body also rebuilds the leaf from `prot` and flushes. NOTE: the in-place branch's
`template_pte` **does** include the §2a commpage-alias clause (`pmap.c:3258-3261`), i.e. an alias VA
gets forced read-only there too — but the commpage alias is `0xFFFF4000`, never the userspace write
fault in the storm, so this is not the cause either.

---

## 6. Did Apple later fix a strip? (DARWIN17)

In `external/xnu-4570.1.46/osfmk/vm/vm_fault.c:4825-4832` Apple replaced the unconditional
`prot &= retry_prot` with:

```c
    if (pmap_has_prot_policy(retry_prot)) {
        /* If the pmap layer cares, pass the full set. */
        prot = retry_prot;
    } else {
        prot &= retry_prot;
    }
```

where ARM's `pmap_has_prot_policy` returns **FALSE** unconditionally
(`xnu-4570.1.46/osfmk/arm/pmap.c:4949-4952`). On ARM the new code is therefore *identical in effect*
to the old `prot &= retry_prot`. So ARM never got a WRITE-preserving fix here — the
`fault_type & ~VM_PROT_WRITE` lookup (`vm_fault.c:3794`) demoting `retry_prot` is **Apple's own
behaviour at every arm release we have**, not a port defect. There is no missing Apple fix.

**Verdict: cannot explain the lost WRITE — on ARM, Apple's later tree behaves the same.**

---

## 7. The `vm_fault_page` `*protection` out-param (task 4)

Trace in our tree (identical to Apple13, §3):

- **Declared**: `vm_fault_page(..., vm_prot_t *protection, ...)` `vm_fault.c:783`.
- **Owner / producer of the caller's value**: `vm_fault` declares `vm_prot_t prot` (`vm_fault.c:2779`)
  and it is set by `vm_map_lookup_locked(..., &prot, ...)` at `vm_fault.c:2856`. `vm_map_lookup_locked`
  writes `*out_prot = prot` at `osfmk/vm/vm_map.c:9167`, after `prot = entry->protection`
  (`vm_map.c:9060`) with the needs_copy demotion `prot &= ~VM_PROT_WRITE` (`vm_map.c:9131`).
- **Consumer into PMAP_ENTER**: `prot` flows `vm_fault.c:3684` (call) → `vm_fault_enter` at
  `vm_fault.c:3695` → `PMAP_ENTER_OPTIONS(pmap, vaddr, m, prot, …)` at `vm_fault.c:2590`.
- **Mutations of `*protection` inside `vm_fault_page`**: only two, both demotions, both
  `&= ~VM_PROT_WRITE`: `vm_fault.c:1893` and `:1925`. `grep -n '\*protection' vm_fault.c` returns
  exactly those two plus the two declaration/doc lines. Neither can *set* WRITE.

So a WRITE can only be lost by a demotion upstream of `PMAP_ENTER`, and the three demotion sites are
Apple's (`vm_fault.c:1893/1925`, `vm_map.c:9131`) plus the line the 1012 rung already names
(`vm_fault.c:3794`). All are upstream-identical.

---

## 8. What this means for the 1012 press

- `leo_enter` is **upstream-identical to the HD2 port** — so if the port's `leo_enter` were the
  running code, the lost WRITE would have to be in its *inputs*, and `docs/1012` §"the F1a loss" is
  right that a `prot` without WRITE at the apex must be the cause.
- But the running code is **Apple's stock `pmap_enter_options`**, not `leo_enter`, because
  `BOARD_CONFIG_QSD8250_LEO` is not defined in the D13 build. The 1012 instrument therefore observes
  a dispatch it mis-attributes.
- The single highest-value next step is not a strip-site patch: it is to reconcile the tree/build with
  the assumption. Either (i) narrow the report to say the apex is `pmap_enter_options` and re-derive
  what `(E′)` means when the leaf came from `pmap.c:3252/3441` rather than `leo_pte_build`, or
  (ii) if `leo_enter` *is* intended, widen `osfmk/arm/pmap.c:3181` to the D13 board macro, rebuild,
  and re-run. `tools/check_1012_capture.py:20` and `scripts/press_1012.sh:11` must be corrected either
  way, since both currently assert the LEO dispatch as fact.

---

## 9. Summary table

| # | item | OURS vs upstream | can it explain lost WRITE? |
|---|---|---|---|
| 1 | `pmap_vm_prot_to_page_flags` | identical (whitespace/comment-stripped) | no |
| 2 | `leo_enter`, `leo_pte_build`, all `leo_*` helpers | identical | no |
| 3 | `vm_fault.c` (incl. sites 1-5, RetryFault, `retry_prot`) | identical to Apple13 except the 5 WRSTRIP probes | no |
| 5 | stock `pmap_enter_options` leaf rebuild | ours = UP-HD2 | n/a (this is what runs) |
| 6 | 4570 `pmap_has_prot_policy` / `retry_prot` change | no ARM effect (`return FALSE`) | no |
| 7 | `vm_fault_page` `*protection` producers/consumers | identical | no |
| **4** | **`leo_enter` dispatch is gated on a macro D13 never defines** | **OURS ≠ the build's reality** | **STRONGEST — the instrumentation measures the wrong function** |