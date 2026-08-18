// d3d9 proxy DLL — the injection surface.
//
// The game imports exactly one d3d9 function (`Direct3DCreate9`, doc 01).
// Dropping this DLL into the game folder makes StarWarsG.exe load us instead;
// we forward the import to the real system d3d9 and, on the first call,
// install the sim-tick hook (the game calls Direct3DCreate9 during startup,
// after the loader lock is released — safe for hook installation).
//
// Hook target (default): StarWarsG.exe + 0x25ca30 (FUN_14025ca30, the
// per-frame sim tick, doc 04). For testing without the real game, set the
// environment variable EAW_PATCH_TARGET=export:NAME to hook the export NAME
// of the main executable instead (used by the fake_game harness).
//
// The offset is resolved at runtime by a signature scan first (Steam can
// ship a different build where the tick moved); the hardcoded offset is the
// fallback. The signature is the tick's verified prologue from the installed
// corruption exe (2026-08-18, see docs/progress/06-patch-dll-findings.md):
//   mov [rsp+0x10], rbx ; mov [rsp+0x18], rsi ; push rdi ; sub rsp, 0x30
//   movaps xmm0, xmm1   ; movaps [rsp+0x20], xmm6
//   addss xmm0, [rcx+8] ; movaps xmm6, xmm1 ; mov rbx, rcx ...
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/job_system.h"
#include "core/parallel_tick.h"
#include "MinHook.h"

namespace {

HMODULE g_realD3d9 = nullptr;

// Sim tick offset (doc 04): FUN_14025ca30 - image base 0x140000000.
constexpr DWORD64 kTickOffset = 0x25ca30;

// The tick prologue signature (validated against the installed corruption
// build). Scanning for it makes the hook survive Steam updates that move the
// function.
constexpr unsigned char kTickSig[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,             // mov [rsp+0x10], rbx
    0x48, 0x89, 0x74, 0x24, 0x18,             // mov [rsp+0x18], rsi
    0x57,                                     // push rdi
    0x48, 0x83, 0xEC, 0x30,                   // sub rsp, 0x30
    0x0F, 0x28, 0xC1,                         // movaps xmm0, xmm1
    0x0F, 0x29, 0x74, 0x24, 0x20,             // movaps [rsp+0x20], xmm6
    0xF3, 0x0F, 0x58, 0x41, 0x08,             // addss xmm0, [rcx+8]
};
constexpr size_t kTickSigLen = sizeof(kTickSig);

using TickFn = void (*)(void* hwnd, float dt);
TickFn g_originalTick = nullptr;

// The engine's worker pool, attached once at init. ParallelTick uses it to
// fan the per-object updates across the hardware.
eaw::JobSystem* g_jobs = nullptr;

// Six object-list structs {GameObj** ptr; int count;} live at these offsets
// in the sim-tick context (the `hwnd` param). Confirmed from the Ghidra
// decompile of FUN_14025ca30 (ghidra/simtick_decompile.txt): each list is
// iterated as `items[i]` for i < count, calling vtable+0x50 on non-null.
struct ListSlot {
    eaw::GameObj** ptr; // offset +0x00
    int count;          // offset +0x08
};

constexpr int kListOffsets[6] = {0x18, 0x30, 0x48, 0x60, 0x78, 0x90};

// Reads and validates the game's live object lists from the tick context.
// Every pointer and count is checked before registering, so a layout drift
// (different build) degrades to "no lists" instead of a crash. IsBadReadPtr
// is the portable (non-SEH) readable-check for pointers we must not touch.
void ScanObjectLists(void* ctx) {
    if (!ctx) return;
    eaw::ObjectListCandidate cands[6];
    int n = 0;
    for (int i = 0; i < 6; ++i) {
        ListSlot slot;
        std::memcpy(&slot,
                    reinterpret_cast<char*>(ctx) + kListOffsets[i],
                    sizeof(slot));
        if (!slot.ptr || slot.count <= 0 || slot.count > (1 << 22)) continue;
        // Validate the items array is readable (probe first and last).
        if (IsBadReadPtr(slot.ptr,
                         static_cast<SIZE_T>(slot.count) * sizeof(void*))) {
            continue;
        }
        cands[n].items = slot.ptr;
        cands[n].count = slot.count;
        cands[n].tag = i;
        ++n;
    }
    int kept = eaw::RegisterObjectLists(cands, n);
    if (getenv("EAW_PATCH_DEBUG")) {
        printf("[hook] scanned %d lists, kept %d\n", n, kept);
    }
}

// Called once per frame by the game loop (before the real sim tick).
void HookedTick(void* hwnd, float dt) {
    // Re-scan the object lists every tick. The game's context object lists
    // change as battles load/unload (menu: 1 object; battle: hundreds), so a
    // one-time scan would go stale. The scan is cheap (6 slot reads + probes)
    // and validated against the real battle build (slot 4 = live unit list,
    // 724-938 objects — docs/progress/06-patch-dll-findings.md).
    ScanObjectLists(hwnd);
    // The parallel object-update pass is OFF by default: it races the game's
    // serial tick (Finding 2 — confirmed crash 2026-08-18). Enable only for
    // controlled experiments, never for normal play.
    if (getenv("EAW_PATCH_PARALLEL_UPDATE")) {
        eaw::ParallelTick(dt);
    }

    // Proof-of-life + telemetry: log the first few ticks.
    static unsigned long long count = 0;
    if (count < 5 || (count % 1000) == 0) {
        if (getenv("EAW_PATCH_DEBUG")) {
            printf("[hook] tick %llu dt=%.4f objs=%lld\n", count, (double)dt,
                   (long long)eaw::LastParallelObjectCount());
        }
        FILE* f = fopen("eaw_patch_hits.txt", "a");
        if (f) {
            fprintf(f, "tick %llu dt=%.4f objs=%lld\n", count, (double)dt,
                    (long long)eaw::LastParallelObjectCount());
            fclose(f);
        }
    }
    ++count;
    // Call the real sim tick (the game continues normally).
    g_originalTick(hwnd, dt);
}

// Scans the loaded game image for the tick prologue. Returns the runtime
// address or null if the signature isn't found (different build).
void* FindTickBySignature(HMODULE game) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const char*>(game) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const DWORD64 base = reinterpret_cast<DWORD64>(game);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        // .text (executable code) — scan only the code section.
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const DWORD64 start = base + sec->VirtualAddress;
        const DWORD64 size = sec->Misc.VirtualSize;
        for (DWORD64 off = 0; off + kTickSigLen < size; ++off) {
            if (std::memcmp(reinterpret_cast<const void*>(start + off),
                            kTickSig, kTickSigLen) == 0) {
                return reinterpret_cast<void*>(start + off);
            }
        }
    }
    return nullptr;
}

// Resolves the hook target: the main exe's SimTick export (test mode),
// the signature-scanned tick (real game, Steam-update-proof), or the
// hardcoded offset (fallback). Returns null if unavailable.
void* ResolveTarget() {
    HMODULE mainModule = GetModuleHandleW(nullptr);
    const char* env = getenv("EAW_PATCH_TARGET");
    if (env && strncmp(env, "export:", 7) == 0) {
        return mainModule
                   ? reinterpret_cast<void*>(GetProcAddress(mainModule, env + 7))
                   : nullptr;
    }
    HMODULE game = GetModuleHandleW(L"StarWarsG.exe");
    if (!game) return nullptr;
    // Prefer the signature scan; fall back to the documented offset.
    if (void* sig = FindTickBySignature(game)) return sig;
    return reinterpret_cast<void*>(
        reinterpret_cast<DWORD64>(game) + kTickOffset);
}

void InstallTickHook() {
    void* target = ResolveTarget();
    if (!target) return; // not inside the game (e.g. Python load test)
    if (MH_Initialize() != MH_OK) return;
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookedTick),
                      reinterpret_cast<void**>(&g_originalTick)) != MH_OK) {
        return;
    }
    if (MH_EnableHook(target) != MH_OK) return;
    // Attach the engine's worker pool to the parallel tick dispatcher.
    if (!g_jobs) {
        g_jobs = new eaw::JobSystem();
        eaw::AttachJobSystem(g_jobs);
    }
    FILE* f = fopen("eaw_patch_hits.txt", "w");
    if (f) {
        fprintf(f, "hook installed at %p\n", target);
        // Record how the target resolved (signature scan vs hardcoded offset)
        // so a Steam build change is visible in the telemetry.
        HMODULE game = GetModuleHandleW(L"StarWarsG.exe");
        if (game) {
            bool sig = FindTickBySignature(game) != nullptr;
            fprintf(f, "tick resolved by: %s\n", sig ? "signature" : "offset");
        }
        fprintf(f, "workers: %u\n", g_jobs->workerCount());
        fclose(f);
    }
}

} // namespace

// The game's only d3d9 import. Forward to the real DLL; install the hook on
// first use (after the loader lock is released).
extern "C" __declspec(dllexport) void* Direct3DCreate9(unsigned sdkVersion) {
    if (!g_realD3d9) {
        char sys[MAX_PATH];
        GetSystemDirectoryA(sys, MAX_PATH);
        strcat_s(sys, "\\d3d9.dll");
        g_realD3d9 = LoadLibraryA(sys);
    }
    InstallTickHook();
    using Fn = void* (*)(unsigned);
    Fn real = g_realD3d9
                 ? reinterpret_cast<Fn>(GetProcAddress(g_realD3d9, "Direct3DCreate9"))
                 : nullptr;
    if (!real) return nullptr;
    return real(sdkVersion);
}
