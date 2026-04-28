# Semgrep Python Analysis

Pattern-based security scanning for Python code using vendored rules.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 105 |

### By Rule

| Rule | Count | Severity | Description |
|------|-------|----------|-------------|
| py-print-statement | 91 | INFO | `print()` in library code |
| py-open-user-input | 14 | INFO | `open()` without path validation |

## Findings

### 1. Print statements in library code (91 findings)

The `bindings/nccl4py/` and `contrib/nccl_ep/` Python code uses `print()` statements instead of the `logging` module. While functional, this makes it harder to control output verbosity in production.

**Fix**: Replace `print()` with `logging.info()` or `logging.debug()` calls.

### 2. File opens without path validation (14 findings)

`open()` calls where the path could potentially be influenced by external input.

**Fix**: Validate and sanitize file paths before opening, especially if derived from user input or configuration.

## Notes

No high-severity findings (command injection, SQL injection, hardcoded secrets, etc.) were detected in the Python code. The codebase is relatively clean from a security perspective.

## Reproduction

```bash
nix build .#analysis-semgrep-python
cat result/count.txt
cat result/report.txt
cat result/report.json
```
