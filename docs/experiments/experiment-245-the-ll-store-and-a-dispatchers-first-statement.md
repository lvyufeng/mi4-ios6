# Experiment 245 — `vm_map_store_init_ll`, and a Dispatcher's First Statement

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change: the object 244's stop named

`vm_map_store_init` is defined by `out/xnu_kernel_obj/osfmk_vm_vm_map_store.o` — 908 bytes of text, 41
of `.rodata.str1.1`, 11 global definitions, 14 references, no storage at all. It is the object
`vm_map_create` calls, so linking it answers 244's stop exactly.

The object is a *dispatcher*, and that is the whole of this experiment's prediction problem. Its 11
definitions come in three layers: the portable entry points (`vm_map_store_init`,
`vm_map_store_entry_link`, `vm_map_store_lookup_entry`, `vm_map_store_update`,
`vm_map_store_copy_insert`, `vm_map_store_copy_reset`) and the two implementations they select
between — `vm_map_store_*_ll`, in `osfmk_vm_vm_map_store_ll.o` (776 bytes), and `vm_map_store_*_rb`,
in `osfmk_vm_vm_map_store_rb.o` (5808 bytes). Both stores are live in 4570: the RB tree is the lookup
structure and the "ll" store is the hole list. So the answer to "which one does this image call
first" is not in the source's program order, and the rule this project has had to learn twice applies
— **read it off the disassembly of the linked image**:

```
80080700 <vm_map_store_init>:
80080700:	push	{r4, lr}
80080704:	mov	r4, r0
80080708:	bl	800839f0 <vm_map_store_init_ll>     <- called unconditionally, first
8008070c:	movw	r1, #49361	; 0xc0d1
80080710:	ldr	r0, [r4, #24]
80080714:	movt	r1, #47789	; 0xbaad
80080718:	cmp	r0, r1                             <- rb_head_store.rbh_root == SKIP_RB_TREE?
8008071c:	popeq	{r4, pc}                   <- yes: return without the rb store
80080720:	mov	r0, r4
80080724:	pop	{r4, lr}
80080728:	b	80083a08 <vm_map_store_init_rb>     <- else: tail call
```

`0xbaadc0d1` is `SKIP_RB_TREE` (`osfmk/vm/vm_map_store.h:126`), so the comparison is
`vm_map_store_has_RB_support` (`vm_map_store.c:41-49`) exactly, and the source's `#ifdef
VM_MAP_STORE_USE_RB` did compile the rb call in. But `vm_map_store_init_ll` comes **first and
unconditionally**, before any of that matters — so the prediction is prepared before the branch is
reached at all.

**The prediction: `stub_hit=vm_map_store_init_ll`, `xnu_entry_stub_caller=0x8008070c`.** The walker
agrees and also lists the alternative the branch would produce
(`vm_map_store_init+0x28 -> vm_map_store_init_rb`), which the run cannot reach: the stop is at
`+0xc`, before the `cmp` at `+0x18`.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000041
 xnu_entry_kv_in_dram=0x00000041
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_map_store_init_ll
 xnu_entry_stub_caller=0x8008070c

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25 contracts that report it, and the device returned to Android on its own. The two lines are
31 + 34 = 65 = `0x41` bytes, which is `kv_written == kv_in_dram` to the byte, and `0x8008070c`
resolves to `vm_map_store_init+0xc` — whose `caller - 4` is `80080708: bl 800839f0
<vm_map_store_init_ll>`, the instruction the prediction was read from.

**The run is now three functions deep in a single boot step.** 244 stopped at
`vm_map_create+0x58`; 245 got through `vm_map_store_init`'s prologue and its first call, so
`kmem_init` → `vm_map_create` → `zalloc` returned, `vm_map_create` → `vm_map_store_init` was entered
and executed. The `vm_map_store_has_RB_support` comparison has *not* run yet, so which of the two
stores this device ends up with is still unmeasured — and the answer is not in doubt either way,
because the LL store is initialised first regardless.

## Cost

| | exp-244 | now |
| --- | --- | --- |
| undefined | 636 | **639** |
| function stubs | 551 | **554** |
| storage stubs | 85 | **85**, unchanged |
| entry text | 610225 B | **611345 B** (+1120) |
| entry image | 719736 B | **719736 B**, unchanged |
| entry `.bss` | 0x800af4f0–0x800de208 | **unchanged** |
| `__entry_image_end` / `end_kern` | 0x800de208 / 0x800df000 | **unchanged** |
| layout / headroom | args +917504, … , 1187320 B | **unchanged** |
| payload text | 1211954 B | **1211954 B**, unchanged |

Resolved 10, added 13 — the object's portable entry points in exchange for its two implementations:

```
resolved   vm_map_store_copy_insert vm_map_store_copy_reset vm_map_store_entry_link
           _vm_map_store_entry_link vm_map_store_entry_unlink _vm_map_store_entry_unlink
           vm_map_store_init vm_map_store_lookup_entry vm_map_store_update
           vm_map_store_update_first_free
added      vm_map_store_copy_insert_ll/_rb  vm_map_store_copy_reset_ll/_rb
           vm_map_store_entry_link_ll/_rb   vm_map_store_entry_unlink_ll/_rb
           vm_map_store_init_ll/_rb         vm_map_store_lookup_entry_rb
           update_first_free_ll/_rb
```

`vm_map_store_has_RB_support` is the one definition of the 11 that resolves nothing: nothing
referenced it, because `vm_map_store_init` inlined it into a `cmp` — which is why the disassembly is
the only place it is visible as a decision at all.

The +1120 bytes of text is the 908 of the object plus three new 24-byte stubs and their names. **The
image, the `.bss`, the layout and the headroom are all unchanged**, so unlike 244 this step moved no
address: the growth fitted inside the linker script's alignment padding, and the payload was rebuilt
only to carry a new copy of the same-sized `.bin`. That is why this experiment's numbers table has
four rows that say "unchanged" — it is a measurement, not an assumption, and it is the first step in
this sequence where a linked object cost nothing in layout.

## What is next

The LL store. `vm_map_store_init_ll` is in `out/xnu_kernel_obj/osfmk_vm_vm_map_store_ll.o` — 776
bytes of text, 8 global definitions (`first_free_is_valid_ll`, `update_first_free_ll`, the four
`vm_map_store_*_ll` wrappers, `vm_map_store_init_ll` and `vm_map_store_lookup_entry_ll`) — and exactly
two references, `_consume_printf_args` and `OSCompareAndSwapPtr`. **Both are already defined by
objects the image links** (`bsd_kern_subr_prf.o` since experiment 235, `libkern_gen_OSAtomicOperations.o`
since 228), which is the first time in this sequence that a step's object has nothing missing under
it at all. So this step's stop is not going to be one of those two names: it will be somewhere inside
one of the eight definitions, or at whatever *those* call. That is a prediction for experiment 246 to
make from the disassembly of its own image, and the honest thing to write down here is that it cannot
be made from the object list.

## Reproduce

```bash
# the change
grep -n 'OSFMK_VM_VM_MAP_STORE_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh

(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:'            # -> 639 undefined, 554 function(s), 85 storage
grep -E 'text size|image bytes|bss |layout|headroom'   # -> 611345 B, image/layout unchanged

# the prediction, from the image the device will run
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<vm_map_store_init>:/{f=1} f{print} f&&/^$/{exit}'
./tools/xnu_entry_callwalk.py --root vm_map_store_init
./tools/host_resolve_entry_addr.sh 0x8008070c    # -> vm_map_store_init+0xc

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12

# what the source says the comparison at +0x18 is
sed -n '41,60p' external/xnu-4570.1.46/osfmk/vm/vm_map_store.c
grep -rn 'define SKIP_RB_TREE' external/xnu-4570.1.46/osfmk/vm/

# where the next object is
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_vm_vm_map_store_ll.o
```
