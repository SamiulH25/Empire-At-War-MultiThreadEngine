// FindLuaUndump.java — locate the function that references the "\x1bLup"
// magic bytes and decompile it (the game's custom bytecode loader).
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.listing.Program;

public class FindLuaUndump extends GhidraScript {
    @Override
    public void run() throws Exception {
        Program prog = currentProgram;
        println("program: " + prog.getName() + " base " + prog.getImageBase());

        // Find the magic bytes \x1bLup in memory.
        byte[] magic = new byte[]{(byte) 0x1b, 0x4c, 0x75, 0x70};
        Address magicAddr = null;
        Memory mem = prog.getMemory();
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized()) continue;
            Address found = mem.findBytes(blk.getStart(), blk.getEnd(), magic, null, true, monitor);
            if (found != null) { magicAddr = found; break; }
        }
        if (magicAddr == null) { println("magic not found"); return; }
        println("magic at " + magicAddr);

        // Find references to it.
        Function target = null;
        for (Reference r : prog.getReferenceManager().getReferencesTo(magicAddr)) {
            Function f = prog.getFunctionManager().getFunctionContaining(r.getFromAddress());
            println("  ref from " + r.getFromAddress() + " -> " + (f != null ? f.getName() : "?"));
            if (f != null && target == null) target = f;
        }
        if (target == null) {
            println("no function refs to magic");
            return;
        }
        println("=== Decompiling " + target.getName() + " @ " + target.getEntryPoint() + " ===");
        DecompInterface di = new DecompInterface();
        di.openProgram(prog);
        DecompileResults res = di.decompileFunction(target, 120, monitor);
        if (res != null && res.decompileCompleted()) {
            println(res.getDecompiledFunction().getC());
        } else {
            println("decompile failed");
        }
        di.dispose();
    }
}
