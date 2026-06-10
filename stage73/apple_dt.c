#include "stage73.h"

#define DT_PROP_NAME_LEN 32u

struct dt_node_hdr {
    uint32_t nProperties;
    uint32_t nChildren;
};

struct dt_prop_hdr {
    char name[DT_PROP_NAME_LEN];
    uint32_t length;
};

static void b_write(struct apple_dt_builder *b, const void *src, uint32_t len)
{
    if (b->error) {
        return;
    }
    if (b->pos + len > b->capacity) {
        b->error = 1;
        return;
    }
    memcpy(b->base + b->pos, src, len);
    b->pos += len;
}

static void b_pad4(struct apple_dt_builder *b)
{
    static const uint8_t zeros[4] = {0, 0, 0, 0};
    uint32_t aligned = align4(b->pos);
    if (aligned > b->pos) {
        b_write(b, zeros, aligned - b->pos);
    }
}

void apple_dt_begin(struct apple_dt_builder *b, void *buf, uint32_t cap)
{
    b->base = (uint8_t *)buf;
    b->capacity = cap;
    b->pos = 0;
    b->error = 0;
    memset(buf, 0, cap);
}

void apple_dt_node_begin(struct apple_dt_builder *b, uint32_t nprops, uint32_t nchildren)
{
    struct dt_node_hdr h;
    h.nProperties = nprops;
    h.nChildren = nchildren;
    b_write(b, &h, sizeof(h));
}

void apple_dt_prop(struct apple_dt_builder *b, const char *name, const void *value, uint32_t len)
{
    struct dt_prop_hdr h;
    memset(&h, 0, sizeof(h));

    for (uint32_t i = 0; i < DT_PROP_NAME_LEN - 1u && name[i]; i++) {
        h.name[i] = name[i];
    }
    h.length = len;

    b_write(b, &h, sizeof(h));
    b_write(b, value, len);
    b_pad4(b);
}

void apple_dt_prop_str(struct apple_dt_builder *b, const char *name, const char *value)
{
    apple_dt_prop(b, name, value, (uint32_t)strlen(value) + 1u);
}

void apple_dt_prop_u32(struct apple_dt_builder *b, const char *name, uint32_t value)
{
    apple_dt_prop(b, name, &value, sizeof(value));
}

void apple_dt_prop_u32_array(struct apple_dt_builder *b, const char *name, const uint32_t *values, uint32_t count)
{
    apple_dt_prop(b, name, values, count * sizeof(values[0]));
}

uint32_t apple_dt_finish(struct apple_dt_builder *b)
{
    b_pad4(b);
    return b->error ? 0u : b->pos;
}

static const uint8_t *skip_node(const uint8_t *p, const uint8_t *end)
{
    if (p + sizeof(struct dt_node_hdr) > end) {
        return 0;
    }

    const struct dt_node_hdr *n = (const struct dt_node_hdr *)p;
    p += sizeof(*n);

    for (uint32_t i = 0; i < n->nProperties; i++) {
        if (p + sizeof(struct dt_prop_hdr) > end) {
            return 0;
        }
        const struct dt_prop_hdr *ph = (const struct dt_prop_hdr *)p;
        p += sizeof(*ph);
        if (p + ph->length > end) {
            return 0;
        }
        p += align4(ph->length);
    }

    for (uint32_t i = 0; i < n->nChildren; i++) {
        p = skip_node(p, end);
        if (!p) {
            return 0;
        }
    }

    return p;
}

static int node_has_name(const uint8_t *node, const uint8_t *end, const char *name)
{
    if (node + sizeof(struct dt_node_hdr) > end) {
        return 0;
    }

    const struct dt_node_hdr *n = (const struct dt_node_hdr *)node;
    const uint8_t *p = node + sizeof(*n);

    for (uint32_t i = 0; i < n->nProperties; i++) {
        if (p + sizeof(struct dt_prop_hdr) > end) {
            return 0;
        }
        const struct dt_prop_hdr *ph = (const struct dt_prop_hdr *)p;
        p += sizeof(*ph);
        if (p + ph->length > end) {
            return 0;
        }
        if (strcmp(ph->name, "name") == 0 && ph->length > 0) {
            const char *v = (const char *)p;
            if (strcmp(v, name) == 0) {
                return 1;
            }
        }
        p += align4(ph->length);
    }

    return 0;
}

static const uint8_t *find_child_by_name(const uint8_t *node, const uint8_t *end, const char *name)
{
    if (node + sizeof(struct dt_node_hdr) > end) {
        return 0;
    }

    const struct dt_node_hdr *n = (const struct dt_node_hdr *)node;
    const uint8_t *p = node + sizeof(*n);

    for (uint32_t i = 0; i < n->nProperties; i++) {
        if (p + sizeof(struct dt_prop_hdr) > end) {
            return 0;
        }
        const struct dt_prop_hdr *ph = (const struct dt_prop_hdr *)p;
        p += sizeof(*ph) + align4(ph->length);
    }

    for (uint32_t i = 0; i < n->nChildren; i++) {
        const uint8_t *child = p;
        if (node_has_name(child, end, name)) {
            return child;
        }
        p = skip_node(child, end);
        if (!p) {
            return 0;
        }
    }

    return 0;
}

static int expect_child(const uint8_t *root, const uint8_t *end, const char *name)
{
    log_puts("MI4IOS6_STAGE73 find /");
    log_puts(name);
    log_puts(" ... ");
    const uint8_t *child = find_child_by_name(root, end, name);
    if (child) {
        log_puts("ok\n");
        return 1;
    }
    log_puts("missing\n");
    return 0;
}

int apple_dt_selftest_and_log(const void *dt, uint32_t len)
{
    const uint8_t *root = (const uint8_t *)dt;
    const uint8_t *end = root + len;

    log_kv32("apple_dt_len", len);

    if (len < sizeof(struct dt_node_hdr)) {
        log_puts("MI4IOS6_STAGE73 apple_dt too small\n");
        return 0;
    }

    const struct dt_node_hdr *rh = (const struct dt_node_hdr *)root;
    log_kv32("apple_dt_root_props", rh->nProperties);
    log_kv32("apple_dt_root_children", rh->nChildren);

    if (rh->nProperties == 0 || rh->nChildren < 5) {
        log_puts("MI4IOS6_STAGE73 apple_dt root invalid\n");
        return 0;
    }

    const uint8_t *after = skip_node(root, end);
    if (after != end) {
        log_puts("MI4IOS6_STAGE73 apple_dt walk length mismatch\n");
        return 0;
    }

    int ok = 1;
    ok &= expect_child(root, end, "chosen");
    ok &= expect_child(root, end, "memory");
    ok &= expect_child(root, end, "cpus");
    ok &= expect_child(root, end, "msm8974-io");
    ok &= expect_child(root, end, "interrupt-controller");
    ok &= expect_child(root, end, "timer");
    ok &= expect_child(root, end, "iokit-platform-scaffold");
    ok &= expect_child(root, end, "msm8974-platform-driver");
    ok &= expect_child(root, end, "msm8974-interrupt-service");
    ok &= expect_child(root, end, "msm8974-timer-service");
    ok &= expect_child(root, end, "msm8974-cpu-service");
    ok &= expect_child(root, end, "msm8974-rejected-driver");
    ok &= expect_child(root, end, "iokit-catalog-property-dryrun");
    ok &= expect_child(root, end, "stage73-platform-personality");
    ok &= expect_child(root, end, "stage73-interrupt-personality");
    ok &= expect_child(root, end, "stage73-timer-personality");
    ok &= expect_child(root, end, "stage73-cpu-personality");
    ok &= expect_child(root, end, "stage73-rejected-personality");

    if (ok) {
        log_puts("MI4IOS6_STAGE73 apple_dt selftest ok\n");
    } else {
        log_puts("MI4IOS6_STAGE73 apple_dt selftest failed\n");
    }

    return ok;
}

const void *apple_dt_find_child(const void *dt, uint32_t len, const void *node, const char *name)
{
    const uint8_t *root = (const uint8_t *)dt;
    const uint8_t *end = root + len;
    const uint8_t *start = node ? (const uint8_t *)node : root;
    return find_child_by_name(start, end, name);
}

const void *apple_dt_get_prop(const void *dt, uint32_t len, const void *node, const char *name, uint32_t *prop_len)
{
    const uint8_t *root = (const uint8_t *)dt;
    const uint8_t *end = root + len;
    const uint8_t *p = node ? (const uint8_t *)node : root;

    if (prop_len) {
        *prop_len = 0;
    }
    if (p + sizeof(struct dt_node_hdr) > end) {
        return 0;
    }

    const struct dt_node_hdr *n = (const struct dt_node_hdr *)p;
    p += sizeof(*n);

    for (uint32_t i = 0; i < n->nProperties; i++) {
        if (p + sizeof(struct dt_prop_hdr) > end) {
            return 0;
        }
        const struct dt_prop_hdr *ph = (const struct dt_prop_hdr *)p;
        p += sizeof(*ph);
        if (p + ph->length > end) {
            return 0;
        }
        if (strcmp(ph->name, name) == 0) {
            if (prop_len) {
                *prop_len = ph->length;
            }
            return p;
        }
        p += align4(ph->length);
    }

    return 0;
}

uint32_t apple_dt_node_child_count(const void *dt, uint32_t len, const void *node)
{
    const uint8_t *root = (const uint8_t *)dt;
    const uint8_t *end = root + len;
    const uint8_t *p = node ? (const uint8_t *)node : root;
    if (p + sizeof(struct dt_node_hdr) > end) {
        return 0;
    }
    return ((const struct dt_node_hdr *)p)->nChildren;
}

uint32_t apple_dt_node_prop_count(const void *dt, uint32_t len, const void *node)
{
    const uint8_t *root = (const uint8_t *)dt;
    const uint8_t *end = root + len;
    const uint8_t *p = node ? (const uint8_t *)node : root;
    if (p + sizeof(struct dt_node_hdr) > end) {
        return 0;
    }
    return ((const struct dt_node_hdr *)p)->nProperties;
}

uint32_t apple_dt_get_u32_prop(const void *dt, uint32_t len, const void *node, const char *name, uint32_t fallback)
{
    uint32_t prop_len;
    const uint32_t *v = (const uint32_t *)apple_dt_get_prop(dt, len, node, name, &prop_len);
    if (!v || prop_len < sizeof(uint32_t)) {
        return fallback;
    }
    return *v;
}
