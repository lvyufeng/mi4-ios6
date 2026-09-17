# Experiment 103 — XNU's entry point assembles, and the "absent build configuration" is now a symbol list

Date: 2026-09-17
Commit under test: `8900206`, plus the changes described below
Artifacts: `stages/stage90/xnu_arm_assemble.sh`, `stages/stage90/xnu_arm_boot/assym.s`
Host only — nothing in this experiment runs on the device.

## The result

**`osfmk/arm/start.s` — XNU's real `_start`, unmodified — assembles with clang 14**, and the
script that does it is in the tree and reproducible.

```
== osfmk/arm/start.s ==
assembles: yes
defined:   __start, _arm_init_tramp, _resume_idle_cpu,
           join_start, join_start_1, invalidate_tte, mapveqp, mapveqpL2,
           cleanflushline/way, invall2flushline/way, arm_init_tramp_addr
undefined: _arm_init, _arm_init_cpu, _arm_init_idle_cpu,
           _EntropyData, _ExceptionVectorsBase, _ExceptionVectorsTable,
           _fiqstack_top, _intstack_top, _gPhysBase, _gPhysSize, _gVirtBase,
           _kdebug_enable, _fleh_reset/undef/swi/prefabt/dataabt/addrexc/irq/decirq
```

That undefined list is the finding. The roadmap has said since 2026-09-16 that Phase 4's blocker is
"the source tree is complete and the build configuration is absent", which is true and does not
say what to do. It is now a list of twenty symbols, each with a known home:

- **`_fleh_*` (8) and `_ExceptionVectorsTable`** are in `osfmk/arm/locore.s` — the same assembly
  family, so the same treatment applies rather than a rewrite.
- **`_gPhysBase`, `_gPhysSize`, `_gVirtBase`, `_kdebug_enable`, `_EntropyData`** and the four
  stack/vector symbols are XNU data symbols that `globals_asm.h` already lists as
  `LOAD_ADDR_GEN_DEF`s (9 of them) — they need *storage*, which is trivial, not code.
- **`_arm_init`** is one C function, and its two siblings (`arm_init_cpu`, `arm_init_idle_cpu`)
  are the secondary-CPU entry points, unused on a single-core bring-up.

## The flags, and the two that matter

Every flag below was found by assembling and reading the error. Two are non-obvious and cost the
most time:

- **`-DASSEMBLER=1`.** Without it, `osfmk/arm/asm.h`'s macro block from line 160 onward is
  preprocessed away — `LOAD_ADDR`, `EXT`, `LEXT`, `LOAD_ADDR_GEN_DEF`, the whole machinery — and
  every use of them reads as a syntax error. That one flag was behind *all* of the twenty-odd
  "expected ')'" errors in the first attempt, and it is why the first attempt looked like a header
  problem.
- **`-Dfmrx=vmrs -Dfmxr=vmsr`.** Apple's assembler accepts the FPA-era mnemonics `fmrx`/`fmxr` for
  VFP system-register transfers; LLVM's wants `vmrs`/`vmsr`. `start.s`'s `#if __ARM_VFP__` block
  uses the former. A macro substitution does the translation without touching Apple's source.

The rest: `-DARMA7=1` (the 32-bit ARMv7 machine configuration, found in `proc_reg.h:73`);
`-DSLIDABLE=0` (position-dependent — the ELF-friendly path, `ldr reg, L##label` rather than
Mach-O `$non_lazy_ptr`); `-mfpu=neon-vfpv4 -mfloat-abi=softfp` (without them the assembler rejects
`vmrs` with "instruction requires: VFP2"); `-D__ARM_L2CACHE_SIZE_LOG__=21` (`proc_reg.h`'s
`L2_CSIZE`, never defined in the tarball — 2 MB is the Krait 400 L2); and `-DKERNEL=1`,
`-DKERNEL_PRIVATE=1`, `-DCONFIG_EMBEDDED=1`.

**clang, not gcc.** Not a preference: `EXTERNAL_HEADERS/stdatomic.h:24` is `#error unsupported
compiler` unless `__clang__`, and GNU as separately rejects `asm.h`'s `.macro`/`.endmacro` (it
wants `.endm`). The C sources and the assembly want the same compiler, and it is clang.

## `assym.s` — the largest single missing piece, and it is now measured

`assym.s` is build-generated: the build runs `genassym.c` through the compiler and scrapes
`offsetof()` values out of its assembly output. It is absent, and both `start.s` and `locore.s`
`#include` it.

`xnu_arm_boot/assym.s` supplies what `start.s` needs. Three kinds of value, and the distinction
between them matters:

- **Derivable exactly.** `BA_VIRT_BASE`, `BA_PHYS_BASE`, `BA_MEM_SIZE`, `BA_TOP_OF_KERNEL_DATA`
  are plain `offsetof()`s into `struct boot_args` (`genassym.c:352-359`), and
  `tools/check_xnu_struct_abi.py` already compares our `boot_args` field by field against XNU's —
  so if the structs agree, the offsets agree. `PGBYTES` is `ARM_PGBYTES`. `SS_R0/R12/LR/PC/CPSR`
  and `SS_SIZE` are countable by hand from `thread_status.h`'s `arm_saved_state`.
- **Decidable from the target.** `MACH_TRAP_TABLE_ENTRY_SIZE_NUM` is `sizeof(mach_trap_t)`;
  `locore.s` handles only 12, 16 and 20, and `mach_trap_t` is `{fn, arg_count, arg_bytes}`, so a
  32-bit target gives 12. Not a guess, a calculation the source itself checks.
- **Placeholders, and labelled as such.** The `CPU_*` offsets are `offsetof()`s into `cpu_data_t`,
  whose layout depends on `__ARM_SMP__`, the cache configuration and a dozen `CONFIG_*` macros.
  They are load-bearing only for `resume_idle_cpu`/`start_cpu`, never for `_start`. Producing them
  properly means compiling `osfmk/arm/cpu_data_internal.h` — bounded and stated, not unknown.

## `locore.s` needs 28 more, and they are all the same kind

`./xnu_arm_assemble.sh --locore` gets past the same two flags and stops with 28 distinct undefined
`assym` constants: 17 `CPU_*` (offsets into `cpu_data_t`), 6 `SS_*` (which the file above already
supplies), 3 `EXC_*`, and the trap-table pair. So the gap for the whole entry path reduces to one
question: **can `osfmk/arm/cpu_data_internal.h` be made to compile?** If it can, a
`genassym`-style generator produces all of them mechanically and both files assemble. If it cannot,
that header is the next named blocker.

That is a materially better position than "the build configuration is absent".

## Chasing the one named blocker: `cpu_data_internal.h` does not converge

The section above says the whole entry path reduces to "can `osfmk/arm/cpu_data_internal.h` be made
to compile?" It was chased, and the answer is not a number of headers.

Starting from 21 errors, each round resolved to the *next* header rather than to a constant:

| Round | Errors | What the round needed |
| --- | --- | --- |
| 0 | 21 | `natural_t` — displaced by `-DASSEMBLER=1`, which makes `mach/arm/vm_types.h` skip its C types. Use `ASSEMBLER` for `.s` only. |
| 1 | 12 | `-I iokit`, then `decl_simple_lock_data`: defined in `osfmk/arm/simple_lock.h` and **included by nothing in the tree**. |
| 2 | 6 | `-include kern/queue.h kern/ast.h mach/vm_statistics.h` |
| 3 | 2 | `MACH_KERNEL_PRIVATE` (which is what gates `mpqueue_head_t`), then `string.h`, which a bare-metal ARM target does not have. |
| 4 | 4 | `TargetConditionals.h`, `mach_ldebug.h`, `zone_debug.h` |
| 5 | 13 | `kern/timer_call.h`, `kern/thread_call.h`, `kern/zalloc.h` — which want `call_entry`, `btlog_t`, and more. **It went up.** |

That last row is the result: **the closure grows as you satisfy it.** Each `-include` is a header
that itself needs three more, and unlike the `assym` constants there is no reason to expect a fixed
point reachable by a handful of stubs. Which is not to say it is impossible — the real XNU build
does exactly this with `genassym` — but it is "reconstruct the include closure", not "write six
stubs", and the difference matters for planning.

**And the reason the assembly works where the C does not is now clear**, which is the last piece.
`start.s` needs only the *machine* configuration — `ARMA7`, `SLIDABLE`, the L2 size, the FPU — and
every one of those is in the source. `genassym.c` (the file the `assym` constants are generated
from) needs the *build* configuration as well: compiled with everything the assembly needs, it
stops at `sync_qos_count_t`, `THREAD_QOS_LAST` and `ipc_kmsg_t`, which are `CONFIG_*`-gated and
generated by a build system Apple did not ship. So the two halves of "the build configuration" are
separable, and only one of them is reachable from the tarball: **the machine configuration is in
the source; the `CONFIG_*` set is not.**

Recorded so the next attempt starts from the measurement rather than from the adjective. The shims
written along the way (`string.h`, `TargetConditionals.h`, `mach_ldebug.h`, `zone_debug.h`) are in
`shims_arm/` and are correct as far as they go — each follows the released-kernel configuration,
which exercises *less* code rather than more.

## The correction to Phase 2

`start.s` **builds its own bootstrap page tables**, at `topOfKernelData`: it invalidates the TTEs
(`invalidate_tte`), sets a V=P section for the current PC, maps the kernel with 1 MB sections from
`physBase`/`virtBase`/`memSize` (`mapveqp`), spills into an L2 table when `memSize` is not 1 MB
aligned (`mapveqpL2`), inserts an exception-vector page table, and only then sets TTBR0/TTBR1 and
enables the MMU in `join_start`.

So the roadmap's Phase 2 bullet — *"Reserve and populate a `topOfKernelData` region containing the
bootstrap page tables XNU will adopt, and validate our tables against what `start.s` writes"* — has
the direction wrong. XNU does not adopt our tables; it writes its own. What it needs from us is
much less: a **writable, 16 KB-aligned `topOfKernelData` with enough room**, which the conforming
`boot_args` already provides and which `experiment-99` verified on the device
(`topOfKernelData = 0x0011c000`, `table_bytes = 0x0000a000`).

That removes an item from Phase 2 rather than adding one, and it is the kind of thing that is worth
knowing before writing a page-table builder nobody needs.

## What changed

- `stages/stage90/xnu_arm_assemble.sh` (new): the measurement, reproducible, with every flag's
  reason recorded next to it.
- `stages/stage90/xnu_arm_boot/assym.s` (new): the minimal `assym.s`, with the three kinds of
  value separated and the placeholders named as placeholders.
- `stages/stage90/shims_arm/mach_kdp.h`, `config_dtrace.h` (new): two more build-generated headers
  that `start.s`/`locore.s` include. `CONFIG_DTRACE 0` because that is the released-kernel
  configuration and it exercises *less* code.
- `docs/status/roadmap.md`: the Phase 4 correction and the corrected Phase 2 bullet.

## What this does not establish

Nothing here has run on the device, and nothing here is linked into the payload. `start.s`
assembling is not `start.s` executing — it still needs the twenty symbols above, and then a
`topOfKernelData` region, and then the jump. This experiment moves the entry point from "blocked on
an unnamed configuration gap" to "blocked on a list".
