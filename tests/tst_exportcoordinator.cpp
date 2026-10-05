// Integration test (really runs ffmpeg). Needs TRIMFAST_TEST_VIDEO (H.264/AAC, >= 5 s,
// keyframe every 1 s: scripts/make_test_media.sh). The optional test with real Insta360
// files needs TRIMFAST_TEST_INSV (the "_00_" file of a pair). Skipped otherwise.
#include "core/ExportCoordinator.h"
#include "core/Insta360Trailer.h"
#include "insta360_testdata.h"
#include "core/InsvPairResolver.h"
#include "core/Mp4Atoms.h"
#include "core/OutputVerifier.h"
#include "core/ToolLocator.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// Presentation time of the first video frame (ms): B-frame codecs show it a little after 0.
qint64 videoStartMsOf(const QString &path)
{
    QProcess p;
    p.start(ToolLocator::find("ffprobe"),
            {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=start_time", "-of", "csv=p=0", path});
    p.waitForFinished(30000);
    return qRound64(p.readAllStandardOutput().trimmed().toDouble() * 1000.0);
}

double durationOf(const QString &path)
{
    QProcess p;
    p.start(ToolLocator::find("ffprobe"),
            {"-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path});
    p.waitForFinished(30000);
    return p.readAllStandardOutput().trimmed().toDouble();
}

ExportCoordinator::Item item(const QString &in, const QString &out, qint64 startMs, qint64 durMs,
                             bool overwrite = false, const QString &format = QString())
{
    ExportCoordinator::Item it;
    it.job.inputPath = in;
    it.job.outputPath = out;
    it.job.startMs = startMs;
    it.job.durationMs = durMs;
    it.job.overwrite = overwrite;
    it.job.format = format;
    return it;
}

// A stand-in for the 107-byte `AMBA` atom of Insta360 files.
QByteArray amba()
{
    const QByteArray payload = QByteArray("\x00\x00\x00\x00kAMBAxV4", 12) + QByteArray(87, '\x5a');
    QByteArray size(4, 0);
    const quint32 total = quint32(8 + payload.size());
    for (int i = 0; i < 4; ++i)
        size[3 - i] = char((total >> (8 * i)) & 0xff);
    return size + QByteArray("AMBA", 4) + payload;
}

QByteArray syntheticTrailer(const QByteArray &payload)
{
    auto u32 = [](quint32 v) {
        QByteArray b(4, 0);
        for (int i = 0; i < 4; ++i)
            b[i] = char((v >> (8 * i)) & 0xff);
        return b;
    };
    return payload + u32(quint32(payload.size() + Insta360Trailer::kFooterSize)) + u32(3) +
           QByteArray(Insta360Trailer::kMagic, 32);
}

QStringList leftovers(const QString &dir)
{
    QStringList names = QDir(dir).entryList(QDir::Files | QDir::Hidden);
    names.erase(std::remove_if(names.begin(), names.end(),
                               [](const QString &n) { return !n.contains("trimfast-partial"); }),
                names.end());
    return names;
}

} // namespace

class TstExportCoordinator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_video = qEnvironmentVariable("TRIMFAST_TEST_VIDEO");
        if (m_video.isEmpty())
            QSKIP("TRIMFAST_TEST_VIDEO not set");
        if (ToolLocator::find("ffmpeg").isEmpty() || ToolLocator::find("ffprobe").isEmpty())
            QSKIP("ffmpeg/ffprobe not found");
    }

    ExportCoordinator::Result run(const QList<ExportCoordinator::Item> &items, QList<int> *percents = nullptr,
                                  QList<int> *indexes = nullptr)
    {
        ExportCoordinator c;
        QSignalSpy done(&c, &ExportCoordinator::finished);
        QSignalSpy prog(&c, &ExportCoordinator::progress);
        c.start(items);
        if (!done.wait(60000))
            return {};
        for (const auto &args : prog) {
            if (percents)
                percents->append(args.at(0).toInt());
            if (indexes)
                indexes->append(args.at(1).toInt());
        }
        return done.first().at(0).value<ExportCoordinator::Result>();
    }

    void singleFile()
    {
        QTemporaryDir dir;
        const QString out = dir.filePath("out.mp4");
        const auto r = run({item(m_video, out, 1000, 2000)});
        QVERIFY2(r.status == ExportCoordinator::Status::Success, qPrintable(r.message));
        QCOMPARE(r.outputs, QStringList{out});
        QVERIFY(QFileInfo(out).size() > 1000);
        QVERIFY(leftovers(dir.path()).isEmpty());
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; "))); // verification is clean
        QVERIFY(r.notes.isEmpty());                                        // no Insta360 trailer here
        QVERIFY(qAbs(durationOf(out) - 2.0) < 0.4);
    }

    void twoFilesSucceedTogether()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("a.mp4");
        const QString b = dir.filePath("b.mp4");
        QList<int> percents, indexes;
        const auto r = run({item(m_video, a, 1000, 2000), item(m_video, b, 1000, 2000)}, &percents, &indexes);
        QVERIFY2(r.status == ExportCoordinator::Status::Success, qPrintable(r.message));
        QCOMPARE(r.outputs, (QStringList{a, b}));
        QVERIFY(QFileInfo::exists(a) && QFileInfo::exists(b));
        QVERIFY(leftovers(dir.path()).isEmpty());

        // Overall progress over both files: never goes backwards, ends at 100, both files seen.
        QVERIFY(!percents.isEmpty());
        QVERIFY(std::is_sorted(percents.begin(), percents.end()));
        QCOMPARE(percents.last(), 100);
        QVERIFY(indexes.contains(0) && indexes.contains(1));
    }

    void failureOfTheSecondRemovesTheFirst()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("a.mp4");
        const QString b = dir.filePath("b.mp4");
        const auto r = run({item(m_video, a, 1000, 2000), item(dir.filePath("missing.mp4"), b, 0, 1000)});
        QCOMPARE(r.status, ExportCoordinator::Status::Failed);
        QVERIFY(!r.message.isEmpty());
        QVERIFY(!QFileInfo::exists(a)); // all or nothing
        QVERIFY(!QFileInfo::exists(b));
        QVERIFY(leftovers(dir.path()).isEmpty());
    }

    void existingFilesAreProtected()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("a.mp4");
        const QString b = dir.filePath("b.mp4");
        writeFile(a, "old A");

        // Without overwrite consent nothing starts at all.
        auto r = run({item(m_video, a, 1000, 2000), item(m_video, b, 1000, 2000)});
        QCOMPARE(r.status, ExportCoordinator::Status::Failed);
        QVERIFY(r.message.contains("already exists"));
        QCOMPARE(readAll(a), QByteArray("old A"));
        QVERIFY(!QFileInfo::exists(b));

        // With consent, but the second file fails: the old file is still intact
        // (files are only swapped in after everything succeeded).
        r = run({item(m_video, a, 1000, 2000, true), item(dir.filePath("missing.mp4"), b, 0, 1000, true)});
        QCOMPARE(r.status, ExportCoordinator::Status::Failed);
        QCOMPARE(readAll(a), QByteArray("old A"));
        QVERIFY(leftovers(dir.path()).isEmpty());

        // With consent and success the old file is replaced.
        r = run({item(m_video, a, 1000, 2000, true)});
        QVERIFY2(r.status == ExportCoordinator::Status::Success, qPrintable(r.message));
        QVERIFY(QFileInfo(a).size() > 1000);
        QVERIFY(readAll(a) != QByteArray("old A"));
    }

    void sameOutputTwiceIsRejected()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("a.mp4");
        const auto r = run({item(m_video, a, 0, 1000), item(m_video, a, 0, 1000)});
        QCOMPARE(r.status, ExportCoordinator::Status::Failed);
        QVERIFY(!QFileInfo::exists(a));
    }

    void cancelLeavesNothing()
    {
        QTemporaryDir dir;
        ExportCoordinator c;
        QSignalSpy done(&c, &ExportCoordinator::finished);
        c.start({item(m_video, dir.filePath("a.mp4"), 0, 3000), item(m_video, dir.filePath("b.mp4"), 0, 3000)});
        QVERIFY(c.isRunning());
        c.cancel();
        QVERIFY(done.wait(10000));
        QCOMPARE(done.first().at(0).value<ExportCoordinator::Result>().status, ExportCoordinator::Status::Cancelled);
        QVERIFY(!c.isRunning());
        QVERIFY(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    // A clip with the Insta360 trailer: it comes back adapted to the exported part (that is what makes
    // Insta360 Studio stabilise the clip), the stills are gone, and the MP4 header keeps its AMBA atom.
    void insta360TrailerIsAdaptedToTheClip()
    {
        using namespace Insta360TestData;
        QTemporaryDir dir;
        // An "Insta360-like" input: a real MP4 with the camera's AMBA atom, followed by a trailer whose
        // motion data covers the whole 6 s (camera clock: the first video frame is 2867 ms).
        const QString in = dir.filePath("VID_1_00_1.insv");
        writeFile(in, readAll(m_video));
        QString atomError;
        QVERIFY2(Mp4Atoms::addUdtaAtoms(in, {amba()}, &atomError), qPrintable(atomError));
        const Insta360Trailer::Parsed source = makeTrailer(2835, 2867 + 6500);
        const QByteArray sourceTrailer = Insta360Trailer::build(source);
        writeFile(in, readAll(in) + sourceTrailer);
        QVERIFY(Insta360Trailer::detect(in).has_value());
        QCOMPARE(Mp4Atoms::vendorAtoms(in), QList<QByteArray>{amba()});

        const QString out = dir.filePath("VID_1_00_1_out.insv");
        const auto r = run({item(in, out, 1000, 2000, false, "mp4")});
        QVERIFY2(r.status == ExportCoordinator::Status::Success, qPrintable(r.message));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; ")));
        QVERIFY(!r.notes.isEmpty()); // tells the user what happened to the camera data

        // The MP4 part: header at the end, AMBA back, then the new trailer.
        const auto detected = Insta360Trailer::detect(out);
        QVERIFY(detected.has_value());
        const auto parts = Mp4Atoms::topLevel(out);
        QVERIFY(!parts.isEmpty());
        QCOMPARE(parts.last().type, QByteArray("moov"));
        QCOMPARE(parts.last().end(), detected->offset);
        QCOMPARE(Mp4Atoms::vendorAtoms(out), QList<QByteArray>{amba()});

        // The trailer describes the exported part: motion data cut to it, size field, no stills.
        const auto back = Insta360Trailer::read(out);
        QVERIFY(back.has_value());
        const auto times = motionTimes(*find(*back, 0x0300));
        // The clip's first frame is shown at `videoStart` in the exported file (0, or ~0.094 s with B-frames).
        const qint64 videoStart = videoStartMsOf(out);
        QVERIFY(videoStart >= 0 && videoStart < 500);
        QCOMPARE(times.first(), qint64(2867 + 1000 - 200 - 1000 + videoStart)); // 200 ms before the clip
        QCOMPARE(times.last(), qint64(2867 + 1000 + 2000 + 300 - 1000 + videoStart));
        QCOMPARE(exposureEntries(*find(*back, 0x0400)).first().first, qint64(2867 + videoStart)); // the clip's first frame
        QCOMPARE(pbFields(find(*back, 0x0101)->data).at(2).raw, varint(detected->offset)); // where the trailer starts
        QVERIFY(find(*back, 0x0200)->data.isEmpty() && find(*back, 0x0500)->data.isEmpty());
        QVERIFY(detected->size < sourceTrailer.size());
        QCOMPARE(find(*back, 0x0900)->data, find(source, 0x0900)->data); // not understood: kept

        QVERIFY(OutputVerifier::verify(in, out).readable); // still a normal file
        QVERIFY(qAbs(durationOf(out) - 2.0) < 0.4);
    }

    // A trailer in a layout we do not know (another camera?) is not guessed at: it is copied unchanged
    // and the user is told that stabilisation may not work.
    void unfamiliarInsta360TrailerIsCopiedUnchangedWithAWarning()
    {
        using namespace Insta360TestData;
        QTemporaryDir dir;
        const QByteArray trailer = syntheticTrailer(QByteArray("lens-calibration;gyro;") + QByteArray(5000, 'g'));
        const QString in = dir.filePath("VID_1_00_1.insv");
        writeFile(in, readAll(m_video) + trailer);
        QVERIFY(Insta360Trailer::detect(in).has_value());

        const QString out = dir.filePath("VID_1_00_1_out.insv");
        const auto r = run({item(in, out, 1000, 2000, false, "mp4")});
        QVERIFY2(r.status == ExportCoordinator::Status::Success, qPrintable(r.message));
        QVERIFY(readAll(out).endsWith(trailer)); // carried over byte for byte, as before
        QCOMPARE(r.warnings.size(), qsizetype(1));
        QVERIFY(r.warnings.first().contains("could not be adapted"));
        QVERIFY(r.warnings.first().contains("stabilise"));
        QVERIFY(OutputVerifier::verify(in, out).readable);
    }

    // The vendor atom is restored for any MP4 that has it, trailer or not.
    void vendorAtomIsRestored()
    {
        QTemporaryDir dir;
        const QString in = dir.filePath("in.mp4");
        writeFile(in, readAll(m_video));
        QString err;
        QVERIFY2(Mp4Atoms::addUdtaAtoms(in, {amba()}, &err), qPrintable(err));
        QVERIFY(Mp4Atoms::udtaTypes(in).contains("AMBA"));

        // The premise: a plain ffmpeg stream copy drops the atom. (If FFmpeg ever keeps it, this test
        // would prove nothing, so it says so.)
        {
            FFmpegRunner plain;
            QSignalSpy plainDone(&plain, &FFmpegRunner::finished);
            FFmpegRunner::Job job;
            job.inputPath = in;
            job.outputPath = dir.filePath("ffmpeg_only.mp4");
            job.startMs = 1000;
            job.durationMs = 2000;
            plain.start(job);
            QVERIFY(plainDone.wait(30000));
            QVERIFY2(Mp4Atoms::vendorAtoms(job.outputPath).isEmpty(),
                     "ffmpeg now keeps the AMBA atom itself: the restore step may be unnecessary");
        }

        const QString out = dir.filePath("out.mp4");
        const auto r = run({item(in, out, 1000, 2000)});
        QVERIFY2(r.status == ExportCoordinator::Status::Success, qPrintable(r.message));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; ")));
        QCOMPARE(Mp4Atoms::vendorAtoms(out), QList<QByteArray>{amba()});

        // The picture is still exactly the input's (the patched header did not disturb the data).
        QVERIFY(OutputVerifier::verify(in, out).readable);
        QVERIFY(qAbs(durationOf(out) - 2.0) < 0.4);

        // An input without the atom gets none added.
        const QString plain = dir.filePath("plain_out.mp4");
        const auto r2 = run({item(m_video, plain, 1000, 2000)});
        QVERIFY2(r2.status == ExportCoordinator::Status::Success, qPrintable(r2.message));
        QVERIFY(Mp4Atoms::vendorAtoms(plain).isEmpty());
    }

    // Real Insta360 pair (two 2880x2880 lens files).
    void realInstaPair()
    {
        const QString lens0 = qEnvironmentVariable("TRIMFAST_TEST_INSV");
        if (lens0.isEmpty())
            QSKIP("TRIMFAST_TEST_INSV not set");
        const QString lens1 = InsvPairResolver::partnerPath(lens0);
        QVERIFY2(QFileInfo::exists(lens0) && QFileInfo::exists(lens1), "pair not found");

        QTemporaryDir dir;
        const QString out0 = dir.filePath("a_00_1_trim.insv");
        const QString out1 = dir.filePath("a_10_1_trim.insv");
        // 1.280 s is a keyframe of the sample (0.64 s GOP); 5.12 s long.
        const auto r = run({item(lens0, out0, 1280, 5120, false, "mp4"), item(lens1, out1, 1280, 5120, false, "mp4")});
        QVERIFY2(r.status == ExportCoordinator::Status::Success, qPrintable(r.message));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; ")));

        // Both halves stay in sync...
        QVERIFY(qAbs(durationOf(out0) - durationOf(out1)) < 0.1);
        QVERIFY(qAbs(durationOf(out0) - 5.12) < 0.4);
        // ... only lens 0 carries the trailer, adapted to the clip (what lets Insta360 Studio stabilise it).
        const auto src = Insta360Trailer::detect(lens0);
        QVERIFY(src.has_value());
        const auto dst = Insta360Trailer::detect(out0);
        QVERIFY(dst.has_value());
        QVERIFY2(dst->size < src->size / 10, "the trailer should only hold the clip's motion data now");
        const auto parsed = Insta360Trailer::read(out0);
        QVERIFY(parsed.has_value());
        const auto motionTimesOut = Insta360TestData::motionTimes(*Insta360TestData::find(*parsed, 0x0300));
        // 5.12 s of clip + 0.5 s margin at 500 Hz, starting before the clip's first frame
        QVERIFY(motionTimesOut.size() > 2700 && motionTimesOut.size() < 2900);
        QVERIFY(Insta360TestData::find(*parsed, 0x0200)->data.isEmpty());
        QVERIFY(Insta360TestData::find(*parsed, 0x0500)->data.isEmpty());
        QCOMPARE(Mp4Atoms::topLevel(out0).last().end(), dst->offset);
        QVERIFY(!Insta360Trailer::detect(out1).has_value());
        QVERIFY(leftovers(dir.path()).isEmpty());

        // The camera's header atom (`AMBA`) is back in each output, identical to its source's.
        for (const auto &pair : {std::make_pair(lens0, out0), std::make_pair(lens1, out1)}) {
            const auto source = Mp4Atoms::vendorAtoms(pair.first);
            QVERIFY2(!source.isEmpty(), qPrintable(pair.first + " has no AMBA atom"));
            QCOMPARE(Mp4Atoms::vendorAtoms(pair.second), source);
            QCOMPARE(Mp4Atoms::topLevel(pair.second).first().type, QByteArray("ftyp"));
        }
    }

    // Long recording: cutting 10 minutes out of a one-hour file is a copy, not an encode.
    // TRIMFAST_TEST_LONG = a long clip with a keyframe every 2 s (or any grid that divides 600 s).
    void longFileThroughput()
    {
        const QString longVideo = qEnvironmentVariable("TRIMFAST_TEST_LONG");
        if (longVideo.isEmpty())
            QSKIP("TRIMFAST_TEST_LONG not set");
        QTemporaryDir dir;
        const QString out = dir.filePath("long_trim.mp4");
        QElapsedTimer t;
        t.start();
        const auto r = run({item(longVideo, out, 600000, 600000)});
        const qint64 ms = t.elapsed();
        QVERIFY2(r.status == ExportCoordinator::Status::Success, qPrintable(r.message));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; ")));
        const double mib = QFileInfo(out).size() / 1048576.0;
        qInfo("exported 10 min: %.1f MiB in %lld ms (%.0f MiB/s)", mib, static_cast<long long>(ms),
              mib * 1000.0 / qMax<qint64>(1, ms));
        QVERIFY(qAbs(durationOf(out) - 600.0) < 3.0);
        QVERIFY2(ms < 60000, "a stream copy of 10 minutes should take seconds");
    }

private:
    QString m_video;
};

QTEST_MAIN(TstExportCoordinator)
#include "tst_exportcoordinator.moc"
