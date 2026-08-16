// FindWinMain.java — locate WinMain via message-loop APIs and dump the
// function that owns GetMessageW/PeekMessageW, plus its call sites.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class FindWinMain extends GhidraScript {

    @Override
    protected void run() throws Exception {
        println("=== FindWinMain: " + currentProgram.getName());
        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();
        String[] api = {"GetMessageW", "PeekMessageW", "CreateWindowExW",
                        "RegisterClassW", "DispatchMessageW", "TranslateMessage"};
        for (String apiName : api) {
            Symbol s = null;
            for (Symbol sym : st.getAllSymbols(false)) {
                if (sym.getName().equals(apiName)) { s = sym; break; }
            }
            if (s == null) { println("(no symbol " + apiName + ")"); continue; }
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(s.getAddress())) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                println(apiName + " referenced from " + (f != null ? f.getName() + " @ " + f.getEntryPoint() : r.getFromAddress()));
            }
        }
        println("=== FindWinMain done");
    }
}
