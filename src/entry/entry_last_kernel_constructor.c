/*
 * Apple's `libsa/lastkernelconstructor.c`, for this image.
 *
 * The file is two statements, and this is the first of them:
 *
 *     static void last_kernel_constructor(void) __attribute__ ((constructor));
 *     static void last_kernel_constructor(void) { iokit_post_constructor_init(); }
 *
 * `iokit_post_constructor_init` is the only caller of `OSKext::initialize()` in the whole kernel
 * (`iokit/Kernel/IOStartIOKit.cpp:89`), and this constructor is the only caller of *it*
 * (`libsa/lastkernelconstructor.c:35`). The object is named for its position rather than for its
 * body: it is in `libsa`, which Apple's ARM manifest does not list, so it is the last of the C++
 * static constructors only because the link puts it last - and `.init_array` is walked in link
 * order. Linking it here is what makes `OSKext::initialize()` run at all, which is the whole of
 * experiment 332: 331's run ended on `sKextLock` (0x80192D90) being NULL inside
 * `OSKext::lookupKextWithIdentifier`, and `sKextLock = IORecursiveLockAlloc()` is that function's
 * first statement.
 *
 * **The second statement of Apple's file is not here, and the two reasons are both measured.**
 * It is
 *
 *     __asm__(".globl _last_kernel_symbol");
 *     __asm__(".zerofill __LAST, __last, _last_kernel_symbol, 0");
 *
 * and the build refuses the file as it stands: `clang --target=armv7...` gives
 * `.zerofill` -> "unknown directive", because the four-operand form is the Darwin assembler's
 * (segment, section, symbol, size) and this image is linked as ELF. The second reason is the name:
 * `_last_kernel_symbol` is Mach-O's spelling of the C identifier and the `.zerofill` is what the
 * underscore is for, while `osfmk/arm/arm_vm_init.c:57` declares `extern void *last_kernel_symbol`
 * and *that* is the spelling the reference carries in an ELF object - so a definition under the
 * Darwin name would satisfy nothing and leave the reference a stub.
 *
 * No fallback is needed for either. The symbol's meaning is "the end of xnu" (`arm_vm_init.c:499`
 * reads it as `vm_kernel_top`), and in this image that address is the linker's own end of the last
 * output section, which `entry.ld` already computes as `__bss_end`. So the definition belongs in
 * the script, next to `ExceptionLowVectorsBase` and `ResetHandlerData`, which are pinned there for
 * the same reason: a linker-script symbol is defined in *pass 1*, and pass 1's undefined set is
 * what the stub generator runs from - so the stub this image has been carrying for
 * `last_kernel_symbol` (a 24-byte function body and a name slot, since `nm -S` finds no type for
 * it) is retired by the script rather than by a stand-in. See `entry.ld`.
 *
 * Compiled by `build_entry.sh` with the same flags as `entry_stubs.c`, which is what the other C
 * translation units in this directory are compiled with.
 */

extern void iokit_post_constructor_init(void);

static void last_kernel_constructor(void) __attribute__ ((constructor));

static void last_kernel_constructor(void)
{
    iokit_post_constructor_init();
}
