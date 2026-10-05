// The records inside the Insta360 trailer: reading, rebuilding and cutting them to a clip.
#include "core/Insta360Trailer.h"
#include "insta360_testdata.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <cstring>

using namespace Insta360Trailer;
using namespace Insta360TestData;

namespace {

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        qFatal("cannot write test file");
    return path;
}

} // namespace

class TstInsta360Trim : public QObject
{
    Q_OBJECT

private slots:
    // ---- reading and rebuilding ----
    void buildThenReadRoundTrips()
    {
        QTemporaryDir dir;
        const Parsed original = makeTrailer();
        const QByteArray mp4(1234, 'M');
        const QString path = writeFile(dir, "a.insv", mp4 + build(original));

        QString err;
        const auto back = read(path, &err);
        QVERIFY2(back.has_value(), qPrintable(err));
        QCOMPARE(back->version, original.version);
        QCOMPARE(back->records.size(), original.records.size());
        for (qsizetype i = 0; i < original.records.size(); ++i) {
            QCOMPARE(back->records.at(i).id, original.records.at(i).id);
            QCOMPARE(back->records.at(i).data, original.records.at(i).data);
        }
        QCOMPARE(build(*back), build(original)); // byte for byte
        const auto info = detect(path);
        QVERIFY(info.has_value());
        QCOMPARE(info->offset, qint64(mp4.size()));
        QCOMPARE(info->size, qint64(build(original).size()));
    }

    void footerIsWellFormed()
    {
        const QByteArray t = build(makeTrailer());
        QCOMPARE(t.right(32), QByteArray(kMagic, 32));
        QCOMPARE(qint64(readLe(t, t.size() - 40, 4)), qint64(t.size())); // size covers everything
        QCOMPARE(qint64(readLe(t, t.size() - 36, 4)), qint64(3));        // version
        // The last record's header starts the 78-byte tail: id 0x0101 and its length.
        QCOMPARE(qint64(readLe(t, t.size() - 78, 2)), qint64(0x0101));
    }

    void readRejectsDamage()
    {
        QTemporaryDir dir;
        QString err;
        QByteArray good = build(makeTrailer());
        QVERIFY(read(writeFile(dir, "ok.insv", QByteArray(10, 'M') + good), &err).has_value());

        // A record that claims to be longer than the trailer.
        QByteArray bad = good;
        const qsizetype lastHeader = bad.size() - 78;
        bad[lastHeader + 2] = char(0xff);
        bad[lastHeader + 3] = char(0xff);
        bad[lastHeader + 4] = char(0xff);
        bad[lastHeader + 5] = char(0x7f);
        QVERIFY(!read(writeFile(dir, "long.insv", QByteArray(10, 'M') + bad), &err).has_value());
        QVERIFY(!err.isEmpty());

        // Records that do not add up to the stated size (a byte too many in front).
        QByteArray gap = good;
        gap[gap.size() - 40] = char(gap[gap.size() - 40] + 1); // size field says 1 more than the records
        const QByteArray withExtra = QByteArray(10, 'M') + QByteArray(1, 'X') + gap;
        QVERIFY(!read(writeFile(dir, "gap.insv", withExtra), &err).has_value());

        QVERIFY(!read(writeFile(dir, "none.insv", QByteArray(100, 'M')), &err).has_value()); // no trailer at all
        QVERIFY(!read(dir.filePath("missing"), &err).has_value());
    }

    // ---- cutting ----
    void motionDataIsCutToTheClipAndMoved()
    {
        const Parsed src = makeTrailer();
        Trim trim;
        trim.startMs = 3000;
        trim.durationMs = 2000;
        trim.mp4Length = 5000000;
        QString err;
        const auto out = trimmed(src, trim, &err);
        QVERIFY2(out.has_value(), qPrintable(err));

        // First frame of the source = first exposure entry = 2867. Clip: [5867, 7867] on that clock;
        // kept with 200 ms before and 300 ms after: [5667, 8167]; moved by -3000.
        const auto times = motionTimes(*find(*out, 0x0300));
        QCOMPARE(times.size(), qsizetype(1251));
        QCOMPARE(times.first(), qint64(5667 - 3000));
        QCOMPARE(times.last(), qint64(8167 - 3000));
        for (qsizetype i = 1; i < times.size(); ++i)
            QCOMPARE(times.at(i) - times.at(i - 1), qint64(2)); // still 500 Hz, nothing lost in between

        // The measurements themselves are untouched: only the time stamp changed.
        const Record &srcMotion = *find(src, 0x0300);
        const Record &outMotion = *find(*out, 0x0300);
        const qsizetype firstSrc = (5667 - 2835) / 2;
        QCOMPARE(outMotion.data.mid(8, 48), srcMotion.data.mid(firstSrc * 56 + 8, 48));
        QCOMPARE(outMotion.data.mid(1250 * 56 + 8, 48), srcMotion.data.mid((firstSrc + 1250) * 56 + 8, 48));
    }

    void videoStartOffsetIsHonoured()
    {
        Trim trim;
        trim.startMs = 3000;
        trim.durationMs = 2000;
        trim.videoStartMs = 94; // the exported file's first frame is shown at 0.094 s
        trim.mp4Length = 1;
        const auto out = trimmed(makeTrailer(), trim);
        QVERIFY(out.has_value());
        QCOMPARE(motionTimes(*find(*out, 0x0300)).first(), qint64(5667 - 3000 + 94));
        QCOMPARE(exposureEntries(*find(*out, 0x0400)).first().first, qint64(2867 + 94));
    }

    void exposureKeepsTheValueInEffectAtTheStart()
    {
        Trim trim;
        trim.startMs = 3000; // clock 5867
        trim.durationMs = 2000;
        trim.mp4Length = 1;
        auto out = trimmed(makeTrailer(), trim);
        QVERIFY(out.has_value());
        // In effect at 5867: the entry of 4226 (0.0016). Nothing else falls inside [5867, 8167].
        auto e = exposureEntries(*find(*out, 0x0400));
        QCOMPARE(e.size(), qsizetype(1));
        QCOMPARE(e.first().first, qint64(2867)); // the clip's first frame sits where the source's did
        QCOMPARE(e.first().second, 0.0016);

        // A longer clip takes in later changes, moved the same way.
        trim.durationMs = 8000; // up to clock 13867 (+300): the change at 9000 is inside, 15000 is not
        out = trimmed(makeTrailer(), trim);
        QVERIFY(out.has_value());
        e = exposureEntries(*find(*out, 0x0400));
        QCOMPARE(e.size(), qsizetype(2));
        QCOMPARE(e.at(1).first, qint64(9000 - 3000));
        QCOMPARE(e.at(1).second, 0.001);
    }

    // Boundaries: an entry exactly at the start of the clip is the one in effect (not a second, later one);
    // an entry exactly at the end of the margin is still kept, one a millisecond later is not.
    void exposureEntriesOnTheBoundaries()
    {
        auto withEntries = [](const QList<std::pair<qint64, double>> &list) {
            Parsed t = makeTrailer();
            QByteArray data;
            for (const auto &e : list) {
                le(data, quint64(e.first), 8);
                quint64 raw;
                std::memcpy(&raw, &e.second, 8);
                le(data, raw, 8);
            }
            for (Record &r : t.records) {
                if (r.id == 0x0400)
                    r.data = data;
            }
            return t;
        };
        Trim trim;
        trim.startMs = 3000; // clock 5867; clip to 7867; the margin keeps entries up to 8167
        trim.durationMs = 2000;
        trim.mp4Length = 1;

        auto out = trimmed(withEntries({{2867, 0.002}, {4226, 0.0016}, {5867, 0.0013}, {8167, 0.0007}, {8168, 0.0006}}), trim);
        QVERIFY(out.has_value());
        const auto e = exposureEntries(*find(*out, 0x0400));
        QCOMPARE(e.size(), qsizetype(2));
        QCOMPARE(e.at(0).first, qint64(2867));    // the entry AT the start is the one carried over ...
        QCOMPARE(e.at(0).second, 0.0013);         // ... not the earlier 0.0016, and not twice
        QCOMPARE(e.at(1).first, qint64(8167 - 3000)); // at the far end of the margin: kept
        QCOMPARE(e.at(1).second, 0.0007);             // (8168 is one millisecond too late)

        // Motion samples on the margins: 5667 (start - 200) and 8167 (end + 300) are in, one step outside is not.
        const auto times = motionTimes(*find(*trimmed(makeTrailer(), trim), 0x0300));
        QCOMPARE(times.first(), qint64(5667 - 3000));
        QCOMPARE(times.last(), qint64(8167 - 3000));
        QVERIFY(!times.contains(5665 - 3000) && !times.contains(8169 - 3000));
    }

    void startOfTheVideoNeedsNoCarriedOverEntry()
    {
        Trim trim;
        trim.startMs = 0; // clock 2867: the first exposure entry itself is "in effect"
        trim.durationMs = 3000;
        trim.mp4Length = 1;
        const auto out = trimmed(makeTrailer(), trim);
        QVERIFY(out.has_value());
        const auto e = exposureEntries(*find(*out, 0x0400));
        QCOMPARE(e.first().first, qint64(2867));
        QCOMPARE(e.first().second, 0.002);
        // 0 ms in: motion starts 200 ms before the clip, i.e. at the start of the data (2835 = clock 32 ms early).
        QCOMPARE(motionTimes(*find(*out, 0x0300)).first(), qint64(2835));
    }

    void infoRecordDescribesTheNewFile()
    {
        const Parsed src = makeTrailer();
        Trim trim;
        trim.startMs = 3000;
        trim.durationMs = 5120; // -> 5 s
        trim.mp4Length = 29245423;
        const auto out = trimmed(src, trim);
        QVERIFY(out.has_value());
        const auto before = pbFields(find(src, 0x0101)->data);
        const auto after = pbFields(find(*out, 0x0101)->data);
        QCOMPARE(after.size(), before.size()); // same fields in the same order
        for (qsizetype i = 0; i < before.size(); ++i) {
            QCOMPARE(after.at(i).field, before.at(i).field);
            if (before.at(i).field == 9)
                QCOMPARE(after.at(i).raw, varint(29245423));
            else if (before.at(i).field == 10)
                QCOMPARE(after.at(i).raw, varint(5));
            else
                QCOMPARE(after.at(i).raw, before.at(i).raw); // everything else: byte for byte (strings, doubles ...)
        }
        // Durations round to whole seconds, never to 0.
        trim.durationMs = 400;
        QCOMPARE(pbFields(find(*trimmed(src, trim), 0x0101)->data).at(3).raw, varint(1));
        trim.durationMs = 2600;
        QCOMPARE(pbFields(find(*trimmed(src, trim), 0x0101)->data).at(3).raw, varint(3));
    }

    void stillsAreEmptiedAndOtherRecordsAreKept()
    {
        const Parsed src = makeTrailer();
        Trim trim;
        trim.startMs = 3000;
        trim.durationMs = 2000;
        trim.mp4Length = 1;
        const auto out = trimmed(src, trim);
        QVERIFY(out.has_value());
        QVERIFY(find(*out, 0x0200)->data.isEmpty()); // pictures of the original footage
        QVERIFY(find(*out, 0x0500)->data.isEmpty());
        QCOMPARE(find(*out, 0x0900)->data, find(src, 0x0900)->data); // not understood: untouched
        QCOMPARE(find(*out, 0x0a00)->data, find(src, 0x0a00)->data);
        // Same records in the same order, same version.
        QCOMPARE(out->version, src.version);
        QCOMPARE(out->records.size(), src.records.size());
        for (qsizetype i = 0; i < src.records.size(); ++i)
            QCOMPARE(out->records.at(i).id, src.records.at(i).id);
        // And the cut trailer is still a valid trailer.
        QTemporaryDir dir;
        const QString path = writeFile(dir, "cut.insv", QByteArray(777, 'M') + build(*out));
        QVERIFY(read(path).has_value());
        QVERIFY(detect(path).has_value());
    }

    void clipBeyondTheDataIsRefused()
    {
        Trim trim;
        trim.startMs = 60000; // the motion data ends at clock 22835, ~20 s in
        trim.durationMs = 2000;
        trim.mp4Length = 1;
        QString err;
        QVERIFY(!trimmed(makeTrailer(), trim, &err).has_value());
        QVERIFY(err.contains("motion"));
    }

    void unfamiliarLayoutsAreRefusedNotGuessed()
    {
        Trim trim;
        trim.startMs = 3000;
        trim.durationMs = 2000;
        trim.mp4Length = 1;
        QString err;

        QVERIFY(!trimmed(makeTrailer(2835, 22835, /*version*/ 4), trim, &err).has_value()); // unknown version
        QVERIFY(err.contains("version"));

        Parsed noMotion = makeTrailer();
        noMotion.records.removeIf([](const Record &r) { return r.id == 0x0300; });
        QVERIFY(!trimmed(noMotion, trim, &err).has_value());

        Parsed noExposure = makeTrailer();
        noExposure.records.removeIf([](const Record &r) { return r.id == 0x0400; });
        QVERIFY(!trimmed(noExposure, trim, &err).has_value());

        Parsed oddMotion = makeTrailer();
        for (Record &r : oddMotion.records) {
            if (r.id == 0x0300)
                r.data.chop(5); // not a whole number of samples
        }
        QVERIFY(!trimmed(oddMotion, trim, &err).has_value());

        Parsed unordered = makeTrailer();
        for (Record &r : unordered.records) {
            if (r.id == 0x0300)
                std::swap_ranges(r.data.begin(), r.data.begin() + 56, r.data.begin() + 56 * 10); // out of time order
        }
        QVERIFY(!trimmed(unordered, trim, &err).has_value());
        QVERIFY(err.contains("order"));

        Parsed noFields = makeTrailer(); // the info record without the size/duration fields
        for (Record &r : noFields.records) {
            if (r.id == 0x0101)
                r.data = pbBytes(1, "serial only");
        }
        QVERIFY(!trimmed(noFields, trim, &err).has_value());

        Trim empty = trim;
        empty.durationMs = 0;
        QVERIFY(!trimmed(makeTrailer(), empty, &err).has_value());
        empty = trim;
        empty.startMs = -5;
        QVERIFY(!trimmed(makeTrailer(), empty, &err).has_value());
    }

    void appendTrimmedWritesAValidFile()
    {
        QTemporaryDir dir;
        const QByteArray sourceMp4(5000, 'S');
        const QString source = writeFile(dir, "src.insv", sourceMp4 + build(makeTrailer()));
        const QByteArray exported(900, 'E'); // the exported file's MP4 part
        const QString dest = writeFile(dir, "out.insv", exported);

        Trim trim;
        trim.startMs = 3000;
        trim.durationMs = 2000;
        trim.mp4Length = exported.size();
        QString err;
        QVERIFY2(appendTrimmed(source, dest, trim, &err), qPrintable(err));

        QFile f(dest);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray all = f.readAll();
        QCOMPARE(all.left(900), exported); // the MP4 part is untouched
        const auto info = detect(dest);
        QVERIFY(info.has_value());
        QCOMPARE(info->offset, qint64(900));
        QVERIFY(info->size < build(makeTrailer()).size()); // smaller: only the clip's motion data, no stills
        const auto back = read(dest);
        QVERIFY(back.has_value());
        QCOMPARE(pbFields(find(*back, 0x0101)->data).at(2).raw, varint(900)); // field 9: where the trailer starts

        // Failures change nothing.
        const QString plain = writeFile(dir, "plain.mp4", QByteArray(100, 'P'));
        const QString out2 = writeFile(dir, "out2.insv", exported);
        QVERIFY(!appendTrimmed(plain, out2, trim, &err)); // the source has no trailer
        QCOMPARE(QFileInfo(out2).size(), qint64(900));
    }

    // ---- a real recording (optional): TRIMFAST_TEST_INSV = the "_00_" file ----
    void realRecordingRoundTripsAndCutsLikeTheVerifiedFiles()
    {
        const QString real = qEnvironmentVariable("TRIMFAST_TEST_INSV");
        if (real.isEmpty())
            QSKIP("TRIMFAST_TEST_INSV not set");
        QString err;
        const auto parsed = read(real, &err);
        QVERIFY2(parsed.has_value(), qPrintable(err));
        QCOMPARE(parsed->records.size(), qsizetype(7));

        // Rebuilding the real trailer reproduces it byte for byte.
        QFile f(real);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto info = detect(real);
        QVERIFY(info.has_value());
        QVERIFY(f.seek(info->offset));
        QCOMPARE(build(*parsed), f.read(info->size));

        // The cut that was checked in Insta360 Studio (stabilisation on): 1.28 s + 5.12 s, MP4 part of
        // 29,245,423 bytes. These counts and the size come from that file.
        Trim trim;
        trim.startMs = 1280;
        trim.durationMs = 5120;
        trim.mp4Length = 29245423;
        const auto cut = trimmed(*parsed, trim, &err);
        QVERIFY2(cut.has_value(), qPrintable(err));
        QCOMPARE(find(*cut, 0x0300)->data.size() / 56, qsizetype(2811));
        QCOMPARE(find(*cut, 0x0400)->data.size() / 16, qsizetype(135));
        QCOMPARE(build(*cut).size(), qsizetype(213746));
        QVERIFY(find(*cut, 0x0200)->data.isEmpty() && find(*cut, 0x0500)->data.isEmpty());
        QCOMPARE(find(*cut, 0x0900)->data, find(*parsed, 0x0900)->data);
    }
};

QTEST_APPLESS_MAIN(TstInsta360Trim)
#include "tst_insta360trim.moc"
