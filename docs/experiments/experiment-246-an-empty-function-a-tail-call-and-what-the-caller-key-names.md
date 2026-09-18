# Experiment 246 — an Empty Function, a Tail Call, and What `xnu_entry_stub_caller` Names

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change, and the surprise in it

`osfmk_vm_vm_map_store_ll.o` — 776 bytes of text, 8 definitions, and exactly two references,
`_consume_printf_args` and `OSCompareAndSwapPtr`, **both already defined** by objects the image links.
This is the first step in the sequence whose object has nothing missing underneath it at all, so the
stop had to be inside one of the eight definitions rather than at a name.

The definition 245's stop named is the empty one:

```
80080b38 <vm_map_store_init_ll>:
80080b38:	bx	lr
```

`vm_map_store_init_ll` is `void ... { }` in 4570 — the LL store has nothing to initialise, and the name
exists because the dispatcher in `vm_map_store.c` calls it unconditionally before consulting
`vm_map_store_has_RB_support`. Four bytes. So linking this object does not resolve the stop, it moves
it: past the call, to whatever the dispatcher does next.

## The prediction, and the branch a runtime value decides

```
80080700 <vm_map_store_init>:
80080708:	bl	80080b38 <vm_map_store_init_ll>   <- now real: `bx lr`, returns
8008070c:	movw	r1, #49361	; 0xc0d1
80080710:	ldr	r0, [r4, #24]                     <- hdr.rb_head_store.rbh_root
80080714:	movt	r1, #47789	; 0xbaad
80080718:	cmp	r0, r1
8008071c:	popeq	{r4, pc}                  <- equal to SKIP_RB_TREE: return
80080720:	mov	r0, r4
80080724:	pop	{r4, lr}
80080728:	b	80083c80 <vm_map_store_init_rb>   <- else: tail call, and a 24-byte stub
```

**The prediction: `stub_hit=vm_map_store_init_rb`, caller `0x8004651c`.** The branch is decided by a
value no walk can read (`hdr.rb_head_store.rbh_root != SKIP_RB_TREE`), which is the shape of failure
that cost experiment 234 a run — so it was resolved the way that one was, by finding the writers:

```
osfmk/vm/vm_map_store.c:45   the only reader:  hdr->rb_head_store.rbh_root == SKIP_RB_TREE
osfmk/vm/vm_map.c:7868, 8509, 8824, 8869, 10524, 11336, 11389   the writers
```

All nine uses are in `vm_map_store.c`'s predicate and in `vm_map.c`'s **copy, clip and context-switch**
paths — `vm_map_copy`, `vm_map_clip_start`/`_end`, `vm_map_switch_context` and the copy variants. None
of them can have run: the boot is inside `kmem_init`, which is creating the first map. So the sentinel
is not in this header, `vm_map_store_has_RB_support` returns TRUE, and the tail call is taken.

The caller is the interesting half. The call is a `b`, and the two instructions before it are
`pop {r4, lr}` — restoring the `lr` that the prologue's `push {r4, lr}` saved, which is
`vm_map_create`'s return address from `bl vm_map_store_init`. **So the reported `lr` is not the tail
call site** (`vm_map_store_init+0x28`) but the caller *of the caller*: `0x8004651c`,
`vm_map_create+0x5c` — the same address experiment 244 reported for a different stub. That is not a
defect in the instrumentation; it is what a return address is, and it is a reading anyone using the
key has to know.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000041
 xnu_entry_kv_in_dram=0x00000041
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_map_store_init_rb
 xnu_entry_stub_caller=0x8004651c

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25 that report it, the device back on Android on its own. `kv_written == kv_in_dram == 0x41`
(65 bytes = 31 + 34), and the caller resolves exactly as predicted:

```
$ ./tools/host_resolve_entry_addr.sh 0x8004651c
0x8004651c  vm_map_create+0x5c
          caller-4 = 0x80046518  vm_map_create+0x58   <- the `bl`, if the call was one
```

**The mismatch is the finding.** The stub that was hit is `vm_map_store_init_rb`, and the instruction
at `caller - 4` is `bl vm_map_store_init` — a **different symbol**. That is how a tail-called stub
reads: the key holds the return address the *first* call put in `lr`, so it names the function that
called the tail-calling function. With one `bl`-called stub the two coincide (every stop from 244 to
245 had `caller - 4` pointing at the stub that was hit); from here they will not always, and the way
to tell them apart is in the key's own value, not in a guess: resolve `caller - 4` and see whether its
target is the stub that stopped the run.

## What ran, and what did not

Three calls of one boot statement are now behind the run: `zalloc` returned, `vm_map_store_init` was
entered, `vm_map_store_init_ll` was **called and returned** — an empty function executing on the
hardware — and `vm_map_store_has_RB_support` was evaluated and answered TRUE. `vm_map_create` has not
returned and `kmem_init` is still on its first statement.

## Cost

| | exp-245 | now |
| --- | --- | --- |
| undefined | 639 | **633** |
| function stubs | 554 | **548** |
| storage stubs | 85 | **85**, unchanged |
| entry text | 611345 B | **611793 B** (+448) |
| entry image | 719736 B | **719736 B**, unchanged |
| `.bss` / `__entry_image_end` / `end_kern` | 0x800af4f0–0x800de208 / 0x800de208 / 0x800df000 | **unchanged** |
| layout / headroom | args +917504, … , 1187320 B | **unchanged** |
| payload text | 1211954 B | **1211954 B**, unchanged |

Resolved 6, added **0** — and the two of the object's eight definitions that were never stubs are
worth naming, because they are the ones a "resolved everything I linked" expectation would get wrong:

```
resolved   update_first_free_ll  vm_map_store_copy_insert_ll  vm_map_store_copy_reset_ll
           vm_map_store_entry_link_ll  vm_map_store_entry_unlink_ll  vm_map_store_init_ll
added      (none)
never referenced   first_free_is_valid_ll (0xc4 bytes)   vm_map_store_lookup_entry_ll (0xc4 bytes)
```

The +448 bytes of text is 776 of object minus 192 of six retired 24-byte stubs, plus their names and
alignment. **The image, `.bss`, `__entry_image_end`, the layout, the headroom and the payload's size
are all unchanged again** — two steps in a row now, both measured. The payload was rebuilt only to
carry a new copy of a same-sized `.bin`.

## What is next

`vm_map_store_init_rb` is in `out/xnu_kernel_obj/osfmk_vm_vm_map_store_rb.o` — 5808 bytes of text,
24 global definitions, 4 references (`panic`, `vm_map_holes_zone`, `zalloc`, `zfree`), **all four of
which are already defined** (`vm_map_holes_zone` is a storage definition in `osfmk_vm_vm_map.o`, which
has been linked since 239; the other three are in `osfmk_kern_zalloc.o` and `osfmk_kern_debug.o`). So
this step too may have nothing missing underneath it — the second in a row. It is the largest object
linked since 239 and the one that brings the red-black tree (`rb_head_RB_INSERT`, `rb_head_RB_REMOVE`,
`rb_node_compare`, `update_holes_on_entry_creation`) and the hole-list code that allocates from
`vm_map_holes_zone`.

## Reproduce

```bash
# the change
grep -n 'OSFMK_VM_VM_MAP_STORE_LL_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh

(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:'            # -> 633 undefined, 548 function(s), 85 storage
grep -E 'text size|image bytes|bss |layout|headroom'

# the empty function, and the dispatcher's branch
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<vm_map_store_init_ll>:/{f=1} f{print} f&&/^$/{exit}'
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<vm_map_store_init>:/{f=1} f{print} f&&/^$/{exit}'

# the runtime value that decides the branch: who writes the sentinel
grep -rn 'SKIP_RB_TREE' external/xnu-4570.1.46/osfmk/vm/

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8004651c   # caller-4 is `bl vm_map_store_init`, not the stub hit

# where the next object is
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_vm_vm_map_store_rb.o
```
