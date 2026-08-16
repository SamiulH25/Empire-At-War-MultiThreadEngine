// d3d9 proxy DLL — the injection surface.
//
// The game imports exactly one d3d9 function (`Direct3DCreate9`, doc 01).
// Dropping this DLL into the game folder makes StarWarsG.exe load us instead;
// we forward the import to the real system d3d9 and, on the first call,
// install the sim-tick hook (the game calls Direct3DCreate9 during startup,
// after the loader lock is released — safe for hook installation).
#include <windows.h>

#include <cstdio>

#include "MinHook.h"

namespace {

HMODULE g_realD3d9 = nullptr;

// Sim tick offset (doc 04): FUN_14025ca30 - image base 0x140000000.
constexpr DWORD64 kTickOffset = 0x25ca30;

using TickFn = void (*)(void* hwnd, float dt);
TickFn g_originalTick = nullptr;

// Called once per frame by the game loop (before the real sim tick).
void HookedTick(void* hwnd, float dt) {
    // Proof-of-life + telemetry: log the first few ticks.
    static unsigned long long count = 0;
    if (count < 5 || (count % 1000) == 0) {
        FILE* f = fopen("eaw_patch_hits.txt", "a");
        if (f) {
            fprintf(f, "tick %llu dt=%.4f\n", count, (double)dt);
            fclose(f);
        }
    }
    ++count;
    // Call the real sim tick (the game continues normally).
    g_originalTick(hwnd, dt);
}

void InstallTickHook() {
    HMODULE game = GetModuleHandleW(L"StarWarsG.exe");
    if (!game) return;
    void* target = reinterpret_cast<void*>(
        reinterpret_cast<DWORD64>(game) + kTickOffset);
    if (MH_Initialize() != MH_OK) return;
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookedTick),
                      reinterpret_cast<void**>(&g_originalTick)) != MH_OK) {
        return;
    }
    MH_EnableHook(target);
    FILE* f = fopen("eaw_patch_hits.txt", "w");
    if (f) {
        fprintf(f, "hook installed at StarWarsG.exe+0x25ca30\n");
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
