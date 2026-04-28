# CMake Lint (gersemi) Analysis

CMake file formatting checks via gersemi.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 16 |
| Files needing reformatting | 16 |

## Files Needing Reformatting

| File | Priority |
|------|----------|
| `CMakeLists.txt` | High - root build file |
| `src/CMakeLists.txt` | High - main source build |
| `src/device/CMakeLists.txt` | Medium |
| `src/nccl_device/CMakeLists.txt` | Medium |
| `src/misc/CMakeLists.txt` | Medium |
| `src/transport/net_ib/gdaki/CMakeLists.txt` | Medium |
| `src/plugin/env/CMakeLists.txt` | Medium |
| `src/generate_header.cmake` | Medium |
| `bindings/ir/CMakeLists.txt` | Low |
| `bindings/nccl4py/CMakeLists.txt` | Low |
| `contrib/nccl_ep/CMakeLists.txt` | Low |
| `plugins/env/example/CMakeLists.txt` | Low |
| `plugins/mixed/example/CMakeLists.txt` | Low |
| `plugins/net/example/CMakeLists.txt` | Low |
| `plugins/profiler/example/CMakeLists.txt` | Low |
| `plugins/tuner/example/CMakeLists.txt` | Low |

## Notes

All 16 CMakeLists.txt and .cmake files would be reformatted by gersemi. This is a formatting-only issue (no functional impact). The fixes are consistent indentation, argument alignment, and whitespace normalization.

To auto-fix all files:
```bash
nix develop
find . -name 'CMakeLists.txt' -o -name '*.cmake' | xargs gersemi -i
```

## Reproduction

```bash
nix build .#analysis-cmake-lint
cat result/report.txt
```
