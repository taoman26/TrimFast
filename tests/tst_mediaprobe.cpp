#include "core/MediaProbe.h"

#include <QtTest>

namespace {

const char *kTypical = R"({
  "streams": [
    {"index":0,"codec_type":"video","codec_name":"h264","width":1920,"height":1080,
     "avg_frame_rate":"30000/1001","r_frame_rate":"30000/1001"},
    {"index":1,"codec_type":"audio","codec_name":"aac","sample_rate":"48000","channels":2},
    {"index":2,"codec_type":"subtitle","codec_name":"mov_text"}
  ],
  "chapters": [{"id":1},{"id":2}],
  "format": {"format_name":"mov,mp4,m4a,3gp,3g2,mj2","duration":"43.080000","size":"85419126"}
})";

} // namespace

class TstMediaProbe : public QObject
{
    Q_OBJECT

private slots:
    void typical()
    {
        QString err;
        const auto info = MediaProbe::parse(kTypical, "/x/a.mp4", &err);
        QVERIFY2(info.has_value(), qPrintable(err));
        QCOMPARE(info->path, QString("/x/a.mp4"));
        QCOMPARE(info->durationMs, qint64(43080));
        QCOMPARE(info->sizeBytes, qint64(85419126));
        QCOMPARE(info->chapterCount, 2);
        QCOMPARE(info->streams.size(), 3);
        QVERIFY(info->videoStream());
        QCOMPARE(info->videoStream()->codec, QString("h264"));
        QCOMPARE(info->videoStream()->width, 1920);
        QVERIFY(qAbs(info->fps() - 29.97) < 0.01);
        QVERIFY(info->audioStream());
        QCOMPARE(info->audioStream()->sampleRate, 48000);
        QCOMPARE(info->audioStream()->channels, 2);
        QVERIFY(info->isValid());
    }

    void fpsFallsBackToRFrameRate()
    {
        const char *json = R"({"streams":[{"index":0,"codec_type":"video","codec_name":"h264",
            "width":640,"height":360,"avg_frame_rate":"0/0","r_frame_rate":"25/1"}],
            "format":{"duration":"1.0"}})";
        const auto info = MediaProbe::parse(json, "a");
        QVERIFY(info.has_value());
        QCOMPARE(info->fps(), 25.0);
    }

    void unknownDurationIsZero()
    {
        const char *json = R"({"streams":[{"index":0,"codec_type":"video","codec_name":"vp9",
            "width":1,"height":1}],"format":{}})";
        const auto info = MediaProbe::parse(json, "a");
        QVERIFY(info.has_value());
        QCOMPARE(info->durationMs, qint64(0));
        QCOMPARE(info->fps(), 0.0);
    }

    void audioOnlyIsRejected()
    {
        QString err;
        const char *json = R"({"streams":[{"index":0,"codec_type":"audio","codec_name":"aac"}],
            "format":{"duration":"1"}})";
        QVERIFY(!MediaProbe::parse(json, "a", &err).has_value());
        QVERIFY(!err.isEmpty());
    }

    void noStreamsIsRejected()
    {
        QString err;
        QVERIFY(!MediaProbe::parse(R"({"streams":[],"format":{}})", "a", &err).has_value());
        QVERIFY(!err.isEmpty());
    }

    void garbageIsRejected()
    {
        QString err;
        QVERIFY(!MediaProbe::parse("not json", "a", &err).has_value());
        QVERIFY(!err.isEmpty());
        QVERIFY(!MediaProbe::parse("", "a").has_value());
    }
};

QTEST_APPLESS_MAIN(TstMediaProbe)
#include "tst_mediaprobe.moc"
