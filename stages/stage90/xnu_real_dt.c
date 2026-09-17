/*
 * Run XNU's own pexpert device-tree code on the payload's device tree.
 *
 * Why this exists, and what it is not. Until now this project has compiled five public-XNU
 * objects and *proved them linkable*, and its own notes say plainly that none of them has ever
 * executed on the device. Everything that walked the device tree was Stage-owned code with XNU's
 * semantics reimplemented (`apple_dt.c`). This module is the first place where unmodified Apple
 * source runs on MSM8974 and does real work: `pexpert/gen/device_tree.c`'s `DTInit`,
 * `DTLookupEntry`, `DTFindEntry` and the entry/property iterators, compiled from the tarball and
 * linked into the payload unchanged.
 *
 * It is deliberately the device-tree layer first, of the five objects available, for three
 * reasons. It is what XNU's `pe_identify_machine` and `pe_init` actually call, so it is on the
 * real path rather than beside it. Its dependencies are tiny - `kalloc`, `kfree`, `strcmp` - so
 * linking it does not drag in a kernel. And it can be *checked*: the tree it walks is one this
 * payload built, so the values XNU's code reports can be compared against the values that were
 * put in, which makes a pass mean something.
 *
 * What it does not do: it does not jump to XNU, does not run XNU's `arm_init`, and does not make
 * any of the Stage-owned `_start`/`arm_init`/`arm_vm_init` symbols real. It runs one XNU
 * subsystem, in the payload's own context, and reports what it found.
 *
 * Walking it the way XNU does - by populating `PE_state` and letting `pe_identify_machine` run -
 * is a later step, because that function's timer bring-up is the Phase 3 shim's job. This module
 * calls the same DT entry points that function calls, in the same order, so what it establishes
 * transfers.
 */

#include "stage90.h"

/*
 * <pexpert/pexpert.h> resolves to the project's shim (shims/pexpert/pexpert.h) because -Ishims
 * comes first; <pexpert/device_tree.h> falls through to external/xnu-upstream/pexpert because the
 * shim has no such file. That is deliberate: PE_state_t must be the same type the linked
 * arm_pe_bootargs.o was compiled against, and the shim is what it saw.
 */
#include <pexpert/device_tree.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/consistent_debug.h>

/*
 * pe_gen.c's console entry points. The declarations are the shim header's, which is what pe_gen.o
 * was compiled against.
 */
extern void pe_init_debug(void);
extern void PE_init_printf(boolean_t vm_initialized);
extern void PE_enter_debugger(const char *cause);
extern void (*PE_putc)(char);

/* The probe's own arena for the entries it hands back; XNU's code allocates nothing here. */
static struct stage90_xnu_real_dt_result g_result;

/*
 * The payload's console sink for XNU. One character at a time, because that is the contract
 * PE_putc has; log_puts is the payload's own and takes a string.
 */
static void real_dt_console_putc(char c)
{
    char buf[2];

    buf[0] = c;
    buf[1] = '\0';
    log_puts(buf);
}

static void real_dt_log(const struct stage90_xnu_real_dt_result *r)
{
    xnu_log_puts("stage90 xnu_real_dt result:\n");
    xnu_log_kv32("xnu_real_dt_version", r->version);
    xnu_log_kv32("xnu_real_dt_size", r->size);
    xnu_log_kv32("xnu_real_dt_status", r->status);
    xnu_log_kv32("xnu_real_dt_tree_ptr", r->tree_ptr);
    xnu_log_kv32("xnu_real_dt_tree_len", r->tree_len);
    xnu_log_kv32("xnu_real_dt_checks", r->checks);
    xnu_log_kv32("xnu_real_dt_failures", r->failures);
    xnu_log_kv32("xnu_real_dt_lookup_cpus_ok", r->lookup_cpus_ok);
    xnu_log_kv32("xnu_real_dt_cpu_children", r->cpu_children);
    xnu_log_kv32("xnu_real_dt_cpu0_timebase_hz", r->cpu0_timebase_hz);
    xnu_log_kv32("xnu_real_dt_cpu0_state_is_running", r->cpu0_state_is_running);
    xnu_log_kv32("xnu_real_dt_cpu_state_running_count", r->cpu_state_running_count);
    xnu_log_kv32("xnu_real_dt_find_name_arm_io_ok", r->find_name_arm_io_ok);
    xnu_log_kv32("xnu_real_dt_arm_io_device_type_len", r->arm_io_device_type_len);
    xnu_log_kv32("xnu_real_dt_arm_io_ranges_len", r->arm_io_ranges_len);
    xnu_log_kv32("xnu_real_dt_arm_io_ranges0", r->arm_io_ranges0);
    xnu_log_kv32("xnu_real_dt_arm_io_ranges1_soc_phys", r->arm_io_ranges1);
    xnu_log_kv32("xnu_real_dt_find_device_type_timer_ok", r->find_device_type_timer_ok);
    xnu_log_kv32("xnu_real_dt_timer_reg_len", r->timer_reg_len);
    xnu_log_kv32("xnu_real_dt_timer_reg0", r->timer_reg0);
    xnu_log_kv32("xnu_real_dt_timer_reg1", r->timer_reg1);
    xnu_log_kv32("xnu_real_dt_xnu_timer_map_addr", r->xnu_timer_map_addr);
    xnu_log_kv32("xnu_real_dt_chosen_lookup_ok", r->chosen_lookup_ok);
    xnu_log_kv32("xnu_real_dt_chosen_memory_map_absent", r->chosen_memory_map_absent);
    xnu_log_kv32("xnu_real_dt_root_lookup_ok", r->root_lookup_ok);
    xnu_log_kv32("xnu_real_dt_boot_args_ptr", r->boot_args_ptr);
    xnu_log_kv32("xnu_real_dt_pe_boot_args_ok", r->pe_boot_args_ok);
    xnu_log_kv32("xnu_real_dt_parse_stage_arg_found", r->parse_stage_arg_found);
    xnu_log_kv32("xnu_real_dt_parse_stage_arg_value", r->parse_stage_arg_value);
    xnu_log_kv32("xnu_real_dt_parse_debug_arg_found", r->parse_debug_arg_found);
    xnu_log_kv32("xnu_real_dt_parse_debug_arg_value", r->parse_debug_arg_value);
    xnu_log_kv32("xnu_real_dt_parse_absent_arg_found", r->parse_absent_arg_found);
    xnu_log_kv32("xnu_real_dt_cd_inherit_ok", r->cd_inherit_ok);
    xnu_log_kv32("xnu_real_dt_cd_enabled", r->cd_enabled);
    xnu_log_kv32("xnu_real_dt_cd_register_ok", r->cd_register_ok);
    xnu_log_kv32("xnu_real_dt_cd_record_readback_ok", r->cd_record_readback_ok);
    xnu_log_kv32("xnu_real_dt_cd_header_intact", r->cd_header_intact);
    xnu_log_kv32("xnu_real_dt_pe_init_debug_ok", r->pe_init_debug_ok);
    xnu_log_kv32("xnu_real_dt_pe_putc_installed", r->pe_putc_installed);
    xnu_log_kv32("xnu_real_dt_console_bytes", r->console_bytes);
    xnu_log_kv32("xnu_real_dt_debugger_calls", r->debugger_calls);
    xnu_log_kv32("xnu_real_dt_checksum", r->checksum);
}

/*
 * Read a NUL-terminated string property. Returns 1 and sets *out if it is present, non-empty and
 * NUL-terminated inside the size the tree reports - the last part matters because this walks a
 * buffer with a declared length and a property whose size lies would otherwise read past it.
 */
static uint32_t real_dt_get_string(DTEntry entry, const char *name, const char **out)
{
    void *value = NULL;
    unsigned int size = 0u;
    const char *s;

    if (DTGetProperty(entry, name, &value, &size) != kSuccess || value == NULL || size == 0u) {
        return 0u;
    }

    s = (const char *)value;
    for (unsigned int i = 0u; i < size; i++) {
        if (s[i] == '\0') {
            *out = s;
            return 1u;
        }
    }
    return 0u;
}

static uint32_t real_dt_get_bytes(DTEntry entry, const char *name, uint32_t *len)
{
    void *value = NULL;
    unsigned int size = 0u;

    if (DTGetProperty(entry, name, &value, &size) != kSuccess || value == NULL || size == 0u) {
        return 0u;
    }
    *len = (uint32_t)size;
    return 1u;
}

/*
 * Read up to two u32 words of a property. Used for `ranges` and `reg`, whose first two words are
 * the ones XNU's platform code uses: pe_identify_machine.c:236 takes `*(ranges_prop + 1)` as the
 * SoC physical base, and :554 computes `ml_io_map(soc_phys + *reg_prop, *(reg_prop + 1))`.
 * Reading them with XNU's own DTGetProperty is what makes the addresses below the ones XNU would
 * actually use rather than the ones the builder intended.
 */
static uint32_t real_dt_get_words(DTEntry entry, const char *name, uint32_t *w0, uint32_t *w1)
{
    void *value = NULL;
    unsigned int size = 0u;
    const uint32_t *words;

    if (DTGetProperty(entry, name, &value, &size) != kSuccess || value == NULL ||
        size < sizeof(uint32_t)) {
        return 0u;
    }
    words = (const uint32_t *)value;
    *w0 = words[0];
    *w1 = (size >= 2u * sizeof(uint32_t)) ? words[1] : 0u;
    return 1u;
}

int stage90_xnu_real_dt_run(struct boot_args *args, void *tree, uint32_t tree_len)
{
    struct stage90_xnu_real_dt_result *r = &g_result;
    DTEntry entry;

    memset(r, 0, sizeof(*r));
    r->version = STAGE90_XNU_REAL_DT_VERSION;
    r->size = sizeof(*r);
    r->boot_args_ptr = (uint32_t)(uintptr_t)args;
    r->tree_ptr = (uint32_t)(uintptr_t)tree;
    r->tree_len = tree_len;

    xnu_log_puts("stage90 xnu_real_dt: running XNU's own pexpert/gen/device_tree.c on this tree\n");

    if (tree == NULL || tree_len == 0u || args == NULL) {
        r->status = STAGE90_STATUS_BASE;
        r->checksum = stage90_xnu_real_dt_checksum(r);
        real_dt_log(r);
        return -1;
    }

    /*
     * The first public-XNU call. Everything above this line in the payload is Stage-owned code;
     * this one is Apple's, compiled from external/xnu-upstream/pexpert/gen/device_tree.c.
     */
    /* Point pe_gen.c's console at the payload's log before any XNU code can write to it. */
    stage90_xnu_shim_console_hook = real_dt_console_putc;

    DTInit(tree);

    /* 1. "/" - the root. A mis-sized header fails here before anything dereferences it. */
    r->checks++;
    if (DTLookupEntry(NULL, "/", &entry) == kSuccess) {
        r->root_lookup_ok = 1u;
    } else {
        r->failures++;
    }

    /*
     * 2. /cpus - the topology walk pe_identify_machine.c:117 does. It iterates the children and
     * reads each one's state, skipping any CPU whose state is not "running"; that skip is why
     * the missing `state` property was a real defect and not a cosmetic one (the timebase
     * frequency of every CPU was being ignored).
     */
    r->checks++;
    if (DTLookupEntry(NULL, "/cpus", &entry) == kSuccess) {
        DTEntryIterator it;
        DTEntry child;

        r->lookup_cpus_ok = 1u;

        if (DTCreateEntryIterator(entry, &it) == kSuccess) {
            while (DTIterateEntries(it, &child) == kSuccess) {
                const char *state = NULL;

                r->cpu_children++;
                if (real_dt_get_string(child, "state", &state) &&
                    strcmp(state, "running") == 0) {
                    r->cpu_state_running_count++;
                }
                if (r->cpu_children == 1u) {
                    unsigned int freq_len = 0u;
                    const void *freq = NULL;
                    unsigned int freq_size = 0u;

                    (void)freq_len;
                    if (DTGetProperty(child, "timebase-frequency", (void **)&freq, &freq_size) == kSuccess &&
                        freq != NULL && freq_size == sizeof(uint32_t)) {
                        r->cpu0_timebase_hz = *(const uint32_t *)freq;
                    }
                    if (state != NULL && strcmp(state, "running") == 0) {
                        r->cpu0_state_is_running = 1u;
                    }
                }
            }
            DTDisposeEntryIterator(it);
        }
        if (r->cpu0_timebase_hz != 19200000u || r->cpu0_state_is_running != 1u) {
            r->failures++;
        }
    } else {
        r->failures++;
    }

    /*
     * 3. DTFindEntry("name", "arm-io") - pe_identify_machine.c:232's lookup, and the one that was
     * returning nothing until the /arm-io node existed. It searches by property *value*, which is
     * a different code path from DTLookupEntry's path walk, so both are worth exercising.
     */
    r->checks++;
    if (DTFindEntry("name", "arm-io", &entry) == kSuccess) {
        const char *type = NULL;

        r->find_name_arm_io_ok = 1u;

        /*
         * XNU does not compare this string to anything - pe_identify_machine.c:234 copies it into
         * gPESoCDeviceTypeBuffer with strlcpy and that is all. So the check is "present and
         * non-empty", and nothing more; an earlier version of this probe asserted it equalled
         * "arm-io" and reported a failure against a tree XNU's own code is perfectly happy with.
         */
        if (real_dt_get_string(entry, "device_type", &type)) {
            r->arm_io_device_type_len = (uint32_t)strlen(type) + 1u;
        }
        if (real_dt_get_bytes(entry, "ranges", &r->arm_io_ranges_len)) {
            (void)real_dt_get_words(entry, "ranges", &r->arm_io_ranges0, &r->arm_io_ranges1);
        }

        /*
         * ranges[1] becomes gPESoCBasePhys (:236), and that value is added to every reg[0] below.
         * The minimum for that read to be in-bounds is two words; the value itself is reported
         * rather than judged, because whether it is the *right* base is the Phase 3 question.
         */
        if (r->arm_io_device_type_len == 0u || r->arm_io_ranges_len < 8u) {
            r->failures++;
        }
    } else {
        r->failures++;
    }

    /* 4. DTFindEntry("device_type", "timer") - the lookup host_dt_check.sh found missing. */
    r->checks++;
    if (DTFindEntry("device_type", "timer", &entry) == kSuccess) {
        r->find_device_type_timer_ok = 1u;
        if (real_dt_get_bytes(entry, "reg", &r->timer_reg_len)) {
            (void)real_dt_get_words(entry, "reg", &r->timer_reg0, &r->timer_reg1);
        }
        if (r->timer_reg_len < 8u) {
            r->failures++;
        }
    } else {
        r->failures++;
    }

    /*
     * The measurement this probe exists to make, done with XNU's own arithmetic.
     * pe_arm_map_interrupt_controller (:554) computes `ml_io_map(soc_phys + *reg_prop, ...)`,
     * i.e. it treats reg[0] as an *offset* from the SoC base. If our reg[0] is absolute instead,
     * this is the address XNU would map - and a value that is not the timer's real address is the
     * concrete statement of the reg-model gap that docs/reference/... describes from reading.
     */
    r->xnu_timer_map_addr = r->arm_io_ranges1 + r->timer_reg0;

    /*
     * 5. /chosen's memory-map - pe_init.c reads it behind a kSuccess check, so absent is a pass.
     * Checked because "absent is fine" is a claim, and this is the code that gets to make it.
     */
    r->checks++;
    if (DTLookupEntry(NULL, "/chosen", &entry) == kSuccess) {
        void *value = NULL;
        unsigned int size = 0u;

        r->chosen_lookup_ok = 1u;
        if (DTGetProperty(entry, "memory-map", &value, &size) != kSuccess) {
            r->chosen_memory_map_absent = 1u;
        }
    } else {
        r->failures++;
    }

    r->status = (r->failures == 0u) ? STAGE90_STATUS_OK : STAGE90_STATUS_FAIL(r->failures);
    /*
     * 6. Real ARM pexpert, on the payload's own state. arm_pe_bootargs.c's PE_boot_args() is
     * three lines of Apple source - it returns ((boot_args *)PE_state.bootArgs)->CommandLine - so
     * pointing PE_state at the payload's own boot_args and reading it back is a direct test that
     * XNU's ARM code is reading the structure this payload built, through the layout Apple
     * declares. That layout is checked independently by tools/check_xnu_struct_abi.py.
     */
    r->checks++;
    PE_state.bootArgs = args;
    PE_state.deviceTreeHead = tree;
    {
        char *cmdline = PE_boot_args();

        r->pe_boot_args_ok = (cmdline != NULL && strcmp(cmdline, args->CommandLine) == 0) ? 1u : 0u;
        if (r->pe_boot_args_ok == 0u) {
            r->failures++;
        }
    }

    /*
     * 7. XNU's own boot-argument parser, on the command line `PE_boot_args()` just handed back.
     *
     * The parser matches a token's *whole name* - bootargs.c does `strncmp(args, arg_string, i) ||
     * (i != strlen(arg_string))`, so it is exact, not a prefix match - and it only considers
     * `name=value` and `-flag` forms; a bare word like `xnu-postpe` is skipped. The three cases
     * below are chosen from the payload's actual CommandLine (boot_args.c: "debug=0x144
     * mi4ios6.stage=83 xnu-pe-init-false ..."), and the third is a negative: a parser that
     * reported success for everything would be indistinguishable from one that works.
     *
     * An earlier version of this asked for "mi4ios6" and expected to find it. It is not a token -
     * "mi4ios6.stage" is - so the probe failed against a correct parser. Third time in this
     * project that an assertion about what the other side does turned out to be the thing at
     * fault, which is why this comment says where the rule was read from.
     */
    r->checks++;
    {
        uint32_t value = 0u;
        uint32_t stage = 0u;

        if (PE_parse_boot_argn("mi4ios6.stage", &stage, (int)sizeof(stage))) {
            r->parse_stage_arg_found = 1u;
            r->parse_stage_arg_value = stage;
        }
        if (PE_parse_boot_argn("debug", &value, (int)sizeof(value))) {
            r->parse_debug_arg_found = 1u;
            r->parse_debug_arg_value = value;
        }
        if (PE_parse_boot_argn("no-such-stage90-arg", &value, (int)sizeof(value))) {
            r->parse_absent_arg_found = 1u;
        }
        /* 0x83 = 131; the cmdline says 83 decimal, which getval returns as a number. */
        if (r->parse_stage_arg_found != 1u || r->parse_stage_arg_value != 83u ||
            r->parse_debug_arg_found != 1u || r->parse_debug_arg_value != 0x144u ||
            r->parse_absent_arg_found != 0u) {
            r->failures++;
        }
    }

    /*
     * 8-10. XNU's consistent-debug registry, driven end to end.
     *
     * This is the crash-log structure iBoot hands the kernel, and it is the first piece of XNU
     * that does something rather than observe something. Three calls, and each one is a
     * different part of the subsystem:
     *
     *   PE_consistent_debug_inherit()  looks up /chosen's "consistent-debug-root" through XNU's
     *                                 own DT reader, takes the first word as a physical address,
     *                                 and maps it. Its -1 return when the property is missing is
     *                                 how XNU reports "this platform has no registry".
     *   PE_consistent_debug_enabled()  the registry pointer is now non-NULL.
     *   PE_consistent_debug_register() allocates an entry by CAS (through the shim's
     *                                 OSCompareAndSwap64, since ldrexd is not ours) and writes a
     *                                 record into it.
     *
     * The record is read back here rather than trusted, because a register that returned 0
     * without writing anything would look identical from the outside.
     */
    r->checks++;
    if (PE_consistent_debug_inherit() == 0) {
        r->cd_inherit_ok = 1u;
    } else {
        r->failures++;
    }

    r->checks++;
    r->cd_enabled = (uint32_t)PE_consistent_debug_enabled();
    if (r->cd_enabled != 1u) {
        r->failures++;
    }

    r->checks++;
    {
        /*
         * A record id that follows the same eight-ASCII-characters convention as the header -
         * this is our own marker, not an Apple one, so it is spelled out here with the letters
         * that say what it is.
         */
        const uint64_t record_id = DEBUG_RECORD_ID_LONG('S', 'T', '9', '0', 'D', 'B', 'G', 'R');
        const uint64_t physaddr = 0xde500000u;   /* the ram_console window, as a plausible payload */
        const uint64_t length = 0x1000u;
        dbg_registry_t *registry =
            (dbg_registry_t *)(uintptr_t)stage90_xnu_consistent_debug_region_init();
        dbg_record_header_t *record = &registry->records[0];

        if (PE_consistent_debug_register(record_id, physaddr, length) == 0) {
            r->cd_register_ok = 1u;
        } else {
            r->failures++;
        }
        if (record->record_id == record_id && record->physaddr == physaddr &&
            record->length == length) {
            r->cd_record_readback_ok = 1u;
        }
        /* The title header must survive: the registry's own id is not the first record slot. */
        r->cd_header_intact =
            (registry->top_level_header.num_records == DEBUG_REGISTRY_MAX_RECORDS &&
             registry->top_level_header.record_size_bytes == (uint32_t)sizeof(dbg_record_header_t)) ? 1u : 0u;
        if (r->cd_register_ok != 1u || r->cd_record_readback_ok != 1u ||
            r->cd_header_intact != 1u) {
            r->failures++;
        }
    }

    /*
     * 11-13. XNU's console and debugger paths, which are the last of the five objects and the
     * ones that produce output rather than consume input.
     *
     * pe_init_debug() parses "debug=0x144" out of the command line with PE_parse_boot_argn and
     * stores it in pe_gen.c's own DEBUGFlag - which is static, so the only way to observe that it
     * worked is to make XNU act on it. PE_enter_debugger() acts on it: it calls Debugger() only
     * when DB_NMI is set in that flag. 0x144 has DB_NMI, so a call proves the parse happened; the
     * shim counts the calls so the log can read the fact rather than infer it.
     *
     * PE_init_printf(FALSE) then installs cnputc as PE_putc, and the payload has pointed cnputc at
     * its own ram_console. So the bytes written below are XNU's console path writing into the log
     * this experiment is read from.
     */
    r->checks++;
    {
        const char *banner = "\nMI4IOS6_STAGE90 xnu console: wrote this line through PE_putc\n";
        uint32_t count = 0u;

        pe_init_debug();
        r->pe_init_debug_ok = 1u;

        PE_init_printf(FALSE);
        r->pe_putc_installed = (PE_putc != 0) ? 1u : 0u;

        /* Only emit through XNU's own pointer, so what appears in the log came from pe_gen.c. */
        if (PE_putc != 0) {
            while (banner[count] != '\0') {
                PE_putc(banner[count]);
                count++;
            }
        }
        r->console_bytes = count;

        if (r->pe_putc_installed != 1u || count == 0u) {
            r->failures++;
        }
    }

    r->checks++;
    PE_enter_debugger("stage90");
    r->debugger_calls = stage90_xnu_shim_debugger_calls;
    /*
     * The command line says debug=0x144, whose DB_NMI bit is 0x4. So pe_init_debug's parse must
     * have stored a flag with DB_NMI set, and PE_enter_debugger must therefore have called
     * Debugger. A zero here would mean the parse did not happen or did not take.
     */
    if (r->debugger_calls != 1u) {
        r->failures++;
    }

    r->checksum = stage90_xnu_real_dt_checksum(r);
    real_dt_log(r);

    if (r->status == STAGE90_STATUS_OK) {
        xnu_log_puts("stage90 xnu_real_dt: XNU's device-tree code walked this tree and agreed\n");
    } else {
        xnu_log_puts("stage90 xnu_real_dt: XNU's device-tree code did NOT agree - see failures\n");
    }

    return (r->status == STAGE90_STATUS_OK) ? 0 : -1;
}

const struct stage90_xnu_real_dt_result *stage90_xnu_real_dt_result(void)
{
    return &g_result;
}
