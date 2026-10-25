/*************************************************************************
 * Unit tests for thread-safe string functions (PR 1 + PR 2)
 *
 * PR 1: strtok_r / gmtime_r replacements for thread-unsafe strtok / gmtime
 * PR 2: ncclStrerror wrapper replacing thread-unsafe strerror
 *
 * Compile: gcc -Wall -Wextra -g -std=c99 -D_GNU_SOURCE -o test_thread_safety test_thread_safety.c -lpthread
 * Run:     ./test_thread_safety
 *************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>

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
 * Function under test: ncclStrerror (extracted from src/include/checks.h)
 * ========================================================================= */

static inline const char *ncclStrerror(int errnum, char *buf, size_t buflen) {
#if (_POSIX_C_SOURCE >= 200112L) && !defined(_GNU_SOURCE)
  /* POSIX variant: int strerror_r(int, char*, size_t) */
  if (strerror_r(errnum, buf, buflen) != 0)
    snprintf(buf, buflen, "Unknown error %d", errnum);
  return buf;
#else
  /* GNU variant: char* strerror_r(int, char*, size_t) */
  return strerror_r(errnum, buf, buflen);
#endif
}

/* =========================================================================
 * PR 2: ncclStrerror table-driven tests
 * ========================================================================= */

typedef struct {
  const char *name;
  int errnum;
  size_t buflen;
  const char *expected_substring; /* NULL = just check non-empty */
  int expect_null_terminated;     /* 1 if we should check buf is null-termed */
} StrerrorTestCase;

static StrerrorTestCase strerror_cases[] = {
    /* Positive: known errno values */
    {"EINVAL", EINVAL, 256, "nvalid argument", 1},
    {"ENOENT", ENOENT, 256, "o such file", 1},
    {"ENOMEM", ENOMEM, 256, "emory", 1},
    {"EBADF", EBADF, 256, "ad file", 1},
    {"EACCES", EACCES, 256, "ermission", 1},
    {"EAGAIN", EAGAIN, 256, NULL, 1},
    {"EPERM", EPERM, 256, "peration not permitted", 1},
    {"success_zero", 0, 256, NULL, 1},

    /* Negative: unknown/invalid errno values */
    {"unknown_99999", 99999, 256, NULL, 1},
    {"negative_errno", -1, 256, NULL, 1},
    {"INT_MAX_errno", INT_MAX, 256, NULL, 1},
    {"INT_MIN_errno", INT_MIN, 256, NULL, 1},

    /* Boundary: small buffers */
    {"tiny_buffer_4", EINVAL, 4, NULL, 1},
    {"tiny_buffer_1", EINVAL, 1, NULL, 1},

    {NULL, 0, 0, NULL, 0} /* sentinel */
};

int test_strerror_known_values(void) {
  for (int i = 0; strerror_cases[i].name != NULL; i++) {
    StrerrorTestCase *tc = &strerror_cases[i];
    char buf[256];
    memset(buf, 'X', sizeof(buf)); /* fill with sentinel */

    size_t buflen = tc->buflen < sizeof(buf) ? tc->buflen : sizeof(buf);
    const char *result = ncclStrerror(tc->errnum, buf, buflen);

    /* Result must be non-NULL */
    char msg[512];
    snprintf(msg, sizeof(msg), "[%s] ncclStrerror returned NULL", tc->name);
    TEST_ASSERT(result != NULL, msg);

    /* Result must be non-empty for valid errno values */
    if (tc->errnum >= 0 && tc->errnum < 200) {
      snprintf(msg, sizeof(msg), "[%s] result is empty", tc->name);
      TEST_ASSERT(strlen(result) > 0, msg);
    }

    /* Check expected substring if provided */
    if (tc->expected_substring != NULL) {
      snprintf(msg, sizeof(msg), "[%s] expected '%s' in '%s'", tc->name,
               tc->expected_substring, result);
      TEST_ASSERT(strstr(result, tc->expected_substring) != NULL, msg);
    }

    /* Check null termination: the RETURNED string must be null-terminated.
     * With GNU strerror_r, the return value may point to a static string
     * (not buf), so we check the returned pointer, not buf directly. */
    if (tc->expect_null_terminated && buflen > 0) {
      size_t result_len = strlen(result); /* strlen on returned pointer */
      snprintf(msg, sizeof(msg),
               "[%s] returned string not properly null-terminated (len=%zu)",
               tc->name, result_len);
      TEST_ASSERT(result_len < 1024, msg); /* sanity: not reading garbage */

      /* If result points into buf, verify it's within bounds */
      if (result >= buf && result < buf + sizeof(buf)) {
        snprintf(msg, sizeof(msg),
                 "[%s] result in buf but extends past buflen", tc->name);
        TEST_ASSERT(result + result_len < buf + buflen, msg);
      }
    }
  }
  TEST_PASS();
}

/* Zero-buffer test: buflen=0 should not crash */
int test_strerror_zero_buffer(void) {
  char buf[4] = "XYZ";
  /* This is implementation-defined behavior; we just verify no crash */
  const char *result = ncclStrerror(EINVAL, buf, 0);
  /* GNU strerror_r may return a static string even with buflen=0 */
  TEST_ASSERT(result != NULL, "result should not be NULL even with buflen=0");
  TEST_PASS();
}

/* Thread safety test for ncclStrerror */
#define STRERROR_NUM_THREADS 8
#define STRERROR_ITERATIONS 10000

typedef struct {
  int thread_id;
  int errnum;
  int pass;
  char failure_msg[256];
} StrerrorThreadArg;

static int strerror_errnums[] = {EINVAL, ENOENT, ENOMEM, EBADF,
                                  EAGAIN, EPERM,  EACCES, EEXIST};

static void *strerror_thread_func(void *arg) {
  StrerrorThreadArg *ta = (StrerrorThreadArg *)arg;
  ta->pass = 1;
  ta->failure_msg[0] = '\0';

  for (int i = 0; i < STRERROR_ITERATIONS; i++) {
    char buf[256];
    const char *result = ncclStrerror(ta->errnum, buf, sizeof(buf));
    if (result == NULL) {
      snprintf(ta->failure_msg, sizeof(ta->failure_msg),
               "thread %d iter %d: NULL result", ta->thread_id, i);
      ta->pass = 0;
      return NULL;
    }
    if (strlen(result) == 0) {
      snprintf(ta->failure_msg, sizeof(ta->failure_msg),
               "thread %d iter %d: empty result", ta->thread_id, i);
      ta->pass = 0;
      return NULL;
    }
  }
  return NULL;
}

int test_strerror_thread_safety(void) {
  pthread_t threads[STRERROR_NUM_THREADS];
  StrerrorThreadArg args[STRERROR_NUM_THREADS];

  for (int i = 0; i < STRERROR_NUM_THREADS; i++) {
    args[i].thread_id = i;
    args[i].errnum = strerror_errnums[i];
    args[i].pass = 0;
    pthread_create(&threads[i], NULL, strerror_thread_func, &args[i]);
  }

  for (int i = 0; i < STRERROR_NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }

  for (int i = 0; i < STRERROR_NUM_THREADS; i++) {
    char msg[512];
    snprintf(msg, sizeof(msg), "thread %d (errno=%d) failed: %s", i,
             args[i].errnum, args[i].failure_msg);
    TEST_ASSERT(args[i].pass, msg);
  }
  TEST_PASS();
}

/* =========================================================================
 * PR 1: strtok_r table-driven tests
 * ========================================================================= */

#define MAX_TOKENS 16

typedef struct {
  const char *name;
  const char *input;
  size_t input_len;      /* 0 = use strlen, >0 = explicit (for embedded nulls) */
  const char *delimiters;
  const char *expected_tokens[MAX_TOKENS]; /* NULL-terminated list */
  int expected_count;
} TokenizeTestCase;

static TokenizeTestCase tokenize_cases[] = {
    /* Positive: normal tokenization */
    {"pipe_delimited", "INIT|COLL|P2P", 0, "|", {"INIT", "COLL", "P2P", NULL}, 3},
    {"comma_delimited", "A,B,C,D", 0, ",", {"A", "B", "C", "D", NULL}, 4},
    {"single_token", "INIT", 0, ",", {"INIT", NULL}, 1},

    /* Negative: edge cases */
    {"empty_string", "", 0, ",", {NULL}, 0},
    {"trailing_delim", "A,B,", 0, ",", {"A", "B", NULL}, 2},
    {"leading_delim", ",A,B", 0, ",", {"A", "B", NULL}, 2},
    {"consecutive_delim", "A,,B", 0, ",", {"A", "B", NULL}, 2},
    {"all_delimiters", ",,,,", 0, ",", {NULL}, 0},

    /* Security: embedded null (strtok_r stops at \0) */
    {"embedded_null", "A,B\0C,D", 7, ",", {"A", "B", NULL}, 2},

    /* Boundary: multi-char delimiters, high bytes */
    {"multi_char_delim", "A,;B;,C", 0, ",;", {"A", "B", "C", NULL}, 3},
    {"high_bytes", "\xff,\xfe,\x80", 0, ",", {"\xff", "\xfe", "\x80", NULL}, 3},

    {NULL, NULL, 0, NULL, {NULL}, 0} /* sentinel */
};

int test_strtok_r_cases(void) {
  for (int i = 0; tokenize_cases[i].name != NULL; i++) {
    TokenizeTestCase *tc = &tokenize_cases[i];

    /* strtok_r modifies the input, so we must copy it */
    size_t len = tc->input_len > 0 ? tc->input_len : strlen(tc->input);
    char *input = (char *)malloc(len + 1);
    memcpy(input, tc->input, len);
    input[len] = '\0';

    char *saveptr = NULL;
    const char *tokens[MAX_TOKENS];
    int count = 0;

    char *token = strtok_r(input, tc->delimiters, &saveptr);
    while (token != NULL && count < MAX_TOKENS) {
      tokens[count++] = token;
      token = strtok_r(NULL, tc->delimiters, &saveptr);
    }

    char msg[512];
    snprintf(msg, sizeof(msg), "[%s] expected %d tokens, got %d", tc->name,
             tc->expected_count, count);
    TEST_ASSERT(count == tc->expected_count, msg);

    for (int j = 0; j < count; j++) {
      snprintf(msg, sizeof(msg), "[%s] token[%d] expected '%s' got '%s'",
               tc->name, j, tc->expected_tokens[j], tokens[j]);
      TEST_ASSERT(strcmp(tokens[j], tc->expected_tokens[j]) == 0, msg);
    }

    free(input);
  }
  TEST_PASS();
}

/* Very long token test */
int test_strtok_r_very_long_token(void) {
  const int LEN = 4096;
  char *input = (char *)malloc(LEN + 1);
  memset(input, 'A', LEN);
  input[LEN] = '\0';

  char *saveptr = NULL;
  char *token = strtok_r(input, ",", &saveptr);

  TEST_ASSERT(token != NULL, "token should not be NULL");
  TEST_ASSERT((int)strlen(token) == LEN, "token should be full length");
  TEST_ASSERT(strtok_r(NULL, ",", &saveptr) == NULL,
              "should be only one token");

  free(input);
  TEST_PASS();
}

/* Thread safety: each thread tokenizes its own string */
#define TOKENIZE_NUM_THREADS 8
#define TOKENIZE_ITERATIONS 1000

typedef struct {
  int thread_id;
  int pass;
  char failure_msg[256];
} TokenizeThreadArg;

static void *tokenize_thread_func(void *arg) {
  TokenizeThreadArg *ta = (TokenizeThreadArg *)arg;
  ta->pass = 1;
  ta->failure_msg[0] = '\0';

  for (int i = 0; i < TOKENIZE_ITERATIONS; i++) {
    char input[64];
    /* Each thread creates a unique string to verify no cross-contamination */
    snprintf(input, sizeof(input), "T%d_A,T%d_B,T%d_C", ta->thread_id,
             ta->thread_id, ta->thread_id);

    char *saveptr = NULL;
    char *t1 = strtok_r(input, ",", &saveptr);
    char *t2 = strtok_r(NULL, ",", &saveptr);
    char *t3 = strtok_r(NULL, ",", &saveptr);
    char *t4 = strtok_r(NULL, ",", &saveptr);

    if (t1 == NULL || t2 == NULL || t3 == NULL || t4 != NULL) {
      snprintf(ta->failure_msg, sizeof(ta->failure_msg),
               "thread %d iter %d: wrong token count", ta->thread_id, i);
      ta->pass = 0;
      return NULL;
    }

    char expected[32];
    snprintf(expected, sizeof(expected), "T%d_A", ta->thread_id);
    if (strcmp(t1, expected) != 0) {
      snprintf(ta->failure_msg, sizeof(ta->failure_msg),
               "thread %d iter %d: t1='%s' expected '%s'", ta->thread_id, i,
               t1, expected);
      ta->pass = 0;
      return NULL;
    }
  }
  return NULL;
}

int test_strtok_r_thread_safety(void) {
  pthread_t threads[TOKENIZE_NUM_THREADS];
  TokenizeThreadArg args[TOKENIZE_NUM_THREADS];

  for (int i = 0; i < TOKENIZE_NUM_THREADS; i++) {
    args[i].thread_id = i;
    args[i].pass = 0;
    pthread_create(&threads[i], NULL, tokenize_thread_func, &args[i]);
  }

  for (int i = 0; i < TOKENIZE_NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }

  for (int i = 0; i < TOKENIZE_NUM_THREADS; i++) {
    char msg[512];
    snprintf(msg, sizeof(msg), "thread %d failed: %s", i, args[i].failure_msg);
    TEST_ASSERT(args[i].pass, msg);
  }
  TEST_PASS();
}

/* =========================================================================
 * PR 1: gmtime_r table-driven tests
 * ========================================================================= */

typedef struct {
  const char *name;
  time_t timestamp;
  int expected_year;  /* tm_year + 1900 */
  int expected_month; /* tm_mon + 1 */
  int expected_day;   /* tm_mday */
} GmtimeTestCase;

static GmtimeTestCase gmtime_cases[] = {
    /* Positive: known timestamps */
    {"epoch", 0, 1970, 1, 1},
    {"y2k", 946684800, 2000, 1, 1},
    {"recent", 1700000000, 2023, 11, 14},

    /* Boundary: negative timestamp (before epoch) */
    {"before_epoch", -1, 1969, 12, 31},

    /* Boundary: Y2038 (32-bit time_t overflow) */
    {"y2038_boundary", 2147483647, 2038, 1, 19},

    /* Boundary: post-Y2038 (requires 64-bit time_t) */
    {"post_y2038", 4102444800L, 2100, 1, 1},

    {NULL, 0, 0, 0, 0} /* sentinel */
};

int test_gmtime_r_cases(void) {
  for (int i = 0; gmtime_cases[i].name != NULL; i++) {
    GmtimeTestCase *tc = &gmtime_cases[i];

    /* Skip post-Y2038 test on 32-bit systems */
    if (sizeof(time_t) <= 4 && tc->timestamp > 2147483647) {
      printf("  SKIP: [%s] (32-bit time_t)\n", tc->name);
      continue;
    }

    struct tm result;
    struct tm *ret = gmtime_r(&tc->timestamp, &result);

    char msg[512];
    snprintf(msg, sizeof(msg), "[%s] gmtime_r returned NULL", tc->name);
    TEST_ASSERT(ret != NULL, msg);
    TEST_ASSERT(ret == &result, "gmtime_r should return pointer to result buf");

    int year = result.tm_year + 1900;
    int month = result.tm_mon + 1;
    int day = result.tm_mday;

    snprintf(msg, sizeof(msg), "[%s] expected %d/%d/%d got %d/%d/%d", tc->name,
             tc->expected_year, tc->expected_month, tc->expected_day, year,
             month, day);
    TEST_ASSERT(year == tc->expected_year && month == tc->expected_month &&
                    day == tc->expected_day,
                msg);
  }
  TEST_PASS();
}

/* Thread safety: concurrent gmtime_r calls */
#define GMTIME_NUM_THREADS 8
#define GMTIME_ITERATIONS 10000

typedef struct {
  int thread_id;
  time_t timestamp;
  int expected_year;
  int pass;
  char failure_msg[256];
} GmtimeThreadArg;

static void *gmtime_thread_func(void *arg) {
  GmtimeThreadArg *ta = (GmtimeThreadArg *)arg;
  ta->pass = 1;
  ta->failure_msg[0] = '\0';

  for (int i = 0; i < GMTIME_ITERATIONS; i++) {
    struct tm result;
    struct tm *ret = gmtime_r(&ta->timestamp, &result);
    if (ret == NULL) {
      snprintf(ta->failure_msg, sizeof(ta->failure_msg),
               "thread %d iter %d: gmtime_r returned NULL", ta->thread_id, i);
      ta->pass = 0;
      return NULL;
    }
    int year = result.tm_year + 1900;
    if (year != ta->expected_year) {
      snprintf(ta->failure_msg, sizeof(ta->failure_msg),
               "thread %d iter %d: expected year %d got %d (cross-thread "
               "contamination!)",
               ta->thread_id, i, ta->expected_year, year);
      ta->pass = 0;
      return NULL;
    }
  }
  return NULL;
}

int test_gmtime_r_thread_safety(void) {
  pthread_t threads[GMTIME_NUM_THREADS];
  GmtimeThreadArg args[GMTIME_NUM_THREADS];

  /* Each thread gets a different timestamp with a different year */
  time_t timestamps[] = {0,          946684800,  1000000000, 1100000000,
                         1200000000, 1300000000, 1400000000, 1500000000};
  int years[] = {1970, 2000, 2001, 2004, 2008, 2011, 2014, 2017};

  for (int i = 0; i < GMTIME_NUM_THREADS; i++) {
    args[i].thread_id = i;
    args[i].timestamp = timestamps[i];
    args[i].expected_year = years[i];
    args[i].pass = 0;
    pthread_create(&threads[i], NULL, gmtime_thread_func, &args[i]);
  }

  for (int i = 0; i < GMTIME_NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }

  for (int i = 0; i < GMTIME_NUM_THREADS; i++) {
    char msg[512];
    snprintf(msg, sizeof(msg), "thread %d (year=%d) failed: %s", i,
             args[i].expected_year, args[i].failure_msg);
    TEST_ASSERT(args[i].pass, msg);
  }
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
    {"strerror-values", test_strerror_known_values,
     "ncclStrerror with known/unknown/boundary errno values"},
    {"strerror-zero-buf", test_strerror_zero_buffer,
     "ncclStrerror with zero-length buffer"},
    {"strerror-threads", test_strerror_thread_safety,
     "ncclStrerror concurrent thread safety"},
    {"strtok-cases", test_strtok_r_cases,
     "strtok_r tokenization (positive/negative/security)"},
    {"strtok-long", test_strtok_r_very_long_token,
     "strtok_r with 4096-byte token"},
    {"strtok-threads", test_strtok_r_thread_safety,
     "strtok_r concurrent thread safety"},
    {"gmtime-cases", test_gmtime_r_cases,
     "gmtime_r known timestamps and boundaries"},
    {"gmtime-threads", test_gmtime_r_thread_safety,
     "gmtime_r concurrent thread safety"},
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

  printf("Thread Safety Unit Tests (PR 1 + PR 2)\n");
  printf("=======================================\n");

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

  printf("\n=======================================\n");
  printf("Results: %d/%d tests passed\n", passed, total);

  if (passed == total) {
    printf("All tests PASSED!\n");
    return 0;
  } else {
    printf("Some tests FAILED!\n");
    return 1;
  }
}
