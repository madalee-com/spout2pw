# Spout2PW Quick Reference

## Environment Variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `SPOUT2PW_APPNAME` | `Spout2PW` | PipeWire session application name |
| `SPOUT2PW_INSTANCE` | *(none)* | Custom PipeWire stream instance name |
| `SPOUT2PW_VALIDATION` | `0` | Enable Vulkan validation layers (`1` = on) |

## Key Types

### Windows Side (`spout2pw.c`)

```c
struct receiver {
    char *name;                              // Sender name
    void *source;                            // Unix-side source handle
    SPOUTDXTOC_RECEIVER *spout;             // Spout2 receiver object
    HANDLE thread;                           // Receiver processing thread
    struct source_info info;                 // Current texture metadata
    bool force_update;                       // Force re-create source
};

struct receiver **receivers;                 // Global receiver array
size_t num_receivers;                      // Number of active receivers
```

### Unix Side (`spout2pw_unix.c`)

```c
struct source {
    void *receiver;                    // Back-pointer to Windows receiver
    struct funnel_stream *stream;      // libfunnel stream handle
    VkCommandBuffer commandBuffer;     // Command buffer for blit
    VkDeviceMemory mem;                // Memory allocation (reserved)
    VkImage image;                     // Imported Vulkan image handle
    pthread_mutex_t lock;              // Protects source state
    pthread_cond_t cond;               // Wake-up condition
    bool quit;                         // Exit signal
    struct source_info info;           // Texture metadata
    int cur_fd;                        // Cached file descriptor
    uint32_t width, height;            // Stream dimensions
    bool update;                       // New data flag
    bool dead;                         // Cleanup complete flag
};
```

## Status Flags

| Constant | Value | Meaning |
|----------|-------|---------|
| `RECEIVER_DISCONNECTED` | `1 << 0` | No Spout2 sender connected |
| `RECEIVER_TEXTURE_UPDATED` | `1 << 1` | New texture frame available |
| `RECEIVER_TEXTURE_INVALID` | `1 << 2` | Texture handle no longer valid |
| `FRAME_IS_NEW` | `1 << 0` | Detected a new frame |

## Unix Call Functions

| Index | Function | Purpose |
|-------|----------|---------|
| 0 | `unix_getenv` | Read env var from Windows side |
| 1 | `unix_startup` | Initialize Vulkan + PipeWire |
| 2 | `unix_teardown` | Shutdown Vulkan + PipeWire |
| 3 | `unix_create_source` | Create a new PipeWire source |
| 4 | `unix_run_source` | Run the main processing loop |
| 5 | `unix_update_source` | Signal a texture update |
| 6 | `unix_destroy_source` | Stop and destroy a source |

## Common DX Formats Mapped to VK

| DX Format Name | Vulkan Format | SRGB? |
|----------------|---------------|-------|
| `DXGI_FORMAT_R32G32B32A32_FLOAT` | `VK_FORMAT_R32G32B32A32_SFLOAT` | No |
| `DXGI_FORMAT_R16G16B16A16_FLOAT` | `VK_FORMAT_R16G16B16A16_SFLOAT` | No |
| `DXGI_FORMAT_R16G16B16A16_UNORM` | `VK_FORMAT_R16G16B16A16_UNORM` | No |
| `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` | `VK_FORMAT_R8G8B8A8_SRGB` | Yes |
| `DXGI_FORMAT_B8G8R8A8_UNORM` | `VK_FORMAT_B8G8R8A8_SRGB` | Yes |
| `DXGI_FORMAT_B8G8R8X8_UNORM` | `VK_FORMAT_B8G8R8A8_SRGB` | Yes (no alpha) |

## Error Codes

| Code | Constant | Meaning |
|------|----------|---------|
| `0x80070005` | `E_ACCESSDENIED` | DXVK interop not available |
| `0x887A0001` | `DXGI_ERROR_UNSUPPORTED` | Format not supported by DXVK |
| `STATUS_NO_SUCH_DEVICE` | — | Source no longer valid (triggers restart) |

## Debug Macros

All debug output uses Wine's tracing system. Enable with:

```bash
WINEDEBUG=+spout2pw wine64 <app_that_uses_spout2>
```

| Macro | Severity |
|-------|----------|
| `TRACE(...)` | Verbose trace |
| `WARN(...)` | Warning |
| `ERR(...)` | Error |
| `FIXME(...)` | Unimplemented path |
