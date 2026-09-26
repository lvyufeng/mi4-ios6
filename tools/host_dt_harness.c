/*
 * Walk our device tree with XNU's own reader, on the build host.
 *
 * Why this exists
 * ---------------
 * `apple_dt_selftest_and_log` proves our tree is internally consistent - that the walk
 * lengths line up and the children we expect are where we expect. It does not prove that
 * *XNU's* reader can find anything, and those are different claims. "Accepted by 4570's
 * readers" is Phase 2's exit criterion, and its host-side half is realisable because
 * pexpert's device_tree.c is portable C once kalloc/kfree are ordinary allocation.
 *
 * So this harness drives XNU's actual DTInit/DTLookupEntry/DTFindEntry/DTGetProperty over
 * the tree our builder produces, and checks the lookups XNU's ARM platform code makes. It
 * is the difference between "we emit the right names" (which
 * tools/xnu_dt_requirements.py does by reading source) and "XNU's walker finds them in the
 * tree we actually build" (which is this).
 *
 * What it does NOT do: exercise the device, or prove anything about runtime addresses. A
 * tree that walks correctly here can still be wrong about a physical address. It removes
 * one class of failure, not all of them.
 *
 * Assembly
 * --------
 * Compiled by tools/host_dt_check.sh, which extracts `build_stage90_apple_dt` verbatim
 * from src/stage90_main.c and the constants it needs out of stage90.h, so the
 * code under test is the code that ships rather than a copy that can drift.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pexpert/device_tree.h>

/* The extracted builder and the constants it uses. */
#include "stage90_dt_shim.h"

/*
 * The payload's DT buffer is **not** defined here. It used to be - `uint8_t g_apple_dt[32768]` in
 * this file, while `stage90_main.c` has its own `static uint8_t g_apple_dt[32768]` and the shim
 * declared a third copy of the bound - three definitions of one number with nothing comparing them,
 * which is the defect class this project has paid for twenty-four times. Since 460 the storage comes
 * out of `stage90_main.c` verbatim (`tools/apple_dt_extract.py` de-`static`s the declaration) and
 * both the shim's `extern` and this harness's `sizeof` take the bound from that one reading.
 */

/*
 * device_tree.c's iterator allocation. In the kernel these are kalloc/kfree; here they are
 * plain allocation, which is the only thing that makes XNU's walker runnable on a host.
 */
void *kalloc(uint32_t size) { return malloc(size); }
void kfree(void *addr, uint32_t size) { (void)size; free(addr); }

/* The payload's logging goes to the ram_console; here it goes to stdout so the harness's
 * own output includes whatever apple_dt.c's selftest decides to say. */
void log_puts(const char *s) { fputs(s, stdout); }
void log_kv32(const char *key, uint32_t value) { printf("%s=0x%08x\n", key, value); }

/* The real apple_dt.c needs its own declarations; the shim header supplies them. */

static int failures = 0;

static void ok(const char *what)
{
    printf("  ok        %s\n", what);
}

static void fail(const char *what, const char *detail)
{
    printf("  MISSING   %s%s%s\n", what, detail ? " - " : "", detail ? detail : "");
    failures++;
}

/* Absent, but deliberately: either XNU survives it, or satisfying it now would activate a
 * wrong mapping. Reported so the distinction is visible rather than buried in a pass. */
static void expected_absent(const char *what, const char *why)
{
    printf("  absent*   %s - %s\n", what, why);
}

/* Present, but nothing XNU reads. No lookup names it; the check was mine, not XNU's. */
static void informational(const char *what)
{
    printf("  (report)  %s\n", what);
}

/* --- XNU's entry points, exactly as pe_identify_machine.c calls them --- */
static void check_lookup(const char *path, const char *why)
{
    DTEntry entry;
    if (DTLookupEntry(NULL, path, &entry) == kSuccess) {
        ok(path);
    } else {
        fail(path, why);
    }
}

static DTEntry lookup(const char *path)
{
    DTEntry entry = NULL;
    (void)DTLookupEntry(NULL, path, &entry);
    return entry;
}

/*
 * 459: `/chosen/memory-map` - the root device, read back with XNU's own reader.
 *
 * This node used to be reported as `expected_absent` ("pe_init.c reads it behind a kSuccess
 * check; absent is fine"), and it was: nothing in the boot *needed* it until this step. What
 * needs it now is `IOFindBSDRoot`, which looks this exact path up in the DT plane and is the
 * only thing in the kernel that turns a device-tree property into a root device
 * (`iokit/bsddev/IOKitBSDInit.cpp:436-490`): the property is two machine words handed to
 * `mdevadd` as {base, size}, and the boot-arg `rd=md0` then selects the device they made.
 * Without the node the function never reaches the `rd` check, `mdevlookup(0)` returns -1 and it
 * panics; without the property it skips the `mdevadd` call and panics the same way. Neither is
 * visible on the host except here, and both are one lookup away from being visible.
 *
 * The two words are checked against the *entry image's* own link - `STAGE90_XNU_RAMDISK_VA` and
 * `_SIZE` arrive from `out/stage90/xnu_arm_entry.h` through the shim header, so this is the
 * payload's value and not a restatement of it. A size mismatch is worth catching here because
 * `DTGetProperty` hands back `length` from the property header: a property of the wrong length
 * is read by XNU as `uintptr_t[2]` regardless, so a 4-byte `RAMDisk` would make `size` whatever
 * the next property's bytes happen to be.
 */
static void check_memory_map(void)
{
    const uint32_t want[2] = { STAGE90_XNU_RAMDISK_VA, STAGE90_XNU_RAMDISK_SIZE };
    DTEntry mm = lookup("/chosen/memory-map");
    DTEntry chosen = NULL;
    void *value = NULL;
    unsigned int size = 0;
    char detail[160];
    char args[256];

    if (!mm) {
        fail("/chosen/memory-map",
             "IOFindBSDRoot reads this path for the RAMDisk property; without the node the boot "
             "waits 60 s for an IOMedia that cannot exist on this machine");
        return;
    }
    if (DTGetProperty(mm, "RAMDisk", &value, &size) != kSuccess) {
        fail("/chosen/memory-map:RAMDisk",
             "without this property IOFindBSDRoot never calls mdevadd, mdevlookup(0) returns -1, "
             "and the same function panics 'specified root memory device, md0, has not been configured'");
        return;
    }
    if (size != sizeof(want)) {
        snprintf(detail, sizeof(detail), "size %u, expected %u - XNU reads it as uintptr_t[2]",
                 size, (unsigned)sizeof(want));
        fail("/chosen/memory-map:RAMDisk", detail);
    } else {
        const uint32_t *words = (const uint32_t *)value;
        if (words[0] != want[0] || words[1] != want[1]) {
            snprintf(detail, sizeof(detail),
                     "0x%08x/0x%08x, expected the entry image's 0x%08x/0x%08x - mdevadd would "
                     "register memory that is not the RAM disk",
                     words[0], words[1], want[0], want[1]);
            fail("/chosen/memory-map:RAMDisk", detail);
        } else {
            snprintf(detail, sizeof(detail), "0x%08x +0x%x, the entry image's g_stage90_ramdisk",
                     want[0], want[1]);
            ok(detail);
        }
    }

    /*
     * The boot args, in the tree's own copy of them. `PE_parse_boot_argn` reads the *boot_args
     * struct*'s CommandLine and not this property (`pexpert/arm/pe_bootargs.c:11`), which is
     * written in stage90_main.c to agree with it - and that agreement is the thing checked here,
     * because a `rd=` that lives in only one of the two copies is exactly the shape of the
     * `serial=0x1` that sat in this property doing nothing for a hundred experiments.
     */
    if (DTLookupEntry(NULL, "/chosen", &chosen) != kSuccess) {
        fail("/chosen:boot-args", "/chosen itself is missing");
        return;
    }
    if (size >= sizeof(args) || DTGetProperty(chosen, "boot-args", &value, &size) != kSuccess) {
        fail("/chosen:boot-args", "no boot-args property on /chosen");
        return;
    }
    memcpy(args, value, size);
    args[size] = '\0';
    if (!strstr(args, "rd=md0")) {
        fail("/chosen:boot-args",
             "the tree's copy of the boot args does not name md0, so the two copies disagree "
             "about the root device");
        return;
    }
    ok("/chosen:boot-args carries rd=md0");
}


static void check_find(const char *prop, const char *value, const char *why)
{
    DTEntry entry = NULL;
    int rc = value ? DTFindEntry(prop, value, &entry)
                   : DTFindEntry(prop, NULL, &entry);
    char label[128];
    snprintf(label, sizeof(label), "DTFindEntry(\"%s\", %s)", prop,
             value ? value : "NULL");
    if (rc == kSuccess) {
        ok(label);
    } else {
        fail(label, why);
    }
}

static void check_prop(DTEntry entry, const char *path, const char *name,
                       const char *why, unsigned int expect_size)
{
    void *value = NULL;
    unsigned int size = 0;
    char label[160];
    snprintf(label, sizeof(label), "%s:%s", path, name);
    if (!entry) {
        fail(label, "no such node");
        return;
    }
    if (DTGetProperty(entry, name, &value, &size) != kSuccess) {
        fail(label, why);
        return;
    }
    if (expect_size && size != expect_size) {
        char detail[96];
        snprintf(detail, sizeof(detail), "size %u, expected %u", size, expect_size);
        fail(label, detail);
        return;
    }
    ok(label);
}

/* XNU reads `state` with strncmp against "running"; anything else makes it skip the cpu. */
static void check_cpu_states(void)
{
    DTEntry cpus = lookup("/cpus");
    /* DTEntryIterator is an opaque pointer: device_tree.c allocates and owns it, which is
     * why it is not a stack struct here. */
    DTEntryIterator iter = NULL;
    DTEntry cpu = NULL;
    unsigned int count = 0;
    unsigned int running = 0;

    if (!cpus || DTCreateEntryIterator(cpus, &iter) != kSuccess || !iter) {
        fail("/cpus children", "iterator could not start");
        return;
    }

    while (DTIterateEntries(iter, &cpu) == kSuccess) {
        void *value = NULL;
        unsigned int size = 0;
        count++;
        if (DTGetProperty(cpu, "state", &value, &size) == kSuccess &&
            size >= 7 && strncmp((const char *)value, "running", 7) == 0) {
            running++;
        }
        /* ml_parse_cpu_topology asserts on `reg` under MACH_ASSERT. */
        if (DTGetProperty(cpu, "reg", &value, &size) != kSuccess) {
            fail("cpu reg", "ml_parse_cpu_topology asserts on this");
        }
    }

    DTDisposeEntryIterator(iter);

    printf("  /cpus children: %u, with state=\"running\": %u\n", count, running);
    if (count == 0) {
        fail("/cpus children", "ml_parse_cpu_topology panics 'No cpus found!'");
    }
    if (running != count) {
        fail("every cpu state=\"running\"",
             "pe_identify_machine skips cpus without it, losing timebase-frequency");
    } else {
        ok("every cpu state=\"running\"");
    }
}

int main(void)
{
    struct apple_dt_builder b;
    uint32_t dt_len;

    printf("walking our device tree with XNU's own reader\n\n");

    /* The builder writes into g_apple_dt itself (that is how the payload is written), so
     * the harness defines the buffer rather than passing its own. */
    apple_dt_begin(&b, g_apple_dt, sizeof(g_apple_dt));
    build_stage90_apple_dt(&b);
    dt_len = apple_dt_finish(&b);

    printf("tree built: %u bytes of %u (%.1f%% used, %u free)\n\n",
           dt_len, (unsigned)sizeof(g_apple_dt),
           100.0 * dt_len / sizeof(g_apple_dt),
           (unsigned)(sizeof(g_apple_dt) - dt_len));
    if (dt_len == 0) {
        printf("FAIL: builder produced nothing\n");
        return 1;
    }

    /*
     * Headroom is a real risk, not a curiosity. apple_dt_finish returns 0 when the
     * builder's capacity was exceeded, and stage90_main then calls platform_reboot() - so
     * an overgrown tree means the payload reboots early, and the reason would have to be
     * inferred from where the log stops. The tree has grown with each /arm-io, /state and
     * personality node added, so the margin is worth reporting on every build rather than
     * discovered when it runs out.
     */
    printf("headroom: %u bytes free (%.1f%% of the buffer)\n",
           (unsigned)(sizeof(g_apple_dt) - dt_len),
           100.0 * (sizeof(g_apple_dt) - dt_len) / sizeof(g_apple_dt));
    if (dt_len > (sizeof(g_apple_dt) * 95u) / 100u) {
        printf("WARNING: over 95%% of the device-tree buffer used; the next node added\n"
               "         would reboot the payload at the DT build, not fail visibly\n");
    }
    printf("\n");

    /* This is the call XNU makes first: PE_init_platform -> DTInit(boot_args->deviceTreeP). */
    DTInit(g_apple_dt);

    printf("paths XNU looks up with DTLookupEntry:\n");
    check_lookup("/chosen", "PE_init_platform reads /chosen before anything else");
    check_lookup("/cpus", "ml_parse_cpu_topology asserts, then panics 'No cpus found!'");
    check_memory_map();

    /*
     * `random-seed` is the third property this project added because XNU reads it and the tree
     * did not have it, after `state` on the cpu nodes and `device_type = "timer"` on /arm-io.
     * The 64-byte size is not decorative: `early_random` panics when `PE_get_random_seed`
     * returns fewer than `sizeof(EntropyData.buffer)`, so a short-but-present property would
     * look correct here and still stop the boot.
     */
    {
        DTEntry chosen = NULL;
        if (DTLookupEntry(NULL, "/chosen", &chosen) != kSuccess) {
            fail("/chosen:random-seed", "/chosen itself is missing");
        } else {
            check_prop(chosen, "/chosen", "random-seed",
                       "PE_get_random_seed returns 0 without it and early_random panics "
                       "'Insufficient entropy is fatal'", 64);
        }
    }

    printf("\nnodes XNU locates by (property, value):\n");
    check_find("name", "device-tree", "PE_init_platform reads target-type/model from it");
    check_find("name", "arm-io", "pe_arm_get_soc_base_phys returns 0 without it");
    check_find("device_type", "timer", "pe_arm_map_interrupt_controller needs it");
    expected_absent("DTFindEntry(\"interrupt-controller\", \"master\")",
                    "adding it now would activate a wrong-address mapping: XNU computes "
                    "soc_phys + reg[0] and our reg is absolute, so it would return 0 from "
                    "the gPicBase check today but map 0xf2000000 the moment that is fixed. "
                    "Phase 3 resolves the reg model first - see the contract doc.");
    informational("no XNU lookup names a cpu by \"cpu0\"; cpus are found by iterating "
                  "/cpus and reading reg, which the topology check below covers");

    printf("\nproperties XNU reads from the nodes it locates:\n");
    DTEntry arm_io = NULL;
    (void)DTFindEntry("name", "arm-io", &arm_io);
    check_prop(arm_io, "/arm-io", "ranges",
               "gPESoCBasePhys comes from ranges[1]", 0);
    check_prop(arm_io, "/arm-io", "device_type",
               "becomes gPESoCDeviceType", 0);
    check_prop(arm_io, "/arm-io", "chip-revision",
               "pe_arm_get_soc_revision falls back to 0", 0);

    DTEntry dt_node = NULL;
    (void)DTFindEntry("name", "device-tree", &dt_node);
    check_prop(dt_node, "/device-tree", "target-type", "PE_init_platform reads it", 0);
    check_prop(dt_node, "/device-tree", "model", "PE_init_platform reads it", 0);

    printf("\ncpu topology:\n");
    check_cpu_states();

    printf("\n");
    if (failures) {
        printf("FAIL: %d requirement(s) XNU's own reader could not satisfy from our tree.\n",
               failures);
        return 1;
    }
    printf("OK: XNU's reader found every node and property its ARM platform code asks for.\n");
    return 0;
}
