// MapThreads.java — find CreateThread/_beginthreadex call sites (via import
// thunks), the LoadThread string, and check for TBB symbols in the exe.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class MapThreads extends GhidraScript {

    @Override
    protected void run() throws Exception {
        println("=== MapThreads: " + currentProgram.getName());
        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();

        // 1) CreateThread / _beginthreadex thunk callers
        for (Symbol s : st.getAllSymbols(false)) {
            String n = s.getName();
            if (n.equals("CreateThread") || n.equals("_beginthreadex") || n.equals("CreateThreadStub")) {
                for (Reference r : currentProgram.getReferenceManager().getReferencesTo(s.getAddress())) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    println("THREADAPI " + n + " <- " + (f != null ? f.getName() + "@" + f.getEntryPoint() : r.getFromAddress()));
                }
            }
        }

        // 2) LoadThread string (file offset 0x803B54 -> search .rdata)
        byte[] needle = "LoadThread".getBytes("UTF-8");
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
                        println("LOADTHREAD STRING at " + fa);
                        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(fa)) {
                            Function f = fm.getFunctionContaining(r.getFromAddress());
                            println("  LOADTHREAD XREF " + (f != null ? f.getName() + "@" + f.getEntryPoint() : r.getFromAddress()));
                        }
                        pos = size; break;
                    }
                }
                pos += got - needle.length + 1;
            }
        }

        // 3) TBB check: any tbbR import or TBB mangled symbol?
        boolean tbbFound = false;
        for (Symbol s : st.getAllSymbols(false)) {
            String un = s.getName().toUpperCase();
            if (un.contains("TBB") || un.contains("CONCURRENT_QUEUE") || un.contains("TASK_SCHEDULER")) {
                println("TBB SYMBOL " + s.getName() + " @ " + s.getAddress() + " src=" + s.getSource());
                tbbFound = true;
            }
        }
        println("TBB in exe: " + (tbbFound ? "YES" : "NO"));
        println("=== MapThreads done");
    }
}
