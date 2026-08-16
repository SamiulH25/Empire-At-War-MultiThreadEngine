// DumpImports.java — list all imported symbols (esp. USER32/d3d9) and their xrefs,
// to find WinMain and the render init.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;

public class DumpImports extends GhidraScript {

    @Override
    protected void run() throws Exception {
        println("=== DumpImports: " + currentProgram.getName());
        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        for (Symbol s : st.getAllSymbols(false)) {
            if (s.getSource() == SourceType.IMPORTED) {
                String nspace = s.getParentNamespace().getName();
                if (nspace.equals("USER32.dll") || nspace.equals("d3d9.dll")
                        || nspace.equals("KERNEL32.dll") || nspace.equals("GDI32.dll")) {
                    for (Reference r : currentProgram.getReferenceManager().getReferencesTo(s.getAddress())) {
                        Function f = fm.getFunctionContaining(r.getFromAddress());
                        println("IMPORT " + nspace + "!" + s.getName()
                                + " <- " + (f != null ? f.getName() + "@" + f.getEntryPoint() : r.getFromAddress()));
                        if (++n > 200) break;
                    }
                }
            }
            if (n > 200) break;
        }
        println("=== DumpImports done (printed " + n + ")");
    }
}
