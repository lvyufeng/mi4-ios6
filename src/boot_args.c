#include "stage90.h"

void build_boot_args(struct boot_args *args, void *dt, uint32_t dt_len)
{
    memset(args, 0, sizeof(*args));

    args->Revision = BOOT_ARGS_REVISION;
    args->Version = BOOT_ARGS_VERSION;
    args->virtBase = 0;                  /* Stage84 boot args are still identity-based before MMU handoff. */
    args->physBase = STAGE90_BASE;
    args->memSize = RAM_CONSOLE_BASE - RAM_PHYS_BASE;
    args->topOfKernelData = (uint32_t)(uintptr_t)__stage90_image_end;

    args->machineType = MACHINE_TYPE_MSM8974;
    args->deviceTreeP = dt;
    args->deviceTreeLength = dt_len;

    /*
     * 459: `rd=md0` is the whole root device, and this string is the copy XNU actually parses.
     * `PE_parse_boot_argn` reads `PE_boot_args()`, which is
     * `((boot_args *)PE_state.bootArgs)->CommandLine` (`pexpert/arm/pe_bootargs.c:11`) - the field
     * `xnu_entry_build_args` fills from this one (`xnu_entry_jump.c:170`). The device tree's
     * `/chosen` `boot-args` property is *not* read by that parser, which is why the `serial=0x1` in
     * it was never able to do anything, and it is written to agree with this string anyway
     * (`stage90_main.c`) so that the two copies cannot be read as alternatives.
     *
     * `md0` names the memory device `IOFindBSDRoot` builds from `/chosen/memory-map`'s `RAMDisk`
     * property (`iokit/bsddev/IOKitBSDInit.cpp:436-490`), whose two words come from the entry
     * image's own `g_stage90_ramdisk` through the generated `xnu_arm_entry.h`. Without it the
     * boot waits 60 seconds for an `IOMedia` this machine cannot produce; with it, the root device is
     * this image's memory and `mockfs` turns that into a filesystem.
     *
     * **515 adds the one token XNU's *own* parser has to see, and pays for it with three the payload
     * cannot read.** `up_style_idle_exit=1` is the boot argument `arm_init` tests
     * (`osfmk/arm/arm_init.c:287-289`) to select `caches.c:414`'s uniprocessor idle-cache branch - the
     * branch 514 found this port taking the *other* of, four instructions before a store to `0x130`.
     * It cannot be abbreviated and it cannot be a bare word: `PE_parse_boot_argn_internal` matches a
     * bare word only when it begins with `-`, and every other token in this string is one
     * (`pexpert/gen/bootargs.c:98-115`), which is why the port reads those with its own matcher
     * (`xnu_pe_init_platform_false.c`). **The field is 256 bytes and this string had 255 of them**,
     * so the three tokens whose readers exist only in stage 85-88's sources and never in this
     * payload's code - `live-pmap`, `tlb-live`, `pmap-restore` - are removed rather than truncating
     * the flag off the end. The `_Static_assert` below is the part that matters: `cmd_len` silently
     * takes the smaller of the two sizes, so before 515 an over-long string lost its *tail* with no
     * error anywhere, and the boot-arg the step depends on is exactly the kind of thing that would
     * have gone missing that way.
     */
    const char cmd[] = "debug=0x144 rd=md0 mi4ios6.stage=83 xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm prevm-pexpert dtinit-facts peid-machine no-pub-peinit no-pub-dtinit no-pub-peid no-pub-thread no-pub-cpuboot no-pub-rtclock up_style_idle_exit=1";
    _Static_assert(sizeof(cmd) <= BOOT_LINE_LENGTH,
                   "this string no longer fits the boot_args CommandLine, and the copy below would "
                   "drop the tail of it silently - the last token is up_style_idle_exit=1, which is "
                   "the boot argument 515's repair is carried by");
    const uint32_t cmd_len = (sizeof(cmd) < sizeof(args->CommandLine)) ?
        (uint32_t)sizeof(cmd) : (uint32_t)sizeof(args->CommandLine);
    memcpy(args->CommandLine, cmd, cmd_len);
    args->CommandLine[BOOT_LINE_LENGTH - 1u] = '\0';

    args->bootFlags = 0;
    args->memSizeActual = RAM_TOP - RAM_PHYS_BASE;
}
