#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Compares what ffprobe reports for an input and the file exported from it, to catch
// anything the stream copy lost (streams, chapters, tags, side data such as spherical
// metadata). Differences are warnings: the export itself succeeded.
namespace OutputVerifier {

struct Report
{
    bool readable = true;       // false: ffprobe could not open the output at all
    QString error;              // reason when !readable
    QStringList warnings;       // differences, one line each
    bool isClean() const { return readable && warnings.isEmpty(); }
};

// Pure comparison of two `ffprobe -show_format -show_streams -show_chapters -of json` outputs.
QStringList compare(const QByteArray &inputJson, const QByteArray &outputJson);

// Runs ffprobe (blocking, metadata only) on both files and compares.
Report verify(const QString &inputPath, const QString &outputPath);

} // namespace OutputVerifier
