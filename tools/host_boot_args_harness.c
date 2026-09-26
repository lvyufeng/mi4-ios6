/*
 * Execute stage90_xnu_boot_args_prepare's invariant checks on the host.
 *
 * Phase 2's module had never been executed anywhere - it is behind a default-off switch and
 * the device has not run it - so this is the first execution evidence for it. Its checks are
 * arithmetic against __stage90_image_end, the one input the host cannot know without the
 * linker; host_boot_args_check.sh supplies it by --defsym, read from the built ELF.
 *
 * Two things make this a real test rather than a re-enactment:
 *   - the module is compiled unmodified from src/, and the symbol is only ever used
 *     as an address value, never dereferenced, so --defsym reproduces the device's computation
 *     exactly;
 *   - the harness recomputes all eight invariants itself, from the struct fields, rather than
 *     trusting the module's own check/failure counters.
 *
 * Built as 32-bit on purpose: the module's own _Static_asserts encode the ARM ILP32 ABI, so
 * "it compiles at -m32" is part of the check rather than a formality worked around.
 */
typedef unsigned int u32;
typedef unsigned long uptr;

extern unsigned char __stage90_image_end[];   /* supplied by --defsym */

int stage90_xnu_boot_args_prepare(void *dt, u32 dt_len);
const void *stage90_xnu_boot_args(void);
const void *stage90_xnu_boot_args_result(void);

void xnu_log_puts(const char *s);
void xnu_log_kv32(const char *k, u32 v);

/* Views over the payload's structs, read by offset - the offsets the module asserts. */
struct r_view {
    u32 version, size, status, virt_base, phys_base, mem_size,
        top_of_kernel_data, image_base, image_end, table_bytes,
        device_tree_ptr, device_tree_length, machine_type,
        command_line_len, checks, failures, checksum;
};
struct a_view {
    unsigned short Revision, Version;
    u32 virtBase, physBase, memSize, topOfKernelData;
    u32 video[6];
    u32 machineType;
    u32 deviceTreeP, deviceTreeLength;
    char CommandLine[256];
    u32 bootFlags, memSizeActual;
};

static unsigned char fake_dt[4096];

static void put(const char *s) { xnu_log_puts(s); }
static void puthex(u32 v) { xnu_log_kv32("", v); }

static void say(const char *label, int ok)
{
    put("    ");
    put(label);
    put(ok ? "yes\n" : "NO\n");
}

void _exit_now(int code);
static int run(void)
{
    const struct r_view *r;
    const struct a_view *a;
    int rc, bad = 0;

    put("executing stage90_xnu_boot_args_prepare on the host (32-bit, ARM ILP32 layout)\n");
    put("  __stage90_image_end supplied by --defsym = 0x00119000 (the built payload's value)\n\n");

    rc = stage90_xnu_boot_args_prepare(fake_dt, sizeof(fake_dt));
    r = (const struct r_view *)stage90_xnu_boot_args_result();
    a = (const struct a_view *)stage90_xnu_boot_args();

    xnu_log_kv32("return_value", (u32)rc);
    xnu_log_kv32("status", r->status);
    xnu_log_kv32("checks", r->checks);
    xnu_log_kv32("failures", r->failures);
    xnu_log_kv32("physBase", a->physBase);
    xnu_log_kv32("virtBase", a->virtBase);
    xnu_log_kv32("memSize", a->memSize);
    xnu_log_kv32("topOfKernelData", a->topOfKernelData);
    xnu_log_kv32("image_end", r->image_end);
    xnu_log_kv32("table_bytes", r->table_bytes);
    xnu_log_kv32("command_line_len", r->command_line_len);

    put("\n  the module's checks, recomputed independently:\n");
    { int ok = (a->physBase & 0xfffffu) == 0;                     bad += !ok; say("physBase 1MB-aligned:        ", ok); }
    { int ok = (a->virtBase & 0xfffffu) == 0;                     bad += !ok; say("virtBase 1MB-aligned:        ", ok); }
    { int ok = (a->topOfKernelData & 0x3fffu) == 0;               bad += !ok; say("topOfKernelData 16KB-aligned:", ok); }
    { int ok = a->topOfKernelData >= r->image_end;                bad += !ok; say("tables above the image:      ", ok); }
    { int ok = r->image_end <= a->physBase + a->memSize;          bad += !ok; say("image inside [physBase,+):   ", ok); }
    { int ok = (a->topOfKernelData + r->table_bytes) <= (a->physBase + a->memSize); bad += !ok; say("tables fit inside the span:  ", ok); }
    { int ok = a->CommandLine[255] == 0;                          bad += !ok; say("command line NUL-terminated: ", ok); }
    { int ok = (a->deviceTreeP != 0) && (a->deviceTreeLength != 0); bad += !ok; say("device tree present:         ", ok); }

    put("\n");
    if (rc != 0 || r->failures != 0 || bad != 0) {
        put("RESULT: FAIL - the module rejected the configuration\n");
        return 1;
    }
    put("RESULT: PASS - every invariant holds for the real image_end\n");
    return 0;
}

int main(void)
{
    _exit_now(run());
    return 0;
}
