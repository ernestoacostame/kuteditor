#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QDebug>
#include <QFile>
#include <QTimer>
#include <QFontDatabase>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#endif

#include "audio/AudioEngine.h"
#include "audio/JackManager.h"
#include "ui/TrackModel.h"
#include "ui/UndoManager.h"
#include "ui/WaveformItem.h"  // FORCE REBUILD v2 — geometryChange + textureSize + Image render target
#include "io/ExportManager.h"
#include "io/ProjectIO.h"
#include "ui/ChapterModel.h"
#include "io/FileHelper.h"
#include "transcription/TranscriptionManager.h"
#include "net/KutPodClient.h"

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
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);

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
    auto *jackManager    = new JackManager(&app);
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

    audioEngine->setTrackModel(trackModel);
    jackManager->setAudioEngine(audioEngine);
    jackManager->setTrackModel(trackModel);
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

    if (!jackManager->start()) {
        qWarning() << "[main] JACK no disponible. La app arranca pero Record/Play no harán nada.";
    }

    bool hasGlobalMenu = false;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    bool dbusMenuProxyEnabled = true;
    if (qEnvironmentVariableIsSet("UBUNTU_MENUPROXY")) {
        const QString proxy = QString::fromUtf8(qgetenv("UBUNTU_MENUPROXY"));
        if (proxy == "0" || proxy.compare("none", Qt::CaseInsensitive) == 0) {
            dbusMenuProxyEnabled = false;
        }
    }
    if (dbusMenuProxyEnabled && QDBusConnection::sessionBus().interface()) {
        hasGlobalMenu = QDBusConnection::sessionBus().interface()->isServiceRegistered("com.canonical.AppMenu.Registrar");
    }
#elif defined(Q_OS_MAC) || defined(Q_OS_OSX)
    hasGlobalMenu = true;
#endif

    qDebug() << "[main] Soporte para Menú Global detectado:" << hasGlobalMenu;

    qDebug() << "[main] Creando QQmlApplicationEngine...";
    QQmlApplicationEngine engine;
    qDebug() << "[main] QQmlApplicationEngine creada.";

    engine.rootContext()->setContextProperty("AudioEngine", audioEngine);
    engine.rootContext()->setContextProperty("TrackModel", trackModel);
    engine.rootContext()->setContextProperty("PipeWireManager", jackManager);
    engine.rootContext()->setContextProperty("PipeWireOutputs", jackManager->outputsModel());
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
    engine.rootContext()->setContextProperty("HasGlobalMenu", hasGlobalMenu);

    // Versión y features para "Acerca de"
    engine.rootContext()->setContextProperty("AppVersion", QStringLiteral(APP_VERSION));
    engine.rootContext()->setContextProperty("FeatureJack", bool(APP_FEATURE_JACK));
    engine.rootContext()->setContextProperty("FeatureSamplerate", bool(APP_FEATURE_SAMPLERATE));
    engine.rootContext()->setContextProperty("FeatureTagLib", bool(APP_FEATURE_TAGLIB));
    engine.rootContext()->setContextProperty("FeatureWhisper", bool(APP_FEATURE_WHISPER));
    engine.rootContext()->setContextProperty("FeatureDeepFilterNet", bool(APP_FEATURE_DEEPFILTERNET));

    const QUrl url(QStringLiteral("qrc:/src/ui/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    qDebug() << "[main] Cargando QML desde:" << url;
    engine.load(url);
    qDebug() << "[main] QML cargado.";

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
    jackManager->stopAll();
    return rc;
}

#include "main.moc"
