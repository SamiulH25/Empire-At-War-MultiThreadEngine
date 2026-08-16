"""Capture a thread snapshot of StarWarsG.exe.

Usage: python scripts/thread_snapshot.py [output_file]
Prints PID, total thread count, and per-thread: TID, start address (module+offset
if resolvable), CPU time (seconds), thread state. Uses PowerShell Get-Process +
WMI/CIM for thread info.
"""
import subprocess, sys, json, datetime

def run_pwsh(script):
    r = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                       capture_output=True, text=True, timeout=60)
    return r.stdout

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else None
    # Find StarWarsG.exe processes
    ps = run_pwsh(
        "Get-Process StarWarsG -ErrorAction SilentlyContinue | "
        "Select-Object Id,ProcessName,CPU,WorkingSet64 | ConvertTo-Json"
    ).strip()
    print(ps)
    if not ps or ps == "null":
        print("StarWarsG.exe not running")
        return
    procs = json.loads(ps)
    if isinstance(procs, dict): procs = [procs]
    for p in procs:
        pid = p["Id"]
        print(f"\n=== PID {pid} ({p['ProcessName']}) CPU={p.get('CPU')} "
              f"WS={p.get('WorkingSet64')} ===")
        # Thread info via .NET ProcessThread (StartAddress + TotalProcessorTime)
        pwsh = r'''
$p = Get-Process -Id %d
$p.Threads | ForEach-Object {
    [PSCustomObject]@{
        TID = $_.Id
        State = $_.ThreadState
        Wait = $_.WaitReason
        Cpu = $_.TotalProcessorTime.TotalSeconds
        StartAddr = ('0x' + $_.StartAddress.ToString('x'))
    }
} | ConvertTo-Json
''' % pid
        ti = run_pwsh(pwsh).strip()
        try:
            threads = json.loads(ti)
            if isinstance(threads, dict): threads = [threads]
            print(f"Total threads: {len(threads)}")
            # Sort by CPU time desc
            threads.sort(key=lambda t: -(t.get('Cpu') or 0))
            for t in threads:
                sa = t.get('StartAddr') or '0x0'
                print(f"  TID {t['TID']:>8} state={t['State']:>2} "
                      f"wait={str(t.get('Wait')) or '':>20} "
                      f"cpu={(t.get('Cpu') or 0):.3f}s "
                      f"start={sa}")
        except Exception as e:
            print("thread parse error:", e, ti[:500])

if __name__ == "__main__":
    main()
