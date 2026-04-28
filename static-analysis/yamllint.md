# yamllint Analysis

YAML file linting for syntax, formatting, and consistency.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 13 |
| Errors | 6 |
| Warnings | 7 |

## Findings by File

### .github/ISSUE_TEMPLATE/ISSUE.yaml (5 issues)
| Line | Severity | Issue |
|------|----------|-------|
| 1 | warning | Missing document start `---` |
| 10 | error | Line too long (247 > 200 chars) |
| 12 | error | Trailing spaces |
| 14 | error | Trailing spaces |

### .github/ISSUE_TEMPLATE/RFE.yaml (3 issues)
| Line | Severity | Issue |
|------|----------|-------|
| 1 | warning | Missing document start `---` |
| 10 | error | Line too long (243 > 200 chars) |
| 11 | error | Trailing spaces |

### .github/ISSUE_TEMPLATE/QUESTION.yaml (2 issues)
| Line | Severity | Issue |
|------|----------|-------|
| 1 | warning | Missing document start `---` |
| 10 | error | Line too long (307 > 200 chars) |

### .github/ISSUE_TEMPLATE/config.yml (1 issue)
| Line | Severity | Issue |
|------|----------|-------|
| 1 | warning | Missing document start `---` |

### nix/analysis/rules/*.yaml (3 warnings)
Our own semgrep rule files are missing `---` document start markers.
- `nix/analysis/rules/semgrep-cpp.yaml:1`
- `nix/analysis/rules/semgrep-cuda.yaml:1`
- `nix/analysis/rules/semgrep-python.yaml:1`

## Recommended Fixes

1. Add `---` document start to all YAML files
2. Wrap long description lines in `.github/ISSUE_TEMPLATE/` templates
3. Remove trailing whitespace in ISSUE.yaml and RFE.yaml

## Reproduction

```bash
nix build .#analysis-yamllint
cat result/report.txt
```
