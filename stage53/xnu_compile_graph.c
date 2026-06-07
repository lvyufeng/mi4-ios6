#include "stage53.h"
#include "xnu_compile_graph_generated.h"

static struct stage53_xnu_compile_graph g_stage53_xnu_compile_graph;

static uint32_t stage53_xnu_compile_graph_checksum(volatile const struct stage53_xnu_compile_graph *graph)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)graph;
    uint32_t count = (uint32_t)(offsetof(struct stage53_xnu_compile_graph, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static void stage53_xnu_compile_graph_log(const struct stage53_xnu_compile_graph *graph)
{
    xnu_log_kv32("stage53_xnu_compile_graph_status", graph->status);
    xnu_log_kv32("stage53_xnu_compile_graph_required_mask", graph->required_mask);
    xnu_log_kv32("stage53_xnu_compile_graph_satisfied_mask", graph->satisfied_mask);
    xnu_log_kv32("stage53_xnu_compile_graph_failure_mask", graph->failure_mask);
    xnu_log_kv32("stage53_xnu_compile_graph_checksum", graph->checksum);
    xnu_log_kv32("stage53_xnu_compile_graph_candidate_count", graph->candidate_count);
    xnu_log_kv32("stage53_xnu_compile_graph_allowed_compile_count", graph->allowed_compile_count);
    xnu_log_kv32("stage53_xnu_compile_graph_allowed_link_count", graph->allowed_link_count);
    xnu_log_kv32("stage53_xnu_compile_graph_forbidden_count", graph->forbidden_count);
    xnu_log_kv32("stage53_xnu_compile_graph_shim_required_count", graph->shim_required_count);
    xnu_log_kv32("stage53_xnu_compile_graph_max_risk_class", graph->max_risk_class);
    xnu_log_kv32("stage53_xnu_compile_graph_device_tree_classified", graph->device_tree_classified);
    xnu_log_kv32("stage53_xnu_compile_graph_bootargs_classified", graph->bootargs_classified);
    xnu_log_kv32("stage53_xnu_compile_graph_pe_gen_classified", graph->pe_gen_classified);
    xnu_log_kv32("stage53_xnu_compile_graph_pe_gen_allowed", graph->pe_gen_allowed);
    xnu_log_kv32("stage53_xnu_compile_graph_forbidden_excluded", graph->forbidden_excluded);
    xnu_log_kv32("stage53_xnu_compile_graph_include_deps_recorded", graph->include_deps_recorded);
    xnu_log_kv32("stage53_xnu_compile_graph_symbol_deps_recorded", graph->symbol_deps_recorded);
    xnu_log_kv32("stage53_xnu_compile_graph_shim_needs_recorded", graph->shim_needs_recorded);
    xnu_log_kv32("stage53_xnu_compile_graph_risk_classes_recorded", graph->risk_classes_recorded);
    xnu_log_kv32("stage53_xnu_compile_graph_baseline_commit32", graph->baseline_commit32);
    xnu_log_kv32("stage53_xnu_compile_graph_master_version", graph->baseline_master_version);
    xnu_log_kv32("stage53_xnu_compile_graph_public_2050_baseline", graph->public_2050_baseline);
    xnu_log_kv32("stage53_xnu_compile_graph_4570_reference_only", graph->xnu_4570_reference_only);
    xnu_log_kv32("stage53_xnu_compile_graph_no_full_xnu_build", graph->no_full_xnu_build);
    xnu_log_kv32("stage53_xnu_compile_graph_no_public_xnu_exec", graph->no_public_xnu_exec);
    xnu_log_kv32("stage53_xnu_compile_graph_no_macho_exec", graph->no_macho_exec);
    xnu_log_kv32("stage53_xnu_compile_graph_no_external_mutation", graph->no_external_mutation);
    xnu_log_kv32("stage53_xnu_compile_graph_outputs_ignored", graph->outputs_ignored);
    xnu_log_kv32("stage53_xnu_compile_graph_fail_closed", graph->fail_closed);
}

int stage53_xnu_compile_graph_selftest(void)
{
    struct stage53_xnu_compile_graph *graph = &g_stage53_xnu_compile_graph;

    memset(graph, 0, sizeof(*graph));
    graph->version = STAGE53_XNU_COMPILE_GRAPH_VERSION;
    graph->size = sizeof(*graph);
    graph->required_mask = STAGE53_XNU_GRAPH_REQUIRED_MASK;
    graph->satisfied_mask = STAGE53_XNU_GRAPH_HOST_SATISFIED_MASK;
    graph->failure_mask = STAGE53_XNU_GRAPH_HOST_FAILURE_MASK;
    graph->candidate_count = STAGE53_XNU_GRAPH_HOST_CANDIDATE_COUNT;
    graph->allowed_compile_count = STAGE53_XNU_GRAPH_HOST_ALLOWED_COMPILE_COUNT;
    graph->allowed_link_count = STAGE53_XNU_GRAPH_HOST_ALLOWED_LINK_COUNT;
    graph->forbidden_count = STAGE53_XNU_GRAPH_HOST_FORBIDDEN_COUNT;
    graph->shim_required_count = STAGE53_XNU_GRAPH_HOST_SHIM_REQUIRED_COUNT;
    graph->max_risk_class = STAGE53_XNU_GRAPH_HOST_MAX_RISK_CLASS;
    graph->device_tree_classified = STAGE53_XNU_GRAPH_HOST_DEVICE_TREE_CLASSIFIED;
    graph->bootargs_classified = STAGE53_XNU_GRAPH_HOST_BOOTARGS_CLASSIFIED;
    graph->pe_gen_classified = STAGE53_XNU_GRAPH_HOST_PE_GEN_CLASSIFIED;
    graph->pe_gen_allowed = STAGE53_XNU_GRAPH_HOST_PE_GEN_ALLOWED;
    graph->forbidden_excluded = STAGE53_XNU_GRAPH_HOST_FORBIDDEN_EXCLUDED;
    graph->include_deps_recorded = STAGE53_XNU_GRAPH_HOST_INCLUDE_DEPS_RECORDED;
    graph->symbol_deps_recorded = STAGE53_XNU_GRAPH_HOST_SYMBOL_DEPS_RECORDED;
    graph->shim_needs_recorded = STAGE53_XNU_GRAPH_HOST_SHIM_NEEDS_RECORDED;
    graph->risk_classes_recorded = STAGE53_XNU_GRAPH_HOST_RISK_CLASSES_RECORDED;
    graph->baseline_commit32 = STAGE53_XNU_GRAPH_HOST_BASELINE_COMMIT32;
    graph->baseline_master_version = STAGE53_XNU_GRAPH_HOST_MASTER_VERSION;
    graph->public_2050_baseline = STAGE53_XNU_GRAPH_HOST_PUBLIC_2050_BASELINE;
    graph->xnu_4570_reference_only = STAGE53_XNU_GRAPH_HOST_4570_REFERENCE_ONLY;
    graph->no_full_xnu_build = STAGE53_XNU_GRAPH_HOST_NO_FULL_XNU_BUILD;
    graph->no_public_xnu_exec = STAGE53_XNU_GRAPH_HOST_NO_PUBLIC_XNU_EXEC;
    graph->no_macho_exec = STAGE53_XNU_GRAPH_HOST_NO_MACHO_EXEC;
    graph->no_external_mutation = STAGE53_XNU_GRAPH_HOST_NO_EXTERNAL_MUTATION;
    graph->outputs_ignored = STAGE53_XNU_GRAPH_HOST_OUTPUTS_IGNORED;
    graph->fail_closed = STAGE53_XNU_GRAPH_HOST_FAIL_CLOSED;

    if (STAGE53_XNU_GRAPH_HOST_STATUS != STAGE53_STATUS_OK ||
        graph->required_mask != STAGE53_XNU_GRAPH_HOST_REQUIRED_MASK ||
        graph->satisfied_mask != graph->required_mask || graph->failure_mask != 0u) {
        graph->failure_mask |= STAGE53_XNU_GRAPH_FAIL_SAFETY_BOUNDARY;
    }
    if (graph->candidate_count < 10u || graph->allowed_compile_count != 3u || graph->allowed_link_count != 3u) {
        graph->failure_mask |= STAGE53_XNU_GRAPH_FAIL_CANDIDATES;
    }
    if (graph->device_tree_classified != 1u || graph->bootargs_classified != 1u ||
        graph->pe_gen_classified != 1u || graph->pe_gen_allowed != 1u) {
        graph->failure_mask |= STAGE53_XNU_GRAPH_FAIL_CANDIDATES | STAGE53_XNU_GRAPH_FAIL_SHIMS;
    }
    if (graph->forbidden_count < 7u || graph->forbidden_excluded != 1u ||
        graph->xnu_4570_reference_only != 1u) {
        graph->failure_mask |= STAGE53_XNU_GRAPH_FAIL_FORBIDDEN;
    }
    if (graph->include_deps_recorded != 1u || graph->symbol_deps_recorded != 1u ||
        graph->shim_needs_recorded != 1u || graph->risk_classes_recorded != 1u ||
        graph->shim_required_count == 0u || graph->max_risk_class < 4u) {
        graph->failure_mask |= STAGE53_XNU_GRAPH_FAIL_SHIMS;
    }
    if (graph->baseline_commit32 != STAGE53_XNU_BASELINE_COMMIT_CC8A9B0C ||
        graph->baseline_master_version != STAGE53_XNU_BASELINE_MASTER_12_3_0 ||
        graph->public_2050_baseline != 1u) {
        graph->failure_mask |= STAGE53_XNU_GRAPH_FAIL_BASELINE;
    }
    if (graph->no_full_xnu_build != 1u || graph->no_public_xnu_exec != 1u ||
        graph->no_macho_exec != 1u || graph->no_external_mutation != 1u ||
        graph->outputs_ignored != 1u || graph->fail_closed != 1u) {
        graph->failure_mask |= STAGE53_XNU_GRAPH_FAIL_SAFETY_BOUNDARY;
    }

    graph->status = (graph->satisfied_mask == graph->required_mask && graph->failure_mask == 0u) ?
        STAGE53_STATUS_OK : (STAGE53_STATUS_BASE | graph->failure_mask);
    graph->checksum = stage53_xnu_compile_graph_checksum(graph);

    stage53_xnu_compile_graph_log(graph);
    return graph->status == STAGE53_STATUS_OK;
}

const struct stage53_xnu_compile_graph *stage53_xnu_compile_graph_result(void)
{
    return &g_stage53_xnu_compile_graph;
}
