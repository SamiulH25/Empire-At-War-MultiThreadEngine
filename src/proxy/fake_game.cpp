// Fake game exe for testing the patch proxy end-to-end without the real
// game. Mirrors the game's startup shape (doc 04):
//   - imports d3d9.dll (the proxy) for Direct3DCreate9
//   - has a sim-tick function
//   - calls Direct3DCreate9 during startup, then ticks every frame
//
// Run with EAW_PATCH_TARGET=export:SimTick so the proxy hooks the export.
// After the run, eaw_patch_hits.txt must show the hook installed and the
// ticks flowing through HookedTick.
#include <windows.h>

#include <cstdio>

// Imported from d3d9.dll (the proxy) exactly like the real game imports it.
extern "C" __declspec(dllimport) void* Direct3DCreate9(unsigned sdkVersion);

// Must stay an actual call target (the proxy hooks it): no inlining, and no
// constprop cloning (GCC would clone it for the loop's constant args and the
// loop would call the clone, bypassing the hook).
extern "C" __declspec(dllexport) __declspec(noinline)
__attribute__((noclone)) void __fastcall
SimTick(void* hwnd, float dt) {
    static unsigned long long count = 0;
    ++count;
    if (count <= 3 || (count % 100) == 0) {
        printf("[fake] tick %llu dt=%.2f\n", count, (double)dt);
    }
    (void)hwnd;
}

int main() {
    printf("[fake] starting; Direct3DCreate9 = %p\n", (void*)&Direct3DCreate9);
    void* d3d = Direct3DCreate9(32);
    printf("[fake] Direct3DCreate9 -> %p\n", d3d);
    if (!d3d) {
        printf("[fake] FAIL: proxy did not return a device\n");
        return 1;
    }
    // "Frames": each call goes through the proxy's HookedTick (if installed)
    // into the real SimTick.
    for (int i = 0; i < 5; ++i) {
        SimTick(nullptr, 0.033f);
        Sleep(10);
    }
    printf("[fake] done\n");
    return 0;
}
