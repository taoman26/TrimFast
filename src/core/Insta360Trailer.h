#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>
#include <optional>

// Insta360 (.insv / .lrv) files carry a proprietary trailer after the MP4 data
// (lens calibration, gyro, ...). FFmpeg's stream copy drops it, so we carry it over
// ourselves. Layout (measured on real files, little endian):
//
//     [ ...trailer records... ][ size u32 ][ version u32 ][ 32-byte magic ]
//
// where `size` is the length of the whole trailer, footer included. The trailer is
// treated as opaque bytes: its records describe the original timeline.
namespace Insta360Trailer {

struct Info
{
    qint64 offset = 0;  // where the trailer starts in the file
    qint64 size = 0;    // trailer length in bytes (to the end of the file)
    quint32 version = 0;
};

constexpr int kFooterSize = 4 + 4 + 32;
// The 32 ASCII characters closing every Insta360 file.
inline constexpr char kMagic[] = "8db42d694ccc418790edff439fe026bf";

// The trailer of `path`, or nullopt when the file has none (or it is inconsistent).
std::optional<Info> detect(const QString &path);

// Appends the trailer bytes of `sourcePath` to the end of `destPath`, unchanged.
bool append(const QString &sourcePath, const Info &info, const QString &destPath, QString *error = nullptr);

// ---- The records inside the trailer (measured on Insta360 ONE X2 files) -----------------------
//
// The trailer is a list of records, each stored as   [data][id u16][length u32]   (little endian),
// followed by 32 zero bytes and the footer (size, version, magic) described above. The last
// record's 6-byte header therefore sits at the start of the 78-byte tail of the file.
//
//   0x0101  protobuf: serial, model, lens calibration, ... field 9 = size of the MP4 part of the file
//           (= where the trailer starts), field 10 = duration in whole seconds
//   0x0200  a full-resolution H.264 still of lens 0   (thumbnail; shows the footage!)
//   0x0300  motion data: 56-byte samples {u64 time ms, 6 x double} at 500 Hz (gyro/accelerometer)
//   0x0400  exposure changes: 16-byte entries {u64 time ms, double exposure}
//   0x0500  a full-resolution H.264 still of lens 1
//   0x0900, 0x0a00  not understood; kept unchanged
//
// All times are on the camera's clock, in which the first video frame is the time of the first
// exposure entry (2867 ms in the sample files; the motion data starts a little earlier).
struct Record
{
    quint16 id = 0;
    QByteArray data;
};

struct Parsed
{
    QList<Record> records; // in file order
    quint32 version = 0;
};

// Reads and validates the whole structure (the records must add up to exactly the trailer size).
std::optional<Parsed> read(const QString &path, QString *error = nullptr);

// The trailer bytes (records, zero padding, footer) for `parsed`.
QByteArray build(const Parsed &parsed);

// What the exported clip is, relative to the file the trailer comes from.
struct Trim
{
    qint64 startMs = 0;        // IN, in the source video's time
    qint64 durationMs = 0;     // length of the exported part
    qint64 videoStartMs = 0;   // presentation time of the first video frame in the exported file (usually 0)
    qint64 mp4Length = 0;      // size of the exported file's MP4 part = where the trailer will start
};

// A trailer that matches the exported clip, which is what Insta360 Studio needs to stabilise it
// (checked on a real recording: with the original, untrimmed motion data the stabilisation was off;
// with the data cut to the clip it worked):
//   * motion data and exposure entries are cut to the clip (with a little margin) and moved so that
//     the clip's first frame is where the source's first frame was;
//   * the size and duration in 0x0101 describe the new file;
//   * the two stills are emptied: they show the original footage, including parts that were cut away;
//   * everything else is kept byte for byte.
// Returns nullopt, with the reason in `error`, if the trailer does not look like the layout above
// (another camera model, damage ...): in that case copy it unchanged rather than guess.
std::optional<Parsed> trimmed(const Parsed &source, const Trim &trim, QString *error = nullptr);

// Reads the trailer of `sourcePath`, trims it for `trim` and appends it to `destPath`.
bool appendTrimmed(const QString &sourcePath, const QString &destPath, const Trim &trim, QString *error = nullptr);

// Margins around the clip that are kept (ms), for the motion data and exposure entries.
constexpr qint64 kMarginBeforeMs = 200;
constexpr qint64 kMarginAfterMs = 300;

} // namespace Insta360Trailer
