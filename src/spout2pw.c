// spout2pw.c - Windows PE service for Spout2PW
//
// This is the Wine PE service that runs on Windows and:
//   1. Monitors Spout2 senders via spoutdxtoc
//   2. Extracts Vulkan images via DXVK interop
//   3. Communicates with the Unix shared library via Wine unixlib calls
//
// Build requirements:
//   - x86_64-w64-mingw32 toolchain
//   - Wine development headers
//   - DirectX 11 SDK
//   - Vulkan SDK (Win32)
//   - spoutdxtoc library
//
// Author: Madalee
// License: MIT

#define _POSIX_C_SOURCE 200809L
#define VK_USE_PLATFORM_WIN32_KHR

#include <assert.h>
#include <math.h>
/* png removed - unused */
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "spout2pw_unix.h"

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define COBJMACROS

#include "d3d11_1.h"
#include "initguid.h"
#include "dxgi1_6.h"

#include <winbase.h>
#include <windef.h>
#include <wingdi.h>
#include <winnt.h>
#include <winsvc.h>
#include <winuser.h>

#include "wine/debug.h"
#include "wine/server.h"
#include <ddk/d3dkmthk.h>
#include <vulkan.h>

#include <spoutdxtoc.h>
#include <vulkan/vulkan.h>

WINE_DEFAULT_DEBUG_CHANNEL(spout2pw);

struct receiver {
    char *name;
    void *source;
    SPOUTDXTOC_RECEIVER *spout;
    HANDLE thread;
    struct source_info info;
    bool force_update;
};

struct receiver **receivers;
size_t num_receivers = 0;

// Minimal types to map raw Vulkan objects without inclusion of full vulkan.h
// headers
// typedef void* VkInstance;
// typedef void* VkPhysicalDevice;
// typedef void* VkDevice;
// typedef uint64_t VkImage;

// Matches modern DXVK specification layouts for parsing tiled texture assets
typedef struct {
    uint32_t sampleCount;
    uint32_t layoutType; // 0 = Linear, 1 = Tiled/Optimal
    uint32_t tiling;     // VK_IMAGE_TILING_OPTIMAL
    uint32_t format;     // Underlying Vulkan format id mapping
    uint64_t modifier;   // DRM memory layout modifier attributes
} DXVK_Image_Layout_Private;

// Pre-declare structures for compilation order
typedef struct IDXGIVkInteropDevice IDXGIVkInteropDevice;
typedef struct IDXGIVkInteropSurface IDXGIVkInteropSurface;
typedef struct IDXGIVkInteropSurfaceVtbl {
    // IUnknown Core Methods
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDXGIVkInteropSurface *This,
                                               REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDXGIVkInteropSurface *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDXGIVkInteropSurface *This);

    // DXVK Custom Surface Methods
    void(STDMETHODCALLTYPE *GetDevice)(IDXGIVkInteropSurface *This,
                                       void **ppDevice);

    // This is the bulletproof method to extract the correct VkImage!
    void(STDMETHODCALLTYPE *GetVkImage)(IDXGIVkInteropSurface *This,
                                        uint64_t *pImage, void *pLayout,
                                        void *pFormatInfo);
} IDXGIVkInteropSurfaceVtbl;

struct IDXGIVkInteropSurface {
    IDXGIVkInteropSurfaceVtbl *lpVtbl;
};

// =========================================================================
// 2. FLAT VULKAN FUNCTION POINTER SIGNATURES (Eliminates MinGW Handle Bloat)
// =========================================================================
typedef VkResult (*PFN_vkCreateBuffer_Flat)(
    VkDevice device, const VkBufferCreateInfo *pCreateInfo,
    const void *pAllocator, uint64_t *pBuffer);
typedef VkResult (*PFN_vkAllocateMemory_Flat)(
    VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
    const void *pAllocator, uint64_t *pMemory);
typedef VkResult (*PFN_vkBindBufferMemory_Flat)(VkDevice device,
                                                uint64_t buffer,
                                                uint64_t memory,
                                                uint64_t memoryOffset);
typedef void (*PFN_vkGetBufferMemoryRequirements_Flat)(
    VkDevice device, uint64_t buffer,
    VkMemoryRequirements *pMemoryRequirements);
typedef VkResult (*PFN_vkMapMemory_Flat)(VkDevice device, uint64_t memory,
                                         uint64_t offset, uint64_t size,
                                         uint32_t flags, void **ppData);
typedef void (*PFN_vkUnmapMemory_Flat)(VkDevice device, uint64_t memory);

typedef VkResult (*PFN_vkCreateCommandPool_Flat)(
    VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
    const void *pAllocator, uint64_t *pCommandPool);
typedef VkResult (*PFN_vkAllocateCommandBuffers_Flat)(
    VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
    VkCommandBuffer *pCommandBuffers);
typedef VkResult (*PFN_vkBeginCommandBuffer_Flat)(
    VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo);
typedef VkResult (*PFN_vkEndCommandBuffer_Flat)(VkCommandBuffer commandBuffer);
typedef void (*PFN_vkCmdCopyImageToBuffer_Flat)(
    VkCommandBuffer commandBuffer, uint64_t srcImage, uint32_t srcImageLayout,
    uint64_t dstBuffer, uint32_t regionCount,
    const VkBufferImageCopy *pRegions);
typedef VkResult (*PFN_vkQueueSubmit_Flat)(VkQueue queue, uint32_t submitCount,
                                           const VkSubmitInfo *pSubmits,
                                           VkFence fence);
typedef VkResult (*PFN_vkQueueWaitIdle_Flat)(VkQueue queue);

// Precise vtable layout of DXVK Device Interop, which exposes
// TransitionSurfaceLayout
typedef struct IDXGIVkInteropDevice1Vtbl {
    // IUnknown Core Methods
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDXGIVkInteropDevice *This,
                                               REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDXGIVkInteropDevice *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDXGIVkInteropDevice *This);

    // DXVK Custom Device Interop Methods
    void(STDMETHODCALLTYPE *GetVulkanHandles)(IDXGIVkInteropDevice *This,
                                              VkInstance *pInstance,
                                              VkPhysicalDevice *pPhysDev,
                                              VkDevice *pDevice);
    void(STDMETHODCALLTYPE *GetSubmissionQueue)(IDXGIVkInteropDevice *This,
                                                void *pQueue,
                                                uint32_t *pQueueFamilyIndex);

    // This method implicitly casts a D3D Texture handle into a Surface layout
    // pointer
    void(STDMETHODCALLTYPE *TransitionSurfaceLayout)(
        IDXGIVkInteropDevice *This, void *pSurface,
        const VkImageSubresourceRange *pSubresources, uint32_t OldLayout,
        uint32_t NewLayout);

    void(STDMETHODCALLTYPE *FlushRenderingCommands)(IDXGIVkInteropDevice *This);
    void(STDMETHODCALLTYPE *LockSubmissionQueue)(IDXGIVkInteropDevice *This);
    void(STDMETHODCALLTYPE *ReleaseSubmissionQueue)(IDXGIVkInteropDevice *This);
    void(STDMETHODCALLTYPE *GetSubmissionQueue1)(IDXGIVkInteropDevice *This,
                                                 void *pQueue,
                                                 uint32_t *pQueueIndex,
                                                 uint32_t *pQueueFamilyIndex);
    HRESULT(STDMETHODCALLTYPE *CreateTexture2DFromVkImage)(
        IDXGIVkInteropDevice *This, const D3D11_TEXTURE2D_DESC *pDesc,
        VkImage vkImage, ID3D11Texture2D **ppTexture2D);
} IDXGIVkInteropDevice1Vtbl;

struct IDXGIVkInteropDevice {
    IDXGIVkInteropDevice1Vtbl *lpVtbl;
};

// Target GUID definitions initialized explicitly for C compilation
static const GUID IID_IDXGIDevice_C = {
    0x54ec77fa,
    0x1377,
    0x44e6,
    {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};
static const GUID IID_IDXGIVkInteropDevice_C = {
    0xe2ef5fa5,
    0xdc21,
    0x4af7,
    {0x90, 0xc4, 0xf6, 0x7e, 0xf6, 0xa0, 0x93, 0x23}};
static const GUID IID_IDXGIVkInteropSurface_C = {
    0x5546cf8c,
    0x77e7,
    0x4341,
    {0xb0, 0x5d, 0x8d, 0x4d, 0x50, 0x00, 0xe7, 0x7d}};

// Standard GUID for Direct3D 11's modern device interop layer
static const GUID IID_ID3D11Device1_C = {
    0xa04b27f1,
    0x50c2,
    0x47f6,
    {0x87, 0x93, 0xc4, 0x73, 0xbf, 0x4d, 0xb8, 0x83}};
static const GUID IID_ID3D11Resource_C = {
    0xdc8e6344,
    0x2486,
    0x4c74,
    {0xba, 0x44, 0x05, 0xc4, 0x3e, 0x6a, 0xaa, 0x24}};

static WCHAR spout2pwW[] = L"Spout2Pw";

// Maximum number of WINEDLLDIR paths to check
#define MAX_WINEDLLDIR 100

static HANDLE exit_event;
static SERVICE_STATUS_HANDLE service_handle;
static SERVICE_STATUS service_status;

static HANDLE sendernames_thread_handle = 0;
static SPOUTDXTOC_SENDERNAMES *spout_names = NULL;

static DWORD WINAPI sendernames_thread(void *arg);

static bool do_restart = false;
static bool startup_done = false;

static ID3D11Device *pDevice = NULL;
static ID3D11DeviceContext *pContext = NULL;
static IDXGIVkInteropDevice *pDxvkInterop = NULL;
static VkInstance vkInstance = NULL;
static VkPhysicalDevice vkPhysDev = NULL;
static VkDevice vkDevice = NULL;

static inline void init_unicode_string(UNICODE_STRING *str, const WCHAR *data) {
    str->Length = wcslen(data) * sizeof(WCHAR);
    str->MaximumLength = str->Length + sizeof(WCHAR);
    str->Buffer = (WCHAR *)data;
}

void show_error(HRESULT res, const char *msg) {
    if (!msg) {
        switch (res) {
        case STATUS_FATAL_APP_EXIT:
            msg = "Unknown fatal error";
            break;
        case STATUS_ACCESS_VIOLATION:
            msg = "Spout2PW crashed (access violation)";
            break;
        case STATUS_NO_SUCH_DEVICE:
            msg = "Device crashed or unavailable";
            break;
        case STATUS_NOT_SUPPORTED:
            msg = "Missing a required feature";
            break;
        default:
            msg = "Unknown error";
            break;
        }
    }

    char *dialog_msg = malloc(strlen(msg) + 256);
    sprintf(dialog_msg,
            "%s (%08lx)\n\n"
            "Please see https://lina.yt/s2pw-error for troubleshooting steps.",
            msg, (long)res);

    ERR("Error: %s\n", dialog_msg);

    // Kick the service status so the window is not closed automatically
    // too quickly.
    service_status.dwCheckPoint++;
    service_status.dwWaitHint = 30000;
    SetServiceStatus(service_handle, &service_status);

    TRACE("Show error message box\n");

    // Hack: https://bugs.winehq.org/show_bug.cgi?id=59393
    AllocConsole();

    MessageBoxA(NULL, dialog_msg, "Spout2PW error",
                MB_OK | MB_ICONERROR | MB_SERVICE_NOTIFICATION | MB_TOPMOST);
    TRACE("Message box returned\n");

    free(dialog_msg);
}

static NTSTATUS WINAPI lock_texture(void *args, ULONG size) {
    struct receiver_params *params = args;
    struct receiver *receiver = params->receiver;
    SPOUTDXTOC_RECEIVER *recv = receiver->spout;
    struct lock_texture_return ret = {.retval = 0};

    if (!SpoutDXToCCheckTextureAccess(recv)) {
        ERR("Failed to lock shared texture\n");
        ret.retval = -1;
    } else {
        if (SpoutDXToCGetFrameCount(recv, &ret.frame_count)) {
            ret.flags |= FRAME_IS_NEW;
        }
    }

    return NtCallbackReturn(&ret, sizeof(ret), STATUS_SUCCESS);
}

static NTSTATUS WINAPI unlock_texture(void *args, ULONG size) {
    struct receiver_params *params = args;
    struct receiver *receiver = params->receiver;
    SPOUTDXTOC_RECEIVER *spout = receiver->spout;

    SpoutDXToCAllowTextureAccess(spout);

    return NtCallbackReturn(NULL, 0, STATUS_SUCCESS);
}

static void trigger_restart(void) {
    TRACE("Restarting service due to error\n");
    do_restart = true;
    SetEvent(exit_event);
}

static DWORD WINAPI receiver_thread(void *arg) {
    struct receiver *receiver = arg;

    TRACE("Receiver thread starting for %s\n", receiver->name);
    UNIX_CALL(run_source, receiver->source);
    TRACE("Receiver thread exiting for %s\n", receiver->name);

    return STATUS_SUCCESS;
}

// Helper macro to release COM objects safely
#define SAFE_RELEASE(p)                                                        \
    if (p) {                                                                   \
        p->lpVtbl->Release(p);                                                 \
        p = NULL;                                                              \
    }

// Helper to find the correct Vulkan memory type index
uint32_t FindMemoryType(VkPhysicalDevice physical_device, uint32_t type_filter,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    return 0xFFFFFFFF; // Invalid index
}

static bool startup(SPOUTDXTOC_RECEIVER *spout) {
    // =========================================================
    // STEP 1: Query the interop device from the main context loop
    // =========================================================
    if (pDevice == NULL) {
        if (!SpoutDXToCGetDXDevice(spout, &pDevice)) {
            ERR("[-] Error: Failed to fetch DXVK Device from Spout!\n");
            return false;
        }
    }

    if (pContext == NULL) {
        pDevice->lpVtbl->GetImmediateContext(pDevice, &pContext);
    }
    // pContext->lpVtbl->Flush(pContext);

    if (pDxvkInterop == NULL) {
        IDXGIDevice *pDxgiDevice = NULL;
        pDevice->lpVtbl->QueryInterface(pDevice, &IID_IDXGIDevice_C,
                                        (void **)&pDxgiDevice);

        HRESULT hr = pDxgiDevice->lpVtbl->QueryInterface(
            pDxgiDevice, &IID_IDXGIVkInteropDevice_C, (void **)&pDxvkInterop);
        pDxgiDevice->lpVtbl->Release(pDxgiDevice);
        if (FAILED(hr)) {
            ERR("[-] Error: Failed to fetch DXVK Device Interop! Check "
                "overrides.\n");
            return false;
        }
    }
    TRACE("Looks like we got the VK Device Interop!\n");

    // Extract underlying handles safely via structural tracking offsets
    if (vkInstance == NULL || vkPhysDev == NULL || vkDevice == NULL) {
        pDxvkInterop->lpVtbl->GetVulkanHandles(pDxvkInterop, &vkInstance,
                                               &vkPhysDev, &vkDevice);
    }
    if (vkInstance == NULL || vkPhysDev == NULL || vkDevice == NULL) {
        ERR("Failed to get Vulkan handles! Instance: %p, Phys Device: %p, "
            "Device: %p",
            vkInstance, vkPhysDev, vkDevice);
        return false;
    }

    TRACE("Starting up libfunnel\n");

    if (!startup_done) {
        // Note: Cast to uint64_t required for Wine callback ID interop
        struct startup_params params = {.lock_texture = (uint64_t)(uintptr_t)lock_texture,
                                        .unlock_texture =
                                            (uint64_t)(uintptr_t)unlock_texture,
                                        .error_msg = NULL,
                                        .vkInstance = (UINT_PTR)vkInstance,
                                        .vkPhysDev = (UINT_PTR)vkPhysDev,
                                        .vkDevice = (UINT_PTR)vkDevice};

        NTSTATUS ret = UNIX_CALL(startup, &params);
        if (ret != STATUS_SUCCESS) {
            ERR("libfunnel startup failed: [%s]", params.error_msg);
            return false;
        }
    }

    return true;
}

static struct source_info get_receiver_info(struct receiver *receiver) {
    CoInitialize(NULL);
    SPOUTDXTOC_RECEIVER *spout = receiver->spout;
    SPOUTDXTOC_SENDERINFO info;
    struct source_info ret = {};

    TRACE("Updating receiver %p -> %p (%s)\n", receiver, spout, receiver->name);

    if (!SpoutDXToCIsConnected(spout)) {
        ret.flags = RECEIVER_DISCONNECTED;
        TRACE("-> Not connected\n");
        return ret;
    }
    TRACE("Spout is connected\n");

    if (!SpoutDXToCGetSenderInfo(spout, &info)) {
        ret.flags = RECEIVER_DISCONNECTED;
        TRACE("-> Failed to get sender info (disconnected?)\n");
        return ret;
    }
    TRACE("Got Spout sender info\n");

    HANDLE share_handle = (HANDLE)(LongToHandle((long)(info.shareHandle)));

    TRACE("Sender %s: %dx%d fmt=%d handle=0x%lx usage=0x%x changed=%d\n",
          receiver->name, info.width, info.height, info.format,
          (long)(intptr_t)info.shareHandle, info.usage, info.changed);

    ret.width = info.width;
    ret.height = info.height;
    ret.format = info.format;
    ret.usage = info.usage;

    if (!info.changed && !receiver->force_update)
        return ret;

    receiver->force_update = true;

    if (SpoutDXToCCheckTextureAccess(spout)) {
        WARN("DX Texture access confirmed\n");
    } else {
        WARN("DX Texture access failed\n");
    }

    if (SpoutDXToCAllowTextureAccess(spout)) {
        WARN("DX Texture access allowedd\n");
    } else {
        WARN("DX Texture access not allowed\n");
    }

    TRACE("Try update texture\n");
    if (SpoutDXToCUpdateDXTexture(spout, &info)) {
        WARN("updated texture\n");
    } else {
        WARN("failed to update texture\n");
    }

    Sleep(50);

    if (!SpoutDXToCGetSenderInfo(spout, &info) ||
        (HANDLE)(LongToHandle((long)(info.shareHandle))) != share_handle) {
        WARN("Texture changed out under us, trying again later (0x%lx -> "
             "0x%lx)\n",
             HandleToLong(share_handle), (long)(info.shareHandle));
        ret.flags |= RECEIVER_TEXTURE_INVALID;
        return ret;
    }

    TRACE("Update DX Texture\n");
    if (!SpoutDXToCUpdateDXTexture(spout, &info)) {
        WARN("Failed to update DX texture\n");
        ret.flags |= RECEIVER_TEXTURE_INVALID;
        NtClose(share_handle);
        return ret;
    }

    if (!startup(spout)) {
        ret.flags = RECEIVER_DISCONNECTED;
        TRACE("-> Failed to get DX from spout (disconnected?)\n");
        return ret;
    }
    TRACE("Completed startup\n");

    ID3D11Texture2D *sharedTexture = NULL;

    if (SpoutDXToCGetTexture(spout, (LONG_PTR **)&sharedTexture)) {
        TRACE("Got texture: %lx\n", PtrToUlong(sharedTexture));
    } else {
        WARN("Failed to get texture\n");
    }

    D3D11_TEXTURE2D_DESC pDesc;
    sharedTexture->lpVtbl->GetDesc(sharedTexture, &pDesc);
    TRACE("bind desc: %dx\n", pDesc.BindFlags);

    ret.bind_flags = pDesc.BindFlags;
    ret.width = pDesc.Width;
    ret.height = pDesc.Height;
    ret.usage = pDesc.Usage;
    ret.format = pDesc.Format;

    // =========================================================================
    // Route through IDXGIResource first to securely fetch Interop
    // Surface
    // =========================================================================
    IDXGIVkInteropSurface *pDxvkSurface = NULL;
    HRESULT hr = sharedTexture->lpVtbl->QueryInterface(
        sharedTexture, &IID_IDXGIVkInteropSurface_C, (void **)&pDxvkSurface);
    if (FAILED(hr)) {
        ERR("[-] Fatal Error: D3D11 Texture directly rejected "
            "IDXGIVkInteropSurface mapping.\n");
        return ret;
    }

    // =========================================================================
    // Safe Extraction via Official DXVK Vtable Method
    // =========================================================================
    uint64_t flatSrcVkImage = 0;
    DXVK_Image_Layout_Private dxvkImageLayout = {0};

    // Pass a pointer to our flat integer variable to receive the real handle
    pDxvkSurface->lpVtbl->GetVkImage(pDxvkSurface, &flatSrcVkImage,
                                     &dxvkImageLayout, NULL);

    TRACE("[+] DXVK Handles Resolved. Instance: %p, Device: %p, VkImage: "
          "0x%llX, Layout Tiling Type: %u\n",
          vkInstance, vkDevice, (unsigned long long)flatSrcVkImage,
          dxvkImageLayout.layoutType);

    // Clear
    // sharedTexture->lpVtbl->Release(sharedTexture);
    pDxvkInterop->lpVtbl->Release(pDxvkInterop);

    ret.vk_image = flatSrcVkImage;

    ret.flags |= RECEIVER_TEXTURE_UPDATED;
    receiver->force_update = false;

    return ret;
}

static void update_receiver(struct receiver *receiver) {
    struct source_info new_info = get_receiver_info(receiver);

    if (!receiver->source) {
        WARN("no receiver source\n");
        if (new_info.flags == RECEIVER_TEXTURE_UPDATED) {
            struct create_source_params params = {
                .sender_name = receiver->name,
                .receiver = receiver,
                .info = new_info,
            };
            TRACE("Creating source\n");
            NTSTATUS ret = UNIX_CALL(create_source, &params);
            receiver->source = params.ret_source;
            if (receiver->source) {
                receiver->thread =
                    CreateThread(NULL, 0, receiver_thread, receiver, 0, 0);
            } else {
                TRACE("Source creation failed: 0x%lx %s\n", ret,
                      params.error_msg);
                show_error(ret, params.error_msg);
            }
        }
        return;
    }

    if (new_info.flags != receiver->info.flags ||
        (new_info.flags & RECEIVER_TEXTURE_UPDATED)) {
        WARN("new info flags no matchy\n");
        struct update_source_params params = {
            .source = receiver->source,
            .info = new_info,
        };
        NTSTATUS ret = UNIX_CALL(update_source, &params);
        if (ret == STATUS_NO_SUCH_DEVICE) {
            ERR("Source '%s' had a fatal error\n", receiver->name);
            trigger_restart();
            return;
        }
        receiver->info = new_info;
    }
}

static void update_receivers(void) {
    for (uint32_t i = 0; i < num_receivers; i++)
        update_receiver(receivers[i]);
}

static struct receiver *find_receiver(const char *name) {
    for (uint32_t i = 0; i < num_receivers; i++)
        if (!strcmp(receivers[i]->name, name))
            return receivers[i];
    return NULL;
}

static void add_receiver(const char *name) {
    SPOUTDXTOC_RECEIVER *spout = SpoutDXToCNewReceiver(name);
    if (!spout) {
        TRACE("Failed to create receiver for %s\n", name);
        return;
    }

    struct receiver *receiver = calloc(1, sizeof(struct receiver));

    receiver->name = strdup(name);
    receiver->source = NULL;
    receiver->spout = spout;
    receiver->thread = NULL;

    num_receivers++;
    receivers = realloc(receivers, sizeof(struct receiver) * num_receivers);
    receivers[num_receivers - 1] = receiver;
}

static void remove_receiver(struct receiver *receiver) {
    TRACE("Destroying source %s\n", receiver->name);
    if (receiver->source)
        UNIX_CALL(destroy_source, receiver->source);

    TRACE("Joining thread for %s\n", receiver->name);
    if (receiver->thread)
        WaitForSingleObject(receiver->thread, INFINITE);

    TRACE("Freeing receiver for %s\n", receiver->name);
    SpoutDXToCFreeReceiver(receiver->spout);

    for (uint32_t i = 0; i < num_receivers; i++) {
        if (receivers[i] == receiver) {
            memmove(&receivers[i], &receivers[i + 1],
                    sizeof(struct receiver) * (num_receivers - i - 1));
            num_receivers--;
            goto free;
        }
    }
    ERR("Did not find receiver %p (%s)\n", receiver, receiver->name);

free:
    TRACE("Done removing %s\n", receiver->name);
    free(receiver->name);
    free(receiver);
}

static DWORD WINAPI sendernames_thread(void *arg) {
    TRACE("Sendernames thread started\n");

    SPOUTDXTOC_NAMELIST list = {0};
    do {
        SPOUTDXTOC_NAMELIST new_list = {0};
        SPOUTDXTOC_NAMELIST added = {0};
        SPOUTDXTOC_NAMELIST removed = {0};

        if (!SpoutDXToCGetSenderList(spout_names, &list, &new_list, &added,
                                     &removed)) {
            SpoutDXToCNamelistClear(&new_list);
            update_receivers();
            continue;
        }

        TRACE("Sender list changed\n");

        for (uint32_t i = 0; i < removed.count; i++) {
            TRACE("Removed sender: %s\n", removed.list[i]);
            struct receiver *receiver = find_receiver(removed.list[i]);
            if (receiver)
                remove_receiver(receiver);
        }

        for (uint32_t i = 0; i < added.count; i++) {
            TRACE("New sender: %s\n", added.list[i]);
            add_receiver(added.list[i]);
        }

        SpoutDXToCNamelistClear(&list);
        SpoutDXToCNamelistClear(&added);
        SpoutDXToCNamelistClear(&removed);
        list = new_list;

        update_receivers();
    } while (WaitForSingleObject(exit_event, 100) == WAIT_TIMEOUT);

    TRACE("Sendernames thread returning\n");

    while (num_receivers)
        remove_receiver(receivers[num_receivers - 1]);

    TRACE("Sendernames thread exit\n");

    return STATUS_SUCCESS;
}

static DWORD WINAPI service_handler(DWORD ctrl, DWORD event_type,
                                    LPVOID event_data, LPVOID context) {
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        TRACE("Service control: Shutting down\n");
        service_status.dwCurrentState = SERVICE_STOP_PENDING;
        service_status.dwControlsAccepted = 0;
        SetServiceStatus(service_handle, &service_status);
        SetEvent(exit_event);
        return NO_ERROR;

    default:
        FIXME("Got service ctrl %lx\n", (long)ctrl);
        SetServiceStatus(service_handle, &service_status);
        return NO_ERROR;
    }
}

// Future use
__attribute__((unused)) static const char *_getenv(const char *var) {
    NTSTATUS ret;
    struct getenv_params params = {.var = var};

    ret = UNIX_CALL(getenv, &params);
    if (ret != STATUS_SUCCESS) {
        TRACE("unix_getenv(%s) failed (0x%lx)\n", var, ret);
        return NULL;
    }

    return params.val;
}

static void WINAPI ServiceMain(DWORD argc, LPWSTR *argv) {
    NTSTATUS ret;
    const char *msg = NULL;

    service_handle =
        RegisterServiceCtrlHandlerExW(spout2pwW, service_handler, NULL);
    if (!service_handle)
        return;

    service_status.dwServiceType = SERVICE_WIN32;
    service_status.dwCurrentState = SERVICE_START_PENDING;
    service_status.dwControlsAccepted = 0;
    service_status.dwWin32ExitCode = 0;
    service_status.dwServiceSpecificExitCode = 0;
    service_status.dwCheckPoint = 1;
    service_status.dwWaitHint = 15000;
    SetServiceStatus(service_handle, &service_status);

    TRACE("Loading unix calls\n");

    ret = __wine_init_unix_call();
    if (ret != STATUS_SUCCESS) {
        msg = "Error initializing UNIX library";
        goto stop;
    }

    TRACE("Initializing spoutdxtoc.dll\n");

restart:

    // NOTE: There is no point continuing if it is.
    spout_names = SpoutDXToCNewSenderNames();
    if (spout_names == NULL) {
        msg = "Error initializing spoutdxtoc.dll";
        goto stop;
    }

    TRACE("Starting service\n");

    exit_event = CreateEventW(NULL, TRUE, FALSE, NULL);

    TRACE("Starting sendernames thread\n");
    sendernames_thread_handle =
        CreateThread(NULL, 0, sendernames_thread, NULL, 0, 0);
    TRACE("Sendernames thread created\n");

    service_status.dwCurrentState = SERVICE_RUNNING;
    service_status.dwControlsAccepted =
        SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    service_status.dwCheckPoint = 0;
    service_status.dwWaitHint = 0;
    SetServiceStatus(service_handle, &service_status);

    TRACE("Waiting for exit event\n");
    WaitForMultipleObjects(1, &exit_event, FALSE, INFINITE);

    SetEvent(exit_event);

    if (sendernames_thread_handle != NULL) {
        TRACE("Stopping sender names thread\n");
        WaitForSingleObject(sendernames_thread_handle, INFINITE);
    }

    TRACE("Shutting down libfunnel\n");
    UNIX_CALL(teardown, NULL);
    if (pDevice != NULL) {
        pDevice->lpVtbl->Release(pDevice);
        pDevice = NULL;
    }

    TRACE("Freeing sender names\n");
    SpoutDXToCFreeSenderNames(spout_names);

    if (do_restart) {
        do_restart = false;
        goto restart;
    }

stop:
    if (ret != STATUS_SUCCESS) {
        show_error(ret, msg);
    }

    FreeConsole();
    service_status.dwCurrentState = SERVICE_STOPPED;
    service_status.dwControlsAccepted = 0;
    service_status.dwCheckPoint = 0;
    service_status.dwWaitHint = 0;
    SetServiceStatus(service_handle, &service_status);

    TRACE("Service stopped\n");
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    static const SERVICE_TABLE_ENTRYW service_table[] = {
        {spout2pwW, ServiceMain}, {NULL, NULL}};

    bool found = false;
    for (int i = 0; i < MAX_WINEDLLDIR; i++) {
        char buf[16];
        sprintf(buf, "WINEDLLDIR%d", i);
        const char *val = getenv(buf);
        if (!val)
            break;
        TRACE("Check DLL path: %s=%s\n", buf, val);
        size_t len = strlen(val);
        const char *match = "\\spout2pw-dlls";
        size_t mlen = strlen(match);
        if (len < mlen)
            continue;
        if (strcmp(val + len - mlen, match))
            continue;
        TRACE("Spout2PW DLL path found\n");
        found = 1;
        break;
    }

    if (!found) {
        ERR("Spout2 not configured in WINEDLLPATH\n");
        return 0;
    }

    TRACE("Starting service ctrl\n");

    StartServiceCtrlDispatcherW(service_table);

    TRACE("WinMain returning\n");
    return 0;
}
