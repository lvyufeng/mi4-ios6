#ifndef MI4IOS6_STAGE71_SHIM_PEXPERT_PEXPERT_H
#define MI4IOS6_STAGE71_SHIM_PEXPERT_PEXPERT_H

#include <stdint.h>
#include <stddef.h>
#include <mach/boolean.h>
#include <pexpert/boot.h>
#include <pexpert/device_tree.h>

int strncmp(const char *a, const char *b, size_t n);
size_t strlen(const char *s);
void *memcpy(void *dst, const void *src, size_t n);

/*
 * Minimal public ARM pexpert ABI surface for host-only object/link proof.
 * This mirrors only the fields required by public pe_bootargs.c; it is not a
 * runtime pexpert implementation and is never called by the booted Stage71
 * payload on hardware.
 */
typedef struct PE_Video {
    unsigned long v_baseAddr;
    unsigned long v_rowBytes;
    unsigned long v_width;
    unsigned long v_height;
    unsigned long v_depth;
    unsigned long v_display;
    char v_pixelFormat[64];
    unsigned long v_offset;
    unsigned long v_length;
    unsigned char v_rotate;
    unsigned char v_scale;
    char reserved1[2];
    long v_baseAddrHigh;
} PE_Video;

typedef struct PE_state {
    boolean_t initialized;
    PE_Video video;
    void *deviceTreeHead;
    void *bootArgs;
} PE_state_t;

extern PE_state_t PE_state;

char *PE_boot_args(void);
int IODTGetDefault(const char *key, void *infoAddr, unsigned int infoSize);
boolean_t PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len);
boolean_t PE_get_default(const char *property_name, void *property_ptr, unsigned int max_property);
boolean_t PE_imgsrc_mount_supported(void);

#endif
