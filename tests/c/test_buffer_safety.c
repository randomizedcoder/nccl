/*************************************************************************
 * Unit tests for buffer safety functions (PR 3 + PR 4)
 *
 * PR 3: sprintf->snprintf, strcpy->strncpy with null-termination
 * PR 4: CUDA error handling macros (tested with stubs, no GPU needed)
 *
 * Compile: gcc -Wall -Wextra -g -std=c99 -D_GNU_SOURCE -o test_buffer_safety test_buffer_safety.c -lpthread
 * Run:     ./test_buffer_safety
 *************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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
 * PR 3: snprintf table-driven tests
 * ========================================================================= */

typedef struct {
  const char *name;
  size_t bufsize;
  const char *format;
  const char *arg;       /* string arg for %s format, NULL if no arg */
  int expect_truncation; /* 1 if output should be truncated */
  size_t expected_max_strlen; /* result strlen should be <= this */
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
    char *buf = (char *)malloc(tc->bufsize + 16); /* extra for overrun detection */
    memset(buf, 'X', tc->bufsize + 16);

    int ret;
    if (tc->arg != NULL) {
      ret = snprintf(buf, tc->bufsize, tc->format, tc->arg);
    } else {
      ret = snprintf(buf, tc->bufsize, "%s", tc->format);
    }

    char msg[512];

    /* snprintf return value is the number that WOULD have been written */
    snprintf(msg, sizeof(msg), "[%s] snprintf returned %d (negative = error)",
             tc->name, ret);
    TEST_ASSERT(ret >= 0, msg);

    /* Buffer must be null-terminated when bufsize > 0 */
    if (tc->bufsize > 0) {
      snprintf(msg, sizeof(msg),
               "[%s] buffer not null-terminated (buf[%zu-1]=%d)", tc->name,
               tc->bufsize, (int)buf[tc->bufsize - 1]);
      int found_null = 0;
      for (size_t j = 0; j < tc->bufsize; j++) {
        if (buf[j] == '\0') {
          found_null = 1;
          break;
        }
      }
      TEST_ASSERT(found_null, msg);

      /* Check strlen <= expected max */
      size_t len = strlen(buf);
      snprintf(msg, sizeof(msg), "[%s] strlen=%zu > expected_max=%zu", tc->name,
               len, tc->expected_max_strlen);
      TEST_ASSERT(len <= tc->expected_max_strlen, msg);
    }

    /* Verify no overrun past buffer */
    snprintf(msg, sizeof(msg), "[%s] buffer overrun detected", tc->name);
    TEST_ASSERT(buf[tc->bufsize] == 'X', msg);

    free(buf);
  }
  TEST_PASS();
}

/* Test snprintf return value when output would be larger than buffer */
int test_snprintf_return_overflow(void) {
  char buf[8];
  /* "Hello World" = 11 chars, buffer = 8 */
  int ret = snprintf(buf, sizeof(buf), "Hello World");

  TEST_ASSERT(ret == 11, "snprintf should return would-be length (11)");
  TEST_ASSERT(strlen(buf) == 7, "output should be truncated to 7 chars");
  TEST_ASSERT(buf[7] == '\0', "must be null-terminated at position 7");
  TEST_PASS();
}

/* Test the pos += snprintf(..., sizeof(buf)-pos, ...) pattern from profiler.cc */
int test_snprintf_incremental_fill(void) {
  char buf[32];
  int pos = 0;

  /* Simulate the profiler pattern: pos += snprintf(buf + pos, sizeof(buf) - pos, ...) */
  pos += snprintf(buf + pos, sizeof(buf) - pos, "Event1");
  pos += snprintf(buf + pos, sizeof(buf) - pos, ",Event2");
  pos += snprintf(buf + pos, sizeof(buf) - pos, ",Event3");

  TEST_ASSERT(strcmp(buf, "Event1,Event2,Event3") == 0,
              "incremental fill should produce correct output");

  /* Now overflow it */
  pos = 0;
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 20; i++) {
    int written = snprintf(buf + pos, sizeof(buf) - pos, "E%d,", i);
    if (pos + written >= (int)sizeof(buf)) {
      /* Clamp pos to prevent underflow in sizeof(buf) - pos */
      pos = sizeof(buf);
      break;
    }
    pos += written;
  }

  /* Buffer should still be null-terminated */
  TEST_ASSERT(buf[sizeof(buf) - 1] == '\0',
              "incremental fill overflow should still null-terminate");
  TEST_PASS();
}

/* Test the pos underflow scenario: sizeof(buf) - pos when pos > sizeof(buf) */
int test_snprintf_pos_underflow_guard(void) {
  char buf[16];
  size_t pos = 0;

  /* Fill buffer near capacity */
  pos += snprintf(buf + pos, sizeof(buf) - pos, "ABCDEFGHIJ"); /* 10 chars */
  TEST_ASSERT(pos == 10, "first write should be 10 chars");

  /* snprintf returns what WOULD be written, which could exceed remaining space */
  int ret = snprintf(buf + pos, sizeof(buf) - pos, "KLMNOPQRSTUVWXYZ");
  /* ret = 16 (would-be), but only sizeof(buf)-pos = 6 bytes available */
  TEST_ASSERT(ret == 16, "return value should be 16 (would-be length)");
  TEST_ASSERT(buf[sizeof(buf) - 1] == '\0', "buffer must be null-terminated");

  /* If we naively do pos += ret, pos = 26 > sizeof(buf) = 16.
   * The safe pattern clamps: pos = min(pos + ret, sizeof(buf) - 1) */
  size_t safe_pos = pos + ret;
  if (safe_pos >= sizeof(buf))
    safe_pos = sizeof(buf) - 1;
  TEST_ASSERT(safe_pos == 15, "clamped pos should be 15");
  TEST_PASS();
}

/* PCI bus ID format test (from src/misc/utils.cc) */
int test_snprintf_bus_id_format(void) {
  char busId[13]; /* "XXXX:XX:XX.X" = 12 chars + null */

  /* Test known PCI address: domain=0, bus=0x3b, dev=0, func=0
   * Matches NCCL int64ToBusId layout: domain>>20, bus>>12, dev>>4, func&0xf */
  int64_t id = ((int64_t)0x3b << 12); /* bus=0x3b, dev=0, func=0 */
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
 * PR 3: strncpy table-driven tests
 * ========================================================================= */

typedef struct {
  const char *name;
  const char *src;
  size_t src_len;   /* 0 = use strlen, >0 = explicit */
  size_t n;         /* destination size */
  int raw_null_terminated;  /* 1 if strncpy alone null-terminates */
  int safe_null_terminated; /* 1 after manual dst[n-1]='\0' */
  size_t expected_safe_strlen;
} StrncpyTestCase;

static StrncpyTestCase strncpy_cases[] = {
    /* Positive: source fits */
    {"fits_with_room", "abc", 0, 10, 1, 1, 3},
    {"empty_src", "", 0, 10, 1, 1, 0},

    /* Negative: source doesn't fit - strncpy does NOT null-terminate */
    {"exact_no_null", "abcd", 0, 4, 0, 1, 3},
    {"truncated", "abcdef", 0, 4, 0, 1, 3},
    {"size_1", "abc", 0, 1, 0, 1, 0},

    /* Security: embedded null in source */
    {"embedded_null", "ab\0cd", 5, 10, 1, 1, 2},

    /* Security: high bytes preserved */
    {"high_bytes", "\xff\xfe\xfd", 0, 10, 1, 1, 3},

    {NULL, NULL, 0, 0, 0, 0, 0} /* sentinel */
};

int test_strncpy_null_termination(void) {
  for (int i = 0; strncpy_cases[i].name != NULL; i++) {
    StrncpyTestCase *tc = &strncpy_cases[i];

    size_t src_len = tc->src_len > 0 ? tc->src_len : strlen(tc->src);
    char *dst = (char *)malloc(tc->n + 16); /* extra for overrun detection */
    memset(dst, 'X', tc->n + 16);

    /* Raw strncpy */
    strncpy(dst, tc->src, tc->n);

    char msg[512];

    /* Check if raw strncpy null-terminated */
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

    /* Apply the safe pattern: manual null-termination (what PR 3 does) */
    dst[tc->n - 1] = '\0';

    size_t safe_len = strlen(dst);
    snprintf(msg, sizeof(msg), "[%s] safe strlen=%zu expected=%zu", tc->name,
             safe_len, tc->expected_safe_strlen);
    TEST_ASSERT(safe_len == tc->expected_safe_strlen, msg);

    /* Verify no overrun */
    snprintf(msg, sizeof(msg), "[%s] buffer overrun detected", tc->name);
    TEST_ASSERT(dst[tc->n] == 'X', msg);

    /* Verify high bytes preserved if applicable */
    if (strcmp(tc->name, "high_bytes") == 0) {
      TEST_ASSERT((unsigned char)dst[0] == 0xff, "high byte 0xff preserved");
      TEST_ASSERT((unsigned char)dst[1] == 0xfe, "high byte 0xfe preserved");
      TEST_ASSERT((unsigned char)dst[2] == 0xfd, "high byte 0xfd preserved");
    }

    free(dst);
    (void)src_len;
  }
  TEST_PASS();
}

/* Socket path boundary test (sockaddr_un.sun_path is typically 108 bytes) */
int test_strncpy_socket_path_boundary(void) {
  const size_t SUN_PATH_LEN = 108;
  char dst[108];
  char src[120];

  /* Create a path that's exactly 107 chars (fits with null) */
  memset(src, 'A', 107);
  src[107] = '\0';
  strncpy(dst, src, SUN_PATH_LEN);
  dst[SUN_PATH_LEN - 1] = '\0';
  TEST_ASSERT(strlen(dst) == 107, "107-char path should fit in sun_path");

  /* Create a path that's 108 chars (doesn't fit) */
  memset(src, 'B', 108);
  src[108] = '\0';
  memset(dst, 'X', SUN_PATH_LEN);
  strncpy(dst, src, SUN_PATH_LEN);
  /* Without manual null-term, dst is NOT null-terminated */
  int has_null = 0;
  for (size_t i = 0; i < SUN_PATH_LEN; i++) {
    if (dst[i] == '\0') {
      has_null = 1;
      break;
    }
  }
  TEST_ASSERT(!has_null, "108-char path in 108-byte buf: no null-termination");

  /* Apply safe pattern */
  dst[SUN_PATH_LEN - 1] = '\0';
  TEST_ASSERT(strlen(dst) == 107, "after safe null-term, truncated to 107");
  TEST_PASS();
}

/* MAX_STR_LEN boundary (from NCCL xml.h, typically 256) */
int test_strncpy_max_str_len_boundary(void) {
  const size_t MAX_STR_LEN = 256; /* matches NCCL's xml.h MAX_STR_LEN */
  char dst[256 + 1];              /* +1 for null */
  char src[300];

  /* Exactly MAX_STR_LEN chars */
  memset(src, 'A', MAX_STR_LEN);
  src[MAX_STR_LEN] = '\0';
  strncpy(dst, src, MAX_STR_LEN + 1);
  dst[MAX_STR_LEN] = '\0';
  TEST_ASSERT(strlen(dst) == MAX_STR_LEN, "MAX_STR_LEN string fits exactly");

  /* Exceeds MAX_STR_LEN */
  memset(src, 'B', 299);
  src[299] = '\0';
  strncpy(dst, src, MAX_STR_LEN + 1);
  dst[MAX_STR_LEN] = '\0';
  TEST_ASSERT(strlen(dst) == MAX_STR_LEN,
              "over-long string truncated to MAX_STR_LEN");
  TEST_PASS();
}

/* =========================================================================
 * PR 4: CUDA error handling with stubs (no GPU required)
 * ========================================================================= */

/* Minimal CUDA type stubs */
typedef int cudaError_t;
#define cudaSuccess 0
#define cudaErrorInvalidValue 1
#define cudaErrorMemoryAllocation 2
#define cudaErrorNoDevice 100

static cudaError_t stub_last_error = 0;

__attribute__((used))
static const char *cudaGetErrorString(cudaError_t err) {
  switch (err) {
  case cudaSuccess:
    return "no error";
  case cudaErrorInvalidValue:
    return "invalid argument";
  case cudaErrorMemoryAllocation:
    return "out of memory";
  case cudaErrorNoDevice:
    return "no CUDA-capable device is detected";
  default:
    return "unknown error";
  }
}

static cudaError_t cudaGetLastError(void) {
  cudaError_t e = stub_last_error;
  stub_last_error = cudaSuccess;
  return e;
}

/* Stub NCCL types */
typedef int ncclResult_t;
#define ncclSuccess 0
#define ncclUnhandledCudaError 1

/* WARN/INFO stub counters */
static int warn_count = 0;
static int info_count = 0;

#define WARN(...)                                                              \
  do {                                                                         \
    warn_count++;                                                              \
  } while (0)
#define INFO(FLAGS, ...)                                                       \
  do {                                                                         \
    (void)(FLAGS);                                                             \
    info_count++;                                                              \
  } while (0)
#define NCCL_ALL 0

/* Copy of CUDACHECK macro from src/include/checks.h */
#define CUDACHECK(cmd)                                                         \
  do {                                                                         \
    cudaError_t err = cmd;                                                     \
    if (err != cudaSuccess) {                                                  \
      WARN("Cuda failure '%s'", cudaGetErrorString(err));                      \
      (void)cudaGetLastError();                                                \
      return ncclUnhandledCudaError;                                           \
    }                                                                          \
  } while (0)

#define CUDACHECKGOTO(cmd, RES, label)                                         \
  do {                                                                         \
    cudaError_t err = cmd;                                                     \
    if (err != cudaSuccess) {                                                  \
      WARN("Cuda failure '%s'", cudaGetErrorString(err));                      \
      (void)cudaGetLastError();                                                \
      RES = ncclUnhandledCudaError;                                            \
      goto label;                                                              \
    }                                                                          \
  } while (0)

#define CUDACHECKIGNORE(cmd)                                                   \
  do {                                                                         \
    cudaError_t err = cmd;                                                     \
    if (err != cudaSuccess) {                                                  \
      INFO(NCCL_ALL, "%s:%d Cuda failure '%s'", __FILE__, __LINE__,            \
           cudaGetErrorString(err));                                            \
      (void)cudaGetLastError();                                                \
    }                                                                          \
  } while (0)

/* Helper: simulates a CUDA call returning a specific error */
static cudaError_t stub_cuda_return;
static cudaError_t fake_cuda_call(void) {
  cudaError_t ret = stub_cuda_return;
  stub_last_error = ret; /* Set sticky error like real CUDA */
  return ret;
}

typedef struct {
  const char *name;
  cudaError_t error_code;
  int expect_warn;
  int expect_success;
} CudaCheckTestCase;

static CudaCheckTestCase cuda_check_cases[] = {
    {"success", cudaSuccess, 0, 1},
    {"invalid_value", cudaErrorInvalidValue, 1, 0},
    {"out_of_memory", cudaErrorMemoryAllocation, 1, 0},
    {"no_device", cudaErrorNoDevice, 1, 0},
    {NULL, 0, 0, 0}};

/* Wrapper function to test CUDACHECK (it uses return) */
static ncclResult_t test_cudacheck_wrapper(cudaError_t err) {
  stub_cuda_return = err;
  CUDACHECK(fake_cuda_call());
  return ncclSuccess;
}

int test_cudacheck_cases(void) {
  for (int i = 0; cuda_check_cases[i].name != NULL; i++) {
    CudaCheckTestCase *tc = &cuda_check_cases[i];
    warn_count = 0;
    stub_last_error = cudaSuccess;

    ncclResult_t ret = test_cudacheck_wrapper(tc->error_code);

    char msg[512];
    if (tc->expect_success) {
      snprintf(msg, sizeof(msg), "[%s] expected ncclSuccess, got %d", tc->name,
               ret);
      TEST_ASSERT(ret == ncclSuccess, msg);
    } else {
      snprintf(msg, sizeof(msg), "[%s] expected error, got ncclSuccess",
               tc->name);
      TEST_ASSERT(ret == ncclUnhandledCudaError, msg);
    }

    if (tc->expect_warn) {
      snprintf(msg, sizeof(msg), "[%s] expected WARN to be called", tc->name);
      TEST_ASSERT(warn_count > 0, msg);
    } else {
      snprintf(msg, sizeof(msg), "[%s] WARN should not be called", tc->name);
      TEST_ASSERT(warn_count == 0, msg);
    }
  }
  TEST_PASS();
}

/* Test CUDACHECKIGNORE: should log but not return */
int test_cudacheckignore(void) {
  /* Success case */
  info_count = 0;
  stub_cuda_return = cudaSuccess;
  CUDACHECKIGNORE(fake_cuda_call());
  TEST_ASSERT(info_count == 0, "CUDACHECKIGNORE should not log on success");

  /* Error case: should log but NOT return */
  info_count = 0;
  stub_cuda_return = cudaErrorInvalidValue;
  CUDACHECKIGNORE(fake_cuda_call());
  TEST_ASSERT(info_count == 1,
              "CUDACHECKIGNORE should log on error");

  /* We reached here, meaning CUDACHECKIGNORE did NOT return */
  TEST_PASS();
}

/* Test CUDACHECKGOTO: should set RES and jump to label */
int test_cudacheckgoto(void) {
  ncclResult_t res = ncclSuccess;
  int reached_label = 0;

  /* Success case */
  warn_count = 0;
  stub_cuda_return = cudaSuccess;
  CUDACHECKGOTO(fake_cuda_call(), res, error_label);
  TEST_ASSERT(res == ncclSuccess, "CUDACHECKGOTO success: res should be 0");
  TEST_ASSERT(warn_count == 0, "CUDACHECKGOTO success: no WARN");
  goto skip_error;

error_label:
  reached_label = 1;

skip_error:
  TEST_ASSERT(!reached_label, "CUDACHECKGOTO success: should not goto label");

  /* Error case */
  res = ncclSuccess;
  reached_label = 0;
  warn_count = 0;
  stub_cuda_return = cudaErrorMemoryAllocation;
  CUDACHECKGOTO(fake_cuda_call(), res, error_label2);
  TEST_ASSERT(0, "CUDACHECKGOTO error: should have jumped to label");

error_label2:
  reached_label = 1;
  TEST_ASSERT(reached_label, "CUDACHECKGOTO error: should reach label");
  TEST_ASSERT(res == ncclUnhandledCudaError,
              "CUDACHECKGOTO error: res should be ncclUnhandledCudaError");
  TEST_ASSERT(warn_count == 1, "CUDACHECKGOTO error: WARN should be called");
  TEST_PASS();
}

/* Test that cudaGetLastError clears the sticky error */
int test_cuda_error_state_cleared(void) {
  /* Set sticky error */
  stub_last_error = cudaErrorInvalidValue;
  TEST_ASSERT(stub_last_error != cudaSuccess, "error should be set");

  /* Clear it */
  cudaError_t cleared = cudaGetLastError();
  TEST_ASSERT(cleared == cudaErrorInvalidValue,
              "cudaGetLastError should return the error");
  TEST_ASSERT(stub_last_error == cudaSuccess,
              "after cudaGetLastError, error should be cleared");

  /* Second call should return success */
  cleared = cudaGetLastError();
  TEST_ASSERT(cleared == cudaSuccess,
              "second cudaGetLastError should return success");
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
    {"cudacheck-cases", test_cudacheck_cases,
     "CUDACHECK macro with various error codes"},
    {"cudacheckignore", test_cudacheckignore,
     "CUDACHECKIGNORE logs but does not return"},
    {"cudacheckgoto", test_cudacheckgoto,
     "CUDACHECKGOTO sets result and jumps to label"},
    {"cuda-error-clear", test_cuda_error_state_cleared,
     "cudaGetLastError clears sticky error state"},
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

  printf("Buffer Safety Unit Tests (PR 3 + PR 4)\n");
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
