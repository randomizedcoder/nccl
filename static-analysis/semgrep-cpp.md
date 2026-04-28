# Semgrep C/C++/CUDA Analysis

Pattern-based static analysis using vendored rules for C/C++ and CUDA-specific patterns.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 2,715 |
| C/C++ generic rules | 47 rules |
| CUDA-specific rules | 11 rules |

### By Rule

| Rule | Count | Severity | Description |
|------|-------|----------|-------------|
| c-style-pointer-cast | 1,512 | INFO | C-style cast instead of C++ cast |
| raw-free-in-cpp | 521 | INFO | `free()` in C++ code |
| reinterpret-cast | 224 | INFO | `reinterpret_cast` usage |
| memcpy-sizeof-pointer | 86 | WARNING | `memcpy` with `sizeof(ptr)` |
| **cuda-missing-sync** | **83** | **WARNING** | Async memcpy without sync |
| raw-malloc | 55 | INFO | `malloc()` in C++ code |
| unsafe-sprintf | 47 | WARNING | `sprintf()` without bounds |
| strerror-thread-unsafe | 46 | INFO | Thread-unsafe `strerror()` |
| atoi-atol-usage | 33 | WARNING | No-error-checking `atoi/atol` |
| unsafe-strcpy | 18 | WARNING | `strcpy()` without bounds |
| **cuda-unchecked-memcpy** | **16** | **WARNING** | Unchecked `cudaMemcpy()` |
| fopen-raw-file-pointer | 14 | INFO | Raw `FILE*` without RAII |
| **cuda-unchecked-malloc** | **11** | **WARNING** | Unchecked `cudaMalloc()` |
| thread-creation | 10 | INFO | `std::thread` lifecycle |
| **cuda-set-device-unchecked** | **9** | **WARNING** | Unchecked `cudaSetDevice()` |
| relaxed-memory-order | 8 | INFO | `memory_order_relaxed` usage |
| **cuda-missing-free** | **4** | **WARNING** | GPU memory leak |
| **cuda-missing-stream-destroy** | **4** | **WARNING** | Stream leak |
| toctou-access | 3 | WARNING | TOCTOU race in `access()` |
| chmod-on-pathname | 2 | WARNING | TOCTOU race in `chmod()` |

## Top Priority Findings

### 1. CUDA Error Handling (36 findings)

Unchecked CUDA API calls that can silently fail:

**cudaMalloc unchecked (11)**:
```
contrib/nccl_ep/nccl_ep.cc:117  cudaMalloc() not checked
contrib/nccl_ep/nccl_ep.cc:139  cudaMalloc() not checked
contrib/nccl_ep/nccl_ep.cc:334  cudaMalloc() not checked
contrib/nccl_ep/nccl_ep.cc:427  cudaMalloc() not checked
```

**cudaMemcpy unchecked (16)**:
```
contrib/nccl_ep/nccl_ep.cc:118  cudaMemcpy() not checked
contrib/nccl_ep/nccl_ep.cc:126  cudaMemcpy() not checked
contrib/nccl_ep/nccl_ep.cc:342  cudaMemcpy() not checked
contrib/nccl_ep/nccl_ep.cc:354  cudaMemcpy() not checked
```

**Fix**: Wrap in `CUDACHECK()` or `CUDACHECKGOTO()` macros per NCCL convention.

### 2. CUDA Async Synchronization (83 findings)

`cudaMemcpyAsync()` calls without subsequent synchronization before the buffer is used. This can cause race conditions between CPU and GPU.

**Fix**: Add `cudaStreamSynchronize()` or `cudaEventSynchronize()` before accessing copied data.

### 3. GPU Resource Leaks (8 findings)

- 4 `cudaMalloc` without matching `cudaFree` before return
- 4 `cudaStreamCreate` without matching `cudaStreamDestroy`

**Fix**: Ensure cleanup on all return paths, or use RAII wrappers.

### 4. Buffer Safety (151 findings)

- 86 `memcpy(dst, src, sizeof(ptr))` - likely copies pointer size, not data
- 47 `sprintf()` without bounds checking
- 18 `strcpy()` without bounds checking

**Fix**: Use `snprintf()`, `strncpy()`, and verify `memcpy` size arguments.

### 5. Thread Safety (46 findings)

`strerror()` is not thread-safe and used in 46 locations.

**Fix**: Use `strerror_r()` or `std::system_category().message()`.

## Reproduction

```bash
nix build .#analysis-semgrep-cpp
cat result/count.txt
cat result/report.txt       # Human-readable
cat result/report.json      # Machine-parseable
```
