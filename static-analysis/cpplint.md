# cpplint Analysis

Google C++ style checking for source and header files.

## Summary

| Metric | Value |
|--------|-------|
| Total findings | 2,741 |

### By Category

| Category | Count | Description |
|----------|-------|-------------|
| readability/casting | 1,609 | C-style casts instead of C++ casts |
| build/include_order | 390 | Include ordering issues |
| runtime/int | 219 | Use of non-portable integer types |
| build/include_what_you_use | 140 | Missing direct includes |
| readability/braces | 93 | Brace style issues |
| runtime/printf | 78 | Unsafe printf-family usage |
| readability/alt_tokens | 53 | Use of alternative tokens |
| runtime/casting | 27 | Unsafe runtime casts |
| runtime/arrays | 22 | C-style array usage |
| build/include | 19 | Include path issues |
| runtime/threadsafe_fn | 16 | Thread-unsafe function usage |
| readability/namespace | 15 | Namespace style issues |

## Top Priority Findings

### 1. Unsafe printf-family usage (78 findings)
These are the most security-relevant findings:

```
plugins/profiler/example/print_event.cc:263  Never use sprintf. Use snprintf instead. [5]
src/debug.cc:221  Almost always, snprintf is better than strcpy [4]
src/debug.cc:227  Almost always, snprintf is better than strcpy [4]
src/debug.cc:229  Almost always, snprintf is better than strcpy [4]
```

**Fix**: Replace `sprintf()` with `snprintf()`, and `strcpy()` with bounded copies.

### 2. Thread-unsafe function usage (16 findings)
Critical for a multi-threaded library like NCCL:

```
src/debug.cc:99          strtok -> strtok_r
plugins/tuner/example/plugin.c:215,225  strtok -> strtok_r
plugins/profiler/inspector/inspector.cc:134  gmtime -> gmtime_r
contrib/nccl_ep/ep_test.cu:301  rand -> rand_r
```

**Fix**: Replace with reentrant (`_r`) variants for thread safety.

### 3. Missing includes (140 findings)
Files using symbols without directly including the providing header:

```
contrib/nccl_ep/device/device_primitives.cuh:70  Add #include <utility> for forward
contrib/nccl_ep/device/device_primitives.cuh:572 Add #include <algorithm> for min
```

**Fix**: Add the missing `#include` directives for explicit dependency declarations.

### 4. C-style casts (1,609 findings)
The largest category by volume. NCCL uses C-style casts extensively. These are lower priority but indicate areas where type safety could be improved with `static_cast<>`, `reinterpret_cast<>`, etc.

## Reproduction

```bash
nix build .#analysis-cpplint
cat result/count.txt
cat result/report.txt
```
