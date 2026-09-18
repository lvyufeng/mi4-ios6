# Experiment 158 — `arm_init`'s closure is the whole kernel, and the manifest omits 126 `optional` sources

Date: 2026-09-18
Build: host-side only, no device run
Why it was run: to decide which part of the compile graph to build next

## The question, and the plan it came from

The entry image could be made to run XNU's *real* `arm_init` (experiment-159) — but linking one
object is a poor measurement, because everything `arm_init` calls then has to be stubbed, and the
device reports the first name in the function's *prologue* rather than the first thing that
genuinely does not exist yet. The obvious next move was to grow the link: add the object that
defines whatever is undefined, repeat, and let the surviving stub list be the real answer. That is
what `tools/entry_closure.py` does:

```
./tools/entry_closure.py --seed out/xnu_kernel_obj/osfmk_arm_arm_init.o \
                         --preset out/stage90/xnu_arm_start.o \
                         --preset out/stage90/xnu_arm_entry_vectors.o \
                         --preset out/stage90/xnu_arm_entry_stubs.o
```

## The answer: it does not converge

```
round 1: 43 undefined, 1 object
  + osfmk_arm_cpu.o            closes 6:  cpu_bootstrap, cpu_data_init, cpu_idle_exit ...
  + machine_routines_asm.o     closes 7:  cpu_idle_wfi, fiq_context_init, get_mmu_control ...
  + osfmk_arm_cpu_common.o     closes 7:  cpu_machine_init, cpu_number, cpu_processor_alloc ...
  + osfmk_arm_caches.o         closes 8:  cache_xcall_handler, clean_dcache, flush_dcache ...
  + osfmk_arm_pmap.o           closes 10: kernel_pmap, kernel_pmap_store, kvtophys ...
  + osfmk_vm_vm_resident.o     closes 16: cpm_allocate, virtual_space_end, virtual_space_start ...
  + osfmk_arm_locks_arm.o      closes 16: _disable_preemption, _enable_preemption ...
  + osfmk_kern_locks.o         closes 23: LockDefaultLckAttr, hw_atomic_add ...
  ...
  + bsd_kern_tty_conf.o        closes 2:  linesw, nlinesw
  + bsd_kern_tty_ptmx.o        closes 2:  ptsd_kqfilter, ptsd_kqops
  + bsd_miscfs_devfs_devfs_tree.o closes 4: devfs_make_node, devfs_make_node_clone ...
  ...
stopping at 400 objects; 405 symbol(s) still undefined
```

The chain is real, not an artefact of the greedy rule. `arm_init` calls `cpu_init`, which reaches
the scheduler's thread and task bootstrap, which reaches IPC, which reaches `bsd/kern/kern_proc.c`,
which reaches the vnode layer, which reaches devfs and the tty line disciplines. **Every kernel
entry point's translation-unit closure is the entire kernel**, because a static kernel is one
connected component through its data. Growing the link object by object therefore has no stopping
point: the closure is not a smaller thing than the kernel, it *is* the kernel.

That kills the plan this experiment was written to execute, and it is worth being explicit about
what replaces it: **a kernel is linked whole.** The useful question is not "what does the closure
need next" but "how many symbols does a whole-kernel link still leave unresolved".

## The whole-pool measurement

```
arm-none-eabi-ld -r -o /tmp/xnu_all_objs.o out/xnu_kernel_obj/*.o out/xnu_asm_obj/*.o
arm-none-eabi-nm -u /tmp/xnu_all_objs.o | wc -l     # 189
```

**703 objects — every one this project builds — leave 189 symbols unresolved.** All 189 are
referenced by an object that is in the pool; none is a reference from nowhere. They fall into
three groups.

### 1. Composition failures: 118 symbols (62%)

Symbols whose name appears in one of the ten sources that do not compile:

| Source | Symbols | Why it does not compile |
| --- | --- | --- |
| `osfmk/vm/vm_object.c` | 58 | see its `.log` |
| `bsd/kern/subr_log.c` | 24 | |
| `bsd/vm/vnode_pager.c` | 8 | |
| `bsd/net/if_bridge.c` | 7 | |
| `libkern/c++/OSKext.cpp` | 6 | the C++ block (experiment-154) |
| `libkern/c++/OSRuntime.cpp` | 5 | the C++ block |
| `libkern/OSKextLib.cpp` | 4 | the C++ block |
| `bsd/kern/subr_prof.c` | 2 | |
| `libkern/os/log.c` | — | `FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT` undeclared |
| `osfmk/kperf/kperfbsd.c` | — | |

`vm_object.c` alone is 58 of the 189, and its referrers are the biggest names in the kernel:
`vm_map.o`, `vm_pageout.o`, `memory_object.o`, `bsd_vm.o`. This is the single highest-value file in
the tree and it is one compile away from closing a third of the gap.

### 2. Compiler runtime and ABI: 14 symbols

`__aeabi_d2ulz`, `__aeabi_l2d`, `__aeabi_ldivmod`, `__aeabi_memclr8`, `__aeabi_memcpy{,4,8}`,
`__aeabi_ul2d`, `__aeabi_uldivmod`, `__cxa_atexit`, `__cxa_pure_virtual`, `__dso_handle`,
`__nosan_bzero`, `__nosan_strncpy`.

Not XNU's to provide: Apple links these from `libcc_kext` plus the kernel's own
`bcopy.s`/`bzero.s` for the memory ones. Nothing in XNU defines an `__aeabi_*` name:

```
grep -rn "__aeabi_memcpy" $XNU/osfmk $XNU/bsd $XNU/libkern    # nothing
```

See experiment-159 for what that means at link time and the alias file that closes the memory half.

### 3. Sources the manifest omits: the rest

`bsd/net/bpf.c` and `bsd/net/ether_if_module.c` are the visible end of a much larger hole. Both are
listed in Apple's `bsd/conf/files` — with an option tag:

```
bsd/conf/files:192: bsd/net/bpf.c           optional bpfilter
bsd/conf/files:199: bsd/net/ether_if_module.c   optional ether
```

and neither is in this project's manifest:

```
grep -c "bsd/net/bpf.c$"            out/xnu_arm_manifest.txt   # 0
grep -c "bsd/net/ether_if_module.c$" out/xnu_arm_manifest.txt   # 0
```

Counting every `optional`-tagged `.c`/`.cpp` in the five `conf/files` this project reads:

| | count |
| --- | --- |
| `optional`-tagged sources present in the manifest | 301 |
| `optional`-tagged sources **absent** from it | **126** |

The 126 are whole subsystems, not strays: all 18 of `libkern/kxld/*` (the kext-linking machinery
`OSKext.cpp` calls into), `bsd/net/{bpf,ether_if_module,if_loop,if_vlan,if_bond,if_gif,if_stf,
bridgestp,devtimer,packet_mangler}.c`, all of `bsd/security/audit/*`, all of `bsd/dev/dtrace/*`,
all of `bsd/nfs/*`, `osfmk/kdp/kdp.c`, `osfmk/kern/{xpr,gzalloc,hibernate}.c`,
`iokit/Kernel/IOHibernate*.{cpp,c}`.

That list explains a second set of the 189 by name, and it is measurable:
`kdp_init`/`kdp_vtophys` (`osfmk/kdp/kdp.c`), `bpf_attach`/`bpf_tap_in`/`bpfdetach` (referenced by
`iptap.o` and `pktap.o`), `ether_add_proto`/`ether_demux` (referenced by `if_fake.o`), `lo_ifp`
(`if_loop.c`), and the `kxld_*` names behind `OSKext.cpp`'s compile failure.

## What this changes

- **The staging question changes.** "Which part of the compile graph next" has no answer, because
  the graph does not have parts at this granularity. What the 189 says instead is that the remaining
  work on the *composition* side is finite and enumerated: close ten files, then decide about the
  manifest's `optional` tag, then provide the compiler runtime.
- **The manifest is the higher-leverage half.** Ten failing files are ten files. A manifest that
  silently drops `optional` sources is a whole class of them, and it is the same defect shape this
  project recorded in `mi4-generator-output-kinds`: a tool that answers a slightly different
  question than the one asked (here: "which sources are unconditional" rather than "which sources
  does this configuration build").
- **`tools/entry_closure.py` is still the right tool for a different job.** It answers "what does
  this image need that its object set does not provide", which is exactly what the entry image asks
  when it links `arm_init.o` — and it is what said `cpu_data_init` (experiment-159) would be the
  first stub hit before the device said it too.

## Reproduce

```bash
# the closure, and where it fails to converge
./tools/entry_closure.py --seed out/xnu_kernel_obj/osfmk_arm_arm_init.o \
    --preset out/stage90/xnu_arm_start.o \
    --preset out/stage90/xnu_arm_entry_vectors.o \
    --preset out/stage90/xnu_arm_entry_stubs.o \
    --out out/stage90/xnu_arm_entry_closure.txt

# the whole pool's unresolved set
arm-none-eabi-ld -r -o /tmp/xnu_all_objs.o out/xnu_kernel_obj/*.o out/xnu_asm_obj/*.o
arm-none-eabi-nm -u /tmp/xnu_all_objs.o | wc -l          # 189

# the manifest's optional sources
grep -c "bsd/net/bpf.c$" out/xnu_arm_manifest.txt        # 0
grep -n "bpf\.c\|ether_if_module\.c" \
    external/xnu-4570.1.46/bsd/conf/files                # both `optional <tag>`
```

## Two measurement defects, recorded because they nearly became findings

**`ld -r` does not report undefined symbols.** The first version of this measurement ran the
relocatable link and read its exit status: `rc=0`, no diagnostics, "the pool is complete". A
relocatable link is *allowed* to leave symbols undefined — that is what a partial link is — so it
never fails on one. The 189 came from `nm -u` on the merged object, which is a different question
and the one that was being asked. A tool's exit status is evidence only about the question the tool
asks.

**`nm -S` prints a value where a size was expected.** The storage stand-ins were sized from
`nm -S` output that had been read column by column, and `-P` changes which column is which:
`name type value size` without `-A`, `file:name type value size` with it. The first reading put
`BootCpuData` at 0x6000 *bytes* (24 KB, a plausible size for a per-CPU data area) when 0x6000 was
its *offset* — and 0x6330, the next symbol's offset, differs by 0x330, which is what the real size
turned out to be. The wrong reading would have produced a 24 KB stand-in for an 816-byte symbol:
harmless in that direction, and exactly the kind of plausible number that stops a check from being
written. What settled it was the source: `osfmk/arm/data.s` states each size, and three of the six
match the offsets exactly.
