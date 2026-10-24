# Dead Code Audit & Removal Plan

This document identifies code in the project that is no longer needed and provides a plan for safe removal.

---

## Summary

| Category | Lines Removed | Files Affected |
|----------|--------------|----------------|
| Unused IOCTL defines + include | ~12 lines | spout2pw.c |
| Unused struct definitions | ~15 lines | spout2pw.c |
| Unused enum `D3D11_TEXTURE_LAYOUT` | ~6 lines | spout2pw.c |
| Unused function `find_memory_type_index` | ~22 lines | spout2pw.c |
| Unused function `save_buffer_to_png` | ~90 lines | spout2pw.c |
| Dead GUID constants | ~16 lines | spout2pw.c |
| Unused function `get_handle_from_legacy_texture` | ~26 lines | spout2pw.c |
| Large commented-out blocks (spout2pw.c) | ~190 lines | spout2pw.c |
| Large commented-out block (spout2pw_unix.c) | ~405 lines | spout2pw_unix.c |
| Duplicate format registration | ~6 lines | spout2pw_unix.c |
| **Total estimated** | **~808 lines** | **2 files** |

---

## Priority 1: Remove Unused Defines & Structs (spout2pw.c)

### 1a. IOCTL definitions and `<winioctl.h>` include

**Lines 44–54 in spout2pw.c:**
```c
#define IOCTL_SHARED_GPU_RESOURCE_GET_UNIX_RESOURCE ...
#define IOCTL_SHARED_GPU_RESOURCE_OPEN               ...
#define IOCTL_SHARED_GPU_RESOURCE_GET_METADATA       ...
#define IOCTL_SHARED_GPU_RESOURCE_GET_INFO           ...
```

**Line 17:** `#include <winioctl.h>`

These were used in an earlier approach that queried the Windows video driver via IOCTLs to get a Unix file descriptor for the shared GPU resource. The current code uses DXVK interop instead. No references found in active code.

**Action:** Remove lines 44–54 and line 17 (`#include <winioctl.h>`).

### 1b. `struct shared_resource_open` and `struct shared_resource_info`

**Lines 68–75 in spout2pw.c:**
```c
struct shared_resource_open { ... };
struct shared_resource_info { ... };
```

Never instantiated or referenced in active code. The old IOCTL approach used these structs; the DXVK interop path does not.

**Action:** Remove lines 68–75.

### 1c. `D3D11_TEXTURE_LAYOUT` enum

**Lines 77–81 in spout2pw.c:**
```c
typedef enum D3D11_TEXTURE_LAYOUT { ... } D3D11_TEXTURE_LAYOUT;
```

This type appears in the dead `DxvkSharedTextureMetadata` struct (line 94), but that struct is only used inside commented-out code. In the active path, `DXVK_Image_Layout_Private` (lines 105–111) is used instead and does not reference this enum.

**Action:** Remove lines 77–81. The `DxvkSharedTextureMetadata.TextureLayout` field at line 94 should be removed along with the struct if it's only used in dead code — see section 3 below.

### 1d. Unused GUID constants

**Lines 228–229 and 250–259:**
```c
static const GUID IID_IDXGIResource_C = { ... };     // line 228
static const GUID IID_IDXGIResource1_Real = { ... };  // line 250
static const GUID IID_IDXGIResource_Legacy = { ... }; // line 255
```

- `IID_IDXGIResource_C` — never referenced in active code
- `IID_IDXGIResource1_Real` — never referenced in active code  
- `IID_IDXGIResource_Legacy` — only used in the dead `get_handle_from_legacy_texture` function (see 1e)

**Action:** Remove lines 228–229, 250–259. Keep `IID_IDXGIVkInteropSurface_C`, `IID_ID3D11Device1_C`, and `IID_ID3D11Resource_C` — they are actively used.

---

## Priority 2: Remove Unused Functions (spout2pw.c)

### 2a. `find_memory_type_index`

**Lines 521–542 in spout2pw.c:**
```c
uint32_t find_memory_type_index(VkPhysicalDevice physicalDevice,
                                PFN_vkGetPhysicalDeviceMemoryProperties getPropsFunc,
                                uint32_t typeBitsFilter, uint32_t requiredProperties) { ... }
```

Only called at line 870, which is inside a commented-out block (lines 808–995). The active code uses `FindMemoryType` instead.

**Action:** Remove lines 521–542.

### 2b. `save_buffer_to_png`

**Lines 545–635+ in spout2pw.c:**
```c
void save_buffer_to_png(VkDevice vkDevice, ...) { ... }
```

Only called at line 978 inside a commented-out block. No other references. This was a debugging function for extracting raw Vulkan memory to PNG.

**Action:** Remove the entire function definition (lines 545–635+).

---

## Priority 3: Remove Commented-Out Code Blocks

### 3a. spout2pw.c — Large dead block (lines ~808–995)

This is an entirely commented-out alternative implementation that:
- Loaded `winevulkan.dll` dynamically
- Resolved Vulkan function pointers manually
- Created D3D11 staging buffers and copied image data via `vkCmdCopyImageToBuffer`
- Saved debug PNGs to disk

All of this was superseded by the DXVK interop approach (lines 785–807, which is active). The block spans from `/*` at line ~808 through `*/` around line 995.

**Action:** Delete lines ~808–995 (the entire commented block).

### 3b. spout2pw_unix.c — Large dead block (lines ~590–995)

This is an entirely commented-out alternative implementation that:
- Created a Vulkan device from scratch (instead of using the DXVK-provided handle)
- Used OPAQUE_FD external memory import with `vkImportMemoryFdKHR`
- Handled memory allocation and image binding manually
- Had the full texture import pipeline

The active code (lines 640–735) uses a simplified approach: it calls `funnel_stream_create`, then sets up the stream with Vulkan formats, and the actual import happens in `import_texture` which now just casts the handle directly. The large commented block shows the old OPAQUE_FD-based approach.

**Action:** Delete lines ~590–995 (the entire commented block). Also remove the associated label labels `err_close:`, `free_stream:`, `free_cmdbufs:`, `free_source:` that were only used within the dead block — but verify they're not referenced elsewhere first.

---

## Priority 4: Fix Minor Issues in Active Code (spout2pw_unix.c)

### 4a. Duplicate format registration

**Lines 744–751:**
```c
ret = funnel_stream_vk_add_format(stream, VK_FORMAT_R8G8B8A8_UNORM, false,
                                  VK_FORMAT_FEATURE_BLIT_DST_BIT);
have_format |= ret == 0;
ret = funnel_stream_vk_add_format(stream, VK_FORMAT_R8G8B8A8_SRGB, false,
                                  VK_FORMAT_FEATURE_BLIT_DST_BIT);
have_format |= ret == 0;
```

These two calls are duplicates of lines 741–743 and 750–752 (different format variants but the same call). Specifically:
- Line 744–746 (`R8G8B8A8_UNORM, false`) duplicates line 741–743 (`R8G8B8A8_SRGB, true`) in terms of pattern — both add a format. But they're actually different formats, so this is intentional.
- Line 747–749 (`R8G8B8A8_UNORM, false`) is an exact duplicate of line 744–746.

**Action:** Remove lines 747–749 (the duplicate `R8G8B8A8_UNORM` registration).

### 4b. Dead label references

After removing the large commented block in spout2pw_unix.c, verify that labels `err_close:`, `free_stream:`, `free_cmdbufs:`, and `free_source:` are either:
- Removed along with the dead code that references them, or
- Kept if they're still used by active code (check the current `create_source` function)

---

## Priority 5: Clean Up Includes

### 5a. spout2pw.c — Remove `<png.h>`?

**Line 6:** `#include <png.h>`

This is used by both `SavePNG` (line 452, active) and `save_buffer_to_png` (removed in Priority 2b). Since `SavePNG` is still called at line 1068, **keep** `<png.h>`.

### 2c. `get_handle_from_legacy_texture` (spout2pw.c)

**Lines 261–286 and lines 1272–1278:**
```c
HANDLE get_handle_from_legacy_texture(ID3D11Texture2D *pTexture) { ... }
```

This function is defined at line 261 but only called at lines 1273 and 1276, both inside a `/* ... */` comment block. The active code path (line 1279) uses `flatSrcVkImage` instead.

**Action:** Remove lines 261–286 (the function definition). Also remove the dead call at lines 1272–1278 as part of Priority 3a (removing the large commented block).

---

## Priority 5: Clean Up Includes

All includes in spout2pw_unix.c appear to be actively used:
- `<funnel-vk.h>` and `<funnel.h>` — used throughout
- `"wine/debug.h"` — used for TRACE/WARN/ERR macros
- `<ntstatus.h>` — used for NTSTATUS types
- `<vulkan/vulkan.h>` and `<vulkan/vulkan_core.h>` — used extensively

**Action:** No changes needed for includes in spout2pw_unix.c.

---

## Verification Plan

After each removal, verify:

1. **Build passes:** Run `meson setup --cross-file=./x86_64-w64-mingw32.txt build/ && ninja -C build/`
2. **No unused-variable warnings:** Add `-Werror=unused-variable -Werror=unused-function` to compiler flags temporarily and confirm zero warnings
3. **Runtime behavior unchanged:** The bridge should still capture Spout2 senders and push frames to PipeWire

---

## Execution Order

| Step | Action | Risk |
|------|--------|------|
| 1 | Remove IOCTL defines, `<winioctl.h>`, unused structs (Priority 1a–1c) | Low — purely dead code |
| 2 | Remove dead GUIDs and `find_memory_type_index` (Priority 1d, 2a) | Low — dead code |
| 3 | Remove `save_buffer_to_png` (Priority 2b) | Low — dead function |
| 4 | Remove large commented blocks (Priority 3) | Medium — verify no orphaned references |
| 5 | Fix duplicate format registration (Priority 4a) | Low — simple dedup |
| 6 | Build and verify (Verification Plan) | Required after each step |

---

## Notes

- **Do not remove** `SavePNG` — it's called from active code at line 1068 in `get_receiver_info`
- **Do not remove** `get_handle_from_legacy_texture` unless we confirm line 1273 (`HandleToLong(get_handle_from_legacy_texture(...))`) is also dead — it appears to be in a non-commented path but the function body may be unreachable due to the `#if 0` / `#else` blocks around it. Verify before removing.
- **Do not remove** `DXVK_Image_Layout_Private` — it's actively used at line 798
- The commented-out code in both files contains valuable debugging logic (manual Vulkan init, FD-based memory import). Consider moving useful snippets to a `debug/` directory or git history rather than deleting them entirely.
