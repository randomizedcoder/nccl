/*************************************************************************
 * Unit tests for buffer overflow prevention (snprintf/strncpy)
 *
 * Tests verify that:
 *   1. snprintf correctly truncates and null-terminates (table-driven)
 *   2. strncpy behavior with and without manual null-termination
 *   3. Return value overflow and incremental fill patterns
 *   4. Source files have been patched to use snprintf/strncpy
 *
 * The source verification test (test_source_verified) intentionally FAILS
 * before the fix is applied, demonstrating the bug exists in unfixed code.
 *
 * Compile: gcc -Wall -Wextra -Wno-format-truncation -g -std=c99 -D_GNU_SOURCE -o test_buffer_overflow test_buffer_overflow.c -lpthread
 * Run:     ./test_buffer_overflow
 *************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* =========================================================================
 * Test framework (matches plugins/tuner/example/test/ pattern)
 * ========================================================================= */

#define TEST_ASSERT(condition, message)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
      printf("  FAIL: %s - %s\n", __func__, message);                         \
      return 0;                                                                \
    }                                                                          \
  } while (0)

#define TEST_PASS()                                                            \
  do {                                                                         \
    printf("  PASS: %s\n", __func__);                                          \
    return 1;                                                                  \
  } while (0)

/* =========================================================================
 * Source verification: check NCCL source for snprintf/strncpy
 * ========================================================================= */

static char *read_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  if (len <= 0) { fclose(f); return NULL; }
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc(len + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t n = fread(buf, 1, len, f);
  buf[n] = '\0';
  fclose(f);
  return buf;
}

int test_source_verified(void) {
  int all_ok = 1;
  char msg[512];

  /* Check 1: debug.cc should use strncpy (not strcpy) */
  const char *debug_path = "../../src/debug.cc";
  char *debug_src = read_file(debug_path);
  if (!debug_src) {
    snprintf(msg, sizeof(msg), "Cannot read %s (run from tests/c/)", debug_path);
    printf("  SKIP: %s - %s\n", __func__, msg);
  } else {
    if (strstr(debug_src, "strncpy") == NULL) {
      snprintf(msg, sizeof(msg),
               "%s: no strncpy found — still uses strcpy (buffer overflow risk)",
               debug_path);
      printf("  FAIL: %s - %s\n", __func__, msg);
      all_ok = 0;
    }
    free(debug_src);
  }

  /* Check 2: socket.cc should not use raw sprintf */
  const char *socket_path = "../../src/misc/socket.cc";
  char *socket_src = read_file(socket_path);
  if (!socket_src) {
    snprintf(msg, sizeof(msg), "Cannot read %s (run from tests/c/)", socket_path);
    printf("  SKIP: %s - %s\n", __func__, msg);
  } else {
    /* Look for sprintf( but not snprintf( */
    char *p = socket_src;
    int found_sprintf = 0;
    while ((p = strstr(p, "sprintf(")) != NULL) {
      /* Check it's not snprintf by looking at preceding char */
      if (p == socket_src || *(p - 1) != 'n') {
        found_sprintf = 1;
        break;
      }
      p++;
    }
    if (found_sprintf) {
      snprintf(msg, sizeof(msg),
               "%s: still uses sprintf() (buffer overflow risk) — expected snprintf",
               socket_path);
      printf("  FAIL: %s - %s\n", __func__, msg);
      all_ok = 0;
    }
    free(socket_src);
  }

  if (all_ok) {
    TEST_PASS();
  }
  return 0;
}

/* =========================================================================
 * snprintf table-driven tests
 * ========================================================================= */

typedef struct {
  const char *name;
  size_t bufsize;
  const char *format;
  const char *arg;
  int expect_truncation;
  size_t expected_max_strlen;
} SnprintfTestCase;

static SnprintfTestCase snprintf_cases[] = {
    /* Positive: exact fit and room to spare */
    {"exact_fit", 12, "Hello %s", "World", 0, 11},
    {"large_buffer", 256, "Hello %s", "World", 0, 11},
    {"empty_arg", 256, "%s", "", 0, 0},
    {"no_arg", 256, "Hello World", NULL, 0, 11},

    /* Negative: truncation */
    {"off_by_one", 11, "Hello %s", "World", 1, 10},
    {"truncated_6", 6, "Hello %s", "World", 1, 5},
    {"size_1", 1, "Hello %s", "World", 1, 0},

    /* Boundary: format string in arg (should be literal, not interpreted) */
    {"format_in_arg", 256, "%s", "%n%n%n%x%x", 0, 10},

    {NULL, 0, NULL, NULL, 0, 0} /* sentinel */
};

int test_snprintf_boundaries(void) {
  for (int i = 0; snprintf_cases[i].name != NULL; i++) {
    SnprintfTestCase *tc = &snprintf_cases[i];
    char *buf = (char *)malloc(tc->bufsize + 16);
    memset(buf, 'X', tc->bufsize + 16);

    int ret;
    if (tc->arg != NULL) {
      ret = snprintf(buf, tc->bufsize, tc->format, tc->arg);
    } else {
      ret = snprintf(buf, tc->bufsize, "%s", tc->format);
    }

    char msg[512];

    snprintf(msg, sizeof(msg), "[%s] snprintf returned %d (negative = error)",
             tc->name, ret);
    TEST_ASSERT(ret >= 0, msg);

    if (tc->bufsize > 0) {
      int found_null = 0;
      for (size_t j = 0; j < tc->bufsize; j++) {
        if (buf[j] == '\0') {
          found_null = 1;
          break;
        }
      }
      snprintf(msg, sizeof(msg), "[%s] buffer not null-terminated", tc->name);
      TEST_ASSERT(found_null, msg);

      size_t len = strlen(buf);
      snprintf(msg, sizeof(msg), "[%s] strlen=%zu > expected_max=%zu", tc->name,
               len, tc->expected_max_strlen);
      TEST_ASSERT(len <= tc->expected_max_strlen, msg);
    }

    snprintf(msg, sizeof(msg), "[%s] buffer overrun detected", tc->name);
    TEST_ASSERT(buf[tc->bufsize] == 'X', msg);

    free(buf);
  }
  TEST_PASS();
}

int test_snprintf_return_overflow(void) {
  char buf[8];
  int ret = snprintf(buf, sizeof(buf), "Hello World");

  TEST_ASSERT(ret == 11, "snprintf should return would-be length (11)");
  TEST_ASSERT(strlen(buf) == 7, "output should be truncated to 7 chars");
  TEST_ASSERT(buf[7] == '\0', "must be null-terminated at position 7");
  TEST_PASS();
}

int test_snprintf_incremental_fill(void) {
  char buf[32];
  int pos = 0;

  pos += snprintf(buf + pos, sizeof(buf) - pos, "Event1");
  pos += snprintf(buf + pos, sizeof(buf) - pos, ",Event2");
  pos += snprintf(buf + pos, sizeof(buf) - pos, ",Event3");

  TEST_ASSERT(strcmp(buf, "Event1,Event2,Event3") == 0,
              "incremental fill should produce correct output");

  pos = 0;
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 20; i++) {
    int written = snprintf(buf + pos, sizeof(buf) - pos, "E%d,", i);
    if (pos + written >= (int)sizeof(buf)) {
      pos = sizeof(buf);
      break;
    }
    pos += written;
  }

  TEST_ASSERT(buf[sizeof(buf) - 1] == '\0',
              "incremental fill overflow should still null-terminate");
  TEST_PASS();
}

int test_snprintf_pos_underflow_guard(void) {
  char buf[16];
  size_t pos = 0;

  pos += snprintf(buf + pos, sizeof(buf) - pos, "ABCDEFGHIJ");
  TEST_ASSERT(pos == 10, "first write should be 10 chars");

  int ret = snprintf(buf + pos, sizeof(buf) - pos, "KLMNOPQRSTUVWXYZ");
  TEST_ASSERT(ret == 16, "return value should be 16 (would-be length)");
  TEST_ASSERT(buf[sizeof(buf) - 1] == '\0', "buffer must be null-terminated");

  size_t safe_pos = pos + ret;
  if (safe_pos >= sizeof(buf))
    safe_pos = sizeof(buf) - 1;
  TEST_ASSERT(safe_pos == 15, "clamped pos should be 15");
  TEST_PASS();
}

int test_snprintf_bus_id_format(void) {
  char busId[13];

  int64_t id = ((int64_t)0x3b << 12);
  int ret =
      snprintf(busId, sizeof(busId), "%04lx:%02lx:%02lx.%01lx",
               (unsigned long)(id >> 20),
               (unsigned long)((id & 0xff000) >> 12),
               (unsigned long)((id & 0xff0) >> 4),
               (unsigned long)(id & 0xf));

  TEST_ASSERT(ret > 0, "snprintf should succeed");
  TEST_ASSERT(strcmp(busId, "0000:3b:00.0") == 0,
              "bus ID should be '0000:3b:00.0'");
  TEST_ASSERT(ret < (int)sizeof(busId), "should fit in 13-byte buffer");
  TEST_PASS();
}

/* =========================================================================
 * strncpy table-driven tests
 * ========================================================================= */

typedef struct {
  const char *name;
  const char *src;
  size_t src_len;
  size_t n;
  int raw_null_terminated;
  int safe_null_terminated;
  size_t expected_safe_strlen;
} StrncpyTestCase;

static StrncpyTestCase strncpy_cases[] = {
    {"fits_with_room", "abc", 0, 10, 1, 1, 3},
    {"empty_src", "", 0, 10, 1, 1, 0},
    {"exact_no_null", "abcd", 0, 4, 0, 1, 3},
    {"truncated", "abcdef", 0, 4, 0, 1, 3},
    {"size_1", "abc", 0, 1, 0, 1, 0},
    {"embedded_null", "ab\0cd", 5, 10, 1, 1, 2},
    {"high_bytes", "\xff\xfe\xfd", 0, 10, 1, 1, 3},
    {NULL, NULL, 0, 0, 0, 0, 0}
};

int test_strncpy_null_termination(void) {
  for (int i = 0; strncpy_cases[i].name != NULL; i++) {
    StrncpyTestCase *tc = &strncpy_cases[i];

    char *dst = (char *)malloc(tc->n + 16);
    memset(dst, 'X', tc->n + 16);

    strncpy(dst, tc->src, tc->n);

    char msg[512];

    int raw_has_null = 0;
    for (size_t j = 0; j < tc->n; j++) {
      if (dst[j] == '\0') {
        raw_has_null = 1;
        break;
      }
    }
    snprintf(msg, sizeof(msg),
             "[%s] raw strncpy null-terminated=%d expected=%d", tc->name,
             raw_has_null, tc->raw_null_terminated);
    TEST_ASSERT(raw_has_null == tc->raw_null_terminated, msg);

    dst[tc->n - 1] = '\0';

    size_t safe_len = strlen(dst);
    snprintf(msg, sizeof(msg), "[%s] safe strlen=%zu expected=%zu", tc->name,
             safe_len, tc->expected_safe_strlen);
    TEST_ASSERT(safe_len == tc->expected_safe_strlen, msg);

    snprintf(msg, sizeof(msg), "[%s] buffer overrun detected", tc->name);
    TEST_ASSERT(dst[tc->n] == 'X', msg);

    if (strcmp(tc->name, "high_bytes") == 0) {
      TEST_ASSERT((unsigned char)dst[0] == 0xff, "high byte 0xff preserved");
      TEST_ASSERT((unsigned char)dst[1] == 0xfe, "high byte 0xfe preserved");
      TEST_ASSERT((unsigned char)dst[2] == 0xfd, "high byte 0xfd preserved");
    }

    free(dst);
  }
  TEST_PASS();
}

int test_strncpy_socket_path_boundary(void) {
  const size_t SUN_PATH_LEN = 108;
  char dst[108];
  char src[120];

  memset(src, 'A', 107);
  src[107] = '\0';
  strncpy(dst, src, SUN_PATH_LEN);
  dst[SUN_PATH_LEN - 1] = '\0';
  TEST_ASSERT(strlen(dst) == 107, "107-char path should fit in sun_path");

  memset(src, 'B', 108);
  src[108] = '\0';
  memset(dst, 'X', SUN_PATH_LEN);
  strncpy(dst, src, SUN_PATH_LEN);
  int has_null = 0;
  for (size_t i = 0; i < SUN_PATH_LEN; i++) {
    if (dst[i] == '\0') {
      has_null = 1;
      break;
    }
  }
  TEST_ASSERT(!has_null, "108-char path in 108-byte buf: no null-termination");

  dst[SUN_PATH_LEN - 1] = '\0';
  TEST_ASSERT(strlen(dst) == 107, "after safe null-term, truncated to 107");
  TEST_PASS();
}

int test_strncpy_max_str_len_boundary(void) {
  const size_t MAX_STR_LEN = 256;
  char dst[256 + 1];
  char src[300];

  memset(src, 'A', MAX_STR_LEN);
  src[MAX_STR_LEN] = '\0';
  strncpy(dst, src, MAX_STR_LEN + 1);
  dst[MAX_STR_LEN] = '\0';
  TEST_ASSERT(strlen(dst) == MAX_STR_LEN, "MAX_STR_LEN string fits exactly");

  memset(src, 'B', 299);
  src[299] = '\0';
  strncpy(dst, src, MAX_STR_LEN + 1);
  dst[MAX_STR_LEN] = '\0';
  TEST_ASSERT(strlen(dst) == MAX_STR_LEN,
              "over-long string truncated to MAX_STR_LEN");
  TEST_PASS();
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

typedef int (*TestFunction)(void);

typedef struct {
  const char *name;
  TestFunction func;
  const char *description;
} TestCase;

static TestCase test_cases[] = {
    {"source-verified", test_source_verified,
     "Verify NCCL source uses snprintf/strncpy (FAILS before fix)"},
    {"snprintf-bounds", test_snprintf_boundaries,
     "snprintf truncation and null-termination"},
    {"snprintf-retval", test_snprintf_return_overflow,
     "snprintf return value when output exceeds buffer"},
    {"snprintf-incr", test_snprintf_incremental_fill,
     "snprintf incremental buffer fill pattern"},
    {"snprintf-underflow", test_snprintf_pos_underflow_guard,
     "snprintf pos underflow guard"},
    {"snprintf-busid", test_snprintf_bus_id_format,
     "snprintf PCI bus ID format (utils.cc)"},
    {"strncpy-null", test_strncpy_null_termination,
     "strncpy null-termination (positive/negative/security)"},
    {"strncpy-socket", test_strncpy_socket_path_boundary,
     "strncpy socket path boundary (108 bytes)"},
    {"strncpy-maxstr", test_strncpy_max_str_len_boundary,
     "strncpy MAX_STR_LEN boundary (xml.h)"},
    {NULL, NULL, NULL}};

static void show_help(const char *prog) {
  printf("Usage: %s [test-name ...]\n\n", prog);
  printf("Available tests:\n");
  for (int i = 0; test_cases[i].name != NULL; i++) {
    printf("  %-25s %s\n", test_cases[i].name, test_cases[i].description);
  }
}

static TestFunction find_test(const char *name) {
  for (int i = 0; test_cases[i].name != NULL; i++) {
    if (strcmp(test_cases[i].name, name) == 0)
      return test_cases[i].func;
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc > 1 &&
      (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    show_help(argv[0]);
    return 0;
  }

  printf("Buffer Overflow Prevention Tests\n");
  printf("=================================\n");

  int passed = 0, total = 0;

  if (argc == 1) {
    for (int i = 0; test_cases[i].name != NULL; i++) {
      total++;
      passed += test_cases[i].func();
    }
  } else {
    for (int arg = 1; arg < argc; arg++) {
      TestFunction func = find_test(argv[arg]);
      if (func) {
        total++;
        passed += func();
      } else {
        printf("ERROR: Unknown test '%s'\n", argv[arg]);
        show_help(argv[0]);
        return 1;
      }
    }
  }

  printf("\n=================================\n");
  printf("Results: %d/%d tests passed\n", passed, total);

  if (passed == total) {
    printf("All tests PASSED!\n");
    return 0;
  } else {
    printf("Some tests FAILED!\n");
    return 1;
  }
}
