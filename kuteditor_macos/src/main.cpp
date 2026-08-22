#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QPalette>
#include <QColor>
#include <QIcon>
#include <QDebug>
#include <QFile>
#include <QTimer>
#include <QFontDatabase>

#include "audio/AudioEngine.h"
#include "audio/CoreAudioManager.h"
#include "ui/TrackModel.h"
#include "ui/UndoManager.h"
#include "ui/WaveformItem.h"  // FORCE REBUILD v2 — geometryChange + textureSize + Image render target
#include "io/ExportManager.h"
#include "io/ProjectIO.h"
#include "ui/ChapterModel.h"
#include "io/FileHelper.h"
#include "transcription/TranscriptionManager.h"
#include "net/KutPodClient.h"
#include "SparkleUpdater.h"


#ifndef HAVE_KUTPOD
#include <QVariantList>
#include <QVariantMap>
class DummyKutPod : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString baseUrl     READ baseUrl     CONSTANT)
    Q_PROPERTY(QString currentUser READ currentUser CONSTANT)
    Q_PROPERTY(bool    loggedIn    READ loggedIn    CONSTANT)
    Q_PROPERTY(bool    busy        READ busy        CONSTANT)
    Q_PROPERTY(QVariantList podcasts READ podcasts  CONSTANT)

public:
    explicit DummyKutPod(QObject *parent = nullptr) : QObject(parent) {}

    QString baseUrl()     const { return QString(); }
    QString currentUser() const { return QString(); }
    bool    loggedIn()    const { return false; }
    bool    busy()        const { return false; }
    QVariantList podcasts() const { return QVariantList(); }

    Q_INVOKABLE void login(const QString &, const QString &, const QString &) {}
    Q_INVOKABLE void logout() {}
    Q_INVOKABLE void restoreSession() {}
    Q_INVOKABLE void fetchPodcasts() {}
    Q_INVOKABLE QVariantList cachedPodcasts() const { return QVariantList(); }
    Q_INVOKABLE void fetchEpisodes(const QString &, int, int, const QString &) {}
    Q_INVOKABLE void publishEpisode(const QString &, const QVariantMap &) {}
    Q_INVOKABLE void updateEpisode(const QString &, const QString &, const QVariantMap &) {}
    Q_INVOKABLE QString lastError() const { return QString(); }

signals:
    void sessionChanged();
    void busyChanged();
    void podcastsChanged();
    void loginSuccess(QString user);
    void loginFailed(QString reason);
    void podcastsLoaded(QVariantList items);
    void podcastsFailed(QString reason);
    void episodesLoaded(QString podcastId, QVariantList items, int total, int page, int pages);
    void episodesFailed(QString podcastId, QString reason);
    void publishProgress(int percent);
    void publishFinished(bool success, QString episodeUrl, QString errorMessage);
    void episodeUpdateFinished(bool success, QString episodeUrl, QString errorMessage);
};
#endif

int main(int argc, char *argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    qputenv("QT_DONT_USE_NATIVE_DIALOGS", "1");
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QApplication app(argc, argv);

    app.setStyleSheet(
        "QPushButton { "
        "  color: #ecf0f1; "
        "  background-color: #34383d; "
        "  border: 1px solid #4f545c; "
        "  padding: 4px 12px; "
        "  border-radius: 4px; "
        "  min-height: 18px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #3e4248; "
        "  border-color: #e74c3c; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #2a2d31; "
        "} "
        "QPushButton:disabled { "
        "  color: #666; "
        "  background-color: #2a2d31; "
        "  border-color: #2a2d31; "
        "}"
        "QLineEdit { "
        "  color: #1a1d21; "
        "  background-color: #ffffff; "
        "}"
    );

    // Configurar paleta oscura global para todos los controles y ventanas (incluyendo subventanas de diálogo)
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(26, 29, 33));          // #1a1d21
    darkPalette.setColor(QPalette::WindowText, QColor(236, 240, 241));    // #ecf0f1
    darkPalette.setColor(QPalette::Base, QColor(42, 45, 49));            // #2a2d31
    darkPalette.setColor(QPalette::AlternateBase, QColor(52, 56, 61));   // #34383d
    darkPalette.setColor(QPalette::ToolTipBase, QColor(26, 29, 33));
    darkPalette.setColor(QPalette::ToolTipText, QColor(236, 240, 241));
    darkPalette.setColor(QPalette::Text, QColor(236, 240, 241));
    darkPalette.setColor(QPalette::Button, QColor(52, 56, 61));          // #34383d
    darkPalette.setColor(QPalette::ButtonText, QColor(236, 240, 241));
    darkPalette.setColor(QPalette::BrightText, Qt::white);
    darkPalette.setColor(QPalette::Link, QColor(231, 76, 60));           // #e74c3c
    darkPalette.setColor(QPalette::Highlight, QColor(231, 76, 60));      // #e74c3c
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    darkPalette.setColor(QPalette::Midlight, QColor(60, 65, 70));        // #3c4146
    darkPalette.setColor(QPalette::Mid, QColor(31, 34, 38));             // #1f2226
    darkPalette.setColor(QPalette::Dark, QColor(17, 20, 24));            // #111418
    QGuiApplication::setPalette(darkPalette);

    QCoreApplication::setOrganizationName("Kut");
    QCoreApplication::setApplicationName("KutEditor");
    QGuiApplication::setDesktopFileName("kuteditor");

    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/kuteditor.svg")));

    // Registrar fuente Material Symbols para iconos en QML
    int fontId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/src/ui/fonts/MaterialSymbolsOutlined-ExtraLight.ttf"));
    if (fontId < 0)
        qWarning() << "[main] No se pudo cargar la fuente Material Symbols";

    // Registrar WaveformItem como tipo QML nativo.
    // Uso: import KutComponents 1.0; WaveformItem { peaks: ...; gain: ... }
    qmlRegisterType<WaveformItem>("KutComponents", 1, 0, "WaveformItem");

    // Registrar Icons singleton (Material Symbols unicodes)
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/src/ui/Icons.qml")),
                             "KutIcons", 1, 0, "Icons");

    auto *trackModel     = new TrackModel(&app);
    auto *audioEngine    = new AudioEngine(&app);
    auto *coreAudioManager = new CoreAudioManager(&app);
    auto *exportManager  = new ExportManager(&app);
    auto *undoManager    = new UndoManager(&app);
    auto *projectIO      = new ProjectIO(&app);
    auto *fileHelper     = new FileHelper(&app);
    auto *transcription  = new TranscriptionManager(&app);
    auto *chapterModel   = new ChapterModel(&app);
#ifdef HAVE_KUTPOD
    auto *kutpodClient   = new KutPodClient(&app);
    kutpodClient->restoreSession();
#else
    auto *kutpodClient   = new DummyKutPod(&app);
#endif
    auto *sparkleUpdater = new SparkleUpdater(&app);

    audioEngine->setTrackModel(trackModel);
    coreAudioManager->setAudioEngine(audioEngine);
    coreAudioManager->setTrackModel(trackModel);
    exportManager->setTrackModel(trackModel);
    exportManager->setAudioEngine(audioEngine);
    exportManager->setChapterModel(chapterModel);
    undoManager->setTrackModel(trackModel);
    undoManager->setChapterModel(chapterModel);
    projectIO->setTrackModel(trackModel);
    projectIO->setTranscriptionManager(transcription);
    projectIO->setChapterModel(chapterModel);
    transcription->setTrackModel(trackModel);
    chapterModel->setTrackModel(trackModel);

    QObject::connect(trackModel, &TrackModel::countChanged,
                     projectIO, &ProjectIO::markDirty);
    QObject::connect(trackModel, &TrackModel::clipsChanged,
                     projectIO, [projectIO](int){ projectIO->markDirty(); });
    QObject::connect(trackModel, &TrackModel::dataChanged,
                     projectIO, [projectIO](const QModelIndex&, const QModelIndex&,
                                            const QVector<int> &roles) {
        Q_UNUSED(roles);
        projectIO->markDirty();
    });
    QObject::connect(trackModel, &TrackModel::trackDeviceChanged,
                     projectIO, [projectIO](int, const QString&){ projectIO->markDirty(); });
    QObject::connect(chapterModel, &ChapterModel::countChanged,
                     projectIO, &ProjectIO::markDirty);
    QObject::connect(chapterModel, &ChapterModel::dataChanged,
                     projectIO, [projectIO](const QModelIndex&, const QModelIndex&,
                                            const QVector<int>&) { projectIO->markDirty(); });
    QObject::connect(transcription, &TranscriptionManager::transcriptionFinished,
                     projectIO, &ProjectIO::markDirty);
    QObject::connect(transcription, &TranscriptionManager::transcriptionStateChanged,
                     projectIO, &ProjectIO::markDirty);

    if (!coreAudioManager->start()) {
        qWarning() << "[main] CoreAudio no disponible. La app arranca pero Record/Play no harán nada.";
    }

    QQmlApplicationEngine engine;



    engine.rootContext()->setContextProperty("AudioEngine", audioEngine);
    engine.rootContext()->setContextProperty("TrackModel", trackModel);
    engine.rootContext()->setContextProperty("PipeWireManager", coreAudioManager);
    engine.rootContext()->setContextProperty("PipeWireOutputs", coreAudioManager->outputsModel());
    engine.rootContext()->setContextProperty("ExportManager", exportManager);
    engine.rootContext()->setContextProperty("UndoManager", undoManager);
    engine.rootContext()->setContextProperty("ProjectIO", projectIO);
    engine.rootContext()->setContextProperty("FileHelper", fileHelper);
    engine.rootContext()->setContextProperty("Transcription", transcription);
    engine.rootContext()->setContextProperty("ChapterModel", chapterModel);
    engine.rootContext()->setContextProperty("KutPod", kutpodClient);
#ifdef HAVE_KUTPOD
    engine.rootContext()->setContextProperty("FeatureKutPod", true);
#else
    engine.rootContext()->setContextProperty("FeatureKutPod", false);
#endif
    engine.rootContext()->setContextProperty("SparkleUpdater", sparkleUpdater);


    // Versión y features para "Acerca de"
    engine.rootContext()->setContextProperty("AppVersion", QStringLiteral(APP_VERSION));
    engine.rootContext()->setContextProperty("FeatureJack", bool(APP_FEATURE_AUDIO));
    engine.rootContext()->setContextProperty("FeatureSamplerate", bool(APP_FEATURE_SAMPLERATE));
    engine.rootContext()->setContextProperty("FeatureTagLib", bool(APP_FEATURE_TAGLIB));
    engine.rootContext()->setContextProperty("FeatureWhisper", bool(APP_FEATURE_WHISPER));
    engine.rootContext()->setContextProperty("FeatureDeepFilterNet", bool(APP_FEATURE_DEEPFILTERNET));

    const QUrl url(QStringLiteral("qrc:/src/ui/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);

    // Si se pasó un archivo como argumento (doble clic en .kut), cargarlo.
    const QStringList args = app.arguments();
    if (args.size() > 1) {
        const QString filePath = args.last();
        if (QFile::exists(filePath)) {
            // Cargar después de que la UI esté lista.
            QTimer::singleShot(200, [projectIO, filePath]() {
                projectIO->open(filePath);
            });
        }
    }

    const int rc = app.exec();
    coreAudioManager->stopAll();
    return rc;
}

#include "main.moc"
