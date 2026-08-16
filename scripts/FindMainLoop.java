// FindMainLoop.java — locate the game loop entry and its per-frame phase calls.
// Runs via analyzeHeadless -process StarWarsG.exe -noanalysis -postScript FindMainLoop.java
// Output: prints entry chain WinMain -> game loop -> phase calls (to stdout/log).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

public class FindMainLoop extends GhidraScript {

    @Override
    protected void run() throws Exception {
        println("=== FindMainLoop: " + currentProgram.getName());

        // 1) Entry point
        Address entry = currentProgram.getSymbolTable().getExternalEntryPointIterator().next();
        println("Entry: " + entry);

        // 2) Walk from entry: find functions; look for the one that calls many things
        //    (the game loop). Heuristic: list functions with > 20 outgoing calls.
        FunctionManager fm = currentProgram.getFunctionManager();
        println("Total functions: " + fm.getFunctionCount());
        int count = 0;
        for (Function f : fm.getFunctions(true)) {
            int callCount = 0;
            for (Instruction insn : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                if (insn.getFlowType().isCall()) callCount++;
            }
            if (callCount >= 25) {
                println("HOT-CALLS func=" + f.getName() + " @ " + f.getEntryPoint()
                        + " calls=" + callCount);
                if (++count > 40) break;
            }
        }

        // 3) Find WinMain-ish: functions that reference RegisterClassW / CreateWindowExW
        SymbolTable st = currentProgram.getSymbolTable();
        for (Symbol s : st.getAllSymbols(false)) {
            String n = s.getName();
            if (n.equals("RegisterClassW") || n.equals("CreateWindowExW")
                    || n.equals("GetMessageW") || n.equals("PeekMessageW")) {
                for (Reference r : currentProgram.getReferenceManager().getReferencesTo(s.getAddress())) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    if (f != null) {
                        println("WINDOW-API " + n + " referenced from " + f.getName()
                                + " @ " + f.getEntryPoint());
                    }
                }
            }
        }
        println("=== FindMainLoop done");
    }
}
