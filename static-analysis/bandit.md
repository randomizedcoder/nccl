# Bandit Analysis

Python security scanning for common vulnerability patterns.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 13 |
| High severity | 0 |
| Medium severity | 2 |
| Low severity | 11 |

### By Test

| Test | Count | Description |
|------|-------|-------------|
| B110 try_except_pass | 5 | Silent exception swallowing |
| B404 blacklist | 3 | Import of subprocess module |
| B608 hardcoded_sql | 2 | SQL injection via string construction |
| B311 blacklist | 1 | Use of random (not crypto-safe) |
| B101 assert_used | 1 | Assert used (disabled in optimized mode) |
| B603 subprocess | 1 | Subprocess without shell=True check |

## Top Priority Findings

### 1. Possible SQL injection (MEDIUM severity)
```
plugins/profiler/inspector/exporter/example/perf_summary_exporter.py:403
plugins/profiler/inspector/exporter/example/perf_summary_exporter.py:405
```
SQL queries constructed via string formatting instead of parameterized queries.

**Fix**: Use parameterized queries (`cursor.execute("SELECT ?", (param,))`) instead of string interpolation.

### 2. Silent exception handling (5 findings)
Multiple locations use `try: ... except: pass` which silently swallows errors:
- `bindings/nccl4py/nccl/core/communicator.py`
- `contrib/nccl_ep/python/nccl_ep/nccl_wrapper.py` (2 locations)

**Fix**: At minimum, log the exception. Better: handle specific exception types.

### 3. Subprocess usage (1 finding)
One call to `subprocess` without explicit `shell=False`.

**Fix**: Explicitly pass `shell=False` and use argument lists.

## Reproduction

```bash
nix build .#analysis-bandit
cat result/count.txt
cat result/report.txt       # Text format
cat result/report.json      # JSON format
```
