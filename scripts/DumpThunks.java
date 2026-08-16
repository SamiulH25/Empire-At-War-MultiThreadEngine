// DumpThunks.java — list external thunk functions (imports) and their callers,
// filtering for USER32/d3d9/KERNEL32-related names.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.mem.MemoryBlock;

public class DumpThunks extends GhidraScript {

    @Override
    protected void run() throws Exception {
        println("=== DumpThunks: " + currentProgram.getName());
        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        for (Function f : fm.getFunctions(true)) {
            if (f.isThunk()) {
                Function target = f.getThunkedFunction(false);
                String tname = (target != null) ? target.getName() : "?";
                String u = tname.toUpperCase();
                if (u.contains("GETMESSAGE") || u.contains("PEEKMESSAGE")
                        || u.contains("CREATEWINDOW") || u.contains("DISPATCH")
                        || u.contains("REGISTERCLASS") || u.contains("DIRECT3D")
                        || u.contains("PRESENT") || u.contains("SWAP")) {
                    println("THUNK " + f.getName() + "@" + f.getEntryPoint() + " -> " + tname);
                    for (Reference r : currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint())) {
                        Function caller = fm.getFunctionContaining(r.getFromAddress());
                        println("   called from " + (caller != null ? caller.getName() + "@" + caller.getEntryPoint() : r.getFromAddress()));
                        if (++n > 60) break;
                    }
                }
            }
            if (n > 60) break;
        }
        println("=== DumpThunks done");
    }
}
