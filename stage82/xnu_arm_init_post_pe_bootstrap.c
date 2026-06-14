#include "stage82.h"

/*
 * Stage82 Stage-owned arm_init post-PE_init_platform(FALSE) bootstrap and
 * timebase-registration micro-sequence.
 *
 * This models the public ARM XNU arm_init region after PE_init_platform(FALSE,
 * args) and before arm_vm_init(xmaxmem, args).  It records local CPU topology,
 * BootCpuData/CpuDataEntries-shaped facts, bootstrap ordering, rtclock early
 * timebase callback registration, boot-arg parse facts, and an explicit
 * stop-before-arm_vm_init boundary.  It does not call or execute public XNU
 * bootstrap, rtclock, pmap, pexpert, IOKit, or arm_vm_init runtime code.
 */

struct stage82_boot_cpu_data_shape {
    uint32_t cpu_number;
    uint32_t cpu_count;
    uint32_t machine_type;
    uint32_t vector_base;
    uint32_t int_stack_top;
    uint32_t fiq_stack_top;
    uint32_t timer_base;
    uint32_t timer_frequency;
};

struct stage82_cpu_data_entry_shape {
    uint32_t cpu_number;
    uint32_t data_ptr;
    uint32_t data_phys;
    uint32_t active;
};

static struct stage82_xnu_arm_init_post_pe_bootstrap_result g_stage82_xnu_arm_init_post_pe_bootstrap_result;
static struct stage82_boot_cpu_data_shape g_stage82_boot_cpu_data_shape;
static struct stage82_cpu_data_entry_shape g_stage82_cpu_data_entries_shape[4];
static uint32_t g_stage82_timebase_callback_cookie;

static inline uint32_t stage82_post_pe_read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t stage82_post_pe_read_ttbcr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(v));
    return v;
}

static inline uint32_t stage82_post_pe_read_dacr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t stage82_post_pe_read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static uint32_t stage82_post_pe_checksum(
    const struct stage82_xnu_arm_init_post_pe_bootstrap_result *r)
{
    const uint32_t *words = (const uint32_t *)r;
    uint32_t checksum = 0u;
    uint32_t count = (uint32_t)(offsetof(struct stage82_xnu_arm_init_post_pe_bootstrap_result, checksum) /
                                sizeof(uint32_t));
    uint32_t i;

    for (i = 0u; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage82_post_pe_words_checksum(const uint32_t *words, uint32_t count, uint32_t seed)
{
    uint32_t checksum = seed;
    uint32_t i;

    for (i = 0u; i < count; i++) {
        checksum = (checksum << 5) ^ (checksum >> 27) ^ words[i];
    }

    return checksum;
}

static uint32_t stage82_post_pe_string_checksum(const char *s)
{
    uint32_t checksum = 0x8000feedu;

    if (!s) {
        return 0u;
    }

    while (*s) {
        checksum = (checksum << 5) ^ (checksum >> 27) ^ (uint32_t)(uint8_t)*s;
        s++;
    }

    return checksum;
}

static uint32_t stage82_post_pe_has_token(const char *s, const char *token)
{
    size_t token_len;

    if (!s || !token) {
        return 0u;
    }

    token_len = strlen(token);
    if (token_len == 0u) {
        return 0u;
    }

    for (const char *p = s; *p; p++) {
        size_t i = 0u;
        while (i < token_len && p[i] && p[i] == token[i]) {
            i++;
        }
        if (i == token_len) {
            return 1u;
        }
    }

    return 0u;
}

static uint32_t stage82_post_pe_parse_hex_after(const char *s, const char *key,
                                                uint32_t *present, uint32_t fallback)
{
    size_t key_len;
    uint32_t value = 0u;
    uint32_t saw_digit = 0u;

    if (present) {
        *present = 0u;
    }
    if (!s || !key) {
        return fallback;
    }

    key_len = strlen(key);
    for (const char *p = s; *p; p++) {
        size_t i = 0u;
        while (i < key_len && p[i] && p[i] == key[i]) {
            i++;
        }
        if (i != key_len) {
            continue;
        }

        p += key_len;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
        }
        while (*p) {
            uint32_t digit;
            if (*p >= '0' && *p <= '9') {
                digit = (uint32_t)(*p - '0');
            } else if (*p >= 'a' && *p <= 'f') {
                digit = (uint32_t)(*p - 'a') + 10u;
            } else if (*p >= 'A' && *p <= 'F') {
                digit = (uint32_t)(*p - 'A') + 10u;
            } else {
                break;
            }
            value = (value << 4) | digit;
            saw_digit = 1u;
            p++;
        }
        if (saw_digit == 1u) {
            if (present) {
                *present = 1u;
            }
            return value;
        }
        return fallback;
    }

    return fallback;
}

static void stage82_post_pe_timebase_callback_shape(void)
{
    g_stage82_timebase_callback_cookie ^= 0x5442434bu; /* 'TBCK' */
}

static void stage82_post_pe_log(const struct stage82_xnu_arm_init_post_pe_bootstrap_result *r)
{
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_status", r->status);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_required_mask", r->required_mask);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_satisfied_mask", r->satisfied_mask);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_failure_mask", r->failure_mask);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_checksum", r->checksum);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_source_pe_init_status", r->source_pe_init_status);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_source_pe_init_satisfied_mask", r->source_pe_init_satisfied_mask);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_source_pe_init_failure_mask", r->source_pe_init_failure_mask);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_source_pe_init_checksum", r->source_pe_init_checksum);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_source_early_init_status", r->source_early_init_status);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_args_ptr", r->boot_args_ptr);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_args_valid", r->boot_args_valid);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_command_line_checksum", r->command_line_checksum);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_command_line_postpe_marker", r->command_line_postpe_marker);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_command_line_cpu_topo_marker", r->command_line_cpu_topo_marker);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_command_line_bootcpu_marker", r->command_line_bootcpu_marker);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_command_line_rtclock_marker", r->command_line_rtclock_marker);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_command_line_armvm_marker", r->command_line_armvm_marker);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_command_line_no_public_markers", r->command_line_no_public_markers);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_pe_state_inherited", r->pe_state_inherited);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_pe_cpu_count", r->pe_cpu_count);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_pe_vector_base", r->pe_vector_base);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_cpu_topology_parsed", r->cpu_topology_parsed);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_cpu_count", r->cpu_count);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu", r->boot_cpu);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_max_cpu_number", r->max_cpu_number);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_master_cpu", r->master_cpu);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_master_cpu_valid", r->master_cpu_valid);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_ptr", r->boot_cpu_data_ptr);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_checksum", r->boot_cpu_data_checksum);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_cpu_number", r->boot_cpu_data_cpu_number);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_cpu_count", r->boot_cpu_data_cpu_count);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_vector_base", r->boot_cpu_data_vector_base);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_int_stack_top", r->boot_cpu_data_int_stack_top);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_fiq_stack_top", r->boot_cpu_data_fiq_stack_top);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_timer_base", r->boot_cpu_data_timer_base);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_timer_frequency", r->boot_cpu_data_timer_frequency);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_populated", r->boot_cpu_data_populated);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_cpu_data_entries_ptr", r->cpu_data_entries_ptr);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_cpu_data_entries_checksum", r->cpu_data_entries_checksum);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_cpu_data_entries_selected_index", r->cpu_data_entries_selected_index);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_cpu_data_entries_populated", r->cpu_data_entries_populated);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_thread_bootstrap_order", r->thread_bootstrap_order);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_cpu_bootstrap_order", r->cpu_bootstrap_order);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_rtclock_early_init_order", r->rtclock_early_init_order);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_kernel_early_bootstrap_order", r->kernel_early_bootstrap_order);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_cpu_init_order", r->cpu_init_order);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_processor_bootstrap_order", r->processor_bootstrap_order);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_bootstrap_order_valid", r->bootstrap_order_valid);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_timebase_callback_ptr", r->timebase_callback_ptr);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_timebase_callback_checksum", r->timebase_callback_checksum);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_timebase_callback_registered", r->timebase_callback_registered);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_timebase_frequency", r->timebase_frequency);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_timebase_initial_lo", r->timebase_initial_lo);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_timebase_initial_hi", r->timebase_initial_hi);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_pe_register_timebase_callback_shaped", r->pe_register_timebase_callback_shaped);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_arg_diag_present", r->boot_arg_diag_present);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_arg_diag_value", r->boot_arg_diag_value);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_arg_maxmem_present", r->boot_arg_maxmem_present);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_arg_maxmem_value", r->boot_arg_maxmem_value);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_arg_debug_present", r->boot_arg_debug_present);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_arg_debug_value", r->boot_arg_debug_value);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_arg_immediate_nmi_present", r->boot_arg_immediate_nmi_present);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_arg_immediate_nmi_value", r->boot_arg_immediate_nmi_value);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_boot_arg_parse_ok", r->boot_arg_parse_ok);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_stop_before_arm_vm_init", r->stop_before_arm_vm_init);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_arm_vm_init_not_called", r->arm_vm_init_not_called);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_public_thread_bootstrap_executed", r->public_thread_bootstrap_executed);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_public_cpu_bootstrap_executed", r->public_cpu_bootstrap_executed);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_public_rtclock_early_init_executed", r->public_rtclock_early_init_executed);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_public_arm_vm_init_executed", r->public_arm_vm_init_executed);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_control_registers_unchanged", r->control_registers_unchanged);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_live_pmap_installed", r->live_pmap_installed);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_tlb_invalidated", r->tlb_invalidated);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_cache_policy_changed", r->cache_policy_changed);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_persistent_write_attempted", r->persistent_write_attempted);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_stage_owned_local_only", r->stage_owned_local_only);
    xnu_log_kv32("stage82_xnu_arm_init_post_pe_bootstrap_safety_boundary_preserved", r->safety_boundary_preserved);
}

int stage82_xnu_arm_init_post_pe_bootstrap_run(
    struct boot_args *args,
    struct stage82_xnu_entry_stub_result *entry_result)
{
    struct stage82_xnu_arm_init_post_pe_bootstrap_result *r =
        &g_stage82_xnu_arm_init_post_pe_bootstrap_result;
    const struct stage82_xnu_pe_init_platform_false_result *pe;
    uint64_t now;
    uint32_t source_ok;
    uint32_t boot_args_ok;
    uint32_t pe_state_ok;
    uint32_t cpu_topology_ok;
    uint32_t master_cpu_ok;
    uint32_t boot_cpu_data_ok;
    uint32_t cpu_entries_ok;
    uint32_t stack_ok;
    uint32_t thread_ok;
    uint32_t cpu_boot_ok;
    uint32_t rtclock_ok;
    uint32_t kernel_cpu_processor_ok;
    uint32_t boot_arg_ok;
    uint32_t stop_ok;
    uint32_t public_bootstrap_ok;
    uint32_t public_rtclock_ok;
    uint32_t public_xnu_ok;
    uint32_t arm_vm_pmap_ok;
    uint32_t pexpert_iokit_ok;
    uint32_t macho_ok;
    uint32_t mutation_ok;
    uint32_t i;

    memset(r, 0, sizeof(*r));
    memset(&g_stage82_boot_cpu_data_shape, 0, sizeof(g_stage82_boot_cpu_data_shape));
    memset(g_stage82_cpu_data_entries_shape, 0, sizeof(g_stage82_cpu_data_entries_shape));

    r->version = STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE82_STATUS_BASE;
    r->required_mask = STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_REQUIRED_MASK;

    r->ttbr0_before = stage82_post_pe_read_ttbr0();
    r->ttbcr_before = stage82_post_pe_read_ttbcr();
    r->dacr_before = stage82_post_pe_read_dacr();
    r->sctlr_before = stage82_post_pe_read_sctlr();
    r->control_registers_sampled = 1u;

    pe = stage82_xnu_pe_init_platform_false_result();
    r->source_pe_init_status = pe->status;
    r->source_pe_init_satisfied_mask = pe->satisfied_mask;
    r->source_pe_init_failure_mask = pe->failure_mask;
    r->source_pe_init_checksum = pe->checksum;
    r->source_early_init_status = pe->source_early_init_status;
    r->source_early_init_checksum = pe->source_early_init_checksum;
    source_ok = (pe->status == STAGE82_STATUS_OK &&
                 pe->satisfied_mask == STAGE82_XNU_PE_INIT_PLATFORM_FALSE_REQUIRED_MASK &&
                 pe->failure_mask == 0u && pe->checksum != 0u) ? 1u : 0u;
    if (source_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_SOURCE_PE_INIT_OK;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_SOURCE_PE_INIT;
    }

    r->boot_args_ptr = (uint32_t)(uintptr_t)args;
    if (args) {
        r->boot_args_rev_ver = ((uint32_t)args->Version << 16) | args->Revision;
        r->boot_args_phys_base = args->physBase;
        r->boot_args_mem_size = args->memSize;
        r->boot_args_machine_type = args->machineType;
        r->boot_args_device_tree_ptr = (uint32_t)(uintptr_t)args->deviceTreeP;
        r->boot_args_device_tree_length = args->deviceTreeLength;
        r->command_line_checksum = stage82_post_pe_string_checksum(args->CommandLine);
        r->command_line_stage82_marker = stage82_post_pe_has_token(args->CommandLine, "mi4ios6.stage=82");
        r->command_line_postpe_marker = stage82_post_pe_has_token(args->CommandLine, "xnu-postpe");
        r->command_line_cpu_topo_marker = stage82_post_pe_has_token(args->CommandLine, "cpu-topo");
        r->command_line_bootcpu_marker = stage82_post_pe_has_token(args->CommandLine, "bootcpu");
        r->command_line_rtclock_marker = stage82_post_pe_has_token(args->CommandLine, "rtclock");
        r->command_line_armvm_marker = stage82_post_pe_has_token(args->CommandLine, "xnu-armvm");
        r->command_line_no_public_markers =
            (stage82_post_pe_has_token(args->CommandLine, "no-pub-thread") &&
             stage82_post_pe_has_token(args->CommandLine, "no-pub-cpuboot") &&
             stage82_post_pe_has_token(args->CommandLine, "no-pub-rtclock")) ? 1u : 0u;
    }

    boot_args_ok = (args && args->Revision == BOOT_ARGS_REVISION &&
                    args->Version == BOOT_ARGS_VERSION && args->physBase == STAGE82_BASE &&
                    args->memSize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE) &&
                    args->machineType == MACHINE_TYPE_MSM8974 && args->deviceTreeP != NULL &&
                    args->deviceTreeLength != 0u && r->command_line_stage82_marker == 1u &&
                    r->command_line_postpe_marker == 1u && r->command_line_cpu_topo_marker == 1u &&
                    r->command_line_bootcpu_marker == 1u && r->command_line_rtclock_marker == 1u &&
                    r->command_line_armvm_marker == 1u &&
                    r->command_line_no_public_markers == 1u) ? 1u : 0u;
    r->boot_args_valid = boot_args_ok;
    if (boot_args_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_BOOT_ARGS_VALID;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_BOOT_ARGS;
    }

    r->pe_boot_args_ptr = (uint32_t)(uintptr_t)PE_state_stage82.bootArgs;
    r->pe_device_tree_head = (uint32_t)(uintptr_t)PE_state_stage82.deviceTreeHead;
    r->pe_device_tree_length = PE_state_stage82.deviceTreeLength;
    r->pe_cpu_count = PE_state_stage82.cpuCount;
    r->pe_machine_type = PE_state_stage82.machineType;
    r->pe_vector_base = PE_state_stage82.vectorBase;
    r->pe_gic_dist_base = PE_state_stage82.gicDistributorBase;
    r->pe_gic_cpu_base = PE_state_stage82.gicCpuBase;
    r->pe_timer_base = PE_state_stage82.timerBase;
    r->pe_timer_frequency = PE_state_stage82.timerFrequency;
    r->pe_state_inherited = (PE_state_stage82.bootArgs == args &&
                             PE_state_stage82.deviceTreeHead == (args ? args->deviceTreeP : (void *)0) &&
                             PE_state_stage82.deviceTreeLength == (args ? args->deviceTreeLength : 0u) &&
                             PE_state_stage82.cpuCount == 4u &&
                             PE_state_stage82.machineType == MACHINE_TYPE_MSM8974 &&
                             PE_state_stage82.vectorBase != 0u &&
                             PE_state_stage82.gicDistributorBase == 0xf9000000u &&
                             PE_state_stage82.gicCpuBase == 0xf9002000u &&
                             PE_state_stage82.timerBase == 0xf9020000u &&
                             PE_state_stage82.timerFrequency == 19200000u) ? 1u : 0u;
    pe_state_ok = r->pe_state_inherited;
    if (pe_state_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_PE_STATE_INHERITED;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PE_STATE;
    }

    r->cpu_topology_parsed = (r->pe_cpu_count == 4u) ? 1u : 0u;
    r->cpu_count = r->pe_cpu_count;
    r->boot_cpu = 0u;
    r->max_cpu_number = (r->cpu_count != 0u) ? (r->cpu_count - 1u) : 0u;
    cpu_topology_ok = (r->cpu_topology_parsed == 1u && r->cpu_count == 4u && r->max_cpu_number == 3u) ? 1u : 0u;
    if (cpu_topology_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CPU_TOPOLOGY;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CPU_TOPOLOGY;
    }

    r->master_cpu = r->boot_cpu;
    r->master_cpu_valid = (r->master_cpu == 0u && r->master_cpu <= r->max_cpu_number) ? 1u : 0u;
    master_cpu_ok = r->master_cpu_valid;
    if (master_cpu_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_MASTER_CPU;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_MASTER_CPU;
    }

    g_stage82_boot_cpu_data_shape.cpu_number = r->master_cpu;
    g_stage82_boot_cpu_data_shape.cpu_count = r->cpu_count;
    g_stage82_boot_cpu_data_shape.machine_type = r->pe_machine_type;
    g_stage82_boot_cpu_data_shape.vector_base = r->pe_vector_base;
    g_stage82_boot_cpu_data_shape.int_stack_top = (uint32_t)(uintptr_t)stage82_irq_stack_top;
    g_stage82_boot_cpu_data_shape.fiq_stack_top = (uint32_t)(uintptr_t)stage82_fiq_stack_top;
    g_stage82_boot_cpu_data_shape.timer_base = r->pe_timer_base;
    g_stage82_boot_cpu_data_shape.timer_frequency = r->pe_timer_frequency;

    r->boot_cpu_data_ptr = (uint32_t)(uintptr_t)&g_stage82_boot_cpu_data_shape;
    r->boot_cpu_data_cpu_number = g_stage82_boot_cpu_data_shape.cpu_number;
    r->boot_cpu_data_cpu_count = g_stage82_boot_cpu_data_shape.cpu_count;
    r->boot_cpu_data_machine_type = g_stage82_boot_cpu_data_shape.machine_type;
    r->boot_cpu_data_vector_base = g_stage82_boot_cpu_data_shape.vector_base;
    r->boot_cpu_data_int_stack_top = g_stage82_boot_cpu_data_shape.int_stack_top;
    r->boot_cpu_data_fiq_stack_top = g_stage82_boot_cpu_data_shape.fiq_stack_top;
    r->boot_cpu_data_timer_base = g_stage82_boot_cpu_data_shape.timer_base;
    r->boot_cpu_data_timer_frequency = g_stage82_boot_cpu_data_shape.timer_frequency;
    r->boot_cpu_data_checksum = stage82_post_pe_words_checksum(
        (const uint32_t *)&g_stage82_boot_cpu_data_shape,
        (uint32_t)(sizeof(g_stage82_boot_cpu_data_shape) / sizeof(uint32_t)),
        0x42434441u);
    r->boot_cpu_data_populated = (r->boot_cpu_data_ptr != 0u &&
                                  r->boot_cpu_data_cpu_number == 0u &&
                                  r->boot_cpu_data_cpu_count == 4u &&
                                  r->boot_cpu_data_machine_type == MACHINE_TYPE_MSM8974 &&
                                  r->boot_cpu_data_vector_base != 0u &&
                                  r->boot_cpu_data_timer_frequency == 19200000u &&
                                  r->boot_cpu_data_checksum != 0u) ? 1u : 0u;
    boot_cpu_data_ok = r->boot_cpu_data_populated;
    if (boot_cpu_data_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_BOOT_CPU_DATA;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_BOOT_CPU_DATA;
    }

    stack_ok = (r->boot_cpu_data_vector_base != 0u &&
                r->boot_cpu_data_int_stack_top != 0u &&
                r->boot_cpu_data_fiq_stack_top != 0u &&
                r->boot_cpu_data_int_stack_top != r->boot_cpu_data_fiq_stack_top) ? 1u : 0u;
    if (stack_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_STACK_FACTS;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_STACK_FACTS;
    }

    for (i = 0u; i < ARRAY_SIZE(g_stage82_cpu_data_entries_shape); i++) {
        g_stage82_cpu_data_entries_shape[i].cpu_number = i;
        g_stage82_cpu_data_entries_shape[i].data_ptr = (i == r->master_cpu) ? r->boot_cpu_data_ptr : 0u;
        g_stage82_cpu_data_entries_shape[i].data_phys = g_stage82_cpu_data_entries_shape[i].data_ptr;
        g_stage82_cpu_data_entries_shape[i].active = (i == r->master_cpu) ? 1u : 0u;
    }
    r->cpu_data_entries_ptr = (uint32_t)(uintptr_t)g_stage82_cpu_data_entries_shape;
    r->cpu_data_entries_selected_index = r->master_cpu;
    r->cpu_data_entries_boot_entry_ptr = g_stage82_cpu_data_entries_shape[r->master_cpu].data_ptr;
    r->cpu_data_entries_boot_entry_phys = g_stage82_cpu_data_entries_shape[r->master_cpu].data_phys;
    r->cpu_data_entries_checksum = stage82_post_pe_words_checksum(
        (const uint32_t *)g_stage82_cpu_data_entries_shape,
        (uint32_t)(sizeof(g_stage82_cpu_data_entries_shape) / sizeof(uint32_t)),
        0x4344454eu);
    r->cpu_data_entries_self_consistent = (r->cpu_data_entries_ptr != 0u &&
                                           r->cpu_data_entries_selected_index == 0u &&
                                           r->cpu_data_entries_boot_entry_ptr == r->boot_cpu_data_ptr &&
                                           r->cpu_data_entries_boot_entry_phys == r->boot_cpu_data_ptr &&
                                           r->cpu_data_entries_checksum != 0u) ? 1u : 0u;
    r->cpu_data_entries_populated = r->cpu_data_entries_self_consistent;
    cpu_entries_ok = r->cpu_data_entries_populated;
    if (cpu_entries_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CPU_DATA_ENTRIES;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CPU_DATA_ENTRIES;
    }

    r->thread_bootstrap_order = 1u;
    r->cpu_bootstrap_order = 2u;
    r->rtclock_early_init_order = 3u;
    r->kernel_early_bootstrap_order = 4u;
    r->cpu_init_order = 5u;
    r->processor_bootstrap_order = 6u;
    r->bootstrap_order_valid = (r->thread_bootstrap_order == 1u &&
                                r->cpu_bootstrap_order == 2u &&
                                r->rtclock_early_init_order == 3u &&
                                r->kernel_early_bootstrap_order == 4u &&
                                r->cpu_init_order == 5u &&
                                r->processor_bootstrap_order == 6u) ? 1u : 0u;
    thread_ok = (r->thread_bootstrap_order == 1u);
    cpu_boot_ok = (r->cpu_bootstrap_order == 2u);
    kernel_cpu_processor_ok = (r->bootstrap_order_valid == 1u);
    if (thread_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_THREAD_BOOTSTRAP;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_THREAD_BOOTSTRAP;
    }
    if (cpu_boot_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CPU_BOOTSTRAP;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CPU_BOOTSTRAP;
    }
    if (kernel_cpu_processor_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_KERNEL_CPU_PROCESSOR;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_KERNEL_CPU_PROCESSOR;
    }

    g_stage82_timebase_callback_cookie = 0u;
    stage82_post_pe_timebase_callback_shape();
    now = timebase_ticks();
    r->timebase_callback_ptr = (uint32_t)(uintptr_t)&stage82_post_pe_timebase_callback_shape;
    r->timebase_callback_checksum = r->timebase_callback_ptr ^ 0x5442434bu ^ g_stage82_timebase_callback_cookie;
    r->timebase_callback_registered = (r->timebase_callback_ptr != 0u &&
                                       g_stage82_timebase_callback_cookie == 0x5442434bu) ? 1u : 0u;
    r->timebase_frequency = ml_get_timebase_frequency();
    if (r->timebase_frequency == 0u) {
        r->timebase_frequency = timebase_freq_hz();
    }
    r->timebase_initial_lo = (uint32_t)now;
    r->timebase_initial_hi = (uint32_t)(now >> 32);
    r->pe_register_timebase_callback_shaped = r->timebase_callback_registered;
    rtclock_ok = (r->timebase_callback_registered == 1u &&
                  r->timebase_frequency == 19200000u &&
                  r->timebase_callback_checksum != 0u) ? 1u : 0u;
    if (rtclock_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_RTCLOCK_CALLBACK;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_RTCLOCK_CALLBACK;
    }

    if (args) {
        r->boot_arg_diag_value = stage82_post_pe_parse_hex_after(args->CommandLine, "diag=",
                                                                 &r->boot_arg_diag_present, 0u);
        r->boot_arg_maxmem_value = stage82_post_pe_parse_hex_after(args->CommandLine, "maxmem=",
                                                                   &r->boot_arg_maxmem_present, 0u);
        r->boot_arg_debug_value = stage82_post_pe_parse_hex_after(args->CommandLine, "debug=",
                                                                  &r->boot_arg_debug_present, 0u);
        r->boot_arg_immediate_nmi_value = stage82_post_pe_parse_hex_after(args->CommandLine, "immediate_NMI=",
                                                                          &r->boot_arg_immediate_nmi_present, 0u);
    }
    r->boot_arg_parse_ok = (r->boot_arg_debug_present == 1u &&
                            r->boot_arg_debug_value == 0x144u &&
                            r->boot_arg_diag_present == 0u &&
                            r->boot_arg_maxmem_present == 0u &&
                            r->boot_arg_immediate_nmi_present == 0u) ? 1u : 0u;
    boot_arg_ok = r->boot_arg_parse_ok;
    if (boot_arg_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_BOOT_ARG_PARSE;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_BOOT_ARG_PARSE;
    }

    r->stop_before_arm_vm_init = 1u;
    r->arm_vm_init_not_called = 1u;
    stop_ok = (r->stop_before_arm_vm_init == 1u && r->arm_vm_init_not_called == 1u);
    if (stop_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_STOP_BEFORE_ARM_VM;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_STOP_BEFORE_ARM_VM;
    }

    public_bootstrap_ok = (r->public_thread_bootstrap_executed == 0u &&
                           r->public_cpu_bootstrap_executed == 0u &&
                           r->public_kernel_early_bootstrap_executed == 0u &&
                           r->public_cpu_init_executed == 0u &&
                           r->public_processor_bootstrap_executed == 0u) ? 1u : 0u;
    if (public_bootstrap_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_PUBLIC_BOOTSTRAP_BLOCKED;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PUBLIC_BOOTSTRAP;
    }

    public_rtclock_ok = (r->public_rtclock_early_init_executed == 0u &&
                         r->public_pe_register_timebase_callback_executed == 0u) ? 1u : 0u;
    if (public_rtclock_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_PUBLIC_RTCLOCK_BLOCKED;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PUBLIC_RTCLOCK;
    }

    public_xnu_ok = (r->public_xnu_start_executed == 0u && r->public_arm_init_executed == 0u) ? 1u : 0u;
    if (public_xnu_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_PUBLIC_XNU_ENTRY;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PUBLIC_XNU_ENTRY;
    }

    arm_vm_pmap_ok = (r->public_arm_vm_init_executed == 0u &&
                      r->public_pmap_runtime_executed == 0u) ? 1u : 0u;
    if (arm_vm_pmap_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_ARM_VM_PMAP_RUNTIME;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_ARM_VM_PMAP_RUNTIME;
    }

    pexpert_iokit_ok = (r->public_pexpert_runtime_executed == 0u &&
                        r->public_iokit_runtime_executed == 0u) ? 1u : 0u;
    if (pexpert_iokit_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_PEXPERT_IOKIT_RUNTIME;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_PEXPERT_IOKIT_RUNTIME;
    }

    macho_ok = (r->generated_macho_executed == 0u) ? 1u : 0u;
    if (macho_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_MACHO_EXEC;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_MACHO_EXEC;
    }

    r->live_pmap_installed = entry_result ? entry_result->live_pmap_installed : 0u;
    r->tlb_invalidated = entry_result ? entry_result->tlb_invalidated : 0u;
    r->cache_policy_changed = entry_result ? entry_result->cache_policy_changed : 0u;
    r->persistent_write_attempted = entry_result ? entry_result->persistent_write_attempted : 0u;
    mutation_ok = (r->live_pmap_installed == 0u && r->ttbr_written == 0u &&
                   r->ttbcr_written == 0u && r->dacr_written == 0u && r->sctlr_written == 0u &&
                   r->tlb_invalidated == 0u && r->cache_policy_changed == 0u &&
                   r->persistent_write_attempted == 0u) ? 1u : 0u;
    if (mutation_ok == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_NO_MUTATION;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_MUTATION;
    }

    r->ttbr0_after = stage82_post_pe_read_ttbr0();
    r->ttbcr_after = stage82_post_pe_read_ttbcr();
    r->dacr_after = stage82_post_pe_read_dacr();
    r->sctlr_after = stage82_post_pe_read_sctlr();
    if (r->control_registers_sampled == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CONTROL_REGS_SAMPLED;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CONTROL_REG_SAMPLE;
    }
    r->control_registers_unchanged = (r->ttbr0_before == r->ttbr0_after &&
                                      r->ttbcr_before == r->ttbcr_after &&
                                      r->dacr_before == r->dacr_after &&
                                      r->sctlr_before == r->sctlr_after) ? 1u : 0u;
    if (r->control_registers_unchanged == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_CONTROL_REGS_UNCHANGED;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_CONTROL_REG_CHANGED;
    }

    r->stage_owned_local_only = 1u;
    if (r->stage_owned_local_only == 1u) {
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_LOCAL_ONLY;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_LOCAL_ONLY;
    }

    if (source_ok == 1u && boot_args_ok == 1u && pe_state_ok == 1u && cpu_topology_ok == 1u &&
        master_cpu_ok == 1u && boot_cpu_data_ok == 1u && cpu_entries_ok == 1u &&
        stack_ok == 1u && thread_ok == 1u && cpu_boot_ok == 1u && rtclock_ok == 1u &&
        kernel_cpu_processor_ok == 1u && boot_arg_ok == 1u && stop_ok == 1u &&
        public_bootstrap_ok == 1u && public_rtclock_ok == 1u && public_xnu_ok == 1u &&
        arm_vm_pmap_ok == 1u && pexpert_iokit_ok == 1u && macho_ok == 1u &&
        mutation_ok == 1u && r->control_registers_unchanged == 1u &&
        r->stage_owned_local_only == 1u) {
        r->safety_boundary_preserved = 1u;
        r->satisfied_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_SAT_SAFETY_BOUNDARY;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_SAFETY_BOUNDARY;
    }

    if (r->satisfied_mask != r->required_mask && r->failure_mask == 0u) {
        r->failure_mask |= STAGE82_XNU_ARM_INIT_POST_PE_BOOTSTRAP_FAIL_SAFETY_BOUNDARY;
    }

    r->status = (r->satisfied_mask == r->required_mask && r->failure_mask == 0u) ?
        STAGE82_STATUS_OK : STAGE82_STATUS_FAIL(r->failure_mask);
    r->checksum = stage82_post_pe_checksum(r);

    stage82_post_pe_log(r);
    if (r->status == STAGE82_STATUS_OK) {
        xnu_log_puts("stage82_xnu_arm_init_post_pe_bootstrap: returning before arm_vm_init\n");
    } else {
        xnu_log_puts("stage82_xnu_arm_init_post_pe_bootstrap: failed before arm_vm_init boundary\n");
    }

    return r->status == STAGE82_STATUS_OK;
}

const struct stage82_xnu_arm_init_post_pe_bootstrap_result *
stage82_xnu_arm_init_post_pe_bootstrap_result(void)
{
    return &g_stage82_xnu_arm_init_post_pe_bootstrap_result;
}
