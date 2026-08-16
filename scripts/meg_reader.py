"""Read Petroglyph .meg archives per the Petrolution format spec.

Format (https://modtools.petrolution.net/docs/MegFileFormat):
  Header:
    +0000h numFilenames uint32
    +0004h numFiles uint32
  Filename Table record:
    +0000h length uint16   ; length of filename in characters
    +0004h name (ASCII, NOT zero-terminated)
  File Table record (sorted by CRC ascending):
    +0000h crc uint32      ; CRC-32 of the filename
    +0004h index uint32
    +0008h size uint32
    +000Ch start uint32    ; offset of file data from start of .meg
    +0010h name uint32     ; index into the Filename Table

Usage:
  from meg_reader import MegaFile
  mf = MegaFile("config.meg")
  print(mf.names())
  data = mf.read("DATA\\SCRIPTS\\AI\\AI_PLAN_EXPANSIONGENERIC.LUA")
"""
import struct, zlib

class MegaFile:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self._data = f.read()
        self._parse_header()

    def _parse_header(self):
        self.num_filenames, self.num_files = struct.unpack_from("<II", self._data, 0)
        # Filename table
        self._filenames = []  # index -> name
        pos = 8
        for _ in range(self.num_filenames):
            (length,) = struct.unpack_from("<H", self._data, pos)
            pos += 2
            name = self._data[pos:pos+length].decode("ascii")
            pos += length
            self._filenames.append(name)
        # File table: (crc, index, size, start, name_idx)
        self._files = []
        for _ in range(self.num_files):
            crc, idx, size, start, name_idx = struct.unpack_from("<IIIII", self._data, pos)
            pos += 20
            self._files.append((crc, idx, size, start, name_idx))

    def names(self):
        return list(self._filenames)

    def read(self, name):
        for crc, idx, size, start, name_idx in self._files:
            stored = self._filenames[name_idx]
            if stored == name:
                return self._data[start:start+size]
        raise KeyError(name)

    def crc_matches(self, name):
        """Verify a file's stored CRC matches the CRC-32 of its name."""
        target = (zlib.crc32(name.encode("ascii")) & 0xFFFFFFFF)
        for crc, idx, size, start, name_idx in self._files:
            if self._filenames[name_idx] == name:
                return crc == target
        return False
