#ifndef MI4IOS6_STAGE84_H
#define MI4IOS6_STAGE84_H

#include <stdint.h>
#include <stddef.h>

#include "xnu_workspace.h"
#include "xnu_object_subset.h"
#include "xnu_compile_graph.h"
#include "xnu_link.h"

#define STAGE84_BASE          0x00008000u
#define RAM_CONSOLE_BASE     0xde500000u
#define RAM_CONSOLE_SIZE     0x00200000u
#define RAM_CONSOLE_SIG      0x43474244u /* 'DBGC' */
#define MSM8974_PSHOLD       0xfc4ab000u
#define MSM_IMEM_BASE_PHYS   0x0fa00000u
#define RESTART_REASON       (MSM_IMEM_BASE_PHYS + 0x65cu)
#define RESTART_NORMAL       0x78665501u

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

#define STAGE84_STATUS_OK                         0x84000001u
#define STAGE84_STATUS_BASE                       0x84000000u
#define STAGE84_STATUS_FAIL_FLAG                  0x40000000u
#define STAGE84_STATUS_FAIL(mask)                  (STAGE84_STATUS_BASE | STAGE84_STATUS_FAIL_FLAG | ((uint32_t)(mask) & 0x3fffffffu))

#define STAGE84_MACHO_PROBE_VERSION               1u
#define STAGE84_MACHO_MAGIC                       0xfeedfaceu
#define STAGE84_MACHO_CIGAM                       0xcefaedfeu
#define STAGE84_MACHO_CPU_TYPE_ARM                12u
#define STAGE84_MACHO_CPU_SUBTYPE_ARM_ALL         0u
#define STAGE84_MACHO_CPU_SUBTYPE_ARM_V7          9u
#define STAGE84_MACHO_FILETYPE_EXECUTE            2u
#define STAGE84_MACHO_FILETYPE_PRELOAD            5u
#define STAGE84_MACHO_LC_SEGMENT                  1u
#define STAGE84_MACHO_LC_SYMTAB                   2u
#define STAGE84_MACHO_LC_UNIXTHREAD               5u
#define STAGE84_MACHO_LC_MAIN                     0x80000028u

#define STAGE84_MACHO_CMD_SEGMENT                 0x00000001u
#define STAGE84_MACHO_CMD_SYMTAB                  0x00000002u
#define STAGE84_MACHO_CMD_UNIXTHREAD              0x00000004u
#define STAGE84_MACHO_CMD_MAIN                    0x00000008u
#define STAGE84_MACHO_CMD_UNKNOWN                 0x84000000u

#define STAGE84_MACHO_SEG_TEXT                    0x00000001u
#define STAGE84_MACHO_SEG_DATA                    0x00000002u
#define STAGE84_MACHO_SEG_LINKEDIT                0x00000004u
#define STAGE84_MACHO_SEG_KLD                     0x00000008u
#define STAGE84_MACHO_SEG_LAST                    0x00000010u
#define STAGE84_MACHO_SEG_PRELINK_TEXT            0x00000020u
#define STAGE84_MACHO_SEG_PRELINK_INFO            0x00000040u
#define STAGE84_MACHO_SEG_PRELINK_STATE           0x00000080u
#define STAGE84_MACHO_SEG_PRELINK                 0x00000100u
#define STAGE84_MACHO_SEG_PRELINK_MASK            (STAGE84_MACHO_SEG_PRELINK_TEXT | \
                                                   STAGE84_MACHO_SEG_PRELINK_INFO | \
                                                   STAGE84_MACHO_SEG_PRELINK_STATE | \
                                                   STAGE84_MACHO_SEG_PRELINK)
#define STAGE84_MACHO_SEG_REQUIRED                (STAGE84_MACHO_SEG_TEXT | \
                                                   STAGE84_MACHO_SEG_DATA | \
                                                   STAGE84_MACHO_SEG_LINKEDIT)

#define STAGE84_MACHO_ENTRY_UNIXTHREAD            0x00000001u
#define STAGE84_MACHO_ENTRY_MAIN                  0x00000002u
#define STAGE84_MACHO_ENTRY_NOT_EXECUTED          0x84000000u

#define STAGE84_MACHO_SECT_TEXT_TEXT              0x00000001u
#define STAGE84_MACHO_SECT_DATA_CONST             0x00000002u
#define STAGE84_MACHO_SECT_PRELINK_TEXT_TEXT      0x00000004u
#define STAGE84_MACHO_SECT_PRELINK_INFO_INFO      0x00000008u
#define STAGE84_MACHO_SECT_PRELINK_INFO_KERNEL    0x00000010u
#define STAGE84_MACHO_SECT_PRELINK_INFO_KEXTS     0x00000020u
#define STAGE84_MACHO_SECT_PRELINK_STATE_KERNEL   0x00000040u
#define STAGE84_MACHO_SECT_PRELINK_STATE_KEXTS    0x00000080u
#define STAGE84_MACHO_SECT_PRELINK_SYMTAB         0x00000100u
#define STAGE84_MACHO_SECT_PRELINK_MASK           (STAGE84_MACHO_SECT_PRELINK_TEXT_TEXT | \
                                                   STAGE84_MACHO_SECT_PRELINK_INFO_INFO | \
                                                   STAGE84_MACHO_SECT_PRELINK_INFO_KERNEL | \
                                                   STAGE84_MACHO_SECT_PRELINK_INFO_KEXTS | \
                                                   STAGE84_MACHO_SECT_PRELINK_STATE_KERNEL | \
                                                   STAGE84_MACHO_SECT_PRELINK_STATE_KEXTS | \
                                                   STAGE84_MACHO_SECT_PRELINK_SYMTAB)
#define STAGE84_MACHO_SECT_REQUIRED               (STAGE84_MACHO_SECT_TEXT_TEXT | \
                                                   STAGE84_MACHO_SECT_DATA_CONST)

#define STAGE84_MACHO_VALID_MAGIC                 0x00000001u
#define STAGE84_MACHO_VALID_ENDIAN                0x00000002u
#define STAGE84_MACHO_VALID_CPU                   0x00000004u
#define STAGE84_MACHO_VALID_SUBTYPE               0x00000008u
#define STAGE84_MACHO_VALID_FILETYPE              0x00000010u
#define STAGE84_MACHO_VALID_COMMAND_BOUNDS        0x00000020u
#define STAGE84_MACHO_VALID_SEGMENT_COMMAND       0x00000040u
#define STAGE84_MACHO_VALID_REQUIRED_SEGMENTS     0x00000080u
#define STAGE84_MACHO_VALID_FILE_EXTENTS          0x00000100u
#define STAGE84_MACHO_VALID_ENTRY_NOT_EXECUTED    0x00000200u
#define STAGE84_MACHO_VALID_SECTION_BOUNDS        0x00000400u
#define STAGE84_MACHO_VALID_REQUIRED_SECTIONS     0x00000800u
#define STAGE84_MACHO_VALID_LOAD_PLAN             0x00001000u
#define STAGE84_MACHO_VALID_PRELINK_REPORT        0x00002000u
#define STAGE84_MACHO_VALID_STAGING               0x00004000u
#define STAGE84_MACHO_VALID_MATERIALIZED_PARSE    0x00008000u
#define STAGE84_MACHO_VALID_MARKERS               0x00010000u
#define STAGE84_MACHO_VALID_ZERO_FILL             0x00020000u
#define STAGE84_MACHO_VALID_REQUIRED              (STAGE84_MACHO_VALID_MAGIC | \
                                                   STAGE84_MACHO_VALID_ENDIAN | \
                                                   STAGE84_MACHO_VALID_CPU | \
                                                   STAGE84_MACHO_VALID_SUBTYPE | \
                                                   STAGE84_MACHO_VALID_FILETYPE | \
                                                   STAGE84_MACHO_VALID_COMMAND_BOUNDS | \
                                                   STAGE84_MACHO_VALID_SEGMENT_COMMAND | \
                                                   STAGE84_MACHO_VALID_REQUIRED_SEGMENTS | \
                                                   STAGE84_MACHO_VALID_FILE_EXTENTS | \
                                                   STAGE84_MACHO_VALID_ENTRY_NOT_EXECUTED | \
                                                   STAGE84_MACHO_VALID_SECTION_BOUNDS | \
                                                   STAGE84_MACHO_VALID_REQUIRED_SECTIONS | \
                                                   STAGE84_MACHO_VALID_LOAD_PLAN | \
                                                   STAGE84_MACHO_VALID_STAGING | \
                                                   STAGE84_MACHO_VALID_MATERIALIZED_PARSE | \
                                                   STAGE84_MACHO_VALID_MARKERS | \
                                                   STAGE84_MACHO_VALID_ZERO_FILL)

#define STAGE84_MACHO_FAIL_NULL                   0x00000001u
#define STAGE84_MACHO_FAIL_SIZE                   0x00000002u
#define STAGE84_MACHO_FAIL_MAGIC                  0x00000004u
#define STAGE84_MACHO_FAIL_ENDIAN                 0x00000008u
#define STAGE84_MACHO_FAIL_CPU                    0x00000010u
#define STAGE84_MACHO_FAIL_SUBTYPE                0x00000020u
#define STAGE84_MACHO_FAIL_FILETYPE               0x00000040u
#define STAGE84_MACHO_FAIL_COMMAND_BOUNDS         0x00000080u
#define STAGE84_MACHO_FAIL_COMMAND_SIZE           0x00000100u
#define STAGE84_MACHO_FAIL_SEGMENT_BOUNDS         0x00000200u
#define STAGE84_MACHO_FAIL_SEGMENT_EXTENT         0x00000400u
#define STAGE84_MACHO_FAIL_REQUIRED_SEGMENTS      0x00000800u
#define STAGE84_MACHO_FAIL_NO_ENTRY_METADATA      0x00001000u
#define STAGE84_MACHO_FAIL_SECTION_BOUNDS         0x00002000u
#define STAGE84_MACHO_FAIL_REQUIRED_SECTIONS      0x00004000u
#define STAGE84_MACHO_FAIL_LOAD_PLAN              0x00008000u
#define STAGE84_MACHO_FAIL_PHYS_RANGE             0x00010000u
#define STAGE84_MACHO_FAIL_LOAD_PLAN_FULL         0x00020000u
#define STAGE84_MACHO_FAIL_STAGE_SPAN             0x00040000u
#define STAGE84_MACHO_FAIL_STAGE_PLAN_FULL        0x00080000u
#define STAGE84_MACHO_FAIL_STAGE_SRC_BOUNDS       0x00100000u
#define STAGE84_MACHO_FAIL_STAGE_DST_BOUNDS       0x00200000u
#define STAGE84_MACHO_FAIL_STAGE_COPY             0x00400000u
#define STAGE84_MACHO_FAIL_STAGE_REPARSE          0x00800000u
#define STAGE84_MACHO_FAIL_STAGE_MARKERS          0x01000000u
#define STAGE84_MACHO_FAIL_STAGE_ZERO_FILL        0x02000000u
#define STAGE84_MACHO_FAIL_STAGE_ARENA_ALIAS      0x04000000u

#define STAGE84_MACHO_MARKER_TEXT                 0x00000001u
#define STAGE84_MACHO_MARKER_DATA                 0x00000002u
#define STAGE84_MACHO_MARKER_PRELINK_TEXT         0x00000004u
#define STAGE84_MACHO_MARKER_REQUIRED             (STAGE84_MACHO_MARKER_TEXT | \
                                                   STAGE84_MACHO_MARKER_DATA | \
                                                   STAGE84_MACHO_MARKER_PRELINK_TEXT)

#define STAGE84_MACHO_LOAD_PLAN_VERSION           1u
#define STAGE84_MACHO_LOAD_PLAN_MAX               8u
#define STAGE84_MACHO_LOAD_PLAN_FILE_BACKED       0x00000001u
#define STAGE84_MACHO_LOAD_PLAN_PRELINK           0x00000002u
#define STAGE84_MACHO_LOAD_PLAN_ZERO_FILL         0x00000004u

#define STAGE84_MACHO_STAGING_ARENA_CAP           0x00010000u
#define STAGE84_MACHO_STAGING_ARENA_ALIGN         4096u
#define STAGE84_MACHO_STAGING_PLAN_VERSION        1u
#define STAGE84_MACHO_STAGING_PLAN_MAX            STAGE84_MACHO_LOAD_PLAN_MAX

struct stage84_macho_load_plan_entry {
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

struct stage84_macho_load_plan {
    uint32_t version;
    uint32_t entry_count;
    uint32_t vm_base;
    uint32_t vm_end;
    uint32_t phys_base;
    uint32_t phys_end;
    uint32_t file_end;
    uint32_t status;
    struct stage84_macho_load_plan_entry entries[STAGE84_MACHO_LOAD_PLAN_MAX];
};

struct stage84_macho_staging_plan_entry {
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

struct stage84_macho_staging_plan {
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
    struct stage84_macho_staging_plan_entry entries[STAGE84_MACHO_STAGING_PLAN_MAX];
};

struct stage84_macho_probe_result {
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
    struct stage84_macho_load_plan load_plan;
    struct stage84_macho_staging_plan staging_plan;
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

#define STAGE84_LOADER_PREFLIGHT_VERSION          1u
#define STAGE84_XNU_BASELINE_2050_22_13           0x20502213u
#define STAGE84_XNU_BASELINE_COMMIT_CC8A9B0C      0xcc8a9b0cu
#define STAGE84_XNU_BASELINE_MASTER_12_3_0        0x000c0300u
#define STAGE84_XNU_ARM_REFERENCE_4570_1_46       0x45700146u

#define STAGE84_XNU_TTE_WORKSPACE_PAGES           10u
#define STAGE84_XNU_TTE_WORKSPACE_BYTES           (STAGE84_XNU_TTE_WORKSPACE_PAGES * 4096u)
#define STAGE84_XNU_TTE_ALIGNMENT                 0x00004000u
#define STAGE84_XNU_TTE_DRYRUN_VERSION            1u
#define STAGE84_XNU_TTE_PAGE_SIZE                 4096u
#define STAGE84_XNU_TTE_L1_ALIGN                  0x00004000u
#define STAGE84_XNU_TTE_L1_SIZE                   0x00004000u
#define STAGE84_XNU_TTE_L1_ENTRY_COUNT            4096u
#define STAGE84_XNU_TTE_L1_SECTION_SIZE           0x00100000u
#define STAGE84_XNU_TTE_L2_ALIGN                  0x00000400u
#define STAGE84_XNU_TTE_L2_SIZE                   0x00001000u
#define STAGE84_XNU_TTE_DESC_SECTION_SO           0x00010c02u
#define STAGE84_XNU_TTE_DESC_TYPE_MASK            0x00000003u
#define STAGE84_XNU_TTE_DESC_TYPE_SECTION         0x00000002u
#define STAGE84_XNU_TTE_DESC_BASE_MASK            0xfff00000u
#define STAGE84_XNU_TTE_DESC_ATTR_MASK            0x000fffffu
#define STAGE84_XNU_TTE_SECTION_OFFSET_MASK       0x000fffffu

#define STAGE84_XNU_SAFE_TABLE_VERSION             1u
#define STAGE84_XNU_SAFE_TABLE_ALIGNMENT           STAGE84_XNU_TTE_L1_ALIGN
#define STAGE84_XNU_SAFE_TABLE_L1_BYTES            STAGE84_XNU_TTE_L1_SIZE
#define STAGE84_XNU_SAFE_TABLE_L1_COUNT            STAGE84_XNU_TTE_L1_ENTRY_COUNT
#define STAGE84_XNU_SAFE_TABLE_KIND_IDENTITY       0x00000001u
#define STAGE84_XNU_SAFE_TABLE_KIND_HIGHVA         0x00000002u
#define STAGE84_XNU_SAFE_TABLE_KIND_REQUIRED       0x00000003u
#define STAGE84_XNU_SAFE_TABLE_MAT_IDENTITY        0x00000001u
#define STAGE84_XNU_SAFE_TABLE_MAT_HIGHVA          0x00000002u
#define STAGE84_XNU_SAFE_TABLE_MAT_REQUIRED        0x00000003u

#define STAGE84_TTE_DESC_VERIFY_LOWMEM_FIRST      0x00000001u
#define STAGE84_TTE_DESC_VERIFY_LOWMEM_LAST       0x00000002u
#define STAGE84_TTE_DESC_VERIFY_KERNEL_FIRST      0x00000004u
#define STAGE84_TTE_DESC_VERIFY_KERNEL_LAST       0x00000008u
#define STAGE84_TTE_DESC_VERIFY_RAM_CONSOLE       0x00000010u
#define STAGE84_TTE_DESC_VERIFY_GIC_FIRST         0x00000020u
#define STAGE84_TTE_DESC_VERIFY_GIC_LAST          0x00000040u
#define STAGE84_TTE_DESC_VERIFY_HIGHVA_FIRST      0x00000080u
#define STAGE84_TTE_DESC_VERIFY_HIGHVA_LAST       0x00000100u
#define STAGE84_TTE_DESC_VERIFY_REQUIRED          0x000001ffu

#define STAGE84_TTE_TRANSLATE_KERNEL_FIRST        0x00000001u
#define STAGE84_TTE_TRANSLATE_KERNEL_LAST         0x00000002u
#define STAGE84_TTE_TRANSLATE_LOW_RAM             0x00000004u
#define STAGE84_TTE_TRANSLATE_RAM_CONSOLE         0x00000008u
#define STAGE84_TTE_TRANSLATE_GIC                 0x00000010u
#define STAGE84_TTE_TRANSLATE_HIGHVA_FIRST        0x00000020u
#define STAGE84_TTE_TRANSLATE_HIGHVA_LAST         0x00000040u
#define STAGE84_TTE_TRANSLATE_REQUIRED            0x0000007fu

#define STAGE84_TTE_XNU_POLICY_BOOT_TTEP          0x00000001u
#define STAGE84_TTE_XNU_POLICY_WORKSPACE          0x00000002u
#define STAGE84_TTE_XNU_POLICY_AVAIL_START        0x00000004u
#define STAGE84_TTE_XNU_POLICY_AVAIL_END          0x00000008u
#define STAGE84_TTE_XNU_POLICY_SAFE_DRYRUN        0x00000010u
#define STAGE84_TTE_XNU_POLICY_REQUIRED           0x0000001fu

#define STAGE84_TTE_SAT_WORKSPACE_RANGE           0x00000001u
#define STAGE84_TTE_SAT_WORKSPACE_ALIGNMENT       0x00000002u
#define STAGE84_TTE_SAT_LAYOUT_FITS               0x00000004u
#define STAGE84_TTE_SAT_KERNEL_MAPPING            0x00000008u
#define STAGE84_TTE_SAT_DEVICE_MAPPING            0x00000010u
#define STAGE84_TTE_SAT_RAM_CONSOLE_MAPPING       0x00000020u
#define STAGE84_TTE_SAT_NO_OVERLAP                0x00000040u
#define STAGE84_TTE_SAT_NO_TTBR_WRITE             0x00000080u
#define STAGE84_TTE_SAT_CACHES_UNCHANGED          0x00000100u
#define STAGE84_TTE_SAT_LOCAL_SIM_ONLY            0x00000200u
#define STAGE84_TTE_SAT_DESCRIPTOR_READBACK       0x00000400u
#define STAGE84_TTE_SAT_DESCRIPTOR_ATTRS          0x00000800u
#define STAGE84_TTE_SAT_TRANSLATION_KERNEL        0x00001000u
#define STAGE84_TTE_SAT_TRANSLATION_LOW_RAM       0x00002000u
#define STAGE84_TTE_SAT_TRANSLATION_CONSOLE       0x00004000u
#define STAGE84_TTE_SAT_TRANSLATION_DEVICE        0x00008000u
#define STAGE84_TTE_SAT_XNU_BOOT_POLICY           0x00010000u
#define STAGE84_TTE_SAT_HIGHVA_MAPPING            0x00020000u
#define STAGE84_TTE_SAT_HIGHVA_TRANSLATION        0x00040000u
#define STAGE84_TTE_SAT_SAFE_TABLE                0x00080000u
#define STAGE84_TTE_SAT_STAGE_OWNED_TABLES        0x00100000u
#define STAGE84_TTE_SAT_REQUIRED                  0x001fffffu

#define STAGE84_TTE_FAIL_WORKSPACE_RANGE          0x00000001u
#define STAGE84_TTE_FAIL_WORKSPACE_ALIGNMENT      0x00000002u
#define STAGE84_TTE_FAIL_LAYOUT_OVERFLOW          0x00000004u
#define STAGE84_TTE_FAIL_KERNEL_MAPPING           0x00000008u
#define STAGE84_TTE_FAIL_DEVICE_MAPPING           0x00000010u
#define STAGE84_TTE_FAIL_RAM_CONSOLE_MAPPING      0x00000020u
#define STAGE84_TTE_FAIL_OVERLAP                  0x00000040u
#define STAGE84_TTE_FAIL_LOCAL_ARENA_ALIAS        0x00000080u
#define STAGE84_TTE_FAIL_CHECKSUM                 0x00000100u
#define STAGE84_TTE_FAIL_DESCRIPTOR_READBACK      0x00000200u
#define STAGE84_TTE_FAIL_DESCRIPTOR_ATTRS         0x00000400u
#define STAGE84_TTE_FAIL_TRANSLATION_KERNEL       0x00000800u
#define STAGE84_TTE_FAIL_TRANSLATION_LOW_RAM      0x00001000u
#define STAGE84_TTE_FAIL_TRANSLATION_CONSOLE      0x00002000u
#define STAGE84_TTE_FAIL_TRANSLATION_DEVICE       0x00004000u
#define STAGE84_TTE_FAIL_XNU_BOOT_POLICY          0x00008000u
#define STAGE84_TTE_FAIL_HIGHVA_MAPPING           0x00010000u
#define STAGE84_TTE_FAIL_HIGHVA_TRANSLATION       0x00020000u
#define STAGE84_TTE_FAIL_SAFE_TABLE               0x00040000u
#define STAGE84_TTE_FAIL_STAGE_OWNED_TABLES       0x00080000u
#define STAGE84_TTE_FAIL_SAFETY                   0x80000000u

struct stage84_xnu_safe_table_materialization {
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

struct stage84_xnu_tte_dryrun {
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
    struct stage84_xnu_safe_table_materialization safe_table;
    uint32_t ttbr0_written;
    uint32_t ttbr1_written;
    uint32_t caches_enabled;
    uint32_t local_sim_zeroed;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t checksum;
    uint32_t status;
};


#define STAGE84_TTBR_RT_VERSION                    1u
#define STAGE84_TTBR_RT_CACHE_MASK                 ((1u << 2) | (1u << 12))

#define STAGE84_TTBR_RT_SAT_PREFLIGHT              0x00000001u
#define STAGE84_TTBR_RT_SAT_TABLE_ALIGNED          0x00000002u
#define STAGE84_TTBR_RT_SAT_STAGE_OWNED_TABLE      0x00000004u
#define STAGE84_TTBR_RT_SAT_REQUIRED_MAPPINGS      0x00000008u
#define STAGE84_TTBR_RT_SAT_DESCRIPTOR_READBACK    0x00000010u
#define STAGE84_TTBR_RT_SAT_STATE_SAVED            0x00000020u
#define STAGE84_TTBR_RT_SAT_CACHE_BITS_PRESERVED   0x00000040u
#define STAGE84_TTBR_RT_SAT_SWITCHED_TO_STAGE_L1   0x00000080u
#define STAGE84_TTBR_RT_SAT_DURING_SELFTEST        0x00000100u
#define STAGE84_TTBR_RT_SAT_ORIGINAL_RESTORED      0x00000200u
#define STAGE84_TTBR_RT_SAT_POST_RESTORE_SELFTEST  0x00000400u
#define STAGE84_TTBR_RT_SAT_NO_XNU_EXEC            0x00000800u
#define STAGE84_TTBR_RT_SAT_NO_MACHO_EXEC          0x00001000u
#define STAGE84_TTBR_RT_SAT_NO_PROPOSED_PHYS_WRITE 0x00002000u
#define STAGE84_TTBR_RT_SAT_NO_PROPOSED_TTE_WRITE  0x00004000u
#define STAGE84_TTBR_RT_SAT_NO_PERSIST_WRITE       0x00008000u
#define STAGE84_TTBR_RT_SAT_REQUIRED               0x0000ffffu

#define STAGE84_TTBR_RT_FAIL_PREFLIGHT             0x00000001u
#define STAGE84_TTBR_RT_FAIL_ALIGNMENT             0x00000002u
#define STAGE84_TTBR_RT_FAIL_OWNERSHIP             0x00000004u
#define STAGE84_TTBR_RT_FAIL_MAPPING               0x00000008u
#define STAGE84_TTBR_RT_FAIL_DESCRIPTOR            0x00000010u
#define STAGE84_TTBR_RT_FAIL_STATE_SAVE            0x00000020u
#define STAGE84_TTBR_RT_FAIL_CACHE_CHANGE          0x00000040u
#define STAGE84_TTBR_RT_FAIL_SWITCH                0x00000080u
#define STAGE84_TTBR_RT_FAIL_DURING_SELFTEST       0x00000100u
#define STAGE84_TTBR_RT_FAIL_RESTORE               0x00000200u
#define STAGE84_TTBR_RT_FAIL_POST_RESTORE          0x00000400u
#define STAGE84_TTBR_RT_FAIL_SAFETY                0x80000000u

#define STAGE84_TTBR_RT_MAP_LOW_STAGE              0x00000001u
#define STAGE84_TTBR_RT_MAP_STACK                  0x00000002u
#define STAGE84_TTBR_RT_MAP_VECTOR                 0x00000004u
#define STAGE84_TTBR_RT_MAP_TABLE                  0x00000008u
#define STAGE84_TTBR_RT_MAP_RAM_CONSOLE0           0x00000010u
#define STAGE84_TTBR_RT_MAP_RAM_CONSOLE1           0x00000020u
#define STAGE84_TTBR_RT_MAP_IMEM                   0x00000040u
#define STAGE84_TTBR_RT_MAP_GIC_TIMER              0x00000080u
#define STAGE84_TTBR_RT_MAP_PSHOLD                 0x00000100u
#define STAGE84_TTBR_RT_MAP_REQUIRED               0x000001ffu

struct stage84_ttbr0_roundtrip {
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

#define STAGE84_DT_READY_BINARY_SELFTEST          0x00000001u
#define STAGE84_DT_READY_CHOSEN                   0x00000002u
#define STAGE84_DT_READY_MEMORY                   0x00000004u
#define STAGE84_DT_READY_CPUS                     0x00000008u
#define STAGE84_DT_READY_INTERRUPT_CONTROLLER     0x00000010u
#define STAGE84_DT_READY_TIMER                    0x00000020u
#define STAGE84_DT_READY_DEVICE_TREE_NODE         0x00000040u
#define STAGE84_DT_READY_TARGET_TYPE              0x00000080u
#define STAGE84_DT_READY_MODEL                    0x00000100u
#define STAGE84_DT_READY_BOOT_ARGS                0x00000200u
#define STAGE84_DT_READY_RAM_CONSOLE              0x00000400u
#define STAGE84_DT_READY_CPU_CLOCKS               0x00000800u
#define STAGE84_DT_READY_IOKIT_SCAFFOLD           0x00001000u
#define STAGE84_DT_READY_PLATFORM_DRIVER          0x00002000u
#define STAGE84_DT_READY_IOKIT_REGISTRY_SERVICES  0x00004000u
#define STAGE84_DT_READY_IOKIT_REJECTED_DRIVER    0x00008000u
#define STAGE84_DT_READY_IOKIT_CATALOG_PROPERTY   0x00010000u
#define STAGE84_DT_READY_IOKIT_PROPERTY_INHERITANCE 0x00020000u
#define STAGE84_DT_READY_IOKIT_REGISTRY_TOPOLOGY  0x00040000u
#define STAGE84_DT_READY_IOKIT_ATTACH_START_READINESS 0x00080000u
#define STAGE84_DT_READY_IOKIT_LIFECYCLE_REGISTER_SERVICE 0x00100000u
#define STAGE84_DT_READY_IOKIT_PROVIDER_NOTIFICATION 0x00200000u
#define STAGE84_DT_READY_IOKIT_PROVIDER_CALLBACK  0x00400000u
#define STAGE84_DT_READY_IOKIT_CLIENT_OPEN        0x00800000u
#define STAGE84_DT_READY_REQUIRED                 (STAGE84_DT_READY_BINARY_SELFTEST | \
                                                   STAGE84_DT_READY_CHOSEN | \
                                                   STAGE84_DT_READY_MEMORY | \
                                                   STAGE84_DT_READY_CPUS | \
                                                   STAGE84_DT_READY_INTERRUPT_CONTROLLER | \
                                                   STAGE84_DT_READY_TIMER | \
                                                   STAGE84_DT_READY_DEVICE_TREE_NODE | \
                                                   STAGE84_DT_READY_TARGET_TYPE | \
                                                   STAGE84_DT_READY_MODEL | \
                                                   STAGE84_DT_READY_BOOT_ARGS | \
                                                   STAGE84_DT_READY_RAM_CONSOLE | \
                                                   STAGE84_DT_READY_CPU_CLOCKS | \
                                                   STAGE84_DT_READY_IOKIT_SCAFFOLD | \
                                                   STAGE84_DT_READY_PLATFORM_DRIVER | \
                                                   STAGE84_DT_READY_IOKIT_REGISTRY_SERVICES | \
                                                   STAGE84_DT_READY_IOKIT_REJECTED_DRIVER | \
                                                   STAGE84_DT_READY_IOKIT_CATALOG_PROPERTY | \
                                                   STAGE84_DT_READY_IOKIT_PROPERTY_INHERITANCE | \
                                                   STAGE84_DT_READY_IOKIT_REGISTRY_TOPOLOGY | \
                                                   STAGE84_DT_READY_IOKIT_ATTACH_START_READINESS | \
                                                   STAGE84_DT_READY_IOKIT_LIFECYCLE_REGISTER_SERVICE | \
                                                   STAGE84_DT_READY_IOKIT_PROVIDER_NOTIFICATION | \
                                                   STAGE84_DT_READY_IOKIT_PROVIDER_CALLBACK | \
                                                   STAGE84_DT_READY_IOKIT_CLIENT_OPEN)

#define STAGE84_PLATFORM_GAP_PMAP_BOOTSTRAP       0x00000001u
#define STAGE84_PLATFORM_GAP_PEXPERT_IMPL         0x00000002u
#define STAGE84_PLATFORM_GAP_GIC_HOOK             0x00000004u
#define STAGE84_PLATFORM_GAP_TIMER_HOOK           0x00000008u
#define STAGE84_PLATFORM_GAP_IOKIT_STACK          0x00000010u
#define STAGE84_PLATFORM_GAP_REQUIRED_RECORDED    (STAGE84_PLATFORM_GAP_PMAP_BOOTSTRAP | \
                                                   STAGE84_PLATFORM_GAP_PEXPERT_IMPL | \
                                                   STAGE84_PLATFORM_GAP_GIC_HOOK | \
                                                   STAGE84_PLATFORM_GAP_TIMER_HOOK | \
                                                   STAGE84_PLATFORM_GAP_IOKIT_STACK)

#define STAGE84_LOADER_IRQ_READY_GIC_DIST         0x00000001u
#define STAGE84_LOADER_IRQ_READY_GIC_CPU          0x00000002u
#define STAGE84_LOADER_IRQ_READY_SERVICE          0x00000004u
#define STAGE84_LOADER_IRQ_READY_TIMEBASE         0x00000008u
#define STAGE84_LOADER_IRQ_READY_REQUIRED         (STAGE84_LOADER_IRQ_READY_GIC_DIST | \
                                                   STAGE84_LOADER_IRQ_READY_GIC_CPU | \
                                                   STAGE84_LOADER_IRQ_READY_SERVICE | \
                                                   STAGE84_LOADER_IRQ_READY_TIMEBASE)

#define STAGE84_LOADER_SAFETY_NO_EXECUTE          0x00000001u
#define STAGE84_LOADER_SAFETY_STAGE_OWNED_TTBR    0x00000002u
#define STAGE84_LOADER_SAFETY_CACHES_UNCHANGED    0x00000004u
#define STAGE84_LOADER_SAFETY_NO_PERSIST_WRITE    0x00000008u
#define STAGE84_LOADER_SAFETY_RAM_CONSOLE         0x00000010u
#define STAGE84_LOADER_SAFETY_LOCAL_ARENA_ONLY    0x00000020u
#define STAGE84_LOADER_SAFETY_NO_PHYS_WRITE       0x00000040u
#define STAGE84_LOADER_SAFETY_TTE_DRYRUN_ONLY     0x00000080u
#define STAGE84_LOADER_SAFETY_TTBR_RESTORED       0x00000100u
#define STAGE84_LOADER_SAFETY_NO_CACHE_CHANGE     0x00000200u
#define STAGE84_LOADER_SAFETY_TTE_VERIFY_ONLY     0x00000400u
#define STAGE84_LOADER_SAFETY_VTOP_DRYRUN_ONLY    0x00000800u
#define STAGE84_LOADER_SAFETY_HIGHVA_DRYRUN_ONLY  0x00001000u
#define STAGE84_LOADER_SAFETY_SAFE_TABLE_LOCAL_ONLY 0x00002000u
#define STAGE84_LOADER_SAFETY_STAGE_OWNED_TABLES_ONLY 0x00004000u
#define STAGE84_LOADER_SAFETY_NO_FULL_XNU_BUILD   0x00008000u
#define STAGE84_LOADER_SAFETY_NO_PUBLIC_XNU_EXEC  0x00010000u
#define STAGE84_LOADER_SAFETY_NO_EXTERNAL_MUTATION 0x00020000u
#define STAGE84_LOADER_SAFETY_CONTROLLED_PUBLIC_XNU_LINK_NO_EXEC 0x00040000u
#define STAGE84_LOADER_SAFETY_XNU_COMPILE_GRAPH_NO_EXEC 0x00080000u
#define STAGE84_LOADER_SAFETY_NO_PLATFORM_RUNTIME_EXEC 0x00100000u
#define STAGE84_LOADER_SAFETY_REQUIRED            (STAGE84_LOADER_SAFETY_NO_EXECUTE | \
                                                   STAGE84_LOADER_SAFETY_STAGE_OWNED_TTBR | \
                                                   STAGE84_LOADER_SAFETY_CACHES_UNCHANGED | \
                                                   STAGE84_LOADER_SAFETY_NO_PERSIST_WRITE | \
                                                   STAGE84_LOADER_SAFETY_RAM_CONSOLE | \
                                                   STAGE84_LOADER_SAFETY_LOCAL_ARENA_ONLY | \
                                                   STAGE84_LOADER_SAFETY_NO_PHYS_WRITE | \
                                                   STAGE84_LOADER_SAFETY_TTE_DRYRUN_ONLY | \
                                                   STAGE84_LOADER_SAFETY_TTBR_RESTORED | \
                                                   STAGE84_LOADER_SAFETY_NO_CACHE_CHANGE | \
                                                   STAGE84_LOADER_SAFETY_TTE_VERIFY_ONLY | \
                                                   STAGE84_LOADER_SAFETY_VTOP_DRYRUN_ONLY | \
                                                   STAGE84_LOADER_SAFETY_HIGHVA_DRYRUN_ONLY | \
                                                   STAGE84_LOADER_SAFETY_SAFE_TABLE_LOCAL_ONLY | \
                                                   STAGE84_LOADER_SAFETY_STAGE_OWNED_TABLES_ONLY | \
                                                   STAGE84_LOADER_SAFETY_NO_FULL_XNU_BUILD | \
                                                   STAGE84_LOADER_SAFETY_NO_PUBLIC_XNU_EXEC | \
                                                   STAGE84_LOADER_SAFETY_NO_EXTERNAL_MUTATION | \
                                                   STAGE84_LOADER_SAFETY_CONTROLLED_PUBLIC_XNU_LINK_NO_EXEC | \
                                                   STAGE84_LOADER_SAFETY_XNU_COMPILE_GRAPH_NO_EXEC | \
                                                   STAGE84_LOADER_SAFETY_NO_PLATFORM_RUNTIME_EXEC)

#define STAGE84_XNU_PEXPERT_HOOK_READINESS_CONTRACT_VERSION 1u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_EXPECTED_GIC_DIST_BASE 0xf9000000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_EXPECTED_GIC_CPU_BASE 0xf9002000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_EXPECTED_TIMER_BASE 0xf9020000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_EXPECTED_TIMER_FREQ 19200000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_EXPECTED_CPU_COUNT 4u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_EXPECTED_IRQ_COUNT_MIN 288u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI0_ID 18u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI1_ID 19u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI_MASK \
    ((1u << STAGE84_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI0_ID) | \
     (1u << STAGE84_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI1_ID))

#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_SOURCE_ROLLUPS 0x00000001u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_PE_STATE 0x00000002u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_APPLE_DT 0x00000004u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_BOOTARGS_MARKERS 0x00000008u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_GIC_BASES 0x00000010u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_GIC_SNAPSHOT 0x00000020u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_GIC_IRQ_CAPACITY 0x00000040u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_SGI_HOOK 0x00000080u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_TIMER_PPI_PLAN 0x00000100u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_TIMER_IRQ_HOOK 0x00000200u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_TIMEBASE_HOOK 0x00000400u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_VECTOR_HOOK 0x00000800u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_PLATFORM_ROLLUP 0x00001000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_INTERRUPT_ROLLUP 0x00002000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_PUBLIC_GRAPH_BOUNDARY 0x00004000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_OBJECT_LINK_BOUNDARY 0x00008000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_PMAP_TRANSITION_BOUNDARY 0x00010000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PUBLIC_PEXPERT_EXEC 0x00020000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PLATFORM_RUNTIME_EXEC 0x00040000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PUBLIC_PMAP_EXEC 0x00080000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_NO_LIVE_PMAP_INSTALL 0x00100000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PROPOSED_PMAP_WRITE 0x00200000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PMAP_CONTROL_WRITE 0x00400000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_NO_TLB_CACHE_CHANGE 0x00800000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_NO_XNU_MACHO_EXEC 0x01000000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PERSIST_WRITE 0x02000000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_SAT_FAIL_CLOSED 0x04000000u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_REQUIRED_MASK 0x07ffffffu

#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_PE_STATE 0x00000002u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_DT 0x00000004u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_BOOTARGS 0x00000008u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_GIC 0x00000010u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_SGI 0x00000020u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_TIMER 0x00000040u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_TIMEBASE 0x00000080u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_ROLLUP 0x00000100u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_PUBLIC_BOUNDARY 0x00000200u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_PMAP_BOUNDARY 0x00000400u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_CHECKSUM 0x00000800u
#define STAGE84_XNU_PEXPERT_HOOK_READINESS_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_pexpert_hook_readiness_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_compile_graph_status;
    uint32_t source_compile_graph_satisfied_mask;
    uint32_t source_compile_graph_failure_mask;
    uint32_t source_compile_graph_checksum;
    uint32_t source_object_subset_status;
    uint32_t source_object_subset_satisfied_mask;
    uint32_t source_object_subset_failure_mask;
    uint32_t source_object_subset_checksum;
    uint32_t source_link_status;
    uint32_t source_link_satisfied_mask;
    uint32_t source_link_failure_mask;
    uint32_t source_link_checksum;
    uint32_t source_pmap_transition_status;
    uint32_t source_pmap_transition_satisfied_mask;
    uint32_t source_pmap_transition_failure_mask;
    uint32_t source_pmap_transition_checksum;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t command_line_checksum;
    uint32_t command_line_stage84_marker;
    uint32_t command_line_pexpert_marker;
    uint32_t apple_dt_semantic_mask;
    uint32_t pe_boot_args_ptr;
    uint32_t pe_device_tree_head;
    uint32_t pe_device_tree_length;
    uint32_t pe_memory_base;
    uint32_t pe_memory_size;
    uint32_t pe_cpu_count;
    uint32_t pe_machine_type;
    uint32_t pe_vector_base;
    uint32_t pe_gic_dist_base;
    uint32_t pe_gic_cpu_base;
    uint32_t pe_timer_base;
    uint32_t pe_timer_frequency;
    uint32_t expected_cpu_count;
    uint32_t expected_gic_dist_base;
    uint32_t expected_gic_cpu_base;
    uint32_t expected_timer_base;
    uint32_t expected_timer_frequency;
    uint32_t expected_irq_count_min;
    uint32_t gic_dist_base;
    uint32_t gic_cpu_base;
    uint32_t gic_dist_ctlr;
    uint32_t gic_dist_typer;
    uint32_t gic_dist_iidr;
    uint32_t gic_cpu_iidr;
    uint32_t gic_cpu_ctlr;
    uint32_t gic_cpu_pmr;
    uint32_t gic_cpu_bpr;
    uint32_t gic_irq_count;
    uint32_t gic_cpu_interface_count;
    uint32_t gic_isenabler0;
    uint32_t gic_ispendr0;
    uint32_t gic_priority0;
    uint32_t gic_targets0;
    uint32_t gic_snapshot_validated;
    uint32_t sgi_selftest_passed;
    uint32_t sgi_irq_count;
    uint32_t sgi_sgi0_count;
    uint32_t sgi_last_irq_id;
    uint32_t timer_ppi0_id;
    uint32_t timer_ppi1_id;
    uint32_t timer_ppi_mask;
    uint32_t timer_ppi_mask_observed;
    uint32_t timer_selftest_passed;
    uint32_t timer_irq_count;
    uint32_t timer_timer_count;
    uint32_t timer_last_irq_id;
    uint32_t timer_last_ctl;
    uint32_t timebase_freq_hz;
    uint32_t ml_timebase_freq_hz;
    uint32_t timebase_expected_hz;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_interrupt_ready_mask;
    uint32_t public_pexpert_compile_allowed;
    uint32_t public_arm_pe_bootargs_allowed;
    uint32_t public_arm_consistent_debug_allowed;
    uint32_t public_pexpert_runtime_blocked;
    uint32_t public_object_subset_ready;
    uint32_t controlled_link_ready;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_CPU_COUNT 4u
#define STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_GIC_DIST_BASE 0xf9000000u
#define STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_TIMER_BASE 0xf9020000u
#define STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_TIMEBASE_FREQ 19200000u
#define STAGE84_XNU_IOKIT_REFERENCE_IODEVICE_TREE_SUPPORT 0x00000001u
#define STAGE84_XNU_IOKIT_REFERENCE_IOPLATFORM_EXPERT    0x00000002u
#define STAGE84_XNU_IOKIT_REFERENCE_IOCPU                0x00000004u
#define STAGE84_XNU_IOKIT_REFERENCE_REQUIRED             0x00000007u

#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_SOURCE_PEXPERT 0x00000001u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_DEVICE_TREE_IMPORT 0x00000004u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_IOPLATFORM_NODE 0x00000008u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_IOCPU_TOPOLOGY 0x00000010u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PLATFORM_DRIVER_NODE 0x00000020u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_MSM8974_DRIVER_FACTS 0x00000040u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_GIC_TIMER_PROVIDER_PLAN 0x00000080u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PUBLIC_IOKIT_REFERENCE 0x00000100u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PUBLIC_PEXPERT_BOUNDARY 0x00000200u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_OBJECT_LINK_BOUNDARY 0x00000400u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PMAP_TRANSITION_BOUNDARY 0x00000800u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_IOKIT_RUNTIME_EXEC 0x00001000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PLATFORM_DRIVER_EXEC 0x00002000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PUBLIC_XNU_EXEC 0x00004000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PUBLIC_PMAP_EXEC 0x00008000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_LIVE_PMAP_INSTALL 0x00010000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PROPOSED_PMAP_WRITE 0x00020000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PMAP_CONTROL_WRITE 0x00040000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_TLB_CACHE_CHANGE 0x00080000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_XNU_MACHO_EXEC 0x00100000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PERSIST_WRITE 0x00200000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PLATFORM_ROLLUP 0x00400000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_FAIL_CLOSED 0x00800000u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_REQUIRED_MASK 0x00ffffffu

#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_IOKIT 0x00000004u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_DRIVER 0x00000008u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_PUBLIC_BOUNDARY 0x00000010u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_PMAP_BOUNDARY 0x00000020u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_ROLLUP 0x00000040u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_CHECKSUM 0x00000080u
#define STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_platform_scaffold_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_pexpert_status;
    uint32_t source_pexpert_satisfied_mask;
    uint32_t source_pexpert_failure_mask;
    uint32_t source_pexpert_checksum;
    uint32_t source_compile_graph_status;
    uint32_t source_compile_graph_satisfied_mask;
    uint32_t source_compile_graph_failure_mask;
    uint32_t source_compile_graph_checksum;
    uint32_t source_object_subset_status;
    uint32_t source_object_subset_failure_mask;
    uint32_t source_link_status;
    uint32_t source_link_failure_mask;
    uint32_t source_pmap_transition_status;
    uint32_t source_pmap_transition_satisfied_mask;
    uint32_t source_pmap_transition_failure_mask;
    uint32_t source_pmap_transition_checksum;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t device_tree_node_ptr;
    uint32_t device_tree_node_prop_count;
    uint32_t device_tree_target_type_present;
    uint32_t device_tree_model_present;
    uint32_t iokit_platform_node_ptr;
    uint32_t iokit_platform_prop_count;
    uint32_t iokit_platform_compatible_present;
    uint32_t iokit_platform_ioclass_present;
    uint32_t iokit_platform_provider_present;
    uint32_t iokit_platform_device_type_present;
    uint32_t iokit_platform_registry_plane_present;
    uint32_t platform_driver_node_ptr;
    uint32_t platform_driver_prop_count;
    uint32_t platform_driver_compatible_present;
    uint32_t platform_driver_ioclass_present;
    uint32_t platform_driver_provider_present;
    uint32_t platform_driver_match_category_present;
    uint32_t platform_driver_gic_base;
    uint32_t platform_driver_timer_base;
    uint32_t platform_driver_timebase_frequency;
    uint32_t platform_driver_cpu_count;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t iodevice_tree_support_reference;
    uint32_t ioplatform_expert_reference;
    uint32_t iocpu_reference;
    uint32_t public_pexpert_runtime_blocked;
    uint32_t public_object_subset_ready;
    uint32_t controlled_link_ready;
    uint32_t pexpert_hook_ready;
    uint32_t pmap_transition_ready;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};


#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_MATCH_EXPECTED_PROVIDER_CLASS_HASH 0x49504f44u /* 'IPOD' local provider marker */
#define STAGE84_XNU_IOKIT_MATCH_EXPECTED_PROBE_SCORE 0x00000650u

#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_SOURCE_SCAFFOLD 0x00000001u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_PROVIDER_NODE 0x00000004u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_DRIVER_NODE 0x00000008u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_PROVIDER_CLASS_MATCH 0x00000010u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_MATCH_CATEGORY 0x00000020u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_COMPATIBLE_MATCH 0x00000040u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_MSM8974_FACTS 0x00000080u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_IOKIT_REFERENCE 0x00000100u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_SERVICE_MATCH_DRYRUN 0x00000200u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_PROBE_SCORE 0x00000400u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_ATTACH_START_DEFERRED 0x00000800u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00001000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_PMAP_BOUNDARY 0x00002000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00004000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PLATFORM_DRIVER_EXEC 0x00008000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x00010000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00020000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00040000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PROPOSED_PMAP_WRITE 0x00080000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PMAP_CONTROL_WRITE 0x00100000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x00200000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00400000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PERSIST_WRITE 0x00800000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_PLATFORM_ROLLUP 0x01000000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_SAT_FAIL_CLOSED 0x02000000u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_REQUIRED_MASK 0x03ffffffu

#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_PROVIDER 0x00000004u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_DRIVER 0x00000008u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_MATCH 0x00000010u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000020u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000040u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_ROLLUP 0x00000080u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_CHECKSUM 0x00000100u
#define STAGE84_XNU_IOKIT_MATCH_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_match_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_scaffold_status;
    uint32_t source_scaffold_satisfied_mask;
    uint32_t source_scaffold_failure_mask;
    uint32_t source_scaffold_checksum;
    uint32_t source_pexpert_status;
    uint32_t source_pexpert_failure_mask;
    uint32_t source_pmap_transition_status;
    uint32_t source_pmap_transition_failure_mask;
    uint32_t source_compile_graph_status;
    uint32_t source_compile_graph_failure_mask;
    uint32_t source_object_subset_status;
    uint32_t source_object_subset_failure_mask;
    uint32_t source_link_status;
    uint32_t source_link_failure_mask;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t provider_node_ptr;
    uint32_t provider_prop_count;
    uint32_t provider_compatible_present;
    uint32_t provider_ioclass_present;
    uint32_t provider_device_type_present;
    uint32_t provider_registry_plane_present;
    uint32_t provider_class_is_platform_device;
    uint32_t provider_registry_is_device_tree;
    uint32_t driver_node_ptr;
    uint32_t driver_prop_count;
    uint32_t driver_compatible_present;
    uint32_t driver_ioclass_present;
    uint32_t driver_provider_present;
    uint32_t driver_match_category_present;
    uint32_t driver_name_match_present;
    uint32_t driver_probe_score_present;
    uint32_t driver_local_only_present;
    uint32_t driver_probe_score;
    uint32_t provider_class_match;
    uint32_t provider_path_match;
    uint32_t match_category_match;
    uint32_t compatible_match;
    uint32_t driver_gic_base;
    uint32_t driver_timer_base;
    uint32_t driver_timebase_frequency;
    uint32_t driver_cpu_count;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t provider_candidate_count;
    uint32_t driver_candidate_count;
    uint32_t selected_driver_count;
    uint32_t rejected_driver_count;
    uint32_t dryrun_match_count;
    uint32_t attach_deferred;
    uint32_t start_deferred;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};


#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_PROVIDER_COUNT 1u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRIVER_CANDIDATE_COUNT 5u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_COUNT 4u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_COUNT 1u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT 0x00000001u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT 0x00000002u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT 0x00000004u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT 0x00000008u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT 0x00000010u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED \
    (STAGE84_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT | \
     STAGE84_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT | \
     STAGE84_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT | \
     STAGE84_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT)
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED \
    STAGE84_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_PROBE_SCORE \
    STAGE84_XNU_IOKIT_MATCH_EXPECTED_PROBE_SCORE
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_PROBE_SCORE 0x00000660u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PROBE_SCORE 0x00000661u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_CPU_PROBE_SCORE 0x00000662u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_EXPECTED_IRQ_COUNT_MIN 288u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PPI_MASK \
    ((1u << STAGE84_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI0_ID) | \
     (1u << STAGE84_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI1_ID))

#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_SOURCE_MATCH 0x00000001u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_REGISTRY_ROOT 0x00000004u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_SERVICE_NODE_SET 0x00000008u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_PROVIDER_MATRIX 0x00000010u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_CATEGORY_MATRIX 0x00000020u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_COMPATIBLE_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_PROBE_SCORE_MATRIX 0x00000080u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_SERVICE_FACTS 0x00000100u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_REJECT_MATRIX 0x00000200u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_DRYRUN_COUNTS 0x00000400u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_ATTACH_START_DEFERRED 0x00000800u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_IOKIT_REFERENCE 0x00001000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00002000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_PMAP_BOUNDARY 0x00004000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00008000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_PLATFORM_DRIVER_EXEC 0x00010000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x00020000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00040000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00080000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_PROPOSED_PMAP_WRITE 0x00100000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_PMAP_CONTROL_WRITE 0x00200000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x00400000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_NO_PERSIST_WRITE 0x01000000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_PLATFORM_ROLLUP 0x02000000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_SAT_FAIL_CLOSED 0x04000000u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_REQUIRED_MASK 0x07ffffffu

#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_REGISTRY 0x00000004u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_MATCH 0x00000008u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_FACTS 0x00000010u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000020u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000040u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_ROLLUP 0x00000080u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_CHECKSUM 0x00000100u
#define STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_registry_service_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_match_status;
    uint32_t source_match_satisfied_mask;
    uint32_t source_match_failure_mask;
    uint32_t source_match_checksum;
    uint32_t source_scaffold_status;
    uint32_t source_scaffold_satisfied_mask;
    uint32_t source_scaffold_failure_mask;
    uint32_t source_scaffold_checksum;
    uint32_t source_pexpert_status;
    uint32_t source_pexpert_failure_mask;
    uint32_t source_pmap_transition_status;
    uint32_t source_pmap_transition_failure_mask;
    uint32_t source_compile_graph_status;
    uint32_t source_compile_graph_failure_mask;
    uint32_t source_object_subset_status;
    uint32_t source_object_subset_failure_mask;
    uint32_t source_link_status;
    uint32_t source_link_failure_mask;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t registry_root_node_ptr;
    uint32_t registry_root_prop_count;
    uint32_t registry_root_provider_ok;
    uint32_t platform_driver_node_ptr;
    uint32_t platform_driver_prop_count;
    uint32_t interrupt_service_node_ptr;
    uint32_t interrupt_service_prop_count;
    uint32_t timer_service_node_ptr;
    uint32_t timer_service_prop_count;
    uint32_t cpu_service_node_ptr;
    uint32_t cpu_service_prop_count;
    uint32_t rejected_driver_node_ptr;
    uint32_t rejected_driver_prop_count;
    uint32_t provider_class_match_mask;
    uint32_t category_match_mask;
    uint32_t compatible_match_mask;
    uint32_t probe_score_match_mask;
    uint32_t local_only_mask;
    uint32_t selected_service_mask;
    uint32_t rejected_service_mask;
    uint32_t service_match_matrix;
    uint32_t service_fact_mask;
    uint32_t platform_probe_score;
    uint32_t interrupt_probe_score;
    uint32_t timer_probe_score;
    uint32_t cpu_probe_score;
    uint32_t rejected_probe_score;
    uint32_t interrupt_gic_base;
    uint32_t interrupt_gic_cpu_base;
    uint32_t interrupt_irq_count;
    uint32_t timer_base;
    uint32_t timer_frequency;
    uint32_t timer_ppi_mask;
    uint32_t cpu_count;
    uint32_t cpu_timebase_frequency;
    uint32_t provider_candidate_count;
    uint32_t driver_candidate_count;
    uint32_t selected_driver_count;
    uint32_t rejected_driver_count;
    uint32_t dryrun_match_count;
    uint32_t service_node_count;
    uint32_t registry_plane_count;
    uint32_t attach_deferred_count;
    uint32_t start_deferred_count;
    uint32_t attach_deferred;
    uint32_t start_deferred;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_PROVIDER_COUNT 1u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_SERVICE_CANDIDATE_COUNT \
    STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRIVER_CANDIDATE_COUNT
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_PUBLISHED_COUNT \
    STAGE84_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_COUNT
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_REJECTED_COUNT \
    STAGE84_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_COUNT
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_ROOT_ORDINAL 0u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_PLATFORM_ORDINAL 1u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_INTERRUPT_ORDINAL 2u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_TIMER_ORDINAL 3u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_CPU_ORDINAL 4u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_REJECTED_ORDINAL 0xffffffffu

#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_SOURCE_REGISTRY 0x00000001u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PROVIDER_ROOT 0x00000004u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_SERVICE_NODE_SET 0x00000008u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PARENT_MATRIX 0x00000010u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PUBLISH_ORDER 0x00000020u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_DEPENDENCY_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_SERVICE_FACTS 0x00000080u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_REJECT_UNPUBLISHED 0x00000100u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_DRYRUN_COUNTS 0x00000200u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_ATTACH_START_DEFERRED 0x00000400u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_IOKIT_REFERENCE 0x00000800u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00001000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PMAP_BOUNDARY 0x00002000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00004000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PROVIDER_RUNTIME_EXEC 0x00008000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PLATFORM_DRIVER_EXEC 0x00010000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x00020000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00040000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00080000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PROPOSED_PMAP_WRITE 0x00100000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PMAP_CONTROL_WRITE 0x00200000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x00400000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PERSIST_WRITE 0x01000000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PLATFORM_ROLLUP 0x02000000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_FAIL_CLOSED 0x04000000u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_REQUIRED_MASK 0x07ffffffu

#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PROVIDER 0x00000004u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PUBLISH 0x00000008u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_DEPENDENCY 0x00000010u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_FACTS 0x00000020u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000040u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000080u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_ROLLUP 0x00000100u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_CHECKSUM 0x00000200u
#define STAGE84_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_provider_plane_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_registry_status;
    uint32_t source_registry_satisfied_mask;
    uint32_t source_registry_failure_mask;
    uint32_t source_registry_checksum;
    uint32_t source_match_status;
    uint32_t source_match_failure_mask;
    uint32_t source_scaffold_status;
    uint32_t source_scaffold_failure_mask;
    uint32_t source_pexpert_status;
    uint32_t source_pexpert_failure_mask;
    uint32_t source_pmap_transition_status;
    uint32_t source_pmap_transition_failure_mask;
    uint32_t source_compile_graph_status;
    uint32_t source_compile_graph_failure_mask;
    uint32_t source_object_subset_status;
    uint32_t source_object_subset_failure_mask;
    uint32_t source_link_status;
    uint32_t source_link_failure_mask;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t registry_root_node_ptr;
    uint32_t registry_root_prop_count;
    uint32_t registry_root_provider_ok;
    uint32_t platform_driver_node_ptr;
    uint32_t platform_driver_prop_count;
    uint32_t interrupt_service_node_ptr;
    uint32_t interrupt_service_prop_count;
    uint32_t timer_service_node_ptr;
    uint32_t timer_service_prop_count;
    uint32_t cpu_service_node_ptr;
    uint32_t cpu_service_prop_count;
    uint32_t rejected_driver_node_ptr;
    uint32_t rejected_driver_prop_count;
    uint32_t provider_parent_match_mask;
    uint32_t provider_dependency_mask;
    uint32_t provider_publish_order_mask;
    uint32_t provider_published_service_mask;
    uint32_t provider_unpublished_service_mask;
    uint32_t provider_plane_matrix;
    uint32_t source_selected_service_mask;
    uint32_t source_rejected_service_mask;
    uint32_t service_fact_mask;
    uint32_t root_publish_ordinal;
    uint32_t platform_publish_ordinal;
    uint32_t interrupt_publish_ordinal;
    uint32_t timer_publish_ordinal;
    uint32_t cpu_publish_ordinal;
    uint32_t rejected_publish_ordinal;
    uint32_t publish_order_checksum;
    uint32_t provider_candidate_count;
    uint32_t service_candidate_count;
    uint32_t published_service_count;
    uint32_t unpublished_candidate_count;
    uint32_t dryrun_publish_count;
    uint32_t provider_plane_count;
    uint32_t registry_plane_count;
    uint32_t attach_deferred_count;
    uint32_t start_deferred_count;
    uint32_t attach_deferred;
    uint32_t start_deferred;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_provider_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_CATALOG_COUNT 1u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_PERSONALITY_COUNT \
    STAGE84_XNU_IOKIT_REGISTRY_SERVICE_DRIVER_CANDIDATE_COUNT
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_SELECTED_COUNT \
    STAGE84_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_COUNT
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_REJECTED_COUNT \
    STAGE84_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_COUNT
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_CATALOG_VERSION 1u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_PLATFORM_ORDINAL 0u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_INTERRUPT_ORDINAL 1u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_TIMER_ORDINAL 2u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_CPU_ORDINAL 3u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_REJECTED_ORDINAL 0xffffffffu

#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_SOURCE_PROVIDER 0x00000001u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_CATALOG_ROOT 0x00000004u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PERSONALITY_NODE_SET 0x00000008u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PERSONALITY_NAME_MATRIX 0x00000010u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_BUNDLE_IDENTIFIER_MATRIX 0x00000020u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_CATALOG_MARKER_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PROVIDER_MATRIX 0x00000080u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_CATEGORY_MATRIX 0x00000100u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_COMPATIBLE_MATRIX 0x00000200u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PROBE_SCORE_MATRIX 0x00000400u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_LOCAL_ONLY_MATRIX 0x00000800u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_SERVICE_FACTS 0x00001000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_REJECT_PERSONALITY 0x00002000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_DRYRUN_COUNTS 0x00004000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PROPERTY_CHECKSUM 0x00008000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_ATTACH_START_DEFERRED 0x00010000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_IOKIT_REFERENCE 0x00020000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00040000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PMAP_BOUNDARY 0x00080000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00100000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_CATALOG_RUNTIME_EXEC 0x00200000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PLATFORM_DRIVER_EXEC 0x00400000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x01000000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x02000000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PROPOSED_PMAP_WRITE 0x04000000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PMAP_CONTROL_WRITE 0x08000000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x10000000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x20000000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_FAIL_CLOSED 0x40000000u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_CATALOG 0x00000004u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PERSONALITY 0x00000008u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_MATCH 0x00000010u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_FACTS 0x00000020u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_COUNTS 0x00000040u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000080u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000100u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_ROLLUP 0x00000200u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_CHECKSUM 0x00000400u
#define STAGE84_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_catalog_property_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_provider_status;
    uint32_t source_provider_satisfied_mask;
    uint32_t source_provider_failure_mask;
    uint32_t source_provider_checksum;
    uint32_t source_registry_status;
    uint32_t source_registry_satisfied_mask;
    uint32_t source_registry_failure_mask;
    uint32_t source_registry_checksum;
    uint32_t source_match_status;
    uint32_t source_scaffold_status;
    uint32_t source_pexpert_status;
    uint32_t source_pmap_transition_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t catalog_root_node_ptr;
    uint32_t catalog_root_prop_count;
    uint32_t catalog_name_present;
    uint32_t catalog_version;
    uint32_t catalog_local_only;
    uint32_t platform_personality_node_ptr;
    uint32_t platform_personality_prop_count;
    uint32_t interrupt_personality_node_ptr;
    uint32_t interrupt_personality_prop_count;
    uint32_t timer_personality_node_ptr;
    uint32_t timer_personality_prop_count;
    uint32_t cpu_personality_node_ptr;
    uint32_t cpu_personality_prop_count;
    uint32_t rejected_personality_node_ptr;
    uint32_t rejected_personality_prop_count;
    uint32_t source_selected_service_mask;
    uint32_t source_rejected_service_mask;
    uint32_t source_service_fact_mask;
    uint32_t source_parent_match_mask;
    uint32_t source_dependency_mask;
    uint32_t source_publish_order_mask;
    uint32_t personality_name_match_mask;
    uint32_t bundle_identifier_match_mask;
    uint32_t catalog_marker_match_mask;
    uint32_t provider_class_match_mask;
    uint32_t category_match_mask;
    uint32_t compatible_match_mask;
    uint32_t probe_score_match_mask;
    uint32_t local_only_match_mask;
    uint32_t selected_personality_mask;
    uint32_t rejected_personality_mask;
    uint32_t catalog_property_matrix;
    uint32_t platform_personality_ordinal;
    uint32_t interrupt_personality_ordinal;
    uint32_t timer_personality_ordinal;
    uint32_t cpu_personality_ordinal;
    uint32_t rejected_personality_ordinal;
    uint32_t personality_order_checksum;
    uint32_t personality_property_checksum;
    uint32_t expected_property_checksum;
    uint32_t catalog_count;
    uint32_t personality_candidate_count;
    uint32_t selected_personality_count;
    uint32_t rejected_personality_count;
    uint32_t property_dryrun_count;
    uint32_t attach_deferred_count;
    uint32_t start_deferred_count;
    uint32_t attach_deferred;
    uint32_t start_deferred;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_catalog_runtime_exec;
    uint32_t no_provider_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT \
    STAGE84_XNU_IOKIT_CATALOG_PROPERTY_SELECTED_COUNT
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_CANDIDATE_COUNT \
    STAGE84_XNU_IOKIT_CATALOG_PROPERTY_PERSONALITY_COUNT
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_COUNT \
    STAGE84_XNU_IOKIT_CATALOG_PROPERTY_REJECTED_COUNT
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_PLATFORM_ORDINAL \
    STAGE84_XNU_IOKIT_CATALOG_PROPERTY_PLATFORM_ORDINAL
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_INTERRUPT_ORDINAL \
    STAGE84_XNU_IOKIT_CATALOG_PROPERTY_INTERRUPT_ORDINAL
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_TIMER_ORDINAL \
    STAGE84_XNU_IOKIT_CATALOG_PROPERTY_TIMER_ORDINAL
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_CPU_ORDINAL \
    STAGE84_XNU_IOKIT_CATALOG_PROPERTY_CPU_ORDINAL
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_ORDINAL \
    STAGE84_XNU_IOKIT_CATALOG_PROPERTY_REJECTED_ORDINAL
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_REGISTRY_PLANE_HASH 0x494f4454u /* 'IODT' */
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_EXPECTED_PROPERTY_CHECKSUM 0x494f4455u

#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_SOURCE_CATALOG 0x00000001u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_PERSONALITY_NODE_SET 0x00000004u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_REGISTRY_ENTRY_MARKERS 0x00000008u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_ENTRY_ORDINAL_MATRIX 0x00000010u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_REGISTRY_PLANE_MATRIX 0x00000020u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_CLASS_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_PROVIDER_MATRIX 0x00000080u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_CATEGORY_MATRIX 0x00000100u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_COMPATIBLE_MATRIX 0x00000200u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_PROBE_SCORE_MATRIX 0x00000400u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_BUNDLE_MATRIX 0x00000800u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INSTANCE_SOURCE_BIT_MATRIX 0x00001000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INSTANCE_PUBLICATION_MATRIX 0x00002000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INSTANCE_LOCAL_ONLY_MATRIX 0x00004000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_ATTACH_START_DEFERRED 0x00008000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_REJECT_UNATTACHED 0x00010000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_DRYRUN_COUNTS 0x00020000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_PROPERTY_CHECKSUM 0x00040000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_IOKIT_REFERENCE 0x00080000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00100000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_PMAP_BOUNDARY 0x00200000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00400000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_REGISTRY_ENTRY_RUNTIME_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_PROPERTY_RUNTIME_EXEC 0x01000000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_ATTACH_START_RUNTIME_EXEC 0x02000000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x04000000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x08000000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x10000000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x20000000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_FAIL_CLOSED 0x40000000u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_PERSONALITY 0x00000004u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_REGISTRY_ENTRY 0x00000008u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INHERITANCE 0x00000010u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INSTANCE 0x00000020u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_REJECTED 0x00000040u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_COUNTS 0x00000080u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000100u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000200u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_ROLLUP 0x00000400u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_CHECKSUM 0x00000800u
#define STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_property_inheritance_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_catalog_status;
    uint32_t source_catalog_satisfied_mask;
    uint32_t source_catalog_failure_mask;
    uint32_t source_catalog_checksum;
    uint32_t source_catalog_property_checksum;
    uint32_t source_provider_status;
    uint32_t source_provider_satisfied_mask;
    uint32_t source_provider_failure_mask;
    uint32_t source_registry_status;
    uint32_t source_registry_satisfied_mask;
    uint32_t source_registry_failure_mask;
    uint32_t source_match_status;
    uint32_t source_scaffold_status;
    uint32_t source_pexpert_status;
    uint32_t source_pmap_transition_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t platform_personality_node_ptr;
    uint32_t platform_personality_prop_count;
    uint32_t interrupt_personality_node_ptr;
    uint32_t interrupt_personality_prop_count;
    uint32_t timer_personality_node_ptr;
    uint32_t timer_personality_prop_count;
    uint32_t cpu_personality_node_ptr;
    uint32_t cpu_personality_prop_count;
    uint32_t rejected_personality_node_ptr;
    uint32_t rejected_personality_prop_count;
    uint32_t source_selected_personality_mask;
    uint32_t source_rejected_personality_mask;
    uint32_t source_catalog_property_matrix;
    uint32_t registry_entry_marker_mask;
    uint32_t property_inheritance_marker_mask;
    uint32_t registry_entry_ordinal_mask;
    uint32_t registry_plane_match_mask;
    uint32_t inherited_ioclass_mask;
    uint32_t inherited_provider_class_mask;
    uint32_t inherited_category_mask;
    uint32_t inherited_compatible_mask;
    uint32_t inherited_probe_score_mask;
    uint32_t inherited_bundle_identifier_mask;
    uint32_t inherited_property_mask;
    uint32_t instance_source_bit_mask;
    uint32_t instance_publication_mask;
    uint32_t instance_local_only_mask;
    uint32_t instance_property_mask;
    uint32_t attach_deferred_mask;
    uint32_t start_deferred_mask;
    uint32_t selected_registry_entry_mask;
    uint32_t rejected_unattached_mask;
    uint32_t property_inheritance_matrix;
    uint32_t platform_registry_entry_ordinal;
    uint32_t interrupt_registry_entry_ordinal;
    uint32_t timer_registry_entry_ordinal;
    uint32_t cpu_registry_entry_ordinal;
    uint32_t rejected_registry_entry_ordinal;
    uint32_t registry_entry_order_checksum;
    uint32_t registry_plane_hash;
    uint32_t property_inheritance_checksum;
    uint32_t expected_property_inheritance_checksum;
    uint32_t registry_entry_candidate_count;
    uint32_t selected_entry_count;
    uint32_t rejected_entry_count;
    uint32_t registry_entry_dryrun_count;
    uint32_t inherited_property_dryrun_count;
    uint32_t instance_property_dryrun_count;
    uint32_t attach_deferred_count;
    uint32_t start_deferred_count;
    uint32_t rejected_unattached_count;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_catalog_runtime_exec;
    uint32_t no_provider_runtime_exec;
    uint32_t no_registry_entry_runtime_exec;
    uint32_t no_property_runtime_exec;
    uint32_t no_attach_runtime_exec;
    uint32_t no_start_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_ENTRY_COUNT \
    STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_CANDIDATE_COUNT \
    STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_CANDIDATE_COUNT
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_COUNT \
    STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_COUNT
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL \
    STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_PLATFORM_ORDINAL
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL \
    STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_INTERRUPT_ORDINAL
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL \
    STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_TIMER_ORDINAL
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL \
    STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_CPU_ORDINAL
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL \
    STAGE84_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_ORDINAL
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_PLANE_HASH 0x494f4454u /* 'IODT' */
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_EXPECTED_TOPOLOGY_CHECKSUM 0xb6b0bbbbu

#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_SOURCE_PROPERTY_INHERITANCE 0x00000001u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PERSONALITY_NODE_SET 0x00000004u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_TOPOLOGY_MARKER_MATRIX 0x00000008u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_REGISTRY_PLANE_MATRIX 0x00000010u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PARENT_PATH_MATRIX 0x00000020u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_ENTRY_PATH_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PARENT_ORDINAL_MATRIX 0x00000080u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_SIBLING_ORDER_MATRIX 0x00000100u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PROVENANCE_MATRIX 0x00000200u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_SELECTED_TOPOLOGY_MATRIX 0x00000400u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_REJECT_UNLINKED 0x00000800u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_DRYRUN_COUNTS 0x00001000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_TOPOLOGY_CHECKSUM 0x00002000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_IOKIT_REFERENCE 0x00004000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00008000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PMAP_BOUNDARY 0x00010000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00020000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_REGISTRY_ENTRY_RUNTIME_EXEC 0x00040000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_REGISTRY_TOPOLOGY_RUNTIME_EXEC 0x00080000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_PROPERTY_RUNTIME_EXEC 0x00100000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_ATTACH_START_RUNTIME_EXEC 0x00200000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x00400000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x01000000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x02000000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x04000000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_PERSIST_WRITE 0x08000000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_LOCAL_ONLY 0x10000000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_FAIL_CLOSED 0x20000000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PATH_CHECKSUM 0x40000000u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_PERSONALITY 0x00000004u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_TOPOLOGY 0x00000008u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_REJECTED 0x00000010u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_COUNTS 0x00000020u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000040u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000080u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_ROLLUP 0x00000100u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_CHECKSUM 0x00000200u
#define STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_registry_topology_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_property_inheritance_status;
    uint32_t source_property_inheritance_satisfied_mask;
    uint32_t source_property_inheritance_failure_mask;
    uint32_t source_property_inheritance_checksum;
    uint32_t source_property_inheritance_property_checksum;
    uint32_t source_selected_registry_entry_mask;
    uint32_t source_rejected_unattached_mask;
    uint32_t source_inherited_property_mask;
    uint32_t source_instance_property_mask;
    uint32_t source_attach_deferred_mask;
    uint32_t source_start_deferred_mask;
    uint32_t source_property_inheritance_matrix;
    uint32_t source_catalog_status;
    uint32_t source_provider_status;
    uint32_t source_registry_status;
    uint32_t source_match_status;
    uint32_t source_scaffold_status;
    uint32_t source_pexpert_status;
    uint32_t source_pmap_transition_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t platform_personality_node_ptr;
    uint32_t platform_personality_prop_count;
    uint32_t interrupt_personality_node_ptr;
    uint32_t interrupt_personality_prop_count;
    uint32_t timer_personality_node_ptr;
    uint32_t timer_personality_prop_count;
    uint32_t cpu_personality_node_ptr;
    uint32_t cpu_personality_prop_count;
    uint32_t rejected_personality_node_ptr;
    uint32_t rejected_personality_prop_count;
    uint32_t topology_marker_mask;
    uint32_t registry_plane_match_mask;
    uint32_t parent_path_match_mask;
    uint32_t entry_path_match_mask;
    uint32_t parent_ordinal_match_mask;
    uint32_t sibling_order_match_mask;
    uint32_t provenance_match_mask;
    uint32_t selected_topology_entry_mask;
    uint32_t rejected_unlinked_mask;
    uint32_t topology_matrix;
    uint32_t platform_parent_ordinal;
    uint32_t interrupt_parent_ordinal;
    uint32_t timer_parent_ordinal;
    uint32_t cpu_parent_ordinal;
    uint32_t rejected_parent_ordinal;
    uint32_t platform_sibling_order;
    uint32_t interrupt_sibling_order;
    uint32_t timer_sibling_order;
    uint32_t cpu_sibling_order;
    uint32_t rejected_sibling_order;
    uint32_t topology_order_checksum;
    uint32_t parent_order_checksum;
    uint32_t path_checksum;
    uint32_t registry_plane_hash;
    uint32_t topology_checksum;
    uint32_t expected_topology_checksum;
    uint32_t topology_candidate_count;
    uint32_t selected_topology_entry_count;
    uint32_t rejected_topology_entry_count;
    uint32_t topology_dryrun_count;
    uint32_t topology_relationship_count;
    uint32_t rejected_unlinked_count;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_catalog_runtime_exec;
    uint32_t no_provider_runtime_exec;
    uint32_t no_registry_entry_runtime_exec;
    uint32_t no_registry_topology_runtime_exec;
    uint32_t no_property_runtime_exec;
    uint32_t no_attach_runtime_exec;
    uint32_t no_start_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_ENTRY_COUNT \
    STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_ENTRY_COUNT
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_CANDIDATE_COUNT \
    STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_CANDIDATE_COUNT
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_REJECTED_COUNT \
    STAGE84_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_COUNT
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_PATH_HASH 0x41535452u /* 'ASTR' */
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_EXPECTED_CHECKSUM 0xf7e3efe7u

#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_SOURCE_TOPOLOGY 0x00000001u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_PERSONALITY_NODE_SET 0x00000004u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_READINESS_MATRIX 0x00000008u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_START_READINESS_MATRIX 0x00000010u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_PROVIDER_PATH_MATRIX 0x00000020u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_START_PROVIDER_PATH_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_PROVIDER_ORDINAL_MATRIX 0x00000080u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_START_PROVIDER_ORDINAL_MATRIX 0x00000100u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_ORDER_MATRIX 0x00000200u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_START_ORDER_MATRIX 0x00000400u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_PROVIDER_START_DEPENDENCY 0x00000800u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_SOURCE_DEFERRED 0x00001000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_SELECTED_ATTACH_START_MATRIX 0x00002000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_REJECT_UNATTACHED_UNSTARTED 0x00004000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_DRYRUN_COUNTS 0x00008000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_START_CHECKSUM 0x00010000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_IOKIT_REFERENCE 0x00020000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00040000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_PMAP_BOUNDARY 0x00080000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00100000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_ATTACH_RUNTIME_EXEC 0x00200000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_START_RUNTIME_EXEC 0x00400000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_REGISTRY_TOPOLOGY_RUNTIME_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x01000000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x02000000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x04000000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x08000000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x10000000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_PERSIST_WRITE 0x20000000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED 0x40000000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PERSONALITY 0x00000004u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_ATTACH 0x00000008u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_START 0x00000010u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PROVIDER 0x00000020u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_ORDER 0x00000040u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_REJECTED 0x00000080u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_COUNTS 0x00000100u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000200u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000400u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_ROLLUP 0x00000800u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_CHECKSUM 0x00001000u
#define STAGE84_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_attach_start_readiness_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_topology_status;
    uint32_t source_topology_satisfied_mask;
    uint32_t source_topology_failure_mask;
    uint32_t source_topology_checksum;
    uint32_t source_topology_topology_checksum;
    uint32_t source_selected_topology_entry_mask;
    uint32_t source_rejected_unlinked_mask;
    uint32_t source_topology_matrix;
    uint32_t source_property_inheritance_status;
    uint32_t source_catalog_status;
    uint32_t source_provider_status;
    uint32_t source_registry_status;
    uint32_t source_match_status;
    uint32_t source_scaffold_status;
    uint32_t source_pexpert_status;
    uint32_t source_pmap_transition_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t platform_personality_node_ptr;
    uint32_t platform_personality_prop_count;
    uint32_t interrupt_personality_node_ptr;
    uint32_t interrupt_personality_prop_count;
    uint32_t timer_personality_node_ptr;
    uint32_t timer_personality_prop_count;
    uint32_t cpu_personality_node_ptr;
    uint32_t cpu_personality_prop_count;
    uint32_t rejected_personality_node_ptr;
    uint32_t rejected_personality_prop_count;
    uint32_t attach_readiness_mask;
    uint32_t start_readiness_mask;
    uint32_t attach_provider_path_match_mask;
    uint32_t start_provider_path_match_mask;
    uint32_t attach_provider_ordinal_match_mask;
    uint32_t start_provider_ordinal_match_mask;
    uint32_t attach_order_match_mask;
    uint32_t start_order_match_mask;
    uint32_t provider_start_required_mask;
    uint32_t provider_start_satisfied_mask;
    uint32_t attach_runtime_blocked_mask;
    uint32_t start_runtime_blocked_mask;
    uint32_t attach_start_provenance_match_mask;
    uint32_t source_attach_deferred_mask;
    uint32_t source_start_deferred_mask;
    uint32_t selected_attach_start_entry_mask;
    uint32_t rejected_attach_start_mask;
    uint32_t attach_start_matrix;
    uint32_t platform_attach_provider_ordinal;
    uint32_t platform_start_provider_ordinal;
    uint32_t interrupt_attach_provider_ordinal;
    uint32_t interrupt_start_provider_ordinal;
    uint32_t timer_attach_provider_ordinal;
    uint32_t timer_start_provider_ordinal;
    uint32_t cpu_attach_provider_ordinal;
    uint32_t cpu_start_provider_ordinal;
    uint32_t platform_attach_order;
    uint32_t interrupt_attach_order;
    uint32_t timer_attach_order;
    uint32_t cpu_attach_order;
    uint32_t rejected_attach_order;
    uint32_t platform_start_order;
    uint32_t interrupt_start_order;
    uint32_t timer_start_order;
    uint32_t cpu_start_order;
    uint32_t rejected_start_order;
    uint32_t attach_order_checksum;
    uint32_t start_order_checksum;
    uint32_t attach_start_path_hash;
    uint32_t attach_start_checksum;
    uint32_t expected_attach_start_checksum;
    uint32_t attach_start_candidate_count;
    uint32_t selected_attach_start_entry_count;
    uint32_t rejected_attach_start_entry_count;
    uint32_t attach_readiness_count;
    uint32_t start_readiness_count;
    uint32_t provider_start_required_count;
    uint32_t provider_start_satisfied_count;
    uint32_t rejected_unattached_unstarted_count;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_catalog_runtime_exec;
    uint32_t no_provider_runtime_exec;
    uint32_t no_registry_entry_runtime_exec;
    uint32_t no_registry_topology_runtime_exec;
    uint32_t no_property_runtime_exec;
    uint32_t no_attach_runtime_exec;
    uint32_t no_start_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_ENTRY_COUNT \
    STAGE84_XNU_IOKIT_ATTACH_START_READINESS_ENTRY_COUNT
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_CANDIDATE_COUNT \
    STAGE84_XNU_IOKIT_ATTACH_START_READINESS_CANDIDATE_COUNT
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_REJECTED_COUNT \
    STAGE84_XNU_IOKIT_ATTACH_START_READINESS_REJECTED_COUNT
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_PATH_HASH 0x4c465253u /* 'LFRS' */
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_EXPECTED_CHECKSUM 0xbba5bdb5u

#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_SOURCE_ATTACH_START 0x00000001u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PERSONALITY_NODE_SET 0x00000004u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_MATCHED_MATRIX 0x00000008u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PROVIDER_PUBLISHED_MATRIX 0x00000010u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_REGISTRY_LINKED_MATRIX 0x00000020u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_TOPOLOGY_LINKED_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_ATTACH_START_SOURCE_MATRIX 0x00000080u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_REGISTER_SERVICE_READINESS 0x00000100u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_SERVICE_REGISTERED 0x00000200u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NOTIFICATION_READY 0x00000400u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_ORDER_MATRIX 0x00000800u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PROVIDER_DEPENDENCY 0x00001000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_RUNTIME_BLOCKED 0x00002000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PROVENANCE 0x00004000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_SELECTED_LIFECYCLE_MATRIX 0x00008000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_REJECT_UNREGISTERED 0x00010000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_DRYRUN_COUNTS 0x00020000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_LIFECYCLE_CHECKSUM 0x00040000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_IOKIT_REFERENCE 0x00080000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00100000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PMAP_BOUNDARY 0x00200000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00400000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_REGISTER_SERVICE_RUNTIME_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_NOTIFICATION_RUNTIME_EXEC 0x01000000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x02000000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x04000000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x08000000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x10000000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x20000000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED 0x40000000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_PERSONALITY 0x00000004u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_LIFECYCLE 0x00000008u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_REGISTER_SERVICE 0x00000010u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_NOTIFICATION 0x00000020u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_ORDER_DEPENDENCY 0x00000040u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_REJECTED 0x00000080u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_COUNTS 0x00000100u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000200u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000400u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_ROLLUP 0x00000800u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_CHECKSUM 0x00001000u
#define STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_attach_start_status;
    uint32_t source_attach_start_satisfied_mask;
    uint32_t source_attach_start_failure_mask;
    uint32_t source_attach_start_contract_checksum;
    uint32_t source_attach_start_checksum;
    uint32_t source_selected_attach_start_entry_mask;
    uint32_t source_rejected_attach_start_mask;
    uint32_t source_attach_start_matrix;
    uint32_t source_attach_readiness_mask;
    uint32_t source_start_readiness_mask;
    uint32_t source_topology_status;
    uint32_t source_property_inheritance_status;
    uint32_t source_catalog_status;
    uint32_t source_provider_status;
    uint32_t source_registry_status;
    uint32_t source_match_status;
    uint32_t source_scaffold_status;
    uint32_t source_pexpert_status;
    uint32_t source_pmap_transition_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t platform_personality_node_ptr;
    uint32_t platform_personality_prop_count;
    uint32_t interrupt_personality_node_ptr;
    uint32_t interrupt_personality_prop_count;
    uint32_t timer_personality_node_ptr;
    uint32_t timer_personality_prop_count;
    uint32_t cpu_personality_node_ptr;
    uint32_t cpu_personality_prop_count;
    uint32_t rejected_personality_node_ptr;
    uint32_t rejected_personality_prop_count;
    uint32_t matched_mask;
    uint32_t provider_published_mask;
    uint32_t registry_linked_mask;
    uint32_t topology_linked_mask;
    uint32_t attach_readiness_mask;
    uint32_t start_readiness_mask;
    uint32_t register_service_readiness_mask;
    uint32_t service_registered_mask;
    uint32_t notification_ready_mask;
    uint32_t dependency_ready_mask;
    uint32_t lifecycle_provenance_match_mask;
    uint32_t register_runtime_blocked_mask;
    uint32_t notification_runtime_blocked_mask;
    uint32_t register_order_match_mask;
    uint32_t notification_order_match_mask;
    uint32_t provider_dependency_match_mask;
    uint32_t register_provider_start_required_mask;
    uint32_t register_provider_start_satisfied_mask;
    uint32_t selected_lifecycle_entry_mask;
    uint32_t rejected_lifecycle_mask;
    uint32_t lifecycle_matrix;
    uint32_t platform_register_provider_ordinal;
    uint32_t interrupt_register_provider_ordinal;
    uint32_t timer_register_provider_ordinal;
    uint32_t cpu_register_provider_ordinal;
    uint32_t rejected_register_provider_ordinal;
    uint32_t platform_register_order;
    uint32_t interrupt_register_order;
    uint32_t timer_register_order;
    uint32_t cpu_register_order;
    uint32_t rejected_register_order;
    uint32_t platform_notification_order;
    uint32_t interrupt_notification_order;
    uint32_t timer_notification_order;
    uint32_t cpu_notification_order;
    uint32_t rejected_notification_order;
    uint32_t register_order_checksum;
    uint32_t notification_order_checksum;
    uint32_t lifecycle_path_hash;
    uint32_t lifecycle_checksum;
    uint32_t expected_lifecycle_checksum;
    uint32_t lifecycle_candidate_count;
    uint32_t selected_lifecycle_entry_count;
    uint32_t rejected_lifecycle_entry_count;
    uint32_t register_service_readiness_count;
    uint32_t service_registered_count;
    uint32_t notification_ready_count;
    uint32_t dependency_ready_count;
    uint32_t rejected_unregistered_count;
    uint32_t register_provider_required_count;
    uint32_t register_provider_satisfied_count;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_catalog_runtime_exec;
    uint32_t no_provider_runtime_exec;
    uint32_t no_registry_entry_runtime_exec;
    uint32_t no_registry_topology_runtime_exec;
    uint32_t no_property_runtime_exec;
    uint32_t no_attach_runtime_exec;
    uint32_t no_start_runtime_exec;
    uint32_t no_register_service_runtime_exec;
    uint32_t no_notification_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};


#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_ENTRY_COUNT     STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_ENTRY_COUNT
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_CANDIDATE_COUNT     STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_CANDIDATE_COUNT
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_REJECTED_COUNT     STAGE84_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_REJECTED_COUNT
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_PATH_HASH 0x504e5444u /* 'PNTD' */
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_EXPECTED_CHECKSUM 0xebebe9ffu

#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_SOURCE_LIFECYCLE 0x00000001u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PERSONALITY_NODE_SET 0x00000004u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PROVIDER_NOTIFICATION_READY 0x00000008u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_INTEREST_READY 0x00000010u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_DELIVERY_READY 0x00000020u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PROVIDER_PATH_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PROVIDER_ORDINAL_MATRIX 0x00000080u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_INTEREST_TYPE_MATRIX 0x00000100u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_EXPECTED_NOTIFY_MATRIX 0x00000200u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_DELIVERED_NOTIFY_MATRIX 0x00000400u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_ORDER_MATRIX 0x00000800u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_DEPENDENCY_MATRIX 0x00001000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_RUNTIME_BLOCKED 0x00002000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PROVENANCE 0x00004000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_SELECTED_NOTIFICATION_MATRIX 0x00008000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_REJECT_UNDELIVERED 0x00010000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_DRYRUN_COUNTS 0x00020000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NOTIFICATION_CHECKSUM 0x00040000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_IOKIT_REFERENCE 0x00080000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00100000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PMAP_BOUNDARY 0x00200000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00400000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_INTEREST_RUNTIME_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_DELIVERY_RUNTIME_EXEC 0x01000000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x02000000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x04000000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x08000000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x10000000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x20000000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED 0x40000000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PERSONALITY 0x00000004u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PROVIDER_NOTIFICATION 0x00000008u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_INTEREST 0x00000010u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_DELIVERY 0x00000020u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_ORDER_DEPENDENCY 0x00000040u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_REJECTED 0x00000080u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_COUNTS 0x00000100u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000200u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000400u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_ROLLUP 0x00000800u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_CHECKSUM 0x00001000u
#define STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_lifecycle_status;
    uint32_t source_lifecycle_satisfied_mask;
    uint32_t source_lifecycle_failure_mask;
    uint32_t source_lifecycle_contract_checksum;
    uint32_t source_lifecycle_checksum;
    uint32_t source_selected_lifecycle_entry_mask;
    uint32_t source_rejected_lifecycle_mask;
    uint32_t source_lifecycle_matrix;
    uint32_t source_notification_ready_mask;
    uint32_t source_register_service_readiness_mask;
    uint32_t source_service_registered_mask;
    uint32_t source_attach_start_status;
    uint32_t source_topology_status;
    uint32_t source_property_inheritance_status;
    uint32_t source_catalog_status;
    uint32_t source_provider_status;
    uint32_t source_registry_status;
    uint32_t source_match_status;
    uint32_t source_scaffold_status;
    uint32_t source_pexpert_status;
    uint32_t source_pmap_transition_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t platform_personality_node_ptr;
    uint32_t platform_personality_prop_count;
    uint32_t interrupt_personality_node_ptr;
    uint32_t interrupt_personality_prop_count;
    uint32_t timer_personality_node_ptr;
    uint32_t timer_personality_prop_count;
    uint32_t cpu_personality_node_ptr;
    uint32_t cpu_personality_prop_count;
    uint32_t rejected_personality_node_ptr;
    uint32_t rejected_personality_prop_count;
    uint32_t provider_notification_ready_mask;
    uint32_t interest_ready_mask;
    uint32_t delivery_ready_mask;
    uint32_t interest_provider_path_match_mask;
    uint32_t interest_provider_ordinal_match_mask;
    uint32_t interest_type_match_mask;
    uint32_t expected_notify_mask_match_mask;
    uint32_t delivered_notify_mask_match_mask;
    uint32_t notify_dependency_ready_mask;
    uint32_t interest_runtime_blocked_mask;
    uint32_t delivery_runtime_blocked_mask;
    uint32_t interest_order_match_mask;
    uint32_t delivery_order_match_mask;
    uint32_t notify_provenance_match_mask;
    uint32_t selected_provider_notification_entry_mask;
    uint32_t rejected_provider_notification_mask;
    uint32_t provider_notification_matrix;
    uint32_t platform_interest_provider_ordinal;
    uint32_t interrupt_interest_provider_ordinal;
    uint32_t timer_interest_provider_ordinal;
    uint32_t cpu_interest_provider_ordinal;
    uint32_t rejected_interest_provider_ordinal;
    uint32_t platform_interest_type_mask;
    uint32_t interrupt_interest_type_mask;
    uint32_t timer_interest_type_mask;
    uint32_t cpu_interest_type_mask;
    uint32_t rejected_interest_type_mask;
    uint32_t platform_expected_notify_mask;
    uint32_t interrupt_expected_notify_mask;
    uint32_t timer_expected_notify_mask;
    uint32_t cpu_expected_notify_mask;
    uint32_t rejected_expected_notify_mask;
    uint32_t platform_delivered_notify_mask;
    uint32_t interrupt_delivered_notify_mask;
    uint32_t timer_delivered_notify_mask;
    uint32_t cpu_delivered_notify_mask;
    uint32_t rejected_delivered_notify_mask;
    uint32_t platform_interest_order;
    uint32_t interrupt_interest_order;
    uint32_t timer_interest_order;
    uint32_t cpu_interest_order;
    uint32_t rejected_interest_order;
    uint32_t platform_delivery_order;
    uint32_t interrupt_delivery_order;
    uint32_t timer_delivery_order;
    uint32_t cpu_delivery_order;
    uint32_t rejected_delivery_order;
    uint32_t interest_order_checksum;
    uint32_t delivery_order_checksum;
    uint32_t notify_mask_checksum;
    uint32_t provider_notification_path_hash;
    uint32_t provider_notification_checksum;
    uint32_t expected_provider_notification_checksum;
    uint32_t provider_notification_candidate_count;
    uint32_t selected_provider_notification_entry_count;
    uint32_t rejected_provider_notification_entry_count;
    uint32_t provider_notification_ready_count;
    uint32_t interest_ready_count;
    uint32_t delivery_ready_count;
    uint32_t notify_dependency_ready_count;
    uint32_t rejected_undelivered_count;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_catalog_runtime_exec;
    uint32_t no_provider_runtime_exec;
    uint32_t no_registry_entry_runtime_exec;
    uint32_t no_registry_topology_runtime_exec;
    uint32_t no_property_runtime_exec;
    uint32_t no_attach_runtime_exec;
    uint32_t no_start_runtime_exec;
    uint32_t no_register_service_runtime_exec;
    uint32_t no_notification_runtime_exec;
    uint32_t no_interest_runtime_exec;
    uint32_t no_delivery_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_ENTRY_COUNT \
    STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_ENTRY_COUNT
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_CANDIDATE_COUNT \
    STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_CANDIDATE_COUNT
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_REJECTED_COUNT \
    STAGE84_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_REJECTED_COUNT
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_PATH_HASH 0x50434243u /* 'PCBC' */
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_EXPECTED_CHECKSUM 0xbba8abbdu

#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_SOURCE_PROVIDER_NOTIFICATION 0x00000001u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PERSONALITY_NODE_SET 0x00000004u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PROVIDER_CALLBACK_READY 0x00000008u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_CLIENT_NOTIFICATION_READY 0x00000010u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PROVIDER_PATH_MATRIX 0x00000020u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PROVIDER_ORDINAL_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_CALLBACK_TYPE_MATRIX 0x00000080u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_EXPECTED_CALLBACK_MATRIX 0x00000100u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_DELIVERED_CALLBACK_MATRIX 0x00000200u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_CLIENT_ACK_MATRIX 0x00000400u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_ORDER_MATRIX 0x00000800u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_DEPENDENCY_MATRIX 0x00001000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_RUNTIME_BLOCKED 0x00002000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PROVENANCE 0x00004000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_SELECTED_CALLBACK_MATRIX 0x00008000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_REJECT_NO_CALLBACK 0x00010000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_DRYRUN_COUNTS 0x00020000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_CALLBACK_CHECKSUM 0x00040000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_IOKIT_REFERENCE 0x00080000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00100000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PMAP_BOUNDARY 0x00200000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00400000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_CALLBACK_RUNTIME_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_CLIENT_NOTIFICATION_RUNTIME_EXEC 0x01000000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x02000000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x04000000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x08000000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_TLB_CACHE_CHANGE 0x10000000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x20000000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED 0x40000000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_PERSONALITY 0x00000004u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK 0x00000008u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CLIENT_NOTIFICATION 0x00000010u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_ACK 0x00000020u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_ORDER_DEPENDENCY 0x00000040u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_REJECTED 0x00000080u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_COUNTS 0x00000100u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000200u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000400u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_ROLLUP 0x00000800u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CHECKSUM 0x00001000u
#define STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_provider_notification_status;
    uint32_t source_provider_notification_satisfied_mask;
    uint32_t source_provider_notification_failure_mask;
    uint32_t source_provider_notification_contract_checksum;
    uint32_t source_provider_notification_checksum;
    uint32_t source_selected_provider_notification_entry_mask;
    uint32_t source_rejected_provider_notification_mask;
    uint32_t source_provider_notification_matrix;
    uint32_t source_provider_notification_ready_mask;
    uint32_t source_interest_ready_mask;
    uint32_t source_delivery_ready_mask;
    uint32_t source_no_interest_runtime_exec;
    uint32_t source_no_delivery_runtime_exec;
    uint32_t source_lifecycle_status;
    uint32_t source_attach_start_status;
    uint32_t source_topology_status;
    uint32_t source_property_inheritance_status;
    uint32_t source_catalog_status;
    uint32_t source_provider_status;
    uint32_t source_registry_status;
    uint32_t source_match_status;
    uint32_t source_scaffold_status;
    uint32_t source_pexpert_status;
    uint32_t source_pmap_transition_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t platform_personality_node_ptr;
    uint32_t platform_personality_prop_count;
    uint32_t interrupt_personality_node_ptr;
    uint32_t interrupt_personality_prop_count;
    uint32_t timer_personality_node_ptr;
    uint32_t timer_personality_prop_count;
    uint32_t cpu_personality_node_ptr;
    uint32_t cpu_personality_prop_count;
    uint32_t rejected_personality_node_ptr;
    uint32_t rejected_personality_prop_count;
    uint32_t provider_callback_ready_mask;
    uint32_t client_notification_ready_mask;
    uint32_t callback_provider_path_match_mask;
    uint32_t callback_provider_ordinal_match_mask;
    uint32_t callback_type_match_mask;
    uint32_t expected_callback_mask_match_mask;
    uint32_t delivered_callback_mask_match_mask;
    uint32_t client_ack_mask_match_mask;
    uint32_t callback_dependency_ready_mask;
    uint32_t callback_runtime_blocked_mask;
    uint32_t client_notification_runtime_blocked_mask;
    uint32_t callback_order_match_mask;
    uint32_t client_notification_order_match_mask;
    uint32_t callback_provenance_match_mask;
    uint32_t selected_provider_callback_entry_mask;
    uint32_t rejected_provider_callback_mask;
    uint32_t provider_callback_client_notification_matrix;
    uint32_t platform_callback_provider_ordinal;
    uint32_t interrupt_callback_provider_ordinal;
    uint32_t timer_callback_provider_ordinal;
    uint32_t cpu_callback_provider_ordinal;
    uint32_t rejected_callback_provider_ordinal;
    uint32_t platform_callback_type_mask;
    uint32_t interrupt_callback_type_mask;
    uint32_t timer_callback_type_mask;
    uint32_t cpu_callback_type_mask;
    uint32_t rejected_callback_type_mask;
    uint32_t platform_expected_callback_mask;
    uint32_t interrupt_expected_callback_mask;
    uint32_t timer_expected_callback_mask;
    uint32_t cpu_expected_callback_mask;
    uint32_t rejected_expected_callback_mask;
    uint32_t platform_delivered_callback_mask;
    uint32_t interrupt_delivered_callback_mask;
    uint32_t timer_delivered_callback_mask;
    uint32_t cpu_delivered_callback_mask;
    uint32_t rejected_delivered_callback_mask;
    uint32_t platform_client_ack_mask;
    uint32_t interrupt_client_ack_mask;
    uint32_t timer_client_ack_mask;
    uint32_t cpu_client_ack_mask;
    uint32_t rejected_client_ack_mask;
    uint32_t platform_callback_order;
    uint32_t interrupt_callback_order;
    uint32_t timer_callback_order;
    uint32_t cpu_callback_order;
    uint32_t rejected_callback_order;
    uint32_t platform_client_notification_order;
    uint32_t interrupt_client_notification_order;
    uint32_t timer_client_notification_order;
    uint32_t cpu_client_notification_order;
    uint32_t rejected_client_notification_order;
    uint32_t callback_order_checksum;
    uint32_t client_notification_order_checksum;
    uint32_t callback_mask_checksum;
    uint32_t provider_callback_path_hash;
    uint32_t provider_callback_checksum;
    uint32_t expected_provider_callback_checksum;
    uint32_t provider_callback_candidate_count;
    uint32_t selected_provider_callback_entry_count;
    uint32_t rejected_provider_callback_entry_count;
    uint32_t provider_callback_ready_count;
    uint32_t client_notification_ready_count;
    uint32_t callback_dependency_ready_count;
    uint32_t client_ack_ready_count;
    uint32_t rejected_no_callback_count;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_catalog_runtime_exec;
    uint32_t no_provider_runtime_exec;
    uint32_t no_registry_entry_runtime_exec;
    uint32_t no_registry_topology_runtime_exec;
    uint32_t no_property_runtime_exec;
    uint32_t no_attach_runtime_exec;
    uint32_t no_start_runtime_exec;
    uint32_t no_register_service_runtime_exec;
    uint32_t no_notification_runtime_exec;
    uint32_t no_interest_runtime_exec;
    uint32_t no_delivery_runtime_exec;
    uint32_t no_callback_runtime_exec;
    uint32_t no_client_notification_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};


#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_ENTRY_COUNT \
    STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_ENTRY_COUNT
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_CANDIDATE_COUNT \
    STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_CANDIDATE_COUNT
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_REJECTED_COUNT \
    STAGE84_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_REJECTED_COUNT
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_PATH_HASH 0x4f50434cu /* 'OPCL' */
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_EXPECTED_CHECKSUM 0xf4f8e8f5u

#define STAGE84_XNU_ENTRY_STUB_VERSION 1u
#define STAGE84_XNU_ENTRY_STUB_MAGIC 0x58535442u /* 'XSTB' */
#define STAGE84_XNU_ENTRY_STUB_RETURN_BAD_ARG 0x8000bad2u

#define STAGE84_XNU_ENTRY_STUB_SAT_START_CALLED       0x00000001u
#define STAGE84_XNU_ENTRY_STUB_SAT_START_ENTERED      0x00000002u
#define STAGE84_XNU_ENTRY_STUB_SAT_ARM_INIT_CALLED    0x00000004u
#define STAGE84_XNU_ENTRY_STUB_SAT_ARM_INIT_RETURNED  0x00000008u
#define STAGE84_XNU_ENTRY_STUB_SAT_RETURN_STATUS      0x00000010u
#define STAGE84_XNU_ENTRY_STUB_SAT_BOOT_ARGS_VALID    0x00000020u
#define STAGE84_XNU_ENTRY_STUB_SAT_DEVICE_TREE_VALID  0x00000040u
#define STAGE84_XNU_ENTRY_STUB_SAT_MAGIC              0x00000080u
#define STAGE84_XNU_ENTRY_STUB_SAT_OUTPUT             0x00000100u
#define STAGE84_XNU_ENTRY_STUB_SAT_MMU_UNCHANGED      0x00000200u
#define STAGE84_XNU_ENTRY_STUB_SAT_NO_EXCEPTION       0x00000400u
#define STAGE84_XNU_ENTRY_STUB_SAT_SAFETY_BOUNDARY    0x00000800u
#define STAGE84_XNU_ENTRY_STUB_SAT_EARLY_INIT_OK      0x00001000u
#define STAGE84_XNU_ENTRY_STUB_SAT_PE_INIT_PLATFORM_FALSE_OK 0x00002000u
#define STAGE84_XNU_ENTRY_STUB_SAT_ARM_INIT_POST_PE_BOOTSTRAP_OK 0x00004000u
#define STAGE84_XNU_ENTRY_STUB_SAT_ARM_VM_INIT_FULL_PMAP_OK 0x00008000u
#define STAGE84_XNU_ENTRY_STUB_REQUIRED_MASK          0x0000ffffu

#define STAGE84_XNU_ENTRY_STUB_FAIL_START_NOT_CALLED      0x00000001u
#define STAGE84_XNU_ENTRY_STUB_FAIL_START_NOT_ENTERED     0x00000002u
#define STAGE84_XNU_ENTRY_STUB_FAIL_ARM_INIT_NOT_CALLED   0x00000004u
#define STAGE84_XNU_ENTRY_STUB_FAIL_ARM_INIT_NOT_RETURNED 0x00000008u
#define STAGE84_XNU_ENTRY_STUB_FAIL_BAD_RETURN_STATUS     0x00000010u
#define STAGE84_XNU_ENTRY_STUB_FAIL_BAD_BOOT_ARGS         0x00000020u
#define STAGE84_XNU_ENTRY_STUB_FAIL_BAD_DEVICE_TREE       0x00000040u
#define STAGE84_XNU_ENTRY_STUB_FAIL_BAD_MAGIC             0x00000080u
#define STAGE84_XNU_ENTRY_STUB_FAIL_NO_OUTPUT             0x00000100u
#define STAGE84_XNU_ENTRY_STUB_FAIL_MMU_CHANGED           0x00000200u
#define STAGE84_XNU_ENTRY_STUB_FAIL_EXCEPTION             0x00000400u
#define STAGE84_XNU_ENTRY_STUB_FAIL_SAFETY_BOUNDARY       0x00000800u
#define STAGE84_XNU_ENTRY_STUB_FAIL_EARLY_INIT             0x00001000u
#define STAGE84_XNU_ENTRY_STUB_FAIL_PE_INIT_PLATFORM_FALSE 0x00002000u
#define STAGE84_XNU_ENTRY_STUB_FAIL_ARM_INIT_POST_PE_BOOTSTRAP 0x00004000u
#define STAGE84_XNU_ENTRY_STUB_FAIL_ARM_VM_INIT_FULL_PMAP   0x00008000u

struct stage84_xnu_entry_stub_result {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t start_stub_function_ptr;
    uint32_t arm_init_stub_function_ptr;
    uint32_t result_ptr;
    uint32_t magic_expected;
    uint32_t magic;
    uint32_t start_called;
    uint32_t start_entered;
    uint32_t arm_init_called;
    uint32_t arm_init_returned;
    uint32_t return_value;
    uint32_t arm_init_status;
    uint32_t boot_args_ptr;
    uint32_t boot_args_rev_ver;
    uint32_t boot_args_phys_base;
    uint32_t boot_args_mem_size;
    uint32_t boot_args_top_of_kernel_data;
    uint32_t boot_args_machine_type;
    uint32_t boot_args_device_tree_ptr;
    uint32_t boot_args_device_tree_length;
    uint32_t boot_args_valid;
    uint32_t device_tree_valid;
    uint32_t output_lines;
    uint32_t output_bytes;
    uint32_t ttbr0_before;
    uint32_t ttbr0_after;
    uint32_t sctlr_before;
    uint32_t sctlr_after;
    uint32_t mmu_state_unchanged;
    uint32_t no_exception_observed;
    uint32_t public_xnu_start_executed;
    uint32_t public_arm_init_executed;
    uint32_t public_pmap_runtime_executed;
    uint32_t public_iokit_runtime_executed;
    uint32_t generated_macho_executed;
    uint32_t live_pmap_installed;
    uint32_t tlb_invalidated;
    uint32_t cache_policy_changed;
    uint32_t persistent_write_attempted;
    uint32_t safety_boundary_preserved;
    uint32_t early_pmap_platform_init_called;
    uint32_t early_pmap_platform_init_returned;
    uint32_t early_pmap_platform_init_status;
    uint32_t early_pmap_platform_init_checksum;
    uint32_t pe_init_platform_false_called;
    uint32_t pe_init_platform_false_returned;
    uint32_t pe_init_platform_false_status;
    uint32_t pe_init_platform_false_checksum;
    uint32_t arm_init_post_pe_bootstrap_called;
    uint32_t arm_init_post_pe_bootstrap_returned;
    uint32_t arm_init_post_pe_bootstrap_status;
    uint32_t arm_init_post_pe_bootstrap_checksum;
    uint32_t arm_vm_init_full_pmap_called;
    uint32_t arm_vm_init_full_pmap_returned;
    uint32_t arm_vm_init_full_pmap_status;
    uint32_t arm_vm_init_full_pmap_checksum;
    uint32_t checksum;
};

#define STAGE84_XNU_EARLY_INIT_VERSION 1u
#define STAGE84_XNU_EARLY_INIT_REQUIRED_MASK 0x01ffffffu

#define STAGE84_XNU_EARLY_INIT_SAT_BOOT_ARGS_VALID          0x00000001u
#define STAGE84_XNU_EARLY_INIT_SAT_DEVICE_TREE_VALID        0x00000002u
#define STAGE84_XNU_EARLY_INIT_SAT_SOURCE_ENTRY_ACTIVE      0x00000004u
#define STAGE84_XNU_EARLY_INIT_SAT_SOURCE_PMAP_TRANSITION   0x00000008u
#define STAGE84_XNU_EARLY_INIT_SAT_SOURCE_PLATFORM_FACTS    0x00000010u
#define STAGE84_XNU_EARLY_INIT_SAT_CONTROL_REGS_SAMPLED     0x00000020u
#define STAGE84_XNU_EARLY_INIT_SAT_CONTROL_REGS_UNCHANGED   0x00000040u
#define STAGE84_XNU_EARLY_INIT_SAT_CANDIDATE_L1_IMPORTED    0x00000080u
#define STAGE84_XNU_EARLY_INIT_SAT_PROPOSED_REG_PLAN        0x00000100u
#define STAGE84_XNU_EARLY_INIT_SAT_TRANSLATION_CONTINUITY   0x00000200u
#define STAGE84_XNU_EARLY_INIT_SAT_RECOVERY_CONTINUITY      0x00000400u
#define STAGE84_XNU_EARLY_INIT_SAT_PE_STATE_VALIDATED       0x00000800u
#define STAGE84_XNU_EARLY_INIT_SAT_PLATFORM_FACTS_MATCHED   0x00001000u
#define STAGE84_XNU_EARLY_INIT_SAT_LOCAL_ONLY               0x00002000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_PUBLIC_XNU_ENTRY      0x00004000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_PUBLIC_PMAP_RUNTIME   0x00008000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_PUBLIC_PEXPERT        0x00010000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_PUBLIC_IOKIT          0x00020000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_MACHO_EXEC            0x00040000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_LIVE_PMAP_INSTALL     0x00080000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_CONTROL_REG_WRITES    0x00100000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_TLB_INVALIDATE        0x00200000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_CACHE_POLICY_CHANGE   0x00400000u
#define STAGE84_XNU_EARLY_INIT_SAT_NO_PERSISTENT_WRITE      0x00800000u
#define STAGE84_XNU_EARLY_INIT_SAT_SAFETY_BOUNDARY          0x01000000u

#define STAGE84_XNU_EARLY_INIT_FAIL_BOOT_ARGS               0x00000001u
#define STAGE84_XNU_EARLY_INIT_FAIL_DEVICE_TREE             0x00000002u
#define STAGE84_XNU_EARLY_INIT_FAIL_SOURCE_ENTRY            0x00000004u
#define STAGE84_XNU_EARLY_INIT_FAIL_PMAP_TRANSITION         0x00000008u
#define STAGE84_XNU_EARLY_INIT_FAIL_PLATFORM_FACTS          0x00000010u
#define STAGE84_XNU_EARLY_INIT_FAIL_CONTROL_REG_SAMPLE      0x00000020u
#define STAGE84_XNU_EARLY_INIT_FAIL_CONTROL_REG_CHANGED     0x00000040u
#define STAGE84_XNU_EARLY_INIT_FAIL_CANDIDATE_L1            0x00000080u
#define STAGE84_XNU_EARLY_INIT_FAIL_PROPOSED_REG_PLAN       0x00000100u
#define STAGE84_XNU_EARLY_INIT_FAIL_TRANSLATION             0x00000200u
#define STAGE84_XNU_EARLY_INIT_FAIL_RECOVERY                0x00000400u
#define STAGE84_XNU_EARLY_INIT_FAIL_PE_STATE                0x00000800u
#define STAGE84_XNU_EARLY_INIT_FAIL_PUBLIC_RUNTIME          0x00001000u
#define STAGE84_XNU_EARLY_INIT_FAIL_MUTATION                0x00002000u
#define STAGE84_XNU_EARLY_INIT_FAIL_SAFETY_BOUNDARY         0x80000000u

struct stage84_xnu_early_pmap_platform_init_result {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t boot_args_ptr;
    uint32_t boot_args_rev_ver;
    uint32_t boot_args_valid;
    uint32_t device_tree_valid;
    uint32_t source_entry_status;
    uint32_t source_pmap_transition_status;
    uint32_t source_pmap_transition_satisfied_mask;
    uint32_t source_pmap_transition_failure_mask;
    uint32_t source_pmap_transition_checksum;
    uint32_t source_pe_state_valid;
    uint32_t ttbr0_before;
    uint32_t ttbr0_after;
    uint32_t ttbcr_before;
    uint32_t ttbcr_after;
    uint32_t dacr_before;
    uint32_t dacr_after;
    uint32_t sctlr_before;
    uint32_t sctlr_after;
    uint32_t control_registers_sampled;
    uint32_t control_registers_unchanged;
    uint32_t candidate_l1_base;
    uint32_t candidate_l1_limit;
    uint32_t candidate_l1_alignment;
    uint32_t proposed_ttbr0;
    uint32_t proposed_ttbcr;
    uint32_t proposed_dacr;
    uint32_t proposed_sctlr;
    uint32_t proposed_plan_readonly;
    uint32_t translation_kernel_pa;
    uint32_t translation_workspace_pa;
    uint32_t translation_ram_console_pa;
    uint32_t translation_device_pa;
    uint32_t translation_case_count;
    uint32_t translation_plan_reused;
    uint32_t recovery_continuity_inherited;
    uint32_t platform_memory_base;
    uint32_t platform_memory_size;
    uint32_t platform_cpu_count;
    uint32_t platform_gic_dist_base;
    uint32_t platform_gic_cpu_base;
    uint32_t platform_timer_base;
    uint32_t platform_timer_frequency;
    uint32_t platform_machine_type;
    uint32_t platform_vector_base;
    uint32_t platform_facts_valid;
    uint32_t stage_owned_local_only;
    uint32_t public_xnu_start_executed;
    uint32_t public_arm_init_executed;
    uint32_t public_pmap_runtime_executed;
    uint32_t public_pexpert_runtime_executed;
    uint32_t public_iokit_runtime_executed;
    uint32_t generated_macho_executed;
    uint32_t live_pmap_installed;
    uint32_t ttbr_written;
    uint32_t ttbcr_written;
    uint32_t dacr_written;
    uint32_t sctlr_written;
    uint32_t tlb_invalidated;
    uint32_t cache_policy_changed;
    uint32_t persistent_write_attempted;
    uint32_t safety_boundary_preserved;
    uint32_t checksum;
};


#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_VERSION 1u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_REQUIRED_MASK 0x01ffffffu

#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_SOURCE_EARLY_INIT_OK      0x00000001u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_BOOT_ARGS_VALID           0x00000002u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_DEVICE_TREE_VALID         0x00000004u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_PE_STATE_CAPTURED         0x00000008u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_PE_STATE_MATCHED          0x00000010u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_DTINIT_FACTS              0x00000020u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_APPLE_DT_ROOT             0x00000040u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_CHOSEN_BOOT_ARGS          0x00000080u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_MEMORY_FACTS              0x00000100u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_CPUS_FACTS                0x00000200u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_GIC_FACTS                 0x00000400u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_TIMER_FACTS               0x00000800u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_IDENTIFY_MACHINE_FACTS    0x00001000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_PLATFORM_FACTS_MATCHED    0x00002000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_PUBLIC_PEXPERT_BLOCKED    0x00004000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_CONTROL_REGS_SAMPLED      0x00008000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_CONTROL_REGS_UNCHANGED    0x00010000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_LOCAL_ONLY                0x00020000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_PUBLIC_XNU_ENTRY       0x00040000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_PUBLIC_PMAP_RUNTIME    0x00080000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_PUBLIC_PEXPERT_RUNTIME 0x00100000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_PUBLIC_IOKIT_RUNTIME   0x00200000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_MACHO_EXEC             0x00400000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_MUTATION               0x00800000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_SAT_SAFETY_BOUNDARY           0x01000000u

#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_SOURCE_EARLY_INIT        0x00000001u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_BOOT_ARGS                0x00000002u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_DEVICE_TREE              0x00000004u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PE_STATE_CAPTURE         0x00000008u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PE_STATE_MATCH           0x00000010u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_DTINIT_FACTS             0x00000020u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_APPLE_DT_ROOT            0x00000040u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_CHOSEN_BOOT_ARGS         0x00000080u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_MEMORY_FACTS             0x00000100u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_CPUS_FACTS               0x00000200u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_GIC_FACTS                0x00000400u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_TIMER_FACTS              0x00000800u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_IDENTIFY_MACHINE         0x00001000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PLATFORM_FACTS           0x00002000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_PEXPERT_BOUNDARY  0x00004000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_CONTROL_REG_SAMPLE       0x00008000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_CONTROL_REG_CHANGED      0x00010000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_LOCAL_ONLY               0x00020000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_XNU_ENTRY         0x00040000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_PMAP_RUNTIME      0x00080000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_PEXPERT_RUNTIME   0x00100000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_IOKIT_RUNTIME     0x00200000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_MACHO_EXEC               0x00400000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_MUTATION                 0x00800000u
#define STAGE84_XNU_PE_INIT_PLATFORM_FALSE_FAIL_SAFETY_BOUNDARY          0x01000000u

struct stage84_xnu_pe_init_platform_false_result {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_early_init_status;
    uint32_t source_early_init_satisfied_mask;
    uint32_t source_early_init_failure_mask;
    uint32_t source_early_init_checksum;
    uint32_t boot_args_ptr;
    uint32_t boot_args_rev_ver;
    uint32_t boot_args_phys_base;
    uint32_t boot_args_mem_size;
    uint32_t boot_args_machine_type;
    uint32_t boot_args_device_tree_ptr;
    uint32_t boot_args_device_tree_length;
    uint32_t boot_args_valid;
    uint32_t command_line_checksum;
    uint32_t command_line_stage84_marker;
    uint32_t command_line_pe_init_false_marker;
    uint32_t command_line_prevm_pexpert_marker;
    uint32_t command_line_dtinit_marker;
    uint32_t command_line_peid_marker;
    uint32_t command_line_no_public_markers;
    uint32_t device_tree_valid;
    uint32_t pe_boot_args_ptr;
    uint32_t pe_device_tree_head;
    uint32_t pe_device_tree_length;
    uint32_t pe_memory_base;
    uint32_t pe_memory_size;
    uint32_t pe_cpu_count;
    uint32_t pe_machine_type;
    uint32_t pe_vector_base;
    uint32_t pe_gic_dist_base;
    uint32_t pe_gic_cpu_base;
    uint32_t pe_timer_base;
    uint32_t pe_timer_frequency;
    uint32_t pe_state_valid;
    uint32_t pe_state_matched;
    uint32_t dtinit_device_tree_head;
    uint32_t dtinit_device_tree_length;
    uint32_t dtinit_root_parseable;
    uint32_t dtinit_no_public_call;
    uint32_t apple_dt_root_prop_count;
    uint32_t apple_dt_root_child_count;
    uint32_t apple_dt_device_tree_present;
    uint32_t apple_dt_device_tree_prop_count;
    uint32_t apple_dt_chosen_present;
    uint32_t apple_dt_boot_args_present;
    uint32_t apple_dt_boot_args_checksum;
    uint32_t apple_dt_memory_present;
    uint32_t apple_dt_memory_base;
    uint32_t apple_dt_memory_size;
    uint32_t apple_dt_cpus_present;
    uint32_t apple_dt_cpu_count;
    uint32_t apple_dt_gic_present;
    uint32_t apple_dt_gic_dist_base;
    uint32_t apple_dt_gic_cpu_base;
    uint32_t apple_dt_gic_interrupt_cells;
    uint32_t apple_dt_timer_present;
    uint32_t apple_dt_timer_base;
    uint32_t apple_dt_timer_frequency;
    uint32_t apple_dt_semantic_mask;
    uint32_t identify_model_checksum;
    uint32_t identify_compatible_checksum;
    uint32_t identify_target_type_checksum;
    uint32_t identify_model_matched;
    uint32_t identify_compatible_matched;
    uint32_t identify_target_type_matched;
    uint32_t identify_msm8974_matched;
    uint32_t identify_cancro_matched;
    uint32_t identify_machine_no_public_call;
    uint32_t platform_facts_matched;
    uint32_t public_pe_init_platform_executed;
    uint32_t public_dtinit_executed;
    uint32_t public_pe_identify_machine_executed;
    uint32_t public_xnu_start_executed;
    uint32_t public_arm_init_executed;
    uint32_t public_pmap_runtime_executed;
    uint32_t public_pexpert_runtime_executed;
    uint32_t public_iokit_runtime_executed;
    uint32_t generated_macho_executed;
    uint32_t ttbr0_before;
    uint32_t ttbr0_after;
    uint32_t ttbcr_before;
    uint32_t ttbcr_after;
    uint32_t dacr_before;
    uint32_t dacr_after;
    uint32_t sctlr_before;
    uint32_t sctlr_after;
    uint32_t control_registers_sampled;
    uint32_t control_registers_unchanged;
    uint32_t live_pmap_installed;
    uint32_t ttbr_written;
    uint32_t ttbcr_written;
    uint32_t dacr_written;
    uint32_t sctlr_written;
    uint32_t tlb_invalidated;
    uint32_t cache_policy_changed;
    uint32_t persistent_write_attempted;
    uint32_t stage_owned_local_only;
    uint32_t safety_boundary_preserved;
    uint32_t checksum;
};

#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_VERSION 1u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_REQUIRED_MASK 0x01ffffffu

#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_SOURCE_PE_INIT_OK       0x00000001u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_BOOT_ARGS_VALID         0x00000002u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_PE_STATE_INHERITED      0x00000004u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CPU_TOPOLOGY            0x00000008u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_MASTER_CPU              0x00000010u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_BOOT_CPU_DATA           0x00000020u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CPU_DATA_ENTRIES        0x00000040u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_STACK_FACTS             0x00000080u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_THREAD_BOOTSTRAP        0x00000100u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CPU_BOOTSTRAP           0x00000200u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_RTCLOCK_CALLBACK        0x00000400u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_KERNEL_CPU_PROCESSOR    0x00000800u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_BOOT_ARG_PARSE          0x00001000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_STOP_BEFORE_ARM_VM      0x00002000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_PUBLIC_BOOTSTRAP_BLOCKED 0x00004000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_PUBLIC_RTCLOCK_BLOCKED  0x00008000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CONTROL_REGS_SAMPLED    0x00010000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CONTROL_REGS_UNCHANGED  0x00020000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_LOCAL_ONLY              0x00040000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_PUBLIC_XNU_ENTRY     0x00080000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_ARM_VM_PMAP_RUNTIME  0x00100000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_PEXPERT_IOKIT_RUNTIME 0x00200000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_MACHO_EXEC           0x00400000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_MUTATION             0x00800000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_SAFETY_BOUNDARY         0x01000000u

#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_SOURCE_PE_INIT         0x00000001u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_BOOT_ARGS              0x00000002u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PE_STATE               0x00000004u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CPU_TOPOLOGY           0x00000008u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_MASTER_CPU             0x00000010u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_BOOT_CPU_DATA          0x00000020u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CPU_DATA_ENTRIES       0x00000040u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_STACK_FACTS            0x00000080u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_THREAD_BOOTSTRAP       0x00000100u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CPU_BOOTSTRAP          0x00000200u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_RTCLOCK_CALLBACK       0x00000400u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_KERNEL_CPU_PROCESSOR   0x00000800u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_BOOT_ARG_PARSE         0x00001000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_STOP_BEFORE_ARM_VM     0x00002000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PUBLIC_BOOTSTRAP       0x00004000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PUBLIC_RTCLOCK         0x00008000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CONTROL_REG_SAMPLE     0x00010000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CONTROL_REG_CHANGED    0x00020000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_LOCAL_ONLY             0x00040000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PUBLIC_XNU_ENTRY       0x00080000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_ARM_VM_PMAP_RUNTIME    0x00100000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PEXPERT_IOKIT_RUNTIME  0x00200000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_MACHO_EXEC             0x00400000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_MUTATION               0x00800000u
#define STAGE84_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_SAFETY_BOUNDARY        0x01000000u

struct stage84_xnu_arm_init_post_pe_bootstrap_result {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_pe_init_status;
    uint32_t source_pe_init_satisfied_mask;
    uint32_t source_pe_init_failure_mask;
    uint32_t source_pe_init_checksum;
    uint32_t source_early_init_status;
    uint32_t source_early_init_checksum;
    uint32_t boot_args_ptr;
    uint32_t boot_args_rev_ver;
    uint32_t boot_args_phys_base;
    uint32_t boot_args_mem_size;
    uint32_t boot_args_machine_type;
    uint32_t boot_args_device_tree_ptr;
    uint32_t boot_args_device_tree_length;
    uint32_t boot_args_valid;
    uint32_t command_line_checksum;
    uint32_t command_line_stage84_marker;
    uint32_t command_line_postpe_marker;
    uint32_t command_line_cpu_topo_marker;
    uint32_t command_line_bootcpu_marker;
    uint32_t command_line_rtclock_marker;
    uint32_t command_line_armvm_marker;
    uint32_t command_line_no_public_markers;
    uint32_t pe_boot_args_ptr;
    uint32_t pe_device_tree_head;
    uint32_t pe_device_tree_length;
    uint32_t pe_cpu_count;
    uint32_t pe_machine_type;
    uint32_t pe_vector_base;
    uint32_t pe_gic_dist_base;
    uint32_t pe_gic_cpu_base;
    uint32_t pe_timer_base;
    uint32_t pe_timer_frequency;
    uint32_t pe_state_inherited;
    uint32_t cpu_topology_parsed;
    uint32_t cpu_count;
    uint32_t boot_cpu;
    uint32_t max_cpu_number;
    uint32_t master_cpu;
    uint32_t master_cpu_valid;
    uint32_t boot_cpu_data_ptr;
    uint32_t boot_cpu_data_checksum;
    uint32_t boot_cpu_data_cpu_number;
    uint32_t boot_cpu_data_cpu_count;
    uint32_t boot_cpu_data_machine_type;
    uint32_t boot_cpu_data_vector_base;
    uint32_t boot_cpu_data_int_stack_top;
    uint32_t boot_cpu_data_fiq_stack_top;
    uint32_t boot_cpu_data_timer_base;
    uint32_t boot_cpu_data_timer_frequency;
    uint32_t boot_cpu_data_populated;
    uint32_t cpu_data_entries_ptr;
    uint32_t cpu_data_entries_checksum;
    uint32_t cpu_data_entries_selected_index;
    uint32_t cpu_data_entries_boot_entry_ptr;
    uint32_t cpu_data_entries_boot_entry_phys;
    uint32_t cpu_data_entries_self_consistent;
    uint32_t cpu_data_entries_populated;
    uint32_t thread_bootstrap_order;
    uint32_t cpu_bootstrap_order;
    uint32_t rtclock_early_init_order;
    uint32_t kernel_early_bootstrap_order;
    uint32_t cpu_init_order;
    uint32_t processor_bootstrap_order;
    uint32_t bootstrap_order_valid;
    uint32_t timebase_callback_ptr;
    uint32_t timebase_callback_checksum;
    uint32_t timebase_callback_registered;
    uint32_t timebase_frequency;
    uint32_t timebase_initial_lo;
    uint32_t timebase_initial_hi;
    uint32_t pe_register_timebase_callback_shaped;
    uint32_t boot_arg_diag_present;
    uint32_t boot_arg_diag_value;
    uint32_t boot_arg_maxmem_present;
    uint32_t boot_arg_maxmem_value;
    uint32_t boot_arg_debug_present;
    uint32_t boot_arg_debug_value;
    uint32_t boot_arg_immediate_nmi_present;
    uint32_t boot_arg_immediate_nmi_value;
    uint32_t boot_arg_parse_ok;
    uint32_t stop_before_arm_vm_init;
    uint32_t arm_vm_init_not_called;
    uint32_t public_xnu_start_executed;
    uint32_t public_arm_init_executed;
    uint32_t public_thread_bootstrap_executed;
    uint32_t public_cpu_bootstrap_executed;
    uint32_t public_kernel_early_bootstrap_executed;
    uint32_t public_cpu_init_executed;
    uint32_t public_processor_bootstrap_executed;
    uint32_t public_rtclock_early_init_executed;
    uint32_t public_pe_register_timebase_callback_executed;
    uint32_t public_arm_vm_init_executed;
    uint32_t public_pmap_runtime_executed;
    uint32_t public_pexpert_runtime_executed;
    uint32_t public_iokit_runtime_executed;
    uint32_t generated_macho_executed;
    uint32_t ttbr0_before;
    uint32_t ttbr0_after;
    uint32_t ttbcr_before;
    uint32_t ttbcr_after;
    uint32_t dacr_before;
    uint32_t dacr_after;
    uint32_t sctlr_before;
    uint32_t sctlr_after;
    uint32_t control_registers_sampled;
    uint32_t control_registers_unchanged;
    uint32_t live_pmap_installed;
    uint32_t ttbr_written;
    uint32_t ttbcr_written;
    uint32_t dacr_written;
    uint32_t sctlr_written;
    uint32_t tlb_invalidated;
    uint32_t cache_policy_changed;
    uint32_t persistent_write_attempted;
    uint32_t stage_owned_local_only;
    uint32_t safety_boundary_preserved;
    uint32_t checksum;
};

/* Stage84 live-pmap ABI */

#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_VERSION 2u
/* Stage84 adds L2 table support (bit 15) and high-VA data verification (bit 16).
 * required = 0x01007fff (Stage82 base) | 0x8000 (L2) | 0x10000 (high-VA) = 0x0101ffff */
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_REQUIRED_MASK 0x0101ffffu

#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_SOURCE_POST_PE_OK       0x00000001u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_BOOT_ARGS_VALID         0x00000002u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_CANDIDATE_L1_VALID      0x00000004u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_CANDIDATE_L1_POPULATED  0x00000008u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_CONTROL_REGS_SAVED      0x00000010u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_LIVE_PMAP_INSTALLED     0x00000020u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_LIVE_PMAP_VERIFIED      0x00000040u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_HIGH_ALIAS_VERIFIED     0x00000080u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_RAM_CONSOLE_VERIFIED    0x00000100u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_GIC_VERIFIED            0x00000200u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_LIVE_PMAP_RESTORED      0x00000400u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_ORIGINAL_RESTORED       0x00000800u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_NO_PUBLIC_ARM_VM_INIT   0x00001000u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_NO_PUBLIC_PMAP_RUNTIME  0x00002000u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_NO_PERSISTENT_WRITE     0x00004000u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_L2_TABLES_POPULATED     0x00008000u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_HIGH_VA_DATA_VERIFIED   0x00010000u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_SAT_SAFETY_BOUNDARY         0x01000000u

#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_SOURCE_POST_PE         0x00000001u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_BOOT_ARGS              0x00000002u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_CANDIDATE_L1_ALIGN     0x00000004u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_MMU_DISABLED           0x00000008u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_TTBR0_MISMATCH         0x00000010u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_CONTROL_REG_CHANGED    0x00000020u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_HIGH_ALIAS             0x00000040u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_RAM_CONSOLE            0x00000080u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_GIC                    0x00000100u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_RESTORE                0x00000200u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_L2_ALLOC               0x00000400u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_HIGH_VA_DATA           0x00010000u
#define STAGE84_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_SAFETY_BOUNDARY        0x80000000u

struct stage84_xnu_arm_vm_init_full_pmap_result {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_post_pe_status;
    uint32_t source_post_pe_checksum;
    uint32_t boot_args_ptr;
    uint32_t boot_args_valid;
    uint32_t memory_size;
    uint32_t phys_base;
    uint32_t candidate_l1_base;
    uint32_t candidate_l1_checksum;
    uint32_t candidate_l2_pool_base;
    uint32_t candidate_l2_pool_checksum;
    uint32_t l2_tables_allocated;
    uint32_t virt_base;
    uint32_t original_ttbr0;
    uint32_t original_ttbcr;
    uint32_t original_dacr;
    uint32_t original_sctlr;
    uint32_t live_ttbr0;
    uint32_t live_ttbcr;
    uint32_t live_dacr;
    uint32_t live_sctlr;
    uint32_t restored_ttbr0;
    uint32_t restored_ttbcr;
    uint32_t restored_dacr;
    uint32_t restored_sctlr;
    uint32_t ttbr0_write_count;
    uint32_t tlb_invalidate_count;
    uint32_t live_pmap_installed;
    uint32_t live_pmap_verified;
    uint32_t live_pmap_restored;
    uint32_t high_alias_verified;
    uint32_t ram_console_verified;
    uint32_t gic_verified;
    uint32_t high_va_data_verified;
    uint32_t public_arm_vm_init_executed;
    uint32_t public_pmap_runtime_executed;
    uint32_t persistent_write_attempted;
    uint32_t safety_boundary_preserved;
    uint32_t checksum;
};
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_SOURCE_PROVIDER_CALLBACK 0x00000001u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_APPLE_DT 0x00000002u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PERSONALITY_NODE_SET 0x00000004u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_CLIENT_OPEN_READY 0x00000008u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PROVIDER_CLAIM_READY 0x00000010u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_CLIENT_CLOSE_READY 0x00000020u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PROVIDER_PATH_MATRIX 0x00000040u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PROVIDER_ORDINAL_MATRIX 0x00000080u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_OPEN_TYPE_MATRIX 0x00000100u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_EXPECTED_OPEN_MATRIX 0x00000200u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_CLAIMED_OPEN_MATRIX 0x00000400u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_CLIENT_CLOSE_MATRIX 0x00000800u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_ORDER_MATRIX 0x00001000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_DEPENDENCY_MATRIX 0x00002000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_RUNTIME_BLOCKED 0x00004000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PROVENANCE 0x00008000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_SELECTED_OPEN_MATRIX 0x00010000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_REJECT_NO_OPEN 0x00020000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_DRYRUN_COUNTS 0x00040000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_OPEN_CHECKSUM 0x00080000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_IOKIT_REFERENCE 0x00100000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PUBLIC_BOUNDARY 0x00200000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PMAP_BOUNDARY 0x00400000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC 0x00800000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_OPEN_RUNTIME_EXEC 0x01000000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_CLAIM_RUNTIME_EXEC 0x02000000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_CLOSE_RUNTIME_EXEC 0x04000000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC 0x08000000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x10000000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x20000000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED 0x40000000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_DT 0x00000002u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_PERSONALITY 0x00000004u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_OPEN 0x00000008u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_CLAIM 0x00000010u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_CLOSE 0x00000020u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_ORDER_DEPENDENCY 0x00000040u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_REJECTED 0x00000080u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_COUNTS 0x00000100u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_PUBLIC_BOUNDARY 0x00000200u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_PMAP_BOUNDARY 0x00000400u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_ROLLUP 0x00000800u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_CHECKSUM 0x00001000u
#define STAGE84_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_provider_callback_status;
    uint32_t source_provider_callback_satisfied_mask;
    uint32_t source_provider_callback_failure_mask;
    uint32_t source_provider_callback_contract_checksum;
    uint32_t source_provider_callback_checksum;
    uint32_t source_selected_provider_callback_entry_mask;
    uint32_t source_rejected_provider_callback_mask;
    uint32_t source_provider_callback_matrix;
    uint32_t source_client_ack_mask_match_mask;
    uint32_t source_no_callback_runtime_exec;
    uint32_t source_no_client_notification_runtime_exec;
    uint32_t source_provider_notification_status;
    uint32_t source_lifecycle_status;
    uint32_t source_attach_start_status;
    uint32_t source_topology_status;
    uint32_t source_property_inheritance_status;
    uint32_t source_catalog_status;
    uint32_t source_provider_status;
    uint32_t source_registry_status;
    uint32_t source_match_status;
    uint32_t source_scaffold_status;
    uint32_t source_pexpert_status;
    uint32_t source_pmap_transition_status;
    uint32_t source_compile_graph_status;
    uint32_t source_object_subset_status;
    uint32_t source_link_status;
    uint32_t source_loader_safety_mask;
    uint32_t source_loader_safety_required_mask;
    uint32_t boot_args_ptr;
    uint32_t device_tree_ptr;
    uint32_t device_tree_length;
    uint32_t apple_dt_semantic_mask;
    uint32_t platform_personality_node_ptr;
    uint32_t platform_personality_prop_count;
    uint32_t interrupt_personality_node_ptr;
    uint32_t interrupt_personality_prop_count;
    uint32_t timer_personality_node_ptr;
    uint32_t timer_personality_prop_count;
    uint32_t cpu_personality_node_ptr;
    uint32_t cpu_personality_prop_count;
    uint32_t rejected_personality_node_ptr;
    uint32_t rejected_personality_prop_count;
    uint32_t client_open_ready_mask;
    uint32_t provider_claim_ready_mask;
    uint32_t client_close_ready_mask;
    uint32_t open_provider_path_match_mask;
    uint32_t open_provider_ordinal_match_mask;
    uint32_t open_type_match_mask;
    uint32_t expected_open_mask_match_mask;
    uint32_t claimed_open_mask_match_mask;
    uint32_t client_close_mask_match_mask;
    uint32_t open_dependency_ready_mask;
    uint32_t open_runtime_blocked_mask;
    uint32_t provider_claim_runtime_blocked_mask;
    uint32_t client_close_runtime_blocked_mask;
    uint32_t open_order_match_mask;
    uint32_t close_order_match_mask;
    uint32_t open_provenance_match_mask;
    uint32_t selected_client_open_entry_mask;
    uint32_t rejected_client_open_mask;
    uint32_t client_open_provider_claim_close_matrix;
    uint32_t platform_open_provider_ordinal;
    uint32_t interrupt_open_provider_ordinal;
    uint32_t timer_open_provider_ordinal;
    uint32_t cpu_open_provider_ordinal;
    uint32_t rejected_open_provider_ordinal;
    uint32_t platform_open_type_mask;
    uint32_t interrupt_open_type_mask;
    uint32_t timer_open_type_mask;
    uint32_t cpu_open_type_mask;
    uint32_t rejected_open_type_mask;
    uint32_t platform_expected_open_mask;
    uint32_t interrupt_expected_open_mask;
    uint32_t timer_expected_open_mask;
    uint32_t cpu_expected_open_mask;
    uint32_t rejected_expected_open_mask;
    uint32_t platform_claimed_open_mask;
    uint32_t interrupt_claimed_open_mask;
    uint32_t timer_claimed_open_mask;
    uint32_t cpu_claimed_open_mask;
    uint32_t rejected_claimed_open_mask;
    uint32_t platform_client_close_mask;
    uint32_t interrupt_client_close_mask;
    uint32_t timer_client_close_mask;
    uint32_t cpu_client_close_mask;
    uint32_t rejected_client_close_mask;
    uint32_t platform_open_order;
    uint32_t interrupt_open_order;
    uint32_t timer_open_order;
    uint32_t cpu_open_order;
    uint32_t rejected_open_order;
    uint32_t platform_close_order;
    uint32_t interrupt_close_order;
    uint32_t timer_close_order;
    uint32_t cpu_close_order;
    uint32_t rejected_close_order;
    uint32_t open_order_checksum;
    uint32_t close_order_checksum;
    uint32_t client_open_mask_checksum;
    uint32_t client_open_path_hash;
    uint32_t client_open_checksum;
    uint32_t expected_client_open_checksum;
    uint32_t client_open_candidate_count;
    uint32_t selected_client_open_entry_count;
    uint32_t rejected_client_open_entry_count;
    uint32_t client_open_ready_count;
    uint32_t provider_claim_ready_count;
    uint32_t client_close_ready_count;
    uint32_t rejected_no_open_count;
    uint32_t no_open_runtime_exec;
    uint32_t no_claim_runtime_exec;
    uint32_t no_close_runtime_exec;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t no_iokit_runtime_exec;
    uint32_t no_catalog_runtime_exec;
    uint32_t no_provider_runtime_exec;
    uint32_t no_registry_entry_runtime_exec;
    uint32_t no_registry_topology_runtime_exec;
    uint32_t no_property_runtime_exec;
    uint32_t no_attach_runtime_exec;
    uint32_t no_start_runtime_exec;
    uint32_t no_register_service_runtime_exec;
    uint32_t no_notification_runtime_exec;
    uint32_t no_interest_runtime_exec;
    uint32_t no_delivery_runtime_exec;
    uint32_t no_callback_runtime_exec;
    uint32_t no_client_notification_runtime_exec;
    uint32_t no_platform_driver_exec;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_public_pmap_exec;
    uint32_t no_live_pmap_tables_installed;
    uint32_t proposed_workspace_written;
    uint32_t pmap_ttbr_written;
    uint32_t pmap_ttbcr_written;
    uint32_t pmap_dacr_written;
    uint32_t pmap_sctlr_written;
    uint32_t pmap_tlbs_invalidated;
    uint32_t caches_changed;
    uint32_t persistent_write_attempted;
    uint32_t xnu_start_executed;
    uint32_t generated_macho_executed;
    uint32_t proposed_platform_gap_mask;
    uint32_t proposed_pexpert_gap_mask;
    uint32_t local_only;
    uint32_t fail_closed;
    uint32_t checksum;
};

#define STAGE84_XNU_BOOTSTRAP_CONTRACT_VERSION    1u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_PAGE_SIZE  4096u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_PAGE_ALIGN_MASK (STAGE84_XNU_BOOTSTRAP_CONTRACT_PAGE_SIZE - 1u)

#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_SOURCE_ROLLUPS 0x00000001u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_MACHO_IMPORTED 0x00000002u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_STAGING_IMPORTED 0x00000004u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_TTE_IMPORTED 0x00000008u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_HIGHVA_IMPORTED 0x00000010u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_SAFE_TABLE_LOCAL_ONLY 0x00000020u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_TTBR_RESTORED 0x00000040u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_CACHES_PRESERVED 0x00000080u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_VIRTBASE_ALIGNED 0x00000100u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_PHYSBASE_ALIGNED 0x00000200u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_TOPOFKERNELDATA_ALIGNED 0x00000400u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_ALIGNED 0x00000800u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_AVAIL_START_ALIGNED 0x00001000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_AFTER_IMAGE 0x00002000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_AVAIL_START_AFTER_WORKSPACE 0x00004000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_AVAIL_END_INSIDE_RAM 0x00008000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_STAGE_IMAGE_OVERLAP 0x00010000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_RAM_CONSOLE_OVERLAP 0x00020000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_DEVICE_TREE_OVERLAP 0x00040000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_STAGING_ARENA_OVERLAP 0x00080000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_SAFE_TABLE_OVERLAP 0x00100000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_PAGE_COVERAGE 0x00200000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_SECTION_COVERAGE 0x00400000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_XNU_EXEC 0x00800000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PLATFORM_RUNTIME_EXEC 0x01000000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_MACHO_EXEC 0x02000000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PROPOSED_PHYS_WRITE 0x04000000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PROPOSED_TTE_WRITE 0x08000000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PERSIST_WRITE 0x10000000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_NO_CACHE_POLICY_CHANGE 0x20000000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SAT_FAIL_CLOSED 0x40000000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_LOADED_IMAGE 0x00000001u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_FILE_BYTES   0x00000002u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_ZERO_FILL    0x00000004u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_WORKSPACE    0x00000008u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_L1_SECTIONS  0x00000010u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_REQUIRED     0x0000001fu

#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_IDENTITY_LOW 0x00000001u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_IDENTITY_KERNEL 0x00000002u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_RAM_CONSOLE 0x00000004u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_GIC 0x00000008u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_HIGHVA 0x00000010u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_REQUIRED 0x0000001fu

#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_SOURCE_ROLLUP 0x00000001u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_ALIGNMENT 0x00000002u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE_ORDER 0x00000004u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_AVAIL_RANGE 0x00000008u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_STAGE_IMAGE_OVERLAP 0x00000010u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_RAM_CONSOLE_OVERLAP 0x00000020u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_DEVICE_TREE_OVERLAP 0x00000040u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_LOCAL_ARENA_OVERLAP 0x00000080u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFE_TABLE_OVERLAP 0x00000100u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_PAGE_COVERAGE 0x00000200u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_SECTION_COVERAGE 0x00000400u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_TTBR_RESTORE 0x00000800u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_CACHE_POLICY 0x00001000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_CHECKSUM 0x00002000u
#define STAGE84_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_bootstrap_contract {
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

#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_VERSION    1u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_SAT_ROOT   0x00000001u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_SAT_VM_PLAN 0x00000002u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_SAT_VM_STATE 0x00000004u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_SAT_ALLOCATOR 0x00000008u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_SAT_WORKSPACE 0x00000010u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_SAT_POLICY 0x00000020u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_REQUIRED   0x0000003fu

#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_ROOT  0x00000001u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_VM_PLAN 0x00000002u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_VM_STATE 0x00000004u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_ALLOCATOR 0x00000008u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_WORKSPACE 0x00000010u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_POLICY 0x00000020u
#define STAGE84_PMAP_BOOTSTRAP_SNAPSHOT_FAIL_CHECKSUM 0x00000040u

struct stage84_pmap_bootstrap_snapshot {
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

#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_VERSION 1u
#define STAGE84_XNU_PMAP_BOOTSTRAP_PAGE_SIZE        4096u
#define STAGE84_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX     0x40000000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_VSTART_ROUND     0x00400000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_VM_MIN_KERNEL_ADDRESS 0x80000000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_VM_MAX_KERNEL_ADDRESS 0xfffeffffu
#define STAGE84_XNU_PMAP_BOOTSTRAP_L1_ALIGNMENT     0x00004000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_SECTION_SIZE     0x00100000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_SECTION_DESC_SO  0x00010c02u
#define STAGE84_XNU_PMAP_BOOTSTRAP_ALLOC_TAG        0x414c4c43u
#define STAGE84_XNU_PMAP_BOOTSTRAP_WORKSPACE_TAG    0x504d4150u

#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_SOURCE_BOOTSTRAP 0x00000001u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_SOURCE_ROLLUPS 0x00000002u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_REFERENCE_PROVENANCE 0x00000004u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_PUBLIC_GLOBALS 0x00000008u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_TTEP_LAYOUT 0x00000010u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_AVAIL_RANGE 0x00000020u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_VIRTUAL_SPACE 0x00000040u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_SNAPSHOT_IMPORTED 0x00000080u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_SPAN 0x00000100u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_CURSOR 0x00000200u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_FIRST_ALLOC 0x00000400u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_POLICY 0x00000800u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_RANGE 0x00001000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_L1 0x00002000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_SECTIONS 0x00004000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_POLICY 0x00008000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_STAGE_OVERLAP 0x00010000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_DT_OVERLAP 0x00020000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_STAGING_OVERLAP 0x00040000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_SAFE_TABLE_OVERLAP 0x00080000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_RAM_CONSOLE_OVERLAP 0x00100000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_PMAP_COMPILE 0x00200000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_PMAP_LINK 0x00400000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_PMAP_EXEC 0x00800000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_ARM_VM_INIT_EXEC 0x01000000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_LIVE_PMAP_INSTALL 0x02000000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_XNU_JUMP 0x04000000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_MACHO_EXEC 0x08000000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PROPOSED_WRITES 0x10000000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PERSIST_CACHE 0x20000000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_FAIL_CLOSED 0x40000000u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_REQUIRED_MASK 0x7fffffffu

#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_REFERENCE 0x00000002u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ARITHMETIC 0x00000004u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SNAPSHOT 0x00000008u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ALLOCATOR 0x00000010u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE 0x00000020u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_OVERLAP 0x00000040u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_COMPILE 0x00000080u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_LINK 0x00000100u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_EXEC 0x00000200u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_LIVE_PMAP_INSTALL 0x00000400u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_CHECKSUM 0x00000800u
#define STAGE84_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_pmap_bootstrap_contract {
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

#define STAGE84_XNU_PMAP_TABLE_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT   4096u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_L1_BYTES         0x00004000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_L1_ALIGNMENT     0x00004000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SECTION_SIZE     0x00100000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_DESC_SECTION_SO  0x00010c02u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK   0x00000003u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_SECTION 0x00000002u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_DESC_BASE_MASK   0xfff00000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK   0x000fffffu
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SECTION_OFFSET_MASK 0x000fffffu

#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_SOURCE_PMAP_BOOTSTRAP 0x00000001u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_SOURCE_SNAPSHOT 0x00000002u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000004u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_CONSTANTS 0x00000008u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_LOCAL_BUFFER 0x00000010u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_LOCAL_ZERO 0x00000020u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_LOW_MEMORY_RANGE 0x00000040u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_KERNEL_RANGE 0x00000080u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_WORKSPACE_RANGE 0x00000100u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_RAM_CONSOLE_RANGE 0x00000200u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_DESCRIPTOR_READBACK 0x00000400u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_DESCRIPTOR_ATTRS 0x00000800u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_KERNEL 0x00001000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_LOWMEM 0x00002000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_WORKSPACE 0x00004000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_RAM_CONSOLE 0x00008000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM 0x00010000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00020000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00040000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00080000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_NO_TLB_INVALIDATE 0x00100000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00200000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00400000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_NO_PERSIST_CACHE 0x00800000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_SAT_FAIL_CLOSED 0x01000000u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_REQUIRED_MASK 0x01ffffffu

#define STAGE84_XNU_PMAP_TABLE_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_FAIL_CONSTANTS 0x00000002u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_FAIL_LOCAL_BUFFER 0x00000004u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE 0x00000008u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_FAIL_DESCRIPTOR 0x00000010u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_FAIL_TRANSLATION 0x00000020u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_FAIL_CHECKSUM 0x00000040u
#define STAGE84_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_pmap_table_dryrun_contract {
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

#define STAGE84_XNU_PMAP_PAGE_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_PAGE_SIZE        0x00001000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_PAGE_OFFSET_MASK 0x00000fffu
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_WINDOW_SIZE      0x00400000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_WINDOW_ALIGN     0x00400000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT   4096u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_BYTES         0x00004000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_ALIGNMENT     0x00004000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_TYPE    0x00000001u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_TYPE_MASK     0x00000003u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_MASK    0xfffffc00u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_ATTR_MASK 0x000003ffu
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_PAGE_BYTES    0x00001000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_ALIGNMENT     0x00001000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_COARSE_BYTES  0x00000400u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE 4u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_PTES_PER_TABLE 256u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT     1024u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_MASK    0x000ff000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_SHIFT   12u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_MASK    0x00000002u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_SMALL   0x00000002u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_PTE_PAGE_MASK    0xfffff000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_MASK    0x00000fffu
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_DEFAULT 0x00000412u

#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_SOURCE_PMAP_BOOTSTRAP 0x00000001u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_SOURCE_TABLE_DRYRUN 0x00000002u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000004u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_CONSTANTS 0x00000008u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_L1_BUFFER 0x00000010u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_L2_BUFFER 0x00000020u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_ZERO 0x00000040u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_WINDOW 0x00000080u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_L1_COARSE_DESCRIPTORS 0x00000100u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_PTE_POPULATION 0x00000200u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_PTE_READBACK 0x00000400u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_ATTRS 0x00000800u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_TRANSLATION_KERNEL 0x00001000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_TRANSLATION_WORKSPACE 0x00002000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_TRANSLATION_WINDOW 0x00004000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM 0x00008000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00010000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00020000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00040000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_NO_TLB_INVALIDATE 0x00080000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00100000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00200000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_NO_PERSIST_CACHE 0x00400000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_SAT_FAIL_CLOSED 0x00800000u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_REQUIRED_MASK 0x00ffffffu

#define STAGE84_XNU_PMAP_PAGE_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_FAIL_CONSTANTS 0x00000002u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_FAIL_LOCAL_BUFFER 0x00000004u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_FAIL_WINDOW 0x00000008u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_FAIL_DESCRIPTOR 0x00000010u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_FAIL_PTE 0x00000020u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_FAIL_TRANSLATION 0x00000040u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_FAIL_CHECKSUM 0x00000080u
#define STAGE84_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_pmap_page_dryrun_contract {
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

#define STAGE84_XNU_PMAP_ATTR_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITEBACK 0x0u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITECOMB 0x1u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITETHRU 0x2u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_CACHE_DISABLE 0x3u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_CACHE_INNERWRITEBACK 0x4u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_CACHE_POSTED STAGE84_XNU_PMAP_ATTR_DRYRUN_CACHE_DISABLE
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_CACHE_DEFAULT STAGE84_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITEBACK
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_AP_RWNA 0x0u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_AP_RWRW 0x1u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_AP_RONA 0x2u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_AP_RORO 0x3u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_GUARDED 0x01u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT 0x02u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_NOT_CACHEABLE 0x04u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_WRITE_THROUGH 0x08u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_INNER 0x10u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_EARLY_ACK 0x20u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_DEFAULT STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_COPYBACK STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_INNERWBACK (STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT | STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_INNER)
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_IO (STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT | STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_NOT_CACHEABLE | STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_GUARDED)
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_POSTED (STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_IO | STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_EARLY_ACK)
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_WTHRU (STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_WRITE_THROUGH | STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT | STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_GUARDED)
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_WCOMB (STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_NOT_CACHEABLE | STAGE84_XNU_PMAP_ATTR_DRYRUN_VM_MEM_COHERENT)
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_PTE_TYPE 0x00000002u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_PTE_NX 0x00000001u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_PTE_AF 0x00000010u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_PTE_SH 0x00000400u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_PTE_ATTRINDXMASK 0x0000004cu
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_PTE_APMASK 0x00000220u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_PTE_TEMPLATE_MASK 0x0000067fu
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_TTE_TYPE_SECTION 0x00000002u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_TTE_TYPE_TABLE 0x00000001u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_TTE_AF 0x00000400u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_TTE_SH 0x00010000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_TTE_NX 0x00000010u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_TTE_ATTRINDXMASK 0x0000100cu
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_TTE_APMASK 0x00008800u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_TTE_SECTION_TEMPLATE_MASK 0x00019c1fu

#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_SOURCE_TABLE_DRYRUN 0x00000001u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_SOURCE_PAGE_DRYRUN 0x00000002u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000004u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_PUBLIC_CONSTANTS 0x00000008u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_CACHE_ATTRINDX_VALUES 0x00000010u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_ATTRINDX_MACROS 0x00000020u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_ACCESS_PROTECTIONS 0x00000040u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_SECTION_TEMPLATE 0x00000080u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_PAGE_TEMPLATE 0x00000100u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_WIMG_DEFAULT_COPYBACK 0x00000200u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_WIMG_DEVICE_POSTED 0x00000400u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_WIMG_OTHER_CACHE_MODES 0x00000800u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_PAGE_PROT_HELPERS 0x00001000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_PRIOR_READBACKS 0x00002000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_LOCAL_ONLY 0x00004000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00008000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00010000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00020000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00040000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_NO_TLB_INVALIDATE 0x00080000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_NO_CACHE_CHANGE 0x00100000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00200000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_NO_PERSIST_WRITE 0x00400000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_SAT_FAIL_CLOSED 0x00800000u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_REQUIRED_MASK 0x00ffffffu

#define STAGE84_XNU_PMAP_ATTR_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_FAIL_CONSTANTS 0x00000002u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_FAIL_ATTRINDX 0x00000004u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_FAIL_AP 0x00000008u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_FAIL_TEMPLATE 0x00000010u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_FAIL_WIMG 0x00000020u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_FAIL_READBACK 0x00000040u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_FAIL_CHECKSUM 0x00000080u
#define STAGE84_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_pmap_attr_dryrun_contract {
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

#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT 3u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_SIZE STAGE84_XNU_PMAP_PAGE_DRYRUN_WINDOW_SIZE
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_ALIGN STAGE84_XNU_PMAP_PAGE_DRYRUN_WINDOW_ALIGN
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_SIZE STAGE84_XNU_PMAP_PAGE_DRYRUN_PAGE_SIZE
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_OFFSET_MASK STAGE84_XNU_PMAP_PAGE_DRYRUN_PAGE_OFFSET_MASK
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_BYTES STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_BYTES
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ALIGNMENT STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_ALIGNMENT
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_TYPE STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_TYPE
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TYPE_MASK STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_TYPE_MASK
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_MASK STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_MASK
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_ATTR_MASK STAGE84_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_ATTR_MASK
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_PAGE_BYTES
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_ALIGNMENT STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_ALIGNMENT
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_COARSE_BYTES STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_COARSE_BYTES
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTES_PER_TABLE STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_PTES_PER_TABLE
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_MASK STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_MASK
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_SHIFT STAGE84_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_SHIFT
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_MASK STAGE84_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_MASK
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_SMALL STAGE84_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_SMALL
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_PAGE_MASK STAGE84_XNU_PMAP_PAGE_DRYRUN_PTE_PAGE_MASK
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_ATTR_MASK STAGE84_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_MASK
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_GIC_PHYS_BASE 0xf9000000u

#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_TABLE_DRYRUN 0x00000001u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_PAGE_DRYRUN 0x00000002u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_ATTR_DRYRUN 0x00000004u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000008u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_CONSTANTS 0x00000010u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_L1_BUFFER 0x00000020u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_L2_BANK 0x00000040u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_ZERO 0x00000080u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_WINDOW_KERNEL 0x00000100u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_WINDOW_RAM_CONSOLE 0x00000200u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_WINDOW_DEVICE 0x00000400u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_L1_COARSE_DESCRIPTORS 0x00000800u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_PTE_POPULATION 0x00001000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_PTE_READBACK 0x00002000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_ATTRS 0x00004000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_KERNEL 0x00008000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_WORKSPACE 0x00010000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_RAM_CONSOLE 0x00020000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_DEVICE 0x00040000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM 0x00080000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00100000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00200000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00400000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00800000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_TLB_INVALIDATE 0x01000000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_CACHE_CHANGE 0x02000000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x04000000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_PERSIST_WRITE 0x08000000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_FAIL_CLOSED 0x10000000u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_REQUIRED_MASK 0x1fffffffu

#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_CONSTANTS 0x00000002u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_LOCAL_BUFFER 0x00000004u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_WINDOW 0x00000008u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_L1_DESCRIPTOR 0x00000010u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_PTE 0x00000020u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_ATTR 0x00000040u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_TRANSLATION 0x00000080u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_CHECKSUM 0x00000100u
#define STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_pmap_multiwindow_dryrun_contract {
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

#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT_VERSION 1u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L1_ENTRY_COUNT STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L1_BYTES STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_BYTES
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L1_ALIGNMENT STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ALIGNMENT
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_TYPE STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_TYPE
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L1_TYPE_MASK STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TYPE_MASK
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_MASK STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_MASK
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_ATTR_MASK STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_ATTR_MASK
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L2_BANK_BYTES (STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT * STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES)
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L2_COARSE_BYTES STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_COARSE_BYTES
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L2_PAGE_BYTES STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L2_PTE_COUNT STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L2_INDEX_MASK STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_MASK
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_L2_INDEX_SHIFT STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_SHIFT
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_PTE_TYPE_MASK STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_MASK
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_PTE_TYPE_SMALL STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_SMALL
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_PTE_PAGE_MASK STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_PAGE_MASK
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_PAGE_OFFSET_MASK STAGE84_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_OFFSET_MASK
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_TTBR0_BASE_MASK 0xffffc000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_CONTROL_CACHE_MASK STAGE84_TTBR_RT_CACHE_MASK

#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_SOURCE_MULTIWINDOW 0x00000001u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_SOURCE_ROLLUPS 0x00000002u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_CANDIDATE_L1_BUFFER 0x00000004u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_CANDIDATE_ZERO 0x00000008u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_DESCRIPTOR_IMPORT 0x00000010u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_KERNEL 0x00000020u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_WORKSPACE 0x00000040u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_RAM_CONSOLE 0x00000080u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_DEVICE 0x00000100u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_TTBR0_PLAN 0x00000200u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_TTBCR_PLAN 0x00000400u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_DACR_PLAN 0x00000800u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_SCTLR_PLAN 0x00001000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_RECOVERY_CONTINUITY 0x00002000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM 0x00004000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC 0x00008000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE 0x00010000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL 0x00020000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_CONTROL_REG_WRITE 0x00040000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_TLB_INVALIDATE 0x00080000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_CACHE_CHANGE 0x00100000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_XNU_MACHO_EXEC 0x00200000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_PERSIST_WRITE 0x00400000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_SAT_FAIL_CLOSED 0x00800000u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_REQUIRED_MASK 0x00ffffffu

#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SOURCE 0x00000001u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_FAIL_LOCAL_BUFFER 0x00000002u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_FAIL_DESCRIPTOR 0x00000004u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_FAIL_TRANSLATION 0x00000008u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CONTROL_PLAN 0x00000010u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_FAIL_RECOVERY 0x00000020u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CHECKSUM 0x00000040u
#define STAGE84_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY 0x80000000u

struct stage84_xnu_pmap_transition_dryrun_contract {
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

#define STAGE84_LOADER_SAT_BASELINE               0x00000001u
#define STAGE84_LOADER_SAT_MACHO                  0x00000002u
#define STAGE84_LOADER_SAT_BOOT_ARGS              0x00000004u
#define STAGE84_LOADER_SAT_WORKSPACE              0x00000008u
#define STAGE84_LOADER_SAT_DT                     0x00000010u
#define STAGE84_LOADER_SAT_PLATFORM_GAPS          0x00000020u
#define STAGE84_LOADER_SAT_INTERRUPTS             0x00000040u
#define STAGE84_LOADER_SAT_SAFETY                 0x00000080u
#define STAGE84_LOADER_SAT_MATERIALIZED           0x00000100u
#define STAGE84_LOADER_SAT_TTE_DRYRUN             0x00000200u
#define STAGE84_LOADER_SAT_TTE_VERIFY             0x00000400u
#define STAGE84_LOADER_SAT_VTOP_DRYRUN            0x00000800u
#define STAGE84_LOADER_SAT_HIGHVA_DRYRUN          0x00001000u
#define STAGE84_LOADER_SAT_SAFE_TABLE             0x00002000u
#define STAGE84_LOADER_SAT_STAGE_OWNED_TABLES     0x00004000u
#define STAGE84_LOADER_SAT_TTBR_ROUNDTRIP         0x00008000u
#define STAGE84_LOADER_SAT_STAGE_OWNED_TTBR       0x00010000u
#define STAGE84_LOADER_SAT_TTBR_RESTORED          0x00020000u
#define STAGE84_LOADER_SAT_CACHE_PRESERVED        0x00040000u
#define STAGE84_LOADER_SAT_XNU_WORKSPACE          0x00080000u
#define STAGE84_LOADER_SAT_CANCRO_TARGET          0x00100000u
#define STAGE84_LOADER_SAT_STAGE84_PLAN           0x00200000u
#define STAGE84_LOADER_SAT_XNU_OBJECT_SUBSET      0x00400000u
#define STAGE84_LOADER_SAT_XNU_LINK               0x00800000u
#define STAGE84_LOADER_SAT_XNU_COMPILE_GRAPH      0x01000000u
#define STAGE84_LOADER_SAT_XNU_BOOTSTRAP_CONTRACT 0x02000000u
#define STAGE84_LOADER_SAT_XNU_PMAP_BOOTSTRAP_CONTRACT 0x04000000u
#define STAGE84_LOADER_SAT_XNU_PMAP_TABLE_DRYRUN_CONTRACT 0x08000000u
#define STAGE84_LOADER_SAT_XNU_PMAP_PAGE_DRYRUN_CONTRACT 0x10000000u
#define STAGE84_LOADER_SAT_XNU_PMAP_ATTR_DRYRUN_CONTRACT 0x20000000u
#define STAGE84_LOADER_SAT_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT 0x40000000u
#define STAGE84_LOADER_SAT_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT 0x80000000u
#define STAGE84_LOADER_SAT_REQUIRED               (STAGE84_LOADER_SAT_BASELINE | \
                                                   STAGE84_LOADER_SAT_MACHO | \
                                                   STAGE84_LOADER_SAT_BOOT_ARGS | \
                                                   STAGE84_LOADER_SAT_WORKSPACE | \
                                                   STAGE84_LOADER_SAT_DT | \
                                                   STAGE84_LOADER_SAT_PLATFORM_GAPS | \
                                                   STAGE84_LOADER_SAT_INTERRUPTS | \
                                                   STAGE84_LOADER_SAT_SAFETY | \
                                                   STAGE84_LOADER_SAT_MATERIALIZED | \
                                                   STAGE84_LOADER_SAT_TTE_DRYRUN | \
                                                   STAGE84_LOADER_SAT_TTE_VERIFY | \
                                                   STAGE84_LOADER_SAT_VTOP_DRYRUN | \
                                                   STAGE84_LOADER_SAT_HIGHVA_DRYRUN | \
                                                   STAGE84_LOADER_SAT_SAFE_TABLE | \
                                                   STAGE84_LOADER_SAT_STAGE_OWNED_TABLES | \
                                                   STAGE84_LOADER_SAT_TTBR_ROUNDTRIP | \
                                                   STAGE84_LOADER_SAT_STAGE_OWNED_TTBR | \
                                                   STAGE84_LOADER_SAT_TTBR_RESTORED | \
                                                   STAGE84_LOADER_SAT_CACHE_PRESERVED | \
                                                   STAGE84_LOADER_SAT_XNU_WORKSPACE | \
                                                   STAGE84_LOADER_SAT_CANCRO_TARGET | \
                                                   STAGE84_LOADER_SAT_STAGE84_PLAN | \
                                                   STAGE84_LOADER_SAT_XNU_COMPILE_GRAPH | \
                                                   STAGE84_LOADER_SAT_XNU_OBJECT_SUBSET | \
                                                   STAGE84_LOADER_SAT_XNU_LINK | \
                                                   STAGE84_LOADER_SAT_XNU_BOOTSTRAP_CONTRACT | \
                                                   STAGE84_LOADER_SAT_XNU_PMAP_BOOTSTRAP_CONTRACT | \
                                                   STAGE84_LOADER_SAT_XNU_PMAP_TABLE_DRYRUN_CONTRACT | \
                                                   STAGE84_LOADER_SAT_XNU_PMAP_PAGE_DRYRUN_CONTRACT | \
                                                   STAGE84_LOADER_SAT_XNU_PMAP_ATTR_DRYRUN_CONTRACT | \
                                                   STAGE84_LOADER_SAT_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT | \
                                                   STAGE84_LOADER_SAT_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT)

struct stage84_loader_preflight {
    uint32_t version;
    uint32_t size;
    uint32_t xnu_baseline_tag;
    uint32_t xnu_baseline_commit;
    uint32_t xnu_master_version;
    uint32_t xnu_arm_reference;
    struct stage84_macho_probe_result macho;
    struct stage84_macho_staging_plan staging;
    uint32_t materialized_status;
    uint32_t materialized_file_bytes;
    uint32_t materialized_zero_bytes;
    struct stage84_xnu_tte_dryrun tte_dryrun;
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
    struct stage84_xnu_workspace xnu_workspace;
    uint32_t xnu_workspace_status;
    uint32_t xnu_workspace_satisfied_mask;
    uint32_t xnu_workspace_failure_mask;
    uint32_t xnu_workspace_checksum;
    uint32_t cancro_target_status;
    uint32_t stage84_plan_status;
    struct stage84_xnu_compile_graph xnu_compile_graph;
    uint32_t xnu_compile_graph_status;
    uint32_t xnu_compile_graph_satisfied_mask;
    uint32_t xnu_compile_graph_failure_mask;
    uint32_t xnu_compile_graph_checksum;
    uint32_t xnu_compile_graph_status_rollup;
    struct stage84_xnu_object_subset xnu_object_subset;
    uint32_t xnu_object_subset_status;
    uint32_t xnu_object_subset_satisfied_mask;
    uint32_t xnu_object_subset_failure_mask;
    uint32_t xnu_object_subset_checksum;
    uint32_t xnu_object_subset_status_rollup;
    struct stage84_xnu_link xnu_link;
    uint32_t xnu_link_status;
    uint32_t xnu_link_satisfied_mask;
    uint32_t xnu_link_failure_mask;
    uint32_t xnu_link_checksum;
    uint32_t xnu_link_status_rollup;
    struct stage84_xnu_bootstrap_contract xnu_bootstrap_contract;
    uint32_t xnu_bootstrap_contract_status;
    uint32_t xnu_bootstrap_contract_satisfied_mask;
    uint32_t xnu_bootstrap_contract_failure_mask;
    uint32_t xnu_bootstrap_contract_checksum;
    uint32_t xnu_bootstrap_contract_status_rollup;
    struct stage84_xnu_pmap_bootstrap_contract xnu_pmap_bootstrap_contract;
    uint32_t xnu_pmap_bootstrap_contract_status;
    uint32_t xnu_pmap_bootstrap_contract_satisfied_mask;
    uint32_t xnu_pmap_bootstrap_contract_failure_mask;
    uint32_t xnu_pmap_bootstrap_contract_checksum;
    uint32_t xnu_pmap_bootstrap_contract_status_rollup;
    struct stage84_xnu_pmap_table_dryrun_contract xnu_pmap_table_dryrun_contract;
    uint32_t xnu_pmap_table_dryrun_contract_status;
    uint32_t xnu_pmap_table_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_table_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_table_dryrun_contract_checksum;
    uint32_t xnu_pmap_table_dryrun_contract_status_rollup;
    struct stage84_xnu_pmap_page_dryrun_contract xnu_pmap_page_dryrun_contract;
    uint32_t xnu_pmap_page_dryrun_contract_status;
    uint32_t xnu_pmap_page_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_page_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_page_dryrun_contract_checksum;
    uint32_t xnu_pmap_page_dryrun_contract_status_rollup;
    struct stage84_xnu_pmap_attr_dryrun_contract xnu_pmap_attr_dryrun_contract;
    uint32_t xnu_pmap_attr_dryrun_contract_status;
    uint32_t xnu_pmap_attr_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_attr_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_attr_dryrun_contract_checksum;
    uint32_t xnu_pmap_attr_dryrun_contract_status_rollup;
    struct stage84_xnu_pmap_multiwindow_dryrun_contract xnu_pmap_multiwindow_dryrun_contract;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_status;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_checksum;
    uint32_t xnu_pmap_multiwindow_dryrun_contract_status_rollup;
    struct stage84_xnu_pmap_transition_dryrun_contract xnu_pmap_transition_dryrun_contract;
    uint32_t xnu_pmap_transition_dryrun_contract_status;
    uint32_t xnu_pmap_transition_dryrun_contract_satisfied_mask;
    uint32_t xnu_pmap_transition_dryrun_contract_failure_mask;
    uint32_t xnu_pmap_transition_dryrun_contract_checksum;
    uint32_t xnu_pmap_transition_dryrun_contract_status_rollup;
    struct stage84_xnu_pexpert_hook_readiness_contract xnu_pexpert_hook_readiness_contract;
    uint32_t xnu_pexpert_hook_readiness_contract_status;
    uint32_t xnu_pexpert_hook_readiness_contract_satisfied_mask;
    uint32_t xnu_pexpert_hook_readiness_contract_failure_mask;
    uint32_t xnu_pexpert_hook_readiness_contract_checksum;
    uint32_t xnu_pexpert_hook_readiness_contract_status_rollup;
    struct stage84_xnu_iokit_platform_scaffold_contract xnu_iokit_platform_scaffold_contract;
    uint32_t xnu_iokit_platform_scaffold_contract_status;
    uint32_t xnu_iokit_platform_scaffold_contract_satisfied_mask;
    uint32_t xnu_iokit_platform_scaffold_contract_failure_mask;
    uint32_t xnu_iokit_platform_scaffold_contract_checksum;
    uint32_t xnu_iokit_platform_scaffold_contract_status_rollup;
    struct stage84_xnu_iokit_match_dryrun_contract xnu_iokit_match_dryrun_contract;
    uint32_t xnu_iokit_match_dryrun_contract_status;
    uint32_t xnu_iokit_match_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_match_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_match_dryrun_contract_checksum;
    uint32_t xnu_iokit_match_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_registry_service_dryrun_contract xnu_iokit_registry_service_dryrun_contract;
    uint32_t xnu_iokit_registry_service_dryrun_contract_status;
    uint32_t xnu_iokit_registry_service_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_registry_service_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_registry_service_dryrun_contract_checksum;
    uint32_t xnu_iokit_registry_service_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_provider_plane_dryrun_contract xnu_iokit_provider_plane_dryrun_contract;
    uint32_t xnu_iokit_provider_plane_dryrun_contract_status;
    uint32_t xnu_iokit_provider_plane_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_provider_plane_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_provider_plane_dryrun_contract_checksum;
    uint32_t xnu_iokit_provider_plane_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_catalog_property_dryrun_contract xnu_iokit_catalog_property_dryrun_contract;
    uint32_t xnu_iokit_catalog_property_dryrun_contract_status;
    uint32_t xnu_iokit_catalog_property_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_catalog_property_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_catalog_property_dryrun_contract_checksum;
    uint32_t xnu_iokit_catalog_property_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_property_inheritance_dryrun_contract xnu_iokit_property_inheritance_dryrun_contract;
    uint32_t xnu_iokit_property_inheritance_dryrun_contract_status;
    uint32_t xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_property_inheritance_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_property_inheritance_dryrun_contract_checksum;
    uint32_t xnu_iokit_property_inheritance_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_registry_topology_dryrun_contract xnu_iokit_registry_topology_dryrun_contract;
    uint32_t xnu_iokit_registry_topology_dryrun_contract_status;
    uint32_t xnu_iokit_registry_topology_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_registry_topology_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_registry_topology_dryrun_contract_checksum;
    uint32_t xnu_iokit_registry_topology_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_attach_start_readiness_dryrun_contract xnu_iokit_attach_start_readiness_dryrun_contract;
    uint32_t xnu_iokit_attach_start_readiness_dryrun_contract_status;
    uint32_t xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_attach_start_readiness_dryrun_contract_checksum;
    uint32_t xnu_iokit_attach_start_readiness_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract xnu_iokit_lifecycle_register_service_readiness_dryrun_contract;
    uint32_t xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status;
    uint32_t xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_checksum;
    uint32_t xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract xnu_iokit_provider_notification_delivery_readiness_dryrun_contract;
    uint32_t xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status;
    uint32_t xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_checksum;
    uint32_t xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract;
    uint32_t xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status;
    uint32_t xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_checksum;
    uint32_t xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status_rollup;
    struct stage84_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract;
    uint32_t xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status;
    uint32_t xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_satisfied_mask;
    uint32_t xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_failure_mask;
    uint32_t xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_checksum;
    uint32_t xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status_rollup;
    struct stage84_xnu_entry_stub_result xnu_entry_stub;
    uint32_t xnu_entry_stub_status;
    uint32_t xnu_entry_stub_satisfied_mask;
    uint32_t xnu_entry_stub_failure_mask;
    uint32_t xnu_entry_stub_checksum;
    uint32_t xnu_entry_stub_status_rollup;
    struct stage84_xnu_early_pmap_platform_init_result xnu_early_pmap_platform_init;
    uint32_t xnu_early_pmap_platform_init_status;
    uint32_t xnu_early_pmap_platform_init_satisfied_mask;
    uint32_t xnu_early_pmap_platform_init_failure_mask;
    uint32_t xnu_early_pmap_platform_init_checksum;
    uint32_t xnu_early_pmap_platform_init_status_rollup;
    struct stage84_xnu_pe_init_platform_false_result xnu_pe_init_platform_false;
    uint32_t xnu_pe_init_platform_false_status;
    uint32_t xnu_pe_init_platform_false_satisfied_mask;
    uint32_t xnu_pe_init_platform_false_failure_mask;
    uint32_t xnu_pe_init_platform_false_checksum;
    uint32_t xnu_pe_init_platform_false_status_rollup;
    struct stage84_xnu_arm_init_post_pe_bootstrap_result xnu_arm_init_post_pe_bootstrap;
    uint32_t xnu_arm_init_post_pe_bootstrap_status;
    uint32_t xnu_arm_init_post_pe_bootstrap_satisfied_mask;
    uint32_t xnu_arm_init_post_pe_bootstrap_failure_mask;
    uint32_t xnu_arm_init_post_pe_bootstrap_checksum;
    uint32_t xnu_arm_init_post_pe_bootstrap_status_rollup;
    struct stage84_xnu_arm_vm_init_full_pmap_result xnu_arm_vm_init_full_pmap;
    uint32_t xnu_arm_vm_init_full_pmap_status;
    uint32_t xnu_arm_vm_init_full_pmap_satisfied_mask;
    uint32_t xnu_arm_vm_init_full_pmap_failure_mask;
    uint32_t xnu_arm_vm_init_full_pmap_checksum;
    uint32_t xnu_arm_vm_init_full_pmap_status_rollup;
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

extern struct pe_platform_state PE_state_stage84;
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

extern struct gic_state_snapshot GIC_state_stage84;
extern const uint8_t stage84_embedded_macho[];
extern const uint32_t stage84_embedded_macho_size;
int stage84_macho_probe(const void *artifact, uint32_t artifact_size,
                        struct stage84_macho_probe_result *result);
int stage84_loader_preflight_run(struct boot_args *args);
int stage84_xnu_bootstrap_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_bootstrap_contract *stage84_xnu_bootstrap_contract_result(void);
int stage84_xnu_pmap_bootstrap_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_pmap_bootstrap_contract *stage84_xnu_pmap_bootstrap_contract_result(void);
int stage84_xnu_pmap_table_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_pmap_table_dryrun_contract *stage84_xnu_pmap_table_dryrun_contract_result(void);
int stage84_xnu_pmap_page_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_pmap_page_dryrun_contract *stage84_xnu_pmap_page_dryrun_contract_result(void);
int stage84_xnu_pmap_attr_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_pmap_attr_dryrun_contract *stage84_xnu_pmap_attr_dryrun_contract_result(void);
int stage84_xnu_pmap_multiwindow_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_pmap_multiwindow_dryrun_contract *stage84_xnu_pmap_multiwindow_dryrun_contract_result(void);
int stage84_xnu_pmap_transition_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_pmap_transition_dryrun_contract *stage84_xnu_pmap_transition_dryrun_contract_result(void);
int stage84_xnu_pexpert_hook_readiness_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_pexpert_hook_readiness_contract *stage84_xnu_pexpert_hook_readiness_contract_result(void);
int stage84_xnu_iokit_platform_scaffold_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_platform_scaffold_contract *stage84_xnu_iokit_platform_scaffold_contract_result(void);
int stage84_xnu_iokit_match_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_match_dryrun_contract *stage84_xnu_iokit_match_dryrun_contract_result(void);
int stage84_xnu_iokit_registry_service_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_registry_service_dryrun_contract *stage84_xnu_iokit_registry_service_dryrun_contract_result(void);
int stage84_xnu_iokit_provider_plane_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_provider_plane_dryrun_contract *stage84_xnu_iokit_provider_plane_dryrun_contract_result(void);
int stage84_xnu_iokit_catalog_property_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_catalog_property_dryrun_contract *stage84_xnu_iokit_catalog_property_dryrun_contract_result(void);
int stage84_xnu_iokit_property_inheritance_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_property_inheritance_dryrun_contract *stage84_xnu_iokit_property_inheritance_dryrun_contract_result(void);
int stage84_xnu_iokit_registry_topology_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_registry_topology_dryrun_contract *stage84_xnu_iokit_registry_topology_dryrun_contract_result(void);
int stage84_xnu_iokit_attach_start_readiness_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_attach_start_readiness_dryrun_contract *stage84_xnu_iokit_attach_start_readiness_dryrun_contract_result(void);
int stage84_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract *stage84_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_result(void);
int stage84_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract *stage84_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_result(void);
int stage84_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract *stage84_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_result(void);
int stage84_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_selftest(const struct stage84_loader_preflight *preflight);
const struct stage84_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract *stage84_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_result(void);
uint32_t stage84_xnu_start_stub(struct boot_args *args, struct stage84_xnu_entry_stub_result *result);
uint32_t stage84_arm_init_stub(struct boot_args *args, struct stage84_xnu_entry_stub_result *result);
int stage84_xnu_entry_stub_run(struct boot_args *args);
const struct stage84_xnu_entry_stub_result *stage84_xnu_entry_stub_result(void);
int stage84_xnu_early_pmap_platform_init_run(struct boot_args *args,
                                            struct stage84_xnu_entry_stub_result *entry_result);
const struct stage84_xnu_early_pmap_platform_init_result *stage84_xnu_early_pmap_platform_init_result(void);
int stage84_xnu_pe_init_platform_false_run(struct boot_args *args,
                                           struct stage84_xnu_entry_stub_result *entry_result);
const struct stage84_xnu_pe_init_platform_false_result *stage84_xnu_pe_init_platform_false_result(void);
int stage84_xnu_arm_init_post_pe_bootstrap_run(struct boot_args *args,
                                               struct stage84_xnu_entry_stub_result *entry_result);
const struct stage84_xnu_arm_init_post_pe_bootstrap_result *stage84_xnu_arm_init_post_pe_bootstrap_result(void);
int stage84_xnu_arm_vm_init_full_pmap_run(struct boot_args *args,
                                         struct stage84_xnu_entry_stub_result *entry_result);
const struct stage84_xnu_arm_vm_init_full_pmap_result *stage84_xnu_arm_vm_init_full_pmap_result(void);
extern uint8_t stage84_stack_top[];
extern uint8_t stage84_irq_stack_top[];
extern uint8_t stage84_fiq_stack_top[];
extern volatile uint32_t stage84_irq_count;
extern volatile uint32_t stage84_last_iar;
extern volatile uint32_t stage84_last_irq_id;
extern volatile uint32_t stage84_sgi0_count;
extern volatile uint32_t stage84_spurious_irq_count;
extern volatile uint32_t stage84_timer_irq_count;
extern volatile uint32_t stage84_last_timer_irq_id;
extern volatile uint32_t stage84_last_timer_ctl;
extern volatile uint32_t stage84_other_irq_count;
extern volatile uint32_t stage84_sgi_selftest_passed;
extern volatile uint32_t stage84_sgi_irq_count_observed;
extern volatile uint32_t stage84_sgi_sgi0_count_observed;
extern volatile uint32_t stage84_sgi_last_irq_id_observed;
extern volatile uint32_t stage84_timer_selftest_passed;
extern volatile uint32_t stage84_timer_irq_count_observed;
extern volatile uint32_t stage84_timer_timer_count_observed;
extern volatile uint32_t stage84_timer_last_irq_id_observed;
extern volatile uint32_t stage84_timer_last_ctl_observed;
void gic_readonly_snapshot(uint32_t dist_base, uint32_t cpu_base);
void gic_log_snapshot(void);
int gic_validate_snapshot(void);
void stage84_irq_c_handler(void);
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
int mmu_stage84_ttbr0_roundtrip_selftest(void);
const struct stage84_ttbr0_roundtrip *mmu_stage84_ttbr0_roundtrip_result(void);
const struct stage84_pmap_bootstrap_snapshot *mmu_stage84_pmap_bootstrap_snapshot_result(void);

/* stage84_main.c */
void stage84_main(void) __attribute__((noreturn));
void platform_reboot(void) __attribute__((noreturn));
int test_kernel_entry(struct boot_args *args);

extern uint8_t __stage84_image_end[];
extern uint8_t stage84_vectors[];
void trigger_stage84_undef_test(void) __attribute__((noreturn));
void trigger_stage84_data_abort_test(void) __attribute__((noreturn));

#endif
