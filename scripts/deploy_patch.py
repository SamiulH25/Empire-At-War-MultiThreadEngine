"""Deploy / roll back the d3d9 patch proxy into the game folder.

The proxy must be named d3d9.dll next to StarWarsG.exe. The real system
d3d9.dll is loaded by the proxy via GetSystemDirectory, so we only need to
place our DLL in the game's corruption folder (where the FoCs exe lives).

Usage:
  python scripts/deploy_patch.py deploy <game_corruption_dir> [proxy_dll]
  python scripts/deploy_patch.py rollback <game_corruption_dir>
  python scripts/deploy_patch.py status <game_corruption_dir>

Default proxy: src/build/d3d9.dll
Game dir example: "C:\\Program Files (x86)\\Steam\\steamapps\\common\\
                  Star Wars Empire at War\\corruption"

Run the game once, then check corruption\\eaw_patch_hits.txt for hook
telemetry (tick hits) and corruption\\eaw_proxy_loaded.txt (attach marker).
"""
import os
import shutil
import sys


def game_dir(root):
    return os.path.join(root, 'corruption')


def deploy(root, proxy):
    d = game_dir(root)
    if not os.path.isdir(d):
        print(f'FAIL: {d} not a directory')
        return 1
    if not os.path.exists(os.path.join(d, 'StarWarsG.exe')):
        print(f'FAIL: no StarWarsG.exe in {d} — wrong folder?')
        return 1
    dst = os.path.join(d, 'd3d9.dll')
    bak = os.path.join(d, 'd3d9.dll.bak')
    if os.path.exists(dst) and not os.path.exists(bak):
        # The game folder ships no d3d9.dll (it uses the system one), so an
        # existing d3d9.dll means a previous deploy; back it up anyway.
        shutil.copy2(dst, bak)
        print(f'backed up existing {dst} -> {bak}')
    shutil.copy2(proxy, dst)
    print(f'deployed {proxy} -> {dst}')
    print('launch the game (FoCs), then check:')
    print(f'  {os.path.join(d, "eaw_proxy_loaded.txt")}')
    print(f'  {os.path.join(d, "eaw_patch_hits.txt")}')
    return 0


def rollback(root):
    d = game_dir(root)
    dst = os.path.join(d, 'd3d9.dll')
    bak = os.path.join(d, 'd3d9.dll.bak')
    if os.path.exists(bak):
        shutil.copy2(bak, dst)
        os.remove(bak)
        print(f'restored {dst} from backup')
    elif os.path.exists(dst):
        os.remove(dst)
        print(f'removed {dst}')
    else:
        print('nothing to roll back')
    return 0


def status(root):
    d = game_dir(root)
    for name in ('d3d9.dll', 'd3d9.dll.bak', 'eaw_proxy_loaded.txt',
                 'eaw_patch_hits.txt'):
        p = os.path.join(d, name)
        if os.path.exists(p):
            size = os.path.getsize(p)
            print(f'{name}: present ({size} bytes)')
        else:
            print(f'{name}: absent')
    return 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    root = sys.argv[2]
    proxy = sys.argv[3] if len(sys.argv) > 3 else os.path.join('src', 'build', 'd3d9.dll')
    if cmd == 'deploy':
        return deploy(root, proxy)
    if cmd == 'rollback':
        return rollback(root)
    if cmd == 'status':
        return status(root)
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main())
