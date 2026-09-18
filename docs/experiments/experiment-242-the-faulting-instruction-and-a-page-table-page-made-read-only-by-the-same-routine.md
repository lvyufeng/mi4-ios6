# Experiment 242 — the Faulting Instruction, and a Page-Table Page the Same Routine Had Already Made Read-Only

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

Experiment 241 ended at `DFAR = 0x80300000`, `DFSR = 0x0000080f` — a write, permission fault, level 2 —
and left one question: *which instruction*, and therefore whether the fault is inside the protection
code that created the mapping or a later write into a region it had already protected. That is one
extension of the handler exp-241 designed:

```c
    __asm__ volatile ("mov %0, lr" : "=r"(lr_abt));
    __asm__ volatile ("mrs %0, spsr" : "=r"(spsr));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(ttbr1));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr));
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    ...
    /* LR_abt - 8 for a data abort: the instruction that could not complete. */
    pc_abt = lr_abt - 8u;
    entry_kv("xnu_entry_data_abort_pc", pc_abt);
    /* The instruction itself, so a pc_abt that is not an instruction is visible as one. */
    insn = 0u;
    if (entry_image_ptr((uintptr_t)pc_abt)) {
        insn = entry_word_at((uintptr_t)pc_abt);
    }
    entry_kv("xnu_entry_data_abort_insn", insn);
```

plus four globals the same handler reads — `cpu_ttep`, `avail_start`, `gPhysBase`, `mem_size` — and
the same treatment for `fleh_prefabt` (`lr_abt`, `lr_abt - 4`, the word at it, SPSR, and the four MMU
registers), which has never fired in any run and is written so that it names itself if it ever does.
The word at `pc_abt` is read through `entry_image_ptr`, the guard exp-241 re-pointed at the linker's
`__entry_text_start .. __entry_image_end`, so a `pc_abt` outside the image is one zero and not a
second fault inside the fault handler.

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: exception: data abort
 xnu_entry_kv_written=0x00000228
 xnu_entry_kv_in_dram=0x00000228
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_data_abort_dfar=0x80300000
 xnu_entry_data_abort_dfsr=0x0000080f
 xnu_entry_data_abort_lr=0x80024f64
 xnu_entry_data_abort_pc=0x80024f5c
 xnu_entry_data_abort_insn=0xe4805004
 xnu_entry_data_abort_spsr=0x80000093
 xnu_entry_data_abort_ttbr0=0x8020404a
 xnu_entry_data_abort_ttbr1=0x8020404a
 xnu_entry_data_abort_ttbcr=0x00000002
 xnu_entry_data_abort_sctlr=0x30c5787d
 xnu_entry_data_abort_cpu_ttep=0x80204000
 xnu_entry_data_abort_avail_start=0x80301000
 xnu_entry_data_abort_gphysbase=0x80000000
 xnu_entry_data_abort_mem_size=0x00800000

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `safety_boundary_preserved=0x00000001`
and `mmu_unchanged=0x00000001` where they are reported, `persistent_write_attempted=0x00000000` in all
25 contracts that report it, and the device returned to Android on its own. There is no `stub_hit=`
line, so no unprovided function was reached, and no `prefetch_abort` key, so `fleh_prefabt` still has
not fired. `kv_written == kv_in_dram == 0x228` = 552 bytes, which is 14 keys at the same 33 bytes a
key as exp-240's 25.

## The prediction: three of four to the byte, and the one that was not is the finding

| key | predicted | measured | |
| --- | --- | --- | --- |
| `cpu_ttep` | `0x80204000` | `0x80204000` | `boot_ttep + ARM_PGBYTES*4`, `boot_ttep = topOfKernelData = 0x80200000` |
| `ttbr0` / `ttbr1` | `0x80204xxx` | `0x8020404a` | `set_mmu_ttb(cpu_ttep)` — **the boot tables' regime** |
| `gPhysBase` | `0x80000000` | `0x80000000` | the boot_args, and now also the link address |
| `mem_size` | `0x00800000` | `0x00800000` | `args->memSize`, left alone against `xmaxmem` |
| `avail_start` | `~0x8020C000` | **`0x80301000`** | **wrong, by 245 pages** |

**`TTBR0 = 0x8020404a` settles the question exp-241 asked.** Its base is `cpu_ttep`, which
`arm_vm_init` installs itself (`set_mmu_ttb(cpu_ttep)`, `:487`), so the pmap has *not* installed tables
of its own and the fault is inside `arm_vm_init`'s own setup — and since the write faults on a page
*entry*, the L1 table `cpu_ttep` is live and something in this boot has already converted the L1 entry
for `0x80300000` into a coarse page table.

**`avail_start = 0x80301000` is one page above `DFAR`, and that is not a coincidence.** The prediction
counted only the two pages `arm_vm_init`'s pre-initialization loop adds — and that loop runs *after*
`arm_vm_prot_init`. What it walked past is `pmap_bootstrap`'s own table allocation
(`pmap.c:2841-2850`), which is arithmetic this project can do exactly:

```
  pmap_struct_start                        = 0x8020A000   arm_vm_init.c:399, cpu_ttep + 6 pages
  + pp_attr_table_size   2048 * 2 = 0x1000 = 0x8020B000
  + io_attr_table_size   0 * 1            = 0x8020B000   niorgns = 0: no pmap-io-ranges
  + pv_lock_table_size   npages  = 0x800  = 0x8020B800
  + pv_head_size         round_page(4*2048) = 0x2000 -> 0x8020D800
  + ptd_root_table_size  sizeof(pt_desc_t) * 4096
                         PT_INDEX_MAX == 1 for __ARM_VMSA__ == 7 (pmap.c:281-284)
                         sizeof = 8 + 4 + 4 + 4 = 20 -> 0x14000
  round_page(...)                          = 0x80222000   <- the first page avail_start hands out
```

So the first page-table page `arm_vm_prot_init` allocates is `0x80222000`, not `0x8020A000`, and the
measured `avail_start` at the fault is consistent with that to the page: `0x80300000 - 0x80222000` is
`0xDE000` = **222 pages**, which is what `0x80301000` says the allocator had walked.

## `pc_abt` names the writer: the first PTE store of `pmap_init_pte_static_page`

```
80024f1c <pmap_init_pte_static_page>:
  80024f38:	mov	r3, #0
  80024f3c:	movw	lr, #1554	; 0x612
  80024f40:	movw	r4, #1022	; 0x3fe
  80024f44:	mov	r0, r1			; r0 = ppte
  80024f48:	mov	r5, r2			; r5 = pa
  80024f4c:	cmp	r3, r4
  80024f50:	bfi	r5, lr, #0, #12		; (pa & ~0xfff) | 0x612
  80024f54:	addls	r2, r2, #4096		; pa += 4096
  80024f58:	addls	r3, r3, #1
  80024f5c:	str	r5, [r0], #4		; *ppte++ = ptmp   <-- pc_abt, insn 0xe4805004
  80024f60:	ldrls	r5, [ip]		; avail_end
  80024f64:	cmpls	r2, r5
  80024f68:	bcc	80024f48
```

`insn = 0xe4805004` is `str r5, [r0], #4` and it is the word at `pc_abt`, read out of the image by the
handler — the reported instruction and the disassembly agree, which is what that key is for.
`lr_abt = 0x80024f64 = pc_abt + 8`, the architecture's own offset for a data abort.

`pmap_init_pte_static_page` has **exactly one caller in the whole image**, `0x8001880c`, inside
`arm_vm_page_granular_helper`'s `else` branch — the branch taken when the L1 TTE for the range is not
already a coarse table:

```c
		} else {
			/* TTE must be reincarnated COARSE. */
			ppte = (pt_entry_t *)phystokv(avail_start);
			avail_start += ARM_PGBYTES;

			pmap_init_pte_static_page(kernel_pmap, ppte, pa);
```

and the built code is explicit that `ppte` is the **pre**-increment value while `avail_start` is left
incremented:

```
  800187e4:	ldr	r3, [r1]		; r3 = avail_start        (old)
  800187f4:	add	r6, r3, #4096
  800187f8:	str	r6, [r1]		; avail_start += 4096
  800187fc:	sub	r1, r3, r7		; r7 = gPhysBase
  80018804:	add	r6, r1, r5		; r5 = gVirtBase -> phystokv(old avail_start)
  80018808:	mov	r1, r6			; ppte = r6
  8001880c:	bl	80024f1c
```

So at the fault `ppte = phystokv(avail_start) = 0x80300000`, and `avail_start` afterwards is
`0x80301000` — both measured. `DFAR = 0x80300000 = ppte` **exactly**, so this is `i == 0`: the very
first of the routine's 1024 stores, into the page-table page the helper had just taken from
`avail_start`. The page is at `0x80300000` because `virtBase = physBase = 0x80000000` makes
`phystokv` the identity.

**And the page is read-only because `pmap_init_pte_static_page` itself wrote it that way one call
earlier.** The routine fills **all** 1024 entries of the page — `ARM_PGBYTES/sizeof(*pte_p)` — with
PTEs `pa_to_pte(pa) | ARM_PTE_TYPE | ARM_PTE_AF | ARM_PTE_SH | ARM_PTE_AP(AP_RONA) | ATTRINDX(...)`,
i.e. **read-only**, covering a full 4 MB window, and the caller then re-protects only the pages inside
`[start, _end)`:

```c
		for (i = 0; i < (ARM_PGBYTES / sizeof(*ppte)); i++) {
			if (start <= va && va < _end) {
				ptmp = ... | ARM_PTE_AP(pte_prot_APX);
				ppte[i] = ptmp;
			}
			va += ARM_PGBYTES;
			pa += ARM_PGBYTES;
		}
```

The difference between the two descriptors is visible in the two `bfi`s: the fill forces the low 12
bits of `pa` to `0x612`, while the helper's own base is `0x412` (or `0x413` when XN is asked for) —
`0x612 = 0x412 | 0x200`, the read-only bit. So everything in the 4 MB window but outside
`[start, _end)` is left read-only, and in this image that residue is large:

- `arm_vm_prot_init`'s first call, `RWX(gVirtBase, segSizeTEXT + (segTEXTB - gVirtBase), FALSE)`,
  rounds to `[0x80000000, 0x800906c0)`, so its window `[0x80000000, 0x80400000)` leaves
  `[0x80091000, 0x80400000)` read-only and creates the coarse table (`tte[0..3]` for L1 indices
  `0x800..0x803`) that holds those PTEs;
- the `__DATA` call extends the writable part only to `0x800da248`, so the residue is
  `[0x800db000, 0x80400000)` when the allocation happens.

## Which call, and why this image reaches an allocation at all

The eight calls between them are the whole of `arm_vm_prot_init`. Five of them are **no-ops in this
image for the same reason**: our synthetic Mach-O has two `LC_SEGMENT`s (`__TEXT`, `__DATA`), so
`getsegbynamefromheader` finds nothing for `__KLD`, `__LAST`, `__PRELINK_TEXT` — and
`getsegdatafromheader` says so explicitly rather than leaving the size alone:

```c
	sc = getsegbynamefromheader(mhp, segname);
	if (sc == (kernel_segment_command_t *)0) {
		*size = 0;
		return ((char *)0);
	}
	*size = sc->vmsize;
	result = (void *)sc->vmaddr;
```

so `segKLDB`, `segLASTB` and `segPRELINKTEXTB` are 0 *and* their sizes are 0, and three of the calls
are `RWNX(NULL, 0, ...)` — zero size, no loop, nothing. (Both are globals,
`arm_vm_init.c:109-110`, so a NULL return and a zero are the same value twice.)

**But one of them is not, and it is the one whose size is computed by subtraction:**

```c
	arm_vm_page_granular_RWNX(segPRELINKTEXTB + segSizePRELINKTEXT,
	                             end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT), force_coarse_physmap); // PreLinkInfoDictionary
```

With `segPRELINKTEXTB + segSizePRELINKTEXT == 0` this is `RWNX(0, end_kern, TRUE)` = `RWNX(0, 0x800db000, TRUE)`
— not a small range near `end_kern` at all, but the whole of `[0, end_kern)`:

```
  start = 0, _end = 0x800db000
  helper(0, _end, 0)                    va = 0        not ragged -> returns
  align_start = 0, align_end = 0x80000000
  while (align_start < align_end)       1024 iterations, ARM_TT_L1_PT_SIZE = 4 MB
      helper(align_start, align_end, align_start + 1, ...)   va = align_start+1 -> ragged
                                                             -> a page-table page per iteration
  helper(0, _end, _end)                 va = 0x800db000     -> already a table, no allocation
```

and `force_coarse_physmap` is TRUE in this build (`__ARM_PTE_PHYSMAP__` is 1, `proc_reg.h:88`), so the
loop body really does call the helper. **The loop asks for up to 1024 page-table pages from
`avail_start`** — and `avail_start` is inside the read-only residue, so it faults at the first
allocation that cannot be hidden.

## Why the fault is at `0x80300000` and not at `0x80223000`

The residue starts at `0x800db000`, `avail_start`'s first page is `0x80222000`, and the second
allocation is `0x80223000` — also read-only. So the arithmetic alone says the fault should be at
`0x80223000`, and it is not. The measurement says what is hiding the twenty-two intervening pages, and
it is **`pmap_bootstrap`'s own `memset`**:

```c
	memset((char *)phystokv(pmap_struct_start), 0, avail_start - pmap_struct_start);
```

That writes `[0x8020A000, 0x80222000)` while L1 index `0x802` is still the 1 MB **section** `start.s`
installed, so the MMU caches a writable section translation for that megabyte — and there is no
`flush_mmu_tlb()` between it and the fault. The last one runs just before `pmap_bootstrap`
(`arm_vm_init.c:491`, immediately after `set_mmu_ttb(cpu_ttep)`), so the TLB is empty when
`pmap_bootstrap` starts and nothing invalidates it afterwards. Changing an L1 entry without a
maintenance operation does not remove a cached translation, so **every write in `[0x80200000,
0x80300000)` still bypasses the walk** — and all 222 allocations before the fault are inside that
megabyte. The 223rd is `0x80300000`: the first page of the next megabyte, whose L1 entry the first
`RWX` call had already converted to a coarse table of read-only PTEs, and which nothing had ever
touched.

That makes the fault address a prediction rather than an observation:

```
  DFAR = round_up_1MB(first page avail_start hands out) = round_up_1MB(0x80222000) = 0x80300000  ✓
  222 allocations before it, against a loop with 1024 iterations to spare                          ✓
```

`SCTLR = 0x30c5787d` and `TTBCR = 0x00000002` are consistent and needed for nothing: `M`, `C` and `I`
are set, so the MMU is on with both caches, and the two `TTBR`s being equal means the walk reads one
table either way.

## The diagnosis

**`arm_vm_prot_init` faults because this image's Mach-O has no `__PRELINK_TEXT` segment, so the call
that was written to protect the small prelink-info range near `end_kern` becomes a 1024-iteration loop
over `[0, end_kern)` that hands out page-table pages one at a time from `avail_start` — and
`avail_start` is inside a region the same routine had already made read-only, because
`pmap_init_pte_static_page` fills a whole 4 MB window read-only and the caller only re-protects
`[start, _end)`.** It is not XNU's pmap that is wrong, and it is not an object that is missing: it is
our synthetic header, two segments short of the one XNU's boot code does arithmetic with.

The measurement and the source agree on every step: `TTBR0 = cpu_ttep` puts the fault inside
`arm_vm_init`'s own protection pass; `pc_abt` names `pmap_init_pte_static_page`'s first store; that
routine's only caller is the helper's allocate-a-page branch; `DFAR == ppte == avail_start` to the
byte; `avail_start` at entry follows from `pmap.c:2841-2850` plus `PT_INDEX_MAX == 1`; and the fault's
position inside the residue follows from `pmap_bootstrap`'s `memset` having primed exactly one
megabyte of stale, writable, section TLB.

## Cost

| | exp-241 | now |
| --- | --- | --- |
| entry text | 592785 B | **593841 B** (+1056) |
| `fleh_prefabt` | — | **204 B** (0x80002800) |
| `fleh_dataabt` | — | **368 B** (0x800028cc) |
| `arm_vm_prot_init` | 1280 B | **1280 B**, unchanged |
| `pmap_init_pte_static_page` | 0x60 | **0x60**, unchanged |
| entry image | 703352 B | **703352 B** |
| entry `.bss` | 0x800ab4f0–0x800da248 (191832 B) | **unchanged** |
| layout | args +901120, topOfKernelData +2097152, tree +4194304, window 8388608 | **unchanged** |
| headroom below `topOfKernelData` | 1203640 B | **1203640 B** |
| undefined | 645 | **645** |
| stubs | 559 functions, 86 storage | **unchanged** |
| `ENTRY_KV_BUF` | 2048 | **2048** |
| keys in the run | 2 (`kv_written` 0x4c) | **14** (`kv_written` 0x228) |

**No object was linked**, and the image is the same size to the byte: two handlers, their key names
and one extra globals block are all this experiment costs. The four globals are read, never written;
`entry_kv` and its 2048-byte buffer were already sized for this.

## What is next: give the Mach-O the segment the boot code does arithmetic with

The fix is one segment in `stages/stage90/xnu_arm_boot/entry_macho.s`. The call is
`RWNX(segPRELINKTEXTB + segSizePRELINKTEXT, end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT))`, and
what makes it behave is its `start` being at or past `end_kern` — in a real kernel `__PRELINK_TEXT`
ends where the prelink-info and linkedit regions begin, a short way below `end_kern`. This kernel has
no kexts, so the honest description is a `__PRELINK_TEXT` whose end **is** `end_kern`:

```
  __PRELINK_TEXT   vmaddr = __entry_image_end        vmsize = end_kern - __entry_image_end
```

which is `vmaddr 0x800da248, vmsize 0xdb8` for today's layout — `end_kern` being
`round_page(getlastaddr())` and `getlastaddr()` reading the image's own end, so both numbers are
derived and neither is hard-coded. Then `end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT) == 0`, the
call becomes a zero-size no-op (`helper(start, start, start)` returns on the ragged test and the loop
between the two endpoint calls is empty), and `RWNX(segPRELINKTEXTB, segSizePRELINKTEXT, TRUE)` two
lines above it, already a no-op, stays one.

`__PRELINK_INFO` is the one segment read by `arm_vm_init` that does *not* feed a call in
`arm_vm_prot_init` — it becomes `vm_prelink_sinfo`/`vm_prelink_einfo` for the kext loader later, so it
is worth adding for the same "the header should describe the kernel" reason but it is not part of this
fault. What the missing `__PRELINK_TEXT` costs besides the call is `vm_kext_top = segPRELINKTEXTB +
segSizePRELINKTEXT = 0` and `vm_prelink_stext = vm_prelink_etext = 0`, i.e. a kernel that reports it
has kext memory at address zero; both are consumed after the boot path this experiment is on.

Two things to be careful about, both measurable before the run:

- the header is a fixed structure — `ncmds`, `sizeofcmds` and the command walk in
  `getsegdatafromheader` all move, and `entry_macho.s`'s header comment carries that arithmetic;
- `vmsize` must not push the segment past `end_kern`, or the subtraction goes negative and the
  `unsigned long size` becomes enormous — the failure mode is worse than the one being fixed.

**And it should be enough.** With calls 3–7 all no-ops, and calls 8–11 making no allocation at all
(their VAs land in the coarse table the first call created, so `ppte` is that page and only the
`[start, _end)` stores happen), the allocations drop from 223 to four: the one the first `RWX` call
makes, and the three the last call makes — `RWNX(phystokv(topOfKernelData) + ARM_PGBYTES*10, static_memory_end -
..., TRUE)`, whose alignment loop allocates for `0x80600000` and `0x80700000` and whose endpoint call
allocates for `0x80800000`. By then its *first* call has already re-protected
`[0x8020A000, 0x80400000)` as `RWNX` (the window is the page table's, 4 MB from `0x80000000`), and
every one of those three allocations is inside that range — so no residue is live, no stale TLB is
needed, and `arm_vm_prot_init` should reach its return. The prediction for experiment 243 is
therefore: **the run gets past `arm_vm_prot_init`** — past the EVB special case at `:305-317` and into
`arm_vm_init`'s pre-initialization loop, which walks `virtual_space_start = 0xC0000000` and hands out
its own pages (1280 of them, for `off_end = 5 MB`). If it stops again it stops somewhere that is not
`pmap_init_pte_static_page`, and the same two registers that named this one will name that.

## Reproduce

```bash
# the change: two handlers report lr_abt, pc_abt, the word at pc_abt, and the MMU state
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'text size|image bytes|bss|layout|headroom' # -> 593841 B, 703352 B, layout unchanged

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20

# pc_abt, looked up
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf > /tmp/e242.dis
awk '/<pmap_init_pte_static_page>:/{f=1} f{print} f&&/^$/{exit}' /tmp/e242.dis
grep -n 'pc_abt + 8' stages/stage90/xnu_arm_boot/entry_stubs.c      # and lr_abt == pc_abt + 8
grep -n '80024f5c' /tmp/e242.dis                                    # 0xe4805004 str r5, [r0], #4

# its only caller, and that ppte is the pre-increment avail_start
grep -n 'bl\t80024f1c' /tmp/e242.dis
awk '/<arm_vm_page_granular_helper>:/{f=1} f{print} f&&/^$/{exit}' /tmp/e242.dis | sed -n '20,30p'

# the two descriptors: the fill is the helper's own base plus the read-only bit
python3 -c 'print(hex(0x612), hex(0x412), hex(0x612 - 0x412))'      # 0x612 0x412 0x200
sed -n '/^pmap_init_pte_static_page/,/^}/p' external/xnu-4570.1.46/osfmk/arm/pmap.c

# where the first page avail_start hands out comes from
sed -n '2829,2852p' external/xnu-4570.1.46/osfmk/arm/pmap.c          # the table sizes
sed -n '281,296p' external/xnu-4570.1.46/osfmk/arm/pmap.c            # PT_INDEX_MAX == 1 for VMSA 7
python3 -c 'print(hex((0x8020D800 + 20*4096 + 0xfff) & ~0xfff))'     # 0x80222000
python3 -c 'print((0x80300000 - 0x80222000)//0x1000, "pages")'       # 222

# the call that degenerates, and why it is 0 and not NULL-but-sized
sed -n '283,286p' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
sed -n '96,117p' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c     # the seg globals
grep -n 'getsegdatafromheader' -A 16 external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c | head -20

# the residue, and the section TLB that hides most of it
sed -n '262,278p' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
grep -n 'flush_mmu_tlb\|set_mmu_ttb' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
grep -n 'memset((char \*)phystokv(pmap_struct_start)' external/xnu-4570.1.46/osfmk/arm/pmap.c
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
