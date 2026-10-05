#include "core/Insta360Trailer.h"

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Insta360Trailer {

namespace {

quint32 readU32LE(const char *p)
{
    const auto *u = reinterpret_cast<const unsigned char *>(p);
    return quint32(u[0]) | (quint32(u[1]) << 8) | (quint32(u[2]) << 16) | (quint32(u[3]) << 24);
}

} // namespace

std::optional<Info> detect(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const qint64 fileSize = f.size();
    if (fileSize < kFooterSize)
        return std::nullopt;

    if (!f.seek(fileSize - kFooterSize))
        return std::nullopt;
    const QByteArray footer = f.read(kFooterSize);
    if (footer.size() != kFooterSize)
        return std::nullopt;
    if (std::memcmp(footer.constData() + 8, kMagic, 32) != 0)
        return std::nullopt;

    Info info;
    info.size = readU32LE(footer.constData());
    info.version = readU32LE(footer.constData() + 4);
    // The trailer includes its own footer, and cannot be bigger than the file.
    if (info.size < kFooterSize || info.size > fileSize)
        return std::nullopt;
    info.offset = fileSize - info.size;
    return info;
}

bool append(const QString &sourcePath, const Info &info, const QString &destPath, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    QFile src(sourcePath);
    if (!src.open(QIODevice::ReadOnly))
        return fail(QObject::tr("Cannot read %1").arg(sourcePath));
    if (info.offset < 0 || info.size <= 0 || info.offset + info.size > src.size() || !src.seek(info.offset))
        return fail(QObject::tr("Invalid trailer range in %1").arg(sourcePath));

    QFile dst(destPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Append))
        return fail(QObject::tr("Cannot write %1").arg(destPath));

    constexpr qint64 kChunk = 1 << 20;
    qint64 remaining = info.size;
    while (remaining > 0) {
        const QByteArray chunk = src.read(qMin(kChunk, remaining));
        if (chunk.isEmpty())
            return fail(QObject::tr("Unexpected end of %1").arg(sourcePath));
        if (dst.write(chunk) != chunk.size())
            return fail(QObject::tr("Write failed: %1").arg(dst.errorString()));
        remaining -= chunk.size();
    }
    if (!dst.flush())
        return fail(QObject::tr("Write failed: %1").arg(dst.errorString()));
    return true;
}

// ---------------------------------------------------------------------------------------------

namespace {

constexpr int kRecordHeader = 6;           // id u16 + length u32
constexpr int kFooterPadding = 32;         // zero bytes between the last record header and the size
constexpr qint64 kMaxTrailer = 512LL * 1024 * 1024;

constexpr quint16 kInfo = 0x0101;
constexpr quint16 kStillLens0 = 0x0200;
constexpr quint16 kMotion = 0x0300;
constexpr quint16 kExposure = 0x0400;
constexpr quint16 kStillLens1 = 0x0500;
constexpr int kMotionSample = 56;
constexpr int kExposureEntry = 16;
constexpr quint32 kKnownVersion = 3;

quint64 le64(const char *p)
{
    quint64 v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | static_cast<unsigned char>(p[i]);
    return v;
}

void putLe64(QByteArray &b, qsizetype at, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        b[at + i] = char((v >> (8 * i)) & 0xff);
}

void appendLe(QByteArray &b, quint64 v, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        b.append(char((v >> (8 * i)) & 0xff));
}

// ---- protobuf: just enough to change two varint fields and keep everything else byte for byte ----
bool readVarint(const QByteArray &b, qsizetype &i, quint64 &out)
{
    out = 0;
    for (int shift = 0; shift < 70 && i < b.size(); shift += 7) {
        const quint8 c = static_cast<quint8>(b[i++]);
        out |= quint64(c & 0x7f) << shift;
        if (!(c & 0x80))
            return true;
    }
    return false;
}

QByteArray varint(quint64 v)
{
    QByteArray out;
    do {
        quint8 c = v & 0x7f;
        v >>= 7;
        out.append(char(v ? (c | 0x80) : c));
    } while (v);
    return out;
}

// Replaces the first occurrence of each top-level varint field in `values`. False if the message
// cannot be walked or a field is missing.
bool patchVarints(const QByteArray &in, const QList<std::pair<int, quint64>> &values, QByteArray &out)
{
    out.clear();
    QList<int> done;
    qsizetype i = 0;
    while (i < in.size()) {
        const qsizetype fieldStart = i;
        quint64 key = 0;
        if (!readVarint(in, i, key))
            return false;
        const int field = int(key >> 3);
        const int wire = int(key & 7);
        if (wire == 0) {
            quint64 v = 0;
            if (!readVarint(in, i, v))
                return false;
            for (const auto &want : values) {
                if (want.first == field && !done.contains(field)) {
                    out += varint(key) + varint(want.second);
                    done.append(field);
                    goto next;
                }
            }
        } else if (wire == 1) {
            i += 8;
        } else if (wire == 5) {
            i += 4;
        } else if (wire == 2) {
            quint64 len = 0;
            if (!readVarint(in, i, len) || len > quint64(in.size() - i))
                return false;
            i += qsizetype(len);
        } else {
            return false;
        }
        if (i > in.size())
            return false;
        out += in.mid(fieldStart, i - fieldStart);
    next:;
    }
    return done.size() == values.size();
}

} // namespace

std::optional<Parsed> read(const QString &path, QString *error)
{
    auto fail = [error](const QString &why) -> std::optional<Parsed> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    const auto info = detect(path);
    if (!info)
        return fail(QObject::tr("no Insta360 trailer"));
    if (info->size > kMaxTrailer)
        return fail(QObject::tr("the trailer is unexpectedly large"));

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(info->offset))
        return fail(QObject::tr("cannot read the trailer"));
    const QByteArray raw = f.read(info->size);
    if (raw.size() != info->size)
        return fail(QObject::tr("cannot read the trailer"));

    // Records, from the last one backwards: header at `hdr`, data right before it.
    Parsed parsed;
    parsed.version = info->version;
    const qint64 tail = kFooterPadding + 4 + 4 + 32; // after the last record header
    qint64 hdr = raw.size() - tail - kRecordHeader;
    QList<Record> backwards;
    while (hdr >= 0) {
        if (hdr + kRecordHeader > raw.size())
            return fail(QObject::tr("damaged trailer"));
        const auto *h = reinterpret_cast<const unsigned char *>(raw.constData() + hdr);
        const quint16 id = quint16(h[0] | (h[1] << 8));
        const quint32 len = quint32(h[2]) | (quint32(h[3]) << 8) | (quint32(h[4]) << 16) | (quint32(h[5]) << 24);
        if (qint64(len) > hdr)
            return fail(QObject::tr("damaged trailer (a record is longer than the trailer)"));
        Record r;
        r.id = id;
        r.data = raw.mid(hdr - len, len);
        backwards.append(r);
        hdr -= qint64(len) + kRecordHeader;
        if (hdr == -kRecordHeader)
            break; // exactly consumed
        if (hdr < 0)
            return fail(QObject::tr("damaged trailer (the records do not add up)"));
    }
    if (backwards.isEmpty())
        return fail(QObject::tr("the trailer has no records"));
    std::reverse(backwards.begin(), backwards.end());
    parsed.records = backwards;
    return parsed;
}

QByteArray build(const Parsed &parsed)
{
    QByteArray body;
    for (const Record &r : parsed.records) {
        body += r.data;
        appendLe(body, r.id, 2);
        appendLe(body, quint32(r.data.size()), 4);
    }
    const qint64 total = body.size() + kFooterPadding + 4 + 4 + 32;
    QByteArray out = body;
    out.append(QByteArray(kFooterPadding, '\0'));
    appendLe(out, quint32(total), 4);
    appendLe(out, parsed.version, 4);
    out.append(kMagic, 32);
    return out;
}

std::optional<Parsed> trimmed(const Parsed &source, const Trim &trim, QString *error)
{
    auto fail = [error](const QString &why) -> std::optional<Parsed> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (source.version != kKnownVersion)
        return fail(QObject::tr("trailer version %1 is not known").arg(source.version));
    if (trim.durationMs <= 0 || trim.startMs < 0)
        return fail(QObject::tr("invalid range"));

    auto find = [&](quint16 id) -> const Record * {
        for (const Record &r : source.records) {
            if (r.id == id)
                return &r;
        }
        return nullptr;
    };
    const Record *motion = find(kMotion);
    const Record *exposure = find(kExposure);
    const Record *info = find(kInfo);
    if (!motion || !exposure || !info)
        return fail(QObject::tr("the motion, exposure or info record is missing"));
    if (motion->data.size() % kMotionSample != 0 || exposure->data.size() % kExposureEntry != 0)
        return fail(QObject::tr("the motion or exposure record has an unexpected size"));
    const qsizetype motionCount = motion->data.size() / kMotionSample;
    const qsizetype exposureCount = exposure->data.size() / kExposureEntry;
    if (motionCount == 0 || exposureCount == 0)
        return fail(QObject::tr("the motion or exposure record is empty"));

    // The first video frame is the first exposure entry on the camera's clock.
    const qint64 videoStartClock = qint64(le64(exposure->data.constData()));
    const qint64 clockIn = videoStartClock + trim.startMs;
    const qint64 clockOut = clockIn + trim.durationMs;
    const qint64 shift = -trim.startMs + trim.videoStartMs; // new time = old time + shift

    // Motion samples inside the clip (plus margins). They must be in time order.
    QByteArray motionOut;
    qint64 previous = -1;
    for (qsizetype i = 0; i < motionCount; ++i) {
        const char *s = motion->data.constData() + i * kMotionSample;
        const qint64 t = qint64(le64(s));
        if (t < previous)
            return fail(QObject::tr("the motion data is not in time order"));
        previous = t;
        if (t >= clockIn - kMarginBeforeMs && t <= clockOut + kMarginAfterMs) {
            QByteArray sample(s, kMotionSample);
            putLe64(sample, 0, quint64(t + shift));
            motionOut += sample;
        }
    }
    if (motionOut.isEmpty())
        return fail(QObject::tr("the source has no motion data for this part of the video"));

    // Exposure entries: the one in effect at the start (moved to the start), then those inside.
    QByteArray exposureOut;
    const char *inEffect = nullptr;
    QByteArray inside;
    previous = -1;
    for (qsizetype i = 0; i < exposureCount; ++i) {
        const char *e = exposure->data.constData() + i * kExposureEntry;
        const qint64 t = qint64(le64(e));
        if (t < previous)
            return fail(QObject::tr("the exposure data is not in time order"));
        previous = t;
        if (t <= clockIn) {
            inEffect = e;
        } else if (t <= clockOut + kMarginAfterMs) {
            QByteArray entry(e, kExposureEntry);
            putLe64(entry, 0, quint64(t + shift));
            inside += entry;
        }
    }
    if (inEffect) {
        QByteArray entry(inEffect, kExposureEntry);
        putLe64(entry, 0, quint64(clockIn + shift));
        exposureOut += entry;
    }
    exposureOut += inside;

    QByteArray infoOut;
    const qint64 seconds = std::max<qint64>(1, std::llround(double(trim.durationMs) / 1000.0));
    if (!patchVarints(info->data, {{9, quint64(trim.mp4Length)}, {10, quint64(seconds)}}, infoOut))
        return fail(QObject::tr("the info record could not be updated"));

    Parsed out;
    out.version = source.version;
    for (const Record &r : source.records) {
        Record n = r;
        switch (r.id) {
        case kMotion:
            n.data = motionOut;
            break;
        case kExposure:
            n.data = exposureOut;
            break;
        case kInfo:
            n.data = infoOut;
            break;
        case kStillLens0:
        case kStillLens1:
            n.data.clear(); // pictures of the original footage, including what was cut away
            break;
        default:
            break; // unknown records: unchanged
        }
        out.records.append(n);
    }
    return out;
}

bool appendTrimmed(const QString &sourcePath, const QString &destPath, const Trim &trim, QString *error)
{
    auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    QString why;
    const auto parsed = read(sourcePath, &why);
    if (!parsed)
        return fail(why);
    const auto cut = trimmed(*parsed, trim, &why);
    if (!cut)
        return fail(why);
    const QByteArray bytes = build(*cut);
    QFile dst(destPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Append))
        return fail(QObject::tr("Cannot write %1").arg(destPath));
    if (dst.write(bytes) != bytes.size() || !dst.flush())
        return fail(QObject::tr("Write failed: %1").arg(dst.errorString()));
    return true;
}

} // namespace Insta360Trailer
