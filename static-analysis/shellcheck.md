# ShellCheck Analysis

Shell script linting for bash/sh scripting pitfalls.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 3 |
| Files scanned | 1 (`plugins/mixed/example/test.sh`) |

## All Findings

| Location | Code | Severity | Issue |
|----------|------|----------|-------|
| `plugins/mixed/example/test.sh:31` | SC2086 | note | Double quote to prevent globbing and word splitting |
| `plugins/mixed/example/test.sh:37` | SC2059 | note | Don't use variables in printf format string |
| `plugins/mixed/example/test.sh:44` | SC2059 | note | Don't use variables in printf format string |

## Fixes

### SC2086 - Quote variable expansions (line 31)
Unquoted variables are subject to word splitting and globbing:
```bash
# Before
some_command $variable
# After
some_command "$variable"
```

### SC2059 - Don't use variables in printf format string (lines 37, 44)
Using a variable as a printf format string can cause unexpected behavior:
```bash
# Before
printf $format_var args
# After
printf '%s' "$format_var"
```

## Reproduction

```bash
nix build .#analysis-shellcheck
cat result/report.txt
```
