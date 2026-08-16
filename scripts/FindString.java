// FindString.java — find a byte string in memory and print its address + xrefs.
// Args: the string to find (quoted)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

public class FindString extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String needleStr = (getScriptArgs().length > 0) ? getScriptArgs()[0] : "ThreadLockMutexClass";
        byte[] needle = needleStr.getBytes("UTF-8");
        println("=== FindString '" + needleStr + "' in " + currentProgram.getName());
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized() || !blk.getName().startsWith(".rdata")) continue;
            Address start = blk.getStart();
            long size = blk.getSize();
            println("scanning block " + blk.getName() + " size " + size);
            long pos = 0;
            byte[] buf = new byte[(int)Math.min(size, 1 << 20)];
            while (pos < size) {
                int got = (int)Math.min(buf.length, size - pos);
                mem.getBytes(start.add(pos), buf, 0, got);
                for (int i = 0; i + needle.length <= got; i++) {
                    boolean ok = true;
                    for (int j = 0; j < needle.length; j++)
                        if (buf[i+j] != needle[j]) { ok = false; break; }
                    if (ok) {
                        Address fa = start.add(pos + i);
                        println("FOUND at " + fa + " (block " + blk.getName() + ")");
                        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(fa)) {
                            Function f = fm.getFunctionContaining(r.getFromAddress());
                            println("  XREF " + (f != null ? f.getName() + "@" + f.getEntryPoint() : r.getFromAddress()));
                        }
                        return;
                    }
                }
                pos += got - needle.length + 1;
            }
        }
        println("NOT FOUND in .rdata");
        println("=== FindString done");
    }
}
