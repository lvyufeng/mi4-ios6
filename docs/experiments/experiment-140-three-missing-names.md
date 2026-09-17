# Experiment 140 — three missing names, three narrow answers

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-139) | after |
| --- | --- | --- |
| `RELEASE` | 600 of 615 | **602 of 615** |
| `STAGE90_BOOT` | 407 of 426 | **409 of 426** |
| stub symbols in the image | 377 | **362** |
| boot-path stubs | 120 | **113** |

Three failures, three causes, all of the shape this project keeps meeting — a name used in a file
that reaches no header defining it — and all three answered narrowly rather than by widening a path.

## 1. `uid_t`/`gid_t` in `bsd/sys/kauth.h`, and an include that arrives too late

`bsd/sys/kauth.h:113` declares `uid_t el_uid;` and `:118` `gid_t el_gid;`. The header includes
`sys/_types.h`, `sys/syslimits.h` and `sys/_types/_guid_t.h` — **nothing that defines `uid_t`**. The
definition is `bsd/sys/_types/_uid_t.h:31`, which `bsd/sys/types.h` includes, and in
`kern_ktrace.c`'s closure `sys/types.h` arrives at trace line 128 while `kauth.h` is at 107. So the
types are used eight lines before they are defined, in the same translation unit.

The answer is `-include sys/types.h` **for BSD-component files only**, and the restriction is the
substance: `sys/types.h` is what collides with `kern_types.h` over `clock_t` (the `-D_CLOCK_T` story,
experiment-115 → 126), and a BSD file never reaches `kern_types.h`'s definition because it is behind
`MACH_KERNEL_PRIVATE`, which a BSD file does not define (experiment-118). One file newly passes
(`kern_ktrace.c`), no regressions.

## 2. `u_char` in `osfmk/kern/btlog.c`

`btlog.c:641` casts to `u_char`. The files that *do* reach `u_char` get it through
`osfmk/libsa/types.h` — which `subrs.c` reaches via `<libsa/stdlib.h>`, and which most of osfmk does
not include at all. `sys/_types/_u_char.h:30` is the definition, and it defines `u_char` and nothing
else. One file newly passes (`btlog.c`), no regressions.

## 3. `vnode_trim`, which is not missing

`bsd/vm/vnode_pager.c:211:11: conflicting types for 'vnode_trim'` — left alone, and named here
because it is a different kind of failure from the two above: a *conflict*, not an absence. The two
definitions have to be compared rather than a header supplied, and that is the next stage's work.

## Where it stands

| | |
| --- | --- |
| `RELEASE` | 602 of 615 |
| `STAGE90_BOOT` | 409 of 426 |
| image | 362 stubs, `_start` at `0x803b7074` |
| boot path | 113 stubs |
| `osfmk/arm` | 32 of 32 |

**XNU still does not run**, and the two deepest blockers are unchanged: all 23 "assembly the build
never attempts" are `machine_routines_asm.s` and `data.s`, and both are the Mach-O-versus-ELF
question (experiments 137-138). Everything this stage fixed is above them in the graph.

The three fixes are also all the same *kind* of work — a name used where no header supplies it —
and they are the tail of that kind. Of the 13 remaining `RELEASE` failures, the causes are now
individually distinct: `vm_object.c`'s `const` member (not fixable here), `uipc_mbuf.c`'s
`sync_qos_count_t` (include order), `OSAtomicOperations.c`'s `false`/`true` macros, `subr_prof.c`'s
`STATIC`, `kern_sysctl.c`'s negative-size array, `vnode_pager.c`'s `vnode_trim` conflict,
`task.c`'s `task_collect_crash_info` conflict, `mptcp_var.h`'s `t_mptcb`, `in_pcb.h`'s
`struct in_addr`, `pty`-adjacent fallout — each one file, each its own investigation.

## How to reproduce

```bash
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh          # 602 of 615
./tools/measure_link.sh --keep-stubs       # 362 stubs
./tools/stub_reach.py --from arm_init      # 113 on the boot path
```
