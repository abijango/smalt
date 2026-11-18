// KOReader's util.partialMD5, ported exactly.
//
// Source: koreader/koreader, frontend/util.lua, util.partialMD5, commit
// 4e973dacb07b592d9a32af505a1c20c6e84f7fd6:
//
//     local step, size = 1024, 1024
//     local update = md5()
//     for i = -1, 10 do
//         file:seek("set", lshift(step, 2*i))
//         local sample = file:read(size)
//         if sample then update(sample) else break end
//     end
//     file:close()
//     return update()
//
// `lshift` is LuaJIT's bit.lshift, which reduces the shift count modulo 32. So
// for i = -1 the count is (-2 & 31) = 30 and 1024 << 30 wraps to 0 in 32 bits:
// the first sample is the start of the file. The other offsets are
// 1024 << (2 * i) for i = 0..10: 1 KiB, 4 KiB, 16 KiB ... 1 GiB. Each sample is
// up to 1024 bytes; the first one that reads nothing (end of file) stops the
// loop, so a file shorter than an offset contributes only the samples before it.
#include "smalt/bookorbit_client.h"

#include <QCryptographicHash>
#include <QFile>

namespace smalt {

QString partialMd5(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash md5(QCryptographicHash::Md5);
    constexpr qint64 kStep = 1024;
    constexpr qint64 kSize = 1024;
    for (int i = -1; i <= 10; ++i) {
        const qint64 offset = i < 0 ? 0 : (kStep << (2 * i)); // see the shift note above
        if (!file.seek(offset))
            break;
        const QByteArray sample = file.read(kSize);
        if (sample.isEmpty())
            break; // Lua: read() returns nil at end of file
        md5.addData(sample);
    }
    return QString::fromLatin1(md5.result().toHex());
}

} // namespace smalt
