# NCCL Static Analysis Results

Static analysis of NCCL v2.30.4-1 using Nix-based tooling. Each tool runs in an isolated Nix build sandbox against the full source tree.

## Overview

| Tool | Language | Findings | Top Severity | Report |
|------|----------|----------|--------------|--------|
| [clang-tidy](clang-tidy.md) | C/C++/CUDA | 434,940 | High | Comprehensive linting with compile DB |
| [cppcheck](cppcheck.md) | C/C++/CUDA | 2,534 | Error | Deep analysis with compile DB |
| [semgrep-cpp](semgrep-cpp.md) | C/C++/CUDA | 2,715 | Warning | Pattern-based (47 C++ + 11 CUDA rules) |
| [cpplint](cpplint.md) | C/C++ | 2,741 | - | Google style checker |
| [flawfinder](flawfinder.md) | C/C++ | 1,349 | Level 5 (critical) | Security-focused scanner |
| [ruff](ruff.md) | Python | 1,890 | - | Fast Python linter |
| [pylint](pylint.md) | Python | 1,204 | Error | Comprehensive Python linter |
| [semgrep-python](semgrep-python.md) | Python | 105 | Info | Pattern-based (17 rules) |
| [bandit](bandit.md) | Python | 13 | Medium | Python security scanner |
| [cmake-lint](cmake-lint.md) | CMake | 16 | - | CMake formatting (gersemi) |
| [yamllint](yamllint.md) | YAML | 13 | - | YAML linter |
| [shellcheck](shellcheck.md) | Shell | 3 | - | Shell script linter |
| **Total** | | **447,523** | | |

## Top Priority Issues

These are the findings most likely to represent real bugs or security concerns, distilled across all tools.

### Critical: Potential Bugs

| Issue | Tools | Count | Location |
|-------|-------|-------|----------|
| Thread-unsafe functions (`strtok`, `gmtime`, `getenv`, `strerror`) | clang-tidy, cpplint, semgrep-cpp | 836+ | Throughout `src/`, `plugins/` |
| Unchecked CUDA API calls (`cudaMalloc`, `cudaMemcpy`, `cudaSetDevice`) | semgrep-cpp | 36 | `contrib/nccl_ep/nccl_ep.cc` |
| Async memcpy without synchronization | semgrep-cpp | 83 | `src/`, `contrib/` |
| Uninitialized member variables | cppcheck, clang-tidy | 7,393 | Throughout |
| Narrowing conversions (data loss) | clang-tidy | 2,118 | Throughout |
| Unchecked return values | clang-tidy | 582 | Throughout |

### High: Security Concerns

| Issue | Tools | Count | Location |
|-------|-------|-------|----------|
| `sprintf`/`strcpy` without bounds | semgrep-cpp, flawfinder, cpplint | 65+ | `src/misc/debug.cc`, `plugins/` |
| TOCTOU races (`chmod`, `access`) | flawfinder, semgrep-cpp | 5 | `src/misc/inspector.cc`, `src/misc/utils.cc` |
| SQL injection via string formatting | bandit | 2 | `plugins/profiler/inspector/` |
| `system()` calls | flawfinder | 4 | `src/misc/paths.cc` |
| GPU memory leaks (missing `cudaFree`/`cudaStreamDestroy`) | semgrep-cpp | 8 | `contrib/nccl_ep/` |

### Medium: Code Quality

| Issue | Tools | Count | Location |
|-------|-------|-------|----------|
| One Definition Rule violations | cppcheck | 5 | `src/transport/` |
| Printf format mismatches | cppcheck | 35 | Throughout |
| `memcpy` with `sizeof(pointer)` | semgrep-cpp | 86 | Throughout |
| Assignment in if-condition | clang-tidy | 23 | `src/plugin/`, `src/ras/`, `src/os/` |
| Python `used-before-assignment` | pylint | 1 | `contrib/nccl_ep/python/` |
| Python `no-self-argument` | pylint | 3 | `src/device/symmetric/generate.py` |

## Recommended Fix Priority

1. **Thread safety** - NCCL is a multi-threaded GPU communications library. Replace `strtok`/`gmtime`/`strerror` with reentrant variants (`_r` suffixes). This affects correctness under concurrent use.

2. **CUDA error handling** - Unchecked `cudaMalloc`/`cudaMemcpy` in `contrib/nccl_ep/nccl_ep.cc` can silently fail. Wrap in `CUDACHECK()`/`CUDACHECKGOTO()` macros per NCCL convention.

3. **Buffer safety** - Replace `sprintf` with `snprintf`, `strcpy` with `strncpy`. These are exploitable if any input is externally influenced.

4. **GPU resource leaks** - Missing `cudaFree`/`cudaStreamDestroy` in error paths of `contrib/nccl_ep/`.

5. **SQL injection** - Use parameterized queries in `plugins/profiler/inspector/exporter/example/perf_summary_exporter.py`.

6. **Uninitialized members** - Add default member initializers, particularly in device-side structs where undefined behavior is harder to debug.

## Tool Configuration

All tools run via Nix flake outputs:

```bash
# Individual tools
nix build .#analysis-flawfinder && cat result/count.txt && cat result/report.txt
nix build .#analysis-cppcheck   && cat result/count.txt && cat result/report.txt
nix build .#analysis-clang-tidy && cat result/count.txt && cat result/report.txt

# Composite tiers
nix build .#analysis-quick      # ~2-5 min:  flawfinder, cpplint, ruff, shellcheck, yamllint, cmake-lint
nix build .#analysis-standard   # ~10-20 min: quick + cppcheck, clang-tidy, bandit, pylint
nix build .#analysis-deep       # All tools:  standard + semgrep, clang-analyzer, iwyu, coccinelle, gcc-warnings, gcc-analyzer

# Dev shell with all tools on PATH
nix develop
```

## Notes

- **clang-tidy's 434K findings** are overwhelmingly style/modernization (identifier naming, C-array usage, macro style). The ~4,000 bugprone/cert/concurrency findings are the actionable subset.
- **pylint's 73 `no-member` errors** on `nccl.bindings.*` are expected false positives from C extension bindings that pylint cannot introspect.
- The compile database is cmake-generated with CUDA support (433 translation units).
- All analysis runs in the Nix sandbox with no network access and reproducible results.
