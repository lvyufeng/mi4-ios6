#ifndef MI4IOS6_STAGE43_H
#define MI4IOS6_STAGE43_H

#include <stdint.h>
#include <stddef.h>

#define STAGE43_BASE          0x00008000u
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

#define STAGE43_STATUS_OK                         0x43000001u
#define STAGE43_STATUS_BASE                       0x43000000u

#define STAGE43_MACHO_PROBE_VERSION               1u
#define STAGE43_MACHO_MAGIC                       0xfeedfaceu
#define STAGE43_MACHO_CIGAM                       0xcefaedfeu
#define STAGE43_MACHO_CPU_TYPE_ARM                12u
#define STAGE43_MACHO_CPU_SUBTYPE_ARM_ALL         0u
#define STAGE43_MACHO_CPU_SUBTYPE_ARM_V7          9u
#define STAGE43_MACHO_FILETYPE_EXECUTE            2u
#define STAGE43_MACHO_FILETYPE_PRELOAD            5u
#define STAGE43_MACHO_LC_SEGMENT                  1u
#define STAGE43_MACHO_LC_SYMTAB                   2u
#define STAGE43_MACHO_LC_UNIXTHREAD               5u
#define STAGE43_MACHO_LC_MAIN                     0x80000028u

#define STAGE43_MACHO_CMD_SEGMENT                 0x00000001u
#define STAGE43_MACHO_CMD_SYMTAB                  0x00000002u
#define STAGE43_MACHO_CMD_UNIXTHREAD              0x00000004u
#define STAGE43_MACHO_CMD_MAIN                    0x00000008u
#define STAGE43_MACHO_CMD_UNKNOWN                 0x80000000u

#define STAGE43_MACHO_SEG_TEXT                    0x00000001u
#define STAGE43_MACHO_SEG_DATA                    0x00000002u
#define STAGE43_MACHO_SEG_LINKEDIT                0x00000004u
#define STAGE43_MACHO_SEG_KLD                     0x00000008u
#define STAGE43_MACHO_SEG_LAST                    0x00000010u
#define STAGE43_MACHO_SEG_PRELINK_TEXT            0x00000020u
#define STAGE43_MACHO_SEG_PRELINK_INFO            0x00000040u
#define STAGE43_MACHO_SEG_REQUIRED                (STAGE43_MACHO_SEG_TEXT | \
                                                   STAGE43_MACHO_SEG_DATA | \
                                                   STAGE43_MACHO_SEG_LINKEDIT)

#define STAGE43_MACHO_ENTRY_UNIXTHREAD            0x00000001u
#define STAGE43_MACHO_ENTRY_MAIN                  0x00000002u
#define STAGE43_MACHO_ENTRY_NOT_EXECUTED          0x80000000u

#define STAGE43_MACHO_VALID_MAGIC                 0x00000001u
#define STAGE43_MACHO_VALID_ENDIAN                0x00000002u
#define STAGE43_MACHO_VALID_CPU                   0x00000004u
#define STAGE43_MACHO_VALID_SUBTYPE               0x00000008u
#define STAGE43_MACHO_VALID_FILETYPE              0x00000010u
#define STAGE43_MACHO_VALID_COMMAND_BOUNDS        0x00000020u
#define STAGE43_MACHO_VALID_SEGMENT_COMMAND       0x00000040u
#define STAGE43_MACHO_VALID_REQUIRED_SEGMENTS     0x00000080u
#define STAGE43_MACHO_VALID_FILE_EXTENTS          0x00000100u
#define STAGE43_MACHO_VALID_ENTRY_NOT_EXECUTED    0x00000200u
#define STAGE43_MACHO_VALID_REQUIRED              (STAGE43_MACHO_VALID_MAGIC | \
                                                   STAGE43_MACHO_VALID_ENDIAN | \
                                                   STAGE43_MACHO_VALID_CPU | \
                                                   STAGE43_MACHO_VALID_SUBTYPE | \
                                                   STAGE43_MACHO_VALID_FILETYPE | \
                                                   STAGE43_MACHO_VALID_COMMAND_BOUNDS | \
                                                   STAGE43_MACHO_VALID_SEGMENT_COMMAND | \
                                                   STAGE43_MACHO_VALID_REQUIRED_SEGMENTS | \
                                                   STAGE43_MACHO_VALID_FILE_EXTENTS | \
                                                   STAGE43_MACHO_VALID_ENTRY_NOT_EXECUTED)

#define STAGE43_MACHO_FAIL_NULL                   0x00000001u
#define STAGE43_MACHO_FAIL_SIZE                   0x00000002u
#define STAGE43_MACHO_FAIL_MAGIC                  0x00000004u
#define STAGE43_MACHO_FAIL_ENDIAN                 0x00000008u
#define STAGE43_MACHO_FAIL_CPU                    0x00000010u
#define STAGE43_MACHO_FAIL_SUBTYPE                0x00000020u
#define STAGE43_MACHO_FAIL_FILETYPE               0x00000040u
#define STAGE43_MACHO_FAIL_COMMAND_BOUNDS         0x00000080u
#define STAGE43_MACHO_FAIL_COMMAND_SIZE           0x00000100u
#define STAGE43_MACHO_FAIL_SEGMENT_BOUNDS         0x00000200u
#define STAGE43_MACHO_FAIL_SEGMENT_EXTENT         0x00000400u
#define STAGE43_MACHO_FAIL_REQUIRED_SEGMENTS      0x00000800u
#define STAGE43_MACHO_FAIL_NO_ENTRY_METADATA      0x00001000u

struct stage43_macho_probe_result {
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
    uint32_t min_vmaddr;
    uint32_t max_vmaddr;
    uint32_t max_file_extent;
    uint32_t validation_mask;
    uint32_t failure_mask;
    uint32_t status;
};

#define STAGE43_LOADER_PREFLIGHT_VERSION          1u
#define STAGE43_XNU_BASELINE_2050_22_13           0x20502213u
#define STAGE43_XNU_BASELINE_COMMIT_CC8A9B0C      0xcc8a9b0cu
#define STAGE43_XNU_BASELINE_MASTER_12_3_0        0x000c0300u
#define STAGE43_XNU_ARM_REFERENCE_4570_1_46       0x45700146u

#define STAGE43_XNU_TTE_WORKSPACE_PAGES           10u
#define STAGE43_XNU_TTE_WORKSPACE_BYTES           (STAGE43_XNU_TTE_WORKSPACE_PAGES * 4096u)
#define STAGE43_XNU_TTE_ALIGNMENT                 0x00004000u

#define STAGE43_DT_READY_BINARY_SELFTEST          0x00000001u
#define STAGE43_DT_READY_CHOSEN                   0x00000002u
#define STAGE43_DT_READY_MEMORY                   0x00000004u
#define STAGE43_DT_READY_CPUS                     0x00000008u
#define STAGE43_DT_READY_INTERRUPT_CONTROLLER     0x00000010u
#define STAGE43_DT_READY_TIMER                    0x00000020u
#define STAGE43_DT_READY_DEVICE_TREE_NODE         0x00000040u
#define STAGE43_DT_READY_TARGET_TYPE              0x00000080u
#define STAGE43_DT_READY_MODEL                    0x00000100u
#define STAGE43_DT_READY_BOOT_ARGS                0x00000200u
#define STAGE43_DT_READY_RAM_CONSOLE              0x00000400u
#define STAGE43_DT_READY_CPU_CLOCKS               0x00000800u
#define STAGE43_DT_READY_REQUIRED                 (STAGE43_DT_READY_BINARY_SELFTEST | \
                                                   STAGE43_DT_READY_CHOSEN | \
                                                   STAGE43_DT_READY_MEMORY | \
                                                   STAGE43_DT_READY_CPUS | \
                                                   STAGE43_DT_READY_INTERRUPT_CONTROLLER | \
                                                   STAGE43_DT_READY_TIMER | \
                                                   STAGE43_DT_READY_DEVICE_TREE_NODE | \
                                                   STAGE43_DT_READY_TARGET_TYPE | \
                                                   STAGE43_DT_READY_MODEL | \
                                                   STAGE43_DT_READY_BOOT_ARGS | \
                                                   STAGE43_DT_READY_RAM_CONSOLE | \
                                                   STAGE43_DT_READY_CPU_CLOCKS)

#define STAGE43_PLATFORM_GAP_PMAP_BOOTSTRAP       0x00000001u
#define STAGE43_PLATFORM_GAP_PEXPERT_IMPL         0x00000002u
#define STAGE43_PLATFORM_GAP_GIC_HOOK             0x00000004u
#define STAGE43_PLATFORM_GAP_TIMER_HOOK           0x00000008u
#define STAGE43_PLATFORM_GAP_IOKIT_STACK          0x00000010u
#define STAGE43_PLATFORM_GAP_REQUIRED_RECORDED    (STAGE43_PLATFORM_GAP_PMAP_BOOTSTRAP | \
                                                   STAGE43_PLATFORM_GAP_PEXPERT_IMPL | \
                                                   STAGE43_PLATFORM_GAP_GIC_HOOK | \
                                                   STAGE43_PLATFORM_GAP_TIMER_HOOK | \
                                                   STAGE43_PLATFORM_GAP_IOKIT_STACK)

#define STAGE43_LOADER_IRQ_READY_GIC_DIST         0x00000001u
#define STAGE43_LOADER_IRQ_READY_GIC_CPU          0x00000002u
#define STAGE43_LOADER_IRQ_READY_SERVICE          0x00000004u
#define STAGE43_LOADER_IRQ_READY_TIMEBASE         0x00000008u
#define STAGE43_LOADER_IRQ_READY_REQUIRED         (STAGE43_LOADER_IRQ_READY_GIC_DIST | \
                                                   STAGE43_LOADER_IRQ_READY_GIC_CPU | \
                                                   STAGE43_LOADER_IRQ_READY_SERVICE | \
                                                   STAGE43_LOADER_IRQ_READY_TIMEBASE)

#define STAGE43_LOADER_SAFETY_NO_EXECUTE          0x00000001u
#define STAGE43_LOADER_SAFETY_NO_TTBR_SWITCH      0x00000002u
#define STAGE43_LOADER_SAFETY_CACHES_UNCHANGED    0x00000004u
#define STAGE43_LOADER_SAFETY_NO_PERSIST_WRITE    0x00000008u
#define STAGE43_LOADER_SAFETY_RAM_CONSOLE         0x00000010u
#define STAGE43_LOADER_SAFETY_REQUIRED            (STAGE43_LOADER_SAFETY_NO_EXECUTE | \
                                                   STAGE43_LOADER_SAFETY_NO_TTBR_SWITCH | \
                                                   STAGE43_LOADER_SAFETY_CACHES_UNCHANGED | \
                                                   STAGE43_LOADER_SAFETY_NO_PERSIST_WRITE | \
                                                   STAGE43_LOADER_SAFETY_RAM_CONSOLE)

#define STAGE43_LOADER_SAT_BASELINE               0x00000001u
#define STAGE43_LOADER_SAT_MACHO                  0x00000002u
#define STAGE43_LOADER_SAT_BOOT_ARGS              0x00000004u
#define STAGE43_LOADER_SAT_WORKSPACE              0x00000008u
#define STAGE43_LOADER_SAT_DT                     0x00000010u
#define STAGE43_LOADER_SAT_PLATFORM_GAPS          0x00000020u
#define STAGE43_LOADER_SAT_INTERRUPTS             0x00000040u
#define STAGE43_LOADER_SAT_SAFETY                 0x00000080u
#define STAGE43_LOADER_SAT_REQUIRED               (STAGE43_LOADER_SAT_BASELINE | \
                                                   STAGE43_LOADER_SAT_MACHO | \
                                                   STAGE43_LOADER_SAT_BOOT_ARGS | \
                                                   STAGE43_LOADER_SAT_WORKSPACE | \
                                                   STAGE43_LOADER_SAT_DT | \
                                                   STAGE43_LOADER_SAT_PLATFORM_GAPS | \
                                                   STAGE43_LOADER_SAT_INTERRUPTS | \
                                                   STAGE43_LOADER_SAT_SAFETY)

struct stage43_loader_preflight {
    uint32_t version;
    uint32_t size;
    uint32_t xnu_baseline_tag;
    uint32_t xnu_baseline_commit;
    uint32_t xnu_master_version;
    uint32_t xnu_arm_reference;
    struct stage43_macho_probe_result macho;
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

extern struct pe_platform_state PE_state_stage43;
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

extern struct gic_state_snapshot GIC_state_stage43;
extern const uint8_t stage43_embedded_macho[];
extern const uint32_t stage43_embedded_macho_size;
int stage43_macho_probe(const void *artifact, uint32_t artifact_size,
                        struct stage43_macho_probe_result *result);
int stage43_loader_preflight_run(struct boot_args *args);
extern volatile uint32_t stage43_irq_count;
extern volatile uint32_t stage43_last_iar;
extern volatile uint32_t stage43_last_irq_id;
extern volatile uint32_t stage43_timer_irq_count;
extern volatile uint32_t stage43_last_timer_irq_id;
void gic_readonly_snapshot(uint32_t dist_base, uint32_t cpu_base);
void gic_log_snapshot(void);
int gic_validate_snapshot(void);
void stage43_irq_c_handler(void);
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

/* stage43_main.c */
void stage43_main(void) __attribute__((noreturn));
void platform_reboot(void) __attribute__((noreturn));
int test_kernel_entry(struct boot_args *args);

extern uint8_t __stage43_image_end[];
extern uint8_t stage43_vectors[];
void trigger_stage43_undef_test(void) __attribute__((noreturn));
void trigger_stage43_data_abort_test(void) __attribute__((noreturn));

#endif
