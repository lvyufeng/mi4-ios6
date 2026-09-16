/*
 * Test: does apple_dt_selftest_and_log actually catch a corrupted node header?
 *
 * The claim has been made repeatedly in this project's commits and docs - that a wrong
 * nProperties "does not fail to build and does not fail at DTInit; the walker reads the
 * number it was given and lands in the middle of the next property name", and that
 * apple_dt_selftest_and_log catches it on hardware. It is load-bearing: it is the reason a
 * device-tree edit is considered safe without a hardware run to check it.
 *
 * So it gets tested rather than asserted, and it can be tested on the host using the same
 * extracted builder, the same apple_dt.c, and the payload's own finder.
 *
 * Method: build the real tree, locate a node with the payload's own apple_dt_find_child,
 * corrupt exactly one header field, and ask the payload's own selftest whether it notices.
 * Every case also asserts the *unmodified* tree passes first, so a false "detected" from a
 * broken test cannot be mistaken for coverage.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stage90_dt_shim.h"

/* From apple_dt.c. */
int apple_dt_selftest_and_log(const void *dt, uint32_t len);
const void *apple_dt_find_child(const void *dt, uint32_t len, const void *node,
                                const char *name);

uint8_t g_apple_dt[32768] __attribute__((aligned(4)));

static int loud;
void log_puts(const char *s) { if (loud) fputs(s, stdout); }
void log_kv32(const char *key, uint32_t value) { if (loud) printf("%s=0x%08x\n", key, value); }

/* apple_dt.c's node header: two uint32s ahead of the properties. */
struct node_hdr { uint32_t nProperties; uint32_t nChildren; };

static int cases_run, cases_invalid, cases_undetected, cases_detected;

static void report(const char *node, const char *field, uint32_t value, int delta,
                   int detected)
{
    printf("  %-22s %-11s %2u%+d   %s\n", node, field, value, delta,
           detected ? "detected" : "*** NOT DETECTED ***");
}

static void try_corruption(const char *which, uint32_t delta, int props)
{
    struct apple_dt_builder b;
    uint32_t len, saved;
    struct node_hdr *h;

    apple_dt_begin(&b, g_apple_dt, sizeof(g_apple_dt));
    build_stage90_apple_dt(&b);
    len = apple_dt_finish(&b);
    if (!len) { printf("  builder produced nothing\n"); cases_invalid++; return; }

    /* Locate the node with the payload's own finder, not a re-implementation. */
    h = (struct node_hdr *)apple_dt_find_child(g_apple_dt, len, NULL, which);
    if (!h) { printf("  %-22s not found in the tree\n", which); cases_invalid++; return; }

    /* Baseline: the unmodified tree must pass, or this case proves nothing. */
    loud = 0;
    int base = apple_dt_selftest_and_log(g_apple_dt, len);
    loud = 1;
    if (!base) {
        printf("  %-22s baseline selftest FAILED - case invalid\n", which);
        cases_invalid++;
        return;
    }

    saved = props ? h->nProperties : h->nChildren;
    if (props) h->nProperties = (uint32_t)((int32_t)saved + (int32_t)delta);
    else       h->nChildren   = (uint32_t)((int32_t)saved + (int32_t)delta);

    loud = 0;
    int detected = !apple_dt_selftest_and_log(g_apple_dt, len);
    loud = 1;

    if (props) h->nProperties = saved;
    else       h->nChildren = saved;

    cases_run++;
    if (detected) cases_detected++; else cases_undetected++;
    report(which, props ? "nProperties" : "nChildren", saved, delta, detected);
}

static void try_root_corruption(void)
{
    struct apple_dt_builder b;
    uint32_t len, saved;
    struct node_hdr *root;

    apple_dt_begin(&b, g_apple_dt, sizeof(g_apple_dt));
    build_stage90_apple_dt(&b);
    len = apple_dt_finish(&b);
    root = (struct node_hdr *)g_apple_dt;

    loud = 0;
    int base = apple_dt_selftest_and_log(g_apple_dt, len);
    loud = 1;
    if (!base) {
        printf("  %-22s baseline selftest FAILED - case invalid\n", "/");
        cases_invalid++;
        return;
    }

    saved = root->nChildren;
    root->nChildren = saved + 1u;
    loud = 0;
    int detected = !apple_dt_selftest_and_log(g_apple_dt, len);
    loud = 1;
    root->nChildren = saved;

    cases_run++;
    if (detected) cases_detected++; else cases_undetected++;
    report("/", "nChildren", saved, +1, detected);
}

int main(void)
{
    loud = 1;
    printf("does apple_dt_selftest_and_log catch a corrupted node header?\n\n");
    printf("  %-22s %-11s %-6s %s\n", "node", "field", "value", "result");

    /* Nodes with properties and no children - the common case. */
    try_corruption("arm-io", +1, 1);
    try_corruption("arm-io", -1, 1);
    try_corruption("timer",  +1, 1);
    try_corruption("timer",  -1, 1);
    try_corruption("chosen", +1, 1);
    try_corruption("memory", +1, 1);
    try_corruption("cpus",   +1, 1);

    /* A node that HAS children: /cpus has four cpu@N. */
    try_corruption("cpus",   +1, 0);
    try_corruption("cpus",   -1, 0);

    /* The root has no parent to find it by, so corrupt it directly. */
    try_root_corruption();

    printf("\n%d case(s) run, %d detected, %d undetected, %d invalid\n",
           cases_run, cases_detected, cases_undetected, cases_invalid);

    if (cases_invalid) {
        printf("RESULT: inconclusive - %d case(s) could not be set up.\n", cases_invalid);
        return 2;
    }
    if (cases_undetected) {
        printf("RESULT: %d corruption(s) went UNDETECTED by the payload's own selftest.\n",
               cases_undetected);
        return 1;
    }
    printf("RESULT: every single-field corruption was detected.\n");
    return 0;
}
