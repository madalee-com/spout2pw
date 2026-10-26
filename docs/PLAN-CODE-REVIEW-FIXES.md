# Code Review Fix Plan

**Based on:** `docs/REVIEW.md`  
**Created:** 2025-10-25  
**Status:** Ready for implementation

---

## Phase 1: Critical Fixes (High Priority)

### Task 1.1: Add Include Guard to Header
**File:** `src/spout2pw_unix.h`  
**Lines:** 1  
**Risk:** Low  

```c
// Add at line 1:
#pragma once
```

---

### Task 1.2: Fix Teardown Race Condition
**File:** `src/spout2pw_unix.c`  
**Lines:** 1185-1197  
**Risk:** Medium (requires synchronization)  

**Steps:**
1. Add global flag `static bool teardown_in_progress = false;`
2. Protect with `vk_lock` in `teardown()`
3. Check flag at start of `run_source()` and return early if true
4. Set flag after all threads joined

---

### Task 1.3: Fix Unsafe Function Pointer Casts
**File:** `src/spout2pw_unix.h` and `src/spout2pw.c`  
**Lines:** 70-77, 440-445  
**Risk:** High (undefined behavior)  

**Steps:**
1. Change `struct startup_params` to store function pointers directly:
   ```c
   struct startup_params {
       NTSTATUS (WINAPI *lock_texture)(void *args, ULONG size);
       NTSTATUS (WINAPI *unlock_texture)(void *args, ULONG size);
       // ... remove UINT64 fields
   };
   ```
2. Update all references to use pointer syntax

---

## Phase 2: High Priority Fixes

### Task 2.1: Standardize Boolean Usage
**Files:** `src/spout2pw.c`, `src/spout2pw_unix.c`  
**Lines:** 213-214, 806 (c), 79-90 (unix)  

**Steps:**
1. Replace all `BOOL` with `bool` in `spout2pw.c`
2. Replace all `FALSE`/`TRUE` with `false`/`true`
3. Keep `BOOL` only where Wine API requires it

---

### Task 2.2: Remove Redundant Format Registration
**File:** `src/spout2pw_unix.c`  
**Lines:** 728-748  
**Risk:** Low  

**Steps:**
1. Remove duplicate `VK_FORMAT_R8G8B8A8_UNORM` registration
2. Remove duplicate `VK_FORMAT_R8G8B8A8_SRGB` registration
3. Keep one of each with correct alpha parameter

---

### Task 2.3: Standardize Error Return Types
**File:** `src/spout2pw_unix.c`  
**Lines:** 635-667  
**Risk:** Low  

**Steps:**
1. Decide on consistent return type (recommend `-errno` for Unix side)
2. Update all error returns to match
3. Remove `errno_to_status()` wrapper if no longer needed

---

## Phase 3: Medium Priority Improvements

### Task 3.1: Define Constants for Magic Numbers
**Files:** Multiple  
**Lines:** 881 (c), 46-48 (h)  

**Steps:**
1. Add `#define MAX_WINEDLLDIR 100` to `spout2pw.c`
2. Convert bit flags to `enum` in header

---

### Task 3.2: Add Comments for Wine-Specific Code
**File:** `src/spout2pw_unix.c`  
**Lines:** 122-141  
**Risk:** Low  

**Steps:**
1. Add inline comments explaining pointer casts are intentional Wine interop
2. Link to Wine documentation if available

---

### Task 3.3: Add File-Level Documentation
**Files:** All three source files  
**Risk:** Low  

**Steps:**
1. Add header comment to each file with:
   - Purpose
   - Build requirements
   - Author/maintainer
2. Document public functions with Doxygen-style comments

---

## Phase 4: Cleanup (Low Priority)

### Task 4.1: Remove Commented-Out Code
**Files:** `spout2pw.c`, `spout2pw_unix.c`  
**Lines:** 19-20, 37-38, 407-446  

**Steps:**
1. Delete all commented-out includes and code blocks
2. Keep only if actively needed for debugging

---

### Task 4.2: Standardize Naming Conventions
**File:** `src/spout2pw_unix.c`  
**Lines:** 77-107  
**Risk:** Low  

**Steps:**
1. Prefix structs consistently (e.g., `spout2pw_source`, `spout2pw_vulkan_*`)
2. Update all references

---

## Verification Plan

After each phase, verify:

1. **Build test:** `CCACHE_DISABLE=1 ninja -C build/` passes
2. **Link test:** Both `.so` and `.exe` link without warnings
3. **Static analysis:** Run `clang-tidy` or equivalent if available
4. **Runtime test:** Start service, verify no crashes on shutdown

---

## Estimated Effort

| Phase | Files | Lines | Effort |
|-------|-------|-------|--------|
| 1: Critical | 3 | ~50 | 2-3 hours |
| 2: High | 2 | ~30 | 1-2 hours |
| 3: Medium | 3 | ~40 | 1 hour |
| 4: Cleanup | 2 | ~60 | 30 min |
| **Total** | **3** | **~120** | **4.5-6 hours** |

---

## Implementation Order

1. **Phase 1** (do first) — prevents crashes/UB
2. **Phase 2** (next) — improves correctness
3. **Phase 3** (after build stable) — improves maintainability
4. **Phase 4** (last) — cleanup, no functional change

---

*Plan ready for execution. Each task is independent and can be committed separately.*
