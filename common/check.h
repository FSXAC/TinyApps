/*
 * Minimal test harness: CHECK records failures and keeps going;
 * finish with `return check_report();` from main.
 */
#ifndef CHECK_H
#define CHECK_H

#include <math.h>
#include <stdio.h>

static int check_failures;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);  \
            check_failures++;                                                          \
        }                                                                              \
    } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) < (eps))

static inline int check_report(void) {
    if (check_failures) {
        fprintf(stderr, "%d check(s) failed\n", check_failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}

#endif
