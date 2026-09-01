CC := gcc
CFLAGS := -Wall -Wextra -Wno-format-truncation -g -std=c99 -fPIC -D_GNU_SOURCE
LDFLAGS := -lpthread

TARGETS := test_thread_safety test_buffer_safety

all: $(TARGETS)

test_thread_safety: test_thread_safety.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_buffer_safety: test_buffer_safety.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: $(TARGETS)
	@echo "======================================="
	@echo "Running thread safety tests (PR 1 + 2)"
	@echo "======================================="
	./test_thread_safety $(TEST_CASE)
	@echo ""
	@echo "======================================="
	@echo "Running buffer safety tests (PR 3 + 4)"
	@echo "======================================="
	./test_buffer_safety $(TEST_CASE)

test-verbose: $(TARGETS)
	VERBOSE=1 ./test_thread_safety $(TEST_CASE)
	VERBOSE=1 ./test_buffer_safety $(TEST_CASE)

clean:
	rm -f $(TARGETS)

.PHONY: all test test-verbose clean
