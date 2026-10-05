#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

// Just enough of the MP4/QuickTime box ("atom") structure to carry vendor data over a stream
// copy. FFmpeg rewrites the file's header and drops vendor atoms it does not understand, for
// example the `AMBA` atom that Insta360 cameras (Ambarella chip) put into moov/udta.
namespace Mp4Atoms {

struct Box
{
    QByteArray type;       // four characters, e.g. "moov"
    qint64 offset = 0;     // position of the box in the file
    qint64 size = 0;       // whole box, header included
    int headerSize = 8;    // 8, or 16 with a 64-bit size
    qint64 end() const { return offset + size; }
};

// The top-level boxes of a file. `coversFile` is true when they end exactly at the end of the
// file (nothing unparsable follows). Empty on a file that is not an MP4.
QList<Box> topLevel(const QString &path, bool *coversFile = nullptr);

// Vendor atoms we carry over, by type. (Allow-list: other vendors' atoms may describe the
// original timeline, e.g. GPS tracks, and must not be copied blindly.)
const QList<QByteArray> &preservedVendorAtoms();

// Complete boxes (header included) found in moov/udta of `path` whose type is in
// preservedVendorAtoms(). Reads only the few bytes it needs, so huge files are fine.
QList<QByteArray> vendorAtoms(const QString &path);

// All box types inside moov/udta (for inspection and tests).
QList<QByteArray> udtaTypes(const QString &path);

// Appends `atoms` (complete boxes) to moov/udta of `path`, creating udta when it is missing;
// types already present are skipped. Only done when moov is the LAST box of the file (then the
// media data does not move and nothing else needs fixing); otherwise the file is left untouched
// and false is returned with the reason in `error`.
bool addUdtaAtoms(const QString &path, const QList<QByteArray> &atoms, QString *error = nullptr);

} // namespace Mp4Atoms
