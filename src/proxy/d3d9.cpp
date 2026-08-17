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
    // First call: discover the game's live object lists from the tick
    // context. Afterwards, ParallelTick fans them across the engine's
    // workers. Additive — the real tick still runs below.
    static bool scanned = false;
    if (!scanned) {
        ScanObjectLists(hwnd);
        scanned = true;
    }
    eaw::ParallelTick(dt);

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

// Resolves the hook target: the main exe's SimTick export (test mode) or
// StarWarsG.exe + kTickOffset (real game). Returns null if unavailable.
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
