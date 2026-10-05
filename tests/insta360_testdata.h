// Test data in the layout of a real Insta360 ONE X2 trailer (shared by several tests).
#pragma once

#include "core/Insta360Trailer.h"

#include <QByteArray>
#include <cmath>
#include <cstring>

namespace Insta360TestData {

using namespace Insta360Trailer;

inline void le(QByteArray &b, quint64 v, int n)
{
    for (int i = 0; i < n; ++i)
        b.append(char((v >> (8 * i)) & 0xff));
}

inline quint64 readLe(const QByteArray &b, qsizetype at, int n = 8)
{
    quint64 v = 0;
    for (int i = n - 1; i >= 0; --i)
        v = (v << 8) | static_cast<unsigned char>(b[at + i]);
    return v;
}

inline QByteArray varint(quint64 v)
{
    QByteArray out;
    do {
        quint8 c = v & 0x7f;
        v >>= 7;
        out.append(char(v ? (c | 0x80) : c));
    } while (v);
    return out;
}

inline QByteArray pbVarint(int field, quint64 v) { return varint(quint64(field) << 3) + varint(v); }
inline QByteArray pbBytes(int field, const QByteArray &b) { return varint((quint64(field) << 3) | 2) + varint(b.size()) + b; }
inline QByteArray pbDouble(int field, double d)
{
    QByteArray out = varint((quint64(field) << 3) | 1);
    quint64 raw;
    std::memcpy(&raw, &d, 8);
    le(out, raw, 8);
    return out;
}

// Same layout as a real ONE X2 trailer, small: motion at 500 Hz from `motionFrom`, exposure entries.
inline Parsed makeTrailer(qint64 motionFrom = 2835, qint64 motionTo = 22835, quint32 version = 3)
{
    Parsed t;
    t.version = version;

    QByteArray motion;
    for (qint64 ts = motionFrom; ts <= motionTo; ts += 2) {
        le(motion, quint64(ts), 8);
        for (int k = 0; k < 6; ++k) { // recognisable payload: depends on time and axis
            const double v = std::sin(double(ts) * 0.001 + k);
            quint64 raw;
            std::memcpy(&raw, &v, 8);
            le(motion, raw, 8);
        }
    }
    QByteArray exposure;
    const std::pair<qint64, double> exposures[] = {{2867, 0.002}, {2900, 0.0018}, {4226, 0.0016},
                                                   {9000, 0.001}, {15000, 0.0005}};
    for (const auto &e : exposures) {
        le(exposure, quint64(e.first), 8);
        quint64 raw;
        std::memcpy(&raw, &e.second, 8);
        le(exposure, raw, 8);
    }
    const QByteArray info = pbBytes(1, "IXSE50FNWKU5H6") + pbBytes(2, "Insta360 ONE X2") + pbVarint(9, 964689920) +
                            pbVarint(10, 168) + pbBytes(14, QByteArray(56, 'c')) + pbVarint(20, 25) +
                            pbDouble(25, 36.9425) + pbBytes(53, "2_1478.400_1529.520_1532.060");

    t.records = {{0x0a00, QByteArray("\x01\0\0\0\0\x02\0\0\0\0", 10)},
                 {0x0900, QByteArray(500, 'n')},                       // not understood: must stay as is
                 {0x0500, QByteArray("\0\0\0\x01STILL-OF-LENS-ONE", 20)},
                 {0x0400, exposure},
                 {0x0300, motion},
                 {0x0200, QByteArray("\0\0\0\x01STILL-OF-LENS-ZERO", 21)},
                 {0x0101, info}};
    return t;
}

inline const Record *find(const Parsed &p, quint16 id)
{
    for (const Record &r : p.records) {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

inline QList<qint64> motionTimes(const Record &r)
{
    QList<qint64> t;
    for (qsizetype i = 0; i + 56 <= r.data.size(); i += 56)
        t.append(qint64(readLe(r.data, i)));
    return t;
}

inline QList<std::pair<qint64, double>> exposureEntries(const Record &r)
{
    QList<std::pair<qint64, double>> out;
    for (qsizetype i = 0; i + 16 <= r.data.size(); i += 16) {
        const quint64 raw = readLe(r.data, i + 8);
        double d;
        std::memcpy(&d, &raw, 8);
        out.append({qint64(readLe(r.data, i)), d});
    }
    return out;
}

// All top-level protobuf fields as (field, wire type, raw bytes of the value).
struct PbField { int field; int wire; QByteArray raw; };
inline QList<PbField> pbFields(const QByteArray &b)
{
    QList<PbField> out;
    qsizetype i = 0;
    auto rd = [&]() {
        quint64 v = 0;
        for (int s = 0; s < 70; s += 7) {
            const quint8 c = quint8(b[i++]);
            v |= quint64(c & 0x7f) << s;
            if (!(c & 0x80))
                break;
        }
        return v;
    };
    while (i < b.size()) {
        const quint64 key = rd();
        PbField f{int(key >> 3), int(key & 7), {}};
        const qsizetype s = i;
        if (f.wire == 0) rd();
        else if (f.wire == 1) i += 8;
        else if (f.wire == 5) i += 4;
        else { const quint64 len = rd(); i += qsizetype(len); }
        f.raw = b.mid(s, i - s);
        out.append(f);
    }
    return out;
}

} // namespace Insta360TestData
