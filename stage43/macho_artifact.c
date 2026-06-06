#include "stage43.h"

#define U32(v) \
    (uint8_t)((uint32_t)(v) & 0xffu), \
    (uint8_t)(((uint32_t)(v) >> 8) & 0xffu), \
    (uint8_t)(((uint32_t)(v) >> 16) & 0xffu), \
    (uint8_t)(((uint32_t)(v) >> 24) & 0xffu)

#define SEGNAME(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p) \
    (uint8_t)(a), (uint8_t)(b), (uint8_t)(c), (uint8_t)(d), \
    (uint8_t)(e), (uint8_t)(f), (uint8_t)(g), (uint8_t)(h), \
    (uint8_t)(i), (uint8_t)(j), (uint8_t)(k), (uint8_t)(l), \
    (uint8_t)(m), (uint8_t)(n), (uint8_t)(o), (uint8_t)(p)

#define SEG_CMD(name_bytes, vmaddr, vmsize, fileoff, filesize, maxprot, initprot) \
    U32(STAGE43_MACHO_LC_SEGMENT), U32(56u), \
    name_bytes, \
    U32(vmaddr), U32(vmsize), U32(fileoff), U32(filesize), \
    U32(maxprot), U32(initprot), U32(0u), U32(0u)

const uint8_t stage43_embedded_macho[] __attribute__((aligned(4))) = {
    /* mach_header: 28 bytes, sizeofcmds = 3*56 + symtab(24) + unixthread-prefix(16) = 208. */
    U32(STAGE43_MACHO_MAGIC),
    U32(STAGE43_MACHO_CPU_TYPE_ARM),
    U32(STAGE43_MACHO_CPU_SUBTYPE_ARM_V7),
    U32(STAGE43_MACHO_FILETYPE_PRELOAD),
    U32(5u),
    U32(208u),
    U32(0u),

    SEG_CMD(SEGNAME('_','_','T','E','X','T',0,0,0,0,0,0,0,0,0,0),
            0x80008000u, 0x00001000u, 0x000000f0u, 0x00000010u, 0x00000005u, 0x00000005u),
    SEG_CMD(SEGNAME('_','_','D','A','T','A',0,0,0,0,0,0,0,0,0,0),
            0x80009000u, 0x00001000u, 0x00000100u, 0x00000010u, 0x00000003u, 0x00000003u),
    SEG_CMD(SEGNAME('_','_','L','I','N','K','E','D','I','T',0,0,0,0,0,0),
            0x8000a000u, 0x00001000u, 0x00000110u, 0x00000010u, 0x00000001u, 0x00000001u),

    /* LC_SYMTAB with zero symbols, bounded string table placeholder. */
    U32(STAGE43_MACHO_LC_SYMTAB), U32(24u),
    U32(0u), U32(0u), U32(0x00000110u), U32(0u),

    /* Minimal LC_UNIXTHREAD-like prefix: flavor, count, entry-looking PC value. */
    U32(STAGE43_MACHO_LC_UNIXTHREAD), U32(16u),
    U32(0u), U32(0x80008000u),

    /* Inert segment payload bytes. */
    0x53u, 0x54u, 0x34u, 0x33u, 0x2du, 0x54u, 0x45u, 0x58u,
    0x54u, 0x2du, 0x4eu, 0x4fu, 0x45u, 0x58u, 0x45u, 0x43u,
    0x53u, 0x54u, 0x34u, 0x33u, 0x2du, 0x44u, 0x41u, 0x54u,
    0x41u, 0x2du, 0x4eu, 0x4fu, 0x45u, 0x58u, 0x45u, 0x43u,
    0x53u, 0x54u, 0x34u, 0x33u, 0x2du, 0x4cu, 0x49u, 0x4eu,
    0x4bu, 0x45u, 0x44u, 0x49u, 0x54u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u,
};

const uint32_t stage43_embedded_macho_size = sizeof(stage43_embedded_macho);
