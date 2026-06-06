#ifndef MI4IOS6_STAGE45_H
#define MI4IOS6_STAGE45_H

#include <stdint.h>
#include <stddef.h>

#define STAGE45_BASE          0x00008000u
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

#define STAGE45_STATUS_OK                         0x45000001u
#define STAGE45_STATUS_BASE                       0x45000000u

#define STAGE45_MACHO_PROBE_VERSION               1u
#define STAGE45_MACHO_MAGIC                       0xfeedfaceu
#define STAGE45_MACHO_CIGAM                       0xcefaedfeu
#define STAGE45_MACHO_CPU_TYPE_ARM                12u
#define STAGE45_MACHO_CPU_SUBTYPE_ARM_ALL         0u
#define STAGE45_MACHO_CPU_SUBTYPE_ARM_V7          9u
#define STAGE45_MACHO_FILETYPE_EXECUTE            2u
#define STAGE45_MACHO_FILETYPE_PRELOAD            5u
#define STAGE45_MACHO_LC_SEGMENT                  1u
#define STAGE45_MACHO_LC_SYMTAB                   2u
#define STAGE45_MACHO_LC_UNIXTHREAD               5u
#define STAGE45_MACHO_LC_MAIN                     0x80000028u

#define STAGE45_MACHO_CMD_SEGMENT                 0x00000001u
#define STAGE45_MACHO_CMD_SYMTAB                  0x00000002u
#define STAGE45_MACHO_CMD_UNIXTHREAD              0x00000004u
#define STAGE45_MACHO_CMD_MAIN                    0x00000008u
#define STAGE45_MACHO_CMD_UNKNOWN                 0x80000000u

#define STAGE45_MACHO_SEG_TEXT                    0x00000001u
#define STAGE45_MACHO_SEG_DATA                    0x00000002u
#define STAGE45_MACHO_SEG_LINKEDIT                0x00000004u
#define STAGE45_MACHO_SEG_KLD                     0x00000008u
#define STAGE45_MACHO_SEG_LAST                    0x00000010u
#define STAGE45_MACHO_SEG_PRELINK_TEXT            0x00000020u
#define STAGE45_MACHO_SEG_PRELINK_INFO            0x00000040u
#define STAGE45_MACHO_SEG_PRELINK_STATE           0x00000080u
#define STAGE45_MACHO_SEG_PRELINK                 0x00000100u
#define STAGE45_MACHO_SEG_PRELINK_MASK            (STAGE45_MACHO_SEG_PRELINK_TEXT | \
                                                   STAGE45_MACHO_SEG_PRELINK_INFO | \
                                                   STAGE45_MACHO_SEG_PRELINK_STATE | \
                                                   STAGE45_MACHO_SEG_PRELINK)
#define STAGE45_MACHO_SEG_REQUIRED                (STAGE45_MACHO_SEG_TEXT | \
                                                   STAGE45_MACHO_SEG_DATA | \
                                                   STAGE45_MACHO_SEG_LINKEDIT)

#define STAGE45_MACHO_ENTRY_UNIXTHREAD            0x00000001u
#define STAGE45_MACHO_ENTRY_MAIN                  0x00000002u
#define STAGE45_MACHO_ENTRY_NOT_EXECUTED          0x80000000u

#define STAGE45_MACHO_SECT_TEXT_TEXT              0x00000001u
#define STAGE45_MACHO_SECT_DATA_CONST             0x00000002u
#define STAGE45_MACHO_SECT_PRELINK_TEXT_TEXT      0x00000004u
#define STAGE45_MACHO_SECT_PRELINK_INFO_INFO      0x00000008u
#define STAGE45_MACHO_SECT_PRELINK_INFO_KERNEL    0x00000010u
#define STAGE45_MACHO_SECT_PRELINK_INFO_KEXTS     0x00000020u
#define STAGE45_MACHO_SECT_PRELINK_STATE_KERNEL   0x00000040u
#define STAGE45_MACHO_SECT_PRELINK_STATE_KEXTS    0x00000080u
#define STAGE45_MACHO_SECT_PRELINK_SYMTAB         0x00000100u
#define STAGE45_MACHO_SECT_PRELINK_MASK           (STAGE45_MACHO_SECT_PRELINK_TEXT_TEXT | \
                                                   STAGE45_MACHO_SECT_PRELINK_INFO_INFO | \
                                                   STAGE45_MACHO_SECT_PRELINK_INFO_KERNEL | \
                                                   STAGE45_MACHO_SECT_PRELINK_INFO_KEXTS | \
                                                   STAGE45_MACHO_SECT_PRELINK_STATE_KERNEL | \
                                                   STAGE45_MACHO_SECT_PRELINK_STATE_KEXTS | \
                                                   STAGE45_MACHO_SECT_PRELINK_SYMTAB)
#define STAGE45_MACHO_SECT_REQUIRED               (STAGE45_MACHO_SECT_TEXT_TEXT | \
                                                   STAGE45_MACHO_SECT_DATA_CONST)

#define STAGE45_MACHO_VALID_MAGIC                 0x00000001u
#define STAGE45_MACHO_VALID_ENDIAN                0x00000002u
#define STAGE45_MACHO_VALID_CPU                   0x00000004u
#define STAGE45_MACHO_VALID_SUBTYPE               0x00000008u
#define STAGE45_MACHO_VALID_FILETYPE              0x00000010u
#define STAGE45_MACHO_VALID_COMMAND_BOUNDS        0x00000020u
#define STAGE45_MACHO_VALID_SEGMENT_COMMAND       0x00000040u
#define STAGE45_MACHO_VALID_REQUIRED_SEGMENTS     0x00000080u
#define STAGE45_MACHO_VALID_FILE_EXTENTS          0x00000100u
#define STAGE45_MACHO_VALID_ENTRY_NOT_EXECUTED    0x00000200u
#define STAGE45_MACHO_VALID_SECTION_BOUNDS        0x00000400u
#define STAGE45_MACHO_VALID_REQUIRED_SECTIONS     0x00000800u
#define STAGE45_MACHO_VALID_LOAD_PLAN             0x00001000u
#define STAGE45_MACHO_VALID_PRELINK_REPORT        0x00002000u
#define STAGE45_MACHO_VALID_STAGING               0x00004000u
#define STAGE45_MACHO_VALID_MATERIALIZED_PARSE    0x00008000u
#define STAGE45_MACHO_VALID_MARKERS               0x00010000u
#define STAGE45_MACHO_VALID_ZERO_FILL             0x00020000u
#define STAGE45_MACHO_VALID_REQUIRED              (STAGE45_MACHO_VALID_MAGIC | \
                                                   STAGE45_MACHO_VALID_ENDIAN | \
                                                   STAGE45_MACHO_VALID_CPU | \
                                                   STAGE45_MACHO_VALID_SUBTYPE | \
                                                   STAGE45_MACHO_VALID_FILETYPE | \
                                                   STAGE45_MACHO_VALID_COMMAND_BOUNDS | \
                                                   STAGE45_MACHO_VALID_SEGMENT_COMMAND | \
                                                   STAGE45_MACHO_VALID_REQUIRED_SEGMENTS | \
                                                   STAGE45_MACHO_VALID_FILE_EXTENTS | \
                                                   STAGE45_MACHO_VALID_ENTRY_NOT_EXECUTED | \
                                                   STAGE45_MACHO_VALID_SECTION_BOUNDS | \
                                                   STAGE45_MACHO_VALID_REQUIRED_SECTIONS | \
                                                   STAGE45_MACHO_VALID_LOAD_PLAN | \
                                                   STAGE45_MACHO_VALID_STAGING | \
                                                   STAGE45_MACHO_VALID_MATERIALIZED_PARSE | \
                                                   STAGE45_MACHO_VALID_MARKERS | \
                                                   STAGE45_MACHO_VALID_ZERO_FILL)

#define STAGE45_MACHO_FAIL_NULL                   0x00000001u
#define STAGE45_MACHO_FAIL_SIZE                   0x00000002u
#define STAGE45_MACHO_FAIL_MAGIC                  0x00000004u
#define STAGE45_MACHO_FAIL_ENDIAN                 0x00000008u
#define STAGE45_MACHO_FAIL_CPU                    0x00000010u
#define STAGE45_MACHO_FAIL_SUBTYPE                0x00000020u
#define STAGE45_MACHO_FAIL_FILETYPE               0x00000040u
#define STAGE45_MACHO_FAIL_COMMAND_BOUNDS         0x00000080u
#define STAGE45_MACHO_FAIL_COMMAND_SIZE           0x00000100u
#define STAGE45_MACHO_FAIL_SEGMENT_BOUNDS         0x00000200u
#define STAGE45_MACHO_FAIL_SEGMENT_EXTENT         0x00000400u
#define STAGE45_MACHO_FAIL_REQUIRED_SEGMENTS      0x00000800u
#define STAGE45_MACHO_FAIL_NO_ENTRY_METADATA      0x00001000u
#define STAGE45_MACHO_FAIL_SECTION_BOUNDS         0x00002000u
#define STAGE45_MACHO_FAIL_REQUIRED_SECTIONS      0x00004000u
#define STAGE45_MACHO_FAIL_LOAD_PLAN              0x00008000u
#define STAGE45_MACHO_FAIL_PHYS_RANGE             0x00010000u
#define STAGE45_MACHO_FAIL_LOAD_PLAN_FULL         0x00020000u
#define STAGE45_MACHO_FAIL_STAGE_SPAN             0x00040000u
#define STAGE45_MACHO_FAIL_STAGE_PLAN_FULL        0x00080000u
#define STAGE45_MACHO_FAIL_STAGE_SRC_BOUNDS       0x00100000u
#define STAGE45_MACHO_FAIL_STAGE_DST_BOUNDS       0x00200000u
#define STAGE45_MACHO_FAIL_STAGE_COPY             0x00400000u
#define STAGE45_MACHO_FAIL_STAGE_REPARSE          0x00800000u
#define STAGE45_MACHO_FAIL_STAGE_MARKERS          0x01000000u
#define STAGE45_MACHO_FAIL_STAGE_ZERO_FILL        0x02000000u
#define STAGE45_MACHO_FAIL_STAGE_ARENA_ALIAS      0x04000000u

#define STAGE45_MACHO_MARKER_TEXT                 0x00000001u
#define STAGE45_MACHO_MARKER_DATA                 0x00000002u
#define STAGE45_MACHO_MARKER_PRELINK_TEXT         0x00000004u
#define STAGE45_MACHO_MARKER_REQUIRED             (STAGE45_MACHO_MARKER_TEXT | \
                                                   STAGE45_MACHO_MARKER_DATA | \
                                                   STAGE45_MACHO_MARKER_PRELINK_TEXT)

#define STAGE45_MACHO_LOAD_PLAN_VERSION           1u
#define STAGE45_MACHO_LOAD_PLAN_MAX               8u
#define STAGE45_MACHO_LOAD_PLAN_FILE_BACKED       0x00000001u
#define STAGE45_MACHO_LOAD_PLAN_PRELINK           0x00000002u
#define STAGE45_MACHO_LOAD_PLAN_ZERO_FILL         0x00000004u

#define STAGE45_MACHO_STAGING_ARENA_CAP           0x00010000u
#define STAGE45_MACHO_STAGING_ARENA_ALIGN         4096u
#define STAGE45_MACHO_STAGING_PLAN_VERSION        1u
#define STAGE45_MACHO_STAGING_PLAN_MAX            STAGE45_MACHO_LOAD_PLAN_MAX

struct stage45_macho_load_plan_entry {
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

struct stage45_macho_load_plan {
    uint32_t version;
    uint32_t entry_count;
    uint32_t vm_base;
    uint32_t vm_end;
    uint32_t phys_base;
    uint32_t phys_end;
    uint32_t file_end;
    uint32_t status;
    struct stage45_macho_load_plan_entry entries[STAGE45_MACHO_LOAD_PLAN_MAX];
};

struct stage45_macho_staging_plan_entry {
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

struct stage45_macho_staging_plan {
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
    struct stage45_macho_staging_plan_entry entries[STAGE45_MACHO_STAGING_PLAN_MAX];
};

struct stage45_macho_probe_result {
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
    struct stage45_macho_load_plan load_plan;
    struct stage45_macho_staging_plan staging_plan;
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

#define STAGE45_LOADER_PREFLIGHT_VERSION          1u
#define STAGE45_XNU_BASELINE_2050_22_13           0x20502213u
#define STAGE45_XNU_BASELINE_COMMIT_CC8A9B0C      0xcc8a9b0cu
#define STAGE45_XNU_BASELINE_MASTER_12_3_0        0x000c0300u
#define STAGE45_XNU_ARM_REFERENCE_4570_1_46       0x45700146u

#define STAGE45_XNU_TTE_WORKSPACE_PAGES           10u
#define STAGE45_XNU_TTE_WORKSPACE_BYTES           (STAGE45_XNU_TTE_WORKSPACE_PAGES * 4096u)
#define STAGE45_XNU_TTE_ALIGNMENT                 0x00004000u

#define STAGE45_DT_READY_BINARY_SELFTEST          0x00000001u
#define STAGE45_DT_READY_CHOSEN                   0x00000002u
#define STAGE45_DT_READY_MEMORY                   0x00000004u
#define STAGE45_DT_READY_CPUS                     0x00000008u
#define STAGE45_DT_READY_INTERRUPT_CONTROLLER     0x00000010u
#define STAGE45_DT_READY_TIMER                    0x00000020u
#define STAGE45_DT_READY_DEVICE_TREE_NODE         0x00000040u
#define STAGE45_DT_READY_TARGET_TYPE              0x00000080u
#define STAGE45_DT_READY_MODEL                    0x00000100u
#define STAGE45_DT_READY_BOOT_ARGS                0x00000200u
#define STAGE45_DT_READY_RAM_CONSOLE              0x00000400u
#define STAGE45_DT_READY_CPU_CLOCKS               0x00000800u
#define STAGE45_DT_READY_REQUIRED                 (STAGE45_DT_READY_BINARY_SELFTEST | \
                                                   STAGE45_DT_READY_CHOSEN | \
                                                   STAGE45_DT_READY_MEMORY | \
                                                   STAGE45_DT_READY_CPUS | \
                                                   STAGE45_DT_READY_INTERRUPT_CONTROLLER | \
                                                   STAGE45_DT_READY_TIMER | \
                                                   STAGE45_DT_READY_DEVICE_TREE_NODE | \
                                                   STAGE45_DT_READY_TARGET_TYPE | \
                                                   STAGE45_DT_READY_MODEL | \
                                                   STAGE45_DT_READY_BOOT_ARGS | \
                                                   STAGE45_DT_READY_RAM_CONSOLE | \
                                                   STAGE45_DT_READY_CPU_CLOCKS)

#define STAGE45_PLATFORM_GAP_PMAP_BOOTSTRAP       0x00000001u
#define STAGE45_PLATFORM_GAP_PEXPERT_IMPL         0x00000002u
#define STAGE45_PLATFORM_GAP_GIC_HOOK             0x00000004u
#define STAGE45_PLATFORM_GAP_TIMER_HOOK           0x00000008u
#define STAGE45_PLATFORM_GAP_IOKIT_STACK          0x00000010u
#define STAGE45_PLATFORM_GAP_REQUIRED_RECORDED    (STAGE45_PLATFORM_GAP_PMAP_BOOTSTRAP | \
                                                   STAGE45_PLATFORM_GAP_PEXPERT_IMPL | \
                                                   STAGE45_PLATFORM_GAP_GIC_HOOK | \
                                                   STAGE45_PLATFORM_GAP_TIMER_HOOK | \
                                                   STAGE45_PLATFORM_GAP_IOKIT_STACK)

#define STAGE45_LOADER_IRQ_READY_GIC_DIST         0x00000001u
#define STAGE45_LOADER_IRQ_READY_GIC_CPU          0x00000002u
#define STAGE45_LOADER_IRQ_READY_SERVICE          0x00000004u
#define STAGE45_LOADER_IRQ_READY_TIMEBASE         0x00000008u
#define STAGE45_LOADER_IRQ_READY_REQUIRED         (STAGE45_LOADER_IRQ_READY_GIC_DIST | \
                                                   STAGE45_LOADER_IRQ_READY_GIC_CPU | \
                                                   STAGE45_LOADER_IRQ_READY_SERVICE | \
                                                   STAGE45_LOADER_IRQ_READY_TIMEBASE)

#define STAGE45_LOADER_SAFETY_NO_EXECUTE          0x00000001u
#define STAGE45_LOADER_SAFETY_NO_TTBR_SWITCH      0x00000002u
#define STAGE45_LOADER_SAFETY_CACHES_UNCHANGED    0x00000004u
#define STAGE45_LOADER_SAFETY_NO_PERSIST_WRITE    0x00000008u
#define STAGE45_LOADER_SAFETY_RAM_CONSOLE         0x00000010u
#define STAGE45_LOADER_SAFETY_LOCAL_ARENA_ONLY    0x00000020u
#define STAGE45_LOADER_SAFETY_NO_PHYS_WRITE       0x00000040u
#define STAGE45_LOADER_SAFETY_REQUIRED            (STAGE45_LOADER_SAFETY_NO_EXECUTE | \
                                                   STAGE45_LOADER_SAFETY_NO_TTBR_SWITCH | \
                                                   STAGE45_LOADER_SAFETY_CACHES_UNCHANGED | \
                                                   STAGE45_LOADER_SAFETY_NO_PERSIST_WRITE | \
                                                   STAGE45_LOADER_SAFETY_RAM_CONSOLE | \
                                                   STAGE45_LOADER_SAFETY_LOCAL_ARENA_ONLY | \
                                                   STAGE45_LOADER_SAFETY_NO_PHYS_WRITE)

#define STAGE45_LOADER_SAT_BASELINE               0x00000001u
#define STAGE45_LOADER_SAT_MACHO                  0x00000002u
#define STAGE45_LOADER_SAT_BOOT_ARGS              0x00000004u
#define STAGE45_LOADER_SAT_WORKSPACE              0x00000008u
#define STAGE45_LOADER_SAT_DT                     0x00000010u
#define STAGE45_LOADER_SAT_PLATFORM_GAPS          0x00000020u
#define STAGE45_LOADER_SAT_INTERRUPTS             0x00000040u
#define STAGE45_LOADER_SAT_SAFETY                 0x00000080u
#define STAGE45_LOADER_SAT_MATERIALIZED           0x00000100u
#define STAGE45_LOADER_SAT_REQUIRED               (STAGE45_LOADER_SAT_BASELINE | \
                                                   STAGE45_LOADER_SAT_MACHO | \
                                                   STAGE45_LOADER_SAT_BOOT_ARGS | \
                                                   STAGE45_LOADER_SAT_WORKSPACE | \
                                                   STAGE45_LOADER_SAT_DT | \
                                                   STAGE45_LOADER_SAT_PLATFORM_GAPS | \
                                                   STAGE45_LOADER_SAT_INTERRUPTS | \
                                                   STAGE45_LOADER_SAT_SAFETY | \
                                                   STAGE45_LOADER_SAT_MATERIALIZED)

struct stage45_loader_preflight {
    uint32_t version;
    uint32_t size;
    uint32_t xnu_baseline_tag;
    uint32_t xnu_baseline_commit;
    uint32_t xnu_master_version;
    uint32_t xnu_arm_reference;
    struct stage45_macho_probe_result macho;
    struct stage45_macho_staging_plan staging;
    uint32_t materialized_status;
    uint32_t materialized_file_bytes;
    uint32_t materialized_zero_bytes;
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

extern struct pe_platform_state PE_state_stage45;
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

extern struct gic_state_snapshot GIC_state_stage45;
extern const uint8_t stage45_embedded_macho[];
extern const uint32_t stage45_embedded_macho_size;
int stage45_macho_probe(const void *artifact, uint32_t artifact_size,
                        struct stage45_macho_probe_result *result);
int stage45_loader_preflight_run(struct boot_args *args);
extern volatile uint32_t stage45_irq_count;
extern volatile uint32_t stage45_last_iar;
extern volatile uint32_t stage45_last_irq_id;
extern volatile uint32_t stage45_timer_irq_count;
extern volatile uint32_t stage45_last_timer_irq_id;
void gic_readonly_snapshot(uint32_t dist_base, uint32_t cpu_base);
void gic_log_snapshot(void);
int gic_validate_snapshot(void);
void stage45_irq_c_handler(void);
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

/* stage45_main.c */
void stage45_main(void) __attribute__((noreturn));
void platform_reboot(void) __attribute__((noreturn));
int test_kernel_entry(struct boot_args *args);

extern uint8_t __stage45_image_end[];
extern uint8_t stage45_vectors[];
void trigger_stage45_undef_test(void) __attribute__((noreturn));
void trigger_stage45_data_abort_test(void) __attribute__((noreturn));

#endif
