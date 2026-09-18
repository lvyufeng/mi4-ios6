# Experiment 283 — `osfmk_kern_host.o`, three `kernel_set_special_port` calls, and a payload defect the step's own growth triggered

**Step:** link the object that defines `kernel_set_special_port` — `osfmk/kern/host.c`,
`osfmk_kern_host.o` — the name the plain run of experiment 282 stopped at.
**Prediction:** a report, and not a silence; `kernel_set_special_port` is the **only** stub in
`ipc_host_init`'s body, `ipc_host_init` is the **last** call in `ipc_init`, and `kernel_bootstrap` has
no stubs at all — so all three run to completion and the next report must come from *deeper*, from
inside one of the init functions `kernel_bootstrap` calls after `ipc_init` returns. The name was not
predicted; the shape was.
**Result:** exactly that shape, and the name three functions later: **`stub_hit=clock_config` at
`machine_init+0xc`** — the *first* of the twelve candidates. **19 resolved, 7 added**, the totals
predicted to the unit (921 → 909 undefined). And in between, a payload defect that this step's 4 KB of
extra entry-image text triggered and that the payload's own preflight refused to boot on: two bugs in
`xnu_arm_vm_init_full_pmap.c`, fixed here.

## The object, measured

`osfmk_kern_host.o` is **4920 bytes of text, 48 of data, 452 of bss, 26 definitions and 60
references**. Against the image it resolves **19** — eighteen function stubs plus `realhost`, a
*storage* stand-in of 0x194 bytes whose real definition the object carries:

```
host_get_io_master   host_get_special_port      host_info          host_kernel_version
host_page_size       host_priv_self             host_priv_statistics  host_processor_info
host_processors      host_processor_set_priv    host_processor_sets   host_self
host_set_atm_diagnostic_flag   host_set_multiuser_config_flags
host_set_special_port          host_statistics     host_statistics64
kernel_set_special_port        realhost
```

and adds **7** new obligations, each defined by an object this build has already compiled, so every
stand-in's size still comes from its own definition rather than from a guess — the rule since 244:
`atm_set_diagnostic_config`, `avenrun`, `commpage_update_multiuser_config`, `dead_task_statistics`,
`mach_factor`, `mac_task_check_set_host_special_port`, `vm_purgeable_stats`. `avenrun` and `mach_factor`
are the two *variables* among them, 12 bytes each; the build's own counts (function stubs −14, storage
+2, undefined −12) are the measurement.

`realhost` is the interesting one: 277 sized its stand-in at 0x194 bytes from this same object and
`ipc_host_init` wrote every byte of it, and this step replaces the stand-in with the real definition —
**the same size to the byte**, at `0x80148240`, which is the first time the 277 measurement could be
checked against the thing it was measuring.

## The step's frontier, and why it cannot stop short

Measured on the host before the run, by listing every `bl` in each function and checking each target
against the image's own undefined list:

```
ipc_host_init      lck_mtx_init, ipc_port_alloc_special, ipc_kobject_set,
                   ipc_port_make_send x3, kernel_set_special_port x3   <- the only stubs
ipc_init           kmem_suballoc x2 (panic-guarded), ipc_host_init    <- ipc_host_init is last
kernel_bootstrap   every one of its calls is real, 0 of 46 stubs
```

`ipc_host_init` is a three-fold repetition of `ipc_port_alloc_special` / `ipc_kobject_set` /
`ipc_port_make_send` / `kernel_set_special_port`, one per special port, and the stub the 282 run
stopped at is the `HOST_SECURITY_PORT` one (`mov r1, #0` at the call site, matching `ipc_host.c:113`).
So this step cannot stop anywhere in those three functions: the frontier it names is *transitive*, and
none of the twelve functions `kernel_bootstrap` calls afterwards calls a stub *directly* either — which
is why the name had to come from the run.

## The payload defect the step triggered

**Run one was refused before it booted anything.** `loader_xnu_entry_stub_status` came back
`0xd0008910` with `failure_mask=0x00008910` — `BAD_RETURN_STATUS`, `NO_OUTPUT`, `SAFETY_BOUNDARY` and
`ARM_VM_INIT_FULL_PMAP` — and the only new line in the log was

```
stage90_xnu_arm_vm_init_full_pmap_high_va_data_verified=0x00000000
```

against `0x00000001` in every previous run. `kernel_entry bad: Mach-O/XNU loader preflight`,
`platform_reboot`, and the device came back on its own. That is the safety gate working: 4 KB more XNU
code in the entry image grew the payload past an assumption of the Stage84 live-pmap rung, and the rung
said so rather than letting the boot proceed on a pmap that disagrees with itself.

The check writes and reads `stage90_full_pmap_probe_word` through **both** its own address and
`STAGE90_VIRT_BASE +` that address, so two mappings have to cover it, and both were wrong:

- **Phase 1** (the low identity sections) was a hardcoded pair covering PA [0, 2 MB). The probe word
  sat at `0x001fc0b4`, which they covered; it moved to `0x002000b4`, one section past the end. Phase 1
  now **loops over the image**, which is what Phase 5 already does for the same reason. Fixing that
  alone changed nothing.
- **Phase 3** (the 256 MB RAM direct map starting at `STAGE90_VIRT_BASE + 0x200000`, written as 1 MB
  *sections*) runs *after* Phase 2 and wins the L1 entries Phase 2's page-mapped window had just
  claimed, so the high-VA read went to PA `0x802000b4` instead of PA `0x002000b4`. Phase 3 now
  **skips the slots the image's window covers** — and the image's window is the mapping the check
  exists to test.

Both are the same defect class the Phase 2 comment already records ("embedding a larger XNU entry
image moved a .bss variable 148 bytes past the end of the window"), and both are now derived from
`__stage90_image_end` rather than spelled out, so the next growth moves the mappings with it. Worth
carrying forward: `HIGH_VA_DATA` fires for a failure in *either* half of the comparison, and in the
second run it was the identity half that was broken — the name points at the wrong side.

## The run that reports

After the fix: **301113 bytes**, the preflight clean, `high_va_data_verified=0x00000001`, and

```
stub_hit=clock_config        xnu_entry_stub_caller=0x800076c4
```

`0x800076c4` is `machine_init + 0xc`, and `machine_init` is the *first* of the twelve init functions
`kernel_bootstrap` calls after `ipc_init` returns. So one run measured all of the following:

- `ipc_host_init` completes, including all three `kernel_set_special_port` calls, with `realhost` now
  the real 0x194-byte object — the first time this kernel has written a special port into a host it
  actually owns;
- `ipc_init` completes (`kmem_suballoc` twice and `ipc_host_init` once);
- `kernel_bootstrap` runs from `PE_parse_boot_argn` through `mapping_free_prime` and into
  `machine_init`, with no stub anywhere in its own body;
- and the first thing it needs that this image does not have is `clock_config`.

Build: 921 → 909 undefined, 827 → 813 function stubs, 94 → 96 storage, text 1014960 → 1019088, image
bytes 1115688 → 1132120, `.bss` `0x80113b50` .. `0x80149dc8`, headroom 1811320 → 1794616.

**Safety on both runs:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
`xnu_entry_failures=0x00000000`, and the device returned to Android by itself on both — the first
through the payload's own `platform_reboot`, the second through the normal exit.

**Next:** experiment 284 — `osfmk/kern/clock.c`, `osfmk_kern_clock.o`, the object `clock_config` is
defined in, which `machine_init` calls before anything else it does.
