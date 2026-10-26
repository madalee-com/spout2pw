# spoutdxtoc Changes Since Fork

**Fork point:** `fa10d2b` (hoshinolina/spout2pw)  
**Current:** `dc53c2e`  
**Files changed:** 4

---

## Summary

| File | Lines Added | Lines Removed | Purpose |
|------|-------------|---------------|---------|
| `meson.build` | +19 | -1 | Vulkan deps, Wine path detection |
| `spoutdxtoc.cpp` | +529 | -1 | Vulkan interop, PNG export, NT handle import |
| `spoutdxtoc.def` | +4 | -0 | New function exports |
| `spoutdxtoc.h` | +8 | -0 | API declarations |

**Total:** +560 / -2 lines

---

## Changes Analysis

### 1. `meson.build` — Build System

```diff
+spoutdx_include_dir += [
+  include_directories('/usr/x86_64-w64-mingw32/include/vulkan/'),
+]
+
+# Wine PE library path detection (NEW)
+r = run_command(join_paths(..., '../../tools/get_wine_path.sh'),
+    'x86_64-w64-mingw32', '^/.*/libwinecrt0.a$', check: true)
+
+# Added dependencies (NEW)
 spoutdx_deps = [
   ...
+  cpp.find_library('vulkan-1', dirs: wine_lib_paths),
+  cpp.find_library('libpng16-16', dirs: '/usr/x86_64-w64-mingw32/bin/'),
 ]
```

**Needed?** ✅ **Yes** — Required for Vulkan linking and PNG export.

---

### 2. `spoutdxtoc.cpp` — Core Changes

#### A. Includes (NEW)
```cpp
+#include <strings.h>
+#include <winnt.h>
+#include <png.h>
+#define VK_USE_PLATFORM_WIN32_KHR 1
+#include <vulkan.h>
+#include <vulkan_win32.h>
```

**Needed?** ✅ **Yes** — Required for Vulkan/NT handle interop.

#### B. New Functions (NEW)

| Function | Lines | Purpose | Needed? |
|----------|-------|---------|---------|
| `SpoutDXToCGetDXDevice()` | 14 | Get DX11 device | ❓ Optional |
| `SpoutDXToCGetTexture()` | ~30 | Get shared texture handle | ✅ Yes |
| `SpoutDXToCGetMappedTexture()` | ~20 | Map texture for CPU access | ✅ Yes |
| `savePng()` | ~80 | Export RGBA to PNG file | ❌ **NOT NEEDED** |
| `findMemoryType()` | ~30 | Find GPU memory type | ✅ Yes (for Vulkan) |
| `exportVulkanTextureToPng()` | ~100 | Vulkan → PNG export | ❌ **NOT NEEDED** |
| `transitionImageLayout()` | ~25 | Vulkan layout helper | ✅ Yes (for Vulkan) |
| `SpoutDXToCGetVulkanHandle()` | ~80 | **Import NT handle to Vulkan** | ✅ **CRITICAL** |

#### C. Modified Functions (CHANGED)

| Function | Change | Needed? |
|----------|--------|---------|
| `SpoutDXToCGetSenderInfo()` | `shareHandle` type: `HANDLE` → `uint32_t` | ✅ Yes (fix) |
| `SpoutDXToCUpdateDXTexture()` | Removed `HandleToLong()` cast | ✅ Yes (fix) |

---

## Verdict: What's Actually Needed

### ✅ Essential (Keep)
- `SpoutDXToCGetVulkanHandle()` — **Core feature**: imports Win32 NT handle into Vulkan
- `SpoutDXToCGetTexture()` / `SpoutDXToCGetMappedTexture()` — Texture access
- `findMemoryType()` / `transitionImageLayout()` — Vulkan memory management
- Vulkan includes and dependencies

### ❌ Remove (Not Needed)
- `savePng()` — PNG export utility (not used by spout2pw)
- `exportVulkanTextureToPng()` — Vulkan → PNG export (not used)

---

## Recommendation

**Keep:** ~400 lines of Vulkan interop code  
**Remove:** ~180 lines of PNG export code

The PNG export functions were likely added during development but are not used by the main spout2pw project. They add ~180 lines of unused code.
