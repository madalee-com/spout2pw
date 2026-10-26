#include "spout2dx.h"

#include <algorithm>
#include <string>
#include <strings.h>
#include <vector>
#include <winnt.h>

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
