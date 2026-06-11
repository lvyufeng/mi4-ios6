#include "stage76.h"

static struct boot_args g_boot_args;
static uint8_t g_apple_dt[32768] __attribute__((aligned(4)));

static void build_stage76_apple_dt(struct apple_dt_builder *b)
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

    /* root: 4 properties, 19 children */
    apple_dt_node_begin(b, 4, 19);
    apple_dt_prop_str(b, "name", "/");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-xnu-stage76");
    apple_dt_prop_str(b, "model", "Xiaomi Mi 4 cancro Stage76");
    apple_dt_prop_str(b, "target-type", "cancro");


    /* /device-tree: public ARM XNU pexpert looks up name="device-tree" for model/target-type. */
    apple_dt_node_begin(b, 4, 0);
    apple_dt_prop_str(b, "name", "device-tree");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-xnu-stage76");
    apple_dt_prop_str(b, "model", "Xiaomi Mi 4 cancro Stage76");
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
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-cancro-stage76");
    apple_dt_prop_str(b, "IOClass", "MSM8974PlatformExpert");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76LocalPlatformScaffold");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE76_XNU_IOKIT_MATCH_EXPECTED_PROBE_SCORE);
    apple_dt_prop_u32_array(b, "reg", platform_driver_reg, ARRAY_SIZE(platform_driver_reg));
    apple_dt_prop_u32(b, "cpu-count", 4u);
    apple_dt_prop_u32(b, "timebase-frequency", 19200000u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /msm8974-interrupt-service: local-only interrupt service match facts. */
    apple_dt_node_begin(b, 10, 0);
    apple_dt_prop_str(b, "name", "msm8974-interrupt-service");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-gic-stage76");
    apple_dt_prop_str(b, "IOClass", "MSM8974InterruptController");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76LocalInterruptService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_PROBE_SCORE);
    apple_dt_prop_u32_array(b, "reg", gic_reg, ARRAY_SIZE(gic_reg));
    apple_dt_prop_u32(b, "irq-count", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_EXPECTED_IRQ_COUNT_MIN);
    apple_dt_prop_u32(b, "interrupt-controller", 1u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /msm8974-timer-service: local-only timer service match facts. */
    apple_dt_node_begin(b, 10, 0);
    apple_dt_prop_str(b, "name", "msm8974-timer-service");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-timer-stage76");
    apple_dt_prop_str(b, "IOClass", "MSM8974Timer");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76LocalTimerService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PROBE_SCORE);
    apple_dt_prop_u32_array(b, "reg", timer_reg, ARRAY_SIZE(timer_reg));
    apple_dt_prop_u32(b, "frequency", 19200000u);
    apple_dt_prop_u32(b, "timer-ppi-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PPI_MASK);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /msm8974-cpu-service: local-only CPU topology service match facts. */
    apple_dt_node_begin(b, 10, 0);
    apple_dt_prop_str(b, "name", "msm8974-cpu-service");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-cpu-stage76");
    apple_dt_prop_str(b, "IOClass", "IOCPU");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76LocalCPUService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_PROBE_SCORE);
    apple_dt_prop_u32_array(b, "reg", cpu_service_reg, ARRAY_SIZE(cpu_service_reg));
    apple_dt_prop_u32(b, "cpu-count", 4u);
    apple_dt_prop_u32(b, "timebase-frequency", 19200000u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /msm8974-rejected-driver: negative local candidate for dry-run rejection accounting. */
    apple_dt_node_begin(b, 8, 0);
    apple_dt_prop_str(b, "name", "msm8974-rejected-driver");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-rejected-stage76");
    apple_dt_prop_str(b, "IOClass", "Stage76RejectedDriver");
    apple_dt_prop_str(b, "IOProviderClass", "IOUnknownProvider");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76RejectedLocalService");
    apple_dt_prop_u32(b, "IOProbeScore", 0u);
    apple_dt_prop_u32(b, "rejected-candidate", 1u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /iokit-catalog-property-dryrun: local-only catalog/property/personality model. */
    apple_dt_node_begin(b, 6, 0);
    apple_dt_prop_str(b, "name", "iokit-catalog-property-dryrun");
    apple_dt_prop_str(b, "compatible", "apple,iokit-catalog-property-dryrun");
    apple_dt_prop_str(b, "IOClass", "OSDictionary");
    apple_dt_prop_u32(b, "catalog-version", STAGE76_XNU_IOKIT_CATALOG_PROPERTY_CATALOG_VERSION);
    apple_dt_prop_u32(b, "personality-count", STAGE76_XNU_IOKIT_CATALOG_PROPERTY_PERSONALITY_COUNT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);

    /* /stage76-platform-personality: catalog personality projected from provider-plane dry-run. */
    apple_dt_node_begin(b, 100, 0);
    apple_dt_prop_str(b, "name", "stage76-platform-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-cancro-stage76");
    apple_dt_prop_str(b, "IOClass", "MSM8974PlatformExpert");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76LocalPlatformScaffold");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_PROBE_SCORE);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage76.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 1u);
    apple_dt_prop_u32(b, "source-service-bit", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 1u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE76_XNU_IOKIT_PROPERTY_INHERITANCE_PLATFORM_ORDINAL);
    apple_dt_prop_str(b, "registry-plane", "IODeviceTree");
    apple_dt_prop_u32(b, "attach-deferred", 1u);
    apple_dt_prop_u32(b, "start-deferred", 1u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 1u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage76-property-inheritance");
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 1u);
    apple_dt_prop_u32(b, "attach-readiness", 1u);
    apple_dt_prop_u32(b, "start-readiness", 1u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 0u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage76-registry-topology");
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
    apple_dt_prop_u32(b, "register-service-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_str(b, "register-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 0u);
    apple_dt_prop_u32(b, "register-dependency-ready", 1u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage76-attach-start-readiness");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 1u);
    apple_dt_prop_u32(b, "prov-notify-ready", 1u);
    apple_dt_prop_u32(b, "interest-ready", 1u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 1u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "expected-notify-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "delivered-notify-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "interest-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 1u);
    apple_dt_prop_str(b, "notify-provenance", "stage76-lifecycle-regsvc");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 1u);
    apple_dt_prop_u32(b, "callback-ready", 1u);
    apple_dt_prop_u32(b, "client-notify-ready", 1u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "expected-callback-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "delivered-callback-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "client-ack-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "callback-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 1u);
    apple_dt_prop_str(b, "callback-provenance", "stage76-provider-notify");
    apple_dt_prop_u32(b, "client-open-dryrun", 1u);
    apple_dt_prop_u32(b, "client-open-ready", 1u);
    apple_dt_prop_u32(b, "provider-claim-ready", 1u);
    apple_dt_prop_u32(b, "client-close-ready", 1u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "expected-open-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "claimed-open-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "client-close-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT);
    apple_dt_prop_u32(b, "open-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 1u);
    apple_dt_prop_str(b, "open-provenance", "stage76-provider-callback");

    /* /stage76-interrupt-personality: catalog personality for the local interrupt service. */
    apple_dt_node_begin(b, 100, 0);
    apple_dt_prop_str(b, "name", "stage76-interrupt-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-gic-stage76");
    apple_dt_prop_str(b, "IOClass", "MSM8974InterruptController");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76LocalInterruptService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_PROBE_SCORE);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage76.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 1u);
    apple_dt_prop_u32(b, "source-service-bit", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 1u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE76_XNU_IOKIT_PROPERTY_INHERITANCE_INTERRUPT_ORDINAL);
    apple_dt_prop_str(b, "registry-plane", "IODeviceTree");
    apple_dt_prop_u32(b, "attach-deferred", 1u);
    apple_dt_prop_u32(b, "start-deferred", 1u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 1u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage76-interrupt-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage76-property-inheritance");
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 1u);
    apple_dt_prop_u32(b, "attach-readiness", 1u);
    apple_dt_prop_u32(b, "start-readiness", 1u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 1u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage76-registry-topology");
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
    apple_dt_prop_u32(b, "register-service-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_str(b, "register-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 1u);
    apple_dt_prop_u32(b, "register-dependency-ready", 1u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage76-attach-start-readiness");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 1u);
    apple_dt_prop_u32(b, "prov-notify-ready", 1u);
    apple_dt_prop_u32(b, "interest-ready", 1u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 1u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "expected-notify-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "delivered-notify-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "interest-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 1u);
    apple_dt_prop_str(b, "notify-provenance", "stage76-lifecycle-regsvc");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 1u);
    apple_dt_prop_u32(b, "callback-ready", 1u);
    apple_dt_prop_u32(b, "client-notify-ready", 1u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "expected-callback-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "delivered-callback-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "client-ack-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "callback-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 1u);
    apple_dt_prop_str(b, "callback-provenance", "stage76-provider-notify");
    apple_dt_prop_u32(b, "client-open-dryrun", 1u);
    apple_dt_prop_u32(b, "client-open-ready", 1u);
    apple_dt_prop_u32(b, "provider-claim-ready", 1u);
    apple_dt_prop_u32(b, "client-close-ready", 1u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "expected-open-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "claimed-open-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "client-close-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT);
    apple_dt_prop_u32(b, "open-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 1u);
    apple_dt_prop_str(b, "open-provenance", "stage76-provider-callback");

    /* /stage76-timer-personality: catalog personality for the local timer service. */
    apple_dt_node_begin(b, 100, 0);
    apple_dt_prop_str(b, "name", "stage76-timer-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-timer-stage76");
    apple_dt_prop_str(b, "IOClass", "MSM8974Timer");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76LocalTimerService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PROBE_SCORE);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage76.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 1u);
    apple_dt_prop_u32(b, "source-service-bit", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 1u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE76_XNU_IOKIT_PROPERTY_INHERITANCE_TIMER_ORDINAL);
    apple_dt_prop_str(b, "registry-plane", "IODeviceTree");
    apple_dt_prop_u32(b, "attach-deferred", 1u);
    apple_dt_prop_u32(b, "start-deferred", 1u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 1u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage76-timer-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage76-property-inheritance");
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 1u);
    apple_dt_prop_u32(b, "attach-readiness", 1u);
    apple_dt_prop_u32(b, "start-readiness", 1u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 1u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage76-registry-topology");
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
    apple_dt_prop_u32(b, "register-service-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_str(b, "register-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 1u);
    apple_dt_prop_u32(b, "register-dependency-ready", 1u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage76-attach-start-readiness");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 1u);
    apple_dt_prop_u32(b, "prov-notify-ready", 1u);
    apple_dt_prop_u32(b, "interest-ready", 1u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 1u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "expected-notify-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "delivered-notify-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "interest-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 1u);
    apple_dt_prop_str(b, "notify-provenance", "stage76-lifecycle-regsvc");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 1u);
    apple_dt_prop_u32(b, "callback-ready", 1u);
    apple_dt_prop_u32(b, "client-notify-ready", 1u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "expected-callback-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "delivered-callback-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "client-ack-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "callback-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 1u);
    apple_dt_prop_str(b, "callback-provenance", "stage76-provider-notify");
    apple_dt_prop_u32(b, "client-open-dryrun", 1u);
    apple_dt_prop_u32(b, "client-open-ready", 1u);
    apple_dt_prop_u32(b, "provider-claim-ready", 1u);
    apple_dt_prop_u32(b, "client-close-ready", 1u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "expected-open-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "claimed-open-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "client-close-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT);
    apple_dt_prop_u32(b, "open-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 1u);
    apple_dt_prop_str(b, "open-provenance", "stage76-provider-callback");

    /* /stage76-cpu-personality: catalog personality for the local CPU service. */
    apple_dt_node_begin(b, 100, 0);
    apple_dt_prop_str(b, "name", "stage76-cpu-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-cpu-stage76");
    apple_dt_prop_str(b, "IOClass", "IOCPU");
    apple_dt_prop_str(b, "IOProviderClass", "IOPlatformExpertDevice");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76LocalCPUService");
    apple_dt_prop_u32(b, "IOProbeScore", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_PROBE_SCORE);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage76.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 1u);
    apple_dt_prop_u32(b, "source-service-bit", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 1u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE76_XNU_IOKIT_PROPERTY_INHERITANCE_CPU_ORDINAL);
    apple_dt_prop_str(b, "registry-plane", "IODeviceTree");
    apple_dt_prop_u32(b, "attach-deferred", 1u);
    apple_dt_prop_u32(b, "start-deferred", 1u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 1u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage76-cpu-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage76-property-inheritance");
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 1u);
    apple_dt_prop_u32(b, "attach-readiness", 1u);
    apple_dt_prop_u32(b, "start-readiness", 1u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 1u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage76-registry-topology");
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
    apple_dt_prop_u32(b, "register-service-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_str(b, "register-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 1u);
    apple_dt_prop_u32(b, "register-dependency-ready", 1u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage76-attach-start-readiness");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 1u);
    apple_dt_prop_u32(b, "prov-notify-ready", 1u);
    apple_dt_prop_u32(b, "interest-ready", 1u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 1u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "expected-notify-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "delivered-notify-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "interest-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 1u);
    apple_dt_prop_str(b, "notify-provenance", "stage76-lifecycle-regsvc");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 1u);
    apple_dt_prop_u32(b, "callback-ready", 1u);
    apple_dt_prop_u32(b, "client-notify-ready", 1u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "expected-callback-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "delivered-callback-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "client-ack-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "callback-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 1u);
    apple_dt_prop_str(b, "callback-provenance", "stage76-provider-notify");
    apple_dt_prop_u32(b, "client-open-dryrun", 1u);
    apple_dt_prop_u32(b, "client-open-ready", 1u);
    apple_dt_prop_u32(b, "provider-claim-ready", 1u);
    apple_dt_prop_u32(b, "client-close-ready", 1u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/stage76-platform-personality");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "expected-open-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "claimed-open-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "client-close-mask", STAGE76_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT);
    apple_dt_prop_u32(b, "open-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 1u);
    apple_dt_prop_str(b, "open-provenance", "stage76-provider-callback");

    /* /stage76-rejected-personality: negative catalog personality remains unpublished. */
    apple_dt_node_begin(b, 99, 0);
    apple_dt_prop_str(b, "name", "stage76-rejected-personality");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-rejected-stage76");
    apple_dt_prop_str(b, "IOClass", "Stage76RejectedDriver");
    apple_dt_prop_str(b, "IOProviderClass", "IOUnknownProvider");
    apple_dt_prop_str(b, "IOMatchCategory", "Stage76RejectedLocalService");
    apple_dt_prop_u32(b, "IOProbeScore", 0u);
    apple_dt_prop_str(b, "CFBundleIdentifier", "com.mi4ios6.stage76.localcatalog");
    apple_dt_prop_u32(b, "catalog-property-dryrun", 1u);
    apple_dt_prop_u32(b, "provider-plane-published", 0u);
    apple_dt_prop_u32(b, "rejected-candidate", 1u);
    apple_dt_prop_u32(b, "stage-owned-local-only", 1u);
    apple_dt_prop_u32(b, "registry-entry-dryrun", 0u);
    apple_dt_prop_u32(b, "property-inheritance-dryrun", 1u);
    apple_dt_prop_u32(b, "registry-entry-ordinal", STAGE76_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "registry-entry-attached", 0u);
    apple_dt_prop_u32(b, "registry-topology-dryrun", 0u);
    apple_dt_prop_str(b, "registry-parent-path", "IODeviceTree:/unlinked");
    apple_dt_prop_str(b, "registry-entry-path", "IODeviceTree:/stage76-rejected-personality");
    apple_dt_prop_u32(b, "registry-parent-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "registry-sibling-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_str(b, "registry-topology-provenance", "stage76-rejected-unlinked");
    apple_dt_prop_u32(b, "registry-entry-linked", 0u);
    apple_dt_prop_u32(b, "attach-start-readiness-dryrun", 0u);
    apple_dt_prop_u32(b, "attach-readiness", 0u);
    apple_dt_prop_u32(b, "start-readiness", 0u);
    apple_dt_prop_u32(b, "attach-runtime-exec", 0u);
    apple_dt_prop_u32(b, "start-runtime-exec", 0u);
    apple_dt_prop_str(b, "attach-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_str(b, "start-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_u32(b, "attach-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "start-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "attach-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "start-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "provider-start-required", 0u);
    apple_dt_prop_str(b, "attach-start-provenance", "stage76-rejected-unattached");
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
    apple_dt_prop_u32(b, "register-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "register-provider-start-req", 0u);
    apple_dt_prop_u32(b, "register-service-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "notification-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "register-dependency-ready", 0u);
    apple_dt_prop_str(b, "lifecycle-provenance", "stage76-rejected-unregistered");
    apple_dt_prop_u32(b, "prov-notify-dryrun", 0u);
    apple_dt_prop_u32(b, "prov-notify-ready", 0u);
    apple_dt_prop_u32(b, "interest-ready", 0u);
    apple_dt_prop_u32(b, "interest-runtime-exec", 0u);
    apple_dt_prop_u32(b, "delivery-ready", 0u);
    apple_dt_prop_u32(b, "delivery-runtime-exec", 0u);
    apple_dt_prop_str(b, "interest-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_u32(b, "interest-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "interest-type-mask", 0u);
    apple_dt_prop_u32(b, "expected-notify-mask", 0u);
    apple_dt_prop_u32(b, "delivered-notify-mask", 0u);
    apple_dt_prop_u32(b, "interest-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "delivery-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "notify-dependency-ready", 0u);
    apple_dt_prop_str(b, "notify-provenance", "stage76-rejected-undelivered");
    apple_dt_prop_u32(b, "prov-callback-dryrun", 0u);
    apple_dt_prop_u32(b, "callback-ready", 0u);
    apple_dt_prop_u32(b, "client-notify-ready", 0u);
    apple_dt_prop_u32(b, "callback-runtime-exec", 0u);
    apple_dt_prop_u32(b, "client-notify-runtime-exec", 0u);
    apple_dt_prop_str(b, "callback-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_u32(b, "callback-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "callback-type-mask", 0u);
    apple_dt_prop_u32(b, "expected-callback-mask", 0u);
    apple_dt_prop_u32(b, "delivered-callback-mask", 0u);
    apple_dt_prop_u32(b, "client-ack-mask", 0u);
    apple_dt_prop_u32(b, "callback-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "client-notify-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "callback-dependency-ready", 0u);
    apple_dt_prop_str(b, "callback-provenance", "stage76-rejected-nocallback");
    apple_dt_prop_u32(b, "client-open-dryrun", 0u);
    apple_dt_prop_u32(b, "client-open-ready", 0u);
    apple_dt_prop_u32(b, "provider-claim-ready", 0u);
    apple_dt_prop_u32(b, "client-close-ready", 0u);
    apple_dt_prop_u32(b, "open-runtime-exec", 0u);
    apple_dt_prop_u32(b, "claim-runtime-exec", 0u);
    apple_dt_prop_u32(b, "close-runtime-exec", 0u);
    apple_dt_prop_str(b, "open-provider-path", "IODeviceTree:/unlinked");
    apple_dt_prop_u32(b, "open-provider-ordinal", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "open-type-mask", 0u);
    apple_dt_prop_u32(b, "expected-open-mask", 0u);
    apple_dt_prop_u32(b, "claimed-open-mask", 0u);
    apple_dt_prop_u32(b, "client-close-mask", 0u);
    apple_dt_prop_u32(b, "open-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "close-order", STAGE76_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL);
    apple_dt_prop_u32(b, "open-dependency-ready", 0u);
    apple_dt_prop_str(b, "open-provenance", "stage76-rejected-noopen");

    /* /chosen */
    apple_dt_node_begin(b, 4, 0);
    apple_dt_prop_str(b, "name", "chosen");
    apple_dt_prop_str(b, "boot-args", "debug=0x144 serial=0x1 mi4ios6.stage=76 xnu-cg xnu-bs xnu-pmap-bs pexpert-hook-ready iokit-platform iokit-match iokit-regsvc iokit-provider iokit-catalog iokit-propinh iokit-regtop iokit-as-lf-pn-open pmap-ref st76dt=0x76");
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
        apple_dt_node_begin(b, 6, 0);
        apple_dt_prop_str(b, "name", name);
        apple_dt_prop_str(b, "device_type", "cpu");
        apple_dt_prop_str(b, "compatible", "qcom,krait");
        apple_dt_prop_u32(b, "reg", cpu);
        apple_dt_prop_u32(b, "clock-frequency", 2265600000u);
        apple_dt_prop_u32(b, "timebase-frequency", 19200000u);
    }

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
    apple_dt_node_begin(b, 5, 0);
    apple_dt_prop_str(b, "name", "timer");
    apple_dt_prop_str(b, "compatible", "qcom,msm-timer");
    apple_dt_prop_u32(b, "frequency", 19200000u);
    apple_dt_prop_u32_array(b, "reg", timer_reg, ARRAY_SIZE(timer_reg));
    apple_dt_prop_str(b, "use", "early-timebase");
}

int test_kernel_entry(struct boot_args *args)
{
    log_puts("MI4IOS6_STAGE76 handoff -> test_kernel_entry(boot_args*)\n");

    if (!args) {
        log_puts("MI4IOS6_STAGE76 handoff bad: null args\n");
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
        log_puts("MI4IOS6_STAGE76 handoff bad: rev/version\n");
        return 0;
    }
    if (!args->deviceTreeP || !args->deviceTreeLength) {
        log_puts("MI4IOS6_STAGE76 handoff bad: no dt\n");
        return 0;
    }

    if (!apple_dt_selftest_and_log(args->deviceTreeP, args->deviceTreeLength)) {
        log_puts("MI4IOS6_STAGE76 handoff bad: dt selftest\n");
        return 0;
    }

    log_puts("MI4IOS6_STAGE76 handoff ok: boot_args + fuller apple_dt validated\n");
    return 1;
}

void platform_reboot(void)
{
    volatile uint32_t *restart_reason = (volatile uint32_t *)RESTART_REASON;
    volatile uint32_t *ps_hold = (volatile uint32_t *)MSM8974_PSHOLD;

    *restart_reason = RESTART_NORMAL;
    __asm__ volatile ("dsb sy" ::: "memory");

    log_puts("MI4IOS6_STAGE76 attempting MSM8974 PS_HOLD reset\n");
    *ps_hold = 0;
    __asm__ volatile ("dsb sy" ::: "memory");

    for (;;) {
        __asm__ volatile ("wfe");
    }
}

void stage76_main(void)
{
    struct apple_dt_builder b;
    uint32_t dt_len;

    log_init();
    log_puts("MI4IOS6_STAGE76 v1 entered; public-XNU pexpert/platform graph + bounded ARM pe_bootargs link proof scaffold; ram_console live\n");
    log_kv32("stage76_image_end", (uint32_t)(uintptr_t)__stage76_image_end);

    build_stage76_apple_dt(&b);
    dt_len = apple_dt_finish(&b);
    log_kv32("built_apple_dt_len", dt_len);

    if (!dt_len) {
        log_puts("MI4IOS6_STAGE76 apple_dt build failed\n");
        platform_reboot();
    }

    build_boot_args(&g_boot_args, g_apple_dt, dt_len);
    log_puts("MI4IOS6_STAGE76 boot_args built (rev2); fuller apple_dt attached\n");

    log_puts("MI4IOS6_STAGE76 vector base installed at ");
    log_hex32((uint32_t)(uintptr_t)stage76_vectors);
    log_puts("\n");

    log_puts("MI4IOS6_STAGE76 boot-wrapper handoff -> kernel_entry(boot_args*)\n");
    if (kernel_entry(&g_boot_args)) {
        log_puts("MI4IOS6_STAGE76 kernel_entry returned success\n");
    } else {
        log_puts("MI4IOS6_STAGE76 kernel_entry returned failure\n");
        platform_reboot();
    }

    platform_reboot();
}
