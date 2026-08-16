"""Dump loaded modules of StarWarsG.exe (base + size + name) to resolve
thread start addresses against. Usage: python scripts/modules.py
"""
import subprocess, json

def main():
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
        "Get-Process StarWarsG -ErrorAction SilentlyContinue | "
        "ForEach-Object { $_.Modules | Select-Object ModuleName, "
        "@{n='Base';e={'0x' + $_.BaseAddress.ToString('x')}}, "
        "@{n='Size';e={$_.ModuleMemorySize}} } | ConvertTo-Json"],
        capture_output=True, text=True, timeout=60)
    out = r.stdout.strip()
    if not out or out == "null":
        print("StarWarsG.exe not running")
        return
    mods = json.loads(out)
    if isinstance(mods, dict): mods = [mods]
    for m in sorted(mods, key=lambda x: x.get('Base','')):
        print(f"0x{m['Base']:>16}  {m.get('Size',0):>10}  {m['ModuleName']}")

if __name__ == "__main__":
    main()
