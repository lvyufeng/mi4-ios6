#include "stage80.h"

/*
 * Stage80 Stage-owned PE_init_platform(FALSE, args)-shaped pre-VM
 * pexpert/platform micro-sequence.
 *
 * This remains a local model of the public XNU pexpert handoff shape.  It does
 * not call or execute public PE_init_platform, DTInit, pe_identify_machine,
 * pmap, IOKit, _start, or arm_init runtime code.
 */

static struct stage80_xnu_pe_init_platform_false_result g_stage80_xnu_pe_init_platform_false_result;

static inline uint32_t stage80_pe_init_read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t stage80_pe_init_read_ttbcr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(v));
    return v;
}

static inline uint32_t stage80_pe_init_read_dacr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t stage80_pe_init_read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static uint32_t stage80_pe_init_checksum(
    const struct stage80_xnu_pe_init_platform_false_result *r)
{
    const uint32_t *words = (const uint32_t *)r;
    uint32_t checksum = 0u;
    uint32_t count = (uint32_t)(offsetof(struct stage80_xnu_pe_init_platform_false_result, checksum) /
                                sizeof(uint32_t));
    uint32_t i;

    for (i = 0u; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage80_pe_init_string_checksum(const char *s)
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

static uint32_t stage80_pe_init_has_token(const char *s, const char *token)
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

static uint32_t stage80_pe_init_contains(const char *s, const char *needle)
{
    return stage80_pe_init_has_token(s, needle);
}

static uint32_t stage80_pe_init_reg_word(const void *dt, uint32_t dt_len,
                                         const void *node, const char *name,
                                         uint32_t word_index, uint32_t fallback)
{
    uint32_t len;
    const uint32_t *v = (const uint32_t *)apple_dt_get_prop(dt, dt_len, node, name, &len);

    if (!v || len < (word_index + 1u) * sizeof(uint32_t)) {
        return fallback;
    }

    return v[word_index];
}

static void stage80_pe_init_log(const struct stage80_xnu_pe_init_platform_false_result *r)
{
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_status", r->status);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_required_mask", r->required_mask);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_satisfied_mask", r->satisfied_mask);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_failure_mask", r->failure_mask);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_checksum", r->checksum);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_source_early_init_status", r->source_early_init_status);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_source_early_init_satisfied_mask", r->source_early_init_satisfied_mask);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_source_early_init_failure_mask", r->source_early_init_failure_mask);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_source_early_init_checksum", r->source_early_init_checksum);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_boot_args_ptr", r->boot_args_ptr);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_boot_args_rev_ver", r->boot_args_rev_ver);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_boot_args_valid", r->boot_args_valid);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_command_line_checksum", r->command_line_checksum);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_command_line_stage80_marker", r->command_line_stage80_marker);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_command_line_pe_init_false_marker", r->command_line_pe_init_false_marker);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_command_line_prevm_pexpert_marker", r->command_line_prevm_pexpert_marker);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_command_line_dtinit_marker", r->command_line_dtinit_marker);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_command_line_peid_marker", r->command_line_peid_marker);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_command_line_no_public_markers", r->command_line_no_public_markers);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_device_tree_valid", r->device_tree_valid);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_boot_args_ptr", r->pe_boot_args_ptr);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_device_tree_head", r->pe_device_tree_head);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_device_tree_length", r->pe_device_tree_length);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_memory_base", r->pe_memory_base);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_memory_size", r->pe_memory_size);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_cpu_count", r->pe_cpu_count);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_machine_type", r->pe_machine_type);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_vector_base", r->pe_vector_base);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_gic_dist_base", r->pe_gic_dist_base);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_gic_cpu_base", r->pe_gic_cpu_base);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_timer_base", r->pe_timer_base);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_timer_frequency", r->pe_timer_frequency);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_state_valid", r->pe_state_valid);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_pe_state_matched", r->pe_state_matched);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_dtinit_device_tree_head", r->dtinit_device_tree_head);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_dtinit_device_tree_length", r->dtinit_device_tree_length);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_dtinit_root_parseable", r->dtinit_root_parseable);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_dtinit_no_public_call", r->dtinit_no_public_call);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_root_prop_count", r->apple_dt_root_prop_count);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_root_child_count", r->apple_dt_root_child_count);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_device_tree_present", r->apple_dt_device_tree_present);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_chosen_present", r->apple_dt_chosen_present);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_boot_args_present", r->apple_dt_boot_args_present);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_boot_args_checksum", r->apple_dt_boot_args_checksum);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_memory_base", r->apple_dt_memory_base);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_memory_size", r->apple_dt_memory_size);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_cpu_count", r->apple_dt_cpu_count);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_gic_dist_base", r->apple_dt_gic_dist_base);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_gic_cpu_base", r->apple_dt_gic_cpu_base);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_timer_base", r->apple_dt_timer_base);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_timer_frequency", r->apple_dt_timer_frequency);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_apple_dt_semantic_mask", r->apple_dt_semantic_mask);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_identify_model_checksum", r->identify_model_checksum);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_identify_compatible_checksum", r->identify_compatible_checksum);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_identify_target_type_checksum", r->identify_target_type_checksum);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_identify_model_matched", r->identify_model_matched);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_identify_compatible_matched", r->identify_compatible_matched);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_identify_target_type_matched", r->identify_target_type_matched);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_identify_msm8974_matched", r->identify_msm8974_matched);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_identify_cancro_matched", r->identify_cancro_matched);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_identify_machine_no_public_call", r->identify_machine_no_public_call);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_platform_facts_matched", r->platform_facts_matched);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_public_pe_init_platform_executed", r->public_pe_init_platform_executed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_public_dtinit_executed", r->public_dtinit_executed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_public_pe_identify_machine_executed", r->public_pe_identify_machine_executed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_public_xnu_start_executed", r->public_xnu_start_executed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_public_arm_init_executed", r->public_arm_init_executed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_public_pmap_runtime_executed", r->public_pmap_runtime_executed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_public_pexpert_runtime_executed", r->public_pexpert_runtime_executed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_public_iokit_runtime_executed", r->public_iokit_runtime_executed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_generated_macho_executed", r->generated_macho_executed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_ttbr0_before", r->ttbr0_before);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_ttbr0_after", r->ttbr0_after);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_ttbcr_before", r->ttbcr_before);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_ttbcr_after", r->ttbcr_after);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_dacr_before", r->dacr_before);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_dacr_after", r->dacr_after);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_sctlr_before", r->sctlr_before);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_sctlr_after", r->sctlr_after);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_control_registers_sampled", r->control_registers_sampled);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_control_registers_unchanged", r->control_registers_unchanged);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_live_pmap_installed", r->live_pmap_installed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_ttbr_written", r->ttbr_written);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_ttbcr_written", r->ttbcr_written);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_dacr_written", r->dacr_written);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_sctlr_written", r->sctlr_written);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_tlb_invalidated", r->tlb_invalidated);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_cache_policy_changed", r->cache_policy_changed);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_persistent_write_attempted", r->persistent_write_attempted);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_stage_owned_local_only", r->stage_owned_local_only);
    xnu_log_kv32("stage80_xnu_pe_init_platform_false_safety_boundary_preserved", r->safety_boundary_preserved);
}

int stage80_xnu_pe_init_platform_false_run(
    struct boot_args *args,
    struct stage80_xnu_entry_stub_result *entry_result)
{
    struct stage80_xnu_pe_init_platform_false_result *r = &g_stage80_xnu_pe_init_platform_false_result;
    const struct stage80_xnu_early_pmap_platform_init_result *early;
    const void *dt = (const void *)0;
    uint32_t dt_len = 0u;
    const void *device_tree_node = (const void *)0;
    const void *chosen = (const void *)0;
    const void *memory = (const void *)0;
    const void *cpus = (const void *)0;
    const void *gic = (const void *)0;
    const void *timer = (const void *)0;
    const char *chosen_boot_args = (const char *)0;
    const char *model = (const char *)0;
    const char *compatible = (const char *)0;
    const char *target_type = (const char *)0;
    uint32_t prop_len = 0u;
    uint32_t early_ok;
    uint32_t boot_args_ok;
    uint32_t device_tree_ok;
    uint32_t dtinit_ok;
    uint32_t root_ok;
    uint32_t chosen_ok;
    uint32_t memory_ok;
    uint32_t cpus_ok;
    uint32_t gic_ok;
    uint32_t timer_ok;
    uint32_t identify_ok;
    uint32_t public_pexpert_blocked;
    uint32_t public_xnu_ok;
    uint32_t public_pmap_ok;
    uint32_t public_iokit_ok;
    uint32_t macho_ok;
    uint32_t mutation_ok;

    xnu_log_puts("stage80_xnu_pe_init_platform_false: entered Stage-owned PE_init_platform(FALSE,args)-shaped path\n");
    memset(r, 0, sizeof(*r));

    r->version = STAGE80_XNU_PE_INIT_PLATFORM_FALSE_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE80_STATUS_BASE;
    r->required_mask = STAGE80_XNU_PE_INIT_PLATFORM_FALSE_REQUIRED_MASK;
    r->boot_args_ptr = (uint32_t)(uintptr_t)args;

    r->ttbr0_before = stage80_pe_init_read_ttbr0();
    r->ttbcr_before = stage80_pe_init_read_ttbcr();
    r->dacr_before = stage80_pe_init_read_dacr();
    r->sctlr_before = stage80_pe_init_read_sctlr();
    r->control_registers_sampled = 1u;

    early = stage80_xnu_early_pmap_platform_init_result();
    r->source_early_init_status = early->status;
    r->source_early_init_satisfied_mask = early->satisfied_mask;
    r->source_early_init_failure_mask = early->failure_mask;
    r->source_early_init_checksum = early->checksum;
    early_ok = (early->status == STAGE80_STATUS_OK &&
                early->satisfied_mask == STAGE80_XNU_EARLY_INIT_REQUIRED_MASK &&
                early->failure_mask == 0u && early->checksum != 0u) ? 1u : 0u;
    if (early_ok == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_SOURCE_EARLY_INIT_OK;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_SOURCE_EARLY_INIT;
    }

    if (args) {
        r->boot_args_rev_ver = ((uint32_t)args->Version << 16) | args->Revision;
        r->boot_args_phys_base = args->physBase;
        r->boot_args_mem_size = args->memSize;
        r->boot_args_machine_type = args->machineType;
        r->boot_args_device_tree_ptr = (uint32_t)(uintptr_t)args->deviceTreeP;
        r->boot_args_device_tree_length = args->deviceTreeLength;
        r->command_line_checksum = stage80_pe_init_string_checksum(args->CommandLine);
        r->command_line_stage80_marker = stage80_pe_init_has_token(args->CommandLine, "mi4ios6.stage=80");
        r->command_line_pe_init_false_marker = stage80_pe_init_has_token(args->CommandLine, "xnu-pe-init-false");
        r->command_line_prevm_pexpert_marker = stage80_pe_init_has_token(args->CommandLine, "prevm-pexpert");
        r->command_line_dtinit_marker = stage80_pe_init_has_token(args->CommandLine, "dtinit-facts");
        r->command_line_peid_marker = stage80_pe_init_has_token(args->CommandLine, "peid-machine");
        r->command_line_no_public_markers =
            (stage80_pe_init_has_token(args->CommandLine, "no-pub-peinit") &&
             stage80_pe_init_has_token(args->CommandLine, "no-pub-dtinit") &&
             stage80_pe_init_has_token(args->CommandLine, "no-pub-peid")) ? 1u : 0u;
        dt = args->deviceTreeP;
        dt_len = args->deviceTreeLength;
    }

    boot_args_ok = (args && args->Revision == BOOT_ARGS_REVISION &&
                    args->Version == BOOT_ARGS_VERSION &&
                    args->physBase == STAGE80_BASE &&
                    args->memSize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE) &&
                    args->machineType == MACHINE_TYPE_MSM8974 &&
                    r->command_line_stage80_marker == 1u &&
                    r->command_line_pe_init_false_marker == 1u &&
                    r->command_line_prevm_pexpert_marker == 1u &&
                    r->command_line_dtinit_marker == 1u &&
                    r->command_line_peid_marker == 1u &&
                    r->command_line_no_public_markers == 1u) ? 1u : 0u;
    r->boot_args_valid = boot_args_ok;
    if (boot_args_ok == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_BOOT_ARGS_VALID;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_BOOT_ARGS;
    }

    device_tree_ok = (dt != (const void *)0 && dt_len != 0u) ? 1u : 0u;
    r->device_tree_valid = device_tree_ok;
    if (device_tree_ok == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_DEVICE_TREE_VALID;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_DEVICE_TREE;
    }

    pe_state_init_from_boot_args(args);
    r->pe_state_valid = (pe_state_validate() != 0) ? 1u : 0u;
    r->pe_boot_args_ptr = (uint32_t)(uintptr_t)PE_state_stage80.bootArgs;
    r->pe_device_tree_head = (uint32_t)(uintptr_t)PE_state_stage80.deviceTreeHead;
    r->pe_device_tree_length = PE_state_stage80.deviceTreeLength;
    r->pe_memory_base = PE_state_stage80.memoryBase;
    r->pe_memory_size = PE_state_stage80.memorySize;
    r->pe_cpu_count = PE_state_stage80.cpuCount;
    r->pe_machine_type = PE_state_stage80.machineType;
    r->pe_vector_base = PE_state_stage80.vectorBase;
    r->pe_gic_dist_base = PE_state_stage80.gicDistributorBase;
    r->pe_gic_cpu_base = PE_state_stage80.gicCpuBase;
    r->pe_timer_base = PE_state_stage80.timerBase;
    r->pe_timer_frequency = PE_state_stage80.timerFrequency;
    if (r->pe_state_valid == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_PE_STATE_CAPTURED;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PE_STATE_CAPTURE;
    }

    r->pe_state_matched = (r->pe_state_valid == 1u &&
                           PE_state_stage80.bootArgs == args &&
                           PE_state_stage80.deviceTreeHead == (args ? args->deviceTreeP : (void *)0) &&
                           PE_state_stage80.deviceTreeLength == (args ? args->deviceTreeLength : 0u) &&
                           PE_state_stage80.memoryBase == RAM_PHYS_BASE &&
                           PE_state_stage80.memorySize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE) &&
                           PE_state_stage80.cpuCount == 4u &&
                           PE_state_stage80.machineType == MACHINE_TYPE_MSM8974 &&
                           PE_state_stage80.vectorBase != 0u &&
                           PE_state_stage80.gicDistributorBase == 0xf9000000u &&
                           PE_state_stage80.gicCpuBase == 0xf9002000u &&
                           PE_state_stage80.timerBase == 0xf9020000u &&
                           PE_state_stage80.timerFrequency == 19200000u) ? 1u : 0u;
    if (r->pe_state_matched == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_PE_STATE_MATCHED;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PE_STATE_MATCH;
    }

    if (device_tree_ok == 1u) {
        r->apple_dt_root_prop_count = apple_dt_node_prop_count(dt, dt_len, dt);
        r->apple_dt_root_child_count = apple_dt_node_child_count(dt, dt_len, dt);
        device_tree_node = apple_dt_find_child(dt, dt_len, dt, "device-tree");
        chosen = apple_dt_find_child(dt, dt_len, dt, "chosen");
        memory = apple_dt_find_child(dt, dt_len, dt, "memory");
        cpus = apple_dt_find_child(dt, dt_len, dt, "cpus");
        gic = apple_dt_find_child(dt, dt_len, dt, "interrupt-controller");
        timer = apple_dt_find_child(dt, dt_len, dt, "timer");
    }

    r->dtinit_device_tree_head = (uint32_t)(uintptr_t)dt;
    r->dtinit_device_tree_length = dt_len;
    r->dtinit_root_parseable = (device_tree_ok == 1u &&
                                r->apple_dt_root_prop_count >= 3u &&
                                r->apple_dt_root_child_count >= 5u) ? 1u : 0u;
    r->dtinit_no_public_call = 1u;
    dtinit_ok = (r->dtinit_device_tree_head != 0u && r->dtinit_device_tree_length != 0u &&
                 r->dtinit_root_parseable == 1u && r->dtinit_no_public_call == 1u) ? 1u : 0u;
    if (dtinit_ok == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_DTINIT_FACTS;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_DTINIT_FACTS;
    }

    r->apple_dt_device_tree_present = (device_tree_node != (const void *)0) ? 1u : 0u;
    r->apple_dt_device_tree_prop_count = device_tree_node ? apple_dt_node_prop_count(dt, dt_len, device_tree_node) : 0u;
    root_ok = (r->dtinit_root_parseable == 1u && r->apple_dt_device_tree_present == 1u &&
               r->apple_dt_device_tree_prop_count >= 3u) ? 1u : 0u;
    if (root_ok == 1u) {
        r->apple_dt_semantic_mask |= STAGE80_DT_READY_BINARY_SELFTEST |
            STAGE80_DT_READY_DEVICE_TREE_NODE;
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_APPLE_DT_ROOT;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_APPLE_DT_ROOT;
    }

    if (chosen) {
        chosen_boot_args = (const char *)apple_dt_get_prop(dt, dt_len, chosen, "boot-args", &prop_len);
    }
    r->apple_dt_chosen_present = (chosen != (const void *)0) ? 1u : 0u;
    r->apple_dt_boot_args_present = (chosen_boot_args != (const char *)0 && prop_len != 0u) ? 1u : 0u;
    r->apple_dt_boot_args_checksum = stage80_pe_init_string_checksum(chosen_boot_args);
    chosen_ok = (r->apple_dt_chosen_present == 1u && r->apple_dt_boot_args_present == 1u &&
                 stage80_pe_init_has_token(chosen_boot_args, "mi4ios6.stage=80") == 1u &&
                 stage80_pe_init_has_token(chosen_boot_args, "xnu-pe-init-false") == 1u &&
                 stage80_pe_init_has_token(chosen_boot_args, "dtinit-facts") == 1u &&
                 stage80_pe_init_has_token(chosen_boot_args, "peid-machine") == 1u) ? 1u : 0u;
    if (chosen_ok == 1u) {
        r->apple_dt_semantic_mask |= STAGE80_DT_READY_CHOSEN | STAGE80_DT_READY_BOOT_ARGS;
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_CHOSEN_BOOT_ARGS;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_CHOSEN_BOOT_ARGS;
    }

    r->apple_dt_memory_present = (memory != (const void *)0) ? 1u : 0u;
    r->apple_dt_memory_base = memory ? stage80_pe_init_reg_word(dt, dt_len, memory, "reg", 0u, 0u) : 0u;
    r->apple_dt_memory_size = memory ? stage80_pe_init_reg_word(dt, dt_len, memory, "reg", 1u, 0u) : 0u;
    memory_ok = (r->apple_dt_memory_present == 1u && r->apple_dt_memory_base == RAM_PHYS_BASE &&
                 r->apple_dt_memory_size == (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) ? 1u : 0u;
    if (memory_ok == 1u) {
        r->apple_dt_semantic_mask |= STAGE80_DT_READY_MEMORY;
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_MEMORY_FACTS;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_MEMORY_FACTS;
    }

    r->apple_dt_cpus_present = (cpus != (const void *)0) ? 1u : 0u;
    r->apple_dt_cpu_count = cpus ? apple_dt_node_child_count(dt, dt_len, cpus) : 0u;
    cpus_ok = (r->apple_dt_cpus_present == 1u && r->apple_dt_cpu_count == 4u) ? 1u : 0u;
    if (cpus_ok == 1u) {
        r->apple_dt_semantic_mask |= STAGE80_DT_READY_CPUS | STAGE80_DT_READY_CPU_CLOCKS;
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_CPUS_FACTS;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_CPUS_FACTS;
    }

    r->apple_dt_gic_present = (gic != (const void *)0) ? 1u : 0u;
    r->apple_dt_gic_dist_base = gic ? stage80_pe_init_reg_word(dt, dt_len, gic, "reg", 0u, 0u) : 0u;
    r->apple_dt_gic_cpu_base = gic ? stage80_pe_init_reg_word(dt, dt_len, gic, "reg", 2u, 0u) : 0u;
    r->apple_dt_gic_interrupt_cells = gic ? apple_dt_get_u32_prop(dt, dt_len, gic, "#interrupt-cells", 0u) : 0u;
    gic_ok = (r->apple_dt_gic_present == 1u && r->apple_dt_gic_dist_base == 0xf9000000u &&
              r->apple_dt_gic_cpu_base == 0xf9002000u && r->apple_dt_gic_interrupt_cells == 3u) ? 1u : 0u;
    if (gic_ok == 1u) {
        r->apple_dt_semantic_mask |= STAGE80_DT_READY_INTERRUPT_CONTROLLER;
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_GIC_FACTS;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_GIC_FACTS;
    }

    r->apple_dt_timer_present = (timer != (const void *)0) ? 1u : 0u;
    r->apple_dt_timer_base = timer ? stage80_pe_init_reg_word(dt, dt_len, timer, "reg", 0u, 0u) : 0u;
    r->apple_dt_timer_frequency = timer ? apple_dt_get_u32_prop(dt, dt_len, timer, "frequency", 0u) : 0u;
    timer_ok = (r->apple_dt_timer_present == 1u && r->apple_dt_timer_base == 0xf9020000u &&
                r->apple_dt_timer_frequency == 19200000u) ? 1u : 0u;
    if (timer_ok == 1u) {
        r->apple_dt_semantic_mask |= STAGE80_DT_READY_TIMER;
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_TIMER_FACTS;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_TIMER_FACTS;
    }

    if (device_tree_node) {
        model = (const char *)apple_dt_get_prop(dt, dt_len, device_tree_node, "model", &prop_len);
        compatible = (const char *)apple_dt_get_prop(dt, dt_len, device_tree_node, "compatible", &prop_len);
        target_type = (const char *)apple_dt_get_prop(dt, dt_len, device_tree_node, "target-type", &prop_len);
    }
    r->identify_model_checksum = stage80_pe_init_string_checksum(model);
    r->identify_compatible_checksum = stage80_pe_init_string_checksum(compatible);
    r->identify_target_type_checksum = stage80_pe_init_string_checksum(target_type);
    r->identify_model_matched = (stage80_pe_init_contains(model, "Mi 4") == 1u &&
                                 stage80_pe_init_contains(model, "cancro") == 1u) ? 1u : 0u;
    r->identify_compatible_matched = (stage80_pe_init_contains(compatible, "msm8974") == 1u) ? 1u : 0u;
    r->identify_target_type_matched = (target_type && strcmp(target_type, "cancro") == 0) ? 1u : 0u;
    r->identify_msm8974_matched = (r->identify_compatible_matched == 1u &&
                                   r->boot_args_machine_type == MACHINE_TYPE_MSM8974) ? 1u : 0u;
    r->identify_cancro_matched = (r->identify_model_matched == 1u &&
                                  r->identify_target_type_matched == 1u) ? 1u : 0u;
    r->identify_machine_no_public_call = 1u;
    identify_ok = (r->identify_model_matched == 1u && r->identify_compatible_matched == 1u &&
                   r->identify_target_type_matched == 1u && r->identify_msm8974_matched == 1u &&
                   r->identify_cancro_matched == 1u &&
                   r->identify_machine_no_public_call == 1u) ? 1u : 0u;
    if (identify_ok == 1u) {
        r->apple_dt_semantic_mask |= STAGE80_DT_READY_TARGET_TYPE | STAGE80_DT_READY_MODEL;
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_IDENTIFY_MACHINE_FACTS;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_IDENTIFY_MACHINE;
    }

    r->platform_facts_matched = (memory_ok == 1u && cpus_ok == 1u && gic_ok == 1u && timer_ok == 1u &&
                                 identify_ok == 1u && r->pe_state_matched == 1u) ? 1u : 0u;
    if (r->platform_facts_matched == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_PLATFORM_FACTS_MATCHED;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PLATFORM_FACTS;
    }

    r->public_pe_init_platform_executed = 0u;
    r->public_dtinit_executed = 0u;
    r->public_pe_identify_machine_executed = 0u;
    r->public_xnu_start_executed = (entry_result ? entry_result->public_xnu_start_executed : 0u) |
        early->public_xnu_start_executed;
    r->public_arm_init_executed = (entry_result ? entry_result->public_arm_init_executed : 0u) |
        early->public_arm_init_executed;
    r->public_pmap_runtime_executed = early->public_pmap_runtime_executed;
    r->public_pexpert_runtime_executed = early->public_pexpert_runtime_executed;
    r->public_iokit_runtime_executed = (entry_result ? entry_result->public_iokit_runtime_executed : 0u) |
        early->public_iokit_runtime_executed;
    r->generated_macho_executed = (entry_result ? entry_result->generated_macho_executed : 0u) |
        early->generated_macho_executed;

    public_pexpert_blocked = (r->public_pe_init_platform_executed == 0u &&
                              r->public_dtinit_executed == 0u &&
                              r->public_pe_identify_machine_executed == 0u &&
                              r->public_pexpert_runtime_executed == 0u) ? 1u : 0u;
    if (public_pexpert_blocked == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_PUBLIC_PEXPERT_BLOCKED;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_PEXPERT_BOUNDARY;
    }

    public_xnu_ok = (r->public_xnu_start_executed == 0u && r->public_arm_init_executed == 0u) ? 1u : 0u;
    if (public_xnu_ok == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_PUBLIC_XNU_ENTRY;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_XNU_ENTRY;
    }

    public_pmap_ok = (r->public_pmap_runtime_executed == 0u) ? 1u : 0u;
    if (public_pmap_ok == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_PUBLIC_PMAP_RUNTIME;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_PMAP_RUNTIME;
    }

    if (r->public_pexpert_runtime_executed == 0u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_PUBLIC_PEXPERT_RUNTIME;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_PEXPERT_RUNTIME;
    }

    public_iokit_ok = (r->public_iokit_runtime_executed == 0u) ? 1u : 0u;
    if (public_iokit_ok == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_PUBLIC_IOKIT_RUNTIME;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_PUBLIC_IOKIT_RUNTIME;
    }

    macho_ok = (r->generated_macho_executed == 0u) ? 1u : 0u;
    if (macho_ok == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_MACHO_EXEC;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_MACHO_EXEC;
    }

    r->live_pmap_installed = (entry_result ? entry_result->live_pmap_installed : 0u) |
        early->live_pmap_installed;
    r->ttbr_written = early->ttbr_written;
    r->ttbcr_written = early->ttbcr_written;
    r->dacr_written = early->dacr_written;
    r->sctlr_written = early->sctlr_written;
    r->tlb_invalidated = (entry_result ? entry_result->tlb_invalidated : 0u) | early->tlb_invalidated;
    r->cache_policy_changed = (entry_result ? entry_result->cache_policy_changed : 0u) |
        early->cache_policy_changed;
    r->persistent_write_attempted = (entry_result ? entry_result->persistent_write_attempted : 0u) |
        early->persistent_write_attempted;
    mutation_ok = (r->live_pmap_installed == 0u &&
                   r->ttbr_written == 0u && r->ttbcr_written == 0u &&
                   r->dacr_written == 0u && r->sctlr_written == 0u &&
                   r->tlb_invalidated == 0u && r->cache_policy_changed == 0u &&
                   r->persistent_write_attempted == 0u) ? 1u : 0u;
    if (mutation_ok == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_NO_MUTATION;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_MUTATION;
    }

    r->ttbr0_after = stage80_pe_init_read_ttbr0();
    r->ttbcr_after = stage80_pe_init_read_ttbcr();
    r->dacr_after = stage80_pe_init_read_dacr();
    r->sctlr_after = stage80_pe_init_read_sctlr();

    if (r->control_registers_sampled == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_CONTROL_REGS_SAMPLED;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_CONTROL_REG_SAMPLE;
    }

    r->control_registers_unchanged = (r->ttbr0_before == r->ttbr0_after &&
                                      r->ttbcr_before == r->ttbcr_after &&
                                      r->dacr_before == r->dacr_after &&
                                      r->sctlr_before == r->sctlr_after) ? 1u : 0u;
    if (r->control_registers_unchanged == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_CONTROL_REGS_UNCHANGED;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_CONTROL_REG_CHANGED;
    }

    r->stage_owned_local_only = 1u;
    if (r->stage_owned_local_only == 1u) {
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_LOCAL_ONLY;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_LOCAL_ONLY;
    }

    if (early_ok == 1u && boot_args_ok == 1u && device_tree_ok == 1u && dtinit_ok == 1u &&
        root_ok == 1u && chosen_ok == 1u && r->platform_facts_matched == 1u &&
        public_pexpert_blocked == 1u && public_xnu_ok == 1u && public_pmap_ok == 1u &&
        public_iokit_ok == 1u && macho_ok == 1u && mutation_ok == 1u &&
        r->control_registers_unchanged == 1u && r->stage_owned_local_only == 1u) {
        r->safety_boundary_preserved = 1u;
        r->satisfied_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_SAT_SAFETY_BOUNDARY;
    } else {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_SAFETY_BOUNDARY;
    }

    if (r->satisfied_mask != r->required_mask && r->failure_mask == 0u) {
        r->failure_mask |= STAGE80_XNU_PE_INIT_PLATFORM_FALSE_FAIL_SAFETY_BOUNDARY;
    }
    r->status = (r->satisfied_mask == r->required_mask && r->failure_mask == 0u) ?
        STAGE80_STATUS_OK : STAGE80_STATUS_FAIL(r->failure_mask);
    r->checksum = stage80_pe_init_checksum(r);

    stage80_pe_init_log(r);
    if (r->status == STAGE80_STATUS_OK) {
        xnu_log_puts("stage80_xnu_pe_init_platform_false: returning to Stage-owned arm_init stub\n");
    } else {
        xnu_log_puts("stage80_xnu_pe_init_platform_false: failed, returning to Stage-owned arm_init stub\n");
    }

    return r->status == STAGE80_STATUS_OK;
}

const struct stage80_xnu_pe_init_platform_false_result *stage80_xnu_pe_init_platform_false_result(void)
{
    return &g_stage80_xnu_pe_init_platform_false_result;
}
