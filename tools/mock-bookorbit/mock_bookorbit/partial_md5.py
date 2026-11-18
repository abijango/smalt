"""KOReader's util.partialMD5, for the mock's fixture hashes.

Source: koreader/koreader, frontend/util.lua, function util.partialMD5, as of
commit 4e973dacb07b592d9a32af505a1c20c6e84f7fd6 (master):

    local step, size = 1024, 1024
    local update = md5()
    for i = -1, 10 do
        file:seek("set", lshift(step, 2*i))
        local sample = file:read(size)
        if sample then update(sample) else break end
    end

`lshift` is LuaJIT's bit.lshift, which reduces the shift count modulo 32:
for i = -1 the count is -2 & 31 = 30 and 1024 << 30 wraps to 0 in 32 bits, so
the offsets are 0, 1024, 4096, 16384, ... 1024 << 20 (1 GiB).
"""
import hashlib


def sample_offsets():
    offsets = [0]
    for i in range(0, 11):
        offsets.append(1024 << (2 * i))
    return offsets


def partial_md5(data: bytes) -> str:
    md5 = hashlib.md5()
    for offset in sample_offsets():
        sample = data[offset:offset + 1024]
        if not sample:
            break
        md5.update(sample)
    return md5.hexdigest()
