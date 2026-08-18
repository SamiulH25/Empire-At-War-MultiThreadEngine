// DecompileLuaUndump.java — decompile a specific function by address.
// Args: address (hex, e.g. 1407c1380)
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

public class DecompileLuaUndump extends GhidraScript {
    @Override
    public void run() throws Exception {
        String arg = (getScriptArgs().length > 0) ? getScriptArgs()[0] : "1407c1380";
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(arg);
        FunctionManager fm = currentProgram.getFunctionManager();
        Function f = fm.getFunctionContaining(a);
        if (f == null) { println("NO FUNCTION at " + arg); return; }
        println("=== Decompiling " + f.getName() + " @ " + f.getEntryPoint() + " ===");
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        DecompileResults res = di.decompileFunction(f, 120, monitor);
        if (res != null && res.decompileCompleted()) {
            println(res.getDecompiledFunction().getC());
        } else {
            println("decompile failed");
        }
        di.dispose();
    }
}
