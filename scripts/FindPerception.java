// FindPerception.java — find Init_Perception_DLL call site in the exe and
// decompile it (the 11 callbacks passed as args).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;

public class FindPerception extends GhidraScript {

    @Override
    protected void run() throws Exception {
        println("=== FindPerception: " + currentProgram.getName());
        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();
        for (Symbol s : st.getAllSymbols(false)) {
            if (s.getName().contains("Init_Perception_DLL") || s.getName().contains("Perception_DLL")) {
                println("SYMBOL " + s.getName() + " @ " + s.getAddress() + " src=" + s.getSource());
                for (Reference r : currentProgram.getReferenceManager().getReferencesTo(s.getAddress())) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    println("  XREF " + (f != null ? f.getName() + "@" + f.getEntryPoint() : r.getFromAddress()));
                }
            }
        }
        println("=== FindPerception done");
    }
}
