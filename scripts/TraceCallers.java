// TraceCallers.java — print callers of a given function, up the chain.
// Args: address (hex)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;

public class TraceCallers extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String arg = (getScriptArgs().length > 0) ? getScriptArgs()[0] : "140176160";
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(arg);
        FunctionManager fm = currentProgram.getFunctionManager();
        Function f = fm.getFunctionContaining(a);
        println("=== TraceCallers of " + arg + " (" + (f != null ? f.getName() : "no func") + ")");
        for (int depth = 0; depth < 6; depth++) {
            if (f == null) break;
            println("depth " + depth + ": " + f.getName() + " @ " + f.getEntryPoint());
            boolean found = false;
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint())) {
                Function caller = fm.getFunctionContaining(r.getFromAddress());
                if (caller != null && caller != f) {
                    f = caller;
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
        println("=== TraceCallers done");
    }
}
