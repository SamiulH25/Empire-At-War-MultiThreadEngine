"""Measure live per-thread CPU% of StarWarsG.exe over a sample window.

Usage: python scripts/cpu_sample.py [seconds=10]
Samples the process's per-thread CPU time, waits, samples again, and prints
the per-thread CPU utilization % over the window (and total process CPU%).
"""
import subprocess, sys, json, time

def get_threads(pid):
    pwsh = r'''
$p = Get-Process -Id %d
$p.Threads | ForEach-Object {
    [PSCustomObject]@{
        TID = $_.Id
        Cpu = $_.TotalProcessorTime.TotalSeconds
    }
} | ConvertTo-Json
''' % pid
    r = subprocess.run(["powershell", "-NoProfile", "-Command", pwsh],
                       capture_output=True, text=True, timeout=30)
    out = r.stdout.strip()
    if not out or out == "null":
        return None
    t = json.loads(out)
    return {x['TID']: x['Cpu'] for x in (t if isinstance(t, list) else [t])}

def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
        "Get-Process StarWarsG -ErrorAction SilentlyContinue | Select-Object -First 1 Id,CPU | ConvertTo-Json"],
        capture_output=True, text=True, timeout=30)
    out = r.stdout.strip()
    if not out or out == "null":
        print("StarWarsG.exe not running"); return
    p = json.loads(out)
    pid = p["Id"]
    t0 = time.time()
    a = get_threads(pid)
    if a is None:
        print("no threads"); return
    time.sleep(secs)
    b = get_threads(pid)
    wall = time.time() - t0
    ncpu = int(subprocess.run(["powershell","-NoProfile","-Command",
        "(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors"],
        capture_output=True, text=True).stdout.strip())
    print(f"PID {pid} over {wall:.1f}s (logical CPUs: {ncpu})")
    print(f"{'TID':>8} {'CPU%':>7}")
    total = 0
    rows = []
    for tid, c in b.items():
        d = c - a.get(tid, 0)
        pct = 100.0 * d / wall if wall > 0 else 0
        rows.append((tid, pct, d))
        total += d
    rows.sort(key=lambda x: -x[1])
    for tid, pct, d in rows:
        bar = '#' * int(pct / 5)
        print(f"{tid:>8} {pct:6.1f}%  {bar}")
    print(f"Sum thread CPU: {total:.2f}s in {wall:.1f}s wall = {100*total/wall:.1f}% of one core")
    print(f"Process total:  {100*total/wall/ncpu:.1f}% of all cores")

if __name__ == "__main__":
    main()
