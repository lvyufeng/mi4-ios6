# 940 — 510's entry-point store follows the tree; D13's entry travels in r1 (2026-10-08)

939 closed the SIGPIPE false reading and the D13 entry build advanced to

    FAIL: activate_exec_state is not in the linked image - 510's clause needs the exec path's own
    function to read the call site out of

510 wraps `thread_setentrypoint` — Apple's single store of a process's user entry point — and asserts
that the branch to `__wrap_thread_setentrypoint` sits **inside the exec path's own function**. On 4570
that function is `activate_exec_state` (`bsd/kern/kern_exec.c:725`). **D13 does not have it.**
`grep -rn activate_exec_state <D13>/` is empty: D13's `kern_exec.c` **inlines that path into
`exec_mach_imgact`** (`:722`) and calls the store directly, at `:998`:

    998:	thread_setentrypoint(thread, load_result.entry_point);

## Finding 1 — the call site's *name* follows the tree

The property 510 checks is unchanged — the branch is inside the exec path's function — but the name
that function has is per-tree. The clause now picks it by `$D13_TRACE`: `exec_mach_imgact` on D13,
`activate_exec_state` on 4570. And on D13 the *old* name is itself a defect: a re-pointed
`activate_exec_state` in a D13 image would mean a name the tree lacks had been fabricated into it, so
the D13 arm **fails if `activate_exec_state` is present at all**.

## Finding 2 — D13's entry is 32-bit and travels in **r1**, not the r2:r3 pair

This is the sharper one, and it is the exact mirror of the defect 510's width check was built to
catch. On 4570 the entry is a 64-bit `mach_vm_offset_t` passed as the pair **r2:r3** (r1 unused), so
the wrapper must be `uint64_t`; the clause checks both r2 and r3 are written at the wrapper's call to
the real function. On D13 the header says the same thing —

    osfmk/kern/thread.h:788:  extern void thread_setentrypoint(thread_t thread, mach_vm_offset_t entry);

— but the **definition** and the **caller** are both single-word, and the ABI follows the definition:

- `osfmk/arm/status.c:439`: `void thread_setentrypoint(thread_t thread, uint32_t entry)` — the body is
  `thread->machine.user_regs.pc = entry;`, a single store.
- `bsd/kern/kern_exec.c:998` compiles the call as (D13's own object,
  `out/xnu_kernel_obj_d13/bsd_kern_kern_exec.o`):

      c04: e59d104c  ldr r1, [sp, #76]
      c08: e1a00005  mov r0, r5
      c0c: ebfffffe  bl  0 <thread_setentrypoint>

— **r1** carries the entry, and **no r2 or r3 is written**. (The value comes from
`load_result_t.entry_point`, a `user_addr_t`, which is one word on this 32-bit port.)

So the D13 arm of the wrapper is `uint32_t entry` and reads **r1** — the register the call actually
uses. Taking the 4570 `uint64_t` pair here would leave r1 alone and put the entry in r2:r3, which D13's
32-bit function never reads — reading the caller's leftover word, exactly the failure the 4570 first
build paid for (`_entry = 0x00000008`, pid 1 starting at junk and exiting). The clause's width check is
now per-tree: on D13 every call to the real function must write **r1**.

## The change

- **`src/entry/entry_trace.c`** — `__wrap_thread_setentrypoint` is split on `STAGE90_ENTRY_D13`: the
  D13 arm declares `uint32_t entry` (reads r1) and passes `0u` as the high word of the record; the 4570
  arm is byte-for-byte the existing `uint64_t` pair.
- **`src/entry/build_entry.sh`** (the 510 clause) —
  - the call-site symbol is a per-tree variable (`execsite`): `exec_mach_imgact` on D13, with a
    **refusal** if `activate_exec_state` exists on D13 at all; `activate_exec_state` on 4570;
  - the branch's disassembly is captured into a variable and grepped via a herestring (the 939 SIGPIPE
    form — this reader had the same latent race);
  - the width check reads r1 on D13 (must be written by every call) and keeps the r2/r3 pair on 4570.

## State after this rung

510 passes on D13: the record line reads *"the one branch to its wrapper (0x80499cac) is inside the
exec path's function (0x801fe4b0), and there is exactly 1 such branch in the whole image"*, and the
width line reads *"all 2 call(s) … every one writes r1"*. The build advances to **511**:

    FAIL: ast_taken_user is not in the linked image - 511's clause needs the function the AST is
    delivered from to read the call site out of

Assumed to be the same class (a 4570 call-site name D13 lacks) and therefore its own rung. `make check`
exits **0**.

## Provenance

`src/entry/entry_trace.c`, `src/entry/build_entry.sh`. Host-side, reversible, **no press**.

- D13 build: `/tmp/d13_entry_build20.log` (510's two `say` lines both pass; stops at 511).
- 4570 neutrality: the switch is 0 on 4570, so the `#else` arm and the `activate_exec_state` branch
  compile in unchanged; the width check's 4570 branch is textually the old one.
- `make check` exits 0 (the first run caught four unescaped backticks in the new `layout_fail` string —
  `check_backtick_messages`; fixed).