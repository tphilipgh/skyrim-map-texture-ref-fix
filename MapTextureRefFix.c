/* MapTextureRefFix - SKSE64 plugin for Skyrim Special Edition 1.7.104.0
 *
 * Fixes two reference-counting bugs introduced by the August 2026 update:
 *
 * 1. Map-menu crash (v1.0).
 * BSScaleformImageLoader, when serving an "img://" render-target image (the local map),
 * fetches BSGraphics::Renderer::renderTargets[idx].texture WITHOUT AddRef and then calls
 * Release() on it. Each map open drops one reference; eventually the render-target
 * texture is destroyed while still in use and the next map open is a use-after-free.
 * How soon that crashes depends on how quickly the platform recycles freed memory.
 * This plugin NOPs the unbalanced Release() in memory at load time.
 *
 * 2. Texture double release (v1.1).
 * BSGraphics::Renderer::DestroyTexture releases the texture's three D3D11 objects
 * (ID3D11Texture2D, ID3D11ShaderResourceView, and a third view) and then releases all
 * three AGAIN. The matching creation code takes exactly one reference on each, so every
 * texture destruction over-releases by one. Symptoms: random crashes in texture cleanup
 * (NiSourceTexture on the stack), heap-corruption crashes elsewhere, and under D3DMetal a
 * deterministic crash at the main menu. This plugin jumps over the duplicate block.
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
    0x01010000,
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

/* Patch 2: BSGraphics::Renderer::DestroyTexture @ SkyrimSE.exe+0x100F190 (1.7.104.0).
 * Bytes +0x100F1DD..+0x100F210: trailing nop of the first release block, the 45-byte duplicate
 * release block, then "mov edx,0x28". We replace the duplicate block with "jmp +0x2B" + nops. */
static const BYTE kExpect2[] = {
    0x90,
    0x48,0x8b,0x0b,      0x48,0x85,0xc9, 0x74,0x06, 0x48,0x8b,0x01, 0xff,0x50,0x10,
    0x48,0x8b,0x4b,0x08, 0x48,0x85,0xc9, 0x74,0x06, 0x48,0x8b,0x01, 0xff,0x50,0x10,
    0x48,0x8b,0x4b,0x10, 0x48,0x85,0xc9, 0x74,0x07, 0x48,0x8b,0x01, 0xff,0x50,0x10, 0x90,
    0xba,0x28,0x00,0x00,0x00 };
#define OFF_SEQ2   0x100F1DDu
#define OFF_DUP    0x100F1DEu
#define DUP_LEN    45

/* Append src to dst without overflowing a buffer of cap bytes. Returns 0 if it would not fit. */
static int append_bounded(char *dst, size_t cap, const char *src)
{
    size_t have = (size_t)lstrlenA(dst), need = (size_t)lstrlenA(src);
    if (have + need + 1 > cap) return 0;
    lstrcatA(dst, src);
    return 1;
}

static int dir_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Resolve <Documents>\My Games\<game folder>\SKSE\MapTextureRefFix.log.
 * SKSE uses a different <game folder> per store, so try each and keep the first that exists.
 * Returns 0 if no SKSE folder is found or the path does not fit; logging is then skipped. */
static int log_path(char *out, size_t cap)
{
    static const char *const kGameFolders[] = {
        "\\My Games\\Skyrim Special Edition",
        "\\My Games\\Skyrim Special Edition GOG",
        "\\My Games\\Skyrim Special Edition EPIC",
    };
    char docs[MAX_PATH];
    docs[0] = 0;
    if (SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, docs) != S_OK) return 0;
    for (size_t i = 0; i < sizeof(kGameFolders) / sizeof(kGameFolders[0]); i++) {
        out[0] = 0;
        if (!append_bounded(out, cap, docs)) return 0;
        if (!append_bounded(out, cap, kGameFolders[i])) return 0;
        if (!append_bounded(out, cap, "\\SKSE")) return 0;
        if (!dir_exists(out)) continue;
        return append_bounded(out, cap, "\\MapTextureRefFix.log");
    }
    return 0;
}

static void logline(const char *msg)
{
    char path[MAX_PATH + 128];
    if (!log_path(path, sizeof(path))) return;
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(h, msg, (DWORD)lstrlenA(msg), &w, NULL);
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

static int apply_patch2(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    if (!base) return 0;
    BYTE *seq = base + OFF_SEQ2;
    for (unsigned i = 0; i < sizeof(kExpect2); i++) {
        if (seq[i] != kExpect2[i]) {
            if (i == 1 && seq[1] == 0xEB && seq[2] == 0x2B) { logline("patch2 already applied - nothing to do"); return 1; }
            logline("patch2 NOT APPLIED: byte pattern at SkyrimSE.exe+0x100F1DD does not match 1.7.104.0");
            return 0;
        }
    }
    BYTE *target = base + OFF_DUP;
    DWORD old;
    if (!VirtualProtect(target, DUP_LEN, PAGE_EXECUTE_READWRITE, &old)) { logline("patch2 ERROR: VirtualProtect failed"); return 0; }
    target[0] = 0xEB; target[1] = 0x2B;            /* jmp short +0x2B -> lands on "mov edx,0x28" */
    for (unsigned i = 2; i < DUP_LEN; i++) target[i] = 0x90;
    VirtualProtect(target, DUP_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, DUP_LEN);
    logline("patch2 applied: skipped duplicate D3D11 Release block in BSGraphics::Renderer::DestroyTexture at SkyrimSE.exe+0x100F1DE");
    return 1;
}

__declspec(dllexport) BOOL __cdecl SKSEPlugin_Load(const void *skse)
{
    (void)skse;
    logline("MapTextureRefFix 1.1 loading (target: Skyrim SE 1.7.104.0)");
    apply_patch();
    apply_patch2();
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
