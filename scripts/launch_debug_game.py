"""Launch FoCs with EAW_PATCH_DEBUG set so the proxy writes slot diagnostics
to eaw_patch_debug.txt. Prints the PID and waits; the game window opens
normally.
"""
import os
import subprocess
import sys

GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\StarWarsG.exe"
CWD = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption"


def main() -> int:
    env = dict(os.environ)
    env["EAW_PATCH_DEBUG"] = "1"
    # Keep the parallel update OFF (it races the game and crashes).
    env.pop("EAW_PATCH_PARALLEL_UPDATE", None)
    p = subprocess.Popen([GAME], cwd=CWD, env=env)
    print(f"launched pid {p.pid}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
