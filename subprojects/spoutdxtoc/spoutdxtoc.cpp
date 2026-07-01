#include "spoutdxtoc.h"

#include <algorithm>
#include <string>
#include <strings.h>
#include <vector>
#include <winnt.h>

#include <png.h>

/* Include SpoutDX and ignore its headers warnings */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include "SpoutDirectX.h"
#include "SpoutFrameCount.h"
#include "SpoutSenderNames.h"
#include "SpoutUtils.h"

#define VK_USE_PLATFORM_WIN32_KHR 1

#include <vulkan.h>
#include <vulkan_win32.h>

#pragma GCC diagnostic pop

#ifdef __MINGW32__
#define ffs __builtin_ffs
#endif

struct SpoutDXToCSenderNames {
    spoutSenderNames sendernames;
};

struct SpoutDXToCReceiver {
    std::string sendername;
    uint32_t lastShareHandle;
    uint32_t lastAdapterId;
    ID3D11Texture2D *sharedTexture;

    spoutSenderNames sendernames;
    spoutFrameCount frame;
    spoutDirectX dx;

    CRITICAL_SECTION cs;
    bool texture_locked;
    bool dx_open;
};

SPOUTDXTOC_SENDERNAMES *__stdcall SpoutDXToCNewSenderNames(void) {
    spoututils::EnableSpoutLogFile("C:\\spoutlog.txt");
    SPOUTDXTOC_SENDERNAMES *p = new SpoutDXToCSenderNames();
    return p;
}

void __stdcall SpoutDXToCFreeSenderNames(SPOUTDXTOC_SENDERNAMES *self) {
    assert(self != NULL);

    delete self;
}

int __stdcall SpoutDXToCGetSenderCount(SPOUTDXTOC_SENDERNAMES *self) {
    assert(self != NULL);

    return self->sendernames.GetSenderCount();
}

#define NAME_MAX_SIZE 256

bool __stdcall SpoutDXToCGetSender(SPOUTDXTOC_SENDERNAMES *self, int64_t index,
                                   char **sendername) {
    assert(self != NULL);
    assert(sendername != NULL && *sendername == NULL);

    *sendername = (char *)calloc(1, NAME_MAX_SIZE * sizeof(char));

    if (self->sendernames.GetSender((int)index, *sendername, NAME_MAX_SIZE))
        return true;

    free(*sendername);
    *sendername = NULL;

    return false;
}

bool __stdcall SpoutDXToCGetDXDevice(SPOUTDXTOC_RECEIVER *self, ID3D11Device **hDX11Device) {
    assert(self != NULL);

    if (self->dx_open) {
        *hDX11Device = self->dx.GetDX11Device();

        if (*hDX11Device != nullptr) {
            return true;
        }
    }

    return false;
}

static void vec_to_null_term_clist(std::vector<std::string> &vector,
                                   char ***list) {
    char **name;

    *list = (char **)calloc(vector.size() + 1, sizeof(char *));

    name = *list;
    for (std::string &s : vector) {
        *name = strdup(s.c_str());
        name++;
    }

    name = *list;
    assert(name[vector.size()] == NULL);
}

char **__stdcall SpoutDXToCGetSenderListSimple(SPOUTDXTOC_SENDERNAMES *self,
                                               uint32_t *ret_count) {
    std::vector<std::string> senderlist;
    char **list = NULL;

    assert(self != NULL);

    int nSenders = self->sendernames.GetSenderCount();
    if (nSenders > 0) {
        char sendername[256]{};
        for (int i = 0; i < nSenders; i++) {
            if (self->sendernames.GetSender(i, sendername))
                senderlist.push_back(sendername);
        }
    }

    vec_to_null_term_clist(senderlist, &list);

    if (ret_count != NULL)
        *ret_count = senderlist.size();

    return list;
}

void __stdcall SpoutDXToCNamelistClear(SPOUTDXTOC_NAMELIST *namelist) {
    assert(namelist != NULL);

    if (namelist->list == NULL)
        return;

    for (uint32_t i = 0; namelist->list[i] != NULL; i++)
        free(namelist->list[i]);

    free(namelist->list);
    namelist->list = NULL;
}

bool __stdcall SpoutDXToCGetSenderList(SPOUTDXTOC_SENDERNAMES *self,
                                       SPOUTDXTOC_NAMELIST *old_list,
                                       SPOUTDXTOC_NAMELIST *ret_senders,
                                       SPOUTDXTOC_NAMELIST *ret_added,
                                       SPOUTDXTOC_NAMELIST *ret_removed) {
    std::vector<std::string> senderlist, list, removed;

    assert(self != NULL);

    if (ret_senders == NULL) {
        assert(ret_added != NULL && ret_added->list == NULL);
        assert(ret_removed != NULL && ret_removed->list == NULL);
    } else {
        assert(ret_senders->list == NULL);
        assert(ret_added == NULL || ret_added->list == NULL);
        assert(ret_removed == NULL || ret_removed->list == NULL);
    }

    int nSenders = self->sendernames.GetSenderCount();
    if (nSenders > 0) {
        char sendername[256]{};
        for (int i = 0; i < nSenders; i++) {
            if (self->sendernames.GetSender(i, sendername))
                list.push_back(sendername);
        }
    }
    senderlist = list;

    for (size_t i = 0; i < old_list->count; i++) {
        std::string sender(old_list->list[i]);
        auto it = std::find(list.begin(), list.end(), sender);

        if (it != list.end())
            list.erase(it);
        else
            removed.push_back(sender);
    }

    if (list.empty() && removed.empty())
        return false;

    if (ret_senders != NULL) {
        vec_to_null_term_clist(senderlist, &ret_senders->list);
        ret_senders->count = senderlist.size();
    }

    if (ret_added != NULL) {
        vec_to_null_term_clist(list, &ret_added->list);
        ret_added->count = list.size();
    }

    if (ret_removed != NULL) {
        vec_to_null_term_clist(removed, &ret_removed->list);
        ret_removed->count = removed.size();
    }

    return true;
}

SPOUTDXTOC_RECEIVER *__stdcall SpoutDXToCNewReceiver(const char *SenderName) {
    SPOUTDXTOC_RECEIVER *p = new SpoutDXToCReceiver();

    InitializeCriticalSection(&p->cs);

    p->sendername = std::string(SenderName);
    p->frame.CreateAccessMutex(SenderName);
    p->frame.EnableFrameCount(SenderName);
    return p;
}

void __stdcall SpoutDXToCFreeReceiver(SPOUTDXTOC_RECEIVER *self) {
    assert(self != NULL);

    self->frame.CleanupFrameCount();
    self->frame.CloseAccessMutex();

    if (self->sharedTexture) {
        self->sharedTexture->Release();
        self->sharedTexture = nullptr;
    }

    if (self->dx_open)
        self->dx.CloseDirectX11();

    DeleteCriticalSection(&self->cs);

    delete self;
}

bool __stdcall SpoutDXToCIsConnected(SPOUTDXTOC_RECEIVER *self) {
    assert(self != NULL);

    SharedTextureInfo info;
    if (!self->sendernames.getSharedInfo(self->sendername.c_str(), &info))
        return false;

    if (info.width == 0 || info.height == 0 || info.shareHandle == 0)
        return false;

    return true;
}

static bool InitDXTexture(SPOUTDXTOC_RECEIVER *self, uint32_t shareHandle) {
    IDXGIAdapter *pAdapter = nullptr;

    SpoutLogNotice("InitDXTexture %x", shareHandle);

    if (self->sharedTexture) {
        self->sharedTexture->Release();
        self->sharedTexture = nullptr;
    }

    if (!shareHandle)
        return false;

    if (self->dx_open) {
        SpoutLogNotice(
            "Importing texture 0x%lx into existing DX adapter (index=%d)",
            shareHandle, self->lastAdapterId);
        // Try to open the share handle with the same device
        if (self->dx.OpenDX11shareHandle(self->dx.GetDX11Device(),
                                         &self->sharedTexture,
                                         LongToHandle((long)shareHandle)))
            return true;

        SpoutLogNotice("Import failed");
        return false;
    }

    // First time
    SpoutLogNotice("Importing texture 0x%lx, trying all adapters", shareHandle);

    const int nAdapters = self->dx.GetNumAdapters();
    for (int i = 0; i < nAdapters; i++) {
        SpoutLogNotice("Trying adapter %d", i);
        if (!self->dx.SetAdapter(i))
            continue;

        // Set the adapter pointer for CreateDX11device to use temporarily
        self->dx.SetAdapterPointer(pAdapter);
        if (!self->dx.OpenDirectX11(nullptr))
            continue;

        // Try to open the share handle with the device created from the adapter
        if (self->dx.OpenDX11shareHandle(self->dx.GetDX11Device(),
                                         &self->sharedTexture,
                                         LongToHandle((long)shareHandle))) {
            self->lastAdapterId = i;
            self->dx_open = true;
            SpoutLogNotice("Texture import succeeded");
            return true;
        }

        self->dx.CloseDirectX11();
    }

    SpoutLogError("All adapters failed to import the texture");

    return false;
}

bool __stdcall SpoutDXToCGetSenderInfo(SPOUTDXTOC_RECEIVER *self,
                                       SPOUTDXTOC_SENDERINFO *info) {
    assert(self != NULL);
    assert(info != NULL);

    SharedTextureInfo sinfo;
    if (!self->sendernames.getSharedInfo(self->sendername.c_str(), &sinfo))
        return false;

    info->shareHandle = sinfo.shareHandle;
    info->width = sinfo.width;
    info->height = sinfo.height;
    info->format = sinfo.format;
    info->usage = sinfo.usage;
    info->changed = false;

    if (self->lastShareHandle != sinfo.shareHandle) {
        // Just free the existing texture, defer creating the new one to
        // SpoutDXToCUpdateDXTexture() to work around a race
        EnterCriticalSection(&self->cs);
        self->texture_locked = false;
        InitDXTexture(self, 0);
        LeaveCriticalSection(&self->cs);
        self->lastShareHandle = 0;
        info->changed = true;
    }

    return true;
}

bool SpoutDXToCUpdateDXTexture(SPOUTDXTOC_RECEIVER *self,
                               SPOUTDXTOC_SENDERINFO *info) {

    EnterCriticalSection(&self->cs);

    self->lastShareHandle = 0;
    self->texture_locked = false;
    bool success = InitDXTexture(self, info->shareHandle);

    LeaveCriticalSection(&self->cs);

    if (!success)
        return false;

    self->lastShareHandle = info->shareHandle;
    info->adapterId = self->lastAdapterId;

    return true;
}

bool __stdcall SpoutDXToCCheckTextureAccess(SPOUTDXTOC_RECEIVER *self) {
    assert(self != NULL);
    bool ret = true;

    EnterCriticalSection(&self->cs);

    if (self->sharedTexture) {
        self->texture_locked = ret =
            self->frame.CheckTextureAccess(self->sharedTexture);
    }

    LeaveCriticalSection(&self->cs);
    return ret;
}

bool __stdcall SpoutDXToCAllowTextureAccess(SPOUTDXTOC_RECEIVER *self) {
    assert(self != NULL);
    bool ret = true;

    EnterCriticalSection(&self->cs);

    if (self->sharedTexture && self->texture_locked)
        ret = self->frame.AllowTextureAccess(self->sharedTexture);

    LeaveCriticalSection(&self->cs);
    return ret;
}

bool __stdcall SpoutDXToCGetFrameCount(SPOUTDXTOC_RECEIVER *self,
                                       uint64_t *framecount) {
    assert(self != NULL);

    bool ret = self->frame.GetNewFrame();

    if (framecount)
        *framecount = self->frame.GetSenderFrame();

    return ret;
}

bool __stdcall SpoutDXToCGetTexture(SPOUTDXTOC_RECEIVER *self,
                                    LONG_PTR **hSharedTexture) {
    assert(self != NULL);
    bool ret = true;

    EnterCriticalSection(&self->cs);

    if (self->sharedTexture) {
        SpoutLogError("we should have a shared texture");
        *hSharedTexture = (LONG_PTR *)self->sharedTexture;
    } else {
        ret = false;
    }

    LeaveCriticalSection(&self->cs);
    return ret;
}

bool __stdcall SpoutDXToCGetMappedTexture(SPOUTDXTOC_RECEIVER *self,
                                          LONG_PTR pMappedTexture) {
    assert(self != NULL);
    bool ret = true;

    EnterCriticalSection(&self->cs);

    if (self->sharedTexture) {
        SpoutLogError("we should have a mapped texture");
        self->dx.GetDX11Context()->Map(
            self->sharedTexture, 0, D3D11_MAP_READ, 0,
            (D3D11_MAPPED_SUBRESOURCE *)pMappedTexture);
    } else {
        ret = false;
    }

    LeaveCriticalSection(&self->cs);
    return ret;
}

bool savePng(const char *filename, uint32_t width, uint32_t height,
             const uint8_t *buffer) {
    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                              nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<png_bytep> rowPointers(height);
    for (uint32_t y = 0; y < height; ++y) {
        rowPointers[y] = const_cast<png_bytep>(&buffer[y * width * 4]);
    }

    png_write_image(png, rowPointers.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return true;
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    SpoutLogError("Failed to find suitable GPU memory type!");
}

void exportVulkanTextureToPng(VkDevice device, VkPhysicalDevice physicalDevice,
                              VkCommandPool commandPool, VkQueue queue,
                              VkImage srcImage, uint32_t width, uint32_t height,
                              const char *outFilename) {
    SpoutLogError("In exportVulkanTextureToPng\n");
    VkDeviceSize imageSize = width * height * 4;

    VkBuffer dstBuffer;
    VkDeviceMemory dstBufferMemory;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &dstBuffer) !=
        VK_SUCCESS) {
        SpoutLogError("Failed to create staging buffer!\n");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, dstBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &dstBufferMemory) !=
        VK_SUCCESS) {
        SpoutLogError("Failed to allocate staging buffer memory!");
    }
    vkBindBufferMemory(device, dstBuffer, dstBufferMemory, 0);

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(commandBuffer, srcImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuffer, 1,
                           &region);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

    void *data;
    vkMapMemory(device, dstBufferMemory, 0, imageSize, 0, &data);
    if (!savePng(outFilename, width, height,
                 static_cast<const uint8_t *>(data))) {
        SpoutLogError("Failed to write PNG output!\n");
    } else {
        SpoutLogError("Successfully exported texture to %s\n", outFilename);
    }
    vkUnmapMemory(device, dstBufferMemory);

    vkDestroyBuffer(device, dstBuffer, nullptr);
    vkFreeMemory(device, dstBufferMemory, nullptr);
}

// ============================================================================
// 2. Helper for Layout Transitions (Required for pipeline compatibility)
// ============================================================================

void transitionImageLayout(VkDevice device, VkCommandPool pool, VkQueue queue,
                           VkImage image, VkImageLayout oldLayout,
                           VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        SpoutLogError("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

bool __stdcall SpoutDXToCGetVulkanHandle(SPOUTDXTOC_RECEIVER *self,
                                         uint64_t *pVTexture) {
    assert(self != NULL);
    bool ret = true;

    SharedTextureInfo sinfo;
    if (!self->sendernames.getSharedInfo(self->sendername.c_str(), &sinfo))
        return false;

    SpoutLogWarning("Start vulkan stuff\n");
    VkResult vk_res;
    const char *instance_extensions[] = {
        "VK_EXT_debug_utils",
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    };

    SpoutLogWarning("create vulkan instance\n");
    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.enabledExtensionCount = ARRAYSIZE(instance_extensions);
    instance_info.ppEnabledExtensionNames = instance_extensions;
    VkInstance instance;
    vk_res = vkCreateInstance(&instance_info, NULL, &instance);
    if (vk_res != VK_SUCCESS) {
        SpoutLogWarning("Failed to initialize VK instance: 0x%lX.\n", vk_res);
        return ret;
    }

    SpoutLogWarning("find first device\n");
    // Pick first physical device
    uint32_t device_count = 1;
    VkPhysicalDevice physical_device;
    vkEnumeratePhysicalDevices(instance, &device_count, &physical_device);

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physical_device, &deviceProperties);
    SpoutLogWarning("got device : %s\n", deviceProperties.deviceName);

    SpoutLogWarning("create device\n");
    // Device extensions required to import Win32/NT memory handles
    const char *device_extensions[] = {
        "VK_KHR_external_memory",
        "VK_KHR_external_memory_win32",
    };

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info;
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = 0;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;
    queue_info.pNext = nullptr;

    VkDeviceCreateInfo device_info;
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = ARRAYSIZE(device_extensions);
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.enabledLayerCount = 1;
    device_info.pNext = nullptr;

    VkDevice vk_device;
    vk_res = vkCreateDevice(physical_device, &device_info, nullptr, &vk_device);
    if (vk_res != VK_SUCCESS) {
        SpoutLogWarning("Failed to initialize VK device: 0x%lX.\n", vk_res);
        return ret;
    }

    VkQueue queue;
    vkGetDeviceQueue(vk_device, queue_info.queueFamilyIndex, 0, &queue);

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queue_info.queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool commandPool;
    if (vkCreateCommandPool(vk_device, &poolInfo, nullptr, &commandPool) !=
        VK_SUCCESS) {
        SpoutLogError("Failed to create command pool!\n");
        vkDestroyDevice(vk_device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return -1;
    }

    SpoutLogWarning("create dummy image\n");
    // ==========================================
    // 3. RESOLVE VULKAN MEMORY PROPERTIES
    // ==========================================
    // Query memory requirements specifically for an external Win32 NT handle
    // import
    VkExternalMemoryImageCreateInfoKHR ext_image_info = {};
    ext_image_info.sType =
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR;
    ext_image_info.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    ext_image_info.pNext = nullptr;

    VkImageCreateInfo img_info = {};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.pNext = &ext_image_info;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_info.extent = {sinfo.width, sinfo.height, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.queueFamilyIndexCount = 0;
    img_info.pQueueFamilyIndices = nullptr;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Create a dummy Vulkan image to parse the underlying hardware allocation
    // size requirements
    VkImage vk_image = VK_NULL_HANDLE;
    vkCreateImage(vk_device, &img_info, nullptr, &vk_image);

    SpoutLogWarning("get memory requirements\n");
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(vk_device, vk_image, &mem_reqs);

    // Size required for the shared allocation
    VkDeviceSize allocation_size = mem_reqs.size;

    SpoutLogWarning("find memory type\n");
    // ----------------------------------------------------
    // STEP D: MEMORY REQUIREMENTS & ALLOCATION
    // ----------------------------------------------------
    PFN_vkGetMemoryWin32HandlePropertiesKHR pfnGetMemoryWin32HandleProperties =
        reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
            vkGetDeviceProcAddr(vk_device,
                                "vkGetMemoryWin32HandlePropertiesKHR"));

    if (!pfnGetMemoryWin32HandleProperties) {
        SpoutLogError(
            "Failed to load required external memory function pointers.\n");
        return ret;
    }
    
    VkMemoryWin32HandlePropertiesKHR handleProperties = {};
    handleProperties.sType =
        VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
    handleProperties.pNext = nullptr;

    vk_res = pfnGetMemoryWin32HandleProperties(
        vk_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        LongToHandle(sinfo.shareHandle), &handleProperties);
    if (vk_res != VK_SUCCESS) {
        SpoutLogError("Failed to query Win32 handle properties. Code: 0x%lX\n",
                      vk_res);
        return ret;
    }

    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
    uint32_t memory_type_index = 0xFFFFFFFF;
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) {
            memory_type_index = i;
        }
    }

    // ==========================================
    // 4. IMPORT HANDLE AND ALLOCATE VULKAN MEMORY
    // ==========================================
    VkMemoryDedicatedAllocateInfo dedicatedAllocInfo = {};
    dedicatedAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedAllocInfo.pNext = nullptr;
    dedicatedAllocInfo.image = vk_image;
    dedicatedAllocInfo.buffer = VK_NULL_HANDLE;

    VkImportMemoryWin32HandleInfoKHR importInfo = {};
    importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    importInfo.pNext = &dedicatedAllocInfo;
    importInfo.handleType =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
    importInfo.handle = LongToHandle(sinfo.shareHandle);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &importInfo;
    allocInfo.allocationSize = allocation_size;
    allocInfo.memoryTypeIndex = memory_type_index;

    EnterCriticalSection(&self->cs);

    //if (self->sharedTexture) {
        SpoutLogError("we should have a shared texture as handle: 0x%lX\n", sinfo.shareHandle);

        SpoutLogWarning("allocate memory\n");
        VkDeviceMemory vk_memory;
        vk_res = vkAllocateMemory(vk_device, &allocInfo, nullptr, &vk_memory);
        if (vk_res != VK_SUCCESS) {
            SpoutLogError(
                "vkAllocateMemory failed to import handle. Code: 0x%lX\n",
                vk_res);
            return ret;
        } else {
            SpoutLogWarning("allocated memory for addr: 0x%lX", LongToHandle(sinfo.shareHandle));
        }

        vk_res = vkBindImageMemory(vk_device, vk_image, vk_memory, 0);
        if (vk_res != VK_SUCCESS) {
            SpoutLogError(
                "Failed to bind imported memory to Vulkan Image. Code: 0x%lX\n",
                vk_res);
            return ret;
        } else {
            SpoutLogWarning("SUCCESS! Interop texture completely linked!  With addr: 0x%lX\n", LongToHandle(sinfo.shareHandle));
        }

        /*if (vkMapMemory(vk_device, vk_memory, 0, allocation_size, 0,
                        (void **)&pVTexture) != VK_SUCCESS) {
            SpoutLogError("Failed to map memory!\n");
            return ret;
        }*/

        exportVulkanTextureToPng(vk_device, physical_device, commandPool, queue,
                                 vk_image, sinfo.width, sinfo.height,
                                 "c:\\vulkan_output.png");
                                 
    /*} else {
        ret = false;
    }*/

    LeaveCriticalSection(&self->cs);

    if (vk_res != VK_SUCCESS) {
        SpoutLogWarning(
            "Failed to import NT handle into Vulkan device memory: 0x%lX.\n",
            vk_res);
        return ret;
    }

    return ret;
}