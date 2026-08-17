// Fake game exe for testing the patch proxy end-to-end without the real
// game. Mirrors the game's startup shape (doc 04):
//   - imports d3d9.dll (the proxy) for Direct3DCreate9
//   - has a sim-tick function
//   - calls Direct3DCreate9 during startup, then ticks every frame
//   - has a sim context with object lists at the offsets the proxy scans
//
// Run with EAW_PATCH_TARGET=export:SimTick so the proxy hooks the export.
// After the run, eaw_patch_hits.txt must show the hook installed, the ticks
// flowing through HookedTick, and the parallel object count (the fake object
// list the proxy discovers at the same offsets the real game uses).
#include <windows.h>

#include <cstdio>
#include <cstring>

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

// ---- fake sim context + object lists (mirror the game's layout) ---------
// The proxy scans the tick's first param for six {ptr,count} list slots at
// 0x18/0x30/0x48/0x60/0x78/0x90. We allocate a context with a real list so
// the parallel path is exercised. The object's "vtable" is a one-slot struct
// whose +0x50 member is the Update function.

struct FakeObj {
    // The proxy reads [0] as the vtable pointer, then +0x50 as Update.
    void* vtable;
    unsigned char payload[0x50];
};

struct FakeVTable {
    unsigned char pad[0x50];
    void (*update)(void* obj, float dt);
};

// The Update slot the proxy calls via vtable+0x50 (same as the game's).
void FakeUpdate(void* obj, float dt) {
    FakeObj* o = static_cast<FakeObj*>(obj);
    // Cheap per-object work so the parallel dispatch has something to chew:
    // fold dt into the payload.
    o->payload[0] = static_cast<unsigned char>(o->payload[0] + static_cast<int>(dt * 255.0f));
}

// The sim context: 6 list slots (pointer + count) at the offsets the proxy
// scans. Only slot 0 holds a real list; the rest are empty.
struct FakeSimContext {
    unsigned char pad0[0x18]; // 0x00..0x17
    FakeObj** list0;           // 0x18
    int count0;                // 0x20
    unsigned char pad1[0x08];  // 0x24..0x2b (aligns next slot to 0x30)
    FakeObj** list1;           // 0x30
    int count1;                // 0x38
    unsigned char pad2[0x10];  // 0x3c..0x4b
    FakeObj** list2;           // 0x50
    int count2;                // 0x58
    unsigned char pad3[0x08];  // 0x5c..0x63
    FakeObj** list3;           // 0x68
    int count3;                // 0x70
    unsigned char pad4[0x08];  // 0x74..0x7b
    FakeObj** list4;           // 0x80
    int count4;                // 0x88
    unsigned char pad5[0x08];  // 0x8c..0x93
    FakeObj** list5;           // 0x98
    int count5;                // 0xa0
};

int main() {
    printf("[fake] starting; Direct3DCreate9 = %p\n", (void*)&Direct3DCreate9);
    void* d3d = Direct3DCreate9(32);
    printf("[fake] Direct3DCreate9 -> %p\n", d3d);
    if (!d3d) {
        printf("[fake] FAIL: proxy did not return a device\n");
        return 1;
    }

    // Build the fake object list (kFakeObjects objects).
    const int kFakeObjects = 4096;
    static FakeVTable vt;
    vt.update = &FakeUpdate;
    static FakeObj* objs[kFakeObjects];
    static FakeObj storage[kFakeObjects];
    for (int i = 0; i < kFakeObjects; ++i) {
        storage[i].vtable = &vt; // real vtable: Update at +0x50
        storage[i].payload[0] = static_cast<unsigned char>(i);
        objs[i] = &storage[i];
    }
    static FakeSimContext ctx;
    ctx.list0 = objs;
    ctx.count0 = kFakeObjects;
    ctx.list1 = nullptr; ctx.count1 = 0;
    ctx.list2 = nullptr; ctx.count2 = 0;
    ctx.list3 = nullptr; ctx.count3 = 0;
    ctx.list4 = nullptr; ctx.count4 = 0;
    ctx.list5 = nullptr; ctx.count5 = 0;

    // "Frames": each call goes through the proxy's HookedTick (if installed)
    // into the real SimTick. The proxy's first call discovers the object
    // list and the parallel tick fans it across the workers.
    for (int i = 0; i < 5; ++i) {
        SimTick(&ctx, 0.033f);
        Sleep(10);
    }
    printf("[fake] done\n");
    return 0;
}
