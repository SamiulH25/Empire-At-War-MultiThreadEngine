"""Test: confirm PyGhidra script runs headless and can access the program.

Run via:
  analyzeHeadless.bat <proj> eawea -process StarWarsG.exe -noanalysis \
      -scriptPath <repo>/scripts -postScript pyg_test.py
"""
# PyGhidra (CPython 3 + JPype) exposes Ghidra classes via the ghidra package.
from ghidra.program.model.listing import CodeUnit
import sys

def main():
    program = getCurrentProgram()  # provided by the GhidraScript harness
    print("PYG_TEST_OK program=%s language=%s" % (program.getName(), program.getLanguage().getLanguageID()))

main()
