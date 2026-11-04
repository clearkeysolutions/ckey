#ifndef CKEY_TESTS_CHECK_H
#define CKEY_TESTS_CHECK_H

#include <cstdio>
#include <cstdlib>

/* Always-on test assertion. Unlike assert(), it is not compiled out by
 * NDEBUG, so Release CI builds execute every check. */
#define CKEY_CHECK(condition)                                                            \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

#endif
