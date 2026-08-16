"""Verify the proxy hook log after a fake_game run.

The proxy writes eaw_patch_hits.txt: an install line plus tick-hit lines.
This asserts both exist, so a silent hook failure fails the test.

Usage: python scripts/verify_hook_log.py <path-to-eaw_patch_hits.txt>
"""
import os
import sys


def main():
    if len(sys.argv) < 2:
        print('usage: verify_hook_log.py <hits-file>')
        return 1
    path = sys.argv[1]
    if not os.path.exists(path):
        print(f'FAIL: {path} missing - hook never installed')
        return 1
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        lines = [ln.strip() for ln in f if ln.strip()]
    if not any(ln.startswith('hook installed') for ln in lines):
        print(f'FAIL: no install line in {path}')
        return 1
    ticks = [ln for ln in lines if ln.startswith('tick ')]
    if not ticks:
        print(f'FAIL: no tick hits in {path} - HookedTick never ran')
        return 1
    print(f'ok: hook installed, {len(ticks)} tick hits recorded')
    print('PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
