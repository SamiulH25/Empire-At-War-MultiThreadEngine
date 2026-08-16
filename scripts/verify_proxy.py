"""Verify the patch proxy DLL: exports Direct3DCreate9 and forwards to the
real system d3d9.

Usage: python scripts/verify_proxy.py [path-to-d3d9.dll]
Default path: src/build/d3d9.dll (the CMake output).

Exits 0 if the proxy loads, exports Direct3DCreate9, and the call returns a
non-null IDirect3D9 pointer. Exits 1 otherwise.
"""
import ctypes
import os
import sys


def main():
    default = os.path.join('src', 'build', 'd3d9.dll')
    path = sys.argv[1] if len(sys.argv) > 1 else default
    if not os.path.exists(path):
        print(f'FAIL: {path} not found (build the patch_d3d9 target first)')
        return 1
    dll = ctypes.WinDLL(path)
    fn = dll.Direct3DCreate9
    if fn is None:
        print('FAIL: Direct3DCreate9 export missing')
        return 1
    print('ok: proxy loaded, Direct3DCreate9 exported')
    result = fn(32)
    if not result:
        print('FAIL: Direct3DCreate9 returned null (forwarding broken)')
        return 1
    print(f'ok: forwarded to real d3d9, IDirect3D9 = {result}')
    print('PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
