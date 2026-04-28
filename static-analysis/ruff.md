# Ruff Analysis

Fast Python linting covering pyflakes, pycodestyle, isort, pep8-naming, flake8-bugbear, and more.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 1,890 |

### Top Rule Categories

| Rule | Count | Description |
|------|-------|-------------|
| ANN001 | 196 | Missing type annotation for function argument |
| Q000 | 185 | Use double quotes (style) |
| D212 | 181 | Multi-line docstring summary on first line |
| D413 | 167 | Missing blank line after last docstring section |
| TRY003 | 104 | Avoid specifying long messages outside exception class |
| T201 | 91 | `print()` found (use logging) |
| EM102 | 77 | Exception must not use f-string literal |
| ANN201 | 77 | Missing return type annotation |
| D401 | 63 | Docstring first line should be imperative |
| F405 | 59 | May be undefined from star import |
| D417 | 49 | Missing argument descriptions in docstring |
| UP031 | 28 | Use format specifiers instead of percent format |
| LOG015 | 27 | Logging statement uses string concatenation |
| EM101 | 27 | Exception must not use string literal |
| G004 | 26 | Logging statement uses f-string |

## Top Priority Findings

### 1. Security issues (S-rules)
```
bindings/nccl4py/nccl/core/communicator.py:2509  S110 try-except-pass detected
contrib/nccl_ep/ep_test.py:308                    S311 Pseudo-random generator not for crypto
contrib/nccl_ep/python/nccl_ep/nccl_wrapper.py:329 S110 try-except-pass detected
contrib/nccl_ep/python/nccl_ep/nccl_wrapper.py:378 S110 try-except-pass detected
```

**Fix**: Replace bare `except: pass` with proper error handling or logging. Use `secrets` module for any security-sensitive randomness.

### 2. Bug-prone patterns (B-rules)
```
bindings/nccl4py/nccl/core/interop/cupy.py:74    B904 raise from err
bindings/nccl4py/nccl/core/typing.py:139,323      B904 raise from err
contrib/nccl_ep/python/nccl_ep/__init__.py:66     B028 Missing stacklevel
```

**Fix**: Use `raise ... from err` in except blocks to preserve exception chains. Add `stacklevel=2` to `warnings.warn()` calls.

### 3. Possible undefined names from star imports (59 findings)
```
F405: May be undefined, or defined from star imports
```
Multiple files use `from module import *` making it unclear which names are available.

**Fix**: Replace star imports with explicit imports.

### 4. Print statements in library code (91 findings)
The nccl4py bindings use `print()` instead of the logging module.

**Fix**: Replace with `logging.info()` / `logging.debug()` calls.

## Reproduction

```bash
nix build .#analysis-ruff
cat result/count.txt
cat result/report.txt       # Concise format
cat result/report.json      # JSON for tooling
```
