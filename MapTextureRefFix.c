/* MapTextureRefFix - SKSE64 plugin for Skyrim Special Edition 1.7.104.0
 *
 * Fixes the map-menu crash introduced by the August 2026 update.
 * BSScaleformImageLoader, when serving an "img://" render-target image (the local map),
 * fetches BSGraphics::Renderer::renderTargets[idx].texture WITHOUT AddRef and then calls
 * Release() on it. Each map open drops one reference; eventually the render-target
 * texture is destroyed while still in use and the next map open is a use-after-free.
 * How soon that crashes depends on how quickly the platform recycles freed memory.
 * This plugin NOPs the unbalanced Release() in memory at load time.
 *
 * Plain C, Win32 API only (kernel32 + shell32). Builds with MSVC, MinGW-w64 or clang-cl;
 * no C runtime, Address Library or CommonLib dependency.
 */
#include <windows.h>
#include <shlobj.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    UINT32 dataVersion;
    UINT32 pluginVersion;
    char   name[256];
    char   author[256];
    char   supportEmail[252];
    UINT32 versionIndependenceEx;
    UINT32 versionIndependence;
    UINT32 compatibleVersions[16];
    UINT32 seVersionRequired;
} SKSEPluginVersionData;

__declspec(dllexport) const SKSEPluginVersionData SKSEPlugin_Version = {
    1,
    0x01000000,
    "MapTextureRefFix",
    "danellos",
    "",
    0,
    0,
    { 0x01070680, 0 },   /* 1.7.104.0 only */
    0
};

/* mov rdx,[rsi]; mov rcx,rsi; call [rdx+0x10]; mov eax,1   @ SkyrimSE.exe+0x1173125 (1.7.104.0) */
static const BYTE  kExpect[] = { 0x48,0x8b,0x16, 0x48,0x8b,0xce, 0xff,0x52,0x10, 0xb8,0x01,0x00,0x00,0x00 };
#define OFF_SEQ   0x1173125u
#define OFF_CALL  0x117312Bu
#define CALL_LEN  3

static void logline(const char *msg)
{
    char path[MAX_PATH];
    path[0] = 0;
    if (SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path) != S_OK) return;
    lstrcatA(path, "\\My Games\\Skyrim Special Edition\\SKSE\\MapTextureRefFix.log");
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(h, msg, lstrlenA(msg), &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
    CloseHandle(h);
}

static int apply_patch(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    if (!base) { logline("ERROR: no module base"); return 0; }
    BYTE *seq = base + OFF_SEQ;
    for (unsigned i = 0; i < sizeof(kExpect); i++) {
        if (seq[i] != kExpect[i]) {
            if (seq[6] == 0x90 && seq[7] == 0x90 && seq[8] == 0x90 && i == 6) {
                logline("already patched (NOPs present) - nothing to do");
                return 1;
            }
            logline("NOT APPLIED: byte pattern at SkyrimSE.exe+0x1173125 does not match 1.7.104.0 - wrong game version?");
            return 0;
        }
    }
    BYTE *target = base + OFF_CALL;
    DWORD old;
    if (!VirtualProtect(target, CALL_LEN, PAGE_EXECUTE_READWRITE, &old)) { logline("ERROR: VirtualProtect failed"); return 0; }
    target[0] = 0x90; target[1] = 0x90; target[2] = 0x90;
    VirtualProtect(target, CALL_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, CALL_LEN);
    logline("applied: NOPed unbalanced ID3D11Texture2D::Release at SkyrimSE.exe+0x117312B (BSScaleformImageLoader render-target image path)");
    return 1;
}

__declspec(dllexport) BOOL __cdecl SKSEPlugin_Load(const void *skse)
{
    (void)skse;
    logline("MapTextureRefFix 1.0 loading (target: Skyrim SE 1.7.104.0)");
    apply_patch();
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    (void)h; (void)reason; (void)reserved;
    return TRUE;
}

#ifdef __cplusplus
}
#endif
