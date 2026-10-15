# 1019 — WHAT `__os_alloc_slow`'s `_mach_vm_map` RETURNED: instrumenting the SHIM, not a trap

**This is a rung design, not a build.** No build, no press, no device action, no `git add`, no write to
`out/stage90` while authoring it. It is written on top of the READ of the 1015b capture
(`records/1018-recon.txt`, `a7bafd7`) and `docs/1017-shared-cache-naming.md` (`6dd3b2f`). The one
measurement 1017 left open — and the object 1019 must wrap — was re-derived here **from the extracted
cache by value**, with the command shown.

> **STATUS ON WRITE: 1017 READ (static identification CONFIRMED and extended), 1018-RECON READ.**
> 1017 named `0x3823aa54` = `__os_alloc_slow+0x108` = `udf #254`, reached only when `_mach_vm_map`
> returns non-zero, and wrote §7's "next rung": *"A witness of that call's return value and the map
> arguments (`_mach_vm_map` r0 across the two `blx 0x3823e6e4` sites) would decide whether the
> allocator's ring mapping is being refused."* **1019 is that witness — and the re-derivation below
> changes WHERE it must be taken: not at any trap, at the userspace shim `_mach_vm_map` itself.**

---

## 0. WHAT THE RE-DERIVATION CHANGED (measure first, then design)

1017 §2.5 wrote: *"The two `blx` targets resolve through the image's `__picsymbolstub4` indirect table
to `_mach_vm_deallocate` (stub `0x3823e6d4`, pic idx 58) and `_mach_vm_map` (stub `0x3823e6e4`, pic
idx 59)."* **Re-measured here — both confirmed, and the arithmetic matters:**

```
# the extracted cache is byte-identical to the shipped file (1017 §1.2, re-verified there)
$ python3 -I ...   # section 6
pics addr 3823e334 size 450 reserved1 0 stubsize 16
stub 3823e6c4 pic idx 57 -> _mach_timebase_info
stub 3823e6d4 pic idx 58 -> _mach_vm_deallocate
stub 3823e6e4 pic idx 59 -> _mach_vm_map
```
*(⚠ 1017 §2.5's own "pic idx 58/59" are correct; a first pass at this rung divided the section size by
4 and resolved the wrong symbols. The caught error is recorded because it is the
[[mi4-measurement-defects]] shape.)*

**The load-bearing new fact — the instrument site is NOT a kernel trap.** `_mach_vm_map`
(`/usr/lib/system/libsystem_kernel.dylib`, image `0x381c5000`, symbol `0x381c6618`) is a **userspace
SHIM**, not a trap stub:

```
$ arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x381c6618 /tmp/mvm.bin
381c666a: f7ff e97a  blx 0x381c5960      ; the lowered mach_vm_map syscall
381c66e0: 47b0       blx r6              ; an indirect hook, gated on a runtime pointer
```
and its callee is ARM, not a trap for `mach_vm_map`:

```
$ arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x381c5960 ...
381c596c: e3e0c00e  mvn ip, #14
381c5970: ef000080  svc 0x00000080       ; trap 14 = mach_vm_protect ... (see §0.1)
```

### 0.1 THERE IS NO `mach_vm_map` TRAP ON ARM, ON `-D13` **OR** ON APPLE iOS 7
Measured, not assumed, three ways:
* **The linked image:** `mach_trap_table` (`0x8055d4b4`, `mach_trap_t` = `{int argc; fn; name}`,
  `syscall_sw.h:71-78`). Trap 15 in this build is **`kern_invalid`** (`0x80486408`, `mov r0,#4` =
  `KERN_INVALID_ARGUMENT`). `ios7lab_mach_vm_map_trap` exists in the source
  (`osfmk/ipc/mach_kernelrpc.c:208`) but is compiled behind `#if IOS7_COMPAT_PLATFORM`
  (=`QSD8250_LEO||ARMPBA8`, `ios7_compat.h:5`), which **`-DBOARD_CONFIG_MSM8974_CANCRO` does not open**
  (`tools/build_xnu_arm_kernel.sh:202`) — so it is **not** in this image (grep of the linked `.map`: 0
  hits) and slot 15 stays `kern_invalid`.
* **Apple's own ground truth:** `external/xnu-2050.18.24/osfmk/kern/syscall_sw.c` — the iOS-6 line —
  has **`/* 15 */ MACH_TRAP(kern_invalid, 0, NULL, NULL)`** too. Trap 15 was dropped after iOS 4.
  So **`mach_vm_map` is a MACH MESSAGE** (`_Xmach_vm_map` MIG demux, `mach_vm.defs:337`), reached via
  `mach_msg_trap`.
* **The DYNAMIC corroboration already sits in the 1015b capture — this is the one that turns the
  static fact into a reading.** `entry_note_user_svc` publishes the user's `r12` (the SVC number,
  `entry_stubs.c:7341`). The distinct values are `0xfffffffd`(-3, 43x) and small BSD positives
  (3,5,6,0x174,0x153,0x152,0x147,0x5c) and negatives -4/-5/-6/-26. **`0xfffffff2` = -14 never
  appears** (`grep -o 'usvc_num=0x…' | sort -u`), so the SVC path was never the caller; the allocator
  reached the kernel by `mach_msg`. (⚠ This excludes the SVC path, not trap-15-by-other-means — see §0's
  `# UNMEASURED`.)

**⇒ Therefore 1019's instrument is `--wrap=mach_vm_map` on the MIG demux, which reaches
`__os_alloc_slow`'s very call.** `_Xmach_vm_map` (`out/…kserver_mach_mach_vm_server.o`) calls the
global `mach_vm_map` (`out/xnu_kernel_obj_d13/osfmk_vm_vm_user.o`, `nm`: `T mach_vm_map`) at
`0x8009d3a0` — a **cross-object `bl`**, so `--wrap` sees a genuinely undefined reference and this tree's
`TRACE_LDFLAGS` already wraps many such (`build_entry.sh:169-217`). The wrapper observes the
**result and the arguments the allocator actually passed** — exactly 1017 §7's ask — with **no new
call site in the tree** and **no edit to `mach_vm_map` itself**.

> **`# UNMEASURED` — stated because it bounds the claim.** This design proves `mach_vm_map` is a MIG
> message and NOT a `mach_trap_table[15]` stub. It does **not** derive the numerical MIG message id
> `_mach_vm_map` sends (the ARMv7 userspace `mach_msg` sets no `ip` the way the i386 fast path sets
> `eax`, so the dispatch is inside `mach_msg`'s msgh_id demux; no id is invented here). The rung binds
> the witness **by call identity** (`--wrap=mach_vm_map`), so no id is needed — and §2's control is
> what proves the binding is live.

---

## 1. THE ONE-LINE QUESTION, AND THE PRE-REGISTERED VERDICT CELLS

> **Does `__os_alloc_slow`'s `_mach_vm_map` (the userspace shim whose SHIM-side call is this rung's
> `--wrap` target) RETURN NON-ZERO in pid 1 — i.e. is the shared-cache allocator's `os_alloc_ring`
> mapping being REFUSED by the kernel — and if so, is the refusal `KERN_INVALID_ARGUMENT` (the wire
> shape) or a real `vm_map_enter` failure (KERN_NO_SPACE / a pmap fault)?**

The witness publishes, on the DIRECT channel, per call: `_ret` (the returned `kern_return_t`),
`_addr0`/`_size`/`_mask`/`_flags`/`_prot`/`_src` (the arguments), `_caller` (`__builtin_return_address(0)`
= the shim's continuation), and `_n`. The cells below are **disjoint and jointly exhaustive over
`(_n>=1, _ret, _caller-in-shim?, _frontier-pc)`**.

| # | condition (all read from the new capture) | verdict | what it means |
|---|---|---|---|
| **C-RET-INVAL** | `_n >= 1`, `_ret == KERN_INVALID_ARGUMENT (4)`, `_caller` ∈ the `_mach_vm_map` shim body (`0x381c6618..0x381c66ec`), and the frontier pc is still `0x…aa54` | **THE WIRE SHAPE IS REFUSED** | every `mach_vm_map` from this shim is rejected by argument validation before `vm_map_enter` — the iOS-7 wire (`size/mask/offset` widths) is malformed on this port. This is the SHREG/map_file territory of 998, now reached from pid 1's own allocator |
| **C-RET-NOSPACE** | `_n >= 1`, `_ret == KERN_NO_SPACE (3)`, `_caller` in the shim | **THE MAP FAILED INSIDE `vm_map_enter`** | the wire parsed; the kernel looked for space and did not find it — a `pmap`/`vm_map` space defect, not a wire defect |
| **C-RET-OK-ELSEWHERE** | `_n >= 1` and **every** `_ret == 0`, yet the frontier pc is `0x…aa54` | **THE FAILURE IS NOT `mach_vm_map`** | `_mach_vm_map` returned success on every observed call, so the `udf` is reached by something other than this call's `cmp r0,#0` — 1017 §3.4's "the `udf` is reached from the `_mach_vm_map`-failure branches" is then **falsified for this run** (§5) |
| **C-SHIM-NOT-CALLED** | `_n == 0` **and** the control (§4) is LIVE | **THE SHIM IS NOT ON THE PATH** | the allocator's mapping did not go through `_mach_vm_map` at all under this arm — either the caller is a different `os_alloc` instance or the frontier is upstream of the call |
| **C-CTL (WITHHOLD)** | `_n == 0` **and** the control (§4) is **not** LIVE (no `_ctl_seq`, or no boot self-test row) | **INSTRUMENT_UNSOUND — withhold** | the witness itself did not run; check the `# UNMEASURED` of §0 and the `--wrap` (the 1000 lesson: an absent record whose channel was dead is not evidence) |

**C-CTL is mandatory and comes first.** No cell may be read until §4 proves the witness was live in
the same capture. This is the whole reason C-SHIM-NOT-CALLED is a reading rather than an artefact.

---

## 2. THE INSTRUMENT — `__wrap_mach_vm_map`

**What to wrap, by value.** `mach_vm_map` (`osfmk/vm/vm_user.c:921`), signature
`(vm_map_t target_map, mach_vm_offset_t *address, mach_vm_size_t initial_size, mach_vm_offset_t mask,
int flags, ipc_port_t port, vm_object_offset_t offset, boolean_t copy, vm_prot_t cur_protection,
vm_prot_t max_protection, vm_inherit_t inheritance)`. On `__arm__` `mach_vm_offset_t`/`mach_vm_size_t`
are 32-bit (`vm_types.h`, per `ios7_compat.h:27-29`), so the observable arity is 11 words.

**The wrapper** goes in `src/entry/entry_trace.c` beside `__wrap_kernel_memory_allocate`
(`entry_trace.c:1255-1273` — the closest existing precedent: it wraps a kernel function, records the
**return value**, and reads its **arguments**). Body shape:

```c
uint32_t __real_mach_vm_map(void *, uint32_t *, uint32_t, uint32_t, int, void *,
                            uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t __wrap_mach_vm_map(void *map, uint32_t *address, uint32_t size, uint32_t mask,
                            int flags, void *port, uint32_t offset, uint32_t copy,
                            uint32_t cur_prot, uint32_t max_prot, uint32_t inherit)
{
    uint32_t ret = __real_mach_vm_map(map, address, size, mask, flags, port,
                                      offset, copy, cur_prot, max_prot, inherit);
    /* 1019: publish ret + args + caller on the DIRECT channel, bounded by MVMAP_LIVE_MAX */
    return ret;
}
```

* **`_ret`, `_addr`, `_size`, `_mask`, `_flags`, `_prot` (cur) and `_max` (max) are the arguments the
  allocator passed** — `_prot`/`_max` are the "map arguments" 1017 §7 names.
* **`_caller = (uint32_t)(uintptr_t)__builtin_return_address(0)`** is the single most load-bearing
  field: it distinguishes the allocator's call from any other `mach_vm_map` in the boot by **the
  continuation inside `_mach_vm_map`'s shim** (`0x381c666e` on the lowered path; `0x381c66b8` on the
  fall-through — **both `# UNMEASURED` until read from the linked image**, they are the cache's, and
  the cache is what §6 shows how to read).
* **`mach_vm_map` must become an undefined reference in the link** for `--wrap` to bite: it is defined
  in `osfmk_vm_vm_user.o` and called cross-object from the MIG demux object, so `--wrap=mach_vm_map`
  appended to `TRACE_LDFLAGS` (`build_entry.sh:169-217`, beside `--wrap=sleh_undef` at `:197`) renames
  both, exactly as `--wrap=kernel_memory_allocate` already does.

**Why NOT the two alternatives the brief named, by value:**
* **Not `--wrap` on the shim** (`_mach_vm_map` / `_mach_vm_deallocate`): they live in the **shared
  cache** (userspace), compiled long before this link; `--wrap` cannot reach them. The kernel-side
  `mach_vm_map` is the only wrap-able point on the path, and it is the same call.
* **Not a mach-trap hook** (`mach_syscall_trace`, `model_dep.c:714`, called at `traps_lo.s:221` for
  every MACH trap): it sees `mach_msg`/`semaphore_wait`, and the `_FAST` wire adapters, but **not**
  `mach_vm_map` — because there is no such trap (§0.1). A trap hook would observe the wrong object and
  the wrong arg widths.

---

## 3. THE CHANNEL, AND WHY

The seven keys ride **`entry_write_kv` — the DIRECT channel** (not the `entry_kv`/`entry_live_write`
capped channel that `__wrap_kernel_memory_allocate`'s `t268_*` keys and `__wrap_sleh_undef` used).
The reason is `records/1018-recon.txt` §0-§2 exactly: the frontier is reached ~4000 records past
`xnu_live_capped` (`:29963`), and **a witness whose keys stop at the cap cannot read the frontier it
exists for** — the 1018 defect, and the reason `STAGE90_XNU_UNDEF_DIRECT` exists. `entry_write_kv`
(`entry_stubs.c:4476`) has no record-count gate; its only bound is the console's
`max = 0x00200000u - 12u` (`:4019`), which the 1015b capture uses ~1.5 MB of 2 MB.

`entry_trace.c` already declares `extern void entry_kv(...)` (`:148`); this rung adds the
`entry_write_kv` declaration the same way `__wrap_sleh_undef` did (`entry_trace.c:5543`).

---

## 4. THE CONTROL (both halves, mechanical — the 1018 §1.2 discipline)

A witness that can only ever be silent proves nothing. The arm carries **two** controls and the checker
requires **both**:

1. **A boot self-test row.** `entry_trace.c`'s existing `__wrap_sleh_undef` self-test or a new one-shot
   `xnu_live_mvmap_selftest=0x1` written at `stage90_xnu_arm_*` startup — proving **the console and the
   wrapper object are live** before any allocator call. (The existing analogue is
   `stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x1`, capture `:3480`.)
2. **A `_seq`/`_n` row.** `xnu_live_mvmap_n >= 1` — proving **this wrapper was entered**, not merely
   linked.

`ctl_live = (selftest row present) AND (_n >= 1 row present)`. With it, C-SHIM-NOT-CALLED (`_n==0`)
WITHHOLDS (§1 C-CTL): the honest "the shim is not on the path" can only be reached when the witness is
proven entered yet never sees the allocator's call. Without it, `_n==0` is indistinguishable from a
wrong `--wrap` — which is the 1000 lesson verbatim.

**A third control that is nearly free and should be used:** the **same** wrapper will see the kernel's
*other* `mach_vm_map` callers (MIG demux for any process). A `_caller` value **outside** the shim body
written by the same wrapper is a **positive** control that the wrapper's channel works even if the
allocator's specific call is absent — strengthen or replace control (1) with it if the self-test row is
hard to place.

---

## 5. THE FALSIFICATION — WHAT WOULD REFUTE 1017

1017 §3.2/§3.4 assert, statically: *"on the success path the function returns normally at
`0x3823aa52` and never reaches `0x3823aa54`… a sampled pc that is the trap tail is consistent with
`os_alloc`'s `_mach_vm_map` having failed."* **1019 refutes that identification if and only if** the
witness is proven live (§4) and **C-RET-OK-ELSEWHERE** holds: every `mach_vm_map` from the shim
returned `KERN_SUCCESS`, yet pid 1's frame still sits on `0x…aa54`.

Candidates then (all testable, none papered over — the 1013c shape, "a walker read as device truth"):
* the sampled `pc` is a **stale** saved value, not the executing instruction;
* the frontier `0x…aa54` is reached from **`_mach_vm_deallocate`**'s path or another branch, not the
  `_mach_vm_map`-failure branch (1017 §3.4 assumed the two failure branches are the only source);
* the branch at `0x3823a9b0`/`0x3823aa26` is taken for a value that is not the shim's return (register
  reuse between `blx` and `cmp`).

Conversely, **C-RET-INVAL or C-RET-NOSPACE CONFIRMS 1017's identification dynamically** and moves the
frontier from "a `udf` we named" to "a map we can fix" — the first time in this line the frontier is an
*argument* rather than a *pc*.

⚠ **A reading that must NOT be mistaken for falsification** ([[mi4-measurement-defects]]): `_n == 0`
with a dead control is **C-CTL**, a withhold, never §5. The whole control apparatus exists for this one
boundary.

---

## 6. EXACT COMMANDS (reproducible — every value above is from these)

```bash
CACHE=/tmp/dyld-extract/dyld_shared_cache_armv7       # byte-identical to rootfs.hfs @0x455d000 (1017 §1.2)

# (1) the stub-index arithmetic that 1017 §2.5 got right and a first pass here got wrong:
#     __picsymbolstub4 is 16-byte-stride, reserved1=0, so idx = (stub - 0x3823e334)/16.
python3 -I - <<'EOF'
import struct
d=open('/tmp/dyld-extract/dyld_shared_cache_armv7','rb').read(); mh=0xc239000
ncmds=struct.unpack_from('<I',d,mh+16)[0]; off=mh+28
for i in range(ncmds):
    cmd,csz=struct.unpack_from('<II',d,off)
    if cmd==0x2: symtab=struct.unpack_from('<IIII',d,off+8)
    if cmd==0xb: dysym=struct.unpack_from('<'+'I'*18,d,off+8)
    if cmd==0x1:
        ns=struct.unpack_from('<I',d,off+48)[0]; so=off+56
        for s in range(ns):
            sn=d[so:so+16].split(b'\0')[0].decode()
            if sn=='__picsymbolstub4':
                a,sz=struct.unpack_from('<II',d,so+32); r1,stubsz=struct.unpack_from('<II',d,so+60)
                pics=(a,r1,stubsz)
            so+=68
    off+=csz
pa,r1,stubsz=pics; indoff=dysym[12]; symoff,nsyms,stroff,strsize=symtab
def name(v):
    ns=struct.unpack_from('<I',d,symoff+v*12)[0]; return d[stroff+ns:d.index(b'\0',stroff+ns)].decode()
print('stubsize',stubsz)
for stub in (0x3823e6d4,0x3823e6e4):
    idx=(stub-pa)//stubsz + r1
    print('stub %08x -> %s'%(stub,name(struct.unpack_from('<I',d,indoff+idx*4)[0])))
EOF
# expect: 3823e6d4 -> _mach_vm_deallocate ;  3823e6e4 -> _mach_vm_map

# (2) the SHIM `_mach_vm_map`, its call, and its trap number (image libsystem_kernel @0x381c5000)
python3 -I -c "d=open('$CACHE','rb').read(); open('/tmp/mvm.bin','wb').write(d[0x381c6618-0x2c000000:][:0xd4])"
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x381c6618 /tmp/mvm.bin
# -> blx 0x381c5960 (lowered syscall); the callee's `mvn ip,#14; svc #0x80`

# (3) there is no mach_vm_map TRAP: trap 15 in the LINKED image
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf \
  --start-address=0x80486408 --stop-address=0x80486430   # kern_invalid -> mov r0,#4
# and Apple's ground truth:
sed -n '/mach_trap_table\[MACH_TRAP_TABLE_COUNT\]/,/^};/p' \
  external/xnu-2050.18.24/osfmk/kern/syscall_sw.c | grep '^/\* 15 \*/'
# -> /* 15 */ MACH_TRAP(kern_invalid, 0, NULL, NULL)

# (4) the wrap sees a cross-object reference: the MIG demux calls mach_vm_map
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf \
  --start-address=0x8009d2f8 --stop-address=0x8009d3c0 | grep mach_vm_map
# -> 8009d3a0: bl 8006aedc <mach_vm_map>
```

---

## 7. BY-NAME PLUMBING (the seven places — named exactly, per `docs/1015-design.md` §8d)

Switch: **`STAGE90_XNU_MVMAP_WITNESS`** (default 0; validated 0/1 at its own assignment).

1. **The guard `#if`** in `src/entry/entry_trace.c` around `__wrap_mach_vm_map`'s key block
   (`#if defined(STAGE90_XNU_MVMAP_WITNESS) && STAGE90_XNU_MVMAP_WITNESS` — `defined()` first,
   [[mi4-off-option-two-spellings]]).
2. **The `-D`**: `STUB_DEFINES_TRACE+=(-DSTAGE90_XNU_MVMAP_WITNESS="$MVMAP_WITNESS")` beside
   `build_entry.sh:1558` (the `STAGE90_XNU_UNDEF_DIRECT` precedent, `:1535-1558`); plus
   `MVMAP_WITNESS=${STAGE90_XNU_MVMAP_WITNESS:-0}` with its 0/1 `case` at `:1535-1540`.
3. **`--wrap=mach_vm_map`** appended to `TRACE_LDFLAGS` (`build_entry.sh:169-217`, beside
   `--wrap=sleh_undef` at `:197`), gated on `ENTRY_TRACE -eq 1 && MVMAP_WITNESS -eq 1`.
4. **`ENTRY_ARM_KEYS`** (`build_entry.sh:2035`) — add `STAGE90_XNU_MVMAP_WITNESS`.
5. **The record writer** — the `echo "STAGE90_XNU_MVMAP_WITNESS=$MVMAP_WITNESS"` beside
   `build_entry.sh:40188` (the `STAGE90_XNU_UNDEF_DIRECT` echo), which writes
   `out/stage90/xnu_arm_entry-config.txt`.
6. **`scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS`** (`:716`) — add
   `STAGE90_XNU_MVMAP_WITNESS` at the tail (untracked by the 882 grace logic; the 1015/1018 addition
   pattern at `:761/:775`).
7. **The checker** — `tools/check_1019_capture.py`, modelled on `tools/check_1018_capture.py`
   (`:88` `UNDEF_BAND`), enforcing §4's `ctl_live` (boot/self-test row **and** `_n>=1`) and the §1 cells.

**Plus one by-value build clause** at the link site (the 1018 clauses at `build_entry.sh:31249-31303`
are the pattern): refuse a link whose `mach_vm_map` is **not** renamed — i.e. `__wrap_mach_vm_map` is
absent from the image (the `--wrap` was lost) or `__real_mach_vm_map` is unresolved — because an image
whose wrapper did not link produces `xnu_live_mvmap_*`'s absence for the wrong reason.

---

## 8. VERIFICATION PLAN (`nm`/`objdump` clauses that prove the instrument is in the LINKED image)

Read **by value, bound to the function's own base** ([[mi4-linked-code-order-is-not-source-order]]),
never a bare grep count:

1. `arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="__wrap_mach_vm_map"'` — exactly one
   match (the object defined it and `--wrap` linked it).
2. `arm-none-eabi-nm` — `mach_vm_map` **still defined** (`T`), `__real_mach_vm_map` resolves to it (the
   `__real_` alias exists), and **no remaining direct `bl mach_vm_map`** from `_Xmach_vm_map`
   (`objdump -d` around `0x8009d3a0`: the `bl` now targets `__wrap_mach_vm_map`).
3. `objdump -d` under `<__wrap_mach_vm_map>:` — count `bl entry_write_kv` **>= 7** with
   `STAGE90_XNU_MVMAP_WITNESS=1` and **0** with `=0` (the OFF image must be the pre-1019 object
   byte-for-byte; the `#if/#else` never a widened constant — the 1018 clause's exact discipline,
   `build_entry.sh:31271/31274`).
4. `objdump -d` — the wrapper's `bl __real_mach_vm_map` and the **`mov r4,r0`(or equivalent) that
   captures the return** are present: the rung's whole claim is that the witness observes the
   **result**, and a wrapper that dropped it would publish arguments only.
5. The **`_caller` field is a real continuation, not 0**: cross-check the published `_caller` against
   the shim's own instruction range (`0x381c6618..0x381c66ec`) — the value is `# UNMEASURED` at design
   time and is read from the linked image at first build.

---

## 9. FILE:LINE AND RECORD ANCHORS

**Prior rungs:** `docs/1017-shared-cache-naming.md` (§2.5 the two stubs, §3.2/§3.4 the failure-branch
claim, §7 the ask this rung answers); `records/1018-recon.txt` (§0-§2 the cap and the two channels);
`docs/1018-design.md` (§1.2 the two-half control, §5 the measurement-defect self-check).

**The instrument site (read by value, §6):** `vm_user.c:921` (`mach_vm_map`); the cross-object `bl` at
linked `0x8009d3a0` from `_Xmach_vm_map`
(`out/…kserver_mach_mach_vm_server.o`); the shim `/usr/lib/system/libsystem_kernel.dylib`
`_mach_vm_map` `0x381c6618`; `osfmk/ipc/mach_kernelrpc.c:208` (`ios7lab_mach_vm_map_trap`, **not
compiled here**); `osfmk/arm/ios7_compat.h:5` (`IOS7_COMPAT_PLATFORM`); `tools/build_xnu_arm_kernel.sh:202`
(`-DBOARD_CONFIG_MSM8974_CANCRO=1`, which does not open that gate);
`external/xnu-2050.18.24/osfmk/kern/syscall_sw.c` (trap 15 = `kern_invalid`, Apple's own iOS-6 line).

**The channel and the cap:** `entry_stubs.c:4476` (`entry_write_kv`), `:4019` (console bound),
`:4254`/`:4441` (the cap and `entry_live_write`); `entry_trace.c:148` (`extern entry_kv`),
`:1255-1273` (`__wrap_kernel_memory_allocate`, the wrapper pattern), `:5537-5603` (`__wrap_sleh_undef`).

**The plumbing (this rung's seven):** `build_entry.sh:197` (`--wrap=sleh_undef`), `:169-217`
(`TRACE_LDFLAGS`), `:1535-1558` (`UNDEF_DIRECT` assignment + `STUB_DEFINES_TRACE`), `:2035`
(`ENTRY_ARM_KEYS`), `:31249-31303` (the 1018 by-value clauses), `:40188` (the echo line);
`scripts/preflight_boot_check.sh:716` (`ENTRY_CFG_KEYS`), `:761/:775` (the 1018 tail addition);
`tools/check_1018_capture.py:88`.

**The capture (control + the SVC corroboration):** `out/stage90/captures/recovered-unknown-arm-20261015-163856-last_kmsg.txt`
— `:3480` the undef boot self-test, `:29963` `xnu_live_capped`, `:33890-33897` the last `usvc` block
(`_num=0xfffffffd`), `:33976-34009` the stall samples (`pc=0x3b04aa54`).

**No build, no press, no device action, no `git add`, and no write to `out/stage90` was performed while
authoring this file.**