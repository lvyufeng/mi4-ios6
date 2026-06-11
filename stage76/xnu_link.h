#ifndef MI4IOS6_STAGE76_XNU_LINK_H
#define MI4IOS6_STAGE76_XNU_LINK_H

#include <stdint.h>

#define STAGE76_XNU_LINK_VERSION                  2u

#define STAGE76_XNU_LINK_OBJECTS_PRESENT          0x00000001u
#define STAGE76_XNU_LINK_SUPPORT_PRESENT          0x00000002u
#define STAGE76_XNU_LINK_TOOLCHAIN_PRESENT        0x00000004u
#define STAGE76_XNU_LINK_ELF_LINKED               0x00000008u
#define STAGE76_XNU_LINK_UNDEFINEDS_CLOSED        0x00000010u
#define STAGE76_XNU_LINK_LAYOUT_RECORDED          0x00000020u
#define STAGE76_XNU_LINK_SYMBOLS_RECORDED         0x00000040u
#define STAGE76_XNU_LINK_HASH_RECORDED            0x00000080u
#define STAGE76_XNU_LINK_PUBLIC_2050_BASELINE     0x00000100u
#define STAGE76_XNU_LINK_PUBLIC_ONLY              0x00000200u
#define STAGE76_XNU_LINK_NO_FULL_XNU_BUILD        0x00000400u
#define STAGE76_XNU_LINK_NO_PUBLIC_XNU_EXEC       0x00000800u
#define STAGE76_XNU_LINK_NO_MACHO_EXEC            0x00001000u
#define STAGE76_XNU_LINK_NO_EXTERNAL_MUTATION     0x00002000u
#define STAGE76_XNU_LINK_OUTPUTS_IGNORED          0x00004000u
#define STAGE76_XNU_LINK_FAIL_CLOSED              0x00008000u
#define STAGE76_XNU_LINK_NO_PLATFORM_RUNTIME_EXEC 0x00010000u
#define STAGE76_XNU_LINK_REQUIRED_MASK            0x0001ffffu

#define STAGE76_XNU_LINK_FAIL_OBJECTS             0x00000001u
#define STAGE76_XNU_LINK_FAIL_SUPPORT             0x00000002u
#define STAGE76_XNU_LINK_FAIL_TOOLCHAIN           0x00000004u
#define STAGE76_XNU_LINK_FAIL_LINK                0x00000008u
#define STAGE76_XNU_LINK_FAIL_UNDEFINEDS          0x00000010u
#define STAGE76_XNU_LINK_FAIL_LAYOUT              0x00000020u
#define STAGE76_XNU_LINK_FAIL_PUBLIC_BASELINE     0x00000040u
#define STAGE76_XNU_LINK_FAIL_SAFETY_BOUNDARY     0x80000000u

struct stage76_xnu_link {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t linked_object_count;
    uint32_t support_object_count;
    uint32_t undefined_symbol_count;
    uint32_t global_symbol_count;
    uint32_t elf_bytes;
    uint32_t elf_sha32;
    uint32_t text_addr;
    uint32_t text_size;
    uint32_t data_addr;
    uint32_t data_size;
    uint32_t bss_addr;
    uint32_t bss_size;
    uint32_t baseline_commit32;
    uint32_t baseline_master_version;
    uint32_t public_only;
    uint32_t no_full_xnu_build;
    uint32_t no_public_xnu_exec;
    uint32_t no_macho_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_external_mutation;
    uint32_t outputs_ignored;
    uint32_t fail_closed;
    uint32_t checksum;
};

int stage76_xnu_link_selftest(void);
const struct stage76_xnu_link *stage76_xnu_link_result(void);

#endif
