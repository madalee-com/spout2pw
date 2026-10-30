# Spout2PW Architecture

## System Design

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Windows Application (running under Wine)                               │
│                                                                         │
│  ┌──────────────┐   Spout2 API     ┌───────────────────────────────┐   │
│  │ DirectX App  │ ───────────────► │  spoutdxtoc.dll               │   │
│  │ (sender)     │                │  (DX texture capture wrapper)  │   │
│  └──────────────┘                └─────────────────────────────────┘   │
│                                         │                              │
│                                         ▼                              │
│                                  ┌───────────────┐                     │
│                                  │ spout2pw.c    │ (Wine PE service)  │
│                                  │ (Windows side)│                     │
│                                  └───────┬───────┘                     │
└──────────────────────────────────────────┼────────────────────────────┘
                                           │
                           __wine_unix_call (cross-boundary)
                                           │
┌──────────────────────────────────────────▼────────────────────────────┐
│  Native Linux Process                                                  │
│                                      ┌─────────────────────┐          │
│  ┌─────────────┐                     │ spout2pw_unix.c    │          │
│  │ PipeWire    │◄──────────────────►│ (Unix side)         │          │
│  │ Sink        │   funnel_stream   │                     │          │
│  └─────────────┘                     └────────┬──────────┘          │
│                                               │                      │
│                               ┌────────────────┴────────────────┐    │
│                               ▼                                 ▼    │
│                        Vulkan Pipeline              Vulkan Device  │
│                        (blit to stream)                (GPU)        │
│                         │                               │           │
│                         ▼                               ▼           │
│                  Imported Image                   DXVK Interop     │
│                  (from Wine handle)              (VK image extract) │
└─────────────────────────────────────────────────────────────────────┘
```

## Data Flow (Per Frame)

```
1. Spout2 sender publishes a new texture
                │
                ▼
2. sendernames_thread detects the sender
   → add_receiver(name)
                │
                ▼
3. update_receiver() calls SpoutDXToCUpdateDXTexture()
   → SpoutDXToCGetTexture() returns ID3D11Texture2D*
                │
                ▼
4. QueryInterface for IDXGIVkInteropSurface
   → GetVkImage() returns raw VkImage handle (uint64)
                │
                ▼
5. UNIX_CALL(create_source, ...) / update_source(...)
   → Unix side: funnel_stream_create + import_texture()
                │
                ▼
6. run_source() loop:
   a. Dequeue buffer from funnel stream
   b. Get Vulkan semaphores/fence from buffer
   c. Record command buffer:
      vkCmdBeginRenderPass(...)
      vkCmdBlitImage(src_image, dst_image, ...)
      vkCmdEndRenderPass(...)
   d. Submit command buffer to GPU queue
   e. Enqueue buffer to PipeWire
                │
                ▼
7. PipeWire delivers frame to sink (OBS, VLC, etc.)
```

## Thread Model

| Thread | Location | Purpose |
|--------|----------|---------|
| Main thread | Windows (Wine) | `WinMain` → `StartServiceCtrlDispatcherW` → wait loop |
| `sendernames_thread` | Windows (Wine) | Polls Spout2 sender list, manages receiver lifecycle |
| `receiver_thread` | Windows (Wine) | Per-receiver: calls `SpoutDXToCUpdateDXTexture`, `SpoutDXToCGetSenderInfo`, then dispatches to Unix side |
| `run_source` | Linux (native) | Per-source: main processing loop — dequeue, blit, submit |

## Cross-Boundary Calls

| Call Direction | Function | Purpose |
|----------------|----------|---------|
| Windows → Linux | `UNIX_CALL(startup, ...)` | Init Vulkan + PipeWire |
| Windows → Linux | `UNIX_CALL(create_source, ...)` | Create new PipeWire stream |
| Windows → Linux | `UNIX_CALL(update_source, ...)` | Signal texture update |
| Windows → Linux | `UNIX_CALL(destroy_source, ...)` | Stop and destroy source |
| Windows → Linux | `UNIX_CALL(teardown, ...)` | Shutdown Vulkan + PipeWire |
| Linux → Windows | `lock_texture` callback | Acquire Spout2 texture lock |
| Linux → Windows | `unlock_texture` callback | Release Spout2 texture lock |
| Linux → Windows | `_getenv` | Read environment variables (e.g., `SPOUT2PW_APPNAME`) |

## Memory Model

### DXVK Client-Object Translation

DXVK exposes Vulkan handles through a client-object layer. The Unix side must translate these:

```c
struct vulkan_client_object {
    uint64_t loader_magic;  // Validation token
    uint64_t unix_handle;   // Real Linux Vk* handle
};
```

The translation is done by casting the handle pointer back to this struct and reading `unix_handle`.

### Shared GPU Resource via IOCTLs

The code defines four IOCTL codes for querying shared GPU resources:

| IOCTL | Purpose |
|-------|---------|
| `IOCTL_SHARED_GPU_RESOURCE_GET_UNIX_RESOURCE` | Get Unix-side resource handle |
| `IOCTL_SHARED_GPU_RESOURCE_OPEN` | Open a shared resource |
| `IOCTL_SHARED_GPU_RESOURCE_GET_METADATA` | Get texture metadata |
| `IOCTL_SHARED_GPU_RESOURCE_GET_INFO` | Get resource info |

These were used in earlier versions (commented out) but the current implementation uses DXVK's interop surface instead.

## Build System

The project uses **Meson** with cross-compilation:

```
meson setup --cross-file=./x86_64-w64-mingw32.txt build/
ninja -C build/
```

### Targets

| Target | Type | Output | Description |
|--------|------|--------|-------------|
| `spout2pw` (windows) | `executable` | `build/spout2pw.exe` | Wine PE service (runs inside Wine) |
| `spout2pw` (unix) | `shared_library` | `build/libspout2pw.so` | Native Linux shared library |

### Subprojects

| Subproject | Purpose |
|------------|---------|
| `spoutdxtoc` | Spout2 DirectX capture library (git submodule) |
| `libfunnel` | PipeWire streaming wrapper for Vulkan content (git submodule) |

### Dependencies

| Dependency | Windows | Linux |
|------------|---------|-------|
| DXGI | `dxgi.lib` | — |
| D3D11 | `d3d11.lib` | — |
| NTLLD | `ntdll.lib` | — |
| UCRT | `ucrt.lib` | — |
| Wine CRT0 | `winecrt0.lib` | — |
| Vulkan | `vulkan-1.lib` | `vulkan` |
| libpng | `libpng16-16.dll` | — |
| spoutdxtoc | submodule | submodule |
| funnel | — | `funnel` + `funnel_vk` |

## See Also

- [WINE-INTEGRATION.md](WINE-INTEGRATION.md) — Detailed mechanics of how spout2pw.exe loads as a Wine builtin DLL, how `spout2pw.so` is found via `WINEDLLPATH`, service registration, and the c0000135 startup error.
- [PLAN-MULTI-SENDER.md](PLAN-MULTI-SENDER.md) — Analysis of multiple Spout sender support and the direct-VkImage texture access method vs the older DMA-BUF approach.
