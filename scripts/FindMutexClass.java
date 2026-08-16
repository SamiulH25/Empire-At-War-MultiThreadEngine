// FindMutexClass.java — find the ThreadLockMutexClass error string in memory,
// list xrefs (the class methods), and identify the underlying primitive
// (CreateMutexW / InitializeCriticalSection) by decompiling the ctor.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;

public class FindMutexClass extends GhidraScript {

    @Override
    protected void run() throws Exception {
        println("=== FindMutexClass: " + currentProgram.getName());
        byte[] needle = "ThreadLockMutexClass".getBytes("UTF-8");
        Memory mem = currentProgram.getMemory();
        Address found = null;
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized()) continue;
            Address a = blk.getStart();
            long n = blk.getSize();
            long idx = 0;
            byte[] buf = new byte[(int)Math.min(n, 1 << 20)];
            while (idx < n) {
                int got = (int)Math.min(buf.length, n - idx);
                mem.getBytes(a.add(idx), buf, 0, got);
                for (int i = 0; i + needle.length <= got; i++) {
                    boolean ok = true;
                    for (int j = 0; j < needle.length; j++)
                        if (buf[i+j] != needle[j]) { ok = false; break; }
                    if (ok) { found = a.add(idx + i); break; }
                }
                if (found != null) break;
                idx += got - needle.length + 1;
            }
            if (found != null) break;
        }
        if (found == null) { println("STRING NOT FOUND"); return; }
        println("String at " + found);
        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(found)) {
            Function f = fm.getFunctionContaining(r.getFromAddress());
            println("XREF " + (f != null ? f.getName() + "@" + f.getEntryPoint() : r.getFromAddress()));
            if (++n > 30) break;
        }
        // Also find the other mutex strings nearby for context
        println("=== FindMutexClass done");
    }
}
