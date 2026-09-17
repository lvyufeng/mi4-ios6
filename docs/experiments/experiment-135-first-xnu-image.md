# Experiment 135 — an XNU image, linked at XNU's own addresses, with a real `_start`

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/measure_link.sh` (new), `tools/xnu_measure_link.ld` (new)

| Configuration | text | data | bss | total | entry |
| --- | --- | --- | --- | --- | --- |
| `RELEASE` | 4 179 211 B | 99 032 B | 299 644 B | **4.58 MB** | `_start` @ `0x803a7074` |
| `STAGE90_BOOT` | 2 096 167 B | 52 984 B | 222 776 B | **2.37 MB** | `_start` @ `0x801cd074` |

**There is now an XNU-derived ELF.** It is not bootable — see the last section — but it is the first
time the project has had a linked kernel image at all, and three of its properties are new
information rather than a restatement of what was already known.

## What was done

`link_xnu_arm.sh` stops at `ld -r`, which resolves nothing and correctly refuses to produce an image.
This goes one step further: it links a **final** ELF against a linker script that places the kernel
where `start.s` says the kernel lives —

```
. = 0x80000000;       /* virtBase: the link-time virtual base LOAD_ADDR subtracts (start.s:108) */
                      /* physBase = 0, so the bytes load at physical 0                      */
.text   ALIGN(0x1000) /* start.s:195-207 maps with 1 MB sections, so alignment matters        */
```

— and stubs every symbol the build cannot provide as a weak function, so the linker can do the part
of its job that has nothing to do with missing code.

The stub count is **not** a result: it is set by construction to the number of undefined symbols
(443 for `RELEASE`, 477 for the minimal configuration). What is a result is everything else.

## Three things this measured

**1. The linker has nothing else to say.** Zero diagnostics other than undefined references — no
branch out of range, no relocation against an unrepresentable symbol, no section that will not
place. And:

```
$ arm-none-eabi-readelf -r out/link/RELEASE-measure.elf
There are no relocations in this file.
```

Every relocation resolved. That is a statement about the compiled code's *shape*, and `ld -r`
cannot make it, because `-r` does not lay anything out.

**2. XNU's real entry sequence is in the image, and it reads `boot_args` where the contract says.**
`_start` at `0x803a7074`:

```
803a7074:  mov  r1, #0                        cpu_data = 0
803a707c:  cpsid if                          disable IRQ and FIQ
803a7080:  mcr  p15, 0, fp, c7, c5, {0}      ICIALLU  -- invalidate the I-cache
803a7088:  mrc  p15, 0, fp, c1, c0, {0}
803a708c:  orr  fp, fp, #6144                SCTLR.I | SCTLR.A
803a7090:  mcr  p15, 0, fp, c1, c0, {0}      ... and enable them
803a7098:  ldr  r8, [r0, #8]                 virtBase   <- boot_args + 8
803a70a0:  ldr  r9, [r0, #4]                 physBase   <- boot_args + 4
803a70a4:  ldr  sl, [r0, #12]                memSize    <- boot_args + 12
803a70a8:  ldr  r4, [pc, #796]               -> fleh_reset
803a70ac:  ldr  r5, [pc, #796]               -> ExceptionVectorsTable
```

Those three offsets are the ones `docs/reference/xnu-handoff-contract.md` derives from
`start.s:104-106` and `tools/check_xnu_struct_abi.py` guards. Seeing the actual loads at `+4`/`+8`/`+12`
in a linked image is the same claim from the one direction that had not been checked.

`arm_init` (`0x802a1620`), `arm_vm_init` (`0x802a3580`), `machine_startup`, `kernel_bootstrap` and
`rtclock_init` are all present — **real XNU functions, not stubs**.

**3. It fits.** `RELEASE` spans `0x80000000`–`0x8046227c` = 4.38 MB against the 93 MB `memSize` the
conforming `boot_args` declares, and `.data` begins exactly 1 MB-aligned at `0x80400000`, which is
what `start.s:195-207`'s section descriptors require. Had the kernel been larger than `memSize`, or
had a section straddled a 1 MB boundary in the wrong way, that would have shown up here and not in a
hardware run.

## What it is not

**It is not bootable.** 443 of its symbols are stubs that `mov r0, #0; bx lr`. If this image were
loaded, XNU's entry sequence would run correctly — it is real code — and then `arm_init` would call
stubs that silently return 0.

~~Most consequentially, `PE_putc` and `kprintf` are among the undefined symbols, so a boot attempt
would produce no output at all: a silent hang with no log.~~

**CORRECTED 2026-09-17 — that sentence was wrong, and it was checked against the list it was
describing.** `PE_putc`, `kprintf`, `vprintf`, `cnputc` and `PE_init_printf` are all **defined** in
the compiled set:

```
$ arm-none-eabi-nm --defined-only out/xnu_kernel_obj/*.o | grep -wE 'PE_putc|kprintf|cnputc'
00000008 B PE_putc          <- a BSS variable, not a function: `void (*PE_putc)(char c);`
000000c4 T kprintf
000006dc T cnputc
```

`PE_putc` is a *function pointer* (`pexpert/gen/pe_gen.c:107`), which `PE_init_printf` (`:112`)
assigns to `cnputc`; it is zero-initialised BSS, so it is a defined symbol either way. See
[`experiment-136`](experiment-136-assym-and-a-correction.md) for what that changes — the console
path is present as code, and the reason a boot attempt might still be silent is a different and
narrower one than "the symbols are missing".

The distance between this and a boot attempt is therefore not "the linker needs to succeed" — it
does — but **which of the 443 stubs have to become real before a boot tells you anything**.
`boot_closure.py` already answers that question structurally (460 symbols on the boot path, 9 of
them compiler runtime); the console path is the one to make real first, because without it every
subsequent step is unobservable.

## How to reproduce

```bash
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh          # 599 of 615
./tools/measure_link.sh                    # RELEASE: 4.58 MB, entry _start @ 0x803a7074
./tools/measure_link.sh --min              # STAGE90_BOOT: 2.37 MB
```

The assembled ARM layer in `out/xnu_asm_obj/` is optional input and is used when present — `start.s`
is the entry the kernel actually has, and linking without it is why the first run of this tool
reported `cannot find entry symbol _start`.
