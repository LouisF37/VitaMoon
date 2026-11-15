/*
 * Minimal test helpers for the PC-side tests (tools/tests/run.sh).
 */
#ifndef TOOLS_TESTS_TEST_H
#define TOOLS_TESTS_TEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int test_failures = 0;
static int test_checks = 0;
static const char *test_name = "";

#define TEST_BEGIN(name) (test_name = (name), printf("[%s]\n", test_name))

#define CHECK(cond, what)                                                      \
  do {                                                                         \
    test_checks++;                                                             \
    if (cond) {                                                                \
      printf("  ok    %s\n", what);                                            \
    } else {                                                                   \
      test_failures++;                                                         \
      printf("  FAIL  %s  (%s:%d: %s)\n", what, __FILE__, __LINE__, #cond);    \
    }                                                                          \
  } while (0)

#define TEST_END()                                                             \
  (printf("[%s] %d checks, %d failed\n\n", test_name, test_checks, test_failures), \
   test_failures ? 1 : 0)

#endif
