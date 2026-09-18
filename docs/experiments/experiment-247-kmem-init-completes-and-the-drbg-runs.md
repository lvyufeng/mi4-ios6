# Experiment 247 — `kmem_init` Completes, the DRBG Runs, and `kernel_map` Gets Its First Region

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

`osfmk_vm_vm_map_store_rb.o` — 5808 bytes of text, 24 global definitions, 4 references (`panic`,
`vm_map_holes_zone`, `zalloc`, `zfree`), all four already defined, so (like 246) the stop has to be
inside the object rather than at one of its references. It is the largest object since 239's
`vm_map.o`, and the one that brings the red-black tree (`rb_head_RB_INSERT`, `rb_head_RB_REMOVE`,
`rb_head_RB_MINMAX`, `rb_node_compare`, `update_holes_on_entry_creation`,
`update_holes_on_entry_deletion`, `vm_map_combine_hole`, `vm_map_delete_hole`) and the hole-list code
that allocates from `vm_map_holes_zone` — which is a storage definition in the already-linked
`osfmk_vm_vm_map.o`, so unlike most storage in this sequence it is real data and not a stand-in.

The definition 246's stop named is also three instructions:

```
80081b18 <vm_map_store_init_rb>:
80081b18:	mov	r1, #0
80081b1c:	str	r1, [r0, #24]     <- hdr->rb_head_store.rbh_root = 0
80081b20:	bx	lr
```

So this step does not resolve a stop either — like 246's `bx lr`, it *moves* it, and it moves it out
of the dispatcher entirely.

## The prediction, and the three outcomes it named

`kmem_init`'s closure was enumerated from the disassembly first, call target by call target, because
for the first time in this sequence the prediction was not "which stub is next in a stub" but "does
this whole function finish":

```
kmem_init     -> vm_map_create  pmap_virtual_region  vm_map_enter  panic  __aeabi_uldivmod
vm_map_create -> zalloc  panic  vm_map_store_init  zalloc  lck_rw_init  lck_mtx_init_ext
```

**Every one of those is real.** So the prediction was that `kmem_init` runs to completion, and the
question becomes what `vm_mem_bootstrap` does next — which is this, from `vm_init.c:146-161`:

```c
	kmem_init(start, end);
	kmem_ready = TRUE;
	if (!PE_parse_boot_argn("kmapoff", &kmapoff_pgcnt, sizeof (kmapoff_pgcnt)))
		kmapoff_pgcnt = early_random() & 0x1ff;		/* 9 bits */
	if (kmapoff_pgcnt > 0 &&
	    vm_allocate_kernel(kernel_map, &kmapoff_kaddr,
	        kmapoff_pgcnt * PAGE_SIZE_64, VM_FLAGS_ANYWHERE, VM_KERN_MEMORY_OSFMK) != KERN_SUCCESS)
		panic("cannot vm_allocate %u kernel_map pages", kmapoff_pgcnt);
```

and the compiled control flow, which is worth writing down because two of its four exits are stubs and
one is a panic:

```
80040398: bl PE_parse_boot_argn("kmapoff")   ; our boot args have no such key
800403a0: beq .Lelse            -> 800403b4: bl early_random ; bfc r0,#9,#23 ; str r0,[r4]
800403c4: beq .Lskip            ; kmapoff_pgcnt == 0 -> skip the allocation
800403f0: bl vm_allocate_kernel (STUB)   <- the allocation, reached when the 9 bits are non-zero
800403f8: beq .Lskip            ; returned KERN_SUCCESS
80040408: bl panic              ; otherwise
```

**The prediction: `stub_hit=vm_allocate_kernel`, caller `vm_mem_bootstrap+0xf8`**, with the two
alternatives named rather than hidden — the DRBG path inside `early_random` (which makes indirect
calls the walker cannot follow, per 218's finding that the factory writes the function pointer the
next call goes through), and the 1-in-512 case where `early_random() & 0x1ff` is zero, which would
skip to `pmap_init` (real) and stop at `kext_alloc_init` (`vm_mem_bootstrap+0x1a4`) instead. All three
outcomes are distinguishable from the run's own two keys, which is why the caller report was worth
building: the two callers are 0xb0 bytes apart.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003f
 xnu_entry_kv_in_dram=0x0000003f
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_allocate_kernel
 xnu_entry_stub_caller=0x800403f4

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, the device back on Android on its own. `kv_written == kv_in_dram == 0x3f` (63 bytes =
29 + 34), and `0x800403f4` resolves to `vm_mem_bootstrap+0xf8`, whose `caller - 4` is
`800403f0: bl 80085030 <vm_allocate_kernel>` — the call site itself, because this call was a `bl`
(246's was a tail call, and the two cases are told apart exactly this way).

**`kmem_init` completed.** That is a first for this sequence: 244, 245 and 246 each stopped one call
deep inside the function; this run finished it. Everything below is therefore measured rather than
predicted:

- **`kernel_map` exists.** `vm_map_create` ran in full — `zalloc(vm_map_zone)` returned, the
  dispatcher initialised both stores (`vm_map_store_init_ll` is empty, `vm_map_store_has_RB_support`
  is TRUE, `vm_map_store_init_rb` wrote `rbh_root = 0`), `vm_map_supports_hole_optimization` was TRUE
  so a second `zalloc(vm_map_holes_zone)` created the hole list, and `lck_rw_init` +
  `lck_mtx_init_ext` initialised the map's two locks.
- **A region was entered into it.** `pmap_virtual_region(0, ...)` returns TRUE for
  `region_select == 0` on `__ARM_VMSA__ == 7` with `*start = gVirtBase & 0xFFC00000 = 0x80000000` and
  `*size = 0x40000000`, so `kmem_init`'s loop called
  `vm_map_enter(kernel_map, 0x80000000, 0x40000000, 0, VM_FLAGS_FIXED, …)` — and the `panic` on a
  non-`KERN_SUCCESS` return (`kmem_init+0x7c`) did **not** fire, so it succeeded. That is XNU's own
  RB-tree/hole-list map code reserving a gigabyte on this hardware, allocating its entries with
  `vm_map_entry_create` as it went.
- **`PE_parse_boot_argn("kmapoff", …)` returned FALSE** — the boot args this project hands over carry
  no such key, which is the same measurement the branch structure implies but is now on the record.
- **`early_random()` ran and returned a value whose low nine bits are not zero.** On its first call
  that means the whole path executed (`random.c:318-373`): `PE_get_random_seed` filled
  `EntropyData.buffer` to its full size — the `panic("EntropyData needed %lu bytes, but got %u")`
  did not fire — then `entropy_readall`, then `ccdrbg_factory_nisthmac`, then `ccdrbg_init` with a
  `ml_get_timebase()` nonce and the boot CPU as personalisation, then `ccdrbg_generate`, then
  `cc_clear`. **None of those is a stub in this image** (checked by name against the generated stub
  list), and both `ccdrbg_init`'s and `ccdrbg_generate`'s `panic`s would have shown as an
  undefined-instruction exception in `fleh_undef`. None did. So the first cryptographic path in this
  boot ran, on the device, with a real seed: the HMAC-SHA1 DRBG.

None of that is inference from the *absence* of a line in a log alone — each has a positive marker in
this run's two keys: the stop is past `early_random`'s call (`+0xf4` > `+0xb4`), the `beq` that would
have skipped the allocation was not taken, and the `panic` call is *after* the stop.

## Cost

| | exp-246 | now |
| --- | --- | --- |
| undefined | 633 | **626** |
| function stubs | 548 | **541** |
| storage stubs | 85 | **85**, unchanged |
| entry text | 611793 B | **617809 B** (+6016) |
| entry image | 719736 B | **719736 B**, unchanged |
| `.bss` / `__entry_image_end` / `end_kern` | 0x800af4f0–0x800de208 / 0x800de208 / 0x800df000 | **unchanged** |
| layout / headroom | args +917504, … , 1187320 B | **unchanged** |
| payload text | 1211954 B | **1211954 B**, unchanged |

Resolved 7, added **0** — the whole `_rb` family, and nothing else:

```
resolved   vm_map_store_init_rb  vm_map_store_lookup_entry_rb  vm_map_store_entry_link_rb
           vm_map_store_entry_unlink_rb  vm_map_store_copy_insert_rb
           vm_map_store_copy_reset_rb  update_first_free_rb
added      (none)
```

+6016 bytes of text is the object's 5808 less the 168 of seven retired 24-byte stubs, plus their
names and alignment. **Three steps in a row now — 245, 246 and 247 — have linked an object without
moving a single address**: the image, `.bss`, `__entry_image_end`, the layout, the headroom and the
payload's own size are all unchanged, because the linker script's alignment padding has absorbed the
6464 bytes of text these three steps added (611345 → 617809). That is worth stating as a measured
fact rather than a hope, because it will stop being true eventually and the difference will matter.

## What is next

`vm_allocate_kernel` is defined by `out/xnu_kernel_obj/osfmk_vm_vm_user.o` — 16196 bytes of text,
91 references, so unlike the last three steps this one brings a lot with it: `vm_map_copy*`,
`vm_map_protect`, `vm_map_remove`, `vm_map_wire_kernel`, the `upl_*` family, and references to
`ipc_port_*` and `memory_object_*`, many of which will be new stubs. The prediction for 248 is a
prediction about *its* disassembly, and the caller report makes the run's answer unambiguous either
way.
