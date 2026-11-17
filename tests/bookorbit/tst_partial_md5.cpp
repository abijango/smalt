// Acceptance: "Partial MD5 matches KOReader's for a set of sample files."
//
// The expected digests come from tests/bookorbit/partial_md5_ref.py, an
// independent Python model of KOReader's util.partialMD5 (LuaJIT shift
// semantics included). Regenerate the table with that script.
#include "smalt/bookorbit_client.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

struct Sample
{
    const char *name;
    qint64 size;
    bool sparse;
    const char *digest;
};

const Sample kSamples[] = {
    { "empty", 0LL, false, "d41d8cd98f00b204e9800998ecf8427e" },
    { "one_byte", 1LL, false, "93b885adfe0da089cdf634904fd59f71" },
    { "size_1023", 1023LL, false, "248ee9d92180cb660efeae327e796163" },
    { "size_1024", 1024LL, false, "57b49e462481dbca8bde8fb0d6ec968e" },
    { "size_1025", 1025LL, false, "f34a1343ac7a3891d8c75bb91a187312" },
    { "size_2048", 2048LL, false, "4194a858d23a2e18261b715497040cc7" },
    { "size_5120", 5120LL, false, "2a462c705c0c697334150f8c5519ac58" },
    { "size_5121", 5121LL, false, "2a462c705c0c697334150f8c5519ac58" },
    { "size_20000", 20000LL, false, "b198b5a856706c64602d88562d569180" },
    { "size_70000", 70000LL, false, "fa50096fb865aa5361e1cbdb083c1d05" },
    { "size_300000", 300000LL, false, "fcdbc62d0e2692cb687a0457ac465750" },
    { "size_5MiB_plus_1", 5242881LL, false, "c2afe458a99b4c9434ed65e0fe55e8c5" },
    { "sparse_1GiB_exactly", 1073741824LL, true, "10d78962a7ca64154b54c37ab2901335" },
    { "sparse_1GiB_plus_1", 1073741825LL, true, "9648d650ee9acdb658c357d8d378cf57" },
    { "sparse_1p2GiB", 1288490188LL, true, "5dd03addd39cf3ad464be46d936cf224" },
};

// Same pattern as partial_md5_ref.py: byte p is ((p * 2654435761) >> 13) & 0xFF.
quint8 patternByte(quint64 p)
{
    return static_cast<quint8>(((p * 2654435761ULL) >> 13) & 0xFF);
}

QByteArray patternBytes(quint64 from, qint64 count)
{
    QByteArray out(static_cast<int>(count), Qt::Uninitialized);
    for (qint64 k = 0; k < count; ++k)
        out[static_cast<int>(k)] = static_cast<char>(patternByte(from + k));
    return out;
}

bool writeSample(const QString &path, qint64 size, bool sparse)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (!sparse) {
        for (qint64 off = 0; off < size; off += 65536)
            if (f.write(patternBytes(off, qMin<qint64>(65536, size - off))) < 0)
                return false;
        return true;
    }
    if (!f.resize(size))
        return false;
    // LuaJIT's lshift: offsets 0 and 1024 << 2i (i = 0..10), inside the file.
    QVector<qint64> offsets;
    offsets << 0;
    for (int i = 0; i <= 10; ++i)
        offsets << (qint64(1024) << (2 * i));
    for (int i = 0; i < offsets.size(); ++i) {
        if (offsets[i] >= size)
            continue;
        f.seek(offsets[i]);
        if (f.write(patternBytes(offsets[i], qMin<qint64>(1024, size - offsets[i]))) < 0)
            return false;
    }
    return true;
}

} // namespace

class TstPartialMd5 : public QObject
{
    Q_OBJECT

private slots:

    void matchesKoreader_data()
    {
        QTest::addColumn<qint64>("size");
        QTest::addColumn<bool>("sparse");
        QTest::addColumn<QString>("digest");
        for (const Sample &s : kSamples)
            QTest::newRow(s.name) << s.size << s.sparse << QString::fromLatin1(s.digest);
    }

    void matchesKoreader()
    {
        QFETCH(qint64, size);
        QFETCH(bool, sparse);
        QFETCH(QString, digest);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("sample.bin"));
        if (!writeSample(path, size, sparse))
            QSKIP("cannot create the sample file on this file system");
        QCOMPARE(smalt::partialMd5(path), digest);
    }

    void unreadBytesDoNotMatter()
    {
        // Bytes outside the sampled windows (here 2048..4095 and past 5120) are never read.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(QStringLiteral("a.bin"));
        const QString b = dir.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeSample(a, 20000, false));
        QVERIFY(QFile::copy(a, b));
        QFile f(b);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(3000);
        f.write("changed");
        f.seek(10000);
        f.write("changed");
        f.close();
        QCOMPARE(smalt::partialMd5(b), smalt::partialMd5(a));

        // ... while a byte inside a window does change it (offset 4096..5119).
        QFile g(b);
        QVERIFY(g.open(QIODevice::ReadWrite));
        g.seek(4100);
        g.write("X");
        g.close();
        QVERIFY(smalt::partialMd5(b) != smalt::partialMd5(a));
    }

    void unreadableFileGivesEmptyString()
    {
        QVERIFY(smalt::partialMd5(QStringLiteral("/nonexistent/smalt/none.epub")).isEmpty());
        QVERIFY(smalt::partialMd5(QString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstPartialMd5)
#include "tst_partial_md5.moc"
