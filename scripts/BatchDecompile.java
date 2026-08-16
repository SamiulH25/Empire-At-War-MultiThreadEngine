// BatchDecompile.java — decompile multiple functions (args: addr1 addr2 ...)
// and print first 20 lines of each.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;

public class BatchDecompile extends GhidraScript {

    @Override
    protected void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (String arg : getScriptArgs()) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(arg);
            Function f = fm.getFunctionContaining(a);
            if (f == null) { println("NO FUNC " + arg); continue; }
            DecompileResults res = di.decompileFunction(f, 60, monitor);
            if (res != null && res.decompileCompleted()) {
                String[] lines = res.getDecompiledFunction().getC().split("\n");
                println("=== " + arg + " (" + f.getName() + ") ===");
                for (int i = 0; i < Math.min(lines.length, 20); i++) println(lines[i]);
            } else {
                println("DECOMPILE FAILED " + arg);
            }
        }
        di.dispose();
    }
}
