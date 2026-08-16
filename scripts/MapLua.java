// MapLua.java — Lua version, LuaCreateThread, state creation, binding names.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class MapLua extends GhidraScript {

    void scanStrings(String[] needles, String tag) throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        for (String needleStr : needles) {
            byte[] needle = needleStr.getBytes("UTF-8");
            for (MemoryBlock blk : currentProgram.getMemory().getBlocks()) {
                if (!blk.isInitialized() || !blk.getName().startsWith(".rdata")) continue;
                long pos = 0; long size = blk.getSize();
                byte[] buf = new byte[(int)Math.min(size, 1 << 20)];
                while (pos < size) {
                    int got = (int)Math.min(buf.length, size - pos);
                    currentProgram.getMemory().getBytes(blk.getStart().add(pos), buf, 0, got);
                    for (int i = 0; i + needle.length <= got; i++) {
                        boolean ok = true;
                        for (int j = 0; j < needle.length; j++)
                            if (buf[i+j] != needle[j]) { ok = false; break; }
                        if (ok) {
                            Address fa = blk.getStart().add(pos + i);
                            println(tag + " STRING at " + fa);
                            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(fa)) {
                                Function f = fm.getFunctionContaining(r.getFromAddress());
                                println("  " + tag + " XREF " + (f != null ? f.getName() + "@" + f.getEntryPoint() : r.getFromAddress()));
                            }
                            pos = size; break;
                        }
                    }
                    pos += got - needle.length + 1;
                }
            }
        }
    }

    @Override
    protected void run() throws Exception {
        println("=== MapLua: " + currentProgram.getName());
        // 1) Lua version markers
        scanStrings(new String[]{"Lua 5.", "LUA_VERSION", "Lua 5.0", "Lua 5.1", "lua_newstate", "luaL_newstate"}, "LUA");
        // 2) LuaCreateThread error strings (from census: "LuaCreateThread" & "Expected a LuaFunction parameter")
        scanStrings(new String[]{"LuaCreateThread", "Expected a LuaFunction parameter"}, "LUA");
        // 3) Binding-ish names (end in _Thread or known bindings)
        SymbolTable st = currentProgram.getSymbolTable();
        int n = 0;
        for (Symbol s : st.getAllSymbols(false)) {
            String nm = s.getName();
            if (nm.endsWith("_Thread") || nm.startsWith("Lua_") || nm.contains("LuaCreate")) {
                println("LUA SYMBOL " + nm + " @ " + s.getAddress());
                if (++n > 60) break;
            }
        }
        println("=== MapLua done");
    }
}
