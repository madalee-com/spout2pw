#define _POSIX_C_SOURCE 200809L
#define VK_USE_PLATFORM_WIN32_KHR

#include <assert.h>
#include <math.h>
#include <png.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "spout2pw_unix.h"

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define COBJMACROS

#include <winioctl.h>

#include "d3d11_1.h"
#include "initguid.h"
// #include "d3d12.h"
// #include "d3d12sdklayers.h"
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
// #include "subprojects/spoutdxtoc/Spout2/SPOUTSDK/SpoutGL/SpoutDirectX.h" //
// for creating a shared texture

WINE_DEFAULT_DEBUG_CHANNEL(spout2pw);

#define IOCTL_SHARED_GPU_RESOURCE_GET_UNIX_RESOURCE                            \
    CTL_CODE(FILE_DEVICE_VIDEO, 3, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_SHARED_GPU_RESOURCE_OPEN                                         \
    CTL_CODE(FILE_DEVICE_VIDEO, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_SHARED_GPU_RESOURCE_GET_METADATA                                 \
    CTL_CODE(FILE_DEVICE_VIDEO, 5, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_SHARED_GPU_RESOURCE_GET_INFO                                     \
    CTL_CODE(FILE_DEVICE_VIDEO, 7, METHOD_BUFFERED, FILE_READ_ACCESS)

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

struct shared_resource_open {
    unsigned int kmt_handle;
    WCHAR name[1];
};

struct shared_resource_info {
    UINT64 resource_size;
};

typedef enum D3D11_TEXTURE_LAYOUT {
    D3D11_TEXTURE_LAYOUT_UNDEFINED = 0,
    D3D11_TEXTURE_LAYOUT_ROW_MAJOR = 1,
    D3D11_TEXTURE_LAYOUT_64K_STANDARD_SWIZZLE = 2
} D3D11_TEXTURE_LAYOUT;

struct DxvkSharedTextureMetadata {
    UINT Width;
    UINT Height;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D11_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    D3D11_TEXTURE_LAYOUT TextureLayout;
};

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
static const GUID IID_IDXGIResource_C = {
    0x035f3ab4,
    0x482e,
    0x4e50,
    {0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x9c, 0x44}};
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
static const GUID IID_IDXGIResource1_Real = {
    0x30961379,
    0x4609,
    0x4a41,
    {0x99, 0x8e, 0x54, 0xfe, 0x56, 0x7e, 0xe0, 0x11}};
static const GUID IID_IDXGIResource_Legacy = {
    0x035f3ab4,
    0x482e,
    0x4e50,
    {0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b}};

HANDLE get_handle_from_legacy_texture(ID3D11Texture2D *pTexture) {
    IDXGIResource *pDxgiResource = NULL;

    // Query the baseline interface supported by the legacy allocation
    HRESULT hr = pTexture->lpVtbl->QueryInterface(
        pTexture, &IID_IDXGIResource_Legacy, (void **)&pDxgiResource);
    if (FAILED(hr) || !pDxgiResource) {
        ERR("[-] Critical: Texture rejected baseline IDXGIResource query.\n");
        return NULL;
    }

    HANDLE sharedHandle = NULL;

    // Retrieve the raw shared handle pointer address from the legacy resource
    // tracker
    hr = pDxgiResource->lpVtbl->GetSharedHandle(pDxgiResource, &sharedHandle);
    pDxgiResource->lpVtbl->Release(pDxgiResource);

    if (FAILED(hr) || !sharedHandle) {
        ERR("[-] Legacy GetSharedHandle failed with code: 0x%08X\n",
            (unsigned int)hr);
        return NULL;
    }

    return sharedHandle;
}

// Direct extraction hook bypassing COM type validation via the DXVK internal
// structure definition In DXVK, D3D11Texture2D structures derive directly from
// the surface container base offset.
typedef struct {
    void *vtbl;
    void *padding[4];
    VkImage vkImage; // Offset maps to DXVK's inner m_image reference tracker
} DXVK_Texture2D_Internal;

static WCHAR spout2pwW[] = L"Spout2Pw";
static HANDLE exit_event;
static SERVICE_STATUS_HANDLE service_handle;
static SERVICE_STATUS service_status;

static HANDLE sendernames_thread_handle = 0;
static SPOUTDXTOC_SENDERNAMES *spout_names = NULL;

static DWORD WINAPI sendernames_thread(void *arg);

static bool do_restart = false;
static bool startup_done = FALSE;

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

static NTSTATUS get_shared_metadata(HANDLE handle, void *buf, uint32_t buf_size,
                                    uint32_t *metadata_size) {
    IO_STATUS_BLOCK iosb;

    NTSTATUS status = NtDeviceIoControlFile(
        handle, NULL, NULL, NULL, &iosb, IOCTL_SHARED_GPU_RESOURCE_GET_METADATA,
        NULL, 0, buf, buf_size);

    if (status != STATUS_SUCCESS) {
        ERR("Failed to get shared metadata, status %#lx.\n", (long int)status);
    } else if (metadata_size) {
        *metadata_size = iosb.Information;
    }
    return status;
}

static NTSTATUS get_shared_info(HANDLE handle,
                                struct shared_resource_info *info) {
    IO_STATUS_BLOCK iosb;

    NTSTATUS status = NtDeviceIoControlFile(handle, NULL, NULL, NULL, &iosb,
                                            IOCTL_SHARED_GPU_RESOURCE_GET_INFO,
                                            NULL, 0, info, sizeof(*info));

    if (status != STATUS_SUCCESS) {
        ERR("Failed to get shared info, status %#lx.\n", (long int)status);
    }
    return status;
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

// Function to save raw RGBA data to a PNG file using libpng
int SavePNG(const char *filename, unsigned char *buffer, int width, int height,
            int stride) {
    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return 0;

    png_structp png_ptr =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return 0;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return 0;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return 0;
    }

    png_init_io(png_ptr, fp);

    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr);

    // Create row pointers pointing directly into our CPU staging buffer
    png_bytepp row_pointers = (png_bytepp)malloc(sizeof(png_bytep) * height);
    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_bytep)(buffer + y * stride);
    }

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return 1;
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

// Dynamic function pointer definition for winevulkan query
// Re-creating standard Vulkan memory properties structures locally

// Helper function to resolve the map-safe memory allocation index
uint32_t
find_memory_type_index(VkPhysicalDevice physicalDevice,
                       PFN_vkGetPhysicalDeviceMemoryProperties getPropsFunc,
                       uint32_t typeBitsFilter, uint32_t requiredProperties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    getPropsFunc(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        // 1. Check if the driver supports this allocation index type for our
        // specific buffer requirement
        if ((typeBitsFilter & (1 << i))) {
            // 2. Check if the memory layout features match our required Host
            // mapping capability
            if ((memProperties.memoryTypes[i].propertyFlags &
                 requiredProperties) == requiredProperties) {
                return i; // Found the perfect index match!
            }
        }
    }

    return 0; // Fallback default (highly unsafe if matching fails)
}

// Updated with accurate Flat function layout parameters
void save_buffer_to_png(VkDevice vkDevice,
                        PFN_vkGetDeviceProcAddr getDeviceProc,
                        uint64_t flatMemoryHandle, uint32_t width,
                        uint32_t height) {
    PFN_vkMapMemory_Flat vkMapMemory_F =
        (PFN_vkMapMemory_Flat)getDeviceProc(vkDevice, "vkMapMemory");
    PFN_vkUnmapMemory_Flat vkUnmapMemory_F =
        (PFN_vkUnmapMemory_Flat)getDeviceProc(vkDevice, "vkUnmapMemory");

    if (!vkMapMemory_F || !vkUnmapMemory_F) {
        fprintf(
            stderr,
            "[-] Critical Error: Map tracking functions resolved to NULL.\n");
        return;
    }

    void *pMappedData = NULL;
    uint64_t bufferSize = width * height * 4;

    WARN("[*] Mapping memory buffers for host readout...\n");
    // Securely passed using our flattened 64-bit integer memory layout
    if (vkMapMemory_F(vkDevice, flatMemoryHandle, 0, bufferSize, 0,
                      &pMappedData) != VK_SUCCESS) {
        ERR("[-] Error: System memory allocation map fault.\n");
        return;
    }

    FILE *fp = fopen("c:\\output.png", "wb");
    if (!fp) {
        ERR("[-] Error: Disk write permissions failure for output.png\n");
        vkUnmapMemory_F(vkDevice, flatMemoryHandle);
        return;
    }

    png_structp png_ptr =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);

    if (setjmp(png_jmpbuf(png_ptr))) {
        ERR("[-] Processing fault within libpng data streams.\n");
        fclose(fp);
        vkUnmapMemory_F(vkDevice, flatMemoryHandle);
        return;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
    uint8_t *pRawBytes = (uint8_t *)pMappedData;
    uint32_t rowStride = width * 4;

    for (uint32_t y = 0; y < height; y++) {
        row_pointers[y] = (png_bytep)(pRawBytes + (y * rowStride));
    }

    WARN("[+] Processing raw imagery via libpng targets...\n");
    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    vkUnmapMemory_F(vkDevice, flatMemoryHandle);
    WARN("[+] Capture completed cleanly. output.png generated!\n");
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
        struct startup_params params = {.lock_texture = (UINT_PTR)lock_texture,
                                        .unlock_texture =
                                            (UINT_PTR)unlock_texture,
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
/*
    // --- STEP 1: RESOLVE WINE VULKAN POINTERS ---
    HMODULE hWineVk = LoadLibraryA("winevulkan.dll");
    if (!hWineVk) {
        ERR("[-] Critical Error: winevulkan.dll missing from prefix!\n");
        return ret;
    }

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)GetProcAddress(hWineVk,
                                                  "vkGetInstanceProcAddr");
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr =
        (PFN_vkGetDeviceProcAddr)GetProcAddress(hWineVk, "vkGetDeviceProcAddr");

    // Dynamic instance-level resolution
    PFN_vkGetPhysicalDeviceMemoryProperties
        vkGetPhysicalDeviceMemoryProperties =
            (PFN_vkGetPhysicalDeviceMemoryProperties)vkGetInstanceProcAddr(
                vkInstance, "vkGetPhysicalDeviceMemoryProperties");

    // Flat device-level resolutions (Guarantees zero structure pointer
    // extraction crashes)
    PFN_vkCreateBuffer_Flat vkCreateBuffer_F =
        (PFN_vkCreateBuffer_Flat)vkGetDeviceProcAddr(vkDevice,
                                                     "vkCreateBuffer");
    PFN_vkAllocateMemory_Flat vkAllocateMemory_F =
        (PFN_vkAllocateMemory_Flat)vkGetDeviceProcAddr(vkDevice,
                                                       "vkAllocateMemory");
    PFN_vkBindBufferMemory_Flat vkBindBufferMemory_F =
        (PFN_vkBindBufferMemory_Flat)vkGetDeviceProcAddr(vkDevice,
                                                         "vkBindBufferMemory");
    PFN_vkGetBufferMemoryRequirements_Flat vkGetBufferMemoryRequirements_F =
        (PFN_vkGetBufferMemoryRequirements_Flat)vkGetDeviceProcAddr(
            vkDevice, "vkGetBufferMemoryRequirements");

    PFN_vkCreateCommandPool_Flat vkCreateCommandPool_F =
        (PFN_vkCreateCommandPool_Flat)vkGetDeviceProcAddr(
            vkDevice, "vkCreateCommandPool");
    PFN_vkAllocateCommandBuffers_Flat vkAllocateCommandBuffers_F =
        (PFN_vkAllocateCommandBuffers_Flat)vkGetDeviceProcAddr(
            vkDevice, "vkAllocateCommandBuffers");
    PFN_vkBeginCommandBuffer_Flat vkBeginCommandBuffer_F =
        (PFN_vkBeginCommandBuffer_Flat)vkGetDeviceProcAddr(
            vkDevice, "vkBeginCommandBuffer");
    PFN_vkEndCommandBuffer_Flat vkEndCommandBuffer_F =
        (PFN_vkEndCommandBuffer_Flat)vkGetDeviceProcAddr(vkDevice,
                                                         "vkEndCommandBuffer");
    PFN_vkCmdCopyImageToBuffer_Flat vkCmdCopyImageToBuffer_F =
        (PFN_vkCmdCopyImageToBuffer_Flat)vkGetDeviceProcAddr(
            vkDevice, "vkCmdCopyImageToBuffer");
*/
    /*
        VkBufferCreateInfo bufInfo = {.sType =
       VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = pDesc.Width * pDesc.Height
       * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode =
       VK_SHARING_MODE_EXCLUSIVE}; uint64_t flatBufferHandle = 0; uint64_t
       flatMemoryHandle = 0;

        vkCreateBuffer_F(vkDevice, &bufInfo, NULL, &flatBufferHandle);

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements_F(vkDevice, flatBufferHandle, &memReqs);
        uint32_t memoryTypeIndex = find_memory_type_index(
            vkPhysDev, vkGetPhysicalDeviceMemoryProperties,
       memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo memInfo = {.sType =
                                            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                        .allocationSize = memReqs.size,
                                        .memoryTypeIndex = memoryTypeIndex};
        vkAllocateMemory_F(vkDevice, &memInfo, NULL, &flatMemoryHandle);
        vkBindBufferMemory_F(vkDevice, flatBufferHandle, flatMemoryHandle, 0);
        // Fetch DXVK queue details for the pool allocation context
        VkQueue vkQueue;
        uint32_t queueFamilyIndex = 0;
        pDxvkInterop->lpVtbl->GetSubmissionQueue(pDxvkInterop, &vkQueue,
                                                 &queueFamilyIndex);
        // Initialize Command Infrastructure natively
        uint64_t flatCmdPool = 0;
        VkCommandPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = queueFamilyIndex,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
        vkCreateCommandPool_F(vkDevice, &poolInfo, NULL, &flatCmdPool);
        VkCommandBuffer myRealCmdBuffer = NULL;
        VkCommandBufferAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = (VkCommandPool)flatCmdPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1};
        vkAllocateCommandBuffers_F(vkDevice, &allocInfo, &myRealCmdBuffer);
        // ==========================================// CRITICAL FIX ORDER:
       RECORD
        // OUTSIDE THE LOCK// ==========================================
        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        WARN("[*] Invoking vkBeginCommandBuffer safely OUTSIDE the lock...\n");
        vkBeginCommandBuffer_F(myRealCmdBuffer, &beginInfo);
    */
    // Now freeze the context queue right before execution recording block

    /*
        WARN("[*] Acquiring exclusive DXVK worker thread queue lock...\n");
        pDxvkInterop->lpVtbl->LockSubmissionQueue(pDxvkInterop);
        pDxvkInterop->lpVtbl->FlushRenderingCommands(pDxvkInterop);
        VkImageSubresourceRange subresources = {.aspectMask =
                                                    VK_IMAGE_ASPECT_COLOR_BIT,
                                                .baseMipLevel = 0,
                                                .levelCount = 1,
                                                .baseArrayLayer = 0,
                                                .layerCount = 1};
        pDxvkInterop->lpVtbl->TransitionSurfaceLayout(
            pDxvkInterop, pDxvkSurface, &subresources,
       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        pDxvkInterop->lpVtbl->ReleaseSubmissionQueue(pDxvkInterop);
    */
    /*
        VkBufferImageCopy region = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .mipLevel = 0,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
            .imageExtent = {pDesc.Width, pDesc.Height, 1}};
        WARN("[+] Appending transfer image copy command packets into buffer "
             "chain...\n");
        vkCmdCopyImageToBuffer_F(myRealCmdBuffer, flatSrcVkImage,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 flatBufferHandle, 1, &region);
        // Flush and safely release the DXVK thread lock
        // pDxvkInterop->lpVtbl->FlushRenderingCommands(pDxvkInterop);
        // pDxvkInterop->lpVtbl->ReleaseSubmissionQueue(pDxvkInterop);
        WARN("[+] DXVK loop unlocked cleanly!\n");
        // Close the command buffer recording track cleanly
        vkEndCommandBuffer_F(myRealCmdBuffer);
        // ==========================================// STEP 5: SUBMIT
       OPERATIONS &
        // SAVE// ==========================================
        PFN_vkQueueSubmit_Flat vkQueueSubmit_F =
            (PFN_vkQueueSubmit_Flat)vkGetDeviceProcAddr(vkDevice,
       "vkQueueSubmit"); PFN_vkQueueWaitIdle_Flat vkQueueWaitIdle_F =
            (PFN_vkQueueWaitIdle_Flat)vkGetDeviceProcAddr(vkDevice,
                                                          "vkQueueWaitIdle");

        // Clear and build submission packets
        VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                   .pNext = NULL,
                                   .commandBufferCount = 1,
                                   .pCommandBuffers = &myRealCmdBuffer};

        printf(
            "[*] Flashing Vulkan copy packets down hardware execution
       pipes...\n");
        // Submit recorded command buffer containing vkCmdCopyImageToBuffer
        vkQueueSubmit_F(vkQueue, 1, &submitInfo, VK_NULL_HANDLE);

        // HARDWARE BARRIER: Force CPU to wait until the GPU finishes copying
       the
        // pixel arrays to your host buffer
        printf("[*] Synchronizing memory timelines (vkQueueWaitIdle)...\n");
        vkQueueWaitIdle_F(vkQueue);
        // Fetch native loader vkQueueSubmit manually or trigger via DXVK flush
        // context loop
        WARN("[*] Copy packets processing complete. Finalizing output "
             "processing...\n");

        // Map memory and handoff flat tracking handles to the file writer
        save_buffer_to_png(vkDevice, vkGetDeviceProcAddr, flatMemoryHandle,
                           pDesc.Width, pDesc.Height);
    */
    // Clear
    // sharedTexture->lpVtbl->Release(sharedTexture);
    pDxvkInterop->lpVtbl->Release(pDxvkInterop);

    /*
    const char *outputFilename = "c:\\outputdx.png";

    ID3D11Texture2D *pStagingTexture = NULL;

    // 1. Initialize D3D11 Device and Context
    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                           createDeviceFlags, NULL, 0, D3D11_SDK_VERSION,
                           &pDevice, &featureLevel, &pContext);

    if (FAILED(hr)) {
        ERR("Failed to create D3D11 Device. 0x%lX\n", hr);
        return ret;
    }
    WARN("Created device\n");

    // Now populate required properties
    pDesc.MipLevels = 1; // Staging textures MUST be 1
    pDesc.ArraySize = 1; // Staging textures MUST be 1
    pDesc.SampleDesc.Count =
        1; // Staging textures CANNOT be multisampled(No MSAA)
    pDesc.SampleDesc.Quality = 0;
    pDesc.Usage = D3D11_USAGE_STAGING; // CPU Read targets must use STAGING
    pDesc.BindFlags = 0; // Staging textures MUST have 0 bind flags
    pDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    pDesc.MiscFlags = 0; // Staging textures MUST have 0 misc flags

    hr = ID3D11Device_CreateTexture2D(pDevice, &pDesc, NULL, &pStagingTexture);

    if (FAILED(hr) || pDevice == NULL) {
        ERR("D3D11 Device creation failed with HRESULT 0x%08X\n", hr);
        goto Cleanup;
    }
    WARN("Created staging texture\n");

    IDXGIKeyedMutex *pKeyedMutex = NULL;
    // Check if the shared texture supports or expects a keyed sync state
    hr = sharedTexture->lpVtbl->QueryInterface(
        sharedTexture, &IID_IDXGIKeyedMutex, (void **)&pKeyedMutex);

    WARN("got query int: 0x%lX\n", hr);

    if (SUCCEEDED(hr) && pKeyedMutex) {
        // Acquire key 0 (or whatever key matching your sender application)
        // 100 milliseconds timeout
        hr = pKeyedMutex->lpVtbl->AcquireSync(pKeyedMutex, 0, 100);
        if (FAILED(hr)) {
            ERR("Warning: Keyed mutex acquire failed or timed out.\n");
        }
    }

    WARN("Got keyed mutex\n");

    // 5. Copy GPU data from the shared texture to the CPU-accessible staging
    // texture
    ID3D11DeviceContext_CopyResource(pContext,
                                     (ID3D11Resource *)pStagingTexture,
                                     (ID3D11Resource *)sharedTexture);
    WARN("Copied resource\n");

    // Release the sync flag back to the other process
    if (pKeyedMutex) {
        pKeyedMutex->lpVtbl->ReleaseSync(pKeyedMutex, 0);
        pKeyedMutex->lpVtbl->Release(pKeyedMutex);
    }

    // 6. Map the staging texture to get a direct CPU pointer to the pixel data
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = pContext->lpVtbl->Map(pContext, (ID3D11Resource *)pStagingTexture, 0,
                               D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        ERR("Failed to map staging texture. 0x%lX\n", hr);
        goto Cleanup;
    }
    WARN("Mapped staging texture\n");

    // 7. Save to PNG (Assuming texture is DXGI_FORMAT_R8G8B8A8_UNORM)
    if (pDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
        pDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        printf("Saving %dx%d texture to %s...\n", pDesc.Width, pDesc.Height,
               outputFilename);
        if (SavePNG(outputFilename, (unsigned char *)mappedResource.pData,
                    pDesc.Width, pDesc.Height, mappedResource.RowPitch)) {
            WARN("PNG saved successfully! %s\n", outputFilename);
        } else {
            ERR("Failed to write PNG file.\n");
        }
    } else {
        ERR("Unsupported texture format (Expected RGBA8). Format ID: %d\n",
            pDesc.Format);
    }

    // 8. Unmap the memory region
    pContext->lpVtbl->Unmap(pContext, (ID3D11Resource *)pStagingTexture, 0);

Cleanup:
    SAFE_RELEASE(pStagingTexture);
    SAFE_RELEASE(pContext);
    SAFE_RELEASE(pDevice);
    return ret;
    */

    /*
    D3D11_MAPPED_SUBRESOURCE mappedTexture;

    if (SpoutDXToCGetMappedTexture(spout, (LONG_PTR)&mappedTexture)) {
        WARN("Got mapped texture: %lx\n", PtrToUlong(mappedTexture.pData));
    } else {
        WARN("Failed to get mapped texture\n");
    }
    */
/*
    IDXGIResource1 *dxgiResource = NULL;
    HANDLE resharedHandle = NULL;

    hr = ID3D11Texture2D_QueryInterface(sharedTexture, &IID_IDXGIResource1,
                                        (void **)&dxgiResource);
    if (FAILED(hr)) {
        ERR("Failed to query IDXGIResource.\n");
        return ret;
    }

    // Retrieve the NT handle required for Vulkan interoperability
    // hr = IDXGIResource_GetSharedHandle(dxgiResource, &resharedHandle);
    hr = IDXGIResource1_CreateSharedHandle(
        dxgiResource, NULL, DXGI_SHARED_RESOURCE_READ, NULL, &resharedHandle);

    // ret.shared_handle = HandleToLong(resharedHandle);
    if (FAILED(hr)) {
        ERR("Failed to create shared NT handle: %lx\n", hr);
        return ret;
    }
*/
    /*

        WARN("Start vulkan stuff\n");
    VkResult vk_res;
    // ==========================================
    // 2. INITIALIZE VULKAN
    // ==========================================
    // Instance extensions required for external memory capability queries
    const char *instance_extensions[] = {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME};

    WARN("create vulkan instance\n");
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = instance_extensions};
    VkInstance instance;
    vkCreateInstance(&instance_info, NULL, &instance);

    WARN("find first device\n");
    // Pick first physical device
    uint32_t device_count = 1;
    VkPhysicalDevice physical_device;
    vkEnumeratePhysicalDevices(instance, &device_count, &physical_device);

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physical_device, &deviceProperties);
    WARN("got device : %s\n", deviceProperties.deviceName);

    WARN("create device\n");
    // Device extensions required to import Win32/NT memory handles
    const char *device_extensions[] = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME};

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority};

    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = device_extensions};
    VkDevice vk_device;
    vkCreateDevice(physical_device, &device_info, NULL, &vk_device);

    WARN("create dummy image\n");
    // ==========================================
    // 3. RESOLVE VULKAN MEMORY PROPERTIES
    // ==========================================
    // Query memory requirements specifically for an external Win32 NT
    handle import VkExternalMemoryImageCreateInfoKHR ext_image_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT};

    VkImageCreateInfo img_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                  .pNext = &ext_image_info,
                                  .imageType = VK_IMAGE_TYPE_2D,
                                  .format = VK_FORMAT_R8G8B8A8_UNORM,
                                  .extent = {1920, pDesc.Height, 1},
                                  .mipLevels = 1,
                                  .arrayLayers = 1,
                                  .samples = VK_SAMPLE_COUNT_1_BIT,
                                  .tiling = VK_IMAGE_TILING_OPTIMAL,
                                  .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                           VK_IMAGE_USAGE_SAMPLED_BIT,
                                  .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    // Create a dummy Vulkan image to parse the underlying hardware
    allocation size requirements VkImage dummy_image;
    vkCreateImage(vk_device, &img_info, NULL, &dummy_image);

    WARN("get memory requirements\n");
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(vk_device, dummy_image, &mem_reqs);

    // Size required for the shared allocation
    VkDeviceSize allocation_size = mem_reqs.size;

    WARN("find memory type\n");
    // Find memory type index supporting host-visible capabilities for
    mapped reads on Linux uint32_t memory_type_index =
        FindMemoryType(physical_device, mem_reqs.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    WARN("destroy image\n");
    vkDestroyImage(vk_device, dummy_image, NULL);

    // ==========================================
    // 4. IMPORT HANDLE AND ALLOCATE VULKAN MEMORY
    // ==========================================
    VkImportMemoryWin32HandleInfoKHR import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR,
        .handle = resharedHandle};

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_info,
        .allocationSize = allocation_size,
        .memoryTypeIndex = memory_type_index};

    WARN("allocate memory\n");
    VkDeviceMemory vk_memory;
    vk_res = vkAllocateMemory(vk_device, &alloc_info, NULL, &vk_memory);
    CloseHandle(resharedHandle);

    if (vk_res != VK_SUCCESS) {
        WARN("Failed to import NT handle into Vulkan device memory: %d.\n",
             vk_res);
        return ret;
    }

    WARN("Got here\n");
    */
        /*int linux_fd = -1;
        unsigned int options = 0;
        // Request READ/WRITE access to match the DX11 texture allocation flags
        int status = wine_server_handle_to_fd(resharedHandle, GENERIC_READ,
        &linux_fd, &options);

        if (status != 0 || linux_fd == -1) {
            ERR("Failed to map NT handle to Linux FD. Status: %d\n", status);
            return ret;
        }*/

        // IDXGIResource1_Release(dxgiResource);

        /*
            uint64_t pVTexture;
            SpoutDXToCGetVulkanHandle(spout, &pVTexture);
            ret.opaque_fd = pVTexture;

            WARN("After chaos: 0x%lX\n", (long unsigned int)(ret.opaque_fd));
        */
        // ret.opaque_fd = linux_fd;

        // ret.vk_image = flatSrcVkImage;
    
   
    // Direct3D 11.1 is mandatory to expose the modern OpenSharedResource1 vtable method
    //const D3D_FEATURE_LEVEL requestedFeatureLevels[] = { D3D_FEATURE_LEVEL_11_1 };

    

    
/*
    ret.vk_image = HandleToLong(get_handle_from_legacy_texture(sharedTexture));
    HANDLE registeredNTHandle = NULL;
    DuplicateHandle(GetCurrentProcess(),
                    get_handle_from_legacy_texture(sharedTexture),
                    GetCurrentProcess(), &registeredNTHandle, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);*/
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
    for (int i = 0; i < 100; i++) {
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
