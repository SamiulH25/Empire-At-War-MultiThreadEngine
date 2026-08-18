// XrefsTo.java — print callers of a function address.
// Args: address (hex)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;

public class XrefsTo extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String arg = (getScriptArgs().length > 0) ? getScriptArgs()[0] : "1407c3f30";
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(arg);
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a)) {
            Function f = currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
            println("ref from " + r.getFromAddress() + " type=" + r.getReferenceType() +
                    " in " + (f != null ? f.getName() + " @ " + f.getEntryPoint() : "?"));
        }
    }
}
