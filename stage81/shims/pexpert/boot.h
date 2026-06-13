#ifndef MI4IOS6_STAGE81_SHIM_PEXPERT_BOOT_H
#define MI4IOS6_STAGE81_SHIM_PEXPERT_BOOT_H

#include <stdint.h>

/*
 * Stage81 compile-only public ARM pexpert boot_args shim.
 *
 * The selected Darwin 12 / xnu-2050 public tree lacks a complete ARMv7
 * pexpert/arm boot header. Stage81 uses the public xnu-4570 ARM pexpert
 * shape as an ABI reference for host-only compile/link proof, but the bootable
 * Stage81 payload continues to use its own stage81.h boot_args structure and
 * never executes public-XNU code on hardware.
 */
#define BOOT_LINE_LENGTH        256
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

#endif
