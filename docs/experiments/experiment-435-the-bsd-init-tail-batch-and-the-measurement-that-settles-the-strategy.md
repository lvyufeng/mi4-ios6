# Experiment 435 — the `bsd_init` tail batch: 22 objects, the step whose own numbers say "one call at a time" is not the road to a booted OS

**Step:** 22 objects appended to `LINK_OBJS` after `BSD_NET_NWK_WQ_OBJ` — every pool object that
defines one of the 23 remaining stubs on `bsd_init`'s statement list that a pool object *can* define.
`bsd_net_dlil.o`, `bsd_net_kpi_protocol.o`, `bsd_kern_uipc_socket.o`, `bsd_kern_uipc_domain.o`,
`bsd_net_iptap.o`, `bsd_netinet_flow_divert.o`, `bsd_kern_kern_acct.o`, `bsd_kern_kern_mib.o`,
`bsd_dev_arm_km.o`, `bsd_net_init.o`, `bsd_net_content_filter.o`, `bsd_net_necp.o`,
`bsd_net_network_agent.o`, `bsd_net_if_utun.o`, `bsd_net_if_ipsec.o`, `bsd_net_netsrc.o`,
`bsd_net_ntstat.o`, `bsd_netinet_tcp_cc.o`, `bsd_netinet_mptcp_subr.o`, `osfmk_vm_bsd_vm.o`,
`bsd_miscfs_devfs_devfs_vfsops.o`, `bsd_kern_kern_sig.o`. It is the largest single change this ledger
has made.

## Why this step exists: the distance to the OS was measured first, and it is `bsd_init`

`bsd_init()` is the **last** thing between this boot and the operating system. In
`osfmk/kern/startup.c`'s `kernel_bootstrap_thread` the sequence after it is

    bsd_init();
    OSKextRemoveKextBootstrap();
    kdebug_free_early_buf();
    serial_keyboard_init();
    vm_page_init_local_q();
    thread_bind(PROCESSOR_NULL);
    vm_pageout();          /*NOTREACHED*/

— and **every one of those is already real in the linked image**: `OSKextRemoveKextBootstrap`
`0x80108C4C`, `kdebug_free_early_buf` `0x8003D3B0`, `serial_keyboard_init` `0x80035DE8`,
`vm_page_init_local_q` `0x800196B0`, `thread_bind` `0x8009F8D0`, `vm_pageout` `0x80064950`. So the
minimum bar the goal states — *enter the OS* — is exactly "`bsd_init` returns", and nothing else
stands in the way.

`bsd_init` itself was then measured the same way, from the linked image rather than from the source:
**49 calls remain after `+0x80C`, and 24 of them are stubs.** 23 are defined by 22 pool objects; the
24th, `kmstartup` (`bsd_init + 0x844`), is not covered. `bsd/kern/subr_prof.c` defines it, `bsd_init.c`
calls it from the `#ifdef GPROF` arm, and the file **does not compile**: it uses `STATIC`, which
`bsd/kern/kern_sysctl.c` defines at its own line 204 and no header `subr_prof.c` includes — which is
why `out/xnu_kernel_obj/bsd_kern_subr_prof.o` does not exist and `kmstartup` is still a stub.

## Effect: 41 resolved, 312 added — and the way that was first computed was wrong

Read off `tools/entry_object_effect.py`'s model rather than summed by hand: **41 resolved (35
function, 6 storage) / 312 added (264 function, 48 storage)** — `767 → 1038` undefined, `615 → 844`
function, `152 → 194` storage, and the build's own stub list comes out at exactly **1038 records**.

**312 is the number this step exists to produce, and the first attempt at it was 7** (recorded as
defect 142). That pass took each object's undefined names, kept those that are stubs, and subtracted
every name the **pool** defines — concluding that only `addupc_task`, `bpf_attach`, `bpf_tap_in`,
`bpf_tap_out`, `bpfattach`, `bpfdetach` and `lo_ifp` would become new stubs. **The pool is a superset
of the link.** `entry_object_effect.py` asks `n not in image`, and it is right: 22 objects out of a
695-object pool satisfy 41 stubs and create 312 more, because the 271 names in between are defined by
pool objects that are **not in this link**. The 305-name error is the whole difference between "this
step nearly closes the network block" and "this step opens it".

## The prediction, and the measurement

**Prediction, written before the build: `stub_hit=ifnet_llreach_init`, caller key `0x80205F7C`** — the
answer `tools/xnu_entry_callwalk.py` gives for the built image (`bsd_init` → `dlil_init` →
`ifnet_llreach_init STUB`, the *first call inside the first object*), stated with its caveat: the name
comes from the same tool, so the run is what tests it.

**Measured on hardware: exactly that, to the byte.**

    line 3912:  xnu_entry_image_bytes=0x002c6bd0
    line 3913:  xnu_entry_bss_start=0x802c6c00
    line 3914:  xnu_entry_bss_end=0x8030b090
    line 3919:  xnu_entry_checksum=0x90506705
    line 3933:  xnu_entry_kv_written=0x00000097
    line 3935:  xnu_entry_kv_dropped=0x00000000
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3938:  xnu_entry_stub_caller_v=0x80205f7c
    line 3974:  stub_hit=ifnet_llreach_init
    line 3979:  No errors detected

`_w0`/`_w1` spell the key back as ASCII (`"8020"`, `"5f7c"`), and the call site is confirmed against
the linked image: `dlil_init` is at `0x80205BFC`, the `bl <ifnet_llreach_init>` is at
`dlil_init + 0x37C`, and the key is its return address, `dlil_init + 0x380`.

## The reading: 22 objects, 41 names retired, one call moved — and the road's length measured

`ifnet_llreach_init` is `dlil_init`'s **first** callee, so `dlil_init` cannot return while it is a
stub, so `bsd_init` does not get past `+0x80C` either. **The batch retires 41 names and moves the
frontier one call.** That is not a failure of the step; it is the step's result, and it generalises:

**The 22 objects contain 889 `bl`-to-stub call sites, covering 300 distinct stub names.** The pool
objects that define those 300 names number **84 more**, and their own bodies bring more again. The
frontier walk retires the calls it can *reach*, and reaching them one at a time costs one step per
name.

**And `tools/boot_closure.py` has already measured where that leads, for the whole boot:**

    objects available:               1214
    objects on the boot path:        1056
    symbols needed:                  8342
    needed but undefined everywhere: 122   (+9 compiler-runtime)
    files that fail to compile and are on the boot path:
          1  osfmk/kperf/kperfbsd.c        (1 symbol)

**So the boot path is a closed set of 1056 objects short 122 names, and exactly one file that fails
to compile is on it.** That is a finite, measured work list — and it is the answer to a question this
walk has been circling since 425 and that `mi4-a-kernel-entry-points-closure-is-the-kernel` had
already recorded for `arm_init`: **"grow the link object by object" does not converge, because the
frontier's closure is the subsystem.** 435 is the step that produces the same measurement for
`bsd_init`, from the other side.

## Layout

    entry text   0x2A5980 (2775424)     <- was 0x2363A0: +0x6F5E0
    entry image  0x2C6BD0 (2911184)     <- was 0x2542B0
    .data        0x802A8000 (0x1E6C8)   <- was 0x80238000 (0x1C018)
    .sysctl_set  0x802C66C8 (0x474)
    .init_array  0x802C6B3C (0x94)
    bss          0x802C6C00 .. 0x8030B090 (0x44490 = 279696)   <- was 270168
    layout       args 0x8030D000, topOfKernelData 0x80500000, tree 0x80700000, window 8388608
    headroom     2051952 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 2267413d346c100bc7515c1df7570063418fda6d49322fe265d039b77fc2e95a (5792 KB)
    entry bin    2911184 bytes; sha256
                 148fc583f5e8408fa7816410911ad1498616ec9484be8d5a62e252489848454d

**The whole memory layout moved with the image, and that is the build working rather than a wart.**
`args` `0x80298000 → 0x8030D000`, `topOfKernelData` `0x80400000 → 0x80500000`, tree
`0x80600000 → 0x80700000`, headroom 2051952 below the new top: the build **re-places** the layout
rather than overflowing it, which is why the `.data` boundary this step crosses
(`0x80238000 → 0x802A8000`, 16 KB-aligned) is a boundary and not a wall. The 22 objects bring
`.text` 0x619F0, `.rodata` 0x416 and `.rodata.str1.1` 0xA10D; against them the build retires 41 stub
bodies at 0x18 and 41 name slots.

**The `.bss` growth is `align64` again, at scale**: placed 0x12DE for 22 objects, and the section grew
0x44490 − 0x41F58 = 0x2538 — 0x12DE placed plus 0x125A of alignment. `.text` ends at 0x802A5980 and
`.data` starts at 0x802A8000, a 0x2680 gap the boundary absorbed.

Both stages rebuild byte-identically from the committed sources, after the block was written: the
entry bin above is the one the device ran, and the payload is unchanged by a comment-only edit.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure); nothing flashed, nothing written to storage.
25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`,
`checks=5` / `failures=0`, no `exception:` and no `panic:` line, `kv_dropped=0`. The only net armed
across the jump was the hardware watchdog (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bite_truncated=0x00000000`), dead-man PPI disarmed before it
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). The two `data abort` lines are the payload's own
probes at lines 201 and 3443. Device returned to Android on its own and was confirmed there
(`ro.product.device` cancro, `ro.build.version.release` 10). 301867 bytes / 3979 lines, ending
`No errors detected`.

Per-run logs stay apart: `/tmp/run425_kmsg.txt` … `/tmp/run434_kmsg.txt`, `run435_kmsg.txt`.

## What 436 has to be

**Not another batch of one object per name.** 435's own numbers say why: `dlil_init`'s first callee is
a stub, the 22 objects' stubs number 300 names across 84 further objects, and the boot path as a whole
is 1056 objects short 122 names. The step that reaches the goal is the one that links **the boot
path's closure** — the 1056 objects — and then reads the **122** names that nothing in the pool
defines, each of which is either supplied (as `stage90_pthread_functions.c` supplies the pthread
table and `nwk_wq.c` supplied a real `nwk_wq_init`) or left as a self-naming stand-in, which is what
the entry image already does for every name it cannot provide.

That step's prediction is not a single caller key: it is **122 names, sorted by whether the boot
reaches them**, plus a layout prediction for the largest link this project will have made. And its
run's most likely outcomes are the informative ones — a stop on one of the 122, or a boot that reaches
`vm_pageout`.

**And `kmstartup` stays a stub** until `subr_prof.c` is given the one macro it needs, which is a
one-line, stage-owned fix rather than a link.
