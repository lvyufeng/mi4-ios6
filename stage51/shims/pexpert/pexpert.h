#ifndef MI4IOS6_STAGE51_SHIM_PEXPERT_PEXPERT_H
#define MI4IOS6_STAGE51_SHIM_PEXPERT_PEXPERT_H

#include <stdint.h>
#include <stddef.h>
#include <mach/boolean.h>
#include <pexpert/device_tree.h>

int strncmp(const char *a, const char *b, size_t n);
size_t strlen(const char *s);
void *memcpy(void *dst, const void *src, size_t n);

char *PE_boot_args(void);
int IODTGetDefault(const char *key, void *infoAddr, unsigned int infoSize);
boolean_t PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len);
boolean_t PE_get_default(const char *property_name, void *property_ptr, unsigned int max_property);
boolean_t PE_imgsrc_mount_supported(void);

#endif
