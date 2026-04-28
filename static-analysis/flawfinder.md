# Flawfinder Analysis

CWE-oriented security scanning for C/C++/CUDA source files.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 1,349 |
| Level 5 (Critical) | 3 |
| Level 4 (High) | 412 |
| Level 3 (Medium) | 69 |
| Level 2 (Low) | 615 |
| Level 1 (Info) | 250 |

### By Category

| Category | Count | Description |
|----------|-------|-------------|
| buffer | 889 | Buffer overflow risks (memcpy, strlen, char arrays) |
| shell | 302 | Shell/command execution risks (system, popen) |
| race | 56 | Race conditions (TOCTOU: access, chmod) |
| integer | 41 | Integer overflow/truncation risks |
| misc | 28 | Miscellaneous issues |
| format | 24 | Format string vulnerabilities |

## Critical Findings (Level 5) - Fix These

### 1. TOCTOU race condition in chmod calls
- `plugins/profiler/inspector/inspector.cc:593` - `chmod(filename, 0777)` on path
- `plugins/profiler/inspector/inspector.cc:658` - `chmod(tmp, 0666)` on path
- **CWE-362**: If an attacker can replace the file between open and chmod, they gain unintended permissions
- **Fix**: Use `fchmod()` on an already-open file descriptor instead

### 2. Symlink race in readlink
- `src/misc/utils.cc:189` - `readlink()` on attacker-controllable path
- **CWE-362**: Path can change between check and use
- **Fix**: Use `readlinkat()` with a directory fd, or validate after open

## High-Priority Findings (Level 4) - Review These

### system() calls in paths.cc (CWE-78 Command Injection)
Multiple `system()` calls in `src/graph/paths.cc` at lines 24, 37, 48, 58, 67, 122.
- **Risk**: If arguments are influenced by environment or config, command injection is possible
- **Fix**: Replace with `execve()` or `posix_spawn()` with explicit argument arrays

### strcpy without bounds checking (CWE-120 Buffer Overflow)
- `src/debug.cc:221,227,360` - Uses `strcpy()` which has no bounds checking
- **Fix**: Replace with `snprintf()` or `strncpy()` with explicit size limits

### sprintf without bounds checking
- `plugins/profiler/example/print_event.cc:263` - Uses `sprintf()` (unbounded)
- **Fix**: Replace with `snprintf()` with buffer size

### Format string concerns
- `src/debug.cc:313,356,412,464` - `vsnprintf`/`vfprintf` with potentially attacker-influenced format strings
- `plugins/tuner/example/test/test_plugin.c:87` - `vprintf` format string concern
- **Fix**: Ensure format strings are always literals, never user-controlled

### access() TOCTOU race (CWE-367)
- `plugins/profiler/inspector/inspector.cc:415` - `access()` check followed by use
- **Fix**: Use `faccessat()` or open-then-check pattern

## Reproduction

```bash
nix build .#analysis-flawfinder
cat result/count.txt        # Total count
cat result/report.txt       # Full human-readable report
cat result/report.csv        # Machine-parseable CSV
```
