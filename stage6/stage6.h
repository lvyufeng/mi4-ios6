#ifndef MI4IOS6_STAGE6_H
#define MI4IOS6_STAGE6_H

#include <stdint.h>
#include <stddef.h>

#define STAGE6_BASE          0x00008000u
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

extern struct pe_platform_state PE_state_stage6;
void pe_state_init_from_boot_args(struct boot_args *args);
void pe_state_log(void);
int pe_state_validate(void);

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

/* stage6_main.c */
void stage6_main(void) __attribute__((noreturn));
void platform_reboot(void) __attribute__((noreturn));
int test_kernel_entry(struct boot_args *args);

extern uint8_t __stage6_image_end[];
extern uint8_t stage6_vectors[];
void trigger_stage6_undef_test(void) __attribute__((noreturn));
void trigger_stage6_data_abort_test(void) __attribute__((noreturn));

#endif
