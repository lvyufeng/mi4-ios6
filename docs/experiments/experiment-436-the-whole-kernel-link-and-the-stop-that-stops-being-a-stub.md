# Experiment 436 — the whole-kernel link: 1038 stubs become 44, and the stop stops being a stub

**Step:** the 436 block in `stages/stage90/xnu_arm_boot/build_entry.sh`, between
`LINK_OBJS+=("$OUT/xnu_arm_entry_macho.o")` and `require "$LIBGCC"`. It **globs `out/xnu_kernel_obj/`
and `out/xnu_asm_obj/`** — every object `tools/build_xnu_arm_kernel.sh` produced — and appends
everything not already named above, minus three objects that cannot be linked beside them.

    the whole kernel: 423 object(s) added, 286 already named above,
    refused by name: iokit_KernelConfigTables.o locore.o start.o

It is the largest single change this ledger has made, by an order of magnitude: 435 was 22 objects
and this is 423, out of a 712-object pool.

## Why this step exists, and why it is not 435's method continued

435 measured its own method and priced it: the 22 objects it linked contain **889 `bl`-to-stub call
sites covering 300 distinct stub names**, the pool objects defining those 300 number 84 more, and the
walk retires the calls it can *reach*, one step per name. `tools/boot_closure.py` puts the same
number on the whole boot: **1214 objects available, 1056 on the boot path, 122 names undefined
everywhere.** And the memory `mi4-a-kernel-entry-points-closure-is-the-kernel` had already recorded
the answer for `arm_init`: *the closure of a kernel entry point is the kernel*.

So 436 stops choosing. **Link the pool, and let the generator's own rule — "one stub per symbol
XNU's own objects need and this image does not provide" — report what is left.** The predicate the
whole instrument is built on is unchanged; what changes is that the set is now the kernel rather than
one frontier at a time.

## The three refusals, and why they are collisions rather than choices

A name two linked objects both define is a link error, so an object defining a name the image already
has cannot come in whole. **Fifteen** names are shared between the eleven entry-side objects and the
pool, counted with `nm --defined-only -g`:

| refused object | names | which |
|---|---|---|
| `out/xnu_asm_obj/start.o` | 4 | `_start`, `start_cpu`, `resume_idle_cpu`, `arm_init_tramp` — all four are `xnu_arm_start.o`'s, this image's own entry, which is what the payload jumps to |
| `out/xnu_asm_obj/locore.o` | 10 | `ExceptionVectorsBase` (also `xnu_arm_entry_vectors.o`'s), `ExceptionVectorsTable`, and the eight `fleh_*` handlers (`reset`, `undef`, `swi`, `prefabt`, `dataabt`, `addrexc`, `irq`, `decirq`) — the last nine are `entry_stubs.c`'s hand-written vector glue. **This is the vector table: the image has its own, and two of them is a link error.** |
| `out/xnu_kernel_obj/iokit_KernelConfigTables.o` | 1 | `gIOKernelConfigTables`, defined by `stage90_platform_config_tables.o` — MSM8974's tables rather than Apple's |

**`locore.o` is also the pool's only definer of three names this image does *not* define**:
`thread_bootstrap_return`, `thread_exception_return`, `thread_syscall_return`. Refusing the object
does not lose them — they stay stubs, where they were before this step — but it is why the refusal
costs a name and not only a duplicate, and it is visible in the stub list rather than inferred.

The build checks the refusal list **by name** and fails if one of the three is not in the pool, since
a stale list would otherwise surface much later as a link error and read as something else. It also
refuses a glob that finds fewer than 300 objects: a pool that was never built, or built in part,
would produce a smaller image *and a smaller stub list*, which is a step that looks like progress and
is the opposite.

## Effect: 1038 stubs become 44, and every storage stand-in goes with them

    pass 1 undefined     1038 -> 44        (994 names retired in one step)
    function stubs        844 -> 44
    storage stubs         194 -> 0

**Zero storage stand-ins remain.** Every one of the 194 names that used to be a sized, uninitialized
array is now a real definition from the object that defines it — which retires the
`mi4-stand-in-size-is-not-value` hazard for the whole image, and, as the run shows, replaces it with
its sibling: a real `.bss` global that nothing has written yet.

The 44, all functions, all self-naming stand-ins:

    addupc_task            bpf_attach             bpfattach              bpfdetach
    bpfkqfilter            bpf_tap_in             bpf_tap_out            chudxnu_cpu_alloc
    chudxnu_cpu_free       chudxnu_cpu_signal_handler
    chudxnu_thread_get_callstack64_kperf          _enable_kernel_vfp_context
    ether_add_proto        etherbroadcastaddr     ether_check_multi      ether_del_proto
    ether_demux            ether_frameout_extended
    _hashLookupTable_new   kmstartup              _kprintf               _lastkerneldataconst
    _lastkerneldataconst_padsize                  __llvm_profile_get_size_for_buffer_internal
    __llvm_profile_write_buffer_internal          lo_ifp                 mach_msg_destroy_from_kernel
    osrelease              ostype                 pseudo_inits           sysctl__net_link_ether_children
    thread_bootstrap_return                       thread_exception_return thread_syscall_return
    UNDCancelNotification_rpc                     UNDDisplayAlertSimple_rpc
    UNDDisplayCustomFromBundle_rpc                UNDDisplayNoticeSimple_rpc
    UNDExecute_rpc         version_major          version_minor
    _Z26upl_get_internal_vectoruplP3upl           _Z32upl_get_internal_pagelist_offsetv
    _Z35upl_get_internal_vectorupl_pagelistP3upl

They fall in five groups, and only the first is on the boot path: **one that is** (`kmstartup`,
`bsd_init + 0x844`); **the BPF/ether pair** (`bpf*`, `ether*`, `lo_ifp`,
`sysctl__net_link_ether_children` — `bsd/net/bpf.c` and `bsd/net/ether*.c`, which this pool does not
build); **the user-notification RPCs and `upl_*`** (`UserNotification/UND*.c` and the C++
`upl` pagelist class, neither of which the manifest compiles); **instrumentation and identity**
(`_lastkerneldataconst*`, `_kprintf`, `osrelease`/`ostype`/`version_major`/`version_minor`,
`chudxnu_*`, `__llvm_profile_*`, `_hashLookupTable_new`, `addupc_task`); and **the vector returns**
(`thread_*_return`, from the refused `locore.o`).

## The prediction, and the measurement

**Prediction, written before the build, and what the build said:** 44 stubs, 0 storage, no multiple
definitions. Measured: `stubs: 44 function(s), 0 storage`, `44 symbol(s) undefined`, **0** multiple
definitions in pass 1.

**Prediction of the stop, written before the run and after the build,** from
`tools/xnu_entry_callwalk.py --elf out/stage90/xnu_arm_entry.elf --root bsd_init`:

    first stub on the straight-line path: kmstartup
    the run should stop with stub_hit=kmstartup

with the caller key `0x8003B228`, the return address of the `bl <kmstartup>` at `bsd_init + 0x834`
(`bsd_init` is `0x8003A9F0`) — the twelfth `bl` in `bsd_init`'s last straight run, with no
conditional branch between them.

**Measured on hardware: the falsifier, not the prediction.**

    line 3932:  real XNU entry: exception: data abort
    line 3941:  xnu_entry_stub_caller_w0=0x756e7820       (" xnu")
    line 3942:  xnu_entry_stub_caller_w1=0x746e655f       ("_ent")
    line 3943:  xnu_entry_abort_entries=0x00000001
    line 3944:  xnu_entry_abort_first_dfar=0x0000003c
    line 3945:  xnu_entry_abort_first_pc=0x80409db8
    line 3949:  xnu_entry_abort_first_insn=0xe590603c
    line 3952:  xnu_entry_why=0x80444e74   why_byte=0x00000065  ('e')
    line 3990:  xnu_entry_stub_caller_v=0x00000000
    line 3976:  No errors detected

**`xnu_entry_stub_caller_v = 0`: no stub was reached at all.** The stop is not a stub.

## The reading: a fourth kind of stop, and the first one of its kind

The faulting instruction is `ldr r6, [r0, #0x3c]` at `0x80409DB8`, with `dfar = 0x3C` — i.e. `r0` is
zero — in `aes_encrypt_key128` (`0x80409DA0`, `libkern/crypto/corecrypto_aes.o`):

    80409da0 <aes_encrypt_key128>:
      80409da8:  movw r0, #14364 ; movt r0, #32851     -> 0x8053381c
      80409db4:  ldr  r0, [r0]                          <- g_crypto_funcs
      80409db8:  ldr  r6, [r0, #60]                     <- FAULT, r0 = 0

**`g_crypto_funcs` is a real `.bss` global at `0x8053381C` and it is NULL.** It is defined `B` by
`libkern/crypto/register_crypto.o`, and its only writer is `register_crypto_functions()`
(`libkern/crypto/register_crypto.c:35`), whose only caller in the whole tree is Apple's
**`com.apple.kec.corecrypto` kext** — a binary this tarball does not contain and no object in the pool
supplies. This is **the same shape as 432's `pthread_functions`**, and it is the second table this
boot needs that nothing in the tree writes.

Of the four `bl <aes_encrypt_key128>` sites in the linked image — `tcp_init + 0x104`,
`tcp_sysctl_fastopenkey + 0xDC`, `cpx_set_aes_iv_key + 0x10`, `cpx_iv_aes_ctx + 0x54` — the one the
boot reaches is `tcp_init`'s, through the **inlined `tcp_tfo_init()`** (`bsd/netinet/tcp_subr.c:449`):

    8035ac58:  mov r0, r4 ; mov r1, #16
    8035ac60:  bl  read_frandom                 <- the key
    8035ac70:  bl  aes_encrypt_key128           <- "aes_encrypt_key128(key, &tfo_ctx)"

and `tcp_init` is reached from `bsd_init + 0x818` (`domaininit`). There is no branch between
`tcp_init`'s entry and that call other than `tcp_initialized`, so this is an unconditional call in
every XNU of this version — which means **on the real device `g_crypto_funcs` is non-NULL by the time
`bsd_init` runs**, and the real kernel gets that from the prelinked corecrypto kext.

**So the walk was right about the stubs and the stop was a different kind of stop — the fourth to
appear in this ledger.** `mi4-stub-walk-frontier-kinds` names three: a missing symbol, an invented
zero, and a boot-arg/DT string. This is **kind 2**, and 436 is the first step whose stop is one.
Before it, every name of this sort was a *storage stand-in* — an array the generator sized from the
object that defines it — and a stand-in that stops is not a zero that gets dereferenced. **The
whole-kernel link did not create this failure; it converted an unknown number of them from a named
stop into an opaque fault**, by replacing every stand-in with the real, zeroed definition.

The walk could not have said so for 431's reason: both of its lists are built from the *stub* set,
and `g_crypto_funcs` is not a stub.

## What the step did accomplish, measured

**Five calls further than 435.** 435's stop was `ifnet_llreach_init`, `dlil_init`'s first callee;
436's stop is inside `domaininit`'s transitive closure, so `dlil_init`, `proto_kpi_init`,
`socketinit` and `domaininit` all ran to completion — including the whole network-layer
initialisation that 435 measured as costing one step per name. **994 of the 1038 names retired in one
step, and 194 storage stand-ins with them.**

The frontier is now a **single, named, reachable table** — `g_crypto_funcs` — rather than a queue of
names, and the queue behind it is 44 long, only one of which (`kmstartup`) is on `bsd_init`'s
statement list.

## Layout

    entry text   0x4B2A80 (4926080)     <- was 0x2A5980: +0x20D100
    entry image  0x4E344C (5125196)     <- was 0x2C6BD0 (2911184)
    .data        0x804B4000 (0x2E360)   <- was 0x802A8000 (0x1E6C8)
    .sysctl_set  0x804E2360 (0xFD8)     <- was 0x802C66C8 (0x474)
    .init_array  0x804E3338 (0x114)     <- was 0x802C6B3C (0x94)
    bss          0x804E3480 .. 0x805348F8 (0x51478 = 332920)   <- was 0x44490 (279696)
    layout       args 0x80536000, topOfKernelData 0x80700000, tree 0x80900000
    window       16777216 (16 MB)       <- was 8388608: the image outgrew the 8 MB window
    headroom     1881864 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 77fc44b10c8cc5ba04641e9c183eb8ca9f540b8fd4cebbfb668d45b532b04dbb (8144896 bytes, 7954 KB)
    entry bin    5125196 bytes; sha256
                 f6de9141653bc3482029782c40a065b921d326f9f0b071127c27e7ce540ebd69

**Every row moved, and the window doubled — which is the build working rather than a wall.**
`ENTRY_SIZE` is derived by doubling until it covers `ENTRY_DT_OFFSET + ENTRY_DT_MAX`, so the 8 MB
window became 16 MB and `topOfKernelData` went `0x80500000 → 0x80700000` with `args` at `0x80536000`
and the tree at `0x80900000`. The four layout invariants in the build (image below
`topOfKernelData`; args below it; the 0xA000 of tables clear of the tree; the tree inside the
window) all hold, and the fifth — nothing initialized inside the memset range — is what the build's
own line reports: *"the copied image ends at 0x804e344c, **52 bytes below** `__bss_start`"*.

`verify_sections` still reports **`.bss .data .init_array .sysctl_set .text` and nothing else**, which
is the check that matters for a 423-object step: the new material brings `.rodata.cst4/8/16/32`,
`.rodata.str1.8`, `.rodata.str4.4`, 40 `.text._ZN*` COMDAT sections, `__TEXT,__os_log` and
`__DATA,__sysctl_set` — every one of them matched by a pattern `entry.ld` already had
(`*(.rodata .rodata.*)`, `*(.text .text.*)`, `*("__TEXT,*")`), and none of them an orphan.

**The three notable objects landed where the map says:** `bsd_netinet_tcp_subr.o` at `0x8035A9C0`
(`tcp_init` `0x8035AB6C`), `libkern_crypto_corecrypto_aes.o` at `0x80409BB0`
(`aes_encrypt_key128` `0x80409DA0`), `bsd_vfs_vfs_cprotect.o` at `0x803E0240`.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure); nothing flashed, nothing written to storage.
25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=1` (the
`g_crypto_funcs` fault), `checks=5` / `failures=0`, one `exception:` line and no `panic:` line,
`kv_dropped=0`. The payload is 7954 KB, well inside what `fastboot boot` accepted. The only net armed
across the jump was the hardware watchdog (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bite_truncated=0x00000000`), dead-man PPI disarmed before it
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Device returned to Android on its own and was
confirmed there (`ro.product.device` cancro, `ro.build.version.release` 10). 301742 bytes / 3976
lines, ending `No errors detected`.

**A note on the exception itself, because it is the one thing this step could have made unsafe.** The
fault is a *read* of address `0x3C` — a translation fault on an address the MMU rejects, not a write
to storage and not a wild write. `dfsr=0x5` (section translation fault on read) and the payload's
exception handler caught it, recorded 19 words of machine state and branched to the report. Nothing
was written anywhere; the device was never at risk.

Per-run logs stay apart: `/tmp/run425_kmsg.txt` … `/tmp/run435_kmsg.txt`, `run436_kmsg.txt`.

## What 437 has to be

**Not another object.** The remaining stub queue is 44 names, of which exactly one is on `bsd_init`'s
statement list — `kmstartup` at `+0x834`, which is a one-line fix to `bsd/kern/subr_prof.c`'s missing
`STATIC` macro — and the other 43 are subsystems this pool does not build (BPF/ether, the
UserNotification RPCs, `upl_*`) plus instrumentation. **Linking nothing else can move the boot.**

**The frontier is `g_crypto_funcs`**, and it is 432's shape exactly: a table the image must *supply*
because nothing in the tree writes it. The step is a stage-owned
`stages/stage90/xnu_supply/stage90_crypto_functions.c` that

  - builds a `crypto_functions_t` (`libkern/crypto/register_crypto.h` — Apple's own header, so the
    layout is one definition rather than a mirror) whose **AES entries are real**, because
    `tcp_init`'s `tcp_tfo_init()` calls `aes_encrypt_key128` unconditionally and a real AES-128 key
    schedule is the smallest honest thing that makes that call correct, and
  - leaves **every other entry pointing at a self-naming stand-in**, which is the entry image's own
    idiom and the thing this step just proved it needs: a stand-in that stops and names itself, so
    the *next* zero in this table is a `stub_hit=` line with a caller key rather than a `first_dfar`;

and installs it from an `.init_array` constructor before `_start` hands over, the way 432's
`pthread_functions` table is installed.

**And the ordering question the run raises explicitly:** `tcp_init` calling AES unconditionally means
`g_crypto_funcs` is non-NULL before `bsd_init` on the real device, so the table is not a workaround —
it is a thing this kernel is supposed to have and does not, for the same reason 431's
`pthread_functions` was.
