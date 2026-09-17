#ifndef MI4IOS6_STAGE90_SHIM_PEXPERT_BOOT_H
#define MI4IOS6_STAGE90_SHIM_PEXPERT_BOOT_H

#include <stdint.h>

/*
 * Stage84 compile-only public ARM pexpert boot_args shim.
 *
 * The selected Darwin 12 / xnu-2050 public tree lacks a complete ARMv7
 * pexpert/arm boot header. This mirrors the public xnu-4570 ARM pexpert shape as an ABI
 * reference. Until 2026-09-17 no public-XNU object was executed by the payload; that is no
 * longer true (STAGE90_XNU_REAL_DT, experiment-100), which is why this header now defers the
 * boot_args definition to stage90.h when that is already included - see below.
 */
#ifdef STAGE90_BOOT_ARGS_DEFINED
/* stage90.h defines this one too, as 256u. Same value, and a redefinition warning is a poor
 * way to learn that. */
#else
#define BOOT_LINE_LENGTH        256
#endif
#define kBootArgsRevision       1
#define kBootArgsRevision2      2
#define kBootArgsVersion1       1
#define kBootArgsVersion2       2

typedef struct Boot_Video {
    unsigned long v_baseAddr;
    unsigned long v_display;
    unsigned long v_rowBytes;
    unsigned long v_width;
    unsigned long v_height;
    unsigned long v_depth;
} Boot_Video;

#ifdef STAGE90_BOOT_ARGS_DEFINED
/*
 * stage90.h owns the definition (and defines this macro); supply only the typedef name the
 * public XNU sources use, referencing the same tag. Keeping a second copy here would make any
 * translation unit that includes both a compile error, and would let the two drift silently if
 * they did not.
 */
typedef struct boot_args boot_args;
#else
typedef struct boot_args {
    uint16_t Revision;
    uint16_t Version;
    uint32_t virtBase;
    uint32_t physBase;
    uint32_t memSize;
    uint32_t topOfKernelData;
    Boot_Video Video;
    uint32_t machineType;
    void *deviceTreeP;
    uint32_t deviceTreeLength;
    char CommandLine[BOOT_LINE_LENGTH];
    uint32_t bootFlags;
    uint32_t memSizeActual;
} boot_args;
#endif /* STAGE90_BOOT_ARGS_DEFINED */

#endif
