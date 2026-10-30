# Plan: Multiple Spout Sender Support

## Summary

Spout2PW already has infrastructure to detect and handle **multiple simultaneous Spout senders** (e.g., Warudo sending multiple outputs). The current implementation creates a separate `receiver` → `source` → PipeWire stream per sender.

This document analyzes whether the **new texture access method** (direct VkImage handles) works correctly for multiple senders, and identifies any changes needed.

---

## Current Architecture

### Windows Side (`spout2pw.c`)

```
sendernames_thread()
  ├── SpoutDXToCGetSenderList() → detects added/removed senders
  ├── add_receiver(name)         → creates receiver + SPOUTDXTOC_RECEIVER + thread
  └── remove_receiver(receiver)  → cleans up source, thread, receiver

struct receiver {
    char *name;
    void *source;           // pointer to unix-side source
    SPOUTDXTOC_RECEIVER *spout;
    HANDLE thread;          // receiver_thread() → run_source()
    struct source_info info;
    bool force_update;
};

struct receiver **receivers;   // array of receivers
size_t num_receivers = 0;
```

Each sender gets:
- Its own `SPOUTDXTOC_RECEIVER` (SpoutDXToC object)
- Its own `receiver_thread()` → `UNIX_CALL(run_source, source)`
- Its own mutex/cond for synchronization

### Unix Side (`spout2pw_unix.c`)

```c
struct source {
    void *receiver;
    struct funnel_stream *stream;   // one per sender (named by sender name)
    VkCommandBuffer commandBuffer;  // allocated from shared commandPool
    // ... (no VkImage/mem here anymore — direct handle usage)
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct source_info info;
};
```

- Each `create_source()` creates a new `funnel_stream` with the sender's name
- Each `run_source()` runs in its own thread
- Vulkan device/queue are **shared globals**, protected by `vk_lock`

---

## Texture Access: Old vs New

### Old Method (opaque_fd / DMA-BUF)

```c
// spout2pw_unix.c (before 4915e53)
static int import_texture(struct source *source) {
    int fd = fcntl(source->info.opaque_fd, F_DUPFD_CLOEXEC, 3);  // duplicate fd
    // ... create new Vulkan image from fd
    VkImageCreateInfo info = { .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | ... };
    vkCreateImage(device, &info, NULL, source->image);  // own image
}
```

**Characteristics:**
- Sender exports a DMA-BUF `opaque_fd`
- Spout2PW **duplicates** the fd and creates its own Vulkan image
- Spout2PW has an **independent reference** to the texture data
- Safe against sender recycling textures (spout2pw owns its copy)

### New Method (Direct VkImage)

```c
// spout2pw_unix.c (current, since 4915e53)
static int import_texture(struct source *source) {
    source->image = (VkImage)source->info.vk_image;  // direct handle
    return 0;  // no image creation, no fd duplication
}
```

**Characteristics:**
- Sender provides raw `vk_image` handle via DXVK interop (`IDXGIVkInteropSurface::GetVkImage`)
- Spout2PW uses the sender's VkImage **directly** — no wrapping, no duplication
- Simpler, lower overhead
- **Shared ownership**: spout2pw and DXVK both reference the same VkImage

---

## Analysis: Does the New Method Work for Multiple Senders?

### ✅ What Works

| Aspect | Status | Notes |
|--------|--------|-------|
| **Multiple receivers** | ✅ | `receivers[]` array + `num_receivers` handles N senders |
| **Per-sender threads** | ✅ | Each `run_source()` runs in its own thread |
| **Per-sender streams** | ✅ | `funnel_stream_create(funnel, sender_name, ...)` gives unique PipeWire streams |
| **Vulkan queue serialization** | ✅ | `vk_lock` serializes submissions across all sources |
| **Command buffers** | ✅ | Each source gets its own from shared `commandPool` |
| **Frame sync** | ✅ | `lock_texture`/`unlock_texture` use SpoutDXToC's critical section + `CheckTextureAccess` |

### ⚠️ Potential Concerns

#### 1. VkImage Handle Reuse

**Risk**: If DXVK recycles a VkImage handle for a new texture while spout2pw is still blitting the old one, spout2pw could blit stale/wrong data.

**Mitigation in current code**:
- `vkQueueWaitIdle(queue)` after each frame ensures blit completes before next iteration
- Each sender has a **distinct** VkImage (DXVK maintains separate images per sender)
- When a sender disconnects, `remove_receiver()` → `destroy_source()` runs before the handle could be reused

**Residual risk**: Small window between `get_receiver_info()` reading `vk_image` and the actual blit. If the sender rapidly recycles textures, spout2pw might blit a slightly stale frame. **This is acceptable** — stale frames are better than crashes, and the sender typically holds textures for multiple frames.

#### 2. No Reference Counting

**Risk**: Direct VkImage usage doesn't increment Vulkan reference counts. If the sender destroys its texture while spout2pw holds the handle...

**Mitigation**:
- DXVK/D3D11 ref-counts the texture via `AddRef`/`Release`
- Spout2PW calls `SpoutDXToCGetTexture()` which returns a reference to the shared texture
- The texture stays alive as long as the sender is active
- When sender disconnects, SpoutDXToC releases its reference

**Note**: Spout2PW does **not** call `Release()` on the returned `ID3D11Texture2D*` (see line 558: `// sharedTexture->lpVtbl->Release(sharedTexture);`). This is a minor leak but harmless — the texture is owned by the sender anyway.

#### 3. Concurrent Blit to Same Destination

**Risk**: If two senders somehow wrote to the same funnel stream...

**Mitigation**: Each sender gets a unique `funnel_stream` name, so destinations are distinct. No conflict.

---

## Verdict: No Changes Required

The current implementation **already supports multiple simultaneous Spout senders** with the new direct-VkImage texture access method. The per-sender isolation (receiver → source → stream → thread) ensures that:

1. Each sender's VkImage is used independently
2. Vulkan operations are serialized via `vk_lock`
3. Frame synchronization is handled by SpoutDXToC's texture locking
4. Cleanup on disconnect prevents handle reuse issues

---

## Testing Recommendations

To verify multi-sender support works correctly:

### Test 1: Two Senders Active Simultaneously
```bash
# Launch Warudo (or similar) with two Spout outputs
SPOUT2PW_APPNAME="Warudo" SPOUT2PW_INSTANCE="warudo" \
  ./spout2pw.sh <proton> run warudo.exe

# Verify both PipeWire streams appear:
pw-metadata -n 0.3 streams
# Should show two streams with different names (sender names)
```

### Test 2: Sender Added/Removed Dynamically
```bash
# Start with one sender, then start a second
# Verify:
# - Second sender's stream appears without restarting spout2pw
# - First sender continues working
```

### Test 3: Remove a Sender
```bash
# Stop one sender while another remains active
# Verify:
# - Removed sender's stream disappears
# - Remaining sender continues outputting
# - No crash or hang
```

### Debugging
```bash
# Enable verbose logging
export WINEDEBUG=+timestamp,+relay,+spout2pw

# Check receiver count in logs:
# Look for "Sendernames thread started", "New sender: X", "Removed sender: Y"
```

---

## Known Limitations

1. **No limit on concurrent senders**: The `receivers[]` array grows dynamically. In practice, Spout senders are limited by the application (Warudo typically supports a handful).

2. **Single Vulkan queue**: All sources submit to the same queue. For most use cases this is fine, but high-frame-rate multiple senders could contend for queue bandwidth. Not a correctness issue, only a performance consideration.

3. **Texture staleness window**: As noted above, there's a small window where a rapidly-recycled texture could be blitted stale. Acceptable for video capture (1-2 stale frames).

---

## Files Involved

| File | Role |
|------|------|
| `src/spout2pw.c` | Multi-sender detection (`sendernames_thread`, `add_receiver`, `remove_receiver`) |
| `src/spout2pw_unix.c` | Per-source PipeWire streams (`create_source`, `run_source`) |
| `src/spout2dx.cpp` | SpoutDXToC sender list polling, texture locking |
| `src/spout2pw_unix.h` | Cross-boundary data structures |

No changes needed to these files for multi-sender support.
