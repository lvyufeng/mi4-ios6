/*
 * A linker script for a *measurement* link of the XNU ARM objects.
 *
 * Not a shipping layout. `stages/stage90/xnu_link.ld` places a small payload at PA 0x8000 for the
 * Stage90 handoff; this one places the kernel where XNU's own `start.s` says the kernel lives, to
 * answer a different question: **do these 599 objects fit together, and what does the linker say
 * besides "undefined symbol"**.
 *
 * The address contract is read off `osfmk/arm/start.s` and `docs/reference/xnu-handoff-contract.md`:
 *
 *   - `virtBase` is the kernel's **link-time virtual base**, and it is what `LOAD_ADDR` subtracts
 *     to turn a virtual address into a physical one (`start.s:108`). The project's `VA = 0x80000000
 *     + PA` correspondence puts it at 0x80000000.
 *   - `physBase` is 0 (CONFIG_PHYS_OFFSET in the cancro kernel), so the kernel's bytes load at
 *     physical 0 and are reached at virtual 0x80000000.
 *   - `start.s:195-207` maps the kernel with **1 MB sections** from physBase upward, so the
 *     sections have to be 1 MB aligned for the section descriptors to be meaningful.
 *
 * The entry is `_start` — XNU's real entry point, which `osfmk/arm/start.s` defines — rather than
 * the Stage90 payload's. If the link cannot find it, that is a finding about the object set.
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

SECTIONS
{
    . = 0x80000000;

    .text : ALIGN(0x1000) {
        *(.text*)
        *(.rodata*)
        *(.constdata*)
        *(.const*)
    }

    . = ALIGN(0x1000);
    .data : ALIGN(0x1000) {
        *(.data*)
        *(.data1*)
        *(.got*)
        *(.sdata*)
        *(__DATA*)
        *(__const*)
        *(__literal*)
    }

    . = ALIGN(0x1000);
    __bss_start = .;
    .bss : ALIGN(0x1000) {
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        *(__COMMON*)
    }
    __bss_end = .;

    /DISCARD/ : {
        *(.comment*)
        *(.note*)
        *(.ARM.attributes*)
        *(.llvm_addrsig*)
    }
}
