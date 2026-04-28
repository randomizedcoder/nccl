# Pylint Analysis

Comprehensive Python linting for errors, conventions, refactoring, and warnings.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 1,204 |
| Errors | 79 |
| Warnings | 558 |
| Conventions | 452 |
| Refactors | 89 |

### Top Issues by Symbol

| Symbol | Count | Type | Description |
|--------|-------|------|-------------|
| bad-indentation | 375 | C | Inconsistent indentation |
| line-too-long | 229 | C | Lines exceeding length limit |
| redefined-outer-name | 78 | W | Variable shadows outer scope |
| no-member | 73 | E | Accessing non-existent attribute |
| missing-function-docstring | 69 | C | Missing docstring |
| invalid-name | 57 | C | Naming convention violation |
| consider-using-f-string | 34 | R | Use f-strings instead of % formatting |
| logging-fstring-interpolation | 26 | W | Use lazy % formatting in logging |
| multiple-statements | 19 | C | Multiple statements on one line |
| no-else-return | 17 | R | Unnecessary else after return |
| unspecified-encoding | 14 | W | open() without encoding parameter |
| unused-argument | 13 | W | Unused function argument |

## Top Priority Findings

### 1. Actual errors (79 findings)

#### used-before-assignment (1)
```
contrib/nccl_ep/python/nccl_ep/__init__.py:67 - Using '_import_error' before assignment
```
**Fix**: Ensure the variable is assigned in all code paths before use.

#### no-self-argument (3)
```
src/device/symmetric/generate.py:44 - __init__ missing self
src/device/symmetric/generate.py:46 - __eq__ missing self
src/device/symmetric/generate.py:48 - __hash__ missing self
```
**Fix**: Add `self` as first parameter to these methods.

#### no-member (73) - mostly expected
Most `no-member` errors are from `bindings/nccl4py/nccl/core/` accessing `nccl.bindings.*` members that are dynamically generated C bindings. These are **expected false positives** since the C extension module generates members at import time.

#### undefined-all-variable (2)
```
bindings/nccl4py/nccl/core/__init__.py:105 - 'cupy' undefined in __all__
bindings/nccl4py/nccl/core/__init__.py:106 - 'torch' undefined in __all__
```
**Fix**: These are conditionally imported modules listed in `__all__`. Guard with availability checks.

### 2. Thread safety concerns
```
unspecified-encoding (14 findings) - open() without encoding=
```
**Fix**: Always specify `encoding='utf-8'` (or appropriate encoding) in `open()` calls.

### 3. Code quality
- **375 bad-indentation**: Inconsistent use of spaces (likely tabs vs spaces or mixed indent widths)
- **229 line-too-long**: Lines exceeding standard length limits
- **78 redefined-outer-name**: Variables shadowing outer scope names

## Notes

The 73 `no-member` errors on `nccl.bindings` are expected since these are C extension bindings that pylint cannot introspect. These should be suppressed in a future pylintrc configuration.

## Reproduction

```bash
nix build .#analysis-pylint
cat result/count.txt
cat result/report.txt
cat result/report.json
```
