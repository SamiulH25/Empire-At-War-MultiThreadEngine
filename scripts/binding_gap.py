# Compute engine binding surface used by the game's own Lua libraries
# and which ones our pg_bindings already provides.
import re, sys

def main():
    names = set()
    for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
        m = re.match(r'(\S+)\s+<-', line)
        if m:
            names.add(m.group(1))
    ours = {'GlobalValue_Get', 'GlobalValue_Set', 'Create_Thread', 'Thread',
            'Thread_Is_Thread_Active', 'Thread_Kill', 'GetCurrentTime',
            'GameRandom', 'GameRandom_Get_Float', 'Get_Game_Mode',
            '_ScriptMessage', 'lc'}
    pat = re.compile(r'^(Get_|Find|Is_|Set_|Try_|Has_|Can_|Register_|Process_|'
                     r'Release_|Lock_|Leave_|Move_|Attack_|Spawn|Event_|Cull_|'
                     r'Cancel_|OneOrMore|Prune|Unit|Garrison|Ability|Project|'
                     r'Power|Form|Reinforce|Select|Declare|Mark|Story|Space_|'
                     r'Respond|Should_|Use_|Activate_|Are_|Tf_|Default_|'
                     r'Launch|Formation|Get_|Find_|Activate_|Has_|Set_)')
    missing = sorted(n for n in names
                     if n not in ours and pat.match(n))
    print(f'{len(missing)} engine bindings used by the game libraries')
    for n in missing:
        print(n)

if __name__ == '__main__':
    main()
