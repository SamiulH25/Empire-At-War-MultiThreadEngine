// FindRender.java — find the function that calls the D3D9 device Present
// (via vtable or imports) by scanning for the d3d9 thunk callers and
// decompiling likely render functions.
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
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;

public class FindRender extends GhidraScript {

    @Override
    protected void run() throws Exception {
        println("=== FindRender: " + currentProgram.getName());
        // Find the Direct3DCreate9 thunk and its callers
        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();
        for (Symbol s : st.getAllSymbols(false)) {
            if (s.getName().equals("Direct3DCreate9")) {
                for (Reference r : currentProgram.getReferenceManager().getReferencesTo(s.getAddress())) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    println("D3D9CALLER " + (f != null ? f.getName() + "@" + f.getEntryPoint() : r.getFromAddress()));
                }
            }
        }
        // Scan the sim tick's sibling per-frame functions for calls through
        // vtable slots (render usually uses the device via a vtable). We look
        // for functions that call a vtable+0x.. method many times OR reference
        // the known device global. Simplify: check FUN_140301750 & FUN_14002ffb0
        // (WinMain per-frame calls) for what they call.
        String[] cands = {"140301750", "14002ffb0", "140060330", "14001dc60"};
        for (String ca : cands) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(ca);
            Function f = fm.getFunctionContaining(a);
            if (f == null) { println("no func " + ca); continue; }
            DecompInterface di = new DecompInterface();
            di.openProgram(currentProgram);
            DecompileResults res = di.decompileFunction(f, 60, monitor);
            if (res != null && res.decompileCompleted()) {
                String code = res.getDecompiledFunction().getC();
                // print first 60 lines of decompile
                String[] lines = code.split("\n");
                println("=== CAND " + ca + " (" + f.getName() + ") ===");
                for (int i = 0; i < Math.min(lines.length, 60); i++) println(lines[i]);
            }
            di.dispose();
        }
        println("=== FindRender done");
    }
}
