#!/usr/bin/env python3
"""Independent reference for KOReader's util.partialMD5, and the sample table.

KOReader (koreader/koreader, frontend/util.lua, commit
4e973dacb07b592d9a32af505a1c20c6e84f7fd6) does:

    local step, size = 1024, 1024
    local update = md5()
    for i = -1, 10 do
        file:seek("set", lshift(step, 2*i))
        local sample = file:read(size)
        if sample then update(sample) else break end
    end
    return update()

with `lshift = bit.lshift` from LuaJIT, which masks the shift count to 5 bits
and returns a 32-bit result. This script models exactly that (it does not use a
precomputed offset list, so it is independent of the C++ port's formulation)
and prints the digests tests/bookorbit/tst_partial_md5.cpp hardcodes.

    python3 tests/bookorbit/partial_md5_ref.py          # print the table
    python3 tests/bookorbit/partial_md5_ref.py FILE     # digest of one file

Sample files: byte p of a dense file is ((p * 2654435761) >> 13) & 0xFF. The
"sparse" files only hold that pattern inside the 1 KiB windows partialMD5 reads
(zeros elsewhere), so a 1.2 GiB file costs a few KiB on disk.
"""
import hashlib
import os
import sys
import tempfile

CASES = [
    # name, size, sparse
    ("empty", 0, False),
    ("one_byte", 1, False),
    ("size_1023", 1023, False),
    ("size_1024", 1024, False),
    ("size_1025", 1025, False),
    ("size_2048", 2048, False),
    ("size_5120", 5120, False),
    ("size_5121", 5121, False),
    ("size_20000", 20000, False),
    ("size_70000", 70000, False),
    ("size_300000", 300000, False),
    ("size_5MiB_plus_1", 5 * 1024 * 1024 + 1, False),
    ("sparse_1GiB_exactly", 1 << 30, True),
    ("sparse_1GiB_plus_1", (1 << 30) + 1, True),
    ("sparse_1p2GiB", 1288490188, True),
]


def pattern_byte(p):
    return ((p * 2654435761) >> 13) & 0xFF


def windows():
    # Offsets as LuaJIT computes them, for building the sparse files.
    for i in range(-1, 11):
        yield (1024 << ((2 * i) & 31)) & 0xFFFFFFFF


def make_file(path, size, sparse):
    with open(path, "wb") as f:
        if not sparse:
            f.write(bytes(pattern_byte(p) for p in range(size)))
            return
        f.truncate(size)
        for off in windows():
            if off >= size:
                continue
            n = min(1024, size - off)
            f.seek(off)
            f.write(bytes(pattern_byte(off + k) for k in range(n)))


def partial_md5(path):
    md5 = hashlib.md5()
    with open(path, "rb") as f:
        for i in range(-1, 11):
            offset = (1024 << ((2 * i) & 31)) & 0xFFFFFFFF  # LuaJIT bit.lshift
            f.seek(offset)
            sample = f.read(1024)
            if not sample:
                break
            md5.update(sample)
    return md5.hexdigest()


def main(argv):
    if len(argv) > 1:
        print(partial_md5(argv[1]))
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, size, sparse in CASES:
            path = os.path.join(tmp, name)
            make_file(path, size, sparse)
            print(f'    {{"{name}", {size}LL, {"true" if sparse else "false"}, "{partial_md5(path)}"}},')
            os.remove(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
