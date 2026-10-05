// Mp4Atoms on hand-built MP4 structures (no ffmpeg needed).
#include "core/Mp4Atoms.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QByteArray be32(quint32 v)
{
    QByteArray b(4, 0);
    b[0] = char(v >> 24);
    b[1] = char(v >> 16);
    b[2] = char(v >> 8);
    b[3] = char(v);
    return b;
}

QByteArray box(const char *type, const QByteArray &payload)
{
    return be32(quint32(8 + payload.size())) + QByteArray(type, 4) + payload;
}

// A box with a 64-bit size field ("mdat" of large files).
QByteArray box64(const char *type, const QByteArray &payload)
{
    QByteArray size(8, 0);
    const quint64 total = 16 + quint64(payload.size());
    for (int i = 0; i < 8; ++i)
        size[7 - i] = char((total >> (8 * i)) & 0xff);
    return be32(1) + QByteArray(type, 4) + size + payload;
}

const QByteArray kAmba = box("AMBA", QByteArray("\x00\x00\x00\x00kAMBAxV4 settings", 22));
const QByteArray kOther = box("XTRA", QByteArray(30, 'x')); // not on the allow-list
const QByteArray kMeta = box("meta", QByteArray(24, 'm'));
const QByteArray kTrak = box("trak", QByteArray(200, 't'));
const QByteArray kMvhd = box("mvhd", QByteArray(100, 'h'));
const QByteArray kFtyp = box("ftyp", QByteArray("isom\0\0\2\0isomiso2", 16));
const QByteArray kMdat = box("mdat", QByteArray(1000, 'D'));

QByteArray moovWith(const QByteArray &udtaChildren, bool trakAfterUdta = true)
{
    QByteArray inner = kMvhd;
    if (!udtaChildren.isNull())
        inner += box("udta", udtaChildren);
    if (trakAfterUdta)
        inner += kTrak;
    return box("moov", inner);
}

QString write(const QTemporaryDir &dir, const QString &name, const QByteArray &data)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        qFatal("cannot write test file");
    return path;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QStringList types(const QList<QByteArray> &list)
{
    QStringList out;
    for (const QByteArray &t : list)
        out << QString::fromLatin1(t);
    return out;
}

} // namespace

class TstMp4Atoms : public QObject
{
    Q_OBJECT

private slots:
    void readsTheTopLevel()
    {
        QTemporaryDir dir;
        const QString p = write(dir, "a.mp4", kFtyp + kMdat + moovWith(kAmba));
        bool covers = false;
        const auto top = Mp4Atoms::topLevel(p, &covers);
        QVERIFY(covers);
        QCOMPARE(top.size(), qsizetype(3));
        QCOMPARE(top.at(0).type, QByteArray("ftyp"));
        QCOMPARE(top.at(1).type, QByteArray("mdat"));
        QCOMPARE(top.at(1).offset, qint64(kFtyp.size()));
        QCOMPARE(top.at(1).size, qint64(kMdat.size()));
        QCOMPARE(top.at(2).type, QByteArray("moov"));
        QCOMPARE(top.at(2).end(), qint64(QFileInfo(p).size()));
    }

    void understands64BitSizes()
    {
        QTemporaryDir dir;
        // The real Insta360 files use a 64-bit mdat size.
        const QString p = write(dir, "big.mp4", kFtyp + box64("mdat", QByteArray(500, 'D')) + moovWith(kAmba));
        bool covers = false;
        const auto top = Mp4Atoms::topLevel(p, &covers);
        QVERIFY(covers);
        QCOMPARE(top.size(), qsizetype(3));
        QCOMPARE(top.at(1).headerSize, 16);
        QCOMPARE(top.at(1).size, qint64(16 + 500));
        QCOMPARE(types(Mp4Atoms::udtaTypes(p)), QStringList{"AMBA"});
    }

    void sizeZeroMeansUntilTheEnd()
    {
        QTemporaryDir dir;
        QByteArray moov = moovWith(kAmba);
        moov.replace(0, 4, be32(0)); // "last box, runs to the end of the file"
        const QString p = write(dir, "zero.mp4", kFtyp + kMdat + moov);
        bool covers = false;
        const auto top = Mp4Atoms::topLevel(p, &covers);
        QVERIFY(covers);
        QCOMPARE(top.last().type, QByteArray("moov"));
        QCOMPARE(types(Mp4Atoms::udtaTypes(p)), QStringList{"AMBA"});
    }

    void vendorAtomsOnlyFromTheAllowList()
    {
        QTemporaryDir dir;
        const QString p = write(dir, "a.mp4", kFtyp + kMdat + moovWith(kMeta + kAmba + kOther));
        QCOMPARE(types(Mp4Atoms::udtaTypes(p)), (QStringList{"meta", "AMBA", "XTRA"}));
        const auto vendor = Mp4Atoms::vendorAtoms(p);
        QCOMPARE(vendor.size(), qsizetype(1));
        QCOMPARE(vendor.first(), kAmba); // complete box, byte for byte
    }

    void noUdtaMeansNoAtoms()
    {
        QTemporaryDir dir;
        const QString p = write(dir, "a.mp4", kFtyp + kMdat + moovWith(QByteArray()));
        QVERIFY(Mp4Atoms::udtaTypes(p).isEmpty());
        QVERIFY(Mp4Atoms::vendorAtoms(p).isEmpty());
    }

    void oversizedVendorAtomIsIgnored()
    {
        QTemporaryDir dir;
        const QByteArray huge = box("AMBA", QByteArray(2 * 1024 * 1024, 'x'));
        const QString p = write(dir, "a.mp4", kFtyp + kMdat + moovWith(huge));
        QVERIFY(Mp4Atoms::vendorAtoms(p).isEmpty());
    }

    // ---- addUdtaAtoms ----
    void appendsToAnExistingUdtaWithoutTouchingAnythingElse()
    {
        QTemporaryDir dir;
        const QByteArray original = kFtyp + kMdat + moovWith(kMeta); // udta, then a trak after it
        const QString p = write(dir, "a.mp4", original);
        QString err;
        QVERIFY2(Mp4Atoms::addUdtaAtoms(p, {kAmba}, &err), qPrintable(err));

        const QByteArray now = readAll(p);
        QCOMPARE(now.size(), original.size() + kAmba.size());
        // Everything before moov (ftyp + mdat: the media data) is unchanged: nothing moved.
        QCOMPARE(now.left(kFtyp.size() + kMdat.size()), original.left(kFtyp.size() + kMdat.size()));

        bool covers = false;
        const auto top = Mp4Atoms::topLevel(p, &covers);
        QVERIFY(covers);
        QCOMPARE(top.last().type, QByteArray("moov"));
        QCOMPARE(types(Mp4Atoms::udtaTypes(p)), (QStringList{"meta", "AMBA"}));
        QCOMPARE(Mp4Atoms::vendorAtoms(p).first(), kAmba);

        // The trak that followed udta inside moov is intact (and still found after udta).
        const int trakAt = now.indexOf(kTrak);
        QVERIFY(trakAt > 0);
        QCOMPARE(now.mid(trakAt, kTrak.size()), kTrak);
        QVERIFY(now.indexOf(kAmba) < trakAt);
    }

    void createsUdtaWhenMissing()
    {
        QTemporaryDir dir;
        const QString p = write(dir, "a.mp4", kFtyp + kMdat + moovWith(QByteArray()));
        QString err;
        QVERIFY2(Mp4Atoms::addUdtaAtoms(p, {kAmba}, &err), qPrintable(err));
        bool covers = false;
        Mp4Atoms::topLevel(p, &covers);
        QVERIFY(covers);
        QCOMPARE(types(Mp4Atoms::udtaTypes(p)), QStringList{"AMBA"});
        QCOMPARE(Mp4Atoms::vendorAtoms(p).first(), kAmba);
    }

    void doesNotDuplicate()
    {
        QTemporaryDir dir;
        const QByteArray original = kFtyp + kMdat + moovWith(kAmba);
        const QString p = write(dir, "a.mp4", original);
        QVERIFY(Mp4Atoms::addUdtaAtoms(p, {kAmba}));
        QCOMPARE(readAll(p), original); // already there: the file is not touched
        QVERIFY(Mp4Atoms::addUdtaAtoms(p, {}));
        QCOMPARE(readAll(p), original);

        // Several atoms, one of them a duplicate of another in the same call.
        const QString q = write(dir, "b.mp4", kFtyp + kMdat + moovWith(kMeta));
        QVERIFY(Mp4Atoms::addUdtaAtoms(q, {kAmba, kAmba}));
        QCOMPARE(types(Mp4Atoms::udtaTypes(q)), (QStringList{"meta", "AMBA"}));
    }

    void refusesWhenMoovIsNotLast()
    {
        QTemporaryDir dir;
        // "faststart" layout: moov before the media data. Growing it would shift every sample offset.
        const QByteArray original = kFtyp + moovWith(kMeta) + kMdat;
        const QString p = write(dir, "fast.mp4", original);
        QString err;
        QVERIFY(!Mp4Atoms::addUdtaAtoms(p, {kAmba}, &err));
        QVERIFY(!err.isEmpty());
        QCOMPARE(readAll(p), original); // untouched
    }

    void refusesWhenTheLastBoxIsNotMoov()
    {
        QTemporaryDir dir;
        // moov in the middle, a small valid box last: nothing "looks" broken, only the type tells.
        const QByteArray original = kFtyp + moovWith(kMeta) + kMdat + box("free", QByteArray());
        const QString p = write(dir, "mid.mp4", original);
        QString err;
        QVERIFY(!Mp4Atoms::addUdtaAtoms(p, {kAmba}, &err));
        QVERIFY(!err.isEmpty());
        QCOMPARE(readAll(p), original);
    }

    void refusesWhenSomethingFollowsMoov()
    {
        QTemporaryDir dir;
        const QByteArray original = kFtyp + kMdat + moovWith(kMeta) + QByteArray(40, 'G'); // trailing bytes
        const QString p = write(dir, "trail.mp4", original);
        QString err;
        QVERIFY(!Mp4Atoms::addUdtaAtoms(p, {kAmba}, &err));
        QVERIFY(!err.isEmpty());
        QCOMPARE(readAll(p), original);
    }

    void survivesDamagedInput()
    {
        QTemporaryDir dir;
        QString err;
        const QList<QByteArray> cases = {
            QByteArray(),                                              // empty
            QByteArray("not an mp4 at all, just text"),                // garbage
            kFtyp.left(5),                                             // cut inside a header
            kFtyp + box("mdat", QByteArray(10, 'x')).left(12),         // box larger than the file
            kFtyp + kMdat + moovWith(kMeta).left(40),                  // truncated moov
            kFtyp + kMdat + box("moov", be32(9999) + QByteArray("udta")), // udta bigger than moov
        };
        int n = 0;
        for (const QByteArray &c : cases) {
            const QString p = write(dir, QString("bad%1.mp4").arg(n++), c);
            (void)Mp4Atoms::topLevel(p);
            (void)Mp4Atoms::udtaTypes(p);
            (void)Mp4Atoms::vendorAtoms(p);
            QVERIFY2(!Mp4Atoms::addUdtaAtoms(p, {kAmba}, &err), qPrintable(QString("case %1").arg(n)));
            QCOMPARE(readAll(p), c); // never modified
        }
        QVERIFY(Mp4Atoms::topLevel(dir.filePath("does-not-exist")).isEmpty());
        QVERIFY(Mp4Atoms::vendorAtoms(dir.filePath("does-not-exist")).isEmpty());
        QVERIFY(!Mp4Atoms::addUdtaAtoms(dir.filePath("does-not-exist"), {kAmba}, &err));
    }

    void rejectsMalformedAtomsToAdd()
    {
        QTemporaryDir dir;
        const QByteArray original = kFtyp + kMdat + moovWith(kMeta);
        const QString p = write(dir, "a.mp4", original);
        QString err;
        QByteArray wrongSize = kAmba;
        wrongSize.replace(0, 4, be32(999));
        QVERIFY(!Mp4Atoms::addUdtaAtoms(p, {wrongSize}, &err));
        QVERIFY(!Mp4Atoms::addUdtaAtoms(p, {QByteArray("tiny")}, &err));
        QCOMPARE(readAll(p), original);
    }
};

QTEST_APPLESS_MAIN(TstMp4Atoms)
#include "tst_mp4atoms.moc"
