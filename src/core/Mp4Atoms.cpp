#include "core/Mp4Atoms.h"

#include <QFile>
#include <QObject>
#include <algorithm>
#include <cstring>

namespace Mp4Atoms {

namespace {

constexpr qint64 kMaxMoov = 64 * 1024 * 1024;  // a header larger than this is not expected
constexpr qint64 kMaxVendorAtom = 1024 * 1024; // nor are vendor atoms this large

quint32 be32(const unsigned char *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint64 be64(const unsigned char *p)
{
    return (quint64(be32(p)) << 32) | be32(p + 4);
}

void putBe32(QByteArray &b, qsizetype at, quint32 v)
{
    b[at] = char(v >> 24);
    b[at + 1] = char(v >> 16);
    b[at + 2] = char(v >> 8);
    b[at + 3] = char(v);
}

bool printableType(const char *t)
{
    for (int i = 0; i < 4; ++i) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (c < 0x20 || c > 0x7e)
            return false;
    }
    return true;
}

// The box starting at `pos`, within [pos, limit). Returns false if it is not a sane box.
bool readBox(QFile &f, qint64 pos, qint64 limit, Box &out)
{
    if (pos + 8 > limit || !f.seek(pos))
        return false;
    unsigned char h[16];
    if (f.read(reinterpret_cast<char *>(h), 8) != 8)
        return false;
    qint64 size = be32(h);
    int header = 8;
    if (!printableType(reinterpret_cast<const char *>(h + 4)))
        return false;
    if (size == 1) {
        if (pos + 16 > limit || f.read(reinterpret_cast<char *>(h + 8), 8) != 8)
            return false;
        const quint64 big = be64(h + 8);
        if (big > quint64(limit - pos))
            return false;
        size = static_cast<qint64>(big);
        header = 16;
    } else if (size == 0) {
        size = limit - pos; // runs to the end
    }
    if (size < header || pos + size > limit)
        return false;
    out.type = QByteArray(reinterpret_cast<const char *>(h + 4), 4);
    out.offset = pos;
    out.size = size;
    out.headerSize = header;
    return true;
}

QList<Box> children(QFile &f, const Box &parent, bool *covers = nullptr)
{
    QList<Box> list;
    qint64 pos = parent.offset + parent.headerSize;
    const qint64 end = parent.end();
    while (pos < end) {
        Box b;
        if (!readBox(f, pos, end, b))
            break;
        list.append(b);
        pos = b.end();
    }
    if (covers)
        *covers = (pos == end);
    return list;
}

// The top-level boxes of an open file; `covers` tells whether they end exactly at the end of the file.
QList<Box> readTop(QFile &f, bool *covers)
{
    QList<Box> list;
    qint64 pos = 0;
    const qint64 end = f.size();
    while (pos < end) {
        Box b;
        if (!readBox(f, pos, end, b))
            break;
        list.append(b);
        pos = b.end();
    }
    if (covers)
        *covers = !list.isEmpty() && pos == end;
    return list;
}

// moov and its udta (udta.size == 0 when there is none). False without moov.
bool findUdta(QFile &f, Box &moov, Box &udta)
{
    for (const Box &b : readTop(f, nullptr)) {
        if (b.type != "moov")
            continue;
        moov = b;
        udta = Box();
        for (const Box &c : children(f, moov)) {
            if (c.type == "udta") {
                udta = c;
                break;
            }
        }
        return true;
    }
    return false;
}

} // namespace

QList<Box> topLevel(const QString &path, bool *coversFile)
{
    if (coversFile)
        *coversFile = false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return readTop(f, coversFile);
}

const QList<QByteArray> &preservedVendorAtoms()
{
    static const QList<QByteArray> types = {QByteArrayLiteral("AMBA")};
    return types;
}

QList<QByteArray> udtaTypes(const QString &path)
{
    QList<QByteArray> types;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return types;
    Box moov, udta;
    if (!findUdta(f, moov, udta) || udta.size == 0)
        return types;
    for (const Box &c : children(f, udta))
        types.append(c.type);
    return types;
}

QList<QByteArray> vendorAtoms(const QString &path)
{
    QList<QByteArray> atoms;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return atoms;
    Box moov, udta;
    if (!findUdta(f, moov, udta) || udta.size == 0)
        return atoms;
    for (const Box &c : children(f, udta)) {
        if (!preservedVendorAtoms().contains(c.type) || c.size > kMaxVendorAtom || c.headerSize != 8)
            continue;
        if (!f.seek(c.offset))
            continue;
        const QByteArray bytes = f.read(c.size);
        if (bytes.size() == c.size)
            atoms.append(bytes);
    }
    return atoms;
}

bool addUdtaAtoms(const QString &path, const QList<QByteArray> &atoms, QString *error)
{
    auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (atoms.isEmpty())
        return true;

    QFile f(path);
    if (!f.open(QIODevice::ReadWrite))
        return fail(QObject::tr("cannot open %1 for writing").arg(path));

    // Layout check: moov must be the last box, so that growing it moves nothing else.
    bool covers = false;
    const QList<Box> top = readTop(f, &covers);
    if (top.isEmpty() || !covers)
        return fail(QObject::tr("the file is not a plain MP4"));
    const Box moov = top.last();
    if (moov.type != "moov")
        return fail(QObject::tr("the header (moov) is not at the end of the file"));
    if (moov.headerSize != 8 || moov.size > kMaxMoov)
        return fail(QObject::tr("unsupported header size"));

    if (!f.seek(moov.offset))
        return fail(QObject::tr("cannot read the header"));
    QByteArray moovBytes = f.read(moov.size);
    if (moovBytes.size() != moov.size)
        return fail(QObject::tr("cannot read the header"));

    // Locate udta inside the moov bytes (children are walked on the copy in memory).
    qint64 udtaAt = -1;
    quint32 udtaSize = 0;
    QList<QByteArray> have;
    for (qint64 p = 8; p + 8 <= moovBytes.size();) {
        const auto *h = reinterpret_cast<const unsigned char *>(moovBytes.constData() + p);
        const quint32 size = be32(h);
        if (size < 8 || p + size > moovBytes.size())
            return fail(QObject::tr("damaged header"));
        if (std::memcmp(h + 4, "udta", 4) == 0) {
            udtaAt = p;
            udtaSize = size;
            break;
        }
        p += size;
    }
    if (udtaAt >= 0) {
        for (qint64 p = udtaAt + 8; p + 8 <= udtaAt + qint64(udtaSize);) {
            const auto *h = reinterpret_cast<const unsigned char *>(moovBytes.constData() + p);
            const quint32 size = be32(h);
            if (size < 8 || p + size > udtaAt + qint64(udtaSize))
                break; // an odd atom: leave the rest alone, we only append
            have.append(QByteArray(reinterpret_cast<const char *>(h + 4), 4));
            p += size;
        }
    }

    QByteArray add;
    for (const QByteArray &a : atoms) {
        if (a.size() < 8 || qint64(be32(reinterpret_cast<const unsigned char *>(a.constData()))) != a.size())
            return fail(QObject::tr("an atom to add is malformed"));
        const QByteArray type = a.mid(4, 4);
        if (have.contains(type))
            continue; // already there
        add += a;
        have.append(type);
    }
    if (add.isEmpty())
        return true;

    if (udtaAt >= 0) {
        // Grow the existing udta (it may be followed by other boxes inside moov).
        moovBytes.insert(udtaAt + udtaSize, add);
        putBe32(moovBytes, udtaAt, udtaSize + quint32(add.size()));
    } else {
        QByteArray box(8, 0);
        putBe32(box, 0, quint32(8 + add.size()));
        std::memcpy(box.data() + 4, "udta", 4);
        moovBytes += box + add;
    }
    if (moovBytes.size() > 0x7fffffff)
        return fail(QObject::tr("header too large"));
    putBe32(moovBytes, 0, quint32(moovBytes.size()));

    if (!f.seek(moov.offset) || f.write(moovBytes) != moovBytes.size() || !f.flush())
        return fail(QObject::tr("write failed: %1").arg(f.errorString()));
    return true;
}

} // namespace Mp4Atoms
