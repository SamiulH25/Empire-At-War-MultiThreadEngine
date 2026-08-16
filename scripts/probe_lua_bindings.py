# Probe: extract identifier-ish strings from the game's Lua bytecode files
# to inventory the engine binding surface the game registers.
import re, glob, os, sys

def main():
    pat = re.compile(rb'[\x20-\x7e]{3,}')
    all_names = {}
    for f in glob.glob(os.path.join(sys.argv[1], '*.LUA')):
        data = open(f, 'rb').read()
        base = os.path.basename(f)
        for m in pat.finditer(data):
            s = m.group().decode()
            if s[0].isalpha() or s[0] == '_':
                all_names.setdefault(s, set()).add(base)
    for n in sorted(all_names):
        print(f'{n}  <-  {",".join(sorted(all_names[n]))}')

if __name__ == '__main__':
    main()
