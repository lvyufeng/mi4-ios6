# D13 iOS 7 mach-trap wire defect — traps -10/-12/-14 (994 design + staged fix)

Written 2026-10-12 by the vmwire lane for experiment 994. Every claim below is a machine reading with a
file:line or an address, or is explicitly marked **inference**. Sources read by value: the D13 tree
`external/xnu-hd2-darwin13/xnu` (OUR build tree; paths relative to it), the linked image
`out/stage90/xnu_arm_entry.elf` (the 993 arm), the iOS 7 dyld disassembly in `/tmp/dyld_objd.txt`
(unslid cache VAs), and the standalone ON-shape rehearsal `/tmp/vmwire_on_sim.elf` (two-TU build, NOT the
kernel). The causal link to 993's press (launchd halted at `_dyld_fatal_error` from dyld's OOM path) is
the other lanes' forensics (`d13-dyld-halt-callers.md`, `d13-user-udf-retry-mechanism.md`); this file
does not re-derive it.

## 1. The defect, by value

### 1.1 The wire (what iOS 7 userspace actually builds)

A mach trap's arguments are the SAVED-REGISTER FRAME (`osfmk/arm/traps_lo.s:177-194`: `swi_mach` loads
`mach_trap_table[i]`, does `mov r0, r8`, `bx r1` - r8 is the saved-state pointer - and on `__arm__`
`osfmk/kern/syscall_sw.h:96-99` defines `MACH_TRAP` as `{arg_count, (fn), #name}` with the munger
arguments DROPPED, so the trap function receives the raw frame). dyld's own MIG stubs say how wide each frame is,
and `mvn r12,#9 / #11 / #13` names the trap:

| trap | stub (unslid VA) | bytes | reading |
|---|---|---|---|
| -10 | `__kernelrpc_mach_vm_allocate_trap` 0x2be1b410 | `mov r12,sp; push {r4,r5}; ldr r4,[r12]; mvn r12,#9` | **5 words**, 5th lifted into r4 |
| -12 | `__kernelrpc_mach_vm_deallocate_trap` 0x2be1b42c | `mov r12,sp; push {r4,r5}; ldr r4,[r12]; mvn r12,#11` | **5 words**, 5th lifted into r4 |
| -14 | `__kernelrpc_mach_vm_protect_trap` 0x2be1b448 | `mov r12,sp; push {r4,r5,r6,r8}; ldm r12,{r4,r5,r6}; mvn r12,#13` | **7 words**, words 5-7 lifted |

The caller side fixes the field ORDER. `_mach_vm_allocate` (0x2be1baae) does `ldr r6,[r7,#8]` (its own
5th argument) then `str r6,[sp]` before `blx` the stub, and `_vm_allocate` (0x2be1bb70) builds the frame
as `str r3,[sp]` (flags -> word 5), `movs r3,#0` (size_hi = 0 -> word 4), `add r1,sp,#8` (word 2 = user
pointer), so:

```
trap -10  { target, &address64, size_low, size_high, flags }                      5 words
trap -12  { target, address_low, address_high, size_low, size_high }              5 words
trap -14  { target, address_low, address_high, size_low, size_high,
            set_maximum, new_protection }                                         7 words
```

`_vm_deallocate` (0x2be1bba0) confirms the -12 order: `mov r3,r2` (size -> word 4), `movs r2,#0; str
r2,[sp]` (word 5 = 0), `movs r2,#0` (word 3 = 0), `bl _mach_vm_deallocate` - i.e.
`(target, address, 0, size, 0)`.

### 1.2 The kernel struct (why it is one word short each)

`osfmk/mach/arm/vm_types.h:107-109` (under `#else /* !__LP64__ */`, comment "We target iOS 4.3 for
compatibility") makes `mach_vm_address_t` and `mach_vm_size_t` `uint32_t` **on this build**, and
`osfmk/mach/mach_traps.h`'s `PAD_ARG_`/`PAD_` contribute nothing for a 32-bit type on `__arm__`. So the
native argument structs - whose comments say "Total: 5" / "Total: 7" - are really:

```
_kernelrpc_mach_vm_allocate_trap_args    { target, addr, size, flags }                  4 words
_kernelrpc_mach_vm_deallocate_args       { target, address, size }                      3 words
_kernelrpc_mach_vm_protect_args          { target, address, size, set_maximum,
                                           new_protection }                             5 words
```

Each native body therefore reads the wire **one field early**.

### 1.3 The shipped image proves it (the 993 arm)

`mach_trap_table` @0x80558c14 already points at the real trap functions - entries
`{5, 0x80477b64, "_kernelrpc_mach_vm_allocate_trap"}`, `{5, 0x80477bfc, "...deallocate..."}`,
`{7, 0x80477c54, "...protect..."}` - so only the BODIES are wrong:

- `_kernelrpc_mach_vm_allocate_trap` @0x80477b64: `ldr r3,[r5,#12]` @0x80477bcc is passed as the flags
  word to `bl mach_vm_allocate` @0x80477bd4. Wire word 4 is size_high, not flags; the real flags sits at
  #16. dyld's allocation therefore runs **without `VM_FLAGS_ANYWHERE`**.
- `_kernelrpc_mach_vm_deallocate_trap` @0x80477bfc: `ldmib r5,{r1,r2}` @0x80477c38 loads address from #4
  and **size from #8, which is address_high** - on a 32-bit process that is 0, so the deallocate frees
  nothing.
- `_kernelrpc_mach_vm_protect_trap` @0x80477c54: `ldmib r5,{r1,r2,r3,r5}` @0x80477c90 + `str r5,[sp]`
  pass #12/#16 as set_maximum/new_protection - which are really size_low/size_high.

## 2. The fix (staged)

**New switch: `STAGE90_XNU_IOS7_TRAPS`.** No new config key, no boot argument: when it is defined as 1
the three traps parse the wire at its real width, always. When it is undefined the two files compile the
tree's original text (every appended clause is `|| (defined(...) && ...)`, so `defined()` is evaluated
first and the identifier is never evaluated undefined - `mi4-off-option-two-spellings`).

`osfmk/arm/ios7_compat.h` (after `ios7_compat_vmabi_enabled`):

```c
static inline int ios7_traps_enabled(void)
{
#if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS
    return 1;
#else
    return ios7_compat_vmabi_enabled();
#endif
}
```

`osfmk/ipc/mach_kernelrpc.c`, five edits (`patch_d13_ios7_vmwire.py`, line numbers post-patch):

| # | lines | edit |
|---|---|---|
| 1 | :39 | the FILE-LOCAL gate line opens under the switch too, so the ios7lab wire structs and `ios7lab_vm_allocate` become reachable on CANCRO. **This is one per-file line, never the `IOS7_COMPAT_PLATFORM` macro** - that macro is referenced 32 times across 12 files (`syscall_sw.c`, `kern_fork.c`, `kern_exec.c`, `kern_descrip.c`, `unix_syscalls.c`, `status.c`, `ipc_kobject.c`, `vm_user.c`, `vm_map.c`, `vm_shared_region.c`, `vm_unix.c`, `mach_kernelrpc.c`), and widening it would wake exec/fork/ipc/shared-region behaviour for a one-wire fix |
| 2 | :56-59, :244 | an inner `#if IOS7_COMPAT_PLATFORM` around the port/vm_map lab adapters, so a CANCRO switch-ON build stays the VM wire only (the port traps are not part of this fix) |
| 3 | :279-350 | the two new wire structs + `ios7lab_vm_deallocate` / `ios7lab_vm_protect`, gated on the switch alone (ARMPBA8/LEO images unchanged even with the switch set) |
| 4-6 | :492, :522, :546 | each trap body routes through its adapter when `ios7_traps_enabled()` |

The adapters are board-neutral C, the same shape as the existing `ios7lab_vm_allocate` (:247): resolve
the target port, require it to be the current task, then

- **reject a nonzero high word with `KERN_INVALID_ARGUMENT`** (`address_high`/`size_high`; trap -10's
  adapter additionally requires a zero high half of the user-supplied address when `VM_FLAGS_ANYWHERE`
  is clear). A 32-bit address space cannot name anything above 4 GiB, so this is a refusal, not a silent
  truncation - the alternative (truncate) is how a wire defect turns into a VM defect.
- call the same primitives the native path calls (`mach_vm_allocate` / `mach_vm_deallocate` /
  `mach_vm_protect`), with the low words.
- carry `typedef char ios7lab_vm_{deallocate,protect_trap}_wire_size[...==20 ? 1 : -1]` (and `==28` for
  protect) compile-time wire-size asserts.
- report once per call for the first 8/12 calls, in the house format:
  `IOS7LAB vm_allocate64 size=%08x:%08x flags=%x result=%d address=%08x`,
  `IOS7LAB vm_deallocate64 address=%08x:%08x size=%08x:%08x result=%d`,
  `IOS7LAB vm_protect64 address=%08x:%08x size=%08x:%08x set_max=%x new_prot=%x result=%d`.
  (`printf`, not `kprintf`: kprintf is a no-op macro in one of the sibling files - `osfmk/arm/pmap.c:409`
  - and this file's markers must reach the boot log.)

The wire shapes follow the fork's established LEO structs (`osfmk/ipc/ios7lab_vm_range_wire.h`:
`ios7lab_vm_range_wire` 20 B, `ios7lab_vm_protect_wire` 28 B) but are defined here under non-colliding
names (`ios7lab_vm_deallocate_wire`, `ios7lab_vm_protect_trap_wire`), because that header is in the same
file only under `BOARD_CONFIG_QSD8250_LEO` and a same-named struct would be a redefinition if the switch
were ever set on LEO.

## 3. Switch semantics and the build

- OFF: `mach_kernelrpc.c`'s live source is byte-identical to HEAD's blob (verified by regenerating from
  `git show HEAD:...` and `cmp`) and the added text is inside false conditionals - a preprocessor walk of
  the staged file under {CANCRO,ARMPBA8} x {OFF,ON} shows CANCRO+OFF compiles nothing new, ARMPBA8+OFF
  compiles exactly the original set, CANCRO+ON compiles the wire structs + three adapters + three call
  sites and NOT the port traps.
- ON: on ARMPBA8, `ios7_traps_enabled()` returns 1 unconditionally, i.e. the switch bypasses the
  `ios7lab_vmabi` boot-arg check at :489. **Inference: harmless (nobody builds ARMPBA8 with this
  switch), but it is a semantic change, not a no-op, and is recorded here so it is not discovered as
  "one value, two definitions" later.**
- Two build layers: the two C files are POOL objects, `traps_lo.s` is the ASSEMBLY layer and belongs to
  the swi lane. `tools/stage_d13_ios7_traps.sh` (994's driver, build-plumbing lane) is the entry point;
  `src/entry/build_entry.sh` only LINKS `out/xnu_kernel_obj_d13/*.o`, so the pool must be rebuilt or the
  patch is invisible in the image.

## 4. Tools

- `tools/patch_d13_ios7_vmwire.py <xnu-tree-root>` - stage-only, idempotent. All eight edits are
  PLANNED first and refused unless every anchor occurs exactly once (or the staged copy already exists);
  a refusal writes NOTHING (no partial application). Re-run on a staged tree: "already staged", no write.
- `tools/check_d13_ios7_vmwire.py [image] [--off]` - by-value checker on a LINKED image.
  Default (ON) asserts, from the bytes: (1) `mach_trap_table` entries 10/12/14 still carry {5,5,7} and
  point at the real trap functions with their name words intact; (2) each trap routes through its adapter
  (`bl`/tail-`b` to the symbol, or - if the static adapter was inlined - the trap body itself); (3) the
  wire's EXTRA word is read through the args base register at #16 (traps -10/-12, flags/size_high) and
  #24 (trap -14, new_protection) - offsets the native structs cannot reach (their largest are #12/#8/#16);
  (4) the three report strings are in the image; (6) traps 15/24/25/41/42 are still `kern_invalid`
  (§6.1 - checked in both modes). `--off` asserts the mirror: no adapter symbol, no string, and the
  DEFECT offsets (#12/#8/#16) with nothing reading #16/#16/#24. The two modes assert disjoint offsets,
  so an image left OFF fails ON and vice versa.

Rehearsals actually run (2026-10-12):

- **994's real switch-ON image** (`out/stage90/xnu_arm_entry.elf`, built 18:03, entry bin sha256[:12]
  `8b4c9a3e`): ON **PASS** - `traps 15/24/25/41/42 are kern_invalid`; each adapter is INLINED (no
  function symbol - `nm` on `osfmk_ipc_mach_kernelrpc.o` shows only the three `ios7lab_vm_*.logged`
  `.bss` counters, at 0x8085da08/0c/10 in the image), and each trap body reads its extra word
  (#16 at 0x80477bc8 flags, #16 at 0x80477cc8 size_high, #24 at 0x80477d98 new_protection) - i.e.
  flags & VM_FLAGS_ANYWHERE is taken. `--off` against the same image: **FAIL** (strings present, fixed
  offsets present) - the ON image cannot pass the OFF rehearsal.
- The frozen 993 image (`out/stage90/frozen/armed-d13-1d5cf20e/`): `--off` **PASS** (defect by value -
  `#12 at 0x80477bcc`, `#8 at 0x80477c38`, `#16 at 0x80477c90`, nothing at #16/#16/#24), default ON
  **FAIL** - the rehearsal pair does not regress.
- ON against `/tmp/vmwire_on_sim.elf` (a standalone two-TU ARMv7 ELF carrying the same wire structs,
  adapters, trap bodies and a `mach_trap_table` with entry 0 = `kern_invalid`; callees external so -O2
  cannot fold the reads): **PASS** - adapters inlined, reads at #16/#16/#24 through r4 (an r0 copy),
  the three strings, the deferral.
- ON against the sim with trap 15's row opened instead of `kern_invalid`: **FAIL** on check 6 - the
  deferral check has a negative control.
- ON against the 993 image: **FAIL** (negative control for checks 1-4).

Two checker defects were found by rehearsals, both fixed, both instances of the project's classes:
(a) the first `symbols()` parsed `objdump -t` by fixed field position and returned nothing on the
real image - a measurement defect; (b) the adapter lookup matched `ios7lab_vm_allocate.logged`, a
`.bss` DATA symbol of the adapter's `static` counter, and reported "not routed" on the real 994 image
whose data flow was correct - a stand-in of the right name and the wrong kind. The fix keys
"the adapter" on a disassembly body (`adapter_function()`), never on a name match.

## 5. sha256[:12] of the touched files

| file | before (HEAD blob) | after (staged) |
|---|---|---|
| `osfmk/ipc/mach_kernelrpc.c` | `1126ac35b590` | `842e2571a484` |
| `osfmk/arm/ios7_compat.h` | `420d9d9e9a50` | `079c7a17f827` |

(`external/` is a gitignored nested checkout; `git show HEAD:<path>` in `external/xnu-hd2-darwin13`
reproduces the "before" hashes and the patcher reproduces the "after" files byte for byte.)

## 6. Edits NOT needed in files this lane does not own

- `osfmk/kern/syscall_sw.c:121-125`: **no edit.** Traps 10/12/14 already carry arg counts 5/5/7, and on
  `__arm__` `MACH_TRAP` (`osfmk/kern/syscall_sw.h:96-99`) drops the munger arguments entirely, so the
  frame reaches the trap function unmodified. The fix is entirely inside the bodies.

### 6.1 The rung's deferral: traps 15/24/25/41/42 are NOT opened (by value)

`osfmk/kern/syscall_sw.c:126-170` holds five more `#if IOS7_COMPAT_PLATFORM` rows - trap 15
(`ios7lab_mach_vm_map_trap`, 8 wire words), traps 24/25 (`ios7lab_mach_port_construct/destruct_trap`),
traps 41/42 (`ios7lab_mach_port_guard/unguard_trap`). `IOS7_COMPAT_PLATFORM` is 0 on CANCRO
(`osfmk/arm/ios7_compat.h:5`), and this fix widens **only** `mach_kernelrpc.c`'s own line 39, so
`syscall_sw.c` is compiled untouched and those five rows stay the `#else` branch. Measured in the 993
image (`mach_trap_table` @0x80558c14, entry stride 12 B): entries 15/24/25/41/42 all read
`{arg_count=0, fn=0x804833d8, name="kern_invalid"}` - the same triple as entry 0.

They are deferred deliberately: no evidence on the measured crash path touches them, and their bodies
are LEO lab reimplementations with deliberate isolation/QoS bypass (the same reasoning that forbids
adopting `ios7leo_*` observation counters on CANCRO). **Inference: they will be needed eventually for
full iOS 7 userspace, but they are not this rung.** The checker asserts the deferral in both switch
modes (check 6), keyed on entry 0's own `{fn, name}` - never on a symbol name.

- `osfmk/mach/mach_traps.h:590-614`: **no edit.** The native structs stay the control ABI; with the
  switch ON they are never dereferenced on the routed path, and with it OFF they are what the tree
  always compiled.
- `osfmk/arm/traps_lo.s`: owned by the swi lane (994's timebase change), untouched here.

## 7. NON-CLAIMS

- No build was run and no device was pressed by this lane. Nothing here claims the linked image changes
  until the pool is rebuilt with `-DSTAGE90_XNU_IOS7_TRAPS=1`; the ON-mode checker has NOT been run
  against a real rebuilt image (only against the standalone rehearsal ELF).
- The wire shapes are read from dyld's stubs and callers, not from Apple documentation.
- The adapters' `KERN_INVALID_ARGUMENT` on a nonzero high word is a design choice, not a measurement: no
  observed userspace call passes a nonzero high word, and none can name memory in a 32-bit space.
- Routing traps -10/-12/-14 through the adapters changes what the KERNEL's own traps accept: the control
  ABI (4-word) callers inside this tree would now be misread. That is the point of the switch - the
  kernel's internal callers use the `mach_vm_*` primitives directly, never the traps - but it is a
  NON-CLAIM that anything in-tree still uses the 4-word form.
- `mach_vm_allocate`'s return path for trap -10 still copyin/copyout an 8-byte address cell (the
  existing adapter's behavior); if userspace ever passes a 4-byte cell there, that is unchanged from
  the ARMPBA8 lab's proven path, not verified here.
- The three `printf` markers are bounded (first 8/12 calls) and are the ONLY runtime reading; no counter
  is exported to a sysctl, and a boot that never calls these traps prints nothing.

## 8. Owed

1. ~~Rebuild the pool with the switch + link, then run the checker ON against the new image.~~
   **DONE** (18:03 build, entry bin sha256[:12] `8b4c9a3e`): ON **PASS**, `--off` **FAIL** on it, and
   the frozen 993 pair still behaves (`--off` PASS / ON FAIL).
2. Press (the operator's): expect `IOS7LAB vm_allocate64 ...` markers with a nonzero `flags` for
   dyld's `__simple_salloc` (`VM_FLAGS_ANYWHERE`), and no repeat of the 993 halt.