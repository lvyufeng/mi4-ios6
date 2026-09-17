#include "stage90.h"

static struct boot_args g_boot_args;
static uint8_t g_apple_dt[32768] __attribute__((aligned(4)));

static void build_stage90_apple_dt(struct apple_dt_builder *b)
{
    static const uint32_t memory_reg[] = {
        RAM_PHYS_BASE, RAM_CONSOLE_BASE - RAM_PHYS_BASE,
    };
    static const uint32_t gic_reg[] = {
        0xf9000000u, 0x00001000u,
        0xf9002000u, 0x00001000u,
    };
    static const uint32_t timer_reg[] = {
        0xf9020000u, 0x00001000u,
        0xf9021000u, 0x00001000u,
        0xf9022000u, 0x00001000u,
    };
    static const uint32_t ram_console_reg[] = {
        RAM_CONSOLE_BASE, RAM_CONSOLE_SIZE,
    };
    static const uint32_t io_ranges[] = {
        0x00000000u, 0xf9000000u, 0x07000000u,
    };
    static const uint32_t platform_driver_reg[] = {
        0xf9000000u, 0x00001000u,
        0xf9020000u, 0x00001000u,
    };
    static const uint32_t cpu_service_reg[] = {
        0u, 1u, 2u, 3u,
    };

    apple_dt_begin(b, g_apple_dt, sizeof(g_apple_dt));

    /* root: 4 properties, STAGE90_APPLE_DT_ROOT_CHILDREN children - the count mmu.c's
     * "high root dt summary" step validates, so both sides read one constant. */
    apple_dt_node_begin(b, 4, STAGE90_APPLE_DT_ROOT_CHILDREN);
    apple_dt_prop_str(b, "name", "/");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-xnu-stage90");
    apple_dt_prop_str(b, "model", "Xiaomi Mi 4 cancro Stage84");
    apple_dt_prop_str(b, "target-type", "cancro");


    /* /device-tree: public ARM XNU pexpert looks up name="device-tree" for model/target-type. */
    apple_dt_node_begin(b, 4, 0);
    apple_dt_prop_str(b, "name", "device-tree");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-xnu-stage90");
    apple_dt_prop_str(b, "model", "Xiaomi Mi 4 cancro Stage84");
    apple_dt_prop_str(b, "target-type", "cancro");

    /* /iokit-platform-scaffold: local-only registry-plane import plan, not IOKit runtime execution. */
    apple_dt_node_begin(b, 6, 0);
    apple_dt_prop_str(b, "name", "iokit-platform-scaffold");
    apple_dt_prop_str(b, "compatible", "apple,iokit-platform-scaffold");
    apple_dt_prop_str(b, "IOClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOProviderClass", "IODeviceTree:/");
    apple_dt_prop_str(b, "device_type", "platform");
    apple_dt_prop_str(b, "registry-plane", "IODeviceTree");

    /* /msm8974-platform-driver: Stage-owned platform-driver match facts only. */
    apple_dt_node_begin(b, 10, 0);
    apple_dt_prop_str(b, "name", "msm8974-platform-driver");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-cancro-stage90");
    apple_dt_prop_str(b, "IOClass", "MSM8974PlatformExpert");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84LocalPlatformScaffold");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE90_XNU_IOKIT_MATCH_EXPECTED_PROBE_SCORE);
    apple_dt_prop_u32_array(b, "reg", platform_driver_reg, ARRAY_SIZE(platform_driver_reg));
    apple_dt_prop_u32(b, "cpu-count", 4u);
    apple_dt_prop_u32(b, "timebase-frequency", 19200000u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /msm8974-interrupt-service: local-only interrupt service match facts. */
    apple_dt_node_begin(b, 10, 0);
    apple_dt_prop_str(b, "name", "msm8974-interrupt-service");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-gic-stage90");
    apple_dt_prop_str(b, "IOClass", "MSM8974InterruptController");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84LocalInterruptService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_PROBE_SCORE);
    apple_dt_prop_u32_array(b, "reg", gic_reg, ARRAY_SIZE(gic_reg));
    apple_dt_prop_u32(b, "irq-count", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_EXPECTED_IRQ_COUNT_MIN);
    apple_dt_prop_u32(b, "interrupt-controller", 1u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /msm8974-timer-service: local-only timer service match facts. */
    apple_dt_node_begin(b, 10, 0);
    apple_dt_prop_str(b, "name", "msm8974-timer-service");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-timer-stage90");
    apple_dt_prop_str(b, "IOClass", "MSM8974Timer");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84LocalTimerService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PROBE_SCORE);
    apple_dt_prop_u32_array(b, "reg", timer_reg, ARRAY_SIZE(timer_reg));
    apple_dt_prop_u32(b, "frequency", 19200000u);
    apple_dt_prop_u32(b, "timer-ppi-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PPI_MASK);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /msm8974-cpu-service: local-only CPU topology service match facts. */
    apple_dt_node_begin(b, 10, 0);
    apple_dt_prop_str(b, "name", "msm8974-cpu-service");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-cpu-stage90");
    apple_dt_prop_str(b, "IOClass", "IOCPU");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84LocalCPUService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_PROBE_SCORE);
    apple_dt_prop_u32_array(b, "reg", cpu_service_reg, ARRAY_SIZE(cpu_service_reg));
    apple_dt_prop_u32(b, "cpu-count", 4u);
    apple_dt_prop_u32(b, "timebase-frequency", 19200000u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /msm8974-rejected-driver: negative local candidate for dry-run rejection accounting. */
    apple_dt_node_begin(b, 8, 0);
    apple_dt_prop_str(b, "name", "msm8974-rejected-driver");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-rejected-stage90");
    apple_dt_prop_str(b, "IOClass", "Stage84RejectedDriver");
    apple_dt_prop_str(b, "IOProviderClass", "IOUnknownProvider");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84RejectedLocalService");
    apple_dt_prop_u32(b, "IOProbeScore", 0u);
    apple_dt_prop_u32(b, "rejected-candidate", 1u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /iokit-catalog-property-dryrun: local-only catalog/property/personality model. */
    apple_dt_node_begin(b, 6, 0);
    apple_dt_prop_str(b, "name", "iokit-catalog-property-dryrun");
    apple_dt_prop_str(b, "compatible", "apple,iokit-catalog-property-dryrun");
    apple_dt_prop_str(b, "IOClass", "OSDictionary");
    apple_dt_prop_u32(b, "catalog-version", STAGE90_XNU_IOKIT_CATALOG_PROPERTY_CATALOG_VERSION);
    apple_dt_prop_u32(b, "personality-count", STAGE90_XNU_IOKIT_CATALOG_PROPERTY_PERSONALITY_COUNT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /stage90-platform-personality: catalog personality projected from provider-plane dry-run. */
    apple_dt_node_begin(b, 100, 0);
    apple_dt_prop_str(b, "name", "stage90-platform-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-cancro-stage90");
    apple_dt_prop_str(b, "IOClass", "MSM8974PlatformExpert");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84LocalPlatformScaffold");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_PROBE_SCORE);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage90.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 1u);
    apple_dt_prop_u32(b, "source-service-bit", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 1u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE90_XNU_IOKIT_PROPERTY_INHERITANCE_PLATFORM_ORDINAL);
    apple_dt_prop_str(b, "registry-plane", "IODeviceTree");
    apple_dt_prop_u32(b, "attach-deferred", 1u);
    apple_dt_prop_u32(b, "start-deferred", 1u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 1u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage90-property-inheritance");
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 1u);
    apple_dt_prop_u32(b, "attach-readiness", 1u);
    apple_dt_prop_u32(b, "start-readiness", 1u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 0u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage90-registry-topology");
    apple_dt_prop_u32(b, "lifecycle-regsvc-dryrun", 1u);
    apple_dt_prop_u32(b, "lifecycle-state-matched", 1u);
    apple_dt_prop_u32(b, "lifecycle-provider-published", 1u);
    apple_dt_prop_u32(b, "lifecycle-registry-linked", 1u);
    apple_dt_prop_u32(b, "lifecycle-topology-linked", 1u);
    apple_dt_prop_u32(b, "register-service-readiness", 1u);
    apple_dt_prop_u32(b, "service-registered", 1u);
    apple_dt_prop_u32(b, "register-service-runtime-exec", 0u);
    apple_dt_prop_u32(b, "notification-ready", 1u);
    apple_dt_prop_u32(b, "notification-runtime-exec", 0u);
    apple_dt_prop_u32(b, "register-service-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_str(b, "register-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 0u);
    apple_dt_prop_u32(b, "register-dependency-ready", 1u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage90-attach-start-readiness");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 1u);
    apple_dt_prop_u32(b, "prov-notify-ready", 1u);
    apple_dt_prop_u32(b, "interest-ready", 1u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 1u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "expected-notify-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "delivered-notify-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "interest-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 1u);
    apple_dt_prop_str(b, "notify-provenance", "stage90-lifecycle-regsvc");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 1u);
    apple_dt_prop_u32(b, "callback-ready", 1u);
    apple_dt_prop_u32(b, "client-notify-ready", 1u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "expected-callback-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "delivered-callback-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "client-ack-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "callback-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 1u);
    apple_dt_prop_str(b, "callback-provenance", "stage90-provider-notify");
    apple_dt_prop_u32(b, "client-open-dryrun", 1u);
    apple_dt_prop_u32(b, "client-open-ready", 1u);
    apple_dt_prop_u32(b, "provider-claim-ready", 1u);
    apple_dt_prop_u32(b, "client-close-ready", 1u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "expected-open-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "claimed-open-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "client-close-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "open-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 1u);
    apple_dt_prop_str(b, "open-provenance", "stage90-provider-callback");

    /* /stage90-interrupt-personality: catalog personality for the local interrupt service. */
    apple_dt_node_begin(b, 100, 0);
    apple_dt_prop_str(b, "name", "stage90-interrupt-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-gic-stage90");
    apple_dt_prop_str(b, "IOClass", "MSM8974InterruptController");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84LocalInterruptService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_PROBE_SCORE);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage90.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 1u);
    apple_dt_prop_u32(b, "source-service-bit", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 1u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE90_XNU_IOKIT_PROPERTY_INHERITANCE_INTERRUPT_ORDINAL);
    apple_dt_prop_str(b, "registry-plane", "IODeviceTree");
    apple_dt_prop_u32(b, "attach-deferred", 1u);
    apple_dt_prop_u32(b, "start-deferred", 1u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 1u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage90-interrupt-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage90-property-inheritance");
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 1u);
    apple_dt_prop_u32(b, "attach-readiness", 1u);
    apple_dt_prop_u32(b, "start-readiness", 1u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 1u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage90-registry-topology");
    apple_dt_prop_u32(b, "lifecycle-regsvc-dryrun", 1u);
    apple_dt_prop_u32(b, "lifecycle-state-matched", 1u);
    apple_dt_prop_u32(b, "lifecycle-provider-published", 1u);
    apple_dt_prop_u32(b, "lifecycle-registry-linked", 1u);
    apple_dt_prop_u32(b, "lifecycle-topology-linked", 1u);
    apple_dt_prop_u32(b, "register-service-readiness", 1u);
    apple_dt_prop_u32(b, "service-registered", 1u);
    apple_dt_prop_u32(b, "register-service-runtime-exec", 0u);
    apple_dt_prop_u32(b, "notification-ready", 1u);
    apple_dt_prop_u32(b, "notification-runtime-exec", 0u);
    apple_dt_prop_u32(b, "register-service-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_str(b, "register-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 1u);
    apple_dt_prop_u32(b, "register-dependency-ready", 1u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage90-attach-start-readiness");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 1u);
    apple_dt_prop_u32(b, "prov-notify-ready", 1u);
    apple_dt_prop_u32(b, "interest-ready", 1u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 1u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "expected-notify-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "delivered-notify-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "interest-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 1u);
    apple_dt_prop_str(b, "notify-provenance", "stage90-lifecycle-regsvc");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 1u);
    apple_dt_prop_u32(b, "callback-ready", 1u);
    apple_dt_prop_u32(b, "client-notify-ready", 1u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "expected-callback-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "delivered-callback-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "client-ack-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "callback-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 1u);
    apple_dt_prop_str(b, "callback-provenance", "stage90-provider-notify");
    apple_dt_prop_u32(b, "client-open-dryrun", 1u);
    apple_dt_prop_u32(b, "client-open-ready", 1u);
    apple_dt_prop_u32(b, "provider-claim-ready", 1u);
    apple_dt_prop_u32(b, "client-close-ready", 1u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "expected-open-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "claimed-open-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "client-close-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "open-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 1u);
    apple_dt_prop_str(b, "open-provenance", "stage90-provider-callback");

    /* /stage90-timer-personality: catalog personality for the local timer service. */
    apple_dt_node_begin(b, 100, 0);
    apple_dt_prop_str(b, "name", "stage90-timer-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-timer-stage90");
    apple_dt_prop_str(b, "IOClass", "MSM8974Timer");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84LocalTimerService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PROBE_SCORE);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage90.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 1u);
    apple_dt_prop_u32(b, "source-service-bit", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 1u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE90_XNU_IOKIT_PROPERTY_INHERITANCE_TIMER_ORDINAL);
    apple_dt_prop_str(b, "registry-plane", "IODeviceTree");
    apple_dt_prop_u32(b, "attach-deferred", 1u);
    apple_dt_prop_u32(b, "start-deferred", 1u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 1u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage90-timer-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage90-property-inheritance");
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 1u);
    apple_dt_prop_u32(b, "attach-readiness", 1u);
    apple_dt_prop_u32(b, "start-readiness", 1u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 1u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage90-registry-topology");
    apple_dt_prop_u32(b, "lifecycle-regsvc-dryrun", 1u);
    apple_dt_prop_u32(b, "lifecycle-state-matched", 1u);
    apple_dt_prop_u32(b, "lifecycle-provider-published", 1u);
    apple_dt_prop_u32(b, "lifecycle-registry-linked", 1u);
    apple_dt_prop_u32(b, "lifecycle-topology-linked", 1u);
    apple_dt_prop_u32(b, "register-service-readiness", 1u);
    apple_dt_prop_u32(b, "service-registered", 1u);
    apple_dt_prop_u32(b, "register-service-runtime-exec", 0u);
    apple_dt_prop_u32(b, "notification-ready", 1u);
    apple_dt_prop_u32(b, "notification-runtime-exec", 0u);
    apple_dt_prop_u32(b, "register-service-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_str(b, "register-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 1u);
    apple_dt_prop_u32(b, "register-dependency-ready", 1u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage90-attach-start-readiness");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 1u);
    apple_dt_prop_u32(b, "prov-notify-ready", 1u);
    apple_dt_prop_u32(b, "interest-ready", 1u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 1u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "expected-notify-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "delivered-notify-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "interest-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 1u);
    apple_dt_prop_str(b, "notify-provenance", "stage90-lifecycle-regsvc");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 1u);
    apple_dt_prop_u32(b, "callback-ready", 1u);
    apple_dt_prop_u32(b, "client-notify-ready", 1u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "expected-callback-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "delivered-callback-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "client-ack-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "callback-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 1u);
    apple_dt_prop_str(b, "callback-provenance", "stage90-provider-notify");
    apple_dt_prop_u32(b, "client-open-dryrun", 1u);
    apple_dt_prop_u32(b, "client-open-ready", 1u);
    apple_dt_prop_u32(b, "provider-claim-ready", 1u);
    apple_dt_prop_u32(b, "client-close-ready", 1u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "expected-open-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "claimed-open-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "client-close-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "open-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 1u);
    apple_dt_prop_str(b, "open-provenance", "stage90-provider-callback");

    /* /stage90-cpu-personality: catalog personality for the local CPU service. */
    apple_dt_node_begin(b, 100, 0);
    apple_dt_prop_str(b, "name", "stage90-cpu-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-cpu-stage90");
    apple_dt_prop_str(b, "IOClass", "IOCPU");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84LocalCPUService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_PROBE_SCORE);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage90.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 1u);
    apple_dt_prop_u32(b, "source-service-bit", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 1u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE90_XNU_IOKIT_PROPERTY_INHERITANCE_CPU_ORDINAL);
    apple_dt_prop_str(b, "registry-plane", "IODeviceTree");
    apple_dt_prop_u32(b, "attach-deferred", 1u);
    apple_dt_prop_u32(b, "start-deferred", 1u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 1u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage90-cpu-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage90-property-inheritance");
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 1u);
    apple_dt_prop_u32(b, "attach-readiness", 1u);
    apple_dt_prop_u32(b, "start-readiness", 1u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 1u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage90-registry-topology");
    apple_dt_prop_u32(b, "lifecycle-regsvc-dryrun", 1u);
    apple_dt_prop_u32(b, "lifecycle-state-matched", 1u);
    apple_dt_prop_u32(b, "lifecycle-provider-published", 1u);
    apple_dt_prop_u32(b, "lifecycle-registry-linked", 1u);
    apple_dt_prop_u32(b, "lifecycle-topology-linked", 1u);
    apple_dt_prop_u32(b, "register-service-readiness", 1u);
    apple_dt_prop_u32(b, "service-registered", 1u);
    apple_dt_prop_u32(b, "register-service-runtime-exec", 0u);
    apple_dt_prop_u32(b, "notification-ready", 1u);
    apple_dt_prop_u32(b, "notification-runtime-exec", 0u);
    apple_dt_prop_u32(b, "register-service-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_str(b, "register-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 1u);
    apple_dt_prop_u32(b, "register-dependency-ready", 1u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage90-attach-start-readiness");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 1u);
    apple_dt_prop_u32(b, "prov-notify-ready", 1u);
    apple_dt_prop_u32(b, "interest-ready", 1u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 1u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "expected-notify-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "delivered-notify-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "interest-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 1u);
    apple_dt_prop_str(b, "notify-provenance", "stage90-lifecycle-regsvc");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 1u);
    apple_dt_prop_u32(b, "callback-ready", 1u);
    apple_dt_prop_u32(b, "client-notify-ready", 1u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "expected-callback-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "delivered-callback-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "client-ack-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "callback-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 1u);
    apple_dt_prop_str(b, "callback-provenance", "stage90-provider-notify");
    apple_dt_prop_u32(b, "client-open-dryrun", 1u);
    apple_dt_prop_u32(b, "client-open-ready", 1u);
    apple_dt_prop_u32(b, "provider-claim-ready", 1u);
    apple_dt_prop_u32(b, "client-close-ready", 1u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/stage90-platform-personality");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "expected-open-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "claimed-open-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "client-close-mask", STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "open-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 1u);
    apple_dt_prop_str(b, "open-provenance", "stage90-provider-callback");

    /* /stage90-rejected-personality: negative catalog personality remains unpublished. */
    apple_dt_node_begin(b, 99, 0);
    apple_dt_prop_str(b, "name", "stage90-rejected-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-rejected-stage90");
    apple_dt_prop_str(b, "IOClass", "Stage84RejectedDriver");
    apple_dt_prop_str(b, "IOProviderClass", "IOUnknownProvider");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage84RejectedLocalService");
    apple_dt_prop_u32(b, "IOProbeScore", 0u);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage90.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 0u);
    apple_dt_prop_u32(b, "rejected-candidate", 1u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 0u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE90_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "registry-entry-attached", 0u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 0u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/unlinked");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage90-rejected-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage90-rejected-unlinked");
    apple_dt_prop_u32(b, "registry-entry-linked", 0u);
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 0u);
    apple_dt_prop_u32(b, "attach-readiness", 0u);
    apple_dt_prop_u32(b, "start-readiness", 0u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 0u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage90-rejected-unattached");
    apple_dt_prop_u32(b, "lifecycle-regsvc-dryrun", 0u);
    apple_dt_prop_u32(b, "lifecycle-state-matched", 0u);
    apple_dt_prop_u32(b, "lifecycle-provider-published", 0u);
    apple_dt_prop_u32(b, "lifecycle-registry-linked", 0u);
    apple_dt_prop_u32(b, "lifecycle-topology-linked", 0u);
    apple_dt_prop_u32(b, "register-service-readiness", 0u);
    apple_dt_prop_u32(b, "service-registered", 0u);
    apple_dt_prop_u32(b, "register-service-runtime-exec", 0u);
    apple_dt_prop_u32(b, "notification-ready", 0u);
    apple_dt_prop_u32(b, "notification-runtime-exec", 0u);
    apple_dt_prop_str(b, "register-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 0u);
    apple_dt_prop_u32(b, "register-service-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "register-dependency-ready", 0u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage90-rejected-unregistered");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 0u);
    apple_dt_prop_u32(b, "prov-notify-ready", 0u);
    apple_dt_prop_u32(b, "interest-ready", 0u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 0u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", 0u);
    apple_dt_prop_u32(b, "expected-notify-mask", 0u);
    apple_dt_prop_u32(b, "delivered-notify-mask", 0u);
    apple_dt_prop_u32(b, "interest-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 0u);
    apple_dt_prop_str(b, "notify-provenance", "stage90-rejected-undelivered");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 0u);
    apple_dt_prop_u32(b, "callback-ready", 0u);
    apple_dt_prop_u32(b, "client-notify-ready", 0u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", 0u);
    apple_dt_prop_u32(b, "expected-callback-mask", 0u);
    apple_dt_prop_u32(b, "delivered-callback-mask", 0u);
    apple_dt_prop_u32(b, "client-ack-mask", 0u);
    apple_dt_prop_u32(b, "callback-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 0u);
    apple_dt_prop_str(b, "callback-provenance", "stage90-rejected-nocallback");
    apple_dt_prop_u32(b, "client-open-dryrun", 0u);
    apple_dt_prop_u32(b, "client-open-ready", 0u);
    apple_dt_prop_u32(b, "provider-claim-ready", 0u);
    apple_dt_prop_u32(b, "client-close-ready", 0u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", 0u);
    apple_dt_prop_u32(b, "expected-open-mask", 0u);
    apple_dt_prop_u32(b, "claimed-open-mask", 0u);
    apple_dt_prop_u32(b, "client-close-mask", 0u);
    apple_dt_prop_u32(b, "open-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 0u);
    apple_dt_prop_str(b, "open-provenance", "stage90-rejected-noopen");

    /* /chosen */
    apple_dt_node_begin(b, 4, 0);
    apple_dt_prop_str(b, "name", "chosen");
    apple_dt_prop_str(b, "boot-args", "debug=0x144 serial=0x1 mi4ios6.stage=83 xnu-early-init xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm live-pmap ttbr-live tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine pexpert-hook-ready pmap-ref st83dt=0x83");
    apple_dt_prop_str(b, "stdout-path", "ram-console");
    apple_dt_prop_u32_array(b, "ram-console-reg", ram_console_reg, ARRAY_SIZE(ram_console_reg));

    /* /memory */
    apple_dt_node_begin(b, 4, 0);
    apple_dt_prop_str(b, "name", "memory");
    apple_dt_prop_str(b, "device_type", "memory");
    apple_dt_prop_u32_array(b, "reg", memory_reg, ARRAY_SIZE(memory_reg));
    apple_dt_prop_u32(b, "reserved-top", RAM_CONSOLE_SIZE);

    /* /cpus with four Krait CPU children. */
    apple_dt_node_begin(b, 3, 4);
    apple_dt_prop_str(b, "name", "cpus");
    apple_dt_prop_u32(b, "#address-cells", 1);
    apple_dt_prop_u32(b, "#size-cells", 0);

    for (uint32_t cpu = 0; cpu < 4; cpu++) {
        char name[] = "cpu@0";
        name[4] = (char)('0' + cpu);
        apple_dt_node_begin(b, 7, 0);
        apple_dt_prop_str(b, "name", name);
        apple_dt_prop_str(b, "device_type", "cpu");
        apple_dt_prop_str(b, "compatible", "qcom,krait");
        apple_dt_prop_u32(b, "reg", cpu);
        apple_dt_prop_u32(b, "clock-frequency", 2265600000u);
        apple_dt_prop_u32(b, "timebase-frequency", 19200000u);
        /*
         * `state` is not decorative. XNU's pe_identify_machine reads it and skips any
         * cpu node whose state is not "running" (pe_identify_machine.c:117), so
         * without it every CPU's timebase-frequency is ignored; and ml_parse_cpu_topology
         * panics "unable to retrieve state for cpu 0" under MACH_ASSERT
         * (machine_routines.c:474). tools/xnu_dt_requirements.py checks for it.
         */
        apple_dt_prop_str(b, "state", "running");
    }

    /*
     * /arm-io: the node XNU's ARM platform code locates the SoC through.
     * pe_arm_get_soc_base_phys() finds it by name and takes gPESoCBasePhys from
     * ranges[1] (pe_identify_machine.c:232-237); it returns 0 if the node is absent,
     * and pe_arm_map_interrupt_controller then returns early (:541) so neither the
     * interrupt controller nor the timer is ever mapped. The node is required, not
     * cosmetic.
     *
     * Note the value model, which is Apple's and not Linux's: XNU computes
     * `gPicBase = soc_phys + reg[0]`, i.e. it expects a node's `reg` to be an offset
     * from the SoC base. The /interrupt-controller and /timer nodes below carry
     * absolute addresses, because that is what the project's own iokit contract
     * selftests read. Reconciling the two is a Phase 3 decision (it is also the point
     * at which Apple's pe_arm_map_interrupt_controller would be replaced by an MSM8974
     * shim), so it is recorded rather than guessed at here.
     */
    apple_dt_node_begin(b, 4, 0);
    apple_dt_prop_str(b, "name", "arm-io");
    apple_dt_prop_str(b, "device_type", "soc");
    apple_dt_prop_u32_array(b, "ranges", io_ranges, ARRAY_SIZE(io_ranges));
    apple_dt_prop_u32(b, "chip-revision", 0);

    /* /msm8974-io: Apple-XNU-like container for platform MMIO. */
    apple_dt_node_begin(b, 5, 0);
    apple_dt_prop_str(b, "name", "msm8974-io");
    apple_dt_prop_str(b, "device_type", "soc");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974");
    apple_dt_prop_u32_array(b, "ranges", io_ranges, ARRAY_SIZE(io_ranges));
    apple_dt_prop_u32(b, "chip-revision", 0);

    /* /interrupt-controller: MSM QGIC2 / GICv2. */
    apple_dt_node_begin(b, 5, 0);
    apple_dt_prop_str(b, "name", "interrupt-controller");
    apple_dt_prop_str(b, "compatible", "qcom,msm-qgic2");
    apple_dt_prop_u32_array(b, "reg", gic_reg, ARRAY_SIZE(gic_reg));
    apple_dt_prop_u32(b, "#interrupt-cells", 3);
    apple_dt_prop_u32(b, "interrupt-controller", 1);

    /* /timer: MSM/ARM 19.2 MHz timer facts from Linux DT. */
    apple_dt_node_begin(b, 6, 0);
    apple_dt_prop_str(b, "name", "timer");
    apple_dt_prop_str(b, "compatible", "qcom,msm-timer");
    apple_dt_prop_u32(b, "frequency", 19200000u);
    apple_dt_prop_u32_array(b, "reg", timer_reg, ARRAY_SIZE(timer_reg));
    apple_dt_prop_str(b, "use", "early-timebase");
    /*
     * pe_arm_map_interrupt_controller locates the timer with
     * DTFindEntry("device_type", "timer") - by property *value*, not by the node's name.
     * Having name="timer" is not enough, and a node without this property leaves
     * gTimerBase at 0. Found by tools/host_dt_check.sh, which runs XNU's own walker over
     * this tree; the source-level requirements scan could not see it, because "some node
     * has a device_type property" and "some node has device_type == timer" are different
     * questions and it only asked the first.
     *
     * Inert today, and deliberately so: pe_arm_map_interrupt_controller returns before
     * reaching the timer lookup because the interrupt controller is not found either (see
     * the reg-model note in docs/reference/xnu-handoff-contract.md). Adding this now makes
     * the tree correct for when Phase 3 resolves that, without changing behaviour yet.
     */
    apple_dt_prop_str(b, "device_type", "timer");
}

int test_kernel_entry(struct boot_args *args)
{
    log_puts("MI4IOS6_STAGE90 handoff -> test_kernel_entry(boot_args*)\n");

    if (!args) {
        log_puts("MI4IOS6_STAGE90 handoff bad: null args\n");
        return 0;
    }

    log_kv32("boot_args_ptr", (uint32_t)(uintptr_t)args);
    log_kv32("boot_args_rev_ver", ((uint32_t)args->Version << 16) | args->Revision);
    log_kv32("boot_args_physBase", args->physBase);
    log_kv32("boot_args_memSize", args->memSize);
    log_kv32("boot_args_topOfKernelData", args->topOfKernelData);
    log_kv32("boot_args_deviceTreeP", (uint32_t)(uintptr_t)args->deviceTreeP);
    log_kv32("boot_args_deviceTreeLength", args->deviceTreeLength);

    if (args->Revision != BOOT_ARGS_REVISION || args->Version != BOOT_ARGS_VERSION) {
        log_puts("MI4IOS6_STAGE90 handoff bad: rev/version\n");
        return 0;
    }
    if (!args->deviceTreeP || !args->deviceTreeLength) {
        log_puts("MI4IOS6_STAGE90 handoff bad: no dt\n");
        return 0;
    }

    if (!apple_dt_selftest_and_log(args->deviceTreeP, args->deviceTreeLength)) {
        log_puts("MI4IOS6_STAGE90 handoff bad: dt selftest\n");
        return 0;
    }

    log_puts("MI4IOS6_STAGE90 handoff ok: boot_args + fuller apple_dt validated\n");
    return 1;
}

void platform_reboot(void)
{
    volatile uint32_t *restart_reason = (volatile uint32_t *)RESTART_REASON;
    volatile uint32_t *ps_hold = (volatile uint32_t *)MSM8974_PSHOLD;

    /*
     * Get everything out of the D-cache before the machine goes down, and do it first, so the
     * log lines written below are covered too. This is belt and braces: `ram_console` is mapped
     * non-cacheable under every cacheable attribute mode precisely so that the crash log does
     * not depend on maintenance happening, and `log_puts` ends in `dsb sy; isb`. But this is the
     * path every failure ends on, including the ones where something else is already wrong, so
     * it does not rely on that.
     */
#if STAGE90_CACHE_MODE == STAGE90_CACHE_MODE_ICACHE_DCACHE
    cache_clean_invalidate_dcache_all();
#endif

    log_puts("MI4IOS6_STAGE90 platform_reboot entered\n");
    log_kv32("platform_reboot_restart_reason_addr", RESTART_REASON);
    log_kv32("platform_reboot_ps_hold_addr", MSM8974_PSHOLD);

    log_puts("MI4IOS6_STAGE90 platform_reboot writing restart reason\n");
    *restart_reason = RESTART_NORMAL;
    __asm__ volatile ("dsb sy" ::: "memory");
    log_puts("MI4IOS6_STAGE90 platform_reboot restart reason write complete\n");

    log_puts("MI4IOS6_STAGE90 platform_reboot writing PS_HOLD=0\n");
    *ps_hold = 0;
    __asm__ volatile ("dsb sy" ::: "memory");
    log_puts("MI4IOS6_STAGE90 platform_reboot PS_HOLD write returned\n");

#if STAGE90_HW_WATCHDOG == STAGE90_HW_WATCHDOG_ARMED
    /*
     * Second, independent reset path. PS_HOLD is the PMIC; the watchdog bite is the
     * SoC's own counter. If the PS_HOLD write did not take effect - one reading of
     * the 2026-09-16 hang - this still resets the phone within a tick. If PS_HOLD did
     * work, the machine is already powering down and the bite is harmless.
     */
    stage90_hw_watchdog_bite_now();
#endif

    log_puts("MI4IOS6_STAGE90 platform_reboot entering WFE loop\n");
    for (;;) {
        __asm__ volatile ("wfe");
    }
}

/*
 * Dead-man reset. See STAGE90_DEADMAN_* in stage90.h for why this exists: the
 * PS_HOLD reset path is proven (dozens of earlier stages returned to Android
 * ~25-30s after `fastboot boot`), but until now it was only ever armed inside
 * the handoff, leaving the whole arm_init ladder without recovery.
 */
static uint32_t g_stage90_deadman_armed;

int stage90_arm_deadman_reset(void)
{
#if STAGE90_DEADMAN_ENABLE
    volatile uint32_t *gicd_ctlr;
    volatile uint32_t *gicc_ctlr;
    uint32_t dist_ctlr;
    uint32_t cpu_ctlr;

    if (GIC_state_stage90.distBase == 0u || GIC_state_stage90.cpuBase == 0u) {
        gic_readonly_snapshot(STAGE90_GIC_DIST_BASE, STAGE90_GIC_CPU_BASE);
    }

    /*
     * The timer interrupt needs the distributor and CPU interface live. aboot
     * normally leaves them enabled (the SGI/timer selftests require it and pass
     * on hardware), so this only repairs the case where they are not - it does
     * not touch the priority mask, which would widen the set of delivered IRQs.
     */
    gicd_ctlr = (volatile uint32_t *)(uintptr_t)(GIC_state_stage90.distBase + 0x000u);
    gicc_ctlr = (volatile uint32_t *)(uintptr_t)(GIC_state_stage90.cpuBase + 0x000u);
    dist_ctlr = *gicd_ctlr;
    cpu_ctlr = *gicc_ctlr;

    if ((dist_ctlr & 1u) == 0u) {
        *gicd_ctlr = dist_ctlr | 1u;
    }
    if ((cpu_ctlr & 1u) == 0u) {
        *gicc_ctlr = cpu_ctlr | 1u;
    }
    __asm__ volatile ("dsb sy" ::: "memory");

    log_kv32("deadman_gicd_ctlr_before", dist_ctlr);
    log_kv32("deadman_gicc_ctlr_before", cpu_ctlr);
    log_kv32("deadman_gicd_ctlr_after", *gicd_ctlr);
    log_kv32("deadman_gicc_ctlr_after", *gicc_ctlr);

    if (!stage90_arm_pc_sampling_watchdog(STAGE90_DEADMAN_INTERVAL_US,
                                          STAGE90_DEADMAN_SAMPLES)) {
        log_puts("MI4IOS6_STAGE90 deadman: arm failed; payload runs without a recovery net\n");
        return 0;
    }

    g_stage90_deadman_armed = 1u;
    log_puts("MI4IOS6_STAGE90 deadman: armed (dump + PS_HOLD reboot if the payload stops making progress)\n");
    log_kv32("deadman_interval_us", STAGE90_DEADMAN_INTERVAL_US);
    log_kv32("deadman_samples", STAGE90_DEADMAN_SAMPLES);
    return 1;
#else
    log_puts("MI4IOS6_STAGE90 deadman: disabled by STAGE90_DEADMAN_ENABLE=0\n");
    return 0;
#endif
}

uint32_t stage90_deadman_armed(void)
{
    return g_stage90_deadman_armed;
}

#if STAGE90_HW_WATCHDOG_SELFTEST || STAGE90_DEADMAN_SELFTEST
/*
 * Spin until `deadline_us` has elapsed, then reboot through PS_HOLD.
 *
 * Used by the self-tests. Returns only by rebooting, so the caller need not handle a
 * return - the point is that the device always comes back, whether or not the net under
 * test fired. The elapsed time is the result: a device back at the net's own timeout means
 * the net fired, and one back at the deadline means it did not.
 */
static void stage90_selftest_bounded_spin(uint32_t deadline_us, const char *deadline_msg)
{
    uint64_t start = timebase_ticks();

    for (;;) {
        if (timebase_elapsed_us(start, timebase_ticks()) >= deadline_us) {
            break;
        }
        __asm__ volatile ("nop" ::: "memory");
    }

    /* The net under test did not fire. Report that, then use the proven reset path. */
    log_puts("MI4IOS6_STAGE90 ");
    log_puts(deadline_msg);
    log_puts("\n");
    log_kv32("selftest_deadline_us", deadline_us);
    log_kv32("selftest_elapsed_us", timebase_elapsed_us(start, timebase_ticks()));
    platform_reboot();
}
#endif

void stage90_main(void)
{
    struct apple_dt_builder b;
    uint32_t dt_len;

    log_init();
    log_puts("MI4IOS6_STAGE90 v1 entered; Stage-owned arm_vm_init live-pmap install/verify/restore window; ram_console live\n");
    log_kv32("stage90_image_end", (uint32_t)(uintptr_t)__stage90_image_end);
    log_kv32("stage90_build_handoff_mode", STAGE90_HANDOFF_MODE);
    log_kv32("stage90_build_entry_ladder_level", STAGE90_ENTRY_LADDER_LEVEL);
    log_kv32("stage90_build_hw_watchdog", STAGE90_HW_WATCHDOG);

    /*
     * Arm the hardware watchdog first, before anything that can hang. It needs only
     * the already-mapped MMIO window, not the GIC or the timer, so this is the
     * earliest point at which it can be done and the earliest point at which the
     * payload has a guaranteed way out. Everything after this line - the DT build, the
     * whole arm_init ladder, the handoff - is covered by hardware rather than by
     * software that might itself be what broke.
     */
#if STAGE90_HW_WATCHDOG == STAGE90_HW_WATCHDOG_ARMED
    (void)stage90_hw_watchdog_arm(STAGE90_HW_WATCHDOG_TIMEOUT_S);
#endif

#if STAGE90_HW_WATCHDOG_SELFTEST
    /*
     * Hardware-watchdog self-test: the software dead-man is deliberately NOT armed, so
     * the SoC's own countdown is the only net. If the device returns after
     * STAGE90_HW_WATCHDOG_TIMEOUT_S (plus the bite gap) the last-resort reset is proven.
     *
     * The spin is bounded, so a watchdog that does NOT fire still returns the device - via
     * PS_HOLD, at STAGE90_SELFTEST_DEADLINE_US - and says so. See that macro for why: an
     * unbounded spin here would make the run whose purpose is to prove the recovery net the
     * one run that could hang worst.
     */
    /* The bark/bite gap is 3s, so the reset lands at TIMEOUT_S + 3. Say the number rather
     * than a literal, since the timeout has already changed once (30 -> 25, see stage90.h). */
    log_puts("MI4IOS6_STAGE90 hw_watchdog SELFTEST: spinning; the hardware countdown should reboot us at ~");
    log_hex32(STAGE90_HW_WATCHDOG_TIMEOUT_S + STAGE90_HW_WATCHDOG_BITE_GAP_S);
    log_puts("s; if it does not, the bounded spin reboots us at ~");
    log_hex32(STAGE90_SELFTEST_DEADLINE_US / 1000000u);
    log_puts("s\n");
    stage90_selftest_bounded_spin(STAGE90_SELFTEST_DEADLINE_US,
                                  "hw_watchdog SELFTEST: deadline reached - the hardware watchdog did NOT fire");
#endif

#if STAGE90_DEADMAN_SELFTEST
    /*
     * Dead-man self-test: arm, then prove the software recovery path end to end. The
     * hardware watchdog is already armed above and is the second net; to attribute a
     * success to the software dead-man alone, build with STAGE90_HW_WATCHDOG=0.
     *
     * Bounded for the same reason as the other self-test, with a longer deadline since the
     * dead-man's own budget is 60s.
     */
    (void)stage90_arm_deadman_reset();
    log_puts("MI4IOS6_STAGE90 deadman SELFTEST: spinning; the dead-man should dump and reboot us at ~60s\n");
    stage90_selftest_bounded_spin(STAGE90_SELFTEST_DEADLINE_US,
                                  "deadman SELFTEST: deadline reached - the dead-man did NOT fire");
#endif

    build_stage90_apple_dt(&b);
    dt_len = apple_dt_finish(&b);
    log_kv32("built_apple_dt_len", dt_len);

    if (!dt_len) {
        log_puts("MI4IOS6_STAGE90 apple_dt build failed\n");
        platform_reboot();
    }

    build_boot_args(&g_boot_args, g_apple_dt, dt_len);
    log_puts("MI4IOS6_STAGE90 boot_args built (rev2); fuller apple_dt attached\n");

    log_puts("MI4IOS6_STAGE90 vector base installed at ");
    log_hex32((uint32_t)(uintptr_t)stage90_vectors);
    log_puts("\n");

    log_puts("MI4IOS6_STAGE90 boot-wrapper handoff -> kernel_entry(boot_args*)\n");
    if (kernel_entry(&g_boot_args)) {
        log_puts("MI4IOS6_STAGE90 kernel_entry returned success\n");
    } else {
        log_puts("MI4IOS6_STAGE90 kernel_entry returned failure\n");
        platform_reboot();
    }

    log_puts("MI4IOS6_STAGE90 stage90_main final platform_reboot call\n");
    platform_reboot();
}
