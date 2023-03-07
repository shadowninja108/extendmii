#include <lib.hpp>
#include <nn.hpp>
#include <cstring>
#include <string>

#include "func_ptrs.hpp"
#include "sead/container/seadBuffer.h"
#include "sead/prim/seadSafeString.hpp"

HOOK_DEFINE_TRAMPOLINE(ELinkCreate) {

    static uintptr_t Callback(uintptr_t _this, const char* name) {

        char buffer[500];
        int len = std::snprintf(buffer, sizeof(buffer), "ELink Load: %s", name);
        svcOutputDebugString(buffer, len);

        return Orig(_this, name);
    }
};

HOOK_DEFINE_TRAMPOLINE(ELinkLookup) {
    static int Callback(uintptr_t _this, const char* name) {
        
        auto ret = Orig(_this, name);

        if(ret >= 0) {
            char buffer[500];
            int len = std::snprintf(buffer, sizeof(buffer), "ELink Lookup: %s", name);
            svcOutputDebugString(buffer, len);
        }

        return ret;
    }
};

HOOK_DEFINE_TRAMPOLINE(PctlLoad) {
    static void Callback(uintptr_t _this, uintptr_t arg1, const char** arg2) {
        /* Do the normal load. */
        Orig(_this, arg1, arg2);

        /* If we aren't already trying to load battle, also load battle. */
        if(strcmp(arg2[1], "battle") != 0) {
            /* Horribly hackily copy the sead::SafeString and change it's string. */
            const char* fakeStr[2];
            static_assert(sizeof(fakeStr) == sizeof(sead::SafeString), "");
            
            std::memcpy(fakeStr, reinterpret_cast<void*>(arg2), sizeof(fakeStr));
            fakeStr[1] = "battle";

            /* Do the battle load. */
            Orig(_this, arg1, fakeStr);
        }
    }
};

static std::string s_FolderPath = "content:/cmn/elink/ex/";
static std::vector<u8*> s_ELinkBins;

static Result TryLoad() {

    char buf[500];
    #define PRINT(...)                                              \
        {                                                           \
            int len = std::snprintf(buf, sizeof(buf), __VA_ARGS__); \
            svcOutputDebugString(buf, len);                         \
        }

    /* Yes I know this is ew. */
    #define TRY1(expr)                              \
        {                                           \
            Result r = expr;                        \
            if(R_FAILED(r)) {                       \
                nn::fs::CloseDirectory(dirHandle);  \
                return r;                           \
            }                                       \
        }
        
    #define TRY2(expr)                              \
        {                                           \
            Result r = expr;                        \
            if(R_FAILED(r)) {                       \
                nn::fs::CloseDirectory(dirHandle);  \
                delete[] entries;                   \
                return r;                           \
            }                                       \
        }
    #define TRY3(expr)                              \
        {                                           \
            Result r = expr;                        \
            if(R_FAILED(r)) {                       \
                nn::fs::CloseDirectory(dirHandle);  \
                delete[] entries;                   \
                delete[] data;                      \
                return r;                           \
            }                                       \
        }

    PRINT("[ELink::Load] Initializing...");

    nn::fs::DirectoryHandle dirHandle;
    R_TRY(nn::fs::OpenDirectory(&dirHandle, s_FolderPath.c_str(), nn::fs::OpenDirectoryMode_File));

    PRINT("[ELink::Load] Opened directory...");

    s64 entryCount;
    TRY1(nn::fs::GetDirectoryEntryCount(&entryCount, dirHandle));

    PRINT("[ELink::Load] Got file count ...");

    auto entries = new nn::fs::DirectoryEntry[entryCount];
    TRY2(nn::fs::ReadDirectory(&entryCount, entries, dirHandle, entryCount));

    PRINT("[ELink::Load] Read directory...");

    for(int i = 0; i < entryCount; i++) {

        auto& entry = entries[i];

        PRINT("[ELink::Load] Opening file \"%s\"...", entry.m_Name);

        nn::fs::FileHandle fileHandle;
        TRY2(nn::fs::OpenFile(&fileHandle, (s_FolderPath + std::string(entry.m_Name)).c_str(), nn::fs::OpenMode_Read));

        PRINT("[ELink::Load] Reading file \"%s\"...", entry.m_Name);

        auto data = new u8[entry.m_FileSize];
        TRY3(nn::fs::ReadFile(fileHandle, 0, data, entry.m_FileSize));

        s_ELinkBins.push_back(data);

        nn::fs::CloseFile(fileHandle);
    }

    PRINT("[ELink::Load] Done.");

    #undef TRY1
    #undef TRY2
    #undef TRY3

    delete[] entries;
    nn::fs::CloseDirectory(dirHandle);

    return 0;
}

struct Buffer {
    s32 mLength;
    void** mPtrs;
};

HOOK_DEFINE_REPLACE(ELinkInject) {

    static int Callback(uintptr_t _this, void* vanillaElink) {
        char buf[500];
        auto array = exl::util::pointer_path::Follow<Buffer*, 0x40>(_this);

        /* Add bins. */
        for(size_t i = 0; i < s_ELinkBins.size(); i++) {
            PRINT("[ELink::Inject] Injecting %ld/%ld...", i+1, s_ELinkBins.size());
            array->mPtrs[i] = s_ELinkBins.at(i);
        }
        /* Add vanilla at the end for least priority. */
        PRINT("[ELink::Inject] Injecting vanilla...");
        array->mPtrs[s_ELinkBins.size()] = vanillaElink;

        PRINT("[ELink::Inject] Done.");

        /* Index of the ELink file just inserted, although this ends up being discarded. Meh? */
        return array->mLength-1;
    }

};

HOOK_DEFINE_TRAMPOLINE(ELinkBufferCtor) {
    static void Callback(Buffer* _this, uintptr_t parent, uintptr_t heap, int count) {
        TryLoad();

        /* +1 to make room for vanilla ELink bin. */
        Orig(_this, parent, heap, s_ELinkBins.size() + 1);
    }
};

extern "C" void exl_main(void* x0, void* x1) {
    envSetOwnProcessHandle(exl::util::proc_handle::Get());
    exl::hook::Initialize();

    namespace patch = exl::patch;
    namespace armv8 = exl::armv8;
    namespace reg = armv8::reg;
    namespace inst = armv8::inst;

    ELinkCreate::InstallAtOffset(0x00B1DCE0);
    ELinkLookup::InstallAtOffset(0x00B1D590);
    ELinkInject::InstallAtOffset(0x00B1D7C0);
    ELinkBufferCtor::InstallAtOffset(0x00B1DBF0);
    PctlLoad::InstallAtOffset(0x006C7340);
}

extern "C" NORETURN void exl_exception_entry() {
    /* TODO: exception handling */
    EXL_ABORT(0x420);
}