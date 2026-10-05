#include "core/Insta360Trailer.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QByteArray u32le(quint32 v)
{
    QByteArray b(4, 0);
    b[0] = char(v & 0xff);
    b[1] = char((v >> 8) & 0xff);
    b[2] = char((v >> 16) & 0xff);
    b[3] = char((v >> 24) & 0xff);
    return b;
}

// payload + [size][version][magic], size counting the footer itself.
QByteArray makeTrailer(const QByteArray &payload, quint32 version = 3)
{
    return payload + u32le(quint32(payload.size() + Insta360Trailer::kFooterSize)) + u32le(version) +
           QByteArray(Insta360Trailer::kMagic, 32);
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(data), data.size());
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TstInsta360Trailer : public QObject
{
    Q_OBJECT

private slots:
    void detectsTheTrailer()
    {
        QTemporaryDir dir;
        const QByteArray mp4(5000, 'M');
        const QByteArray payload("calibration|gyro|" + QByteArray(300, 'x'));
        writeFile(dir.filePath("a.insv"), mp4 + makeTrailer(payload));

        const auto info = Insta360Trailer::detect(dir.filePath("a.insv"));
        QVERIFY(info.has_value());
        QCOMPARE(info->offset, qint64(5000)); // right where the MP4 data ends
        QCOMPARE(info->size, qint64(payload.size() + Insta360Trailer::kFooterSize));
        QCOMPARE(info->version, quint32(3));
    }

    void rejectsFilesWithoutTrailer()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("plain.mp4"), QByteArray(5000, 'M'));
        QVERIFY(!Insta360Trailer::detect(dir.filePath("plain.mp4")).has_value());
        writeFile(dir.filePath("tiny"), QByteArray(10, 'x'));
        QVERIFY(!Insta360Trailer::detect(dir.filePath("tiny")).has_value());
        writeFile(dir.filePath("empty"), QByteArray());
        QVERIFY(!Insta360Trailer::detect(dir.filePath("empty")).has_value());
        QVERIFY(!Insta360Trailer::detect(dir.filePath("does-not-exist")).has_value());
    }

    void rejectsInconsistentFooters()
    {
        QTemporaryDir dir;
        // Magic present but the size claims more than the file holds.
        QByteArray bad = QByteArray(100, 'x') + u32le(5000) + u32le(3) + QByteArray(Insta360Trailer::kMagic, 32);
        writeFile(dir.filePath("toobig"), bad);
        QVERIFY(!Insta360Trailer::detect(dir.filePath("toobig")).has_value());
        // Size smaller than the footer itself.
        bad = QByteArray(100, 'x') + u32le(10) + u32le(3) + QByteArray(Insta360Trailer::kMagic, 32);
        writeFile(dir.filePath("toosmall"), bad);
        QVERIFY(!Insta360Trailer::detect(dir.filePath("toosmall")).has_value());
        // Wrong magic.
        QByteArray wrong = makeTrailer("abc");
        wrong[wrong.size() - 1] = 'X';
        writeFile(dir.filePath("wrongmagic"), QByteArray(100, 'x') + wrong);
        QVERIFY(!Insta360Trailer::detect(dir.filePath("wrongmagic")).has_value());
    }

    void appendCopiesExactlyTheTrailer()
    {
        QTemporaryDir dir;
        const QByteArray mp4(5000, 'M');
        QByteArray payload;
        for (int i = 0; i < 3 * 1024 * 1024 + 17; ++i) // > 3 chunks, odd length
            payload.append(char(i * 31 + 7));
        const QByteArray trailer = makeTrailer(payload);
        writeFile(dir.filePath("src.insv"), mp4 + trailer);
        const auto info = Insta360Trailer::detect(dir.filePath("src.insv"));
        QVERIFY(info.has_value());

        const QByteArray out(777, 'O'); // an exported file, without trailer
        writeFile(dir.filePath("out.insv"), out);
        QString err;
        QVERIFY2(Insta360Trailer::append(dir.filePath("src.insv"), *info, dir.filePath("out.insv"), &err),
                 qPrintable(err));
        QCOMPARE(readFile(dir.filePath("out.insv")), out + trailer);

        // The result is itself recognised, with the same trailer.
        const auto again = Insta360Trailer::detect(dir.filePath("out.insv"));
        QVERIFY(again.has_value());
        QCOMPARE(again->offset, qint64(out.size()));
        QCOMPARE(again->size, info->size);
    }

    void appendReportsErrors()
    {
        QTemporaryDir dir;
        QString err;
        Insta360Trailer::Info info;
        info.offset = 0;
        info.size = 100;
        QVERIFY(!Insta360Trailer::append(dir.filePath("missing"), info, dir.filePath("out"), &err));
        QVERIFY(!err.isEmpty());

        writeFile(dir.filePath("small"), QByteArray(10, 'x'));
        QVERIFY(!Insta360Trailer::append(dir.filePath("small"), info, dir.filePath("out"), &err)); // range > file
        QVERIFY(!QFile::exists(dir.filePath("out")) || QFileInfo(dir.filePath("out")).size() == 0);
    }
};

QTEST_APPLESS_MAIN(TstInsta360Trailer)
#include "tst_insta360trailer.moc"
