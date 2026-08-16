// XrefsAt.java — list all xrefs to an address and decompile each enclosing
// function (first N lines). Args: <address-hex> [max-funcs]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;

public class XrefsAt extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String arg = (getScriptArgs().length > 0) ? getScriptArgs()[0] : "1408501f0";
        int maxf = (getScriptArgs().length > 1) ? Integer.parseInt(getScriptArgs()[1]) : 12;
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(arg);
        println("=== XrefsAt " + arg + " in " + currentProgram.getName());
        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a)) {
            Function f = fm.getFunctionContaining(r.getFromAddress());
            String fname = (f != null) ? f.getName() + "@" + f.getEntryPoint() : r.getFromAddress().toString();
            println("XREF " + fname);
            if (f != null && n < maxf) {
                DecompInterface di = new DecompInterface();
                di.openProgram(currentProgram);
                DecompileResults res = di.decompileFunction(f, 60, monitor);
                if (res != null && res.decompileCompleted()) {
                    String code = res.getDecompiledFunction().getC();
                    String[] lines = code.split("\n");
                    println("--- func " + f.getName() + " (first 25 lines) ---");
                    for (int i = 0; i < Math.min(lines.length, 25); i++) println(lines[i]);
                }
                di.dispose();
            }
            if (++n >= maxf + 5) break;
        }
        println("=== XrefsAt done");
    }
}
