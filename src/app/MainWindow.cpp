#include "app/MainWindow.h"

#include "app/PerfLog.h"
#include "core/ExportCoordinator.h"
#include "core/Insta360Trailer.h"
#include "core/InsvPairResolver.h"
#include "core/KeyframeLoader.h"
#include "core/MediaProbe.h"
#include "core/OutputCheck.h"
#include "core/OutputPath.h"
#include "core/SessionManager.h"
#include "core/SettingsManager.h"
#include "core/ToolLocator.h"
#include "core/TimeFormat.h"
#include "playback/VideoPlayer.h"
#include "ui/TimelineWidget.h"
#include "ui/VideoSurface.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QVBoxLayout>

namespace {
QString resolveConfigDir(const QString &configDir)
{
    const QString dir = configDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                            : configDir;
    QDir().mkpath(dir);
    return dir;
}
} // namespace

MainWindow::MainWindow(QWidget *parent, const QString &configDir)
    : QMainWindow(parent)
{
    const QString cfg = resolveConfigDir(configDir);
    m_session = std::make_unique<SessionManager>(cfg + QStringLiteral("/session.ini"));
    m_settings = std::make_unique<SettingsManager>(cfg + QStringLiteral("/settings.ini"));
    ToolLocator::setOverride(QStringLiteral("ffmpeg"), m_settings->ffmpegPath());
    ToolLocator::setOverride(QStringLiteral("ffprobe"), m_settings->ffprobePath());

    setWindowTitle(QStringLiteral("TrimFast"));
    setMinimumSize(960, 600);
    // Preferred size, but never larger than the usable screen area.
    QSize initial(1280, 800);
    if (const QScreen *screen = QGuiApplication::primaryScreen())
        initial = initial.boundedTo(screen->availableGeometry().size() * 9 / 10);
    resize(initial.expandedTo(minimumSize()));

    setAcceptDrops(true);

    m_player = new VideoPlayer(this);
    m_probe = new MediaProbe(this);
    m_keyframeLoader = new KeyframeLoader(this);
    m_markers = new MarkerManager(this);
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, &MainWindow::saveSessionNow);
    m_coordinator = new ExportCoordinator(this);
    connect(m_coordinator, &ExportCoordinator::progress, this, &MainWindow::onExportProgress);
    connect(m_coordinator, &ExportCoordinator::finished, this, &MainWindow::onExportFinished);
    m_pairProbe = new MediaProbe(this);
    m_pairKeyframeLoader = new KeyframeLoader(this);
    connect(m_pairProbe, &MediaProbe::finished, this, &MainWindow::onPairProbed);
    connect(m_pairProbe, &MediaProbe::failed, this, [this](const QString &) {
        m_pairStatus = tr("Pair: unreadable (single file)");
        refreshStatus();
    });
    connect(m_pairKeyframeLoader, &KeyframeLoader::loaded, this,
            [this](const KeyframeIndex &index) { m_pairKeyframes = index; });
    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    connect(m_flashTimer, &QTimer::timeout, this, &MainWindow::refreshStatus);

    createActions();
    createMenus();
    createCentral();

    m_statusMain = new QLabel(tr("Ready"));
    m_statusOut = new QLabel;
    statusBar()->addWidget(m_statusMain, 1);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setFixedWidth(160);
    m_progress->setVisible(false);
    statusBar()->addPermanentWidget(m_progress);
    statusBar()->addPermanentWidget(m_statusOut);

    connect(m_probe, &MediaProbe::finished, this, &MainWindow::onProbed);
    connect(m_probe, &MediaProbe::failed, this, &MainWindow::onProbeFailed);
    connect(m_player, &VideoPlayer::playingChanged, this, &MainWindow::onPlayingChanged);
    connect(m_player, &VideoPlayer::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_player, &VideoPlayer::errorOccurred, this, &MainWindow::showError);
    connect(m_player, &VideoPlayer::loaded, this, [this](qint64 ms) {
        m_timeline->setDuration(ms); // the player's range is what can be sought
        m_markers->setDuration(ms);
    });
    connect(m_markers, &MarkerManager::markersChanged, this, &MainWindow::onMarkersChanged);
    connect(m_timeline, &TimelineWidget::inRequested, this, &MainWindow::handleSetIn);
    connect(m_timeline, &TimelineWidget::outRequested, this, &MainWindow::handleSetOut);
    connect(m_timeline, &TimelineWidget::seekRequested, m_player, &VideoPlayer::seek);
    connect(m_keyframeLoader, &KeyframeLoader::loaded, this, [this](const KeyframeIndex &index) {
        m_keyframes = index;
        m_timeline->setKeyframes(index);
        m_markers->setKeyframes(index); // re-snaps IN
        m_keyframeStatus = tr("Keyframes: %1").arg(index.size());
        refreshStatus();
        m_perfKeyframes = true;
        PerfLog::mark("keyframes loaded");
        perfMaybeQuit();
    });
    connect(m_keyframeLoader, &KeyframeLoader::failed, this, [this](const QString &reason) {
        qWarning().noquote() << "keyframe scan:" << reason;
        m_keyframeStatus = tr("Keyframes: unavailable");
        refreshStatus();
    });
    connect(m_player->videoSink(), &QVideoSink::videoFrameChanged, m_video, &VideoSurface::setFrame);
    connect(m_player->videoSink(), &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (m_perfFirstFrame || !frame.isValid())
            return;
        m_perfFirstFrame = true;
        PerfLog::mark("first frame shown");
        perfMaybeQuit();
    });

    setMediaControlsEnabled(false);
    updateOpenLastAction();
}

MainWindow::~MainWindow()
{
    saveSessionNow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_exporting) {
        const bool yes = askYesNo(tr("Export in progress"),
                                  tr("An export is still running. Cancel it and quit? The partial file will be deleted."));
        if (!yes) {
            event->ignore();
            return;
        }
        m_coordinator->cancel();
    }
    saveSessionNow();
    QMainWindow::closeEvent(event);
}

QAction *MainWindow::makePlaceholderAction(const QString &text, const QKeySequence &key, const QString &id)
{
    auto *a = new QAction(text, this);
    a->setShortcut(key);
    connect(a, &QAction::triggered, this, [this, id] { reportAction(id); });
    addAction(a);
    return a;
}

QAction *MainWindow::makeAction(const QString &text, const QKeySequence &key, std::function<void()> handler)
{
    auto *a = new QAction(text, this);
    a->setShortcut(key);
    connect(a, &QAction::triggered, this, [handler = std::move(handler)] { handler(); });
    addAction(a);
    return a;
}

void MainWindow::addShortcutOnly(const QKeySequence &key, const QString &id)
{
    makePlaceholderAction(id, key, id);
}

void MainWindow::reportAction(const QString &id)
{
    qInfo().noquote() << "action:" << id;
    flashStatus(tr("Action: %1 (not implemented yet)").arg(id), 3000);
}

void MainWindow::createActions()
{
    m_openAction = new QAction(tr("&Open..."), this);
    m_openAction->setShortcut(QKeySequence::Open); // Ctrl+O
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);

    m_exportAction = makeAction(tr("&Export"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { startExport(); });
    m_quitAction = new QAction(tr("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit); // Ctrl+Q
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_openLastAction = new QAction(tr("Open &Last File"), this);
    connect(m_openLastAction, &QAction::triggered, this, [this] { openPath(m_session->lastFile()); });

    m_setInAction = makeAction(tr("Set &IN"), QKeySequence(Qt::Key_I), [this] { handleSetIn(m_player->position()); });
    m_setOutAction = makeAction(tr("Set &OUT"), QKeySequence(Qt::Key_O), [this] { handleSetOut(m_player->position()); });
    // Clear has no shortcut by design (accidental-loss protection).
    m_clearAction = new QAction(tr("&Clear IN/OUT"), this);
    connect(m_clearAction, &QAction::triggered, this, [this] {
        m_markers->clearRange();
        flashStatus(tr("IN/OUT cleared (whole file)"), 3000);
    });

    m_playAction = makeAction(tr("&Play / Pause"), QKeySequence(Qt::Key_Space), [this] { m_player->togglePlay(); });

    m_shortcutsAction = new QAction(tr("&Shortcuts"), this);
    connect(m_shortcutsAction, &QAction::triggered, this, &MainWindow::showShortcuts);
    m_aboutAction = new QAction(tr("&About TrimFast"), this);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    // Navigation shortcuts (no menu entries).
    makeAction(QString(), QKeySequence(Qt::ALT | Qt::Key_I), [this] {
        if (m_markers->hasMedia())
            m_player->seek(m_markers->in());
    });
    makeAction(QString(), QKeySequence(Qt::ALT | Qt::Key_O), [this] {
        if (m_markers->hasMedia())
            m_player->seek(m_markers->out());
    });
    makeAction(QString(), QKeySequence(Qt::Key_Left), [this] { m_player->stepFrames(-1); });
    makeAction(QString(), QKeySequence(Qt::Key_Right), [this] { m_player->stepFrames(1); });
    makeAction(QString(), QKeySequence(Qt::SHIFT | Qt::Key_Left), [this] { m_player->seekRelative(-1000); });
    makeAction(QString(), QKeySequence(Qt::SHIFT | Qt::Key_Right), [this] { m_player->seekRelative(1000); });
    makeAction(QString(), QKeySequence(Qt::CTRL | Qt::Key_Left), [this] { gotoPreviousKeyframe(); });
    makeAction(QString(), QKeySequence(Qt::CTRL | Qt::Key_Right), [this] { gotoNextKeyframe(); });
    makeAction(QString(), QKeySequence(Qt::Key_Home), [this] { m_player->goToStart(); });
    makeAction(QString(), QKeySequence(Qt::Key_End), [this] { m_player->goToEnd(); });
    addShortcutOnly(QKeySequence(Qt::Key_J), QStringLiteral("Reverse play"));
    makeAction(QString(), QKeySequence(Qt::Key_K), [this] { m_player->pause(); });
    makeAction(QString(), QKeySequence(Qt::Key_L), [this] { m_player->play(); });
    makeAction(QString(), QKeySequence(Qt::Key_Escape), [this] { cancelExport(); });
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_openAction);
    file->addAction(m_openLastAction);
    file->addAction(m_exportAction);
    file->addSeparator();
    file->addAction(m_quitAction);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_setInAction);
    edit->addAction(m_setOutAction);
    edit->addSeparator();
    edit->addAction(m_clearAction);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_shortcutsAction);
    help->addAction(m_aboutAction);

    addAction(m_openAction);
    addAction(m_quitAction);
}

void MainWindow::createCentral()
{
    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Preview + position overlay row.
    m_video = new VideoSurface;
    root->addWidget(m_video, 1);

    m_positionLabel = new QLabel(TimeFormat::format(0));
    m_positionLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_positionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_positionLabel->setToolTip(tr("Current position"));
    root->addWidget(m_positionLabel);

    // Timeline.
    root->addWidget(new QLabel(tr("Timeline")));
    m_timeline = new TimelineWidget;
    root->addWidget(m_timeline);

    // IN / OUT readout.
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    auto makeEdit = [&mono] {
        auto *e = new QLineEdit(TimeFormat::format(0));
        e->setReadOnly(true);
        e->setFont(mono);
        e->setFixedWidth(180);
        e->setAlignment(Qt::AlignCenter);
        return e;
    };
    auto *inLabel = new QLabel(tr("IN :"));
    auto *outLabel = new QLabel(tr("OUT :"));
    for (QLabel *l : {inLabel, outLabel}) {
        QFont f = mono;
        f.setBold(true);
        l->setFont(f);
    }
    m_inEdit = makeEdit();
    m_outEdit = makeEdit();
    m_durationLabel = new QLabel(tr("Duration : %1").arg(TimeFormat::format(0)));
    m_modeLabel = new QLabel(tr("Mode : Stream Copy (lossless)"));
    m_durationLabel->setFont(mono);
    m_modeLabel->setFont(mono);
    grid->addWidget(inLabel, 0, 0);
    grid->addWidget(m_inEdit, 0, 1);
    grid->addWidget(m_durationLabel, 0, 2);
    grid->addWidget(outLabel, 1, 0);
    grid->addWidget(m_outEdit, 1, 1);
    grid->addWidget(m_modeLabel, 1, 2);
    grid->setColumnStretch(3, 1);
    root->addLayout(grid);

    // Buttons. Keys are shown on the button; the actions own the shortcuts.
    auto *buttons = new QHBoxLayout;
    auto makeButton = [this](const QString &text, QAction *action) {
        auto *b = new QPushButton(text);
        b->setMinimumWidth(130);
        connect(b, &QPushButton::clicked, action, &QAction::trigger);
        return b;
    };
    m_playButton = makeButton(tr("Play (Space)"), m_playAction);
    m_setInButton = makeButton(tr("Set IN (I)"), m_setInAction);
    m_setOutButton = makeButton(tr("Set OUT (O)"), m_setOutAction);
    m_exportButton = makeButton(tr("Export (Ctrl+E)"), m_exportAction);
    m_exportButton->setDefault(true);
    buttons->addWidget(m_playButton);
    buttons->addWidget(m_setInButton);
    buttons->addWidget(m_setOutButton);
    buttons->addWidget(m_exportButton);
    buttons->addStretch(1);
    root->addLayout(buttons);

    setCentralWidget(central);

    setTabOrder(m_playButton, m_setInButton);
    setTabOrder(m_setInButton, m_setOutButton);
    setTabOrder(m_setOutButton, m_exportButton);
}

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Video"), QString(),
        tr("Video files (*.mp4 *.mov *.mkv *.insv *.lrv *.360 *.m4v *.avi *.webm);;All files (*)"));
    if (!path.isEmpty())
        openPath(path);
}

void MainWindow::openPath(const QString &path)
{
    if (m_exporting) {
        flashStatus(tr("Finish or cancel (Esc) the export first"), 4000);
        return;
    }
    saveSessionNow(); // keep the range of the file we are leaving
    m_sessionActive = false;
    m_perfFirstFrame = false;
    m_perfKeyframes = false;
    setWindowTitle(QStringLiteral("TrimFast")); // nothing is loaded until the probe succeeds
    setMediaControlsEnabled(false);
    m_markers->reset(0);
    m_player->close();
    m_keyframeLoader->cancel();
    resetPair();
    m_video->clearFrame();
    m_timeline->clear();
    m_info = MediaInfo();
    m_keyframes = KeyframeIndex();
    m_keyframeStatus.clear();
    m_baseStatus = tr("Opening %1 ...").arg(QDir::toNativeSeparators(path));
    refreshStatus();
    m_probe->probe(path);
}

void MainWindow::perfMaybeQuit()
{
    if (PerfLog::quitWhenReady() && m_perfFirstFrame && m_perfKeyframes)
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void MainWindow::onProbed(const MediaInfo &info)
{
    PerfLog::mark("ffprobe finished");
    m_info = info;
    const QFileInfo fi(info.path);
    setWindowTitle(tr("TrimFast - %1").arg(fi.fileName()));

    const StreamInfo *v = info.videoStream();
    const StreamInfo *a = info.audioStream();
    QString summary = tr("%1 %2x%3 %4 fps").arg(v->codec.toUpper()).arg(v->width).arg(v->height)
                          .arg(QLocale().toString(v->fps, 'f', v->fps == int(v->fps) ? 0 : 2));
    if (a)
        summary += QStringLiteral(" / ") + a->codec.toUpper();

    m_baseStatus = tr("%1  |  %2  |  %3  |  %4")
                       .arg(QDir::toNativeSeparators(fi.absoluteFilePath()), summary,
                            QLocale().formattedDataSize(info.sizeBytes), TimeFormat::format(info.durationMs));
    m_keyframeStatus = tr("Keyframes: scanning...");
    refreshStatus();
    updateOutputPreview();
    m_video->setCaption(tr("Video Preview Area"), summary);
    onPositionChanged(0);
    m_timeline->setDuration(info.durationMs);

    // New range: the whole file, or the one saved for this file last time.
    m_markers->reset(info.durationMs);
    if (const auto saved = m_session->find(info.path, info.durationMs)) {
        if (m_markers->setRange(saved->inMs, saved->outMs).ok() && !m_markers->isFullRange())
            flashStatus(tr("Restored IN/OUT from the last session"), 4000);
    }
    m_session->setLastFile(info.path);
    updateOpenLastAction();
    m_sessionActive = true;
    setMediaControlsEnabled(true);

    m_player->open(info.path, v->fps);
    m_keyframeLoader->load(info.path);

    // Insta360: look for the other lens' file.
    const QString partner = InsvPairResolver::partnerPath(info.path);
    if (!partner.isEmpty()) {
        if (QFileInfo::exists(partner)) {
            m_pairStatus = tr("Pair: %1 ...").arg(QFileInfo(partner).fileName());
            m_pairProbe->probe(partner);
        } else {
            m_pairStatus = tr("Pair: %1 not found (single file)").arg(QFileInfo(partner).fileName());
        }
        refreshStatus();
    }
}

void MainWindow::resetPair()
{
    m_pairProbe->cancel();
    m_pairKeyframeLoader->cancel();
    m_pairInfo = MediaInfo();
    m_pairKeyframes = KeyframeIndex();
    m_pairStatus.clear();
}

void MainWindow::onPairProbed(const MediaInfo &partner)
{
    const QString name = QFileInfo(partner.path).fileName();
    if (!InsvPairResolver::isConsistent(m_info, partner)) {
        m_pairStatus = tr("Pair: %1 does not match (single file)").arg(name);
        refreshStatus();
        return;
    }
    m_pairInfo = partner;
    updateOutputPreview(); // the free name must be free for both lenses
    m_pairStatus = tr("Pair: %1 found (both are exported)").arg(name);
    m_pairKeyframeLoader->load(partner.path);
    refreshStatus();
}

void MainWindow::showNonModal(QMessageBox::Icon icon, const QString &title, const QString &text)
{
    // Not blocking: the event loop (and tests) keep running.
    auto *box = new QMessageBox(icon, title, text, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

bool MainWindow::askYesNo(const QString &title, const QString &text)
{
    if (m_questionHandler)
        return m_questionHandler(title, text);
    return QMessageBox::question(this, title, text) == QMessageBox::Yes;
}

void MainWindow::updateOutputPreview()
{
    if (!m_info.isValid()) {
        m_statusOut->clear();
        return;
    }
    const QString partner = m_pairInfo.isValid() ? m_pairInfo.path : QString();
    const QString name = OutputPath::suggestUnique(m_info.path, m_settings->outputSuffix(), partner);
    m_statusOut->setText(tr("Out: %1").arg(QFileInfo(name).fileName()));
}

QString MainWindow::askOutputPath(const QString &suggested)
{
    if (m_outputChooser)
        return m_outputChooser(suggested);
    const QString ext = QFileInfo(suggested).suffix();
    return QFileDialog::getSaveFileName(this, tr("Export As"), suggested,
                                        ext.isEmpty() ? QString() : tr("Video (*.%1)").arg(ext));
}

void MainWindow::startExport()
{
    if (m_exporting || !m_markers->hasMedia() || !m_info.isValid())
        return;

    const bool paired = m_pairInfo.isValid();
    const QString partnerInput = paired ? m_pairInfo.path : QString();
    const QString suffix = m_settings->outputSuffix();

    // 1. Where to? (the dialog confirms replacing an existing file)
    const QString suggested = OutputPath::suggestUnique(m_info.path, suffix, partnerInput);
    QString chosen = askOutputPath(suggested);
    if (chosen.isEmpty())
        return;
    // The container must stay the same as the input's: keep its extension.
    const QString inExt = QFileInfo(m_info.path).suffix();
    if (!inExt.isEmpty() && QFileInfo(chosen).suffix().compare(inExt, Qt::CaseInsensitive) != 0)
        chosen += QLatin1Char('.') + inExt;
    chosen = QFileInfo(chosen).absoluteFilePath();

    QList<ExportCoordinator::Item> items;
    auto makeItem = [&](const QString &input, const QString &output) {
        ExportCoordinator::Item it;
        it.job.inputPath = input;
        it.job.outputPath = output;
        it.job.format = OutputPath::forcedFormat(output);
        it.job.startMs = m_markers->in();
        it.job.durationMs = m_markers->length();
        it.job.overwrite = QFileInfo::exists(output); // consent was given in the dialog
        items << it;
    };
    makeItem(m_info.path, chosen);
    if (paired) {
        const QString partnerOut = InsvPairResolver::partnerOutputPath(chosen, m_info.path, partnerInput, suffix);
        if (QFileInfo(partnerOut).absoluteFilePath() == chosen) {
            showNonModal(QMessageBox::Warning, tr("Cannot export"),
                         tr("The two output files would have the same name. Choose a name containing the lens "
                            "number (e.g. _00_ / _10_) or use the suggested one."));
            return;
        }
        if (QFileInfo::exists(partnerOut)) {
            if (!askYesNo(tr("Replace file?"),
                          tr("%1 already exists. Replace it?").arg(QDir::toNativeSeparators(partnerOut))))
                return;
        }
        makeItem(partnerInput, partnerOut);
    }

    // The Insta360 apps pair the two lens files of a recording by their NAMES (VID_..._00_... and
    // VID_..._10_...; confirmed with the real app: names with a "_trim" suffix showed up as two
    // separate videos). A name chosen by hand may break that: say so before it happens.
    {
        bool broken = false;
        for (const ExportCoordinator::Item &it : std::as_const(items)) {
            if (InsvPairResolver::followsCameraNaming(it.job.inputPath)
                && !InsvPairResolver::followsCameraNaming(it.job.outputPath))
                broken = true;
        }
        if (broken) {
            const QString what = paired ? tr("the two lens files of one recording") : tr("a recording");
            if (!askYesNo(tr("File names"),
                          tr("The Insta360 apps recognise %1 by file names that keep the camera's pattern, "
                             "like %2 (and %3 for the other lens). The names you chose do not, so the apps may "
                             "show them as separate videos.\n\nExport with these names anyway?")
                              .arg(what, QFileInfo(suggested).fileName(),
                                   paired ? QFileInfo(InsvPairResolver::partnerOutputPath(suggested, m_info.path,
                                                                                         partnerInput, suffix)).fileName()
                                          : QStringLiteral("...")))) {
                return;
            }
        }
    }

    // 2. Pre-flight: free space and file-system limits.
    {
        qint64 largest = 0;
        qint64 total = 0;
        for (const ExportCoordinator::Item &it : std::as_const(items)) {
            const MediaInfo &src = it.job.inputPath == m_info.path ? m_info : m_pairInfo;
            qint64 extra = 0;
            if (const auto t = Insta360Trailer::detect(it.job.inputPath))
                extra = t->size;
            const qint64 est = OutputCheck::estimateBytes(src.sizeBytes, m_markers->length(), src.durationMs, extra);
            largest = qMax(largest, est);
            total += est;
        }
        QString fs;
        qint64 freeBytes = -1;
        const QStorageInfo storage(QFileInfo(chosen).absolutePath());
        if (storage.isValid() && storage.isReady()) {
            fs = QString::fromLatin1(storage.fileSystemType());
            freeBytes = OutputCheck::usableFreeBytes(storage.bytesAvailable(), storage.bytesTotal());
        }
        const OutputCheck::Result check = OutputCheck::evaluate(largest, total, fs, freeBytes);
        if (!check.ok()) {
            showNonModal(QMessageBox::Warning, tr("Cannot export here"), check.problems.join(QLatin1Char('\n')));
            return;
        }
        if (check.needsConfirmation()) {
            // The OS numbers are not always right: let the user decide.
            if (!askYesNo(tr("Free space"), tr("%1\n\nExport anyway?").arg(check.cautions.join(QLatin1Char('\n')))))
                return;
        }
    }

    // 3. Both lenses must start on the same moment: warn if the partner has no keyframe at IN.
    m_exportExtraWarnings.clear();
    if (paired && !m_pairKeyframes.isEmpty()) {
        const auto floor = m_pairKeyframes.floor(m_markers->in());
        if (!floor || *floor != m_markers->in()) {
            m_exportExtraWarnings << tr("%1 has no keyframe at IN: its start may differ slightly from the other "
                                        "lens.").arg(QFileInfo(partnerInput).fileName());
        }
    }

    setExporting(true);
    m_progress->setValue(0);
    flashStatus(tr("Exporting ... (Esc to cancel)"), 60000);
    m_coordinator->start(items);
}

void MainWindow::cancelExport()
{
    if (m_exporting)
        m_coordinator->cancel();
}

void MainWindow::setExporting(bool exporting)
{
    m_exporting = exporting;
    m_progress->setVisible(exporting);
    m_openAction->setEnabled(!exporting);
    m_openLastAction->setEnabled(!exporting);
    if (exporting) {
        setMediaControlsEnabled(false);
        m_playButton->setEnabled(true); // preview stays usable
    } else {
        setMediaControlsEnabled(m_markers->hasMedia());
        updateOpenLastAction();
    }
}

void MainWindow::onExportProgress(int percent, int fileIndex, int fileCount)
{
    m_progress->setValue(percent);
    const QString which = fileCount > 1 ? tr(" file %1/%2,").arg(fileIndex + 1).arg(fileCount) : QString();
    flashStatus(tr("Exporting%1 %2% (Esc to cancel)").arg(which).arg(percent), 60000);
}

void MainWindow::onExportFinished(const ExportCoordinator::Result &result)
{
    setExporting(false);
    switch (result.status) {
    case ExportCoordinator::Status::Success: {
        QStringList native;
        for (const QString &o : result.outputs)
            native << QDir::toNativeSeparators(o);
        QString text = tr("Exported: %1").arg(native.join(QStringLiteral(", ")));
        flashStatus(text, 20000);

        QStringList problems = m_exportExtraWarnings + result.warnings;
        if (!problems.isEmpty()) {
            showNonModal(QMessageBox::Warning, tr("Exported with warnings"),
                         tr("The file was exported, but:\n\n%1").arg(problems.join(QLatin1Char('\n'))));
        } else if (!result.notes.isEmpty()) {
            showNonModal(QMessageBox::Information, tr("Export complete"),
                         tr("%1\n\n%2").arg(result.message, result.notes.join(QLatin1Char('\n'))));
        }
        break;
    }
    case ExportCoordinator::Status::Cancelled:
        flashStatus(tr("Export cancelled"), 4000);
        break;
    case ExportCoordinator::Status::Failed:
        refreshStatus();
        showNonModal(QMessageBox::Critical, tr("Export failed"), result.message);
        break;
    }
}

void MainWindow::setMediaControlsEnabled(bool enabled)
{
    for (QAction *a : {m_setInAction, m_setOutAction, m_clearAction, m_exportAction})
        a->setEnabled(enabled);
    for (QPushButton *b : {m_playButton, m_setInButton, m_setOutButton, m_exportButton})
        b->setEnabled(enabled);
}

void MainWindow::updateOpenLastAction()
{
    const QString last = m_session->lastFile();
    m_openLastAction->setEnabled(!last.isEmpty() && QFileInfo::exists(last));
    if (!last.isEmpty())
        m_openLastAction->setToolTip(QDir::toNativeSeparators(last));
}

void MainWindow::onMarkersChanged()
{
    const bool has = m_markers->hasMedia();
    m_inEdit->setText(TimeFormat::format(has ? m_markers->in() : 0));
    m_outEdit->setText(TimeFormat::format(has ? m_markers->out() : 0));
    m_durationLabel->setText(tr("Duration : %1").arg(TimeFormat::format(has ? m_markers->length() : 0)));
    m_timeline->setRange(has ? m_markers->in() : 0, has ? m_markers->out() : 0);
    if (m_sessionActive && has)
        m_saveTimer->start(); // debounced: dragging emits many changes
}

void MainWindow::saveSessionNow()
{
    m_saveTimer->stop();
    if (!m_sessionActive || !m_markers->hasMedia() || !m_info.isValid())
        return;
    m_session->save({m_info.path, m_markers->in(), m_markers->out(), m_info.durationMs});
}

void MainWindow::handleSetIn(qint64 ms)
{
    reportMarkerResult(m_markers->setIn(ms), true);
}

void MainWindow::handleSetOut(qint64 ms)
{
    reportMarkerResult(m_markers->setOut(ms), false);
}

void MainWindow::reportMarkerResult(const MarkerManager::Result &r, bool isIn)
{
    const QString name = isIn ? tr("IN") : tr("OUT");
    switch (r.status) {
    case MarkerManager::Result::NoMedia:
        flashStatus(tr("Open a video first"), 3000);
        break;
    case MarkerManager::Result::InvalidRange:
        flashStatus(isIn ? tr("IN must be before OUT") : tr("OUT must be after IN"), 3000);
        break;
    case MarkerManager::Result::Ok:
        if (r.snapped()) {
            flashStatus(tr("IN moved to the previous keyframe %1 (requested %2)")
                                         .arg(TimeFormat::format(r.applied), TimeFormat::format(r.requested)),
                                     5000);
        } else {
            flashStatus(tr("%1 set to %2").arg(name, TimeFormat::format(r.applied)), 2000);
        }
        break;
    }
}

// Shows `text` in the status line for `ms`, then goes back to the file status.
void MainWindow::flashStatus(const QString &text, int ms)
{
    m_statusMain->setText(text);
    m_flashTimer->start(ms);
}

void MainWindow::refreshStatus()
{
    m_flashTimer->stop();
    QString text = m_baseStatus;
    if (!m_keyframeStatus.isEmpty())
        text += QStringLiteral("  |  ") + m_keyframeStatus;
    if (!m_pairStatus.isEmpty())
        text += QStringLiteral("  |  ") + m_pairStatus;
    m_statusMain->setText(text);
}

void MainWindow::gotoPreviousKeyframe()
{
    if (!m_player->hasMedia())
        return;
    if (m_keyframes.isEmpty()) {
        flashStatus(tr("Keyframes are not available yet"), 3000);
        return;
    }
    if (const auto t = m_keyframes.previous(m_player->position()))
        m_player->seek(*t);
}

void MainWindow::gotoNextKeyframe()
{
    if (!m_player->hasMedia())
        return;
    if (m_keyframes.isEmpty()) {
        flashStatus(tr("Keyframes are not available yet"), 3000);
        return;
    }
    if (const auto t = m_keyframes.next(m_player->position()))
        m_player->seek(*t);
}

void MainWindow::onProbeFailed(const QString &reason)
{
    m_baseStatus = tr("Error: %1").arg(reason);
    m_keyframeStatus.clear();
    refreshStatus();
    showNonModal(QMessageBox::Warning, tr("Cannot open file"), reason);
}

void MainWindow::onPlayingChanged(bool playing)
{
    m_playButton->setText(playing ? tr("Pause (Space)") : tr("Play (Space)"));
}

void MainWindow::onPositionChanged(qint64 ms)
{
    m_positionLabel->setText(TimeFormat::format(ms));
    m_timeline->setPosition(ms);
}

void MainWindow::showError(const QString &message)
{
    qWarning().noquote() << "player error:" << message;
    flashStatus(tr("Playback error: %1").arg(message), 8000);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() && event->mimeData()->urls().first().isLocalFile())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (m_exporting)
        return;
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile())
        openPath(urls.first().toLocalFile());
}

void MainWindow::showShortcuts()
{
    QMessageBox::information(
        this, tr("Shortcuts"),
        tr("Ctrl+O\tOpen\n"
           "Space\tPlay / Pause\n"
           "I / O\tSet IN / OUT\n"
           "Alt+I / Alt+O\tJump to IN / OUT\n"
           "Left / Right\t1 frame back / forward\n"
           "Shift+Left / Right\t1 second back / forward\n"
           "Ctrl+Left / Right\tPrevious / next keyframe\n"
           "Home / End\tStart / End\n"
           "J / K / L\tReverse / Stop / Forward\n"
           "Ctrl+E\tExport\n"
           "Esc\tCancel export\n"
           "Ctrl+Q\tQuit"));
}

void MainWindow::showAbout()
{
    const QString home = QStringLiteral(TRIMFAST_HOMEPAGE);
    QMessageBox::about(this, tr("About TrimFast"),
                       tr("<p><b>TrimFast %1</b><br>Lossless video trimmer (FFmpeg stream copy).</p>"
                          "<p><a href=\"%2\">%2</a><br>MIT License</p>")
                           .arg(QApplication::applicationVersion(), home));
}
