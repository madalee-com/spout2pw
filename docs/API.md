# Spout2PW API Reference

This document describes every exported function, callback, and structure in the project.

---

## Shared Structures (`spout2pw_unix.h`)

### `struct receiver_params`

Parameters passed to the texture lock/unlock callbacks on the Unix side.

```c
struct receiver_params {
    struct dispatch_callback_params dispatch;  // Callback address for Wine dispatch
    void *receiver;                            // Opaque receiver handle
};
```

### `struct source_info`

Carries texture metadata between the Windows and Unix sides.

| Field | Type | Description |
|-------|------|-------------|
| `resource_size` | `uint64_t` | Size of the shared GPU resource (bytes) |
| `flags` | `uint32_t` | Status flags (see below) |
| `width` | `uint32_t` | Texture width in pixels |
| `height` | `uint32_t` | Texture height in pixels |
| `format` | `uint32_t` | DirectX pixel format enum |
| `usage` | `uint32_t` | D3D11 usage flag (e.g., `STAGING`, `RENDER_TARGET`) |
| `bind_flags` | `uint32_t` | D3D11 bind flags (SHADER_RESOURCE, RENDER_TARGET, etc.) |
| `vk_image` | `uint64_t` | Raw Vulkan image handle (`VkImage` cast to uint64) |

**Flags**

| Constant | Value | Meaning |
|----------|-------|---------|
| `RECEIVER_DISCONNECTED` | `1 << 0` | Spout2 receiver lost connection to sender |
| `RECEIVER_TEXTURE_UPDATED` | `1 << 1` | New texture frame is available |
| `RECEIVER_TEXTURE_INVALID` | `1 << 2` | Current texture handle is no longer valid |

### `struct lock_texture_return`

Return value from the texture-lock callback.

| Field | Type | Description |
|-------|------|-------------|
| `dispatch` | `struct dispatch_callback_params` | Callback info (echoed back) |
| `retval` | `int32_t` | `0` on success, `-1` on failure |
| `flags` | `uint32_t` | Status flags |
| `frame_count` | `uint64_t` | Current frame count from Spout2 |

**Flags**

| Constant | Value | Meaning |
|----------|-------|---------|
| `FRAME_IS_NEW` | `1 << 0` | A new frame was detected |

### `struct startup_params`

Passed to `UNIX_CALL(startup, ...)` — initializes the Unix-side Vulkan + PipeWire pipeline.

| Field | Type | Description |
|-------|------|-------------|
| `lock_texture` | `UINT64` | Address of the lock callback (Windows side) |
| `unlock_texture` | `UINT64` | Address of the unlock callback (Windows side) |
| `error_msg` | `char *` | Output: error message string on failure |
| `vkInstance` | `uint64_t` | Vulkan instance handle (from DXVK interop) |
| `vkPhysDev` | `uint64_t` | Vulkan physical device handle |
| `vkDevice` | `uint64_t` | Vulkan logical device handle |

### `struct create_source_params`

Parameters for `UNIX_CALL(create_source, ...)`.

| Field | Type | Description |
|-------|------|-------------|
| `sender_name` | `const char *` | Spout2 sender name |
| `receiver` | `void *` | Opaque receiver handle |
| `info` | `struct source_info` | Texture metadata from the sender |
| `ret_source` | `void *` | **Output**: created source pointer (filled on success) |
| `error_msg` | `char *` | Output: error message string on failure |

### `struct update_source_params`

Parameters for `UNIX_CALL(update_source, ...)`.

| Field | Type | Description |
|-------|------|-------------|
| `source` | `void *` | Existing source handle |
| `info` | `struct source_info` | New texture metadata |

### `struct getenv_params`

Parameters for `UNIX_CALL(getenv, ...)`.

| Field | Type | Description |
|-------|------|-------------|
| `var` | `const char *` | Environment variable name |
| `val` | `const char *` | **Output**: value (or `NULL`) |

---

## Unix Call Enum (`spout2pw_unix.h`)

```c
enum spout2pw_funcs {
    unix_getenv,       // 0 — read an environment variable
    unix_startup,      // 1 — initialize Vulkan + PipeWire pipeline
    unix_teardown,     // 2 — destroy Vulkan + PipeWire resources
    unix_create_source,// 3 — create a new PipeWire stream source
    unix_run_source,   // 4 — run the source main loop (blocking thread)
    unix_update_source,// 5 — update an existing source with new texture data
    unix_destroy_source,// 6 — signal a source to stop and clean up
    unix_funcs_count   // sentinel
};
```

The `UNIX_CALL(func, params)` macro dispatches to the corresponding function via Wine's `WINE_UNIX_CALL`.

---

## Windows-Side Functions (`spout2pw.c`)

### Entry Point

#### `int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)`

The process entry point. Validates that Spout2 is configured in `WINEDLLPATH`, then starts the Windows service dispatcher.

**What it does:**
1. Scans `WINEDLLPATH0`–`WINEDLLPATH99` for a path ending in `\spout2pw-dlls`
2. If not found, logs an error and exits (Spout2 is not properly configured)
3. Registers `ServiceMain` with the Windows Service Control Manager
4. Blocks on `WaitForMultipleObjects` until the service receives a stop/shutdown signal

---

### Service Management

#### `static void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)`

The Wine service entry point. Orchestrates the entire lifetime of the bridge.

**Steps:**
1. Registers the service control handler (`service_handler`)
2. Initializes Wine's Unix call interface (`__wine_init_unix_call`)
3. Creates a `spoutdxtoc` sender-names object to monitor Spout2 senders
4. Starts `sendernames_thread` to watch for sender add/remove events
5. Enters a wait loop on `exit_event`
6. On stop: joins threads, calls `UNIX_CALL(teardown)`, releases D3D11 device

**Restart behavior:** If `do_restart` is set (triggered by a fatal error), the service loops back to `restart:` and re-initializes.

---

#### `static DWORD WINAPI service_handler(DWORD ctrl, DWORD event_type, LPVOID event_data, LPVOID context)`

Wine service control handler.

| Control | Action |
|---------|--------|
| `SERVICE_CONTROL_STOP` | Sets `exit_event`, signals service to stop |
| `SERVICE_CONTROL_SHUTDOWN` | Same as STOP |
| Anything else | Logs a FIXME and ignores |

---

### Sender Monitoring

#### `static DWORD WINAPI sendernames_thread(void *arg)`

Main monitoring loop. Runs in its own Win32 thread.

**Loop behavior:**
1. Calls `SpoutDXToCGetSenderList` to poll for changes
2. For each **removed** sender: finds the matching receiver and calls `remove_receiver`
3. For each **added** sender: calls `add_receiver` to create a new receiver
4. Calls `update_receivers()` to refresh all active receivers
5. Sleeps 100ms between polls (via `WaitForSingleObject(exit_event, 100)`)

On exit, iterates through all remaining receivers and calls `remove_receiver` for each.

---

### Receiver Management

#### `static void add_receiver(const char *name)`

Creates a new `struct receiver` and registers it in the global `receivers[]` array.

**Steps:**
1. Calls `SpoutDXToCNewReceiver(name)` to create a Spout2 receiver
2. Allocates and zero-initializes a `struct receiver`
3. Stores the receiver name, Spout handle, and nulls `source` and `thread`
4. Appends to the dynamic `receivers[]` array

#### `static void remove_receiver(struct receiver *receiver)`

Destroys a receiver and removes it from the global array.

**Steps:**
1. Calls `UNIX_CALL(destroy_source, receiver->source)` to signal the Unix thread to stop
2. Waits for the receiver thread to exit (`WaitForSingleObject`)
3. Calls `SpoutDXToCFreeReceiver` to free the Spout2 receiver
4. Removes itself from the `receivers[]` array via `memmove`
5. Frees the receiver's name and the struct

---

### Receiver Updates

#### `static void update_receivers(void)`

Iterates over all registered receivers and calls `update_receiver` on each.

#### `static void update_receiver(struct receiver *receiver)`

Checks if the receiver's Spout2 connection is still active and whether the texture has changed.

**Logic:**
- If `receiver->source` is null and a new texture is available:
  - Calls `UNIX_CALL(create_source, &params)` to create a PipeWire source
  - Starts a new Win32 thread via `receiver_thread`
- If `receiver->source` exists and the flags changed:
  - Calls `UNIX_CALL(update_source, &params)` with the new metadata
  - If the update returns `STATUS_NO_SUCH_DEVICE`, triggers a service restart

#### `static struct receiver *find_receiver(const char *name)`

Linear search through the `receivers[]` array for a receiver whose name matches.

---

### Texture Capture (Windows side)

#### `static struct source_info get_receiver_info(struct receiver *receiver)`

The core texture capture function. Extracts a Vulkan image handle from a Spout2 sender's D3D11 texture via DXVK interop.

**Steps:**
1. Calls `SpoutDXToCIsConnected` — if not connected, returns `RECEIVER_DISCONNECTED`
2. Calls `SpoutDXToCGetSenderInfo` to get width, height, format, usage, share handle
3. Calls `SpoutDXToCUpdateDXTexture` to refresh the texture
4. Calls `SpoutDXToCGetTexture` to obtain the `ID3D11Texture2D *`
5. Queries the texture description (`GetDesc`) for bind flags and format
6. **Key step:** Calls `QueryInterface` for `IDXGIVkInteropSurface` on the texture
7. Calls `GetVkImage` on the interop surface to extract the raw `VkImage` handle as a `uint64_t`
8. Sets `RECEIVER_TEXTURE_UPDATED` in the flags and returns

**Note:** Much of the commented-out code shows alternative approaches that were tried but not used (D3D11 staging textures, raw Vulkan import via Win32 NT handles, etc.). The final approach uses DXVK's internal interop surface.

---

### DXVK Interop Types

The following flat vtable structures are defined to avoid pulling in full DirectX/Vulkan headers:

| Type | Purpose |
|------|---------|
| `IDXGIVkInteropSurfaceVtbl` / `struct IDXGIVkInteropSurface` | Extract raw `VkImage` from a D3D11 texture |
| `IDXGIVkInteropDevice1Vtbl` / `struct IDXGIVkInteropDevice` | Get Vulkan handles and transition image layouts |
| `DXVK_Image_Layout_Private` | Parse DXVK's internal tiled/linear layout metadata |

---

### Vulkan Helper Functions (Windows side)

#### `static uint32_t FindMemoryType(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties)`

Finds the first memory type index that matches a bit mask and has the requested property flags (e.g., `HOST_VISIBLE \| HOST_COHERENT`).

#### `static uint32_t find_memory_type_index(...)`

Similar to above but takes function pointers for `vkGetPhysicalDeviceMemoryProperties` — used when avoiding direct Vulkan header dependencies.

#### `int SavePNG(const char *filename, unsigned char *buffer, int width, int height, int stride)`

Saves raw RGBA pixel data to a PNG file using libpng. Used in debugging paths (not in the main flow).

#### `void save_buffer_to_png(VkDevice vkDevice, PFN_vkGetDeviceProcAddr getDeviceProc, uint64_t flatMemoryHandle, uint32_t width, uint32_t height)`

Maps Vulkan device memory, reads pixel data, and writes a PNG. Uses dynamically resolved Vulkan function pointers to avoid structure-pointer crashes. Debug-only path.

---

### Startup (Windows side)

#### `static bool startup(SPOUTDXTOC_RECEIVER *spout)`

Initializes the cross-component bridge: queries the D3D11 device from Spout2, obtains the DXVK interop interface, extracts Vulkan handles, and calls `UNIX_CALL(startup, &params)` to initialize the Linux-side pipeline.

**Steps:**
1. Calls `SpoutDXToCGetDXDevice` to get `ID3D11Device *`
2. Gets the immediate D3D11 context
3. Queries `IDXGIVkInteropDevice` from the D3D11 device
4. Calls `GetVulkanHandles` to get `VkInstance`, `VkPhysicalDevice`, `VkDevice`
5. Calls `UNIX_CALL(startup, &params)` with lock/unlock callbacks and Vulkan handles

---

### Callbacks (Windows side)

#### `static NTSTATUS WINAPI lock_texture(void *args, ULONG size)`

Called from the Unix side to acquire the Spout2 texture lock before reading.

**Returns:** `STATUS_SUCCESS` on success, or `STATUS_UNSUCCESSFUL` if the lock fails.

#### `static NTSTATUS WINAPI unlock_texture(void *args, ULONG size)`

Called from the Unix side to release the Spout2 texture lock after reading.

---

### Error Handling

#### `void show_error(HRESULT res, const char *msg)`

Displays a message box with the error code and a troubleshooting URL (`https://lina.yt/s2pw-error`). Also kicks the service status to prevent auto-close.

---

## Unix-Side Functions (`spout2pw_unix.c`)

### Global State

| Variable | Type | Purpose |
|----------|------|---------|
| `funnel` | `struct funnel_ctx *` | libfunnel context (PipeWire session) |
| `instance` | `VkInstance` | Vulkan instance (from DXVK interop) |
| `physDevice` | `VkPhysicalDevice` | Selected Vulkan physical device |
| `device` | `VkDevice` | Vulkan logical device |
| `queue` | `VkQueue` | Graphics queue for command submission |
| `commandPool` | `VkCommandPool` | Pool for allocating command buffers |
| `vk_lock` | `pthread_mutex_t` | Protects Vulkan API calls |

---

### Source Structure

#### `struct source`

Represents one active PipeWire stream source.

| Field | Type | Description |
|-------|------|-------------|
| `receiver` | `void *` | Opaque pointer back to the Windows-side receiver |
| `stream` | `struct funnel_stream *` | libfunnel stream handle |
| `commandBuffer` | `VkCommandBuffer` | Dedicated command buffer for blit operations |
| `mem` | `VkDeviceMemory` | Vulkan memory allocation (reserved, not used in final code) |
| `image` | `VkImage` | Imported Vulkan image from the DXVK handle |
| `lock` | `pthread_mutex_t` | Mutex protecting the source state |
| `cond` | `pthread_cond_t` | Condition variable for wake-up signals |
| `quit` | `bool` | Set to true to signal the thread to exit |
| `info` | `struct source_info` | Current texture metadata |
| `cur_fd` | `int` | Cached file descriptor for the imported texture |
| `width` / `height` | `uint32_t` | Current stream dimensions |
| `update` | `bool` | Set to true when new texture data is available |
| `dead` | `bool` | Set to true after the main loop exits |

---

### Vulkan Handle Translation (Unix side)

These inline helpers translate Wine's client-object wrapper handles back to native Linux Vulkan handles:

```c
static inline struct vulkan_instance    *vulkan_instance_from_handle(VkInstance handle);
static inline struct vulkan_physical_device *vulkan_physical_device_from_handle(VkPhysicalDevice handle);
static inline struct vulkan_device      *vulkan_device_from_handle(VkDevice handle);
static inline struct vulkan_image       *vulkan_image_from_handle(VkImage handle);
```

Each casts the `Vk*` handle to a `struct vulkan_client_object *`, then reads the `unix_handle` field which points to the real native Linux handle.

---

### Core Functions

#### `static NTSTATUS startup(void *args)`

Initializes the Unix-side Vulkan and PipeWire pipeline. Called once during service startup.

**Steps:**
1. Initializes `vk_lock` mutex
2. Extracts `VkInstance`, `VkPhysicalDevice`, `VkDevice` from the Windows-side handles via the client-object translation
3. Enumerates physical devices and selects the best one (score: discrete > integrated > virtual > cpu > other)
4. Finds a graphics queue family
5. Creates a Vulkan device (uses the host handle directly, does not create a new one)
6. Resolves `vkGetImageMemoryRequirements2KHR` and `vkGetMemoryFdPropertiesKHR` function pointers
7. Creates a `VkCommandPool`
7. Initializes libfunnel (`funnel_new`)
8. Sets app name / ID (reads `SPOUT2PW_APPNAME` env var, or defaults to `Spout2PW` / `yt.lina.spout2pw`)
9. Connects to the PipeWire session (`funnel_connect`)

#### `static NTSTATUS create_source(void *args)`

Creates a new PipeWire stream for a Spout2 sender.

**Steps:**
1. Allocates a `struct source`
2. Allocates a Vulkan command buffer from the shared command pool
3. Calls `funnel_stream_create` with the sender name
4. Calls `funnel_stream_init_vulkan` to bind the stream to the Vulkan context
5. Optionally sets a custom instance name via `SPOUT2PW_INSTANCE` env var
6. Sets stream mode to `FUNNEL_SYNCHRONOUS` and rate to variable (1/1000s resolution)
7. Adds supported Vulkan formats (`R8G8B8A8_UNORM/SRGB`, `B8G8R8A8_SRGB`) as blit destinations
8. Initializes the source's mutex and condition variable, stores the source pointer in `params->ret_source`

#### `static int import_texture(struct source *source)`

Imports the Vulkan image handle from the Windows side.

**Final implementation (simplified):**
- Validates that `source->info.vk_image` is non-zero
- Casts it directly to `VkImage` and stores it in `source->image`
- Returns `0` on success

*(Commented-out code shows an earlier approach using FD-based external memory import.)*

#### `static NTSTATUS run_source(void *args)`

The main processing loop for a source, runs in its own thread.

**Loop:**
1. If `source->update` is set:
   - If texture is invalid/disconnected: marks inactive and stops the stream
   - If texture is updated: calls `import_texture`, sets stream size, configures and starts the stream
2. If not active: waits on the condition variable
3. Dequeues a buffer from the funnel stream
4. Validates buffer dimensions match the source dimensions
5. Gets Vulkan semaphores and fence from the buffer
6. Records a command buffer:
   - Begins the command buffer
   - Calls `vkCmdBlitImage` to blit from the imported DXVK image to the funnel buffer
   - Ends the command buffer
7. Locks the texture (`lock_texture` callback), submits the command buffer, enqueues the buffer, waits for GPU idle, unlocks the texture

#### `static NTSTATUS update_source(void *args)`

Signals an existing source that new texture data is available.

**Steps:**
1. Locks the source mutex
2. If the source is dead, returns `STATUS_NO_SUCH_DEVICE`
3. Invalidates the old `vk_image` handle
4. Copies the new `source_info` into the source
5. Sets `update = true` and broadcasts the condition variable to wake `run_source`

#### `static NTSTATUS destroy_source(void *args)`

Signals a source to stop.

**Steps:**
1. Sets `source->quit = true`
2. Broadcasts the condition variable
3. Calls `funnel_stream_skip_frame` if not already dead
4. Returns immediately; the source is freed when `run_source` exits

#### `static void teardown(void)`

Cleans up all Unix-side resources in reverse order of creation:
1. `funnel_shutdown(funnel)`
2. `vkDestroyCommandPool`
3. `vkDestroyDevice`
4. `vkDestroyDebugUtilsMessengerEXT` (if created)
5. `vkDestroyInstance`
6. Destroys `vk_lock` mutex

---

### Format Conversion

#### `static struct format_alpha dx_to_vkformat(uint32_t format)`

Maps DirectX pixel formats to Vulkan formats.

| DX Format | VK Format | Alpha? |
|-----------|-----------|--------|
| `R32G32B32A32_FLOAT` | `R32G32B32A32_SFLOAT` | Yes |
| `R16G16B16A16_FLOAT` | `R16G16B16A16_SFLOAT` | Yes |
| `R16G16B16A16_UNORM` | `R16G16B16A16_UNORM` | Yes |
| `R16G16B16A16_SNORM` | `R16G16B16A16_SNORM` | Yes |
| `R10G10B10A2_UNORM` | `A2R10G10B10_UNORM_PACK32` | Yes |
| `R8G8B8A8_UNORM/SRGB/SNORM` | `R8G8B8A8_SRGB` | Yes |
| `B8G8R8A8_UNORM` | `B8G8R8A8_SRGB` | Yes |
| `B8G8R8X8_UNORM` | `B8G8R8A8_SRGB` | No |
| `0` (legacy) | `R8G8B8A8_SRGB` | Yes |
| `D3DFMT_A8R8G8B8` | `B8G8R8A8_SRGB` | Yes |
| `D3DFMT_X8R8G8B8` | `B8G8R8A8_SRGB` | No |

---

### Callback Dispatch (Unix side)

#### `static struct lock_texture_return *lock_texture(void *receiver)`

Dispatches the Windows-side `lock_texture` callback via `KeUserDispatchCallback`. Returns the result or `NULL` on failure.

#### `static void unlock_texture(void *receiver)`

Dispatches the Windows-side `unlock_texture` callback via `KeUserDispatchCallback`.

---

### Wine Unix Call Table

```c
const unixlib_entry_t __wine_unix_call_funcs[] = {
    _getenv,       // 0
    startup,       // 1
    _teardown,     // 2
    create_source, // 3
    run_source,    // 4
    update_source, // 5
    destroy_source,// 6
};
C_ASSERT(ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count);
```

This table is consumed by Wine's `__wine_unix_call` infrastructure to route calls from the PE side to native functions.

---

### Debug Logging

The project uses Wine's debug channel macros:

| Macro | Severity |
|-------|----------|
| `TRACE(...)` | Verbose trace (disabled by default) |
| `WARN(...)` | Warning |
| `ERR(...)` | Error |
| `FIXME(...)` | Unimplemented / unexpected |

Debug channel name: `spout2pw`

---

## Constants and IOCTLs

### Windows-side IO Control Codes

```c
#define IOCTL_SHARED_GPU_RESOURCE_GET_UNIX_RESOURCE  CTL_CODE(FILE_DEVICE_VIDEO, 3, ...)
#define IOCTL_SHARED_GPU_RESOURCE_OPEN               CTL_CODE(FILE_DEVICE_VIDEO, 1, ...)
#define IOCTL_SHARED_GPU_RESOURCE_GET_METADATA       CTL_CODE(FILE_DEVICE_VIDEO, 5, ...)
#define IOCTL_SHARED_GPU_RESOURCE_GET_INFO           CTL_CODE(FILE_DEVICE_VIDEO, 7, ...)
```

These are used to query shared GPU resource metadata from the Windows video driver.

### DXGI Format Constants

Defined locally in `spout2pw_unix.c` (lines 28–37):
- `DXGI_FORMAT_R32G32B32A32_FLOAT` = 2
- `DXGI_FORMAT_R16G16B16A16_FLOAT` = 10
- `DXGI_FORMAT_R16G16B16A16_UNORM` = 11
- `DXGI_FORMAT_R16G16B16A16_SNORM` = 13
- `DXGI_FORMAT_R10G10B10A2_UNORM` = 24
- `DXGI_FORMAT_R8G8B8A8_UNORM` = 28
- `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` = 29
- `DXGI_FORMAT_R8G8B8A8_SNORM` = 31
- `DXGI_FORMAT_B8G8R8A8_UNORM` = 87
- `DXGI_FORMAT_B8G8R8X8_UNORM` = 88

### D3D11 Bind Flags

```c
#define D3D11_BIND_SHADER_RESOURCE   0x8
#define D3D11_BIND_RENDER_TARGET     0x20
#define D3D11_BIND_UNORDERED_ACCESS  0x80
```
