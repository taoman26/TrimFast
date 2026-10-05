#pragma once

#include "core/ExportCoordinator.h"
#include "core/KeyframeIndex.h"
#include "core/MarkerManager.h"
#include "core/MediaInfo.h"

#include <QMainWindow>
#include <QMessageBox>
#include <QString>
#include <functional>
#include <memory>

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class SettingsManager;
class QTimer;
class SessionManager;
class KeyframeLoader;
class MediaProbe;
class TimelineWidget;
class VideoPlayer;
class VideoSurface;

// Main window. Opens a file (ffprobe), previews it, offers playback / seek /
// frame-step / keyframe navigation with a clickable timeline, IN/OUT editing and
// lossless export (FFmpegRunner). IN/OUT, timeline data and export come later;
// their shortcuts still only report that they fired.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // `configDir`: where session.ini and settings.ini live (default: the user's config dir).
    explicit MainWindow(QWidget *parent = nullptr, const QString &configDir = QString());
    ~MainWindow() override;

    // Probes and loads `path` (also used for command-line and drag & drop).
    void openPath(const QString &path);

    // Replaces the "Export As" file dialog: gets the suggested output path, returns the
    // chosen one (empty = cancelled). For tests.
    void setOutputChooser(std::function<QString(const QString &suggested)> chooser) { m_outputChooser = std::move(chooser); }
    // Replaces the Yes/No question boxes (title, text) -> true for "Yes". For tests.
    void setQuestionHandler(std::function<bool(const QString &title, const QString &text)> handler)
    {
        m_questionHandler = std::move(handler);
    }

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void openFile();
    void showShortcuts();
    void showAbout();

private:
    void createActions();
    void createMenus();
    void createCentral();
    void onProbed(const MediaInfo &info);
    void onProbeFailed(const QString &reason);
    void onPlayingChanged(bool playing);
    void onPositionChanged(qint64 ms);
    void showError(const QString &message);
    void gotoPreviousKeyframe();
    void gotoNextKeyframe();
    void refreshStatus();
    void flashStatus(const QString &text, int ms);
    void onMarkersChanged();
    void handleSetIn(qint64 ms);
    void handleSetOut(qint64 ms);
    void reportMarkerResult(const MarkerManager::Result &r, bool isIn);
    void setMediaControlsEnabled(bool enabled);
    void saveSessionNow();
    void updateOpenLastAction();
    void startExport();
    void cancelExport();
    void setExporting(bool exporting);
    void onExportProgress(int percent, int fileIndex, int fileCount);
    void onExportFinished(const ExportCoordinator::Result &result);
    void onPairProbed(const MediaInfo &info);
    void resetPair();
    QString askOutputPath(const QString &suggested);
    bool askYesNo(const QString &title, const QString &text);
    void updateOutputPreview();
    void showNonModal(QMessageBox::Icon icon, const QString &title, const QString &text);

    // Creates an action that only reports its firing (placeholder behaviour).
    QAction *makePlaceholderAction(const QString &text, const QKeySequence &key, const QString &id);
    // Creates an action wired to `handler` (no menu entry unless added by the caller).
    QAction *makeAction(const QString &text, const QKeySequence &key, std::function<void()> handler);
    void addShortcutOnly(const QKeySequence &key, const QString &id);
    void reportAction(const QString &id);

    VideoPlayer *m_player = nullptr;
    MediaProbe *m_probe = nullptr;
    KeyframeLoader *m_keyframeLoader = nullptr;
    MarkerManager *m_markers = nullptr;
    std::unique_ptr<SessionManager> m_session;
    std::unique_ptr<SettingsManager> m_settings;
    ExportCoordinator *m_coordinator = nullptr;
    // Insta360: the other lens' file, when found and consistent with the opened one.
    MediaProbe *m_pairProbe = nullptr;
    KeyframeLoader *m_pairKeyframeLoader = nullptr;
    MediaInfo m_pairInfo;
    KeyframeIndex m_pairKeyframes;
    QString m_pairStatus;
    QStringList m_exportExtraWarnings; // problems found before the export (shown with its result)
    std::function<QString(const QString &suggested)> m_outputChooser;
    std::function<bool(const QString &title, const QString &text)> m_questionHandler;
    QProgressBar *m_progress = nullptr;
    bool m_exporting = false;
    bool m_perfFirstFrame = false;   // PerfLog bookkeeping
    bool m_perfKeyframes = false;
    void perfMaybeQuit();
    QTimer *m_flashTimer = nullptr;    // ends a transient status message
    QTimer *m_saveTimer = nullptr;     // debounces session writes while dragging
    bool m_sessionActive = false;      // true once the range of the open file is settled
    MediaInfo m_info;
    KeyframeIndex m_keyframes;
    QString m_baseStatus;     // path | stream summary | size
    QString m_keyframeStatus; // keyframe scan result
    VideoSurface *m_video = nullptr;
    TimelineWidget *m_timeline = nullptr;
    QLabel *m_positionLabel = nullptr;
    QLineEdit *m_inEdit = nullptr;
    QLineEdit *m_outEdit = nullptr;
    QLabel *m_durationLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QPushButton *m_playButton = nullptr;
    QPushButton *m_setInButton = nullptr;
    QPushButton *m_setOutButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QLabel *m_statusMain = nullptr;
    QLabel *m_statusOut = nullptr;

    QAction *m_openAction = nullptr;
    QAction *m_exportAction = nullptr;
    QAction *m_openLastAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_setInAction = nullptr;
    QAction *m_setOutAction = nullptr;
    QAction *m_clearAction = nullptr;
    QAction *m_shortcutsAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QAction *m_playAction = nullptr;
};
