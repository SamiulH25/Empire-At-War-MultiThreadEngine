// Decompile.java — decompile a function by address and print the C.
// Args: address (hex)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.DecompiledFunction;

public class Decompile extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String arg = (getScriptArgs().length > 0) ? getScriptArgs()[0] : "14005d990";
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(arg);
        FunctionManager fm = currentProgram.getFunctionManager();
        Function f = fm.getFunctionContaining(a);
        if (f == null) { println("NO FUNCTION at " + arg); return; }
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        DecompileResults res = di.decompileFunction(f, 60, monitor);
        if (res != null && res.decompileCompleted()) {
            DecompiledFunction df = res.getDecompiledFunction();
            String code = df.getC();
            println("=== Decompiled " + f.getName() + " @ " + f.getEntryPoint() + " ===");
            println(code);
        } else {
            println("DECOMPILE FAILED for " + f.getName());
        }
        di.dispose();
    }
}
