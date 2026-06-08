#ifndef MI4IOS6_STAGE62_H
#define MI4IOS6_STAGE62_H

#include <stdint.h>
#include <stddef.h>

#include "xnu_workspace.h"
#include "xnu_object_subset.h"
#include "xnu_compile_graph.h"
#include "xnu_link.h"

#define STAGE62_BASE          0x00008000u
#define RAM_CONSOLE_BASE     0xde500000u
#define RAM_CONSOLE_SIZE     0x00200000u
#define RAM_CONSOLE_SIG      0x43474244u /* 'DBGC' */
#define MSM8974_PSHOLD       0xfc4ab000u
#define MSM_IMEM_BASE_PHYS   0x0fa00000u
#define RESTART_REASON       (MSM_IMEM_BASE_PHYS + 0x65cu)
#define RESTART_NORMAL       0x77665501u

#define RAM_PHYS_BASE        0x80000000u
#define RAM_TOP              0xde700000u
#define RAM_CONSOLE_RESERVED 0x00200000u

#define BOOT_LINE_LENGTH     256u
#define BOOT_ARGS_REVISION   2u
#define BOOT_ARGS_VERSION    2u
#define MACHINE_TYPE_MSM8974 0x8974u

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct boot_video {
    uint32_t v_baseAddr;
    uint32_t v_display;
    uint32_t v_rowBytes;
    uint32_t v_width;
    uint32_t v_height;
    uint32_t v_depth;
};

struct boot_args {
    uint16_t Revision;
    uint16_t Version;
    uint32_t virtBase;
    uint32_t physBase;
    uint32_t memSize;
    uint32_t topOfKernelData;
    struct boot_video Video;
    uint32_t machineType;
    void *deviceTreeP;
    uint32_t deviceTreeLength;
    char CommandLine[BOOT_LINE_LENGTH];
    uint32_t bootFlags;
    uint32_t memSizeActual;
};

#define STAGE62_STATUS_OK                         0x62000001u
#define STAGE62_STATUS_BASE                       0x62000000u

#define STAGE62_MACHO_PROBE_VERSION               1u
#define STAGE62_MACHO_MAGIC                       0xfeedfaceu
#define STAGE62_MACHO_CIGAM                       0xcefaedfeu
#define STAGE62_MACHO_CPU_TYPE_ARM                12u
#define STAGE62_MACHO_CPU_SUBTYPE_ARM_ALL         0u
#define STAGE62_MACHO_CPU_SUBTYPE_ARM_V7          9u
#define STAGE62_MACHO_FILETYPE_EXECUTE            2u
#define STAGE62_MACHO_FILETYPE_PRELOAD            5u
#define STAGE62_MACHO_LC_SEGMENT                  1u
#define STAGE62_MACHO_LC_SYMTAB                   2u
#define STAGE62_MACHO_LC_UNIXTHREAD               5u
#define STAGE62_MACHO_LC_MAIN                     0x80000028u

#define STAGE62_MACHO_CMD_SEGMENT                 0x00000001u
#define STAGE62_MACHO_CMD_SYMTAB                  0x00000002u
#define STAGE62_MACHO_CMD_UNIXTHREAD              0x00000004u
#define STAGE62_MACHO_CMD_MAIN                    0x00000008u
#define STAGE62_MACHO_CMD_UNKNOWN                 0x80000000u

#define STAGE62_MACHO_SEG_TEXT                    0x00000001u
#define STAGE62_MACHO_SEG_DATA                    0x00000002u
#define STAGE62_MACHO_SEG_LINKEDIT                0x00000004u
#define STAGE62_MACHO_SEG_KLD                     0x00000008u
#define STAGE62_MACHO_SEG_LAST                    0x00000010u
#define STAGE62_MACHO_SEG_PRELINK_TEXT            0x00000020u
#define STAGE62_MACHO_SEG_PRELINK_INFO            0x00000040u
#define STAGE62_MACHO_SEG_PRELINK_STATE           0x00000080u
#define STAGE62_MACHO_SEG_PRELINK                 0x00000100u
#define STAGE62_MACHO_SEG_PRELINK_MASK            (STAGE62_MACHO_SEG_PRELINK_TEXT | \
                                                   STAGE62_MACHO_SEG_PRELINK_INFO | \
                                                   STAGE62_MACHO_SEG_PRELINK_STATE | \
                                                   STAGE62_MACHO_SEG_PRELINK)
#define STAGE62_MACHO_SEG_REQUIRED                (STAGE62_MACHO_SEG_TEXT | \
                                                   STAGE62_MACHO_SEG_DATA | \
                                                   STAGE62_MACHO_SEG_LINKEDIT)

#define STAGE62_MACHO_ENTRY_UNIXTHREAD            0x00000001u
#define STAGE62_MACHO_ENTRY_MAIN                  0x00000002u
#define STAGE62_MACHO_ENTRY_NOT_EXECUTED          0x80000000u

#define STAGE62_MACHO_SECT_TEXT_TEXT              0x00000001u
#define STAGE62_MACHO_SECT_DATA_CONST             0x00000002u
#define STAGE62_MACHO_SECT_PRELINK_TEXT_TEXT      0x00000004u
#define STAGE62_MACHO_SECT_PRELINK_INFO_INFO      0x00000008u
#define STAGE62_MACHO_SECT_PRELINK_INFO_KERNEL    0x00000010u
#define STAGE62_MACHO_SECT_PRELINK_INFO_KEXTS     0x00000020u
#define STAGE62_MACHO_SECT_PRELINK_STATE_KERNEL   0x00000040u
#define STAGE62_MACHO_SECT_PRELINK_STATE_KEXTS    0x00000080u
#define STAGE62_MACHO_SECT_PRELINK_SYMTAB         0x00000100u
#define STAGE62_MACHO_SECT_PRELINK_MASK           (STAGE62_MACHO_SECT_PRELINK_TEXT_TEXT | \
                                                   STAGE62_MACHO_SECT_PRELINK_INFO_INFO | \
                                                   STAGE62_MACHO_SECT_PRELINK_INFO_KERNEL | \
                                                   STAGE62_MACHO_SECT_PRELINK_INFO_KEXTS | \
                                                   STAGE62_MACHO_SECT_PRELINK_STATE_KERNEL | \
                                                   STAGE62_MACHO_SECT_PRELINK_STATE_KEXTS | \
                                                   STAGE62_MACHO_SECT_PRELINK_SYMTAB)
#define STAGE62_MACHO_SECT_REQUIRED               (STAGE62_MACHO_SECT_TEXT_TEXT | \
                                                   STAGE62_MACHO_SECT_DATA_CONST)

#define STAGE62_MACHO_VALID_MAGIC                 0x00000001u
#define STAGE62_MACHO_VALID_ENDIAN                0x00000002u
#define STAGE62_MACHO_VALID_CPU                   0x00000004u
#define STAGE62_MACHO_VALID_SUBTYPE               0x00000008u
#define STAGE62_MACHO_VALID_FILETYPE              0x00000010u
#define STAGE62_MACHO_VALID_COMMAND_BOUNDS        0x00000020u
#define STAGE62_MACHO_VALID_SEGMENT_COMMAND       0x00000040u
#define STAGE62_MACHO_VALID_REQUIRED_SEGMENTS     0x00000080u
#define STAGE62_MACHO_VALID_FILE_EXTENTS          0x00000100u
#define STAGE62_MACHO_VALID_ENTRY_NOT_EXECUTED    0x00000200u
#define STAGE62_MACHO_VALID_SECTION_BOUNDS        0x00000400u
#define STAGE62_MACHO_VALID_REQUIRED_SECTIONS     0x00000800u
#define STAGE62_MACHO_VALID_LOAD_PLAN             0x00001000u
#define STAGE62_MACHO_VALID_PRELINK_REPORT        0x00002000u
#define STAGE62_MACHO_VALID_STAGING               0x00004000u
#define STAGE62_MACHO_VALID_MATERIALIZED_PARSE    0x00008000u
#define STAGE62_MACHO_VALID_MARKERS               0x00010000u
#define STAGE62_MACHO_VALID_ZERO_FILL             0x00020000u
#define STAGE62_MACHO_VALID_REQUIRED              (STAGE62_MACHO_VALID_MAGIC | \
                                                   STAGE62_MACHO_VALID_ENDIAN | \
                                                   STAGE62_MACHO_VALID_CPU | \
                                                   STAGE62_MACHO_VALID_SUBTYPE | \
                                                   STAGE62_MACHO_VALID_FILETYPE | \
                                                   STAGE62_MACHO_VALID_COMMAND_BOUNDS | \
                                                   STAGE62_MACHO_VALID_SEGMENT_COMMAND | \
                                                   STAGE62_MACHO_VALID_REQUIRED_SEGMENTS | \
                                                   STAGE62_MACHO_VALID_FILE_EXTENTS | \
                                                   STAGE62_MACHO_VALID_ENTRY_NOT_EXECUTED | \
                                                   STAGE62_MACHO_VALID_SECTION_BOUNDS | \
                                                   STAGE62_MACHO_VALID_REQUIRED_SECTIONS | \
                                                   STAGE62_MACHO_VALID_LOAD_PLAN | \
                                                   STAGE62_MACHO_VALID_STAGING | \
                                                   STAGE62_MACHO_VALID_MATERIALIZED_PARSE | \
                                                   STAGE62_MACHO_VALID_MARKERS | \
                                                   STAGE62_MACHO_VALID_ZERO_FILL)

#define STAGE62_MACHO_FAIL_NULL                   0x00000001u
#define STAGE62_MACHO_FAIL_SIZE                   0x00000002u
#define STAGE62_MACHO_FAIL_MAGIC                  0x00000004u
#define STAGE62_MACHO_FAIL_ENDIAN                 0x00000008u
#define STAGE62_MACHO_FAIL_CPU                    0x00000010u
#define STAGE62_MACHO_FAIL_SUBTYPE                0x00000020u
#define STAGE62_MACHO_FAIL_FILETYPE               0x00000040u
#define STAGE62_MACHO_FAIL_COMMAND_BOUNDS         0x00000080u
#define STAGE62_MACHO_FAIL_COMMAND_SIZE           0x00000100u
#define STAGE62_MACHO_FAIL_SEGMENT_BOUNDS         0x00000200u
#define STAGE62_MACHO_FAIL_SEGMENT_EXTENT         0x00000400u
#define STAGE62_MACHO_FAIL_REQUIRED_SEGMENTS      0x00000800u
#define STAGE62_MACHO_FAIL_NO_ENTRY_METADATA      0x00001000u
#define STAGE62_MACHO_FAIL_SECTION_BOUNDS         0x00002000u
#define STAGE62_MACHO_FAIL_REQUIRED_SECTIONS      0x00004000u
#define STAGE62_MACHO_FAIL_LOAD_PLAN              0x00008000u
#define STAGE62_MACHO_FAIL_PHYS_RANGE             0x00010000u
#define STAGE62_MACHO_FAIL_LOAD_PLAN_FULL         0x00020000u
#define STAGE62_MACHO_FAIL_STAGE_SPAN             0x00040000u
#define STAGE62_MACHO_FAIL_STAGE_PLAN_FULL        0x00080000u
#define STAGE62_MACHO_FAIL_STAGE_SRC_BOUNDS       0x00100000u
#define STAGE62_MACHO_FAIL_STAGE_DST_BOUNDS       0x00200000u
#define STAGE62_MACHO_FAIL_STAGE_COPY             0x00400000u
#define STAGE62_MACHO_FAIL_STAGE_REPARSE          0x00800000u
#define STAGE62_MACHO_FAIL_STAGE_MARKERS          0x01000000u
#define STAGE62_MACHO_FAIL_STAGE_ZERO_FILL        0x02000000u
#define STAGE62_MACHO_FAIL_STAGE_ARENA_ALIAS      0x04000000u

#define STAGE62_MACHO_MARKER_TEXT                 0x00000001u
#define STAGE62_MACHO_MARKER_DATA                 0x00000002u
#define STAGE62_MACHO_MARKER_PRELINK_TEXT         0x00000004u
#define STAGE62_MACHO_MARKER_REQUIRED             (STAGE62_MACHO_MARKER_TEXT | \
                                                   STAGE62_MACHO_MARKER_DATA | \
                                                   STAGE62_MACHO_MARKER_PRELINK_TEXT)

#define STAGE62_MACHO_LOAD_PLAN_VERSION           1u
#define STAGE62_MACHO_LOAD_PLAN_MAX               8u
#define STAGE62_MACHO_LOAD_PLAN_FILE_BACKED       0x00000001u
#define STAGE62_MACHO_LOAD_PLAN_PRELINK           0x00000002u
#define STAGE62_MACHO_LOAD_PLAN_ZERO_FILL         0x00000004u

#define STAGE62_MACHO_STAGING_ARENA_CAP           0x00010000u
#define STAGE62_MACHO_STAGING_ARENA_ALIGN         4096u
#define STAGE62_MACHO_STAGING_PLAN_VERSION        1u
#define STAGE62_MACHO_STAGING_PLAN_MAX            STAGE62_MACHO_LOAD_PLAN_MAX

struct stage62_macho_load_plan_entry {
    uint32_t index;
    uint32_t segment_mask;
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
    uint32_t physaddr;
    uint32_t physsize;
    uint32_t flags;
};

struct stage62_macho_load_plan {
    uint32_t version;
    uint32_t entry_count;
    uint32_t vm_base;
    uint32_t vm_end;
    uint32_t phys_base;
    uint32_t phys_end;
    uint32_t file_end;
    uint32_t status;
    struct stage62_macho_load_plan_entry entries[STAGE62_MACHO_LOAD_PLAN_MAX];
};

struct stage62_macho_staging_plan_entry {
    uint32_t index;
    uint32_t segment_mask;
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t copy_src_off;
    uint32_t copy_dst;
    uint32_t copy_size;
    uint32_t zero_dst;
    uint32_t zero_size;
    uint32_t arena_offset;
    uint32_t flags;
};

struct stage62_macho_staging_plan {
    uint32_t version;
    uint32_t entry_count;
    uint32_t vm_base;
    uint32_t vm_end;
    uint32_t vm_span;
    uint32_t arena_base;
    uint32_t arena_end;
    uint32_t arena_size;
    uint32_t file_bytes;
    uint32_t zero_bytes;
    uint32_t materialized_header;
    uint32_t reparsed_vm_base;
    uint32_t reparsed_vm_end;
    uint32_t reparsed_getlastaddr;
    uint32_t marker_check_mask;
    uint32_t zero_check_mask;
    uint32_t validation_mask;
    uint32_t failure_mask;
    uint32_t status;
    struct stage62_macho_staging_plan_entry entries[STAGE62_MACHO_STAGING_PLAN_MAX];
};

struct stage62_macho_probe_result {
    uint32_t version;
    uint32_t artifact_base;
    uint32_t artifact_size;
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t load_command_seen_mask;
    uint32_t load_command_count_seen;
    uint32_t unknown_command_count;
    uint32_t segment_count;
    uint32_t segment_required_mask;
    uint32_t segment_seen_mask;
    uint32_t prelink_segment_seen_mask;
    uint32_t section_count;
    uint32_t section_required_mask;
    uint32_t section_seen_mask;
    uint32_t prelink_section_seen_mask;
    uint32_t entry_kind_mask;
    uint32_t entryoff_or_pc;
    uint32_t text_vmaddr;
    uint32_t text_vmsize;
    uint32_t text_fileoff;
    uint32_t text_filesize;
    uint32_t data_vmaddr;
    uint32_t data_vmsize;
    uint32_t data_fileoff;
    uint32_t data_filesize;
    uint32_t linkedit_vmaddr;
    uint32_t linkedit_vmsize;
    uint32_t linkedit_fileoff;
    uint32_t linkedit_filesize;
    uint32_t prelink_text_vmaddr;
    uint32_t prelink_text_vmsize;
    uint32_t prelink_text_fileoff;
    uint32_t prelink_text_filesize;
    uint32_t prelink_info_vmaddr;
    uint32_t prelink_info_vmsize;
    uint32_t prelink_info_fileoff;
    uint32_t prelink_info_filesize;
    uint32_t prelink_state_vmaddr;
    uint32_t prelink_state_vmsize;
    uint32_t prelink_state_fileoff;
    uint32_t prelink_state_filesize;
    uint32_t text_section_vmaddr;
    uint32_t text_section_size;
    uint32_t data_const_vmaddr;
    uint32_t data_const_size;
    uint32_t prelink_text_section_vmaddr;
    uint32_t prelink_text_section_size;
    uint32_t prelink_info_section_vmaddr;
    uint32_t prelink_info_section_size;
    uint32_t min_vmaddr;
    uint32_t max_vmaddr;
    uint32_t max_file_extent;
    uint32_t load_vm_base;
    uint32_t load_vm_end;
    uint32_t load_file_base;
    uint32_t load_file_end;
    uint32_t load_phys_base;
    uint32_t load_phys_end;
    uint32_t load_phys_size;
    uint32_t load_plan_count;
    uint32_t load_plan_status;
    struct stage62_macho_load_plan load_plan;
    struct stage62_macho_staging_plan staging_plan;
    uint32_t staging_status;
    uint32_t staging_file_bytes;
    uint32_t staging_zero_bytes;
    uint32_t staging_arena_base;
    uint32_t staging_arena_end;
    uint32_t staging_vm_base;
    uint32_t staging_vm_end;
    uint32_t validation_mask;
    uint32_t failure_mask;
    uint32_t status;
};

#define STAGE62_LOADER_PREFLIGHT_VERSION          1u
#define STAGE62_XNU_BASELINE_2050_22_13           0x20502213u
#define STAGE62_XNU_BASELINE_COMMIT_CC8A9B0C      0xcc8a9b0cu
#define STAGE62_XNU_BASELINE_MASTER_12_3_0        0x000c0300u
#define STAGE62_XNU_ARM_REFERENCE_4570_1_46       0x45700146u

#define STAGE62_XNU_TTE_WORKSPACE_PAGES           10u
#define STAGE62_XNU_TTE_WORKSPACE_BYTES           (STAGE62_XNU_TTE_WORKSPACE_PAGES * 4096u)
#define STAGE62_XNU_TTE_ALIGNMENT                 0x00004000u
#define STAGE62_XNU_TTE_DRYRUN_VERSION            1u
#define STAGE62_XNU_TTE_PAGE_SIZE                 4096u
#define STAGE62_XNU_TTE_L1_ALIGN                  0x00004000u
#define STAGE62_XNU_TTE_L1_SIZE                   0x00004000u
#define STAGE62_XNU_TTE_L1_ENTRY_COUNT            4096u
#define STAGE62_XNU_TTE_L1_SECTION_SIZE           0x00100000u
#define STAGE62_XNU_TTE_L2_ALIGN                  0x00000400u
#define STAGE62_XNU_TTE_L2_SIZE                   0x00001000u
#define STAGE62_XNU_TTE_DESC_SECTION_SO           0x00010c02u
#define STAGE62_XNU_TTE_DESC_TYPE_MASK            0x00000003u
#define STAGE62_XNU_TTE_DESC_TYPE_SECTION         0x00000002u
#define STAGE62_XNU_TTE_DESC_BASE_MASK            0xfff00000u
#define STAGE62_XNU_TTE_DESC_ATTR_MASK            0x000fffffu
#define STAGE62_XNU_TTE_SECTION_OFFSET_MASK       0x000fffffu

#define STAGE62_XNU_SAFE_TABLE_VERSION             1u
#define STAGE62_XNU_SAFE_TABLE_ALIGNMENT           STAGE62_XNU_TTE_L1_ALIGN
#define STAGE62_XNU_SAFE_TABLE_L1_BYTES            STAGE62_XNU_TTE_L1_SIZE
#define STAGE62_XNU_SAFE_TABLE_L1_COUNT            STAGE62_XNU_TTE_L1_ENTRY_COUNT
#define STAGE62_XNU_SAFE_TABLE_KIND_IDENTITY       0x00000001u
#define STAGE62_XNU_SAFE_TABLE_KIND_HIGHVA         0x00000002u
#define STAGE62_XNU_SAFE_TABLE_KIND_REQUIRED       0x00000003u
#define STAGE62_XNU_SAFE_TABLE_MAT_IDENTITY        0x00000001u
#define STAGE62_XNU_SAFE_TABLE_MAT_HIGHVA          0x00000002u
#define STAGE62_XNU_SAFE_TABLE_MAT_REQUIRED        0x00000003u

#define STAGE62_TTE_DESC_VERIFY_LOWMEM_FIRST      0x00000001u
#define STAGE62_TTE_DESC_VERIFY_LOWMEM_LAST       0x00000002u
#define STAGE62_TTE_DESC_VERIFY_KERNEL_FIRST      0x00000004u
#define STAGE62_TTE_DESC_VERIFY_KERNEL_LAST       0x00000008u
#define STAGE62_TTE_DESC_VERIFY_RAM_CONSOLE       0x00000010u
#define STAGE62_TTE_DESC_VERIFY_GIC_FIRST         0x00000020u
#define STAGE62_TTE_DESC_VERIFY_GIC_LAST          0x00000040u
#define STAGE62_TTE_DESC_VERIFY_HIGHVA_FIRST      0x00000080u
#define STAGE62_TTE_DESC_VERIFY_HIGHVA_LAST       0x00000100u
#define STAGE62_TTE_DESC_VERIFY_REQUIRED          0x000001ffu

#define STAGE62_TTE_TRANSLATE_KERNEL_FIRST        0x00000001u
#define STAGE62_TTE_TRANSLATE_KERNEL_LAST         0x00000002u
#define STAGE62_TTE_TRANSLATE_LOW_RAM             0x00000004u
#define STAGE62_TTE_TRANSLATE_RAM_CONSOLE         0x00000008u
#define STAGE62_TTE_TRANSLATE_GIC                 0x00000010u
#define STAGE62_TTE_TRANSLATE_HIGHVA_FIRST        0x00000020u
#define STAGE62_TTE_TRANSLATE_HIGHVA_LAST         0x00000040u
#define STAGE62_TTE_TRANSLATE_REQUIRED            0x0000007fu

#define STAGE62_TTE_XNU_POLICY_BOOT_TTEP          0x00000001u
#define STAGE62_TTE_XNU_POLICY_WORKSPACE          0x00000002u
#define STAGE62_TTE_XNU_POLICY_AVAIL_START        0x00000004u
#define STAGE62_TTE_XNU_POLICY_AVAIL_END          0x00000008u
#define STAGE62_TTE_XNU_POLICY_SAFE_DRYRUN        0x00000010u
#define STAGE62_TTE_XNU_POLICY_REQUIRED           0x0000001fu

#define STAGE62_TTE_SAT_WORKSPACE_RANGE           0x00000001u
#define STAGE62_TTE_SAT_WORKSPACE_ALIGNMENT       0x00000002u
#define STAGE62_TTE_SAT_LAYOUT_FITS               0x00000004u
#define STAGE62_TTE_SAT_KERNEL_MAPPING            0x00000008u
#define STAGE62_TTE_SAT_DEVICE_MAPPING            0x00000010u
#define STAGE62_TTE_SAT_RAM_CONSOLE_MAPPING       0x00000020u
#define STAGE62_TTE_SAT_NO_OVERLAP                0x00000040u
#define STAGE62_TTE_SAT_NO_TTBR_WRITE             0x00000080u
#define STAGE62_TTE_SAT_CACHES_UNCHANGED          0x00000100u
#define STAGE62_TTE_SAT_LOCAL_SIM_ONLY            0x00000200u
#define STAGE62_TTE_SAT_DESCRIPTOR_READBACK       0x00000400u
#define STAGE62_TTE_SAT_DESCRIPTOR_ATTRS          0x00000800u
#define STAGE62_TTE_SAT_TRANSLATION_KERNEL        0x00001000u
#define STAGE62_TTE_SAT_TRANSLATION_LOW_RAM       0x00002000u
#define STAGE62_TTE_SAT_TRANSLATION_CONSOLE       0x00004000u
#define STAGE62_TTE_SAT_TRANSLATION_DEVICE        0x00008000u
#define STAGE62_TTE_SAT_XNU_BOOT_POLICY           0x00010000u
#define STAGE62_TTE_SAT_HIGHVA_MAPPING            0x00020000u
#define STAGE62_TTE_SAT_HIGHVA_TRANSLATION        0x00040000u
#define STAGE62_TTE_SAT_SAFE_TABLE                0x00080000u
#define STAGE62_TTE_SAT_STAGE_OWNED_TABLES        0x00100000u
#define STAGE62_TTE_SAT_REQUIRED                  0x001fffffu

#define STAGE62_TTE_FAIL_WORKSPACE_RANGE          0x00000001u
#define STAGE62_TTE_FAIL_WORKSPACE_ALIGNMENT      0x00000002u
#define STAGE62_TTE_FAIL_LAYOUT_OVERFLOW          0x00000004u
#define STAGE62_TTE_FAIL_KERNEL_MAPPING           0x00000008u
#define STAGE62_TTE_FAIL_DEVICE_MAPPING           0x00000010u
#define STAGE62_TTE_FAIL_RAM_CONSOLE_MAPPING      0x00000020u
#define STAGE62_TTE_FAIL_OVERLAP                  0x00000040u
#define STAGE62_TTE_FAIL_LOCAL_ARENA_ALIAS        0x00000080u
#define STAGE62_TTE_FAIL_CHECKSUM                 0x00000100u
#define STAGE62_TTE_FAIL_DESCRIPTOR_READBACK      0x00000200u
#define STAGE62_TTE_FAIL_DESCRIPTOR_ATTRS         0x00000400u
#define STAGE62_TTE_FAIL_TRANSLATION_KERNEL       0x00000800u
#define STAGE62_TTE_FAIL_TRANSLATION_LOW_RAM      0x00001000u
#define STAGE62_TTE_FAIL_TRANSLATION_CONSOLE      0x00002000u
#define STAGE62_TTE_FAIL_TRANSLATION_DEVICE       0x00004000u
#define STAGE62_TTE_FAIL_XNU_BOOT_POLICY          0x00008000u
#define STAGE62_TTE_FAIL_HIGHVA_MAPPING           0x00010000u
#define STAGE62_TTE_FAIL_HIGHVA_TRANSLATION       0x00020000u
#define STAGE62_TTE_FAIL_SAFE_TABLE               0x00040000u
#define STAGE62_TTE_FAIL_STAGE_OWNED_TABLES       0x00080000u
#define STAGE62_TTE_FAIL_SAFETY                   0x80000000u

struct stage62_xnu_safe_table_materialization {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t kind_mask;
    uint32_t local_base;
    uint32_t local_limit;
    uint32_t identity_l1_base;
    uint32_t identity_l1_limit;
    uint32_t highva_l1_base;
    uint32_t highva_l1_limit;
    uint32_t l1_entry_count;
    uint32_t l1_bytes;
    uint32_t written_bytes;
    uint32_t materialized_mask;
    uint32_t failure_mask;
    uint32_t checksum;
};

struct stage62_xnu_tte_dryrun {
    uint32_t version;
    uint32_t size;
    uint32_t source_macho_status;
    uint32_t source_staging_status;
    uint32_t source_load_phys_base;
    uint32_t source_load_phys_end;
    uint32_t source_load_virt_base;
    uint32_t source_load_virt_end;
    uint32_t proposed_physBase;
    uint32_t proposed_virtBase;
    uint32_t proposed_memSize;
    uint32_t proposed_topOfKernelData;
    uint32_t proposed_avail_start;
    uint32_t workspace_base;
    uint32_t workspace_size;
    uint32_t workspace_limit;
    uint32_t workspace_alignment;
    uint32_t local_arena_base;
    uint32_t local_arena_size;
    uint32_t local_arena_limit;
    uint32_t l1_table_base;
    uint32_t l1_table_size;
    uint32_t l1_table_limit;
    uint32_t l1_entry_count;
    uint32_t l2_table_base;
    uint32_t l2_table_size;
    uint32_t l2_table_limit;
    uint32_t scratch_base;
    uint32_t scratch_size;
    uint32_t scratch_limit;
    uint32_t kernel_l1_first_index;
    uint32_t kernel_l1_last_index;
    uint32_t kernel_l1_section_count;
    uint32_t kernel_section_descriptor;
    uint32_t lowmem_l1_first_index;
    uint32_t lowmem_l1_last_index;
    uint32_t lowmem_l1_section_count;
    uint32_t ram_console_l1_index;
    uint32_t ram_console_section_descriptor;
    uint32_t gic_l1_first_index;
    uint32_t gic_l1_last_index;
    uint32_t device_section_descriptor;
    uint32_t descriptor_verify_mask;
    uint32_t descriptor_failure_mask;
    uint32_t descriptor_lowmem_first_word;
    uint32_t descriptor_lowmem_last_word;
    uint32_t descriptor_kernel_first_word;
    uint32_t descriptor_kernel_last_word;
    uint32_t descriptor_ram_console_word;
    uint32_t descriptor_gic_first_word;
    uint32_t descriptor_gic_last_word;
    uint32_t descriptor_type_mask_seen;
    uint32_t descriptor_attr_mask_seen;
    uint32_t descriptor_expected_attr_mask;
    uint32_t translation_check_mask;
    uint32_t translation_failure_mask;
    uint32_t translation_case_count;
    uint32_t translation_kernel_text_va;
    uint32_t translation_kernel_text_pa;
    uint32_t translation_kernel_last_va;
    uint32_t translation_kernel_last_pa;
    uint32_t translation_lowmem_va;
    uint32_t translation_lowmem_pa;
    uint32_t translation_ram_console_va;
    uint32_t translation_ram_console_pa;
    uint32_t translation_gic_va;
    uint32_t translation_gic_pa;
    uint32_t xnu_boot_ttep_expected;
    uint32_t xnu_avail_start_expected;
    uint32_t xnu_avail_end_expected;
    uint32_t xnu_policy_mask;
    uint32_t identity_l1_local_base;
    uint32_t identity_l1_local_limit;
    uint32_t highva_l1_local_base;
    uint32_t highva_l1_local_limit;
    uint32_t highva_virt_base;
    uint32_t highva_virt_end;
    uint32_t highva_phys_base;
    uint32_t highva_phys_end;
    uint32_t highva_virt_phys_delta;
    uint32_t highva_l1_first_index;
    uint32_t highva_l1_last_index;
    uint32_t highva_l1_section_count;
    uint32_t descriptor_highva_first_word;
    uint32_t descriptor_highva_last_word;
    uint32_t translation_highva_first_va;
    uint32_t translation_highva_first_pa;
    uint32_t translation_highva_last_va;
    uint32_t translation_highva_last_pa;
    uint32_t proposed_phys_load_written;
    uint32_t proposed_tte_workspace_written;
    uint32_t live_mmu_tables_replaced;
    uint32_t ttbcr_written;
    uint32_t dacr_written;
    uint32_t tlbs_invalidated;
    uint32_t sctlr_written;
    uint32_t stage_owned_tables_only;
    struct stage62_xnu_safe_table_materialization safe_table;
    uint32_t ttbr0_written;
    uint32_t ttbr1_written;
    uint32_t caches_enabled;
    uint32_t local_sim_zeroed;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t checksum;
    uint32_t status;
};


#define STAGE62_TTBR_RT_VERSION                    1u
#define STAGE62_TTBR_RT_CACHE_MASK                 ((1u << 2) | (1u << 12))

#define STAGE62_TTBR_RT_SAT_PREFLIGHT              0x00000001u
#define STAGE62_TTBR_RT_SAT_TABLE_ALIGNED          0x00000002u
#define STAGE62_TTBR_RT_SAT_STAGE_OWNED_TABLE      0x00000004u
#define STAGE62_TTBR_RT_SAT_REQUIRED_MAPPINGS      0x00000008u
#define STAGE62_TTBR_RT_SAT_DESCRIPTOR_READBACK    0x00000010u
#define STAGE62_TTBR_RT_SAT_STATE_SAVED            0x00000020u
#define STAGE62_TTBR_RT_SAT_CACHE_BITS_PRESERVED   0x00000040u
#define STAGE62_TTBR_RT_SAT_SWITCHED_TO_STAGE_L1   0x00000080u
#define STAGE62_TTBR_RT_SAT_DURING_SELFTEST        0x00000100u
#define STAGE62_TTBR_RT_SAT_ORIGINAL_RESTORED      0x00000200u
#define STAGE62_TTBR_RT_SAT_POST_RESTORE_SELFTEST  0x00000400u
#define STAGE62_TTBR_RT_SAT_NO_XNU_EXEC            0x00000800u
#define STAGE62_TTBR_RT_SAT_NO_MACHO_EXEC          0x00001000u
#define STAGE62_TTBR_RT_SAT_NO_PROPOSED_PHYS_WRITE 0x00002000u
#define STAGE62_TTBR_RT_SAT_NO_PROPOSED_TTE_WRITE  0x00004000u
#define STAGE62_TTBR_RT_SAT_NO_PERSIST_WRITE       0x00008000u
#define STAGE62_TTBR_RT_SAT_REQUIRED               0x0000ffffu

#define STAGE62_TTBR_RT_FAIL_PREFLIGHT             0x00000001u
#define STAGE62_TTBR_RT_FAIL_ALIGNMENT             0x00000002u
#define STAGE62_TTBR_RT_FAIL_OWNERSHIP             0x00000004u
#define STAGE62_TTBR_RT_FAIL_MAPPING               0x00000008u
#define STAGE62_TTBR_RT_FAIL_DESCRIPTOR            0x00000010u
#define STAGE62_TTBR_RT_FAIL_STATE_SAVE            0x00000020u
#define STAGE62_TTBR_RT_FAIL_CACHE_CHANGE          0x00000040u
#define STAGE62_TTBR_RT_FAIL_SWITCH                0x00000080u
#define STAGE62_TTBR_RT_FAIL_DURING_SELFTEST       0x00000100u
#define STAGE62_TTBR_RT_FAIL_RESTORE               0x00000200u
#define STAGE62_TTBR_RT_FAIL_POST_RESTORE          0x00000400u
#define STAGE62_TTBR_RT_FAIL_SAFETY                0x80000000u

#define STAGE62_TTBR_RT_MAP_LOW_STAGE              0x00000001u
#define STAGE62_TTBR_RT_MAP_STACK                  0x00000002u
#define STAGE62_TTBR_RT_MAP_VECTOR                 0x00000004u
#define STAGE62_TTBR_RT_MAP_TABLE                  0x00000008u
#define STAGE62_TTBR_RT_MAP_RAM_CONSOLE0           0x00000010u
#define STAGE62_TTBR_RT_MAP_RAM_CONSOLE1           0x00000020u
#define STAGE62_TTBR_RT_MAP_IMEM                   0x00000040u
#define STAGE62_TTBR_RT_MAP_GIC_TIMER              0x00000080u
#define STAGE62_TTBR_RT_MAP_PSHOLD                 0x00000100u
#define STAGE62_TTBR_RT_MAP_REQUIRED               0x000001ffu

struct stage62_ttbr0_roundtrip {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t original_sctlr;
    uint32_t original_ttbr0;
    uint32_t original_ttbcr;
    uint32_t original_dacr;
    uint32_t original_vbar;
    uint32_t stage_l1_base;
    uint32_t stage_l1_limit;
    uint32_t stage_l1_size;
    uint32_t stage_l1_entry_count;
    uint32_t stage_l1_checksum_before;
    uint32_t stage_l1_checksum_after;
    uint32_t current_sp;
    uint32_t current_sp_section;
    uint32_t vector_base;
    uint32_t vector_section;
    uint32_t image_base;
    uint32_t image_end;
    uint32_t image_first_section;
    uint32_t image_last_section;
    uint32_t mapping_required_mask;
    uint32_t mapping_present_mask;
    uint32_t mapping_failure_mask;
    uint32_t descriptor_low_word;
    uint32_t descriptor_stack_word;
    uint32_t descriptor_vector_word;
    uint32_t descriptor_table_word;
    uint32_t descriptor_ram_console0_word;
    uint32_t descriptor_ram_console1_word;
    uint32_t descriptor_imem_word;
    uint32_t descriptor_gic_timer_word;
    uint32_t descriptor_pshold_word;
    uint32_t switched_ttbr0;
    uint32_t switched_ttbcr;
    uint32_t switched_dacr;
    uint32_t switched_sctlr;
    uint32_t restored_ttbr0;
    uint32_t restored_ttbcr;
    uint32_t restored_dacr;
    uint32_t restored_sctlr;
    uint32_t cache_bits_before;
    uint32_t cache_bits_during;
    uint32_t cache_bits_after;
    uint32_t selftest_probe_before;
    uint32_t selftest_probe_during;
    uint32_t selftest_probe_after;
    uint32_t ram_console_sig_during;
    uint32_t gicd_ctlr_during;
    uint32_t gicc_ctlr_during;
    uint32_t timer_freq_during;
    uint32_t restart_reason_during;
    uint32_t ttbr0_write_count;
    uint32_t ttbcr_write_count;
    uint32_t dacr_write_count;
    uint32_t sctlr_write_count;
    uint32_t tlb_invalidate_count;
    uint32_t xnu_entry_executed;
    uint32_t macho_bytes_executed;
    uint32_t proposed_phys_load_written;
    uint32_t proposed_tte_workspace_written;
    uint32_t persistent_write_attempted;
    uint32_t caches_changed;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t checksum;
};

#define STAGE62_DT_READY_BINARY_SELFTEST          0x00000001u
#define STAGE62_DT_READY_CHOSEN                   0x00000002u
#define STAGE62_DT_READY_MEMORY                   0x00000004u
#define STAGE62_DT_READY_CPUS                     0x00000008u
#define STAGE62_DT_READY_INTERRUPT_CONTROLLER     0x00000010u
#define STAGE62_DT_READY_TIMER                    0x00000020u
#define STAGE62_DT_READY_DEVICE_TREE_NODE         0x00000040u
#define STAGE62_DT_READY_TARGET_TYPE              0x00000080u
#define STAGE62_DT_READY_MODEL                    0x00000100u
#define STAGE62_DT_READY_BOOT_ARGS                0x00000200u
#define STAGE62_DT_READY_RAM_CONSOLE              0x00000400u
#define STAGE62_DT_READY_CPU_CLOCKS               0x00000800u
#define STAGE62_DT_READY_REQUIRED                 (STAGE62_DT_READY_BINARY_SELFTEST | \
                                                   STAGE62_DT_READY_CHOSEN | \
                                                   STAGE62_DT_READY_MEMORY | \
                                                   STAGE62_DT_READY_CPUS | \
                                                   STAGE62_DT_READY_INTERRUPT_CONTROLLER | \
                                                   STAGE62_DT_READY_TIMER | \
                                                   STAGE62_DT_READY_DEVICE_TREE_NODE | \
                                                   STAGE62_DT_READY_TARGET_TYPE | \
                                                   STAGE62_DT_READY_MODEL | \
                                                   STAGE62_DT_READY_BOOT_ARGS | \
                                                   STAGE62_DT_READY_RAM_CONSOLE | \
                                                   STAGE62_DT_READY_CPU_CLOCKS)

#define STAGE62_PLATFORM_GAP_PMAP_BOOTSTRAP       0x00000001u
#define STAGE62_PLATFORM_GAP_PEXPERT_IMPL         0x00000002u
#define STAGE62_PLATFORM_GAP_GIC_HOOK             0x00000004u
#define STAGE62_PLATFORM_GAP_TIMER_HOOK           0x00000008u
#define STAGE62_PLATFORM_GAP_IOKIT_STACK          0x00000010u
#define STAGE62_PLATFORM_GAP_REQUIRED_RECORDED    (STAGE62_PLATFORM_GAP_PMAP_BOOTSTRAP | \
                                                   STAGE62_PLATFORM_GAP_PEXPERT_IMPL | \
                                                   STAGE62_PLATFORM_GAP_GIC_HOOK | \
                                                   STAGE62_PLATFORM_GAP_TIMER_HOOK | \
                                                   STAGE62_PLATFORM_GAP_IOKIT_STACK)

#define STAGE62_LOADER_IRQ_READY_GIC_DIST         0x00000001u
#define STAGE62_LOADER_IRQ_READY_GIC_CPU          0x00000002u
#define STAGE62_LOADER_IRQ_READY_SERVICE          0x00000004u
#define STAGE62_LOADER_IRQ_READY_TIMEBASE         0x00000008u
#define STAGE62_LOADER_IRQ_READY_REQUIRED         (STAGE62_LOADER_IRQ_READY_GIC_DIST | \
                                                   STAGE62_LOADER_IRQ_READY_GIC_CPU | \
                                                   STAGE62_LOADER_IRQ_READY_SERVICE | \
                                                   STAGE62_LOADER_IRQ_READY_TIMEBASE)

#define STAGE62_LOADER_SAFETY_NO_EXECUTE          0x00000001u
#define STAGE62_LOADER_SAFETY_STAGE_OWNED_TTBR    0x00000002u
#define STAGE62_LOADER_SAFETY_CACHES_UNCHANGED    0x00000004u
#define STAGE62_LOADER_SAFETY_NO_PERSIST_WRITE    0x00000008u
#define STAGE62_LOADER_SAFETY_RAM_CONSOLE         0x00000010u
#define STAGE62_LOADER_SAFETY_LOCAL_ARENA_ONLY    0x00000020u
#define STAGE62_LOADER_SAFETY_NO_PHYS_WRITE       0x00000040u
#define STAGE62_LOADER_SAFETY_TTE_DRYRUN_ONLY     0x00000080u
#define STAGE62_LOADER_SAFETY_TTBR_RESTORED       0x00000100u
#define STAGE62_LOADER_SAFETY_NO_CACHE_CHANGE     0x00000200u
#define STAGE62_LOADER_SAFETY_TTE_VERIFY_ONLY     0x00000400u
#define STAGE62_LOADER_SAFETY_VTOP_DRYRUN_ONLY    0x00000800u
#define STAGE62_LOADER_SAFETY_HIGHVA_DRYRUN_ONLY  0x00001000u
#define STAGE62_LOADER_SAFETY_SAFE_TABLE_LOCAL_ONLY 0x00002000u
#define STAGE62_LOADER_SAFETY_STAGE_OWNED_TABLES_ONLY 0x00004000u
#define STAGE62_LOADER_SAFETY_NO_FULL_XNU_BUILD   0x00008000u
#define STAGE62_LOADER_SAFETY_NO_PUBLIC_XNU_EXEC  0x00010000u
#define STAGE62_LOADER_SAFETY_NO_EXTERNAL_MUTATION 0x00020000u
#define STAGE62_LOADER_SAFETY_CONTROLLED_PUBLIC_XNU_LINK_NO_EXEC 0x00040000u
#define STAGE62_LOADER_SAFETY_XNU_COMPILE_GRAPH_NO_EXEC 0x00080000u
#define STAGE62_LOADER_SAFETY_NO_PLATFORM_RUNTIME_EXEC 0x00100000u
#define STAGE62_LOADER_SAFETY_REQUIRED            (STAGE62_LOADER_SAFETY_NO_EXECUTE | \
                                                   STAGE62_LOADER_SAFETY_STAGE_OWNED_TTBR | \
                                                   STAGE62_LOADER_SAFETY_CACHES_UNCHANGED | \
                                                   STAGE62_LOADER_SAFETY_NO_PERSIST_WRITE | \
                                                   STAGE62_LOADER_SAFETY_RAM_CONSOLE | \
                                                   STAGE62_LOADER_SAFETY_LOCAL_ARENA_ONLY | \
                                                   STAGE62_LOADER_SAFETY_NO_PHYS_WRITE | \
                                                   STAGE62_LOADER_SAFETY_TTE_DRYRUN_ONLY | \
                                                   STAGE62_LOADER_SAFETY_TTBR_RESTORED | \
                                                   STAGE62_LOADER_SAFETY_NO_CACHE_CHANGE | \
                                                   STAGE62_LOADER_SAFETY_TTE_VERIFY_ONLY | \
                                                   STAGE62_LOADER_SAFETY_VTOP_DRYRUN_ONLY | \
                                                   STAGE62_LOADER_SAFETY_HIGHVA_DRYRUN_ONLY | \
                                                   STAGE62_LOADER_SAFETY_SAFE_TABLE_LOCAL_ONLY | \
                                                   STAGE62_LOADER_SAFETY_STAGE_OWNED_TABLES_ONLY | \
                                                   STAGE62_LOADER_SAFETY_NO_FULL_XNU_BUILD | \
                                                   STAGE62_LOADER_SAFETY_NO_PUBLIC_XNU_EXEC | \
                                                   STAGE62_LOADER_SAFETY_NO_EXTERNAL_MUTATION | \
                                                   STAGE62_LOADER_SAFETY_CONTROLLED_PUBLIC_XNU_LINK_NO_EXEC | \
                                                   STAGE62_LOADER_SAFETY_XNU_COMPILE_GRAPH_NO_EXEC | \
                                                   STAGE62_LOADER_SAFETY_NO_PLATFORM_RUNTIME_EXEC)

#define STAGE62_XNU_BOOTSTRAP_CONTRACT_VERSION    1u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_PAGE_SIZE  4096u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_PAGE_ALIGN_MASK (STAGE62_XNU_BOOTSTRAP_CONTRACT_PAGE_SIZE - 1u)

#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_SOURCE_ROLLUPS 0x00000001u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_MACHO_IMPORTED 0x00000002u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_STAGING_IMPORTED 0x00000004u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_TTE_IMPORTED 0x00000008u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_HIGHVA_IMPORTED 0x00000010u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_SAFE_TABLE_LOCAL_ONLY 0x00000020u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_TTBR_RESTORED 0x00000040u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_CACHES_PRESERVED 0x00000080u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_VIRTBASE_ALIGNED 0x00000100u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_PHYSBASE_ALIGNED 0x00000200u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_TOPOFKERNELDATA_ALIGNED 0x00000400u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_ALIGNED 0x00000800u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_AVAIL_START_ALIGNED 0x00001000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_AFTER_IMAGE 0x00002000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_AVAIL_START_AFTER_WORKSPACE 0x00004000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_AVAIL_END_INSIDE_RAM 0x00008000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_STAGE_IMAGE_OVERLAP 0x00010000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_RAM_CONSOLE_OVERLAP 0x00020000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_DEVICE_TREE_OVERLAP 0x00040000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_STAGING_ARENA_OVERLAP 0x00080000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_SAFE_TABLE_OVERLAP 0x00100000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_PAGE_COVERAGE 0x00200000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_SECTION_COVERAGE 0x00400000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_XNU_EXEC 0x00800000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PLATFORM_RUNTIME_EXEC 0x01000000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_MACHO_EXEC 0x02000000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PROPOSED_PHYS_WRITE 0x04000000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PROPOSED_TTE_WRITE 0x08000000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PERSIST_WRITE 0x10000000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_NO_CACHE_POLICY_CHANGE 0x20000000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SAT_FAIL_CLOSED 0x40000000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_REQUIRED_MASK 0x7fffffffu

#define STAGE62_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_LOADED_IMAGE 0x00000001u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_FILE_BYTES   0x00000002u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_ZERO_FILL    0x00000004u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_WORKSPACE    0x00000008u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_L1_SECTIONS  0x00000010u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_REQUIRED     0x0000001fu

#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_IDENTITY_LOW 0x00000001u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_IDENTITY_KERNEL 0x00000002u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_RAM_CONSOLE 0x00000004u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_GIC 0x00000008u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_HIGHVA 0x00000010u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_REQUIRED 0x0000001fu

#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_SOURCE_ROLLUP 0x00000001u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_ALIGNMENT 0x00000002u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE_ORDER 0x00000004u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_AVAIL_RANGE 0x00000008u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_STAGE_IMAGE_OVERLAP 0x00000010u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_RAM_CONSOLE_OVERLAP 0x00000020u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_DEVICE_TREE_OVERLAP 0x00000040u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_LOCAL_ARENA_OVERLAP 0x00000080u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFE_TABLE_OVERLAP 0x00000100u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_PAGE_COVERAGE 0x00000200u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_SECTION_COVERAGE 0x00000400u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_TTBR_RESTORE 0x00000800u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_CACHE_POLICY 0x00001000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_CHECKSUM 0x00002000u
#define STAGE62_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage62_xnu_bootstrap_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_macho_status;
    uint32_t source_staging_status;
    uint32_t source_materialized_status;
    uint32_t source_tte_dryrun_status;
    uint32_t source_safe_table_status;
    uint32_t source_ttbr_roundtrip_status;
    uint32_t source_ttbr_restored_status;
    uint32_t source_cache_preserved_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t proposed_virtBase;
    uint32_t proposed_physBase;
    uint32_t proposed_memSize;
    uint32_t loaded_phys_base;
    uint32_t loaded_phys_end;
    uint32_t loaded_virt_base;
    uint32_t loaded_virt_end;
    uint32_t loaded_file_end;
    uint32_t proposed_topOfKernelData;
    uint32_t ttep_workspace_base;
    uint32_t ttep_workspace_size;
    uint32_t ttep_workspace_limit;
    uint32_t proposed_avail_start;
    uint32_t proposed_avail_end;
    uint32_t page_size;
    uint32_t page_align_mask;
    uint32_t loaded_image_bytes;
    uint32_t loaded_image_page_count;
    uint32_t loaded_file_page_count;
    uint32_t zero_fill_bytes;
    uint32_t zero_fill_page_count;
    uint32_t workspace_page_count;
    uint32_t identity_l1_first_index;
    uint32_t identity_l1_last_index;
    uint32_t identity_l1_section_count;
    uint32_t highva_l1_first_index;
    uint32_t highva_l1_last_index;
    uint32_t highva_l1_section_count;
    uint32_t page_coverage_mask;
    uint32_t section_coverage_mask;
    uint32_t stage_image_base;
    uint32_t stage_image_end;
    uint32_t device_tree_base;
    uint32_t device_tree_end;
    uint32_t staging_arena_base;
    uint32_t staging_arena_end;
    uint32_t safe_table_base;
    uint32_t safe_table_end;
    uint32_t ram_console_base;
    uint32_t ram_console_end;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_macho_exec;
    uint32_t no_proposed_phys_write;
    uint32_t no_proposed_tte_write;
    uint32_t no_persist_write;
    uint32_t no_cache_policy_change;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_VERSION    1u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_SAT_ROOT   0x00000001u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_SAT_VM_PLAN 0x00000002u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_SAT_VM_STATE 0x00000004u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_SAT_ALLOCATOR 0x00000008u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_SAT_WORKSPACE 0x00000010u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_SAT_POLICY 0x00000020u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_REQUIRED   0x0000003fu

#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_ROOT  0x00000001u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_VM_PLAN 0x00000002u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_VM_STATE 0x00000004u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_ALLOCATOR 0x00000008u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_WORKSPACE 0x00000010u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_POLICY 0x00000020u
#define STAGE62_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_CHECKSUM 0x00000040u

struct stage62_pmap_bootstrap_snapshot {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t root_status;
    uint32_t root_steps;
    uint32_t root_checksum;
    uint32_t vm_plan_status;
    uint32_t vm_plan_satisfied_mask;
    uint32_t vm_plan_checksum;
    uint32_t vm_state_status;
    uint32_t vm_state_satisfied_mask;
    uint32_t vm_state_checksum;
    uint32_t allocator_status;
    uint32_t allocator_satisfied_mask;
    uint32_t allocator_checksum;
    uint32_t pmap_workspace_status;
    uint32_t pmap_workspace_satisfied_mask;
    uint32_t pmap_workspace_checksum;
    uint32_t kernel_map_base;
    uint32_t kernel_map_limit;
    uint32_t memory_base;
    uint32_t memory_size;
    uint32_t available_memory_base;
    uint32_t available_memory_cursor;
    uint32_t bootstrap_alloc_base;
    uint32_t bootstrap_alloc_size;
    uint32_t bootstrap_alloc_end;
    uint32_t pmap_section_size;
    uint32_t pmap_section_descriptor;
    uint32_t pmap_l1_table_phys;
    uint32_t pmap_l1_table_virt;
    uint32_t mmu_enabled;
    uint32_t cache_policy;
    uint32_t allocator_span_base;
    uint32_t allocator_span_size;
    uint32_t allocator_span_end;
    uint32_t allocator_initial_cursor;
    uint32_t allocator_current_cursor;
    uint32_t allocator_remaining_bytes;
    uint32_t allocator_first_alloc_base;
    uint32_t allocator_first_alloc_size;
    uint32_t allocator_first_alloc_end;
    uint32_t allocator_first_alloc_tag;
    uint32_t allocator_alignment;
    uint32_t workspace_base;
    uint32_t workspace_limit;
    uint32_t workspace_size;
    uint32_t workspace_section_count;
    uint32_t workspace_l1_table_phys;
    uint32_t workspace_l1_table_virt;
    uint32_t workspace_l1_section_descriptor;
    uint32_t workspace_l1_section_size;
    uint32_t workspace_allocation_tag;
    uint32_t workspace_mmu_enabled;
    uint32_t workspace_cache_policy;
    uint32_t stage_owned_snapshot;
    uint32_t no_live_pmap_tables_installed;
    uint32_t checksum;
};

#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_VERSION 1u
#define STAGE62_XNU_PMAP_BOOTSTRAP_PAGE_SIZE        4096u
#define STAGE62_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX     0x40000000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_VSTART_ROUND     0x00400000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_VM_MIN_KERNEL_ADDRESS 0x80000000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_VM_MAX_KERNEL_ADDRESS 0xfffeffffu
#define STAGE62_XNU_PMAP_BOOTSTRAP_L1_ALIGNMENT     0x00004000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_SECTION_SIZE     0x00100000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_SECTION_DESC_SO  0x00010c02u
#define STAGE62_XNU_PMAP_BOOTSTRAP_ALLOC_TAG        0x414c4c43u
#define STAGE62_XNU_PMAP_BOOTSTRAP_WORKSPACE_TAG    0x504d4150u

#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_SOURCE_BOOTSTRAP 0x00000001u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_SOURCE_ROLLUPS 0x00000002u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_REFERENCE_PROVENANCE 0x00000004u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_PUBLIC_GLOBALS 0x00000008u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_TTEP_LAYOUT 0x00000010u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_AVAIL_RANGE 0x00000020u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_VIRTUAL_SPACE 0x00000040u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_SNAPSHOT_IMPORTED 0x00000080u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_SPAN 0x00000100u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_CURSOR 0x00000200u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_FIRST_ALLOC 0x00000400u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_POLICY 0x00000800u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_RANGE 0x00001000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_L1 0x00002000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_SECTIONS 0x00004000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_POLICY 0x00008000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_STAGE_OVERLAP 0x00010000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_DT_OVERLAP 0x00020000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_STAGING_OVERLAP 0x00040000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_SAFE_TABLE_OVERLAP 0x00080000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_RAM_CONSOLE_OVERLAP 0x00100000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_PMAP_COMPILE 0x00200000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_PMAP_LINK 0x00400000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_PMAP_EXEC 0x00800000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_ARM_VM_INIT_EXEC 0x01000000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_LIVE_PMAP_INSTALL 0x02000000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_XNU_JUMP 0x04000000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_MACHO_EXEC 0x08000000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PROPOSED_WRITES 0x10000000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PERSIST_CACHE 0x20000000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_FAIL_CLOSED 0x40000000u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_REQUIRED_MASK 0x7fffffffu

#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SOURCE 0x00000001u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_REFERENCE 0x00000002u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ARITHMETIC 0x00000004u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SNAPSHOT 0x00000008u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ALLOCATOR 0x00000010u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE 0x00000020u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_OVERLAP 0x00000040u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_COMPILE 0x00000080u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_LINK 0x00000100u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_EXEC 0x00000200u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_LIVE_PMAP_INSTALL 0x00000400u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_CHECKSUM 0x00000800u
#define STAGE62_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage62_xnu_pmap_bootstrap_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_bootstrap_status;
    uint32_t source_bootstrap_required_mask;
    uint32_t source_bootstrap_satisfied_mask;
    uint32_t source_bootstrap_failure_mask;
    uint32_t source_bootstrap_checksum;
    uint32_t source_snapshot_status;
    uint32_t source_snapshot_required_mask;
    uint32_t source_snapshot_satisfied_mask;
    uint32_t source_snapshot_failure_mask;
    uint32_t source_snapshot_checksum;
    uint32_t source_tte_dryrun_status;
    uint32_t source_safe_table_status;
    uint32_t source_stage_owned_tables_status;
    uint32_t source_ttbr_roundtrip_status;
    uint32_t source_ttbr_restored_status;
    uint32_t source_cache_preserved_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t pmap_reference_mask;
    uint32_t pmap_runtime_blocked_mask;
    uint32_t pmap_reference_count;
    uint32_t public_pmap_compile_count;
    uint32_t public_pmap_link_count;
    uint32_t public_pmap_execute_count;
    uint32_t arm_vm_init_reference_modeled;
    uint32_t pmap_c_reference_modeled;
    uint32_t pmap_h_reference_modeled;
    uint32_t proc_reg_h_reference_modeled;
    uint32_t vm_param_h_reference_modeled;
    uint32_t gVirtBase;
    uint32_t gPhysBase;
    uint32_t gPhysSize;
    uint32_t boot_ttep;
    uint32_t cpu_ttep;
    uint32_t initial_avail_start;
    uint32_t avail_end;
    uint32_t vstart;
    uint32_t virtual_space_end;
    uint32_t page_size;
    uint32_t mem_size_max;
    uint32_t vm_min_kernel_address;
    uint32_t vm_max_kernel_address;
    uint32_t proposed_virtBase;
    uint32_t proposed_physBase;
    uint32_t proposed_memSize;
    uint32_t proposed_topOfKernelData;
    uint32_t proposed_avail_start;
    uint32_t proposed_avail_end;
    uint32_t loaded_phys_base;
    uint32_t loaded_phys_end;
    uint32_t ttep_workspace_base;
    uint32_t ttep_workspace_limit;
    uint32_t allocator_span_base;
    uint32_t allocator_span_size;
    uint32_t allocator_span_end;
    uint32_t allocator_initial_cursor;
    uint32_t allocator_current_cursor;
    uint32_t allocator_remaining_bytes;
    uint32_t allocator_first_alloc_base;
    uint32_t allocator_first_alloc_size;
    uint32_t allocator_first_alloc_end;
    uint32_t allocator_first_alloc_tag;
    uint32_t allocator_alignment;
    uint32_t workspace_base;
    uint32_t workspace_limit;
    uint32_t workspace_size;
    uint32_t workspace_section_count;
    uint32_t workspace_l1_table_phys;
    uint32_t workspace_l1_table_virt;
    uint32_t workspace_l1_section_descriptor;
    uint32_t workspace_l1_section_size;
    uint32_t workspace_allocation_tag;
    uint32_t workspace_mmu_enabled;
    uint32_t workspace_cache_policy;
    uint32_t modeled_free_page_count;
    uint32_t stage_image_base;
    uint32_t stage_image_end;
    uint32_t device_tree_base;
    uint32_t device_tree_end;
    uint32_t staging_arena_base;
    uint32_t staging_arena_end;
    uint32_t safe_table_base;
    uint32_t safe_table_end;
    uint32_t ram_console_base;
    uint32_t ram_console_end;
    uint32_t public_arm_vm_init_executed;
    uint32_t live_pmap_tables_installed;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_phys_load_written;
    uint32_t proposed_tte_workspace_written;
    uint32_t persistent_write_attempted;
    uint32_t caches_changed;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE62_XNU_PMAP_TABLE_DRYRUN_CONTRACT_VERSION 1u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT   4096u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_L1_BYTES         0x00004000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_L1_ALIGNMENT     0x00004000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SECTION_SIZE     0x00100000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_DESC_SECTION_SO  0x00010c02u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK   0x00000003u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_SECTION 0x00000002u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_DESC_BASE_MASK   0xfff00000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK   0x000fffffu
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SECTION_OFFSET_MASK 0x000fffffu

#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_SOURCE_PMAP_BOOTSTRAP 0x00000001u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_SOURCE_SNAPSHOT 0x00000002u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000004u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_CONSTANTS 0x00000008u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_LOCAL_BUFFER 0x00000010u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_LOCAL_ZERO 0x00000020u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_LOW_MEMORY_RANGE 0x00000040u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_KERNEL_RANGE 0x00000080u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_WORKSPACE_RANGE 0x00000100u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_RAM_CONSOLE_RANGE 0x00000200u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_DESCRIPTOR_READBACK 0x00000400u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_DESCRIPTOR_ATTRS 0x00000800u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_KERNEL 0x00001000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_LOWMEM 0x00002000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_WORKSPACE 0x00004000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_RAM_CONSOLE 0x00008000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM 0x00010000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00020000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00040000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00080000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_NO_TLB_INVALIDATE 0x00100000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00200000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00400000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_NO_PERSIST_CACHE 0x00800000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_SAT_FAIL_CLOSED 0x01000000u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_REQUIRED_MASK 0x01ffffffu

#define STAGE62_XNU_PMAP_TABLE_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_FAIL_CONSTANTS 0x00000002u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_FAIL_LOCAL_BUFFER 0x00000004u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE 0x00000008u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_FAIL_DESCRIPTOR 0x00000010u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_FAIL_TRANSLATION 0x00000020u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_FAIL_CHECKSUM 0x00000040u
#define STAGE62_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage62_xnu_pmap_table_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_pmap_bootstrap_status;
    uint32_t source_pmap_bootstrap_required_mask;
    uint32_t source_pmap_bootstrap_satisfied_mask;
    uint32_t source_pmap_bootstrap_failure_mask;
    uint32_t source_pmap_bootstrap_checksum;
    uint32_t source_snapshot_status;
    uint32_t source_snapshot_required_mask;
    uint32_t source_snapshot_satisfied_mask;
    uint32_t source_snapshot_failure_mask;
    uint32_t source_snapshot_checksum;
    uint32_t source_bootstrap_status;
    uint32_t source_tte_dryrun_status;
    uint32_t source_safe_table_status;
    uint32_t source_stage_owned_tables_status;
    uint32_t source_ttbr_roundtrip_status;
    uint32_t source_ttbr_restored_status;
    uint32_t source_cache_preserved_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t page_size;
    uint32_t l1_entry_count;
    uint32_t l1_bytes;
    uint32_t l1_alignment;
    uint32_t section_size;
    uint32_t section_descriptor;
    uint32_t desc_type_mask;
    uint32_t desc_attr_mask;
    uint32_t desc_base_mask;
    uint32_t local_l1_base;
    uint32_t local_l1_limit;
    uint32_t local_l1_bytes;
    uint32_t local_l1_alignment;
    uint32_t local_l1_zero_checksum;
    uint32_t local_l1_populated_checksum;
    uint32_t local_l1_write_count;
    uint32_t gVirtBase;
    uint32_t gPhysBase;
    uint32_t gPhysSize;
    uint32_t boot_ttep;
    uint32_t cpu_ttep;
    uint32_t initial_avail_start;
    uint32_t avail_end;
    uint32_t vstart;
    uint32_t virtual_space_end;
    uint32_t lowmem_l1_first_index;
    uint32_t lowmem_l1_last_index;
    uint32_t lowmem_l1_section_count;
    uint32_t kernel_l1_first_index;
    uint32_t kernel_l1_last_index;
    uint32_t kernel_l1_section_count;
    uint32_t workspace_l1_first_index;
    uint32_t workspace_l1_last_index;
    uint32_t workspace_l1_section_count;
    uint32_t ram_console_l1_first_index;
    uint32_t ram_console_l1_last_index;
    uint32_t ram_console_l1_section_count;
    uint32_t descriptor_lowmem_first_word;
    uint32_t descriptor_lowmem_last_word;
    uint32_t descriptor_kernel_first_word;
    uint32_t descriptor_kernel_last_word;
    uint32_t descriptor_workspace_first_word;
    uint32_t descriptor_workspace_last_word;
    uint32_t descriptor_ram_console_first_word;
    uint32_t descriptor_ram_console_last_word;
    uint32_t descriptor_type_mask_seen;
    uint32_t descriptor_attr_mask_seen;
    uint32_t descriptor_expected_attr_mask;
    uint32_t translation_kernel_va;
    uint32_t translation_kernel_pa;
    uint32_t translation_kernel_expected_pa;
    uint32_t translation_lowmem_va;
    uint32_t translation_lowmem_pa;
    uint32_t translation_workspace_va;
    uint32_t translation_workspace_pa;
    uint32_t translation_ram_console_va;
    uint32_t translation_ram_console_pa;
    uint32_t translation_case_count;
    uint32_t workspace_base;
    uint32_t workspace_limit;
    uint32_t workspace_size;
    uint32_t workspace_l1_table_phys;
    uint32_t workspace_l1_table_virt;
    uint32_t workspace_l1_table_local_distinct;
    uint32_t public_pmap_compile_count;
    uint32_t public_pmap_link_count;
    uint32_t public_pmap_execute_count;
    uint32_t proposed_workspace_written;
    uint32_t live_pmap_tables_installed;
    uint32_t ttbr_written;
    uint32_t ttbcr_written;
    uint32_t dacr_written;
    uint32_t sctlr_written;
    uint32_t tlbs_invalidated;
    uint32_t public_arm_vm_init_executed;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t persistent_write_attempted;
    uint32_t caches_changed;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE62_XNU_PMAP_PAGE_DRYRUN_CONTRACT_VERSION 1u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_PAGE_SIZE        0x00001000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_PAGE_OFFSET_MASK 0x00000fffu
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_WINDOW_SIZE      0x00400000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_WINDOW_ALIGN     0x00400000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT   4096u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_BYTES         0x00004000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_ALIGNMENT     0x00004000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_TYPE    0x00000001u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_TYPE_MASK     0x00000003u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_MASK    0xfffffc00u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_ATTR_MASK 0x000003ffu
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_PAGE_BYTES    0x00001000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_ALIGNMENT     0x00001000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_COARSE_BYTES  0x00000400u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE 4u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_PTES_PER_TABLE 256u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT     1024u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_MASK    0x000ff000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_SHIFT   12u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_MASK    0x00000002u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_SMALL   0x00000002u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_PTE_PAGE_MASK    0xfffff000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_MASK    0x00000fffu
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_DEFAULT 0x00000412u

#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_SOURCE_PMAP_BOOTSTRAP 0x00000001u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_SOURCE_TABLE_DRYRUN 0x00000002u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000004u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_CONSTANTS 0x00000008u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_L1_BUFFER 0x00000010u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_L2_BUFFER 0x00000020u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_ZERO 0x00000040u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_WINDOW 0x00000080u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_L1_COARSE_DESCRIPTORS 0x00000100u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_PTE_POPULATION 0x00000200u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_PTE_READBACK 0x00000400u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_ATTRS 0x00000800u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_TRANSLATION_KERNEL 0x00001000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_TRANSLATION_WORKSPACE 0x00002000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_TRANSLATION_WINDOW 0x00004000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM 0x00008000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00010000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00020000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00040000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_NO_TLB_INVALIDATE 0x00080000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00100000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00200000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_NO_PERSIST_CACHE 0x00400000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_SAT_FAIL_CLOSED 0x00800000u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_REQUIRED_MASK 0x00ffffffu

#define STAGE62_XNU_PMAP_PAGE_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_FAIL_CONSTANTS 0x00000002u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_FAIL_LOCAL_BUFFER 0x00000004u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_FAIL_WINDOW 0x00000008u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_FAIL_DESCRIPTOR 0x00000010u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_FAIL_PTE 0x00000020u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_FAIL_TRANSLATION 0x00000040u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_FAIL_CHECKSUM 0x00000080u
#define STAGE62_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage62_xnu_pmap_page_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_pmap_bootstrap_status;
    uint32_t source_pmap_bootstrap_required_mask;
    uint32_t source_pmap_bootstrap_satisfied_mask;
    uint32_t source_pmap_bootstrap_failure_mask;
    uint32_t source_pmap_bootstrap_checksum;
    uint32_t source_table_dryrun_status;
    uint32_t source_table_dryrun_required_mask;
    uint32_t source_table_dryrun_satisfied_mask;
    uint32_t source_table_dryrun_failure_mask;
    uint32_t source_table_dryrun_checksum;
    uint32_t source_snapshot_status;
    uint32_t source_snapshot_required_mask;
    uint32_t source_snapshot_satisfied_mask;
    uint32_t source_snapshot_failure_mask;
    uint32_t source_snapshot_checksum;
    uint32_t source_bootstrap_status;
    uint32_t source_tte_dryrun_status;
    uint32_t source_ttbr_roundtrip_status;
    uint32_t source_ttbr_restored_status;
    uint32_t source_cache_preserved_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t page_size;
    uint32_t page_offset_mask;
    uint32_t window_size;
    uint32_t window_alignment;
    uint32_t l1_entry_count;
    uint32_t l1_bytes;
    uint32_t l1_alignment;
    uint32_t l1_table_descriptor_type;
    uint32_t l1_table_type_mask;
    uint32_t l1_table_base_mask;
    uint32_t l1_table_attr_mask;
    uint32_t l2_page_bytes;
    uint32_t l2_alignment;
    uint32_t l2_coarse_table_bytes;
    uint32_t l2_tables_per_page;
    uint32_t l2_ptes_per_table;
    uint32_t l2_pte_count;
    uint32_t pte_type_mask;
    uint32_t pte_type_small;
    uint32_t pte_page_mask;
    uint32_t pte_attr_mask;
    uint32_t pte_attr_default;
    uint32_t local_l1_base;
    uint32_t local_l1_limit;
    uint32_t local_l1_bytes;
    uint32_t local_l1_alignment;
    uint32_t local_l2_base;
    uint32_t local_l2_limit;
    uint32_t local_l2_bytes;
    uint32_t local_l2_alignment;
    uint32_t local_l1_zero_checksum;
    uint32_t local_l2_zero_checksum;
    uint32_t local_l1_populated_checksum;
    uint32_t local_l2_populated_checksum;
    uint32_t local_l1_write_count;
    uint32_t local_l2_pte_write_count;
    uint32_t gVirtBase;
    uint32_t gPhysBase;
    uint32_t gPhysSize;
    uint32_t boot_ttep;
    uint32_t cpu_ttep;
    uint32_t initial_avail_start;
    uint32_t avail_end;
    uint32_t vstart;
    uint32_t virtual_space_end;
    uint32_t workspace_base;
    uint32_t workspace_limit;
    uint32_t workspace_size;
    uint32_t workspace_l1_table_phys;
    uint32_t workspace_l1_table_virt;
    uint32_t page_window_virt_base;
    uint32_t page_window_virt_limit;
    uint32_t page_window_phys_base;
    uint32_t page_window_phys_limit;
    uint32_t page_window_l1_first_index;
    uint32_t page_window_l1_last_index;
    uint32_t page_window_l1_count;
    uint32_t page_window_l2_pte_first_index;
    uint32_t page_window_l2_pte_last_index;
    uint32_t page_window_l2_pte_count;
    uint32_t l1_descriptor_first_word;
    uint32_t l1_descriptor_last_word;
    uint32_t l1_descriptor_type_mask_seen;
    uint32_t l1_descriptor_attr_mask_seen;
    uint32_t l1_descriptor_expected_attr_mask;
    uint32_t pte_first_word;
    uint32_t pte_kernel_word;
    uint32_t pte_workspace_word;
    uint32_t pte_last_word;
    uint32_t pte_type_mask_seen;
    uint32_t pte_attr_mask_seen;
    uint32_t pte_expected_attr_mask;
    uint32_t translation_kernel_va;
    uint32_t translation_kernel_pa;
    uint32_t translation_kernel_expected_pa;
    uint32_t translation_workspace_va;
    uint32_t translation_workspace_pa;
    uint32_t translation_window_first_va;
    uint32_t translation_window_first_pa;
    uint32_t translation_window_last_va;
    uint32_t translation_window_last_pa;
    uint32_t translation_case_count;
    uint32_t public_pmap_compile_count;
    uint32_t public_pmap_link_count;
    uint32_t public_pmap_execute_count;
    uint32_t proposed_workspace_written;
    uint32_t live_pmap_tables_installed;
    uint32_t ttbr_written;
    uint32_t ttbcr_written;
    uint32_t dacr_written;
    uint32_t sctlr_written;
    uint32_t tlbs_invalidated;
    uint32_t public_arm_vm_init_executed;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t persistent_write_attempted;
    uint32_t caches_changed;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE62_XNU_PMAP_ATTR_DRYRUN_CONTRACT_VERSION 1u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITEBACK 0x0u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITECOMB 0x1u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITETHRU 0x2u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_CACHE_DISABLE 0x3u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_CACHE_INNERWRITEBACK 0x4u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_CACHE_POSTED STAGE62_XNU_PMAP_ATTR_DRYRUN_CACHE_DISABLE
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_CACHE_DEFAULT STAGE62_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITEBACK
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_AP_RWNA 0x0u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_AP_RWRW 0x1u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_AP_RONA 0x2u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_AP_RORO 0x3u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_GUARDED 0x01u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT 0x02u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_NOT_CACHEABLE 0x04u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_WRITE_THROUGH 0x08u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_INNER 0x10u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_EARLY_ACK 0x20u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_DEFAULT STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_COPYBACK STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_INNERWBACK (STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT | STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_INNER)
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_IO (STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT | STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_NOT_CACHEABLE | STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_GUARDED)
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_POSTED (STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_IO | STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_EARLY_ACK)
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_WTHRU (STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_WRITE_THROUGH | STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT | STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_GUARDED)
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_WCOMB (STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_NOT_CACHEABLE | STAGE62_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT)
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_PTE_TYPE 0x00000002u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_PTE_NX 0x00000001u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_PTE_AF 0x00000010u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_PTE_SH 0x00000400u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_PTE_ATTRINDXMASK 0x0000004cu
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_PTE_APMASK 0x00000220u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_PTE_TEMPLATE_MASK 0x0000067fu
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_TTE_TYPE_SECTION 0x00000002u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_TTE_TYPE_TABLE 0x00000001u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_TTE_AF 0x00000400u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_TTE_SH 0x00010000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_TTE_NX 0x00000010u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_TTE_ATTRINDXMASK 0x0000100cu
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_TTE_APMASK 0x00008800u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_TTE_SECTION_TEMPLATE_MASK 0x00019c1fu

#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_SOURCE_TABLE_DRYRUN 0x00000001u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_SOURCE_PAGE_DRYRUN 0x00000002u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000004u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_PUBLIC_CONSTANTS 0x00000008u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_CACHE_ATTRINDX_VALUES 0x00000010u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_ATTRINDX_MACROS 0x00000020u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_ACCESS_PROTECTIONS 0x00000040u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_SECTION_TEMPLATE 0x00000080u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_PAGE_TEMPLATE 0x00000100u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_WIMG_DEFAULT_COPYBACK 0x00000200u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_WIMG_DEVICE_POSTED 0x00000400u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_WIMG_OTHER_CACHE_MODES 0x00000800u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_PAGE_PROT_HELPERS 0x00001000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_PRIOR_READBACKS 0x00002000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_LOCAL_ONLY 0x00004000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00008000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00010000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00020000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00040000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_NO_TLB_INVALIDATE 0x00080000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_NO_CACHE_CHANGE 0x00100000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00200000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_NO_PERSIST_WRITE 0x00400000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_SAT_FAIL_CLOSED 0x00800000u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_REQUIRED_MASK 0x00ffffffu

#define STAGE62_XNU_PMAP_ATTR_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_FAIL_CONSTANTS 0x00000002u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_FAIL_ATTRINDX 0x00000004u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_FAIL_AP 0x00000008u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_FAIL_TEMPLATE 0x00000010u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_FAIL_WIMG 0x00000020u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_FAIL_READBACK 0x00000040u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_FAIL_CHECKSUM 0x00000080u
#define STAGE62_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage62_xnu_pmap_attr_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_table_dryrun_status;
    uint32_t source_table_dryrun_required_mask;
    uint32_t source_table_dryrun_satisfied_mask;
    uint32_t source_table_dryrun_failure_mask;
    uint32_t source_table_dryrun_checksum;
    uint32_t source_page_dryrun_status;
    uint32_t source_page_dryrun_required_mask;
    uint32_t source_page_dryrun_satisfied_mask;
    uint32_t source_page_dryrun_failure_mask;
    uint32_t source_page_dryrun_checksum;
    uint32_t source_bootstrap_status;
    uint32_t source_pmap_bootstrap_status;
    uint32_t source_cache_preserved_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t cache_attrindx_writeback;
    uint32_t cache_attrindx_writecomb;
    uint32_t cache_attrindx_writethru;
    uint32_t cache_attrindx_disable;
    uint32_t cache_attrindx_innerwriteback;
    uint32_t cache_attrindx_posted;
    uint32_t cache_attrindx_default;
    uint32_t ap_rwna;
    uint32_t ap_rwrw;
    uint32_t ap_rona;
    uint32_t ap_roro;
    uint32_t vm_wimg_default;
    uint32_t vm_wimg_copyback;
    uint32_t vm_wimg_innerwback;
    uint32_t vm_wimg_io;
    uint32_t vm_wimg_posted;
    uint32_t vm_wimg_wthru;
    uint32_t vm_wimg_wcomb;
    uint32_t pte_attr_writeback;
    uint32_t pte_attr_writecomb;
    uint32_t pte_attr_writethru;
    uint32_t pte_attr_disable;
    uint32_t pte_attr_innerwriteback;
    uint32_t pte_attr_mask;
    uint32_t pte_attr_roundtrip_mask;
    uint32_t tte_attr_writeback;
    uint32_t tte_attr_writecomb;
    uint32_t tte_attr_writethru;
    uint32_t tte_attr_disable;
    uint32_t tte_attr_innerwriteback;
    uint32_t tte_attr_mask;
    uint32_t tte_attr_roundtrip_mask;
    uint32_t pte_ap_rwna_word;
    uint32_t pte_ap_rwrw_word;
    uint32_t pte_ap_rona_word;
    uint32_t pte_ap_roro_word;
    uint32_t tte_ap_rwna_word;
    uint32_t tte_ap_rwrw_word;
    uint32_t tte_ap_rona_word;
    uint32_t tte_ap_roro_word;
    uint32_t section_template_word;
    uint32_t section_template_expected_word;
    uint32_t section_template_mask;
    uint32_t section_template_ap_seen;
    uint32_t section_template_attr_seen;
    uint32_t pte_template_rwx_word;
    uint32_t pte_template_rwnx_word;
    uint32_t pte_template_rox_word;
    uint32_t pte_template_ronx_word;
    uint32_t pte_template_expected_word;
    uint32_t pte_template_mask;
    uint32_t pte_template_ap_seen;
    uint32_t pte_template_attr_seen;
    uint32_t wimg_default_pte_bits;
    uint32_t wimg_copyback_pte_bits;
    uint32_t wimg_innerwback_pte_bits;
    uint32_t wimg_io_pte_bits;
    uint32_t wimg_posted_pte_bits;
    uint32_t wimg_wthru_pte_bits;
    uint32_t wimg_wcomb_pte_bits;
    uint32_t wimg_fallback_pte_bits;
    uint32_t public_arm_vm_page_granular_rwx;
    uint32_t public_arm_vm_page_granular_rwnx;
    uint32_t public_arm_vm_page_granular_rox;
    uint32_t public_arm_vm_page_granular_ronx;
    uint32_t prior_table_section_word;
    uint32_t prior_table_attr_seen;
    uint32_t prior_page_pte_word;
    uint32_t prior_page_attr_seen;
    uint32_t prior_page_expected_attr;
    uint32_t public_pmap_compile_count;
    uint32_t public_pmap_link_count;
    uint32_t public_pmap_execute_count;
    uint32_t public_arm_vm_init_executed;
    uint32_t public_pmap_runtime_executed;
    uint32_t proposed_workspace_written;
    uint32_t live_pmap_tables_installed;
    uint32_t ttbr_written;
    uint32_t ttbcr_written;
    uint32_t dacr_written;
    uint32_t sctlr_written;
    uint32_t tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT_VERSION 1u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT 3u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_SIZE STAGE62_XNU_PMAP_PAGE_DRYRUN_WINDOW_SIZE
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_ALIGN STAGE62_XNU_PMAP_PAGE_DRYRUN_WINDOW_ALIGN
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_SIZE STAGE62_XNU_PMAP_PAGE_DRYRUN_PAGE_SIZE
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_OFFSET_MASK STAGE62_XNU_PMAP_PAGE_DRYRUN_PAGE_OFFSET_MASK
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_BYTES STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_BYTES
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ALIGNMENT STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_ALIGNMENT
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_TYPE STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_TYPE
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TYPE_MASK STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_TYPE_MASK
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_MASK STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_MASK
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_ATTR_MASK STAGE62_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_ATTR_MASK
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_PAGE_BYTES
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_ALIGNMENT STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_ALIGNMENT
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_COARSE_BYTES STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_COARSE_BYTES
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTES_PER_TABLE STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_PTES_PER_TABLE
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_MASK STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_MASK
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_SHIFT STAGE62_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_SHIFT
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_MASK STAGE62_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_MASK
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_SMALL STAGE62_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_SMALL
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_PAGE_MASK STAGE62_XNU_PMAP_PAGE_DRYRUN_PTE_PAGE_MASK
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_ATTR_MASK STAGE62_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_MASK
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_GIC_PHYS_BASE 0xf9000000u

#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_TABLE_DRYRUN 0x00000001u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_PAGE_DRYRUN 0x00000002u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_ATTR_DRYRUN 0x00000004u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000008u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_CONSTANTS 0x00000010u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_L1_BUFFER 0x00000020u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_L2_BANK 0x00000040u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_ZERO 0x00000080u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_WINDOW_KERNEL 0x00000100u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_WINDOW_RAM_CONSOLE 0x00000200u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_WINDOW_DEVICE 0x00000400u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_L1_COARSE_DESCRIPTORS 0x00000800u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_PTE_POPULATION 0x00001000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_PTE_READBACK 0x00002000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_ATTRS 0x00004000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_KERNEL 0x00008000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_WORKSPACE 0x00010000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_RAM_CONSOLE 0x00020000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_DEVICE 0x00040000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM 0x00080000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00100000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00200000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00400000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00800000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_TLB_INVALIDATE 0x01000000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_CACHE_CHANGE 0x02000000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x04000000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_PERSIST_WRITE 0x08000000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_FAIL_CLOSED 0x10000000u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_REQUIRED_MASK 0x1fffffffu

#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_CONSTANTS 0x00000002u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_LOCAL_BUFFER 0x00000004u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_WINDOW 0x00000008u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_L1_DESCRIPTOR 0x00000010u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_PTE 0x00000020u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_ATTR 0x00000040u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_TRANSLATION 0x00000080u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_CHECKSUM 0x00000100u
#define STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage62_xnu_pmap_multiwindow_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_table_dryrun_status;
    uint32_t source_table_dryrun_required_mask;
    uint32_t source_table_dryrun_satisfied_mask;
    uint32_t source_table_dryrun_failure_mask;
    uint32_t source_table_dryrun_checksum;
    uint32_t source_page_dryrun_status;
    uint32_t source_page_dryrun_required_mask;
    uint32_t source_page_dryrun_satisfied_mask;
    uint32_t source_page_dryrun_failure_mask;
    uint32_t source_page_dryrun_checksum;
    uint32_t source_attr_dryrun_status;
    uint32_t source_attr_dryrun_required_mask;
    uint32_t source_attr_dryrun_satisfied_mask;
    uint32_t source_attr_dryrun_failure_mask;
    uint32_t source_attr_dryrun_checksum;
    uint32_t source_pmap_bootstrap_status;
    uint32_t source_snapshot_status;
    uint32_t source_bootstrap_status;
    uint32_t source_tte_dryrun_status;
    uint32_t source_ttbr_roundtrip_status;
    uint32_t source_ttbr_restored_status;
    uint32_t source_cache_preserved_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t page_size;
    uint32_t page_offset_mask;
    uint32_t window_count;
    uint32_t window_size;
    uint32_t window_alignment;
    uint32_t l1_entry_count;
    uint32_t l1_bytes;
    uint32_t l1_alignment;
    uint32_t l1_table_descriptor_type;
    uint32_t l1_table_type_mask;
    uint32_t l1_table_base_mask;
    uint32_t l1_table_attr_mask;
    uint32_t l2_page_bytes;
    uint32_t l2_alignment;
    uint32_t l2_coarse_table_bytes;
    uint32_t l2_tables_per_page;
    uint32_t l2_ptes_per_table;
    uint32_t l2_pte_count;
    uint32_t total_l2_pte_count;
    uint32_t pte_type_mask;
    uint32_t pte_type_small;
    uint32_t pte_page_mask;
    uint32_t pte_attr_mask;
    uint32_t pte_template_kernel;
    uint32_t pte_template_workspace;
    uint32_t pte_template_ram_console;
    uint32_t pte_template_device;
    uint32_t pte_expected_attr_mask;
    uint32_t local_l1_base;
    uint32_t local_l1_limit;
    uint32_t local_l1_bytes;
    uint32_t local_l1_alignment;
    uint32_t local_l2_bank_base;
    uint32_t local_l2_bank_limit;
    uint32_t local_l2_bank_bytes;
    uint32_t local_l2_bank_alignment;
    uint32_t local_l1_zero_checksum;
    uint32_t local_l2_zero_checksum;
    uint32_t local_l1_populated_checksum;
    uint32_t local_l2_populated_checksum;
    uint32_t local_l1_write_count;
    uint32_t local_l2_pte_write_count;
    uint32_t kernel_l1_write_count;
    uint32_t kernel_l2_pte_write_count;
    uint32_t ram_console_l1_write_count;
    uint32_t ram_console_l2_pte_write_count;
    uint32_t device_l1_write_count;
    uint32_t device_l2_pte_write_count;
    uint32_t gVirtBase;
    uint32_t gPhysBase;
    uint32_t gPhysSize;
    uint32_t boot_ttep;
    uint32_t cpu_ttep;
    uint32_t initial_avail_start;
    uint32_t avail_end;
    uint32_t vstart;
    uint32_t virtual_space_end;
    uint32_t workspace_base;
    uint32_t workspace_limit;
    uint32_t workspace_size;
    uint32_t workspace_l1_table_phys;
    uint32_t workspace_l1_table_virt;
    uint32_t kernel_window_virt_base;
    uint32_t kernel_window_virt_limit;
    uint32_t kernel_window_phys_base;
    uint32_t kernel_window_phys_limit;
    uint32_t kernel_window_l1_first_index;
    uint32_t kernel_window_l1_last_index;
    uint32_t kernel_window_l1_count;
    uint32_t ram_console_window_virt_base;
    uint32_t ram_console_window_virt_limit;
    uint32_t ram_console_window_phys_base;
    uint32_t ram_console_window_phys_limit;
    uint32_t ram_console_window_l1_first_index;
    uint32_t ram_console_window_l1_last_index;
    uint32_t ram_console_window_l1_count;
    uint32_t device_window_virt_base;
    uint32_t device_window_virt_limit;
    uint32_t device_window_phys_base;
    uint32_t device_window_phys_limit;
    uint32_t device_window_l1_first_index;
    uint32_t device_window_l1_last_index;
    uint32_t device_window_l1_count;
    uint32_t l1_descriptor_kernel_first_word;
    uint32_t l1_descriptor_kernel_last_word;
    uint32_t l1_descriptor_ram_console_first_word;
    uint32_t l1_descriptor_ram_console_last_word;
    uint32_t l1_descriptor_device_first_word;
    uint32_t l1_descriptor_device_last_word;
    uint32_t l1_descriptor_type_mask_seen;
    uint32_t l1_descriptor_attr_mask_seen;
    uint32_t l1_descriptor_expected_attr_mask;
    uint32_t pte_kernel_first_word;
    uint32_t pte_kernel_last_word;
    uint32_t pte_ram_console_first_word;
    uint32_t pte_ram_console_last_word;
    uint32_t pte_device_first_word;
    uint32_t pte_device_last_word;
    uint32_t pte_type_mask_seen;
    uint32_t pte_attr_mask_seen;
    uint32_t translation_kernel_va;
    uint32_t translation_kernel_pa;
    uint32_t translation_kernel_expected_pa;
    uint32_t translation_workspace_va;
    uint32_t translation_workspace_pa;
    uint32_t translation_workspace_expected_pa;
    uint32_t translation_ram_console_va;
    uint32_t translation_ram_console_pa;
    uint32_t translation_ram_console_expected_pa;
    uint32_t translation_device_va;
    uint32_t translation_device_pa;
    uint32_t translation_device_expected_pa;
    uint32_t translation_case_count;
    uint32_t prior_page_window_virt_base;
    uint32_t prior_page_window_phys_base;
    uint32_t prior_page_pte_kernel_word;
    uint32_t prior_page_pte_attr_seen;
    uint32_t prior_attr_pte_template_rwx;
    uint32_t prior_attr_pte_template_rwnx;
    uint32_t prior_attr_wimg_io_bits;
    uint32_t public_pmap_compile_count;
    uint32_t public_pmap_link_count;
    uint32_t public_pmap_execute_count;
    uint32_t public_arm_vm_init_executed;
    uint32_t public_pmap_runtime_executed;
    uint32_t proposed_workspace_written;
    uint32_t live_pmap_tables_installed;
    uint32_t ttbr_written;
    uint32_t ttbcr_written;
    uint32_t dacr_written;
    uint32_t sctlr_written;
    uint32_t tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT_VERSION 1u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L1_ENTRY_COUNT STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L1_BYTES STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_BYTES
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L1_ALIGNMENT STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ALIGNMENT
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_TYPE STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_TYPE
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L1_TYPE_MASK STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TYPE_MASK
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_MASK STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_MASK
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_ATTR_MASK STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_ATTR_MASK
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L2_BANK_BYTES (STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT * STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES)
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L2_COARSE_BYTES STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_COARSE_BYTES
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L2_PAGE_BYTES STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L2_PTE_COUNT STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L2_INDEX_MASK STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_MASK
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_L2_INDEX_SHIFT STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_SHIFT
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_PTE_TYPE_MASK STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_MASK
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_PTE_TYPE_SMALL STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_SMALL
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_PTE_PAGE_MASK STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_PAGE_MASK
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_PAGE_OFFSET_MASK STAGE62_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_OFFSET_MASK
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_TTBR0_BASE_MASK 0xffffc000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_CONTROL_CACHE_MASK STAGE62_TTBR_RT_CACHE_MASK

#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_SOURCE_MULTIWINDOW 0x00000001u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000002u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_CANDIDATE_L1_BUFFER 0x00000004u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_CANDIDATE_ZERO 0x00000008u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_DESCRIPTOR_IMPORT 0x00000010u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_KERNEL 0x00000020u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_WORKSPACE 0x00000040u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_RAM_CONSOLE 0x00000080u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_DEVICE 0x00000100u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_TTBR0_PLAN 0x00000200u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_TTBCR_PLAN 0x00000400u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_DACR_PLAN 0x00000800u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_SCTLR_PLAN 0x00001000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_RECOVERY_CONTINUITY 0x00002000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM 0x00004000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00008000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00010000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00020000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00040000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_TLB_INVALIDATE 0x00080000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_CACHE_CHANGE 0x00100000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00200000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_PERSIST_WRITE 0x00400000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_SAT_FAIL_CLOSED 0x00800000u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_REQUIRED_MASK 0x00ffffffu

#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_FAIL_LOCAL_BUFFER 0x00000002u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_FAIL_DESCRIPTOR 0x00000004u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_FAIL_TRANSLATION 0x00000008u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CONTROL_PLAN 0x00000010u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_FAIL_RECOVERY 0x00000020u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CHECKSUM 0x00000040u
#define STAGE62_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage62_xnu_pmap_transition_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_multiwindow_status;
    uint32_t source_multiwindow_required_mask;
    uint32_t source_multiwindow_satisfied_mask;
    uint32_t source_multiwindow_failure_mask;
    uint32_t source_multiwindow_checksum;
    uint32_t source_pmap_bootstrap_status;
    uint32_t source_ttbr_roundtrip_status;
    uint32_t source_ttbr_restored_status;
    uint32_t source_cache_preserved_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t l1_entry_count;
    uint32_t l1_bytes;
    uint32_t l1_alignment;
    uint32_t l1_table_descriptor_type;
    uint32_t l1_table_type_mask;
    uint32_t l1_table_base_mask;
    uint32_t l1_table_attr_mask;
    uint32_t l2_bank_base;
    uint32_t l2_bank_limit;
    uint32_t l2_bank_bytes;
    uint32_t l2_coarse_table_bytes;
    uint32_t l2_page_bytes;
    uint32_t l2_pte_count;
    uint32_t pte_type_mask;
    uint32_t pte_type_small;
    uint32_t pte_page_mask;
    uint32_t page_offset_mask;
    uint32_t candidate_l1_base;
    uint32_t candidate_l1_limit;
    uint32_t candidate_l1_bytes;
    uint32_t candidate_l1_alignment;
    uint32_t candidate_l1_zero_checksum;
    uint32_t candidate_l1_populated_checksum;
    uint32_t candidate_l1_write_count;
    uint32_t candidate_kernel_l1_first_index;
    uint32_t candidate_kernel_l1_last_index;
    uint32_t candidate_ram_console_l1_first_index;
    uint32_t candidate_ram_console_l1_last_index;
    uint32_t candidate_device_l1_first_index;
    uint32_t candidate_device_l1_last_index;
    uint32_t candidate_l1_descriptor_kernel_first_word;
    uint32_t candidate_l1_descriptor_ram_console_first_word;
    uint32_t candidate_l1_descriptor_device_first_word;
    uint32_t candidate_l1_descriptor_type_mask_seen;
    uint32_t candidate_l1_descriptor_attr_mask_seen;
    uint32_t proposed_ttbr0;
    uint32_t proposed_ttbcr;
    uint32_t proposed_dacr;
    uint32_t proposed_sctlr;
    uint32_t proposed_ttbr0_alignment;
    uint32_t proposed_sctlr_cache_bits;
    uint32_t restored_ttbr0;
    uint32_t restored_ttbcr;
    uint32_t restored_dacr;
    uint32_t restored_sctlr;
    uint32_t restored_cache_bits;
    uint32_t recovery_l1_base;
    uint32_t recovery_l1_limit;
    uint32_t recovery_l1_distinct;
    uint32_t workspace_base;
    uint32_t workspace_limit;
    uint32_t workspace_l1_table_phys;
    uint32_t workspace_l1_table_virt;
    uint32_t candidate_workspace_distinct;
    uint32_t translation_kernel_va;
    uint32_t translation_kernel_pa;
    uint32_t translation_kernel_expected_pa;
    uint32_t translation_workspace_va;
    uint32_t translation_workspace_pa;
    uint32_t translation_workspace_expected_pa;
    uint32_t translation_ram_console_va;
    uint32_t translation_ram_console_pa;
    uint32_t translation_ram_console_expected_pa;
    uint32_t translation_device_va;
    uint32_t translation_device_pa;
    uint32_t translation_device_expected_pa;
    uint32_t translation_case_count;
    uint32_t public_pmap_compile_count;
    uint32_t public_pmap_link_count;
    uint32_t public_pmap_execute_count;
    uint32_t public_arm_vm_init_executed;
    uint32_t public_pmap_runtime_executed;
    uint32_t proposed_workspace_written;
    uint32_t live_pmap_tables_installed;
    uint32_t ttbr_written;
    uint32_t ttbcr_written;
    uint32_t dacr_written;
    uint32_t sctlr_written;
    uint32_t tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t local_only;
    uint32_t candidate_not_installed;
    uint32_t control_register_plan_readonly;
    uint32_t tlb_plan_readonly;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE62_LOADER_SAT_BASELINE               0x00000001u
#define STAGE62_LOADER_SAT_MACHO                  0x00000002u
#define STAGE62_LOADER_SAT_BOOT_ARGS              0x00000004u
#define STAGE62_LOADER_SAT_WORKSPACE              0x00000008u
#define STAGE62_LOADER_SAT_DT                     0x00000010u
#define STAGE62_LOADER_SAT_PLATFORM_GAPS          0x00000020u
#define STAGE62_LOADER_SAT_INTERRUPTS             0x00000040u
#define STAGE62_LOADER_SAT_SAFETY                 0x00000080u
#define STAGE62_LOADER_SAT_MATERIALIZED           0x00000100u
#define STAGE62_LOADER_SAT_TTE_DRYRUN             0x00000200u
#define STAGE62_LOADER_SAT_TTE_VERIFY             0x00000400u
#define STAGE62_LOADER_SAT_VTOP_DRYRUN            0x00000800u
#define STAGE62_LOADER_SAT_HIGHVA_DRYRUN          0x00001000u
#define STAGE62_LOADER_SAT_SAFE_TABLE             0x00002000u
#define STAGE62_LOADER_SAT_STAGE_OWNED_TABLES     0x00004000u
#define STAGE62_LOADER_SAT_TTBR_ROUNDTRIP         0x00008000u
#define STAGE62_LOADER_SAT_STAGE_OWNED_TTBR       0x00010000u
#define STAGE62_LOADER_SAT_TTBR_RESTORED          0x00020000u
#define STAGE62_LOADER_SAT_CACHE_PRESERVED        0x00040000u
#define STAGE62_LOADER_SAT_XNU_WORKSPACE          0x00080000u
#define STAGE62_LOADER_SAT_CANCRO_TARGET          0x00100000u
#define STAGE62_LOADER_SAT_STAGE62_PLAN           0x00200000u
#define STAGE62_LOADER_SAT_XNU_OBJECT_SUBSET      0x00400000u
#define STAGE62_LOADER_SAT_XNU_LINK               0x00800000u
#define STAGE62_LOADER_SAT_XNU_COMPILE_GRAPH      0x01000000u
#define STAGE62_LOADER_SAT_XNU_BOOTSTRAP_CONTRACT 0x02000000u
#define STAGE62_LOADER_SAT_XNU_PMAP_BOOTSTRAP_CONTRACT 0x04000000u
#define STAGE62_LOADER_SAT_XNU_PMAP_TABLE_DRYRUN_CONTRACT 0x08000000u
#define STAGE62_LOADER_SAT_XNU_PMAP_PAGE_DRYRUN_CONTRACT 0x10000000u
#define STAGE62_LOADER_SAT_XNU_PMAP_ATTR_DRYRUN_CONTRACT 0x20000000u
#define STAGE62_LOADER_SAT_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT 0x40000000u
#define STAGE62_LOADER_SAT_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT 0x80000000u
#define STAGE62_LOADER_SAT_REQUIRED               (STAGE62_LOADER_SAT_BASELINE | \
                                                   STAGE62_LOADER_SAT_MACHO | \
                                                   STAGE62_LOADER_SAT_BOOT_ARGS | \
                                                   STAGE62_LOADER_SAT_WORKSPACE | \
                                                   STAGE62_LOADER_SAT_DT | \
                                                   STAGE62_LOADER_SAT_PLATFORM_GAPS | \
                                                   STAGE62_LOADER_SAT_INTERRUPTS | \
                                                   STAGE62_LOADER_SAT_SAFETY | \
                                                   STAGE62_LOADER_SAT_MATERIALIZED | \
                                                   STAGE62_LOADER_SAT_TTE_DRYRUN | \
                                                   STAGE62_LOADER_SAT_TTE_VERIFY | \
                                                   STAGE62_LOADER_SAT_VTOP_DRYRUN | \
                                                   STAGE62_LOADER_SAT_HIGHVA_DRYRUN | \
                                                   STAGE62_LOADER_SAT_SAFE_TABLE | \
                                                   STAGE62_LOADER_SAT_STAGE_OWNED_TABLES | \
                                                   STAGE62_LOADER_SAT_TTBR_ROUNDTRIP | \
                                                   STAGE62_LOADER_SAT_STAGE_OWNED_TTBR | \
                                                   STAGE62_LOADER_SAT_TTBR_RESTORED | \
                                                   STAGE62_LOADER_SAT_CACHE_PRESERVED | \
                                                   STAGE62_LOADER_SAT_XNU_WORKSPACE | \
                                                   STAGE62_LOADER_SAT_CANCRO_TARGET | \
                                                   STAGE62_LOADER_SAT_STAGE62_PLAN | \
                                                   STAGE62_LOADER_SAT_XNU_COMPILE_GRAPH | \
                                                   STAGE62_LOADER_SAT_XNU_OBJECT_SUBSET | \
                                                   STAGE62_LOADER_SAT_XNU_LINK | \
                                                   STAGE62_LOADER_SAT_XNU_BOOTSTRAP_CONTRACT | \
                                                   STAGE62_LOADER_SAT_XNU_PMAP_BOOTSTRAP_CONTRACT | \
                                                   STAGE62_LOADER_SAT_XNU_PMAP_TABLE_DRYRUN_CONTRACT | \
                                                   STAGE62_LOADER_SAT_XNU_PMAP_PAGE_DRYRUN_CONTRACT | \
                                                   STAGE62_LOADER_SAT_XNU_PMAP_ATTR_DRYRUN_CONTRACT | \
                                                   STAGE62_LOADER_SAT_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT | \
                                                   STAGE62_LOADER_SAT_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT)

struct stage62_loader_preflight {
    uint32_t version;
    uint32_t size;
    uint32_t xnu_baseline_tag;
    uint32_t xnu_baseline_commit;
    uint32_t xnu_master_version;
    uint32_t xnu_arm_reference;
    struct stage62_macho_probe_result macho;
    struct stage62_macho_staging_plan staging;
    uint32_t materialized_status;
    uint32_t materialized_file_bytes;
    uint32_t materialized_zero_bytes;
    struct stage62_xnu_tte_dryrun tte_dryrun;
    uint32_t tte_dryrun_status;
    uint32_t tte_dryrun_satisfied_mask;
    uint32_t tte_dryrun_failure_mask;
    uint32_t tte_dryrun_checksum;
    uint32_t tte_verify_status;
    uint32_t vtop_dryrun_status;
    uint32_t highva_dryrun_status;
    uint32_t safe_table_status;
    uint32_t stage_owned_tables_status;
    uint32_t ttbr_roundtrip_status;
    uint32_t ttbr_roundtrip_satisfied_mask;
    uint32_t ttbr_roundtrip_failure_mask;
    uint32_t ttbr_roundtrip_checksum;
    uint32_t stage_owned_ttbr_status;
    uint32_t ttbr_restored_status;
    uint32_t cache_preserved_status;
    struct stage62_xnu_workspace xnu_workspace;
    uint32_t xnu_workspace_status;
    uint32_t xnu_workspace_satisfied_mask;
    uint32_t xnu_workspace_failure_mask;
    uint32_t xnu_workspace_checksum;
    uint32_t cancro_target_status;
    uint32_t stage62_plan_status;
    struct stage62_xnu_compile_graph xnu_compile_graph;
    uint32_t xnu_compile_graph_status;
    uint32_t xnu_compile_graph_satisfied_mask;
    uint32_t xnu_compile_graph_failure_mask;
    uint32_t xnu_compile_graph_checksum;
    uint32_t xnu_compile_graph_status_rollup;
    struct stage62_xnu_object_subset xnu_object_subset;
    uint32_t xnu_object_subset_status;
    uint32_t xnu_object_subset_satisfied_mask;
    uint32_t xnu_object_subset_failure_mask;
    uint32_t xnu_object_subset_checksum;
    uint32_t xnu_object_subset_status_rollup;
    struct stage62_xnu_link xnu_link;
    uint32_t xnu_link_status;
    uint32_t xnu_link_satisfied_mask;
    uint32_t xnu_link_failure_mask;
    uint32_t xnu_link_checksum;
    uint32_t xnu_link_status_rollup;
    struct stage62_xnu_bootstrap_contract xnu_bootstrap_contract;
    uint32_t xnu_bootstrap_contract_status;
    uint32_t xnu_bootstrap_contract_satisfied_mask;
    uint32_t xnu_bootstrap_contract_failure_mask;
    uint32_t xnu_bootstrap_contract_checksum;
    uint32_t xnu_bootstrap_contract_status_rollup;
    struct stage62_xnu_pmap_bootstrap_contract xnu_pmap_bootstrap_contract;
    uint32_t xnu_pmap_bootstrap_contract_status;
    uint32_t xnu_pmap_bootstrap_contract_satisfied_mask;
    uint32_t xnu_pmap_bootstrap_contract_failure_mask;
    uint32_t xnu_pmap_bootstrap_contract_checksum;
    uint32_t xnu_pmap_bootstrap_contract_status_rollup;
    struct stage62_xnu_pmap_table_dryrun_contract xnu_pmap_table_dryrun_contract;
    uint32_t xnu_pmap_table_dryrun_contract_status;
    uint32_t xnu_pmap_table_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_table_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_table_dryrun_contract_checksum;
    uint32_t xnu_pmap_table_dryrun_contract_status_rollup;
    struct stage62_xnu_pmap_page_dryrun_contract xnu_pmap_page_dryrun_contract;
    uint32_t xnu_pmap_page_dryrun_contract_status;
    uint32_t xnu_pmap_page_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_page_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_page_dryrun_contract_checksum;
    uint32_t xnu_pmap_page_dryrun_contract_status_rollup;
    struct stage62_xnu_pmap_attr_dryrun_contract xnu_pmap_attr_dryrun_contract;
    uint32_t xnu_pmap_attr_dryrun_contract_status;
    uint32_t xnu_pmap_attr_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_attr_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_attr_dryrun_contract_checksum;
    uint32_t xnu_pmap_attr_dryrun_contract_status_rollup;
    struct stage62_xnu_pmap_multiwindow_dryrun_contract xnu_pmap_multiwindow_dryrun_contract;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_status;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_checksum;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_status_rollup;
    struct stage62_xnu_pmap_transition_dryrun_contract xnu_pmap_transition_dryrun_contract;
    uint32_t xnu_pmap_transition_dryrun_contract_status;
    uint32_t xnu_pmap_transition_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_transition_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_transition_dryrun_contract_checksum;
    uint32_t xnu_pmap_transition_dryrun_contract_status_rollup;
    uint32_t boot_args_rev_ver;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t actual_virtBase;
    uint32_t actual_physBase;
    uint32_t actual_memSize;
    uint32_t actual_topOfKernelData;
    uint32_t proposed_virtBase;
    uint32_t proposed_physBase;
    uint32_t proposed_memSize;
    uint32_t proposed_loaded_phys_base;
    uint32_t proposed_loaded_phys_end;
    uint32_t proposed_loaded_file_end;
    uint32_t proposed_topOfKernelData;
    uint32_t proposed_ttep_workspace_base;
    uint32_t proposed_ttep_workspace_size;
    uint32_t proposed_ttep_workspace_limit;
    uint32_t proposed_avail_start;
    uint32_t proposed_workspace_alignment;
    uint32_t proposed_workspace_range_ok;
    uint32_t apple_dt_semantic_mask;
    uint32_t pexpert_gap_mask;
    uint32_t platform_gap_mask;
    uint32_t interrupt_ready_mask;
    uint32_t safety_mask;
    uint32_t readiness_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t checksum;
    uint32_t status;
};

/* Android persistent_ram_buffer header. */
struct persistent_ram_buffer {
    uint32_t sig;
    uint32_t start;
    uint32_t size;
    uint8_t data[];
};

/* runtime.c */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
uint32_t align4(uint32_t v);

/* ram_console.c */
void log_init(void);
void log_puts(const char *s);
void log_hex32(uint32_t v);
void log_kv32(const char *key, uint32_t value);
void log_nl(void);

/* apple_dt.c */
struct apple_dt_builder {
    uint8_t *base;
    uint32_t capacity;
    uint32_t pos;
    uint32_t error;
};

void apple_dt_begin(struct apple_dt_builder *b, void *buf, uint32_t cap);
void apple_dt_node_begin(struct apple_dt_builder *b, uint32_t nprops, uint32_t nchildren);
void apple_dt_prop(struct apple_dt_builder *b, const char *name, const void *value, uint32_t len);
void apple_dt_prop_str(struct apple_dt_builder *b, const char *name, const char *value);
void apple_dt_prop_u32(struct apple_dt_builder *b, const char *name, uint32_t value);
void apple_dt_prop_u32_array(struct apple_dt_builder *b, const char *name, const uint32_t *values, uint32_t count);
uint32_t apple_dt_finish(struct apple_dt_builder *b);
int apple_dt_selftest_and_log(const void *dt, uint32_t len);
const void *apple_dt_find_child(const void *dt, uint32_t len, const void *node, const char *name);
const void *apple_dt_get_prop(const void *dt, uint32_t len, const void *node, const char *name, uint32_t *prop_len);
uint32_t apple_dt_node_child_count(const void *dt, uint32_t len, const void *node);
uint32_t apple_dt_node_prop_count(const void *dt, uint32_t len, const void *node);
uint32_t apple_dt_get_u32_prop(const void *dt, uint32_t len, const void *node, const char *name, uint32_t fallback);

/* boot_args.c */
void build_boot_args(struct boot_args *args, void *dt, uint32_t dt_len);

/* XNU-adjacent PE_state-like platform state */
struct pe_platform_state {
    struct boot_args *bootArgs;
    void *deviceTreeHead;
    uint32_t deviceTreeLength;
    uint32_t memoryBase;
    uint32_t memorySize;
    uint32_t cpuCount;
    uint32_t gicDistributorBase;
    uint32_t gicCpuBase;
    uint32_t timerBase;
    uint32_t timerFrequency;
    uint32_t machineType;
    uint32_t vectorBase;
};

extern struct pe_platform_state PE_state_stage62;
void pe_state_init_from_boot_args(struct boot_args *args);
void pe_state_log(void);
int pe_state_validate(void);

/* MSM8974 GIC state plus controlled SGI and timer IRQ tests */
struct gic_state_snapshot {
    uint32_t distBase;
    uint32_t cpuBase;
    uint32_t distCtlr;
    uint32_t distTyper;
    uint32_t distIidr;
    uint32_t cpuIidr;
    uint32_t cpuCtlr;
    uint32_t cpuPmr;
    uint32_t cpuBpr;
    uint32_t irqCount;
    uint32_t cpuInterfaceCount;
    uint32_t isenabler0;
    uint32_t ispendr0;
    uint32_t priority0;
    uint32_t targets0;
};

extern struct gic_state_snapshot GIC_state_stage62;
extern const uint8_t stage62_embedded_macho[];
extern const uint32_t stage62_embedded_macho_size;
int stage62_macho_probe(const void *artifact, uint32_t artifact_size,
                        struct stage62_macho_probe_result *result);
int stage62_loader_preflight_run(struct boot_args *args);
int stage62_xnu_bootstrap_contract_selftest(const struct stage62_loader_preflight *preflight);
const struct stage62_xnu_bootstrap_contract *stage62_xnu_bootstrap_contract_result(void);
int stage62_xnu_pmap_bootstrap_contract_selftest(const struct stage62_loader_preflight *preflight);
const struct stage62_xnu_pmap_bootstrap_contract *stage62_xnu_pmap_bootstrap_contract_result(void);
int stage62_xnu_pmap_table_dryrun_contract_selftest(const struct stage62_loader_preflight *preflight);
const struct stage62_xnu_pmap_table_dryrun_contract *stage62_xnu_pmap_table_dryrun_contract_result(void);
int stage62_xnu_pmap_page_dryrun_contract_selftest(const struct stage62_loader_preflight *preflight);
const struct stage62_xnu_pmap_page_dryrun_contract *stage62_xnu_pmap_page_dryrun_contract_result(void);
int stage62_xnu_pmap_attr_dryrun_contract_selftest(const struct stage62_loader_preflight *preflight);
const struct stage62_xnu_pmap_attr_dryrun_contract *stage62_xnu_pmap_attr_dryrun_contract_result(void);
int stage62_xnu_pmap_multiwindow_dryrun_contract_selftest(const struct stage62_loader_preflight *preflight);
const struct stage62_xnu_pmap_multiwindow_dryrun_contract *stage62_xnu_pmap_multiwindow_dryrun_contract_result(void);
int stage62_xnu_pmap_transition_dryrun_contract_selftest(const struct stage62_loader_preflight *preflight);
const struct stage62_xnu_pmap_transition_dryrun_contract *stage62_xnu_pmap_transition_dryrun_contract_result(void);
extern volatile uint32_t stage62_irq_count;
extern volatile uint32_t stage62_last_iar;
extern volatile uint32_t stage62_last_irq_id;
extern volatile uint32_t stage62_timer_irq_count;
extern volatile uint32_t stage62_last_timer_irq_id;
void gic_readonly_snapshot(uint32_t dist_base, uint32_t cpu_base);
void gic_log_snapshot(void);
int gic_validate_snapshot(void);
void stage62_irq_c_handler(void);
int gic_sgi_selftest(void);
int gic_timer_selftest(void);

/* XNU-adjacent kernel skeleton */
int kernel_entry(struct boot_args *args);
int pexpert_discover_and_log(struct boot_args *args);
void ml_init_timebase(void);
uint64_t ml_get_timebase(void);
uint32_t ml_get_timebase_frequency(void);
void xnu_log_puts(const char *s);
void xnu_log_kv32(const char *key, uint32_t value);
void xnu_log_kv64(const char *key, uint64_t value);

/* probes.c */
void run_readonly_hardware_probes(void);

/* timebase.c */
void timebase_init(void);
uint64_t timebase_ticks(void);
uint32_t timebase_freq_hz(void);
uint32_t timebase_elapsed_us(uint64_t start, uint64_t end);
void delay_us(uint32_t usec);
void run_timebase_selftest(void);

/* mmu.c */
int mmu_identity_selftest(void);
int mmu_high_alias_selftest(void);
int mmu_high_call_selftest(void);
int mmu_high_bootstrap_selftest(void);
int mmu_stage62_ttbr0_roundtrip_selftest(void);
const struct stage62_ttbr0_roundtrip *mmu_stage62_ttbr0_roundtrip_result(void);
const struct stage62_pmap_bootstrap_snapshot *mmu_stage62_pmap_bootstrap_snapshot_result(void);

/* stage62_main.c */
void stage62_main(void) __attribute__((noreturn));
void platform_reboot(void) __attribute__((noreturn));
int test_kernel_entry(struct boot_args *args);

extern uint8_t __stage62_image_end[];
extern uint8_t stage62_vectors[];
void trigger_stage62_undef_test(void) __attribute__((noreturn));
void trigger_stage62_data_abort_test(void) __attribute__((noreturn));

#endif
